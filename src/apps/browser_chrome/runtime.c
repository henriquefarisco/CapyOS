/*
 * src/apps/browser_chrome/runtime.c (F3.3d)
 *
 * Orquestra envio de requests e poll de eventos sobre os pipes
 * conectados ao engine ring 3. Pipe ops sao injetados via
 * `chrome_runtime_set_pipe_ops()` para permitir teste host sem
 * o kernel real.
 *
 * Contrato em include/apps/browser_chrome_runtime.h.
 *
 * 2026-05-02 (F3.3g): quando a URL pedida pelo engine comeca com
 * `http://` ou `https://`, o dispatcher rota a request pelo stack
 * TCP/TLS real do kernel (`net/http.h`) em vez de cair no resolver
 * embutido (que servia apenas paginas `file://capyos/xyz`). Sem essa
 * rota o navegador respondia 404 para qualquer link externo; com
 * ela, qualquer URL HTTP(S) valida alimenta o parser/render com o
 * body recebido pela rede. O caminho HTTP so existe no build de
 * kernel (guarded por `UNIT_TEST`) para preservar a portabilidade
 * host dos testes unitarios do resolver.
 */

#include "apps/browser_chrome_runtime.h"
#include "apps/browser_chrome_fetch_resolver.h"

#ifndef UNIT_TEST
#include "net/http.h"
#include "net/stack.h"
#endif

static chrome_runtime_pipe_write_fn g_write_fn = (chrome_runtime_pipe_write_fn)0;
static chrome_runtime_pipe_read_fn  g_read_fn  = (chrome_runtime_pipe_read_fn)0;
/* F3.3f: quando o kernel usa a runtime com pipes reais de
 * PIPE_BUF_SIZE=4 KiB, um EVENT_FRAME de 96 KiB nao cabe em
 * um unico write/read. O caller injeta `task_yield` aqui para
 * que `read_full` ceda a CPU quando o pipe drena no meio de
 * um payload, em vez de devolver protocol-error. NULL mantem
 * o comportamento antigo (fail-fast para host tests). */
static chrome_runtime_yield_fn      g_yield_fn = (chrome_runtime_yield_fn)0;

/* Etapa 5 hardening (2026-05-03): URL whitelist callback. NULL = allow
 * all (default). Veja chrome_runtime_set_url_policy. */
static chrome_runtime_url_policy_fn g_url_policy = (chrome_runtime_url_policy_fn)0;

/* Etapa 5 hardening (2026-05-05): limite de budget mutavel por
 * teste. Producao nunca chama o setter e o valor permanece no
 * default CHROME_RUNTIME_NAV_BUDGET_BYTES_MAX. */
static uint64_t g_nav_budget_max =
    (uint64_t)CHROME_RUNTIME_NAV_BUDGET_BYTES_MAX;

void chrome_runtime_set_pipe_ops(chrome_runtime_pipe_write_fn w,
                                 chrome_runtime_pipe_read_fn r) {
    g_write_fn = w;
    g_read_fn = r;
}

void chrome_runtime_set_yield_op(chrome_runtime_yield_fn y) {
    g_yield_fn = y;
}

void chrome_runtime_set_url_policy(chrome_runtime_url_policy_fn fn) {
    g_url_policy = fn;
}

