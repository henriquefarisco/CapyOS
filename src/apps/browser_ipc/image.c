/* src/apps/browser_ipc/image.c -- Etapa 3 secao a fetch+decode (2026-05-05).
 *
 * Encode/decode helpers for the image IPC payloads carried by
 * EVENT_IMAGE_REQUEST (engine -> chrome) and IMAGE_RESPONSE
 * (chrome -> engine). The header itself is encoded by codec.c;
 * these helpers cover only the per-kind payload bodies.
 *
 * Free of kernel/libc symbols (uses only <stdint.h>) so it compiles
 * unchanged in ring 0 and ring 3. Decoded structs hold borrowed
 * pointers into the caller's input buffer.
 *
 * Wire shapes (mirrored from include/apps/browser_ipc.h):
 *
 *   IMAGE_REQUEST payload (10 + url_len bytes):
 *     [0..3]   img_id u32 BE
 *     [4..7]   nav_id u32 BE
 *     [8..9]   url_len u16 BE
 *     [10..]   url utf8
 *
 *   IMAGE_RESPONSE payload (18 + pixel_bytes bytes):
 *     [0..3]   img_id u32 BE
 *     [4..7]   nav_id u32 BE
 *     [8]      status u8
 *     [9]      format u8
 *     [10..11] width u16 BE
 *     [12..13] height u16 BE
 *     [14..17] pixel_bytes u32 BE
 *     [18..]   pixels
 */

#include "apps/browser_ipc.h"
#include <stdint.h>

/* --- BE primitives (duplicated from fetch.c on purpose; both TUs
 *     stay header-free of helpers from each other to keep the
 *     build graph trivially flat). ---------------------------------- */

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

/* --- IMAGE_REQUEST ------------------------------------------------- */

#define IMAGE_REQ_FIXED 10u  /* img_id(4) + nav_id(4) + url_len(2) */

int browser_ipc_image_request_encode(const struct browser_ipc_image_request *req,
                                     uint8_t *out, uint32_t out_size,
                                     uint32_t *out_written) {
    if (!req || !out) return BROWSER_IPC_ERR_INVAL;
    if (req->url_len > 0u && !req->url) return BROWSER_IPC_ERR_INVAL;
    uint64_t need = (uint64_t)IMAGE_REQ_FIXED + (uint64_t)req->url_len;
    if (need > (uint64_t)BROWSER_IPC_MAX_PAYLOAD) {
        return BROWSER_IPC_ERR_PAYLOAD;
    }
    if ((uint64_t)out_size < need) return BROWSER_IPC_ERR_SHORT;

    be_put_u32(&out[0], req->img_id);
    be_put_u32(&out[4], req->nav_id);
    be_put_u16(&out[8], req->url_len);
    for (uint32_t i = 0; i < (uint32_t)req->url_len; i++) {
        out[IMAGE_REQ_FIXED + i] = req->url[i];
    }
    if (out_written) *out_written = (uint32_t)need;
    return BROWSER_IPC_OK;
}

int browser_ipc_image_request_decode(const uint8_t *in, uint32_t in_size,
                                     struct browser_ipc_image_request *req) {
    if (!in || !req) return BROWSER_IPC_ERR_INVAL;
    if (in_size < IMAGE_REQ_FIXED) return BROWSER_IPC_ERR_SHORT;
    req->img_id  = be_get_u32(&in[0]);
    req->nav_id  = be_get_u32(&in[4]);
    req->url_len = be_get_u16(&in[8]);
    if ((uint64_t)IMAGE_REQ_FIXED + (uint64_t)req->url_len > (uint64_t)in_size) {
        return BROWSER_IPC_ERR_PAYLOAD;
    }
    req->url = (req->url_len > 0u) ? &in[IMAGE_REQ_FIXED] : (const uint8_t *)0;
    return BROWSER_IPC_OK;
}

/* --- IMAGE_RESPONSE ------------------------------------------------ */

