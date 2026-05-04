/*
 * src/apps/browser_chrome/chrome.c (F3.3d)
 *
 * Estado e dispatcher do browser chrome. Sem syscalls, sem GUI:
 * o dispatcher consome eventos IPC ja decodificados e produz uma
 * mascara de acoes que a integracao real do compositor (a ser
 * entregue na proxima sub-fase) executa.
 *
 * Contrato em include/apps/browser_chrome.h.
 */

#include "apps/browser_chrome.h"

static void be_get_u16(const uint8_t *p, uint16_t *out) {
    *out = (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static void be_get_u32(const uint8_t *p, uint32_t *out) {
    *out = ((uint32_t)p[0] << 24)
         | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8)
         |  (uint32_t)p[3];
}

static void be_put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)((v >> 8) & 0xFFu);
    p[1] = (uint8_t)(v & 0xFFu);
}

static void copy_clamped(char *dst, uint16_t *dst_len, uint16_t dst_cap,
                         const uint8_t *src, uint32_t src_len) {
    uint16_t take = (uint16_t)((src_len > dst_cap) ? dst_cap : src_len);
    for (uint16_t i = 0; i < take; ++i) dst[i] = (char)src[i];
    *dst_len = take;
    /* NUL-termina apenas se sobrar espaco; senao deixa exatamente
     * `take` bytes como conteudo. Caller que precisar de C-string
     * deve checar dst_len. */
    if (take < dst_cap) dst[take] = '\0';
}

void browser_chrome_init(struct browser_chrome *c, uint64_t now_ticks) {
    if (!c) return;
    c->status = BROWSER_CHROME_STATUS_IDLE;
    c->current_nav_id = 0u;
    c->last_progress_stage = 0u;
    c->last_progress_percent = 0u;
    c->current_url[0] = '\0';
    c->current_url_len = 0u;
    c->current_title[0] = '\0';
    c->current_title_len = 0u;
    c->last_error_reason[0] = '\0';
    c->last_error_reason_len = 0u;
    c->last_frame.nav_id = 0u;
    c->last_frame.width = 0u;
    c->last_frame.height = 0u;
    c->last_frame.stride = 0u;
    c->last_frame.pixels = (const uint8_t *)0;
    c->last_frame.pixel_bytes = 0u;
    c->next_request_seq = 1u;
    c->total_events_handled = 0u;
    c->total_protocol_errors = 0u;
    /* slice 4: zero out the log slot so callers see len=0 until
     * the engine emits a real EVENT_LOG. */
    c->last_log_level = 0u;
    c->last_log_msg_len = 0u;
    c->last_log_msg[0] = '\0';
    /* slice 5b: no pending fetch at startup. */
    c->pending_fetch_active = 0u;
    c->pending_fetch_method = 0u;
    c->pending_fetch_seq = 0u;
    c->pending_fetch_nav_id = 0u;
    c->pending_fetch_url[0] = '\0';
    c->pending_fetch_url_len = 0u;
    browser_watchdog_init(&c->watchdog, now_ticks);
}

uint32_t browser_chrome_alloc_request_seq(struct browser_chrome *c) {
    if (!c) return 0u;
    uint32_t s = c->next_request_seq++;
    if (c->next_request_seq == 0u) c->next_request_seq = 1u;
    return s;
}

uint32_t browser_chrome_build_navigate_payload(const char *url,
                                               size_t url_len,
                                               uint8_t *out_buf,
                                               uint32_t out_size) {
    if (!url || !out_buf) return 0u;
    if (url_len > 0xFFFFu) url_len = 0xFFFFu;
    uint32_t need = 2u + (uint32_t)url_len;
    if (need > out_size) return 0u;
    be_put_u16(&out_buf[0], (uint16_t)url_len);
    for (size_t i = 0; i < url_len; ++i) out_buf[2u + i] = (uint8_t)url[i];
    return need;
}

void browser_chrome_record_navigate_sent(struct browser_chrome *c,
                                         const char *url,
                                         size_t url_len) {
    if (!c) return;
    c->status = BROWSER_CHROME_STATUS_LOADING;
    c->last_progress_stage = 0u;
    c->last_progress_percent = 0u;
    /* Limpa titulo antigo: o engine vai reportar o novo via EVENT_TITLE. */
    c->current_title[0] = '\0';
    c->current_title_len = 0u;
    c->last_error_reason[0] = '\0';
    c->last_error_reason_len = 0u;
    if (url) {
        uint16_t take = (uint16_t)((url_len > BROWSER_CHROME_URL_MAX - 1u)
                                       ? BROWSER_CHROME_URL_MAX - 1u
                                       : url_len);
        for (uint16_t i = 0; i < take; ++i) c->current_url[i] = url[i];
        c->current_url[take] = '\0';
        c->current_url_len = take;
    }
}

