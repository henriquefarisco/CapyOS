/*
 * userland/bin/capybrowser/main.c -- F3.3b: stub do browser engine
 * em ring 3.
 *
 * Esta primeira versao implementa o loop minimo de IPC do browser:
 *   - Le frames do fd 0 (request pipe do chrome).
 *   - Decodifica o header binario big-endian via codec compartilhado.
 *   - Despacha por kind e responde no fd 1 (response pipe).
 *   - Termina em SHUTDOWN ou erro irrecuperavel de leitura.
 *
 * Cobertura desta fase:
 *   - NAVIGATE  -> NAV_STARTED + NAV_PROGRESS(fetch/parse/render) +
 *                  EVENT_FRAME (16x16 RGBA solido) + NAV_READY.
 *   - PING      -> EVENT_PONG (mesmo nonce).
 *   - SHUTDOWN  -> capy_exit(0).
 *   - CANCEL    -> NAV_CANCELLED imediato (sem render real).
 *   - Outros    -> drena payload e ignora.
 *
 * Renderizacao real, parser HTML/CSS, fetch HTTP e watchdog ficam
 * para F3.3c/F3.3d. Este binario apenas prova que o protocolo
 * funciona end-to-end sobre os pipes do M5.
 *
 * Restricoes (mesma trilha de hello/exectarget/capysh):
 *   - Sem libc.
 *   - Sem .bss (loader nao zera).
 *   - Sem alocacoes; tudo em stack ou .rodata.
 */

#include "apps/browser_ipc.h"
#include <capylibc/capylibc.h>
#include <stdint.h>

/* Tamanho da janela "default" emitida no primeiro EVENT_FRAME. Pequeno
 * de proposito: o objetivo desta fase e provar o pipeline; pixels
 * reais virao na fase 3c. */
#define STUB_FRAME_W 16
#define STUB_FRAME_H 16
#define STUB_FRAME_STRIDE (STUB_FRAME_W * 4u) /* BGRA */
#define STUB_FRAME_BYTES  (STUB_FRAME_STRIDE * STUB_FRAME_H)

/* Limite de payload aceito sem alocacao dinamica (1 KiB cobre URLs
 * de ate 1022 bytes, mais que o limite atual do html_viewer). */
#define INPUT_PAYLOAD_BUF 1024u

static const char k_log_started[] = "[capybrowser] engine v0 online\n";

/* read_full: le exatamente n bytes do fd ou retorna -1 em EOF/erro.
 * Usa busy-yield em caso de short read para ceder CPU. */
static long read_full(int fd, void *buf, size_t n) {
    uint8_t *p = (uint8_t *)buf;
    size_t got = 0;
    while (got < n) {
        long rd = capy_read(fd, p + got, n - got);
        if (rd < 0) {
            return -1;
        }
        if (rd == 0) {
            /* EOF: pipe write-end fechou. */
            return -1;
        }
        got += (size_t)rd;
    }
    return (long)n;
}

/* write_full: escreve exatamente n bytes ou retorna -1 em broken pipe. */
static long write_full(int fd, const void *buf, size_t n) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t sent = 0;
    while (sent < n) {
        long wr = capy_write(fd, p + sent, n - sent);
        if (wr <= 0) {
            return -1;
        }
        sent += (size_t)wr;
    }
    return (long)n;
}

/* Sequencia local de eventos enviados pelo engine. */
static uint32_t g_resp_seq = 0;

/* Emite um frame IPC (header + payload). Retorna 0 ok, -1 broken pipe. */
static int send_frame(uint16_t kind, const void *payload, uint32_t payload_len) {
    struct browser_ipc_header hdr = {
        .magic       = BROWSER_IPC_MAGIC,
        .kind        = kind,
        .seq         = ++g_resp_seq,
        .payload_len = payload_len
    };
    uint8_t hdr_buf[BROWSER_IPC_HEADER_SIZE];
    if (browser_ipc_header_encode(&hdr, hdr_buf, sizeof(hdr_buf)) != BROWSER_IPC_OK) {
        return -1;
    }
    if (write_full(1, hdr_buf, sizeof(hdr_buf)) < 0) {
        return -1;
    }
    if (payload_len > 0u) {
        if (write_full(1, payload, payload_len) < 0) {
            return -1;
        }
    }
    return 0;
}

/* Helpers para escrever inteiros big-endian em buffer de payload. */
static void be_put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)((v >> 8) & 0xFFu);
    p[1] = (uint8_t)(v & 0xFFu);
}

static void be_put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)((v >> 24) & 0xFFu);
    p[1] = (uint8_t)((v >> 16) & 0xFFu);
    p[2] = (uint8_t)((v >> 8) & 0xFFu);
    p[3] = (uint8_t)(v & 0xFFu);
}

