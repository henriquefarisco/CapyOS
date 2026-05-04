/* src/apps/browser_ipc/fetch.c -- F3.3c slice 5.
 *
 * Encode/decode helpers for the fetch IPC payloads carried by
 * EVENT_FETCH_REQUEST (engine -> chrome) and FETCH_RESPONSE
 * (chrome -> engine). The header itself is encoded by codec.c;
 * these helpers cover only the per-kind payload bodies.
 *
 * Free of kernel/libc symbols (uses only <stdint.h>) so it compiles
 * unchanged in ring 0 and ring 3. The decoded structs hold borrowed
 * pointers into the caller's input buffer; the caller is responsible
 * for keeping that buffer alive while the struct is in use.
 */

#include "apps/browser_ipc.h"
#include <stdint.h>

/* --- BE primitives ------------------------------------------------- */

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

static uint16_t be_get_u16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t be_get_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

/* --- FETCH_REQUEST ------------------------------------------------- */

#define FETCH_REQ_FIXED 11u  /* seq(4) + nav_id(4) + method(1) + url_len(2) */

int browser_ipc_fetch_request_encode(const struct browser_ipc_fetch_request *req,
                                     uint8_t *out, uint32_t out_size,
                                     uint32_t *out_written) {
    if (!req || !out) return BROWSER_IPC_ERR_INVAL;
    if (req->url_len > 0u && !req->url) return BROWSER_IPC_ERR_INVAL;
    if (req->method != BROWSER_IPC_FETCH_GET &&
        req->method != BROWSER_IPC_FETCH_POST) {
        return BROWSER_IPC_ERR_INVAL;
    }
    uint32_t need = FETCH_REQ_FIXED + (uint32_t)req->url_len;
    if (need > BROWSER_IPC_MAX_PAYLOAD) return BROWSER_IPC_ERR_PAYLOAD;
    if (out_size < need) return BROWSER_IPC_ERR_SHORT;

    be_put_u32(&out[0], req->seq);
    be_put_u32(&out[4], req->nav_id);
    out[8] = req->method;
    be_put_u16(&out[9], req->url_len);
    for (uint32_t i = 0; i < (uint32_t)req->url_len; i++) {
        out[FETCH_REQ_FIXED + i] = req->url[i];
    }
    if (out_written) *out_written = need;
    return BROWSER_IPC_OK;
}

int browser_ipc_fetch_request_decode(const uint8_t *in, uint32_t in_size,
                                     struct browser_ipc_fetch_request *req) {
    if (!in || !req) return BROWSER_IPC_ERR_INVAL;
    if (in_size < FETCH_REQ_FIXED) return BROWSER_IPC_ERR_SHORT;
    req->seq    = be_get_u32(&in[0]);
    req->nav_id = be_get_u32(&in[4]);
    req->method = in[8];
    if (req->method != BROWSER_IPC_FETCH_GET &&
        req->method != BROWSER_IPC_FETCH_POST) {
        return BROWSER_IPC_ERR_KIND;
    }
    req->url_len = be_get_u16(&in[9]);
    if ((uint32_t)FETCH_REQ_FIXED + (uint32_t)req->url_len > in_size) {
        return BROWSER_IPC_ERR_PAYLOAD;
    }
    req->url = (req->url_len > 0u) ? &in[FETCH_REQ_FIXED] : (const uint8_t *)0;
    return BROWSER_IPC_OK;
}

/* --- FETCH_RESPONSE ------------------------------------------------ */

#define FETCH_RESP_FIXED 16u /* seq(4)+nav(4)+status(2)+ctype_len(2)+body_len(4) */

int browser_ipc_fetch_response_encode(const struct browser_ipc_fetch_response *resp,
                                      uint8_t *out, uint32_t out_size,
                                      uint32_t *out_written) {
    if (!resp || !out) return BROWSER_IPC_ERR_INVAL;
    if (resp->content_type_len > 0u && !resp->content_type) {
        return BROWSER_IPC_ERR_INVAL;
    }
    if (resp->body_len > 0u && !resp->body) return BROWSER_IPC_ERR_INVAL;
    uint64_t need = (uint64_t)FETCH_RESP_FIXED
                  + (uint64_t)resp->content_type_len
                  + (uint64_t)resp->body_len;
    if (need > (uint64_t)BROWSER_IPC_MAX_PAYLOAD) {
        return BROWSER_IPC_ERR_PAYLOAD;
    }
    if ((uint64_t)out_size < need) return BROWSER_IPC_ERR_SHORT;

    be_put_u32(&out[0], resp->seq);
    be_put_u32(&out[4], resp->nav_id);
    be_put_u16(&out[8], resp->status);
    be_put_u16(&out[10], resp->content_type_len);
    be_put_u32(&out[12], resp->body_len);
    uint32_t off = FETCH_RESP_FIXED;
    for (uint32_t i = 0; i < (uint32_t)resp->content_type_len; i++) {
        out[off + i] = resp->content_type[i];
    }
    off += (uint32_t)resp->content_type_len;
    for (uint32_t i = 0; i < resp->body_len; i++) {
        out[off + i] = resp->body[i];
    }
    if (out_written) *out_written = (uint32_t)need;
    return BROWSER_IPC_OK;
}

int browser_ipc_fetch_response_decode(const uint8_t *in, uint32_t in_size,
                                      struct browser_ipc_fetch_response *resp) {
    if (!in || !resp) return BROWSER_IPC_ERR_INVAL;
    if (in_size < FETCH_RESP_FIXED) return BROWSER_IPC_ERR_SHORT;
    resp->seq             = be_get_u32(&in[0]);
    resp->nav_id          = be_get_u32(&in[4]);
    resp->status          = be_get_u16(&in[8]);
    resp->content_type_len = be_get_u16(&in[10]);
    resp->body_len        = be_get_u32(&in[12]);
    uint64_t need = (uint64_t)FETCH_RESP_FIXED
                  + (uint64_t)resp->content_type_len
                  + (uint64_t)resp->body_len;
    if (need > (uint64_t)in_size) return BROWSER_IPC_ERR_PAYLOAD;

    uint32_t off = FETCH_RESP_FIXED;
    resp->content_type = (resp->content_type_len > 0u) ? &in[off]
                                                       : (const uint8_t *)0;
    off += (uint32_t)resp->content_type_len;
    resp->body = (resp->body_len > 0u) ? &in[off] : (const uint8_t *)0;
    return BROWSER_IPC_OK;
}