/* Despacho por kind. Cada handler retorna a mascara de acoes. */

static uint32_t handle_title(struct browser_chrome *c,
                             const uint8_t *payload, uint32_t plen) {
    if (plen < 2u) return BROWSER_CHROME_ACTION_PROTOCOL_ERR;
    uint16_t tlen;
    be_get_u16(payload, &tlen);
    if (2u + (uint32_t)tlen > plen) {
        return BROWSER_CHROME_ACTION_PROTOCOL_ERR;
    }
    copy_clamped(c->current_title, &c->current_title_len,
                 BROWSER_CHROME_TITLE_MAX - 1u,
                 payload + 2u, tlen);
    /* Garantir NUL terminator em todos os caminhos. */
    if (c->current_title_len < BROWSER_CHROME_TITLE_MAX) {
        c->current_title[c->current_title_len] = '\0';
    }
    return BROWSER_CHROME_ACTION_UPDATE_TITLE;
}

static uint32_t handle_nav_started(struct browser_chrome *c,
                                   const uint8_t *payload, uint32_t plen) {
    if (plen < 6u) return BROWSER_CHROME_ACTION_PROTOCOL_ERR;
    uint32_t nav;
    be_get_u32(payload, &nav);
    uint16_t ulen;
    be_get_u16(payload + 4u, &ulen);
    if (6u + (uint32_t)ulen > plen) {
        return BROWSER_CHROME_ACTION_PROTOCOL_ERR;
    }
    c->current_nav_id = nav;
    c->status = BROWSER_CHROME_STATUS_LOADING;
    c->last_progress_stage = 0u;
    c->last_progress_percent = 0u;
    /* O url no NAV_STARTED pode ser o final apos redirect; atualiza. */
    copy_clamped(c->current_url, &c->current_url_len,
                 BROWSER_CHROME_URL_MAX - 1u,
                 payload + 6u, ulen);
    if (c->current_url_len < BROWSER_CHROME_URL_MAX) {
        c->current_url[c->current_url_len] = '\0';
    }
    return BROWSER_CHROME_ACTION_UPDATE_STATUS;
}

static uint32_t handle_nav_progress(struct browser_chrome *c,
                                    const uint8_t *payload, uint32_t plen) {
    if (plen < 6u) return BROWSER_CHROME_ACTION_PROTOCOL_ERR;
    uint32_t nav;
    be_get_u32(payload, &nav);
    if (nav != c->current_nav_id) return 0u; /* stale */
    c->last_progress_stage = (uint32_t)payload[4];
    c->last_progress_percent = (uint32_t)payload[5];
    return BROWSER_CHROME_ACTION_UPDATE_STATUS;
}

static uint32_t handle_nav_ready(struct browser_chrome *c,
                                 const uint8_t *payload, uint32_t plen) {
    if (plen < 4u) return BROWSER_CHROME_ACTION_PROTOCOL_ERR;
    uint32_t nav;
    be_get_u32(payload, &nav);
    if (nav != c->current_nav_id) return 0u;
    c->status = BROWSER_CHROME_STATUS_READY;
    return BROWSER_CHROME_ACTION_UPDATE_STATUS;
}

static uint32_t handle_nav_failed(struct browser_chrome *c,
                                  const uint8_t *payload, uint32_t plen) {
    if (plen < 6u) return BROWSER_CHROME_ACTION_PROTOCOL_ERR;
    uint32_t nav;
    be_get_u32(payload, &nav);
    uint16_t rlen;
    be_get_u16(payload + 4u, &rlen);
    if (6u + (uint32_t)rlen > plen) {
        return BROWSER_CHROME_ACTION_PROTOCOL_ERR;
    }
    if (nav != c->current_nav_id) return 0u;
    c->status = BROWSER_CHROME_STATUS_FAILED;
    copy_clamped(c->last_error_reason, &c->last_error_reason_len,
                 BROWSER_CHROME_REASON_MAX - 1u,
                 payload + 6u, rlen);
    if (c->last_error_reason_len < BROWSER_CHROME_REASON_MAX) {
        c->last_error_reason[c->last_error_reason_len] = '\0';
    }
    return BROWSER_CHROME_ACTION_UPDATE_STATUS;
}