#define IMAGE_RESP_FIXED 18u
/* img_id(4) + nav_id(4) + status(1) + format(1) + w(2) + h(2) +
 * pixel_bytes(4) */

int browser_ipc_image_response_encode(const struct browser_ipc_image_response *resp,
                                      uint8_t *out, uint32_t out_size,
                                      uint32_t *out_written) {
    if (!resp || !out) return BROWSER_IPC_ERR_INVAL;
    if (resp->pixel_bytes > 0u && !resp->pixels) {
        return BROWSER_IPC_ERR_INVAL;
    }
    /* Defensive consistency check: pixel_bytes deve bater com w*h*4
     * para BGRA32. Para os outros status (erro), w=h=pixel_bytes=0,
     * que tambem casa. Tolerante a status de erro com w/h zero
     * mesmo que o caller esqueca de zerar pixel_bytes. */
    if (resp->status == (uint8_t)BROWSER_IPC_IMAGE_OK &&
        resp->format == (uint8_t)BROWSER_IPC_IMAGE_FMT_BGRA32) {
        uint64_t expect = (uint64_t)resp->width
                        * (uint64_t)resp->height * 4u;
        if ((uint64_t)resp->pixel_bytes != expect) {
            return BROWSER_IPC_ERR_INVAL;
        }
    }
    uint64_t need = (uint64_t)IMAGE_RESP_FIXED + (uint64_t)resp->pixel_bytes;
    if (need > (uint64_t)BROWSER_IPC_MAX_PAYLOAD) {
        return BROWSER_IPC_ERR_PAYLOAD;
    }
    if ((uint64_t)out_size < need) return BROWSER_IPC_ERR_SHORT;

    be_put_u32(&out[0], resp->img_id);
    be_put_u32(&out[4], resp->nav_id);
    out[8]  = resp->status;
    out[9]  = resp->format;
    be_put_u16(&out[10], resp->width);
    be_put_u16(&out[12], resp->height);
    be_put_u32(&out[14], resp->pixel_bytes);
    for (uint32_t i = 0; i < resp->pixel_bytes; i++) {
        out[IMAGE_RESP_FIXED + i] = resp->pixels[i];
    }
    if (out_written) *out_written = (uint32_t)need;
    return BROWSER_IPC_OK;
}

int browser_ipc_image_response_decode(const uint8_t *in, uint32_t in_size,
                                      struct browser_ipc_image_response *resp) {
    if (!in || !resp) return BROWSER_IPC_ERR_INVAL;
    if (in_size < IMAGE_RESP_FIXED) return BROWSER_IPC_ERR_SHORT;
    resp->img_id      = be_get_u32(&in[0]);
    resp->nav_id      = be_get_u32(&in[4]);
    resp->status      = in[8];
    resp->format      = in[9];
    resp->width       = be_get_u16(&in[10]);
    resp->height      = be_get_u16(&in[12]);
    resp->pixel_bytes = be_get_u32(&in[14]);
    uint64_t need = (uint64_t)IMAGE_RESP_FIXED
                  + (uint64_t)resp->pixel_bytes;
    if (need > (uint64_t)in_size) return BROWSER_IPC_ERR_PAYLOAD;

    resp->pixels = (resp->pixel_bytes > 0u) ? &in[IMAGE_RESP_FIXED]
                                            : (const uint8_t *)0;

    /* Self-consistency: para status OK + BGRA32, exige pixel_bytes
     * = w*h*4. Diverge -> protocolo violado. */
    if (resp->status == (uint8_t)BROWSER_IPC_IMAGE_OK &&
        resp->format == (uint8_t)BROWSER_IPC_IMAGE_FMT_BGRA32) {
        uint64_t expect = (uint64_t)resp->width
                        * (uint64_t)resp->height * 4u;
        if ((uint64_t)resp->pixel_bytes != expect) {
            return BROWSER_IPC_ERR_PAYLOAD;
        }
    }
    return BROWSER_IPC_OK;
}