void chrome_runtime_set_nav_budget_for_test(uint64_t bytes) {
    g_nav_budget_max = (bytes != 0u)
        ? bytes : (uint64_t)CHROME_RUNTIME_NAV_BUDGET_BYTES_MAX;
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
    rt->total_frames_persisted = 0u;
    rt->total_frames_dropped = 0u;
    /* Etapa 5 hardening (2026-05-03): zera rate limiter + url policy
     * + observability bytes + audit ring. */
    rt->incoming_in_window = 0u;
    rt->total_incoming_drops = 0u;
    rt->total_url_blocked = 0u;
    rt->total_event_bytes_received = 0u;
    /* Etapa 5 hardening (2026-05-05): zera nav budget. */
    rt->bytes_in_current_nav = 0u;
    rt->total_nav_budget_kills = 0u;
    capyc_audit_init(&rt->audit);
    for (size_t i = 0; i < CHROME_RUNTIME_EVENT_BUF_MAX; ++i) {
        rt->event_scratch[i] = 0u;
    }
    /* `last_frame_storage` ficaria com lixo do BSS antes do primeiro
     * FRAME; zera so a primeira pagina (~4 KiB) para que uma leitura
     * acidental antes do primeiro repaint nao exponha memoria de
     * outra alocacao. O resto e sobrescrito pelo memcpy. */
    for (size_t i = 0; i < 4096u && i < CHROME_RUNTIME_FRAME_STORAGE_MAX; ++i) {
        rt->last_frame_storage[i] = 0u;
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
 *
 * F3.3f: quando `allow_yield` != 0 e `g_yield_fn` foi injetado,
 * would-block mid-payload cede a CPU e retenta (em vez de virar
 * protocol-error). Isso e o que permite transportar EVENT_FRAME
 * de 96 KiB por um pipe de 4 KiB: o engine fica bloqueado em
 * sys_write->task_yield enquanto o chrome e escalonado; o yield
 * aqui devolve o CPU para o engine completar a escrita. Sem yield
 * (host tests) a semantica antiga e preservada. */
/* F3.3f triagem 2026-05-01: limite de yields consecutivos sem
 * progresso. Sem isso, se a engine morrer mid-payload (process_kill
 * ainda nao fechou o write-end do pipe), `pipe_read` continua
 * retornando -1 (would-block) e ficamos em yield infinito,
 * congelando o desktop (tela do wallpaper sem updates). 4096 yields
 * = ~muito tempo em qualquer scheduler razoavel; se chegamos aqui,
 * a engine claramente nao esta produzindo bytes e devolvemos -2
 * para o caller respawnar. */
#define CHROME_RUNTIME_READ_YIELD_LIMIT 4096u

static int read_full(int pipe_id, uint8_t *buf, uint32_t n,
                     int allow_yield) {
    if (!g_read_fn) return -2;
    uint32_t got = 0u;
    uint32_t yields_without_progress = 0u;
    while (got < n) {
        int rd = g_read_fn(pipe_id, buf + got, (size_t)(n - got));
        if (rd > 0) {
            got += (uint32_t)rd;
            yields_without_progress = 0u;
            continue;
        }
        if (rd == 0) {
            /* EOF observado; mas se ja lemos algo, perdemos o frame. */
            return (got == 0u) ? 0 : -2;
        }
        /* rd < 0: would-block. Se ainda nao temos nada, e ok devolver
         * NO_DATA. Se ja temos parte do frame e ha yield disponivel,
         * cede e retenta; senao, protocolo quebrado. */
        if (got == 0u) return -1;
        if (allow_yield && g_yield_fn) {
            if (yields_without_progress++ >= CHROME_RUNTIME_READ_YIELD_LIMIT) {
                return -2;
            }
            g_yield_fn();
            continue;
        }
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
int chrome_runtime_send_ipc_frame(struct chrome_runtime *rt,
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

/* Etapa 5 hardening (2026-05-05): zera o budget de bytes acumulados
 * para a navegacao em andamento. Chamado em qualquer ponto onde o
 * chrome inicia uma nova navegacao (NAVIGATE/BACK/FORWARD/RELOAD)
 * E quando o engine confirma a nav via EVENT_NAV_STARTED. Resetar
 * duas vezes (em ambos os pontos) e idempotente, e cobre o caso onde
 * a URL final apos redirect difere da pedida. */
static void chrome_runtime_reset_nav_budget(struct chrome_runtime *rt) {
    if (!rt) return;
    rt->bytes_in_current_nav = 0u;
}

int chrome_runtime_send_navigate(struct chrome_runtime *rt,
                                 const char *url,
                                 size_t url_len) {
    if (!rt || !url) return -1;
    /* Etapa 5 hardening (2026-05-03): policy check antes do encode +
     * write. Negacao incrementa contador, grava audit, e nao toca
     * o pipe. */
    if (g_url_policy && g_url_policy(url, (uint32_t)url_len) == 0) {
        rt->total_url_blocked++;
        capyc_audit_record(&rt->audit,
                           (uint8_t)CAPYC_AUDIT_POLICY_DENY,
                           (uint16_t)url_len);
        return -1;
    }
    uint8_t pl[BROWSER_CHROME_URL_MAX + 2];
    uint32_t n = browser_chrome_build_navigate_payload(url, url_len,
                                                        pl, sizeof(pl));
    if (n == 0u) return -1;
    if (chrome_runtime_send_ipc_frame(rt, BROWSER_IPC_NAVIGATE, pl, n) != 0) return -1;
    browser_chrome_record_navigate_sent(&rt->chrome, url, url_len);
    capyc_audit_record(&rt->audit, (uint8_t)CAPYC_AUDIT_NAV,
                       (uint16_t)url_len);
    /* Etapa 5 hardening (2026-05-05): inicio de nova nav -> zera o
     * budget. Eventos do engine relacionados a navs anteriores nao
     * sao "nossos" do ponto de vista do budget desta nav. */
    chrome_runtime_reset_nav_budget(rt);
    return 0;
}

int chrome_runtime_send_cancel(struct chrome_runtime *rt) {
    return chrome_runtime_send_ipc_frame(rt, BROWSER_IPC_CANCEL, (const uint8_t *)0, 0u);
}

int chrome_runtime_send_shutdown(struct chrome_runtime *rt) {
    return chrome_runtime_send_ipc_frame(rt, BROWSER_IPC_SHUTDOWN, (const uint8_t *)0, 0u);
}

int chrome_runtime_send_ping(struct chrome_runtime *rt, uint64_t now_ticks) {
    if (!rt) return -1;
    uint32_t nonce = browser_watchdog_alloc_nonce(&rt->chrome.watchdog);
    uint8_t pl[4];
    be_put_u32(pl, nonce);
    if (chrome_runtime_send_ipc_frame(rt, BROWSER_IPC_PING, pl, sizeof(pl)) != 0) return -1;
    browser_watchdog_record_ping(&rt->chrome.watchdog, nonce, now_ticks);
    return 0;
}

/* 2026-05-02: BROWSER_IPC_RESIZE. Payload BE = width:u16 + height:u16.
 * Engine ring 3 escuta o frame e ajusta seu viewport interno. Sem
 * essa rota, redimensionar a janela do browser nao adaptava o
 * conteudo (o engine continuava rasterizando 480x360). */
int chrome_runtime_send_resize(struct chrome_runtime *rt,
                               uint16_t width, uint16_t height) {
    if (!rt) return -1;
    if (width == 0u || height == 0u) return -1;
    uint8_t pl[4];
    pl[0] = (uint8_t)((width >> 8) & 0xFFu);
    pl[1] = (uint8_t)(width & 0xFFu);
    pl[2] = (uint8_t)((height >> 8) & 0xFFu);
    pl[3] = (uint8_t)(height & 0xFFu);
    if (chrome_runtime_send_ipc_frame(rt, BROWSER_IPC_RESIZE, pl, sizeof(pl)) != 0) return -1;
    return 0;
}

/* Etapa 3 seção b (2026-05-02): BROWSER_IPC_CLICK. Payload BE = x:u16
 * + y:u16 + button:u8. Engine faz hit-test contra o layout atual e
 * emite NAVIGATE se (x,y) estiver dentro de um elemento com `href`. */
int chrome_runtime_send_click(struct chrome_runtime *rt,
                              uint16_t x, uint16_t y, uint8_t button) {
    if (!rt) return -1;
    uint8_t pl[5];
    pl[0] = (uint8_t)((x >> 8) & 0xFFu);
    pl[1] = (uint8_t)(x & 0xFFu);
    pl[2] = (uint8_t)((y >> 8) & 0xFFu);
    pl[3] = (uint8_t)(y & 0xFFu);
    pl[4] = button;
    if (chrome_runtime_send_ipc_frame(rt, BROWSER_IPC_CLICK, pl, sizeof(pl)) != 0) return -1;
    return 0;
}

/* Etapa 3 seção e (2026-05-02): BROWSER_IPC_SCROLL. Payload BE =
 * delta_y:i32. Codificado como uint32 em BE; o decode engine-side
 * reinterpreta como int32 preservando o sinal. */
int chrome_runtime_send_scroll(struct chrome_runtime *rt,
                               int32_t delta_y) {
    if (!rt) return -1;
    uint32_t u = (uint32_t)delta_y;
    uint8_t pl[4];
    pl[0] = (uint8_t)((u >> 24) & 0xFFu);
    pl[1] = (uint8_t)((u >> 16) & 0xFFu);
    pl[2] = (uint8_t)((u >> 8) & 0xFFu);
    pl[3] = (uint8_t)(u & 0xFFu);
    if (chrome_runtime_send_ipc_frame(rt, BROWSER_IPC_SCROLL, pl, sizeof(pl)) != 0) return -1;
    return 0;
}

/* Etapa 3 seção c (2026-05-03): BROWSER_IPC_KEY. Payload BE = keycode:u32
 * + mods:u8. Engine roteia ao input focado (sem foco, no-op). */
int chrome_runtime_send_key(struct chrome_runtime *rt,
                            uint32_t keycode, uint8_t mods) {
    if (!rt) return -1;
    uint8_t pl[5];
    pl[0] = (uint8_t)((keycode >> 24) & 0xFFu);
    pl[1] = (uint8_t)((keycode >> 16) & 0xFFu);
    pl[2] = (uint8_t)((keycode >> 8) & 0xFFu);
    pl[3] = (uint8_t)(keycode & 0xFFu);
    pl[4] = mods;
    if (chrome_runtime_send_ipc_frame(rt, BROWSER_IPC_KEY, pl, sizeof(pl)) != 0) return -1;
    return 0;
}

/* Etapa 3 seção b (2026-05-02): BROWSER_IPC_RELOAD. Vazio; engine
 * mantem a ultima URL navegada em estado interno e re-emite a
 * sequencia completa de navegacao.
 *
 * Etapa 5 hardening refinement (2026-05-05): RELOAD inicia uma nav
 * fresca do ponto de vista do budget; zera bytes_in_current_nav. */
int chrome_runtime_send_reload(struct chrome_runtime *rt) {
    int rc = chrome_runtime_send_ipc_frame(rt, BROWSER_IPC_RELOAD, (const uint8_t *)0, 0u);
    if (rc == 0) chrome_runtime_reset_nav_budget(rt);
    return rc;
}

/* Etapa 3 seção b-polish (2026-05-03): BROWSER_IPC_BACK/FORWARD.
 * Payload vazio; engine guarda o ring de historico e decide a URL
 * destino. Mesmo contrato de send_reload: 0 ok / -1 em broken pipe
 * ou engine morto.
 *
 * Etapa 5 hardening refinement (2026-05-05): BACK/FORWARD tambem
 * iniciam navs frescas; zeram o budget. */
int chrome_runtime_send_back(struct chrome_runtime *rt) {
    int rc = chrome_runtime_send_ipc_frame(rt, BROWSER_IPC_BACK, (const uint8_t *)0, 0u);
    if (rc == 0) chrome_runtime_reset_nav_budget(rt);
    return rc;
}

int chrome_runtime_send_forward(struct chrome_runtime *rt) {
    int rc = chrome_runtime_send_ipc_frame(rt, BROWSER_IPC_FORWARD, (const uint8_t *)0, 0u);
    if (rc == 0) chrome_runtime_reset_nav_budget(rt);
    return rc;
}

/* Slice 5c / F3.3g: drain the pending fetch staged by the chrome
 * dispatcher (EVENT_FETCH_REQUEST handler), resolve it either via
 * the canned page table (for `file://capyos/xyz`) or via the real
 * HTTP/TLS client (for `http://` / `https://`), and write the
 * FETCH_RESPONSE frame back through the request pipe.
 *
 * The response payload is bounded by 16 + ctype_len + body_len.
 * Real HTTP bodies cap at HTTP_MAX_RESPONSE_SIZE (1 MiB) which is
 * also the BROWSER_IPC_MAX_PAYLOAD limit, so the encode buffer must
 * be at least that big. We keep it in file-scope `.bss` instead of
 * on the kernel stack because the kernel stack is bounded at a few
 * KiB in this codebase. The buffer is shared across calls because
 * dispatch_pending_fetch is always invoked synchronously by the
 * single browser_app tick path. */
#define CHROME_RUNTIME_FETCH_PAYLOAD_MAX (1u * 1024u * 1024u + 4096u)

static uint8_t g_fetch_response_scratch[CHROME_RUNTIME_FETCH_PAYLOAD_MAX];

/* Case-sensitive prefix check. Used on url bytes that the chrome
 * already length-validated in the EVENT_FETCH_REQUEST handler. */
static int url_has_prefix(const uint8_t *url, uint16_t url_len,
                          const char *prefix) {
    uint32_t i = 0;
    while (prefix[i] != '\0') {
        if ((uint32_t)i >= (uint32_t)url_len) return 0;
        if ((char)url[i] != prefix[i]) return 0;
        i++;
    }
    return 1;
}

static int url_is_http(const uint8_t *url, uint16_t url_len) {
    return url_has_prefix(url, url_len, "http://")
        || url_has_prefix(url, url_len, "https://");
}

#ifndef UNIT_TEST
/* F3.3g: small scratch to NUL-terminate the URL bytes borrowed from
 * the chrome's pending-fetch slot before handing them to
 * `http_get()`, which expects a C-string. The chrome bounds url_len
 * at BROWSER_CHROME_URL_MAX (768) so HTTP_MAX_URL (1024) is comfortable. */
static char g_fetch_url_scratch[HTTP_MAX_URL];

/* Canned content-type returned when the remote peer omitted the
 * `Content-Type` header. We fall back to `text/html` because the
 * capyhtml parser expects HTML; an unknown-type body is still better
 * than an empty FETCH_RESPONSE (which would render as the stub blue
 * frame). */
static const uint8_t k_fallback_ctype[] = "text/html; charset=utf-8";
#define K_FALLBACK_CTYPE_LEN ((uint16_t)(sizeof(k_fallback_ctype) - 1u))

/* Map the http_get error codes to the closest BROWSER_IPC_FETCH_*
 * status. The engine surfaces non-200 statuses as NAV_FAILED with a
 * "fetch_status=NNN" reason, so mapping to 0 (transport error) lets
 * the user distinguish "server unreachable" from "404 not found". */
static uint16_t http_rc_to_fetch_status(int rc) {
    if (rc == 0) return BROWSER_IPC_FETCH_OK;
    return BROWSER_IPC_FETCH_TRANSPORT_ERR;
}

/* Try to satisfy the fetch via the real HTTP/HTTPS client. On
 * success, populates `out_status`, `out_ctype`/`out_ctype_len`, and
 * `out_body`/`out_body_len` with pointers owned by the
 * `struct http_response` (the caller must keep `*resp` alive until
 * the FETCH_RESPONSE is encoded). The body pointer aliases the
 * kmalloc'd buffer that `http_response_free()` releases. Returns
 * 1 on HTTP success (2xx-5xx from the server), 0 on transport
 * failure (DNS/refused/timeout/net-stack-not-ready), -1 if the URL
 * is too long to fit in the scratch. */
static int try_http_fetch(const uint8_t *url, uint16_t url_len,
                          struct http_response *resp,
                          uint16_t *out_status,
                          const uint8_t **out_ctype, uint16_t *out_ctype_len,
                          const uint8_t **out_body, uint32_t *out_body_len) {
    if (url_len == 0u || (uint32_t)url_len >= sizeof(g_fetch_url_scratch)) {
        return -1;
    }
    for (uint16_t i = 0; i < url_len; ++i) {
        g_fetch_url_scratch[i] = (char)url[i];
    }
    g_fetch_url_scratch[url_len] = '\0';

    if (!net_stack_ready()) {
        *out_status = BROWSER_IPC_FETCH_TRANSPORT_ERR;
        *out_ctype = k_fallback_ctype;
        *out_ctype_len = K_FALLBACK_CTYPE_LEN;
        *out_body = (const uint8_t *)0;
        *out_body_len = 0u;
        return 0;
    }

    int rc = http_get(g_fetch_url_scratch, resp);
    if (rc != 0) {
        *out_status = http_rc_to_fetch_status(rc);
        *out_ctype = k_fallback_ctype;
        *out_ctype_len = K_FALLBACK_CTYPE_LEN;
        *out_body = (const uint8_t *)0;
        *out_body_len = 0u;
        return 0;
    }

    /* Map the numeric HTTP status from the remote server into the
     * browser fetch enum. 200 OK -> OK; other 2xx are rendered as
     * OK as well (engine only cares about body); 404/403 map to the
     * corresponding enum entries; everything else surfaces as 503. */
    int sc = resp->status_code;
    if (sc >= 200 && sc < 300) {
        *out_status = BROWSER_IPC_FETCH_OK;
    } else if (sc == 404) {
        *out_status = BROWSER_IPC_FETCH_NOT_FOUND;
    } else if (sc == 403) {
        *out_status = BROWSER_IPC_FETCH_FORBIDDEN;
    } else {
        *out_status = BROWSER_IPC_FETCH_UNAVAILABLE;
    }

    /* Pull the Content-Type header if present; otherwise default to
     * HTML. We do not parse charset here -- the parser is ASCII/UTF-8
     * tolerant and simply echoes bytes to the rasterizer. */
    const char *ctype = http_find_header(resp, "Content-Type");
    if (ctype && ctype[0] != '\0') {
        uint16_t clen = 0;
        while (ctype[clen] != '\0' && clen < 0xFFFFu) clen++;
        *out_ctype = (const uint8_t *)ctype;
        *out_ctype_len = clen;
    } else {
        *out_ctype = k_fallback_ctype;
        *out_ctype_len = K_FALLBACK_CTYPE_LEN;
    }

    *out_body = resp->body;
    /* Clamp the body so the encoded payload + 16 B header + ctype
     * cannot overflow the IPC limit (1 MiB). This protects both the
     * scratch buffer below and the engine's `fetch_scratch`. */
    uint32_t max_body = CHROME_RUNTIME_FETCH_PAYLOAD_MAX
                        - 16u - (uint32_t)(*out_ctype_len);
    if (max_body > BROWSER_IPC_MAX_PAYLOAD - 16u - (uint32_t)(*out_ctype_len)) {
        max_body = BROWSER_IPC_MAX_PAYLOAD - 16u - (uint32_t)(*out_ctype_len);
    }
    *out_body_len = (resp->body_len <= (size_t)max_body)
                    ? (uint32_t)resp->body_len
                    : max_body;
    return 1;
}
#endif /* !UNIT_TEST */

int chrome_runtime_dispatch_pending_fetch(struct chrome_runtime *rt) {
    if (!rt) return -1;
    struct browser_ipc_fetch_request req;
    if (!browser_chrome_take_pending_fetch(&rt->chrome, &req)) {
        return 0; /* nothing pending */
    }

    uint16_t status = BROWSER_IPC_FETCH_NOT_FOUND;
    const uint8_t *ctype = (const uint8_t *)0;
    uint16_t ctype_len = 0u;
    const uint8_t *body = (const uint8_t *)0;
    uint32_t body_len = 0u;

    /* Outer storage that owns the HTTP response buffer while we
     * encode it. Released after send_frame so the kmalloc'd body
     * isn't held beyond the IPC write. http_response_free is a
     * no-op kfree today (kernel heap is bump-only), but calling it
     * keeps the code honest for when a real free lands. */
#ifndef UNIT_TEST
    struct http_response http_resp;
    int used_http = 0;
#endif

    /* Route: real HTTP(S) goes through the net stack; anything else
     * falls back to the built-in `file://capyos/xyz` resolver. */
    if (url_is_http((const uint8_t *)req.url, req.url_len)) {
#ifndef UNIT_TEST
        int rc = try_http_fetch((const uint8_t *)req.url, req.url_len,
                                 &http_resp,
                                 &status, &ctype, &ctype_len,
                                 &body, &body_len);
        if (rc == 1) {
            used_http = 1;
        } else if (rc == 0) {
            /* transport-level failure: status + empty body already
             * filled. No http_resp to free. */
        } else {
            /* URL too long for the HTTP scratch: surface as 503
             * with an HTML stub so the engine still renders something. */
            status = BROWSER_IPC_FETCH_UNAVAILABLE;
            ctype = k_fallback_ctype;
            ctype_len = K_FALLBACK_CTYPE_LEN;
        }
#else
        /* Host tests don't link the HTTP stack; pretend HTTP URLs
         * transport-fail cleanly so the engine sees status=0. */
        status = BROWSER_IPC_FETCH_TRANSPORT_ERR;
#endif
    } else {
        struct browser_chrome_fetch_result res;
        browser_chrome_resolve_local((const char *)req.url, req.url_len, &res);
        status = res.status;
        ctype = res.content_type;
        ctype_len = res.content_type_len;
        body = res.body;
        body_len = res.body_len;
    }

    uint32_t n = browser_chrome_build_fetch_response_payload(
        req.seq, req.nav_id, status,
        ctype, ctype_len,
        body, body_len,
        g_fetch_response_scratch, sizeof(g_fetch_response_scratch));
    if (n == 0u) {
        /* Body + headers together exceeded the scratch. Retry with
         * a 503 + empty body so the engine gets a valid reply and
         * can surface the failure in the URL bar. */
        n = browser_chrome_build_fetch_response_payload(
            req.seq, req.nav_id, BROWSER_IPC_FETCH_UNAVAILABLE,
            (const uint8_t *)0, 0u, (const uint8_t *)0, 0u,
            g_fetch_response_scratch, sizeof(g_fetch_response_scratch));
        if (n == 0u) {
#ifndef UNIT_TEST
            if (used_http) http_response_free(&http_resp);
#endif
            return -1; /* should never happen: 16 B fits easily */
        }
    }
    int sr = chrome_runtime_send_ipc_frame(rt, BROWSER_IPC_FETCH_RESPONSE,
                        g_fetch_response_scratch, n);
#ifndef UNIT_TEST
    if (used_http) http_response_free(&http_resp);
#endif
    if (sr != 0) {
        rt->engine_alive = 0;
        return -1;
    }
    /* Etapa 5 hardening (2026-05-03): grava no audit log o despacho
     * bem-sucedido com o status code (HTTP-like) como `code`. */
    capyc_audit_record(&rt->audit, (uint8_t)CAPYC_AUDIT_FETCH, status);
    return 1;
}

int chrome_runtime_poll_event(struct chrome_runtime *rt,
                              uint64_t now_ticks,
                              uint32_t *out_actions) {
    if (out_actions) *out_actions = 0u;
    if (!rt) return CHROME_RUNTIME_POLL_PROTOCOL_ERR;

    /* Etapa 5 hardening (2026-05-03): rate limit incoming. Cada tick
     * pode admitir ate CHROME_RUNTIME_INCOMING_RATE_MAX eventos. Se
     * o engine spamma frames maliciosamente, paramos de drenar nesse
     * tick (deixa pipe cheio que vai aplicar back-pressure no engine)
     * e o caller pode reagir como achar melhor. Tick reseta. */
    if (rt->incoming_in_window >= CHROME_RUNTIME_INCOMING_RATE_MAX) {
        rt->total_incoming_drops++;
        capyc_audit_record(&rt->audit, (uint8_t)CAPYC_AUDIT_RATE_DROP, 0u);
        return CHROME_RUNTIME_POLL_RATE_LIMITED;
    }

    /* Le header (12 bytes). Se vazio, NO_DATA. */
    uint8_t hbuf[BROWSER_IPC_HEADER_SIZE];
    int rh = read_full(rt->response_pipe_id, hbuf, sizeof(hbuf),
                       /*allow_yield=*/0);
    if (rh == -1) return CHROME_RUNTIME_POLL_NO_DATA;
    if (rh == 0) {
        rt->engine_alive = 0;
        rt->total_engine_eofs++;
        capyc_audit_record(&rt->audit, (uint8_t)CAPYC_AUDIT_ENGINE_EOF, 0u);
        return CHROME_RUNTIME_POLL_ENGINE_EOF;
    }
    if (rh != 1) {
        capyc_audit_record(&rt->audit, (uint8_t)CAPYC_AUDIT_PROTOCOL, 1u);
        return CHROME_RUNTIME_POLL_PROTOCOL_ERR;
    }

    /* Decode + valida. */
    struct browser_ipc_header hdr;
    int rc = browser_ipc_header_decode(hbuf, sizeof(hbuf), &hdr);
    if (rc != BROWSER_IPC_OK) {
        capyc_audit_record(&rt->audit, (uint8_t)CAPYC_AUDIT_PROTOCOL, 2u);
        return CHROME_RUNTIME_POLL_PROTOCOL_ERR;
    }
    if (hdr.payload_len > (CHROME_RUNTIME_EVENT_BUF_MAX
                           - BROWSER_IPC_HEADER_SIZE)) {
        /* Payload nao cabe no scratch; protocolo violado. */
        capyc_audit_record(&rt->audit, (uint8_t)CAPYC_AUDIT_PROTOCOL, 3u);
        return CHROME_RUNTIME_POLL_PROTOCOL_ERR;
    }

    /* Le payload se houver. */
    uint8_t *plbuf = rt->event_scratch;
    if (hdr.payload_len > 0u) {
        int rp = read_full(rt->response_pipe_id, plbuf, hdr.payload_len,
                           /*allow_yield=*/1);
        if (rp != 1) {
            capyc_audit_record(&rt->audit,
                               (uint8_t)CAPYC_AUDIT_PROTOCOL, 4u);
            return CHROME_RUNTIME_POLL_PROTOCOL_ERR;
        }
    }

    /* Etapa 5 hardening (2026-05-05): EVENT_NAV_STARTED inicia uma
     * janela de budget nova. Reset ANTES do dispatch para que o
     * proprio NAV_STARTED conte como o primeiro evento da nav (poucas
     * centenas de bytes, longe do limite). */
    if (hdr.kind == BROWSER_IPC_EVENT_NAV_STARTED) {
        chrome_runtime_reset_nav_budget(rt);
    }

    uint32_t actions = browser_chrome_dispatch_event(
        &rt->chrome, &hdr, hdr.payload_len > 0u ? plbuf : (const uint8_t *)0,
        now_ticks);
    rt->total_events_polled++;
    /* Etapa 5 hardening (2026-05-03): conta como evento admitido para
     * o budget do tick + acumula bytes recebidos para observability. */
    rt->incoming_in_window++;
    uint64_t event_bytes =
        (uint64_t)BROWSER_IPC_HEADER_SIZE + (uint64_t)hdr.payload_len;
    rt->total_event_bytes_received += event_bytes;
    /* Etapa 5 hardening (2026-05-05): incrementa o contador da
     * navegacao corrente. Checagem de limite vem logo depois. */
    rt->bytes_in_current_nav += event_bytes;
    if (hdr.kind == BROWSER_IPC_EVENT_PONG) {
        rt->total_pongs_received++;
    }

    /* F3.3f triagem 2026-05-02: o dispatcher aliasa
     * `chrome.last_frame.pixels` em `event_scratch + 12u`. Esse
     * alias ficaria invalido assim que o proximo poll sobrescrever
     * o scratch. Para garantir que o consumidor (browser_app, smoke
     * test, etc.) sempre veja pixels validos enquanto `last_frame`
     * descreve um frame, copiamos para o storage dedicado AGORA,
     * antes de retornar ao caller. Frames maiores que o storage sao
     * descartados (pixels=NULL) para evitar mostrar lixo. */
    if ((actions & BROWSER_CHROME_ACTION_REPAINT_FRAME) != 0u) {
        struct browser_chrome_frame_meta *meta = &rt->chrome.last_frame;
        if (meta->pixels && meta->pixel_bytes > 0u
            && meta->pixel_bytes <= CHROME_RUNTIME_FRAME_STORAGE_MAX) {
            for (uint32_t i = 0; i < meta->pixel_bytes; ++i) {
                rt->last_frame_storage[i] = meta->pixels[i];
            }
            meta->pixels = rt->last_frame_storage;
            rt->total_frames_persisted++;
        } else {
            /* Defensivo: pixel_bytes > storage nao deveria acontecer
             * porque o dispatcher valida que payload <= scratch, mas
             * se acontecer (ex.: bug futuro no encoder), zerar evita
             * UB do consumidor. */
            meta->pixels = (const uint8_t *)0;
            meta->pixel_bytes = 0u;
            rt->total_frames_dropped++;
        }
    }

    if (out_actions) *out_actions = actions;

    /* Etapa 5 hardening (2026-05-05): cheque o budget DEPOIS do
     * dispatch, para que o evento corrente (geralmente um frame
     * valido) ainda chegue ao caller -- a kill so se aplica a
     * proxima rodada de eventos. Politica: passou o limite =>
     * engine_alive = 0 + audit + status BUDGET_EXCEEDED. O caller
     * (browser_app) vai detectar engine_alive == 0 no proximo tick
     * e respawnar via record_restart. */
    if (rt->bytes_in_current_nav > g_nav_budget_max) {
        rt->engine_alive = 0;
        rt->total_nav_budget_kills++;
        /* code = KiB acumulados truncados a uint16 (max ~64 MiB). */
        uint64_t kib = rt->bytes_in_current_nav >> 10;
        uint16_t code = (kib > 0xFFFFu) ? 0xFFFFu : (uint16_t)kib;
        capyc_audit_record(&rt->audit,
                           (uint8_t)CAPYC_AUDIT_BUDGET_EXCEEDED, code);
        return CHROME_RUNTIME_POLL_NAV_BUDGET_EXCEEDED;
    }

    return CHROME_RUNTIME_POLL_EVENT_HANDLED;
}

int chrome_runtime_tick(struct chrome_runtime *rt, uint64_t now_ticks) {
    if (!rt) return 0;
    browser_watchdog_tick(&rt->chrome.watchdog, now_ticks);

    /* Etapa 5 hardening (2026-05-03): reseta budget do rate limiter
     * a cada tick. Por design, eventos drenados na "janela" anterior
     * sao esquecidos -- so o burst desde o ultimo tick conta. */
    rt->incoming_in_window = 0u;

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
    /* Etapa 5 hardening (2026-05-05): novo engine = nova nav fresca
     * do ponto de vista do budget. `total_nav_budget_kills` PRESERVA
     * (e telemetria cumulativa, nao reseta entre respawns). */
    rt->bytes_in_current_nav = 0u;
    /* Storage dedicado tambem e invalidado para que um poll
     * que toque last_frame_storage por engano nao exponha bytes
     * do engine antigo. Apenas o primeiro byte basta para detectar
     * via testes; o resto sera sobrescrito pelo proximo FRAME. */
    rt->last_frame_storage[0] = 0u;
}