static uint32_t handle_nav_cancelled(struct browser_chrome *c,
                                     const uint8_t *payload, uint32_t plen) {
    if (plen < 4u) return BROWSER_CHROME_ACTION_PROTOCOL_ERR;
    uint32_t nav;
    be_get_u32(payload, &nav);
    if (nav != c->current_nav_id) return 0u;
    c->status = BROWSER_CHROME_STATUS_CANCELLED;
    return BROWSER_CHROME_ACTION_UPDATE_STATUS;
}

static uint32_t handle_frame(struct browser_chrome *c,
                             const uint8_t *payload, uint32_t plen) {
    if (plen < 12u) return BROWSER_CHROME_ACTION_PROTOCOL_ERR;
    uint32_t nav, stride;
    uint16_t w, h;
    be_get_u32(payload, &nav);
    be_get_u16(payload + 4u, &w);
    be_get_u16(payload + 6u, &h);
    be_get_u32(payload + 8u, &stride);
    if (nav != c->current_nav_id) return 0u; /* stale */

    /* Validacao basica: stride >= w*4, total cabe no payload. */
    uint64_t expected_bytes = (uint64_t)stride * (uint64_t)h;
    if (expected_bytes > (uint64_t)(plen - 12u)) {
        return BROWSER_CHROME_ACTION_PROTOCOL_ERR;
    }
    if ((uint64_t)stride < (uint64_t)w * 4u) {
        return BROWSER_CHROME_ACTION_PROTOCOL_ERR;
    }

    c->last_frame.nav_id = nav;
    c->last_frame.width = w;
    c->last_frame.height = h;
    c->last_frame.stride = stride;
    c->last_frame.pixels = payload + 12u;
    c->last_frame.pixel_bytes = (uint32_t)expected_bytes;
    return BROWSER_CHROME_ACTION_REPAINT_FRAME;
}

static uint32_t handle_pong(struct browser_chrome *c,
                            const uint8_t *payload, uint32_t plen,
                            uint64_t now_ticks) {
    if (plen < 4u) return BROWSER_CHROME_ACTION_PROTOCOL_ERR;
    uint32_t nonce;
    be_get_u32(payload, &nonce);
    browser_watchdog_record_pong(&c->watchdog, nonce, now_ticks);
    return 0u; /* watchdog cuida; nada visivel ao usuario */
}

static uint32_t handle_log(struct browser_chrome *c, uint32_t plen,
                           const uint8_t *payload) {
    /* level u8 + msg_len u16 + msg utf8. */
    if (plen < 3u) return BROWSER_CHROME_ACTION_PROTOCOL_ERR;
    uint16_t mlen;
    be_get_u16(payload + 1u, &mlen);
    if (3u + (uint32_t)mlen > plen) {
        return BROWSER_CHROME_ACTION_PROTOCOL_ERR;
    }
    /* Slice 4: stash the log message in chrome state so callers can
     * read it on LOG_FORWARD without intercepting the IPC frame. */
    c->last_log_level = payload[0];
    uint32_t copy = mlen;
    if (copy >= BROWSER_CHROME_LOG_MSG_MAX) {
        copy = BROWSER_CHROME_LOG_MSG_MAX - 1u;
    }
    for (uint32_t i = 0u; i < copy; i++) {
        c->last_log_msg[i] = (char)payload[3u + i];
    }
    c->last_log_msg[copy] = '\0';
    c->last_log_msg_len = (uint16_t)copy;
    return BROWSER_CHROME_ACTION_LOG_FORWARD;
}

/* slice 5b: engine asked the chrome to fetch a URL on its behalf.
 * The chrome doesn't resolve here -- it just stages the request
 * and signals the runtime to drain via
 * `browser_chrome_take_pending_fetch()`. If a previous fetch is
 * still pending (caller didn't drain), reject as protocol error
 * to avoid silently losing the older request. */
static uint32_t handle_fetch_request(struct browser_chrome *c,
                                     const uint8_t *payload, uint32_t plen) {
    struct browser_ipc_fetch_request req;
    int rc = browser_ipc_fetch_request_decode(payload, plen, &req);
    if (rc != BROWSER_IPC_OK) return BROWSER_CHROME_ACTION_PROTOCOL_ERR;
    if (c->pending_fetch_active) {
        /* Slot busy: engine is supposed to wait for FETCH_RESPONSE
         * before issuing the next request. Surface as protocol err
         * so the watchdog/dispatcher can react. */
        return BROWSER_CHROME_ACTION_PROTOCOL_ERR;
    }
    c->pending_fetch_seq = req.seq;
    c->pending_fetch_nav_id = req.nav_id;
    c->pending_fetch_method = req.method;
    /* Copy the URL into chrome-owned storage so the borrowed pointer
     * from the IPC payload doesn't outlive the buffer. */
    uint16_t take = req.url_len;
    if (take >= BROWSER_CHROME_URL_MAX) {
        take = (uint16_t)(BROWSER_CHROME_URL_MAX - 1u);
    }
    for (uint16_t i = 0; i < take; ++i) {
        c->pending_fetch_url[i] = (char)req.url[i];
    }
    c->pending_fetch_url[take] = '\0';
    c->pending_fetch_url_len = take;
    c->pending_fetch_active = 1u;
    return BROWSER_CHROME_ACTION_FETCH_REQUESTED;
}