static uint32_t be_get_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24)
         | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8)
         |  (uint32_t)p[3];
}

/* Sequencia de eventos para uma navegacao. nav_id distingue
 * navegacoes consecutivas; o chrome ignora frames com nav_id antigo. */
static uint32_t g_nav_id = 0;

static int emit_log(uint8_t level, const char *msg) {
    /* level u8 + msg_len u16 + msg utf8 */
    size_t ml = 0;
    while (msg && msg[ml] != 0 && ml < 0xFFFFu) ml++;
    uint8_t buf[3 + 256];
    if (ml > 256u) ml = 256u;
    buf[0] = level;
    be_put_u16(&buf[1], (uint16_t)ml);
    for (size_t i = 0; i < ml; i++) buf[3 + i] = (uint8_t)msg[i];
    return send_frame(BROWSER_IPC_EVENT_LOG, buf, (uint32_t)(3u + ml));
}

static int emit_nav_started(uint32_t nav_id, const uint8_t *url, uint16_t url_len) {
    uint8_t buf[6 + 1024];
    if ((uint32_t)url_len > 1024u) url_len = 1024u;
    be_put_u32(&buf[0], nav_id);
    be_put_u16(&buf[4], url_len);
    for (uint16_t i = 0; i < url_len; i++) buf[6 + i] = url[i];
    return send_frame(BROWSER_IPC_EVENT_NAV_STARTED, buf, (uint32_t)(6u + url_len));
}

static int emit_nav_progress(uint32_t nav_id, uint8_t stage, uint8_t percent) {
    uint8_t buf[6];
    be_put_u32(&buf[0], nav_id);
    buf[4] = stage;
    buf[5] = percent;
    return send_frame(BROWSER_IPC_EVENT_NAV_PROGRESS, buf, sizeof(buf));
}

static int emit_nav_ready(uint32_t nav_id) {
    uint8_t buf[4];
    be_put_u32(buf, nav_id);
    return send_frame(BROWSER_IPC_EVENT_NAV_READY, buf, sizeof(buf));
}

static int emit_nav_cancelled(uint32_t nav_id) {
    uint8_t buf[4];
    be_put_u32(buf, nav_id);
    return send_frame(BROWSER_IPC_EVENT_NAV_CANCELLED, buf, sizeof(buf));
}

static int emit_pong(uint32_t nonce) {
    uint8_t buf[4];
    be_put_u32(buf, nonce);
    return send_frame(BROWSER_IPC_EVENT_PONG, buf, sizeof(buf));
}

/* Pinta um frame solido azul (0xFF1B66CB ARGB) na area da viewport.
 * Layout: BGRA8888 little-endian (compativel com framebuffer Capy). */
static int emit_stub_frame(uint32_t nav_id) {
    /* Header do frame: nav_id u32 + width u16 + height u16 + stride u32. */
    uint8_t hdr[12];
    be_put_u32(&hdr[0], nav_id);
    be_put_u16(&hdr[4], (uint16_t)STUB_FRAME_W);
    be_put_u16(&hdr[6], (uint16_t)STUB_FRAME_H);
    be_put_u32(&hdr[8], STUB_FRAME_STRIDE);

    /* Sem blit real ainda: mando apenas o header + payload zeros para
     * caber no protocolo. F3.3c trara pixels de verdade. */
    static const uint8_t pixel_ab[4] = { 0xCBu, 0x66u, 0x1Bu, 0xFFu }; /* B,G,R,A */
    uint8_t pixels[STUB_FRAME_BYTES];
    for (uint32_t i = 0; i < STUB_FRAME_BYTES; i += 4u) {
        pixels[i + 0] = pixel_ab[0];
        pixels[i + 1] = pixel_ab[1];
        pixels[i + 2] = pixel_ab[2];
        pixels[i + 3] = pixel_ab[3];
    }

    /* Envia hdr + pixels como um unico frame IPC (12 + STUB_FRAME_BYTES). */
    uint8_t payload[12u + STUB_FRAME_BYTES];
    for (uint32_t i = 0; i < 12u; i++) payload[i] = hdr[i];
    for (uint32_t i = 0; i < STUB_FRAME_BYTES; i++) payload[12u + i] = pixels[i];
    return send_frame(BROWSER_IPC_EVENT_FRAME, payload, (uint32_t)sizeof(payload));
}

/* Sequencia completa de uma navegacao stub: started -> fetch ->
 * parse -> render -> frame -> ready. */
