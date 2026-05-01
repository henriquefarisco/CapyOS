/*
 * src/apps/browser_chrome/runtime.c (F3.3d)
 *
 * Orquestra envio de requests e poll de eventos sobre os pipes
 * conectados ao engine ring 3. Pipe ops sao injetados via
 * `chrome_runtime_set_pipe_ops()` para permitir teste host sem
 * o kernel real.
 *
 * Contrato em include/apps/browser_chrome_runtime.h.
 */

#include "apps/browser_chrome_runtime.h"

static chrome_runtime_pipe_write_fn g_write_fn = (chrome_runtime_pipe_write_fn)0;
static chrome_runtime_pipe_read_fn  g_read_fn  = (chrome_runtime_pipe_read_fn)0;

void chrome_runtime_set_pipe_ops(chrome_runtime_pipe_write_fn w,
                                 chrome_runtime_pipe_read_fn r) {
    g_write_fn = w;
    g_read_fn = r;
}

void chrome_runtime_init(struct chrome_runtime *rt,
                         int request_pipe_id,
                         int response_pipe_id,
                         uint32_t engine_pid,
                         uint64_t now_ticks) {
    if (!rt) return;
    browser_chrome_init(&rt->chrome, now_ticks);
    rt->request_pipe_id = request_pipe_id;
    rt->response_pipe_id = response_pipe_id;
    rt->engine_pid = engine_pid;
    rt->engine_alive = (engine_pid != 0u) ? 1 : 0;
    rt->total_requests_sent = 0u;
    rt->total_events_polled = 0u;
    rt->total_pongs_received = 0u;
    rt->total_engine_eofs = 0u;
    for (size_t i = 0; i < CHROME_RUNTIME_EVENT_BUF_MAX; ++i) {
        rt->event_scratch[i] = 0u;
    }
}

/* Escreve buffer inteiro no request pipe. Retorna 0 ok, -1 erro. */
static int write_full(int pipe_id, const uint8_t *buf, uint32_t n) {
    if (!g_write_fn) return -1;
    uint32_t sent = 0u;
    while (sent < n) {
        int wr = g_write_fn(pipe_id, buf + sent, (size_t)(n - sent));
        if (wr <= 0) {
            return -1;
        }
        sent += (uint32_t)wr;
    }
    return 0;
}

/* Le N bytes exatos do response pipe. Retorna:
 *   1 sucesso
 *   0 EOF (engine fechou)
 *  -1 would-block
 *  -2 erro
 */
static int read_full(int pipe_id, uint8_t *buf, uint32_t n) {
    if (!g_read_fn) return -2;
    uint32_t got = 0u;
    while (got < n) {
        int rd = g_read_fn(pipe_id, buf + got, (size_t)(n - got));
        if (rd > 0) {
            got += (uint32_t)rd;
            continue;
        }
        if (rd == 0) {
            /* EOF observado; mas se ja lemos algo, perdemos o frame. */
            return (got == 0u) ? 0 : -2;
        }
        /* rd < 0: would-block. Se ainda nao temos nada, e ok devolver
         * NO_DATA. Se ja temos parte do frame, e protocolo quebrado
         * porque o engine deve escrever frames inteiros. */
        if (got == 0u) return -1;
        return -2;
    }
    return 1;
}

static void be_put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)((v >> 24) & 0xFFu);
    p[1] = (uint8_t)((v >> 16) & 0xFFu);
    p[2] = (uint8_t)((v >> 8) & 0xFFu);
    p[3] = (uint8_t)(v & 0xFFu);
}

/* Encode + envia um frame com header + payload opcional. */
static int send_frame(struct chrome_runtime *rt,
                      uint16_t kind,
                      const uint8_t *payload,
                      uint32_t plen) {
    if (!rt) return -1;
    if (!rt->engine_alive) return -1;
    uint32_t seq = browser_chrome_alloc_request_seq(&rt->chrome);
    struct browser_ipc_header hdr = {
        .magic = BROWSER_IPC_MAGIC,
        .kind = kind,
        .seq = seq,
        .payload_len = plen
    };
    uint8_t hbuf[BROWSER_IPC_HEADER_SIZE];
    if (browser_ipc_header_encode(&hdr, hbuf, sizeof(hbuf)) != BROWSER_IPC_OK) {
        return -1;
    }
    if (write_full(rt->request_pipe_id, hbuf, sizeof(hbuf)) != 0) {
        return -1;
    }
    if (plen > 0u) {
        if (!payload) return -1;
        if (write_full(rt->request_pipe_id, payload, plen) != 0) {
            return -1;
        }
    }
    rt->total_requests_sent++;
    return 0;
}

int chrome_runtime_send_navigate(struct chrome_runtime *rt,
                                 const char *url,
                                 size_t url_len) {
    if (!rt || !url) return -1;
    uint8_t pl[BROWSER_CHROME_URL_MAX + 2];
    uint32_t n = browser_chrome_build_navigate_payload(url, url_len,
                                                        pl, sizeof(pl));
    if (n == 0u) return -1;
    if (send_frame(rt, BROWSER_IPC_NAVIGATE, pl, n) != 0) return -1;
    browser_chrome_record_navigate_sent(&rt->chrome, url, url_len);
    return 0;
}