int browser_chrome_take_pending_fetch(struct browser_chrome *c,
                                      struct browser_ipc_fetch_request *out) {
    if (!c || !out) return 0;
    if (!c->pending_fetch_active) return 0;
    out->seq = c->pending_fetch_seq;
    out->nav_id = c->pending_fetch_nav_id;
    out->method = c->pending_fetch_method;
    out->url_len = c->pending_fetch_url_len;
    out->url = (const uint8_t *)c->pending_fetch_url;
    c->pending_fetch_active = 0u;
    return 1;
}

uint32_t browser_chrome_build_fetch_response_payload(
    uint32_t seq, uint32_t nav_id, uint16_t status,
    const uint8_t *content_type, uint16_t content_type_len,
    const uint8_t *body, uint32_t body_len,
    uint8_t *out_buf, uint32_t out_size) {
    struct browser_ipc_fetch_response resp;
    resp.seq = seq;
    resp.nav_id = nav_id;
    resp.status = status;
    resp.content_type_len = content_type_len;
    resp.content_type = content_type;
    resp.body_len = body_len;
    resp.body = body;
    uint32_t written = 0u;
    int rc = browser_ipc_fetch_response_encode(&resp, out_buf, out_size,
                                               &written);
    return (rc == BROWSER_IPC_OK) ? written : 0u;
}

uint32_t browser_chrome_dispatch_event(struct browser_chrome *c,
                                       const struct browser_ipc_header *hdr,
                                       const uint8_t *payload,
                                       uint64_t now_ticks) {
    if (!c || !hdr) return BROWSER_CHROME_ACTION_PROTOCOL_ERR;
    if (!browser_ipc_kind_is_event(hdr->kind)) {
        c->total_protocol_errors++;
        return BROWSER_CHROME_ACTION_PROTOCOL_ERR;
    }
    if (hdr->payload_len > 0u && !payload) {
        c->total_protocol_errors++;
        return BROWSER_CHROME_ACTION_PROTOCOL_ERR;
    }

    c->total_events_handled++;
    uint32_t actions = 0u;
    switch (hdr->kind) {
        case BROWSER_IPC_EVENT_TITLE:
            actions = handle_title(c, payload, hdr->payload_len);
            break;
        case BROWSER_IPC_EVENT_NAV_STARTED:
            actions = handle_nav_started(c, payload, hdr->payload_len);
            break;
        case BROWSER_IPC_EVENT_NAV_PROGRESS:
            actions = handle_nav_progress(c, payload, hdr->payload_len);
            break;
        case BROWSER_IPC_EVENT_NAV_READY:
            actions = handle_nav_ready(c, payload, hdr->payload_len);
            break;
        case BROWSER_IPC_EVENT_NAV_FAILED:
            actions = handle_nav_failed(c, payload, hdr->payload_len);
            break;
        case BROWSER_IPC_EVENT_NAV_CANCELLED:
            actions = handle_nav_cancelled(c, payload, hdr->payload_len);
            break;
        case BROWSER_IPC_EVENT_FRAME:
            actions = handle_frame(c, payload, hdr->payload_len);
            break;
        case BROWSER_IPC_EVENT_CURSOR:
            /* Cursor hint: caller pode ler em c->last_frame; nao
             * propaga acao especifica nesta versao. */
            actions = 0u;
            break;
        case BROWSER_IPC_EVENT_PONG:
            actions = handle_pong(c, payload, hdr->payload_len, now_ticks);
            break;
        case BROWSER_IPC_EVENT_LOG:
            actions = handle_log(c, hdr->payload_len, payload);
            break;
        case BROWSER_IPC_EVENT_FETCH_REQUEST:
            actions = handle_fetch_request(c, payload, hdr->payload_len);
            break;
        default:
            actions = BROWSER_CHROME_ACTION_PROTOCOL_ERR;
            break;
    }
    if (actions & BROWSER_CHROME_ACTION_PROTOCOL_ERR) {
        c->total_protocol_errors++;
    }
    return actions;
}