static int run_navigate(const uint8_t *payload, uint32_t payload_len) {
    if (payload_len < 2u) {
        /* sem url_len: erro controlado, NAV_FAILED. */
        uint8_t err[6 + 8];
        be_put_u32(&err[0], ++g_nav_id);
        const char *reason = "no_url";
        be_put_u16(&err[4], 6u);
        for (int i = 0; i < 6; i++) err[6 + i] = (uint8_t)reason[i];
        return send_frame(BROWSER_IPC_EVENT_NAV_FAILED, err, 12u);
    }
    uint16_t url_len = (uint16_t)(((uint16_t)payload[0] << 8) | (uint16_t)payload[1]);
    if (2u + (uint32_t)url_len > payload_len) {
        /* url_len mente sobre o tamanho do payload. */
        uint8_t err[6 + 12];
        be_put_u32(&err[0], ++g_nav_id);
        const char *reason = "url_overflow";
        uint16_t rl = 12u;
        be_put_u16(&err[4], rl);
        for (uint16_t i = 0; i < rl; i++) err[6 + i] = (uint8_t)reason[i];
        return send_frame(BROWSER_IPC_EVENT_NAV_FAILED, err, (uint32_t)(6u + rl));
    }
    const uint8_t *url = payload + 2u;
    uint32_t nav = ++g_nav_id;

    if (emit_nav_started(nav, url, url_len) < 0) return -1;
    if (emit_nav_progress(nav, BROWSER_IPC_STAGE_FETCH,  10u) < 0) return -1;
    if (emit_nav_progress(nav, BROWSER_IPC_STAGE_PARSE,  60u) < 0) return -1;
    if (emit_nav_progress(nav, BROWSER_IPC_STAGE_RENDER, 90u) < 0) return -1;
    if (emit_stub_frame(nav) < 0) return -1;
    if (emit_nav_ready(nav) < 0) return -1;
    return 0;
}

/* Drena um payload do request pipe sem usa-lo. */
static int drain_payload(uint32_t payload_len) {
    uint8_t scratch[256];
    while (payload_len > 0u) {
        size_t take = payload_len > sizeof(scratch) ? sizeof(scratch) : payload_len;
        if (read_full(0, scratch, take) < 0) return -1;
        payload_len -= (uint32_t)take;
    }
    return 0;
}

int main(int rank) {
    (void)rank;

    /* Anuncia versao via EVENT_LOG (chrome encaminha ao klog). */
    (void)emit_log(BROWSER_IPC_LOG_INFO, "capybrowser engine v=0 online");
    (void)capy_write(2, k_log_started, sizeof(k_log_started) - 1u);

    for (;;) {
        uint8_t hdr_buf[BROWSER_IPC_HEADER_SIZE];
        if (read_full(0, hdr_buf, sizeof(hdr_buf)) < 0) {
            /* Pipe de request fechado: chrome saiu. Encerra limpo. */
            capy_exit(0);
        }
        struct browser_ipc_header hdr;
        int rc = browser_ipc_header_decode(hdr_buf, sizeof(hdr_buf), &hdr);
        if (rc != BROWSER_IPC_OK) {
            /* Frame invalido: drena e ignora. Em producao seria erro
             * fatal, mas estamos em stub e queremos resiliencia. */
            (void)emit_log(BROWSER_IPC_LOG_WARN, "bad ipc header");
            capy_exit(2);
        }
        if (!browser_ipc_kind_is_request(hdr.kind)) {
            /* Engine recebendo evento? protocolo violado. */
            (void)emit_log(BROWSER_IPC_LOG_ERROR, "non-request kind from chrome");
            (void)drain_payload(hdr.payload_len);
            continue;
        }
        if (hdr.payload_len > INPUT_PAYLOAD_BUF) {
            /* Recusa, drena. */
            (void)emit_log(BROWSER_IPC_LOG_WARN, "request payload too large");
            (void)drain_payload(hdr.payload_len);
            continue;
        }
        uint8_t payload[INPUT_PAYLOAD_BUF];
        if (hdr.payload_len > 0u) {
            if (read_full(0, payload, hdr.payload_len) < 0) {
                capy_exit(0);
            }
        }
        switch (hdr.kind) {
            case BROWSER_IPC_NAVIGATE:
                (void)run_navigate(payload, hdr.payload_len);
                break;
            case BROWSER_IPC_PING: {
                if (hdr.payload_len < 4u) break;
                uint32_t nonce = be_get_u32(payload);
                (void)emit_pong(nonce);
                break;
            }
            case BROWSER_IPC_CANCEL:
                (void)emit_nav_cancelled(g_nav_id);
                break;
            case BROWSER_IPC_SHUTDOWN:
                capy_exit(0);
            case BROWSER_IPC_BACK:
            case BROWSER_IPC_FORWARD:
            case BROWSER_IPC_RELOAD:
            case BROWSER_IPC_SCROLL:
            case BROWSER_IPC_RESIZE:
            case BROWSER_IPC_CLICK:
            case BROWSER_IPC_KEY:
            default:
                /* Stub: ignora silenciosamente (futuro F3.3c implementa). */
                break;
        }
    }
}