int chrome_runtime_send_cancel(struct chrome_runtime *rt) {
    return send_frame(rt, BROWSER_IPC_CANCEL, (const uint8_t *)0, 0u);
}

int chrome_runtime_send_shutdown(struct chrome_runtime *rt) {
    return send_frame(rt, BROWSER_IPC_SHUTDOWN, (const uint8_t *)0, 0u);
}

int chrome_runtime_send_ping(struct chrome_runtime *rt, uint64_t now_ticks) {
    if (!rt) return -1;
    uint32_t nonce = browser_watchdog_alloc_nonce(&rt->chrome.watchdog);
    uint8_t pl[4];
    be_put_u32(pl, nonce);
    if (send_frame(rt, BROWSER_IPC_PING, pl, sizeof(pl)) != 0) return -1;
    browser_watchdog_record_ping(&rt->chrome.watchdog, nonce, now_ticks);
    return 0;
}

int chrome_runtime_poll_event(struct chrome_runtime *rt,
                              uint64_t now_ticks,
                              uint32_t *out_actions) {
    if (out_actions) *out_actions = 0u;
    if (!rt) return CHROME_RUNTIME_POLL_PROTOCOL_ERR;

    /* Le header (12 bytes). Se vazio, NO_DATA. */
    uint8_t hbuf[BROWSER_IPC_HEADER_SIZE];
    int rh = read_full(rt->response_pipe_id, hbuf, sizeof(hbuf));
    if (rh == -1) return CHROME_RUNTIME_POLL_NO_DATA;
    if (rh == 0) {
        rt->engine_alive = 0;
        rt->total_engine_eofs++;
        return CHROME_RUNTIME_POLL_ENGINE_EOF;
    }
    if (rh != 1) return CHROME_RUNTIME_POLL_PROTOCOL_ERR;

    /* Decode + valida. */
    struct browser_ipc_header hdr;
    int rc = browser_ipc_header_decode(hbuf, sizeof(hbuf), &hdr);
    if (rc != BROWSER_IPC_OK) {
        return CHROME_RUNTIME_POLL_PROTOCOL_ERR;
    }
    if (hdr.payload_len > (CHROME_RUNTIME_EVENT_BUF_MAX
                           - BROWSER_IPC_HEADER_SIZE)) {
        /* Payload nao cabe no scratch; protocolo violado. */
        return CHROME_RUNTIME_POLL_PROTOCOL_ERR;
    }

    /* Le payload se houver. */
    uint8_t *plbuf = rt->event_scratch;
    if (hdr.payload_len > 0u) {
        int rp = read_full(rt->response_pipe_id, plbuf, hdr.payload_len);
        if (rp != 1) {
            return CHROME_RUNTIME_POLL_PROTOCOL_ERR;
        }
    }

    uint32_t actions = browser_chrome_dispatch_event(
        &rt->chrome, &hdr, hdr.payload_len > 0u ? plbuf : (const uint8_t *)0,
        now_ticks);
    rt->total_events_polled++;
    if (hdr.kind == BROWSER_IPC_EVENT_PONG) {
        rt->total_pongs_received++;
    }
    if (out_actions) *out_actions = actions;
    return CHROME_RUNTIME_POLL_EVENT_HANDLED;
}

int chrome_runtime_tick(struct chrome_runtime *rt, uint64_t now_ticks) {
    if (!rt) return 0;
    browser_watchdog_tick(&rt->chrome.watchdog, now_ticks);

    if (browser_watchdog_should_kill(&rt->chrome.watchdog)) {
        rt->engine_alive = 0;
        return 1;
    }
    if (rt->engine_alive
        && browser_watchdog_should_send_ping(&rt->chrome.watchdog, now_ticks)) {
        if (chrome_runtime_send_ping(rt, now_ticks) != 0) {
            rt->engine_alive = 0;
            return -1;
        }
    }
    return 0;
}

void chrome_runtime_record_restart(struct chrome_runtime *rt,
                                   int new_request_pipe_id,
                                   int new_response_pipe_id,
                                   uint32_t new_engine_pid,
                                   uint64_t now_ticks) {
    if (!rt) return;
    rt->request_pipe_id = new_request_pipe_id;
    rt->response_pipe_id = new_response_pipe_id;
    rt->engine_pid = new_engine_pid;
    rt->engine_alive = (new_engine_pid != 0u) ? 1 : 0;
    browser_watchdog_record_restart(&rt->chrome.watchdog, now_ticks);
    /* Estado de navegacao (current_url, current_title) e preservado
     * propositalmente: o usuario espera ver a mesma pagina apos
     * reconectar; o caller pode opcionalmente reenviar NAVIGATE
     * para forcar carregamento. Frame antigo pode estar congelado;
     * limpamos last_frame para evitar mostrar pixels obsoletos como
     * se viessem do novo engine. */
    rt->chrome.last_frame.nav_id = 0u;
    rt->chrome.last_frame.width = 0u;
    rt->chrome.last_frame.height = 0u;
    rt->chrome.last_frame.stride = 0u;
    rt->chrome.last_frame.pixels = (const uint8_t *)0;
    rt->chrome.last_frame.pixel_bytes = 0u;
}
