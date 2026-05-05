/*
 * src/apps/browser_chrome/runtime_image.c
 *
 * Etapa 3 secao a fetch+decode: drena IMAGE_REQUEST do chrome runtime,
 * baixa a URL no lado kernel/chrome, decodifica PNG/JPEG e responde ao
 * engine com pixels BGRA32 ou erro tipado.
 */

#include "apps/browser_chrome_runtime.h"

#ifndef UNIT_TEST
#include "net/http.h"
#include "net/stack.h"
#include "gui/png_loader.h"
#include "gui/jpeg_loader.h"
#endif

#define CHROME_RUNTIME_IMAGE_PAYLOAD_MAX (1u * 1024u * 1024u + 4096u)
#define CHROME_RUNTIME_IMAGE_MAX_W       240u
#define CHROME_RUNTIME_IMAGE_MAX_H       180u

static uint8_t g_image_response_scratch[CHROME_RUNTIME_IMAGE_PAYLOAD_MAX];

#ifndef UNIT_TEST
static char g_image_url_scratch[HTTP_MAX_URL];

static int image_url_has_prefix(const uint8_t *url, uint16_t url_len,
                                const char *prefix) {
    uint32_t i = 0;
    while (prefix[i] != '\0') {
        if ((uint32_t)i >= (uint32_t)url_len) return 0;
        if ((char)url[i] != prefix[i]) return 0;
        i++;
    }
    return 1;
}

static int image_url_is_http(const uint8_t *url, uint16_t url_len) {
    return image_url_has_prefix(url, url_len, "http://")
        || image_url_has_prefix(url, url_len, "https://");
}

static int fetch_image_body(const uint8_t *url, uint16_t url_len,
                            struct http_response *resp,
                            int *out_used_http,
                            const uint8_t **out_body,
                            uint32_t *out_body_len) {
    if (!url || !resp || !out_used_http || !out_body || !out_body_len) {
        return 0;
    }
    *out_used_http = 0;
    if (url_len == 0u || (uint32_t)url_len >= sizeof(g_image_url_scratch)) {
        return 0;
    }
    for (uint16_t i = 0; i < url_len; ++i) {
        g_image_url_scratch[i] = (char)url[i];
    }
    g_image_url_scratch[url_len] = '\0';

    if (!net_stack_ready()) return 0;
    if (http_get(g_image_url_scratch, resp) != 0) return 0;
    *out_used_http = 1;
    if (resp->status_code < 200 || resp->status_code >= 300) return 0;
    *out_body = resp->body;
    *out_body_len = (resp->body_len > BROWSER_IPC_MAX_PAYLOAD)
        ? BROWSER_IPC_MAX_PAYLOAD : (uint32_t)resp->body_len;
    return (*out_body && *out_body_len > 0u) ? 1 : 0;
}

static int detect_image_format(const uint8_t *body, uint32_t len) {
    if (!body || len < 4u) return -1;
    if (body[0] == 0x89u && body[1] == 0x50u
        && body[2] == 0x4Eu && body[3] == 0x47u) return 1;
    if (body[0] == 0xFFu && body[1] == 0xD8u && body[2] == 0xFFu) return 2;
    return -1;
}

static const uint8_t *argb_pixels_as_bgra(const uint32_t *pixels) {
    return (const uint8_t *)pixels;
}
#endif

int chrome_runtime_dispatch_pending_image(struct chrome_runtime *rt) {
    if (!rt) return -1;
    struct browser_ipc_image_request req;
    if (!browser_chrome_take_pending_image(&rt->chrome, &req)) {
        return 0;
    }

    uint8_t status = (uint8_t)BROWSER_IPC_IMAGE_TRANSPORT_ERR;
    uint16_t width = 0u, height = 0u;
    uint32_t pixel_bytes = 0u;
    const uint8_t *pixels_ptr = (const uint8_t *)0;

#ifndef UNIT_TEST
    struct png_image png_img = {0};
    struct jpeg_image jpeg_img = {0};
    int decoded_png = 0, decoded_jpeg = 0, used_http = 0;
    struct http_response http_resp;
    if (image_url_is_http((const uint8_t *)req.url, req.url_len)) {
        const uint8_t *body = (const uint8_t *)0;
        uint32_t body_len = 0u;
        if (fetch_image_body((const uint8_t *)req.url, req.url_len,
                             &http_resp, &used_http, &body, &body_len)) {
            int fmt = detect_image_format(body, body_len);
            if (fmt == 1 && png_decode(body, (size_t)body_len, &png_img) == 0) {
                decoded_png = 1;
                if (png_img.width > CHROME_RUNTIME_IMAGE_MAX_W ||
                    png_img.height > CHROME_RUNTIME_IMAGE_MAX_H) {
                    status = (uint8_t)BROWSER_IPC_IMAGE_OVERSIZED;
                } else {
                    width = (uint16_t)png_img.width;
                    height = (uint16_t)png_img.height;
                    pixel_bytes = (uint32_t)width * (uint32_t)height * 4u;
                    pixels_ptr = argb_pixels_as_bgra(png_img.pixels);
                    status = (uint8_t)BROWSER_IPC_IMAGE_OK;
                }
            } else if (fmt == 2 &&
                       jpeg_decode(body, (size_t)body_len, &jpeg_img) == 0) {
                decoded_jpeg = 1;
                if (jpeg_img.width > CHROME_RUNTIME_IMAGE_MAX_W ||
                    jpeg_img.height > CHROME_RUNTIME_IMAGE_MAX_H) {
                    status = (uint8_t)BROWSER_IPC_IMAGE_OVERSIZED;
                } else {
                    width = (uint16_t)jpeg_img.width;
                    height = (uint16_t)jpeg_img.height;
                    pixel_bytes = (uint32_t)width * (uint32_t)height * 4u;
                    pixels_ptr = argb_pixels_as_bgra(jpeg_img.pixels);
                    status = (uint8_t)BROWSER_IPC_IMAGE_OK;
                }
            } else {
                status = (fmt < 0) ? (uint8_t)BROWSER_IPC_IMAGE_UNSUPPORTED
                                   : (uint8_t)BROWSER_IPC_IMAGE_DECODE_ERR;
            }
        }
    } else {
        status = (uint8_t)BROWSER_IPC_IMAGE_UNSUPPORTED;
    }
#else
    status = (uint8_t)BROWSER_IPC_IMAGE_UNSUPPORTED;
#endif

    struct browser_ipc_image_response resp;
    resp.img_id = req.img_id;
    resp.nav_id = req.nav_id;
    resp.status = status;
    resp.format = (uint8_t)BROWSER_IPC_IMAGE_FMT_BGRA32;
    resp.width = width;
    resp.height = height;
    resp.pixel_bytes = pixel_bytes;
    resp.pixels = pixels_ptr;

    uint32_t n = 0u;
    if (browser_ipc_image_response_encode(&resp, g_image_response_scratch,
                                          sizeof(g_image_response_scratch),
                                          &n) != BROWSER_IPC_OK) {
        resp.status = (uint8_t)BROWSER_IPC_IMAGE_DECODE_ERR;
        resp.width = 0u; resp.height = 0u;
        resp.pixel_bytes = 0u; resp.pixels = (const uint8_t *)0;
        n = 0u;
        if (browser_ipc_image_response_encode(&resp, g_image_response_scratch,
                                              sizeof(g_image_response_scratch),
                                              &n) != BROWSER_IPC_OK) {
#ifndef UNIT_TEST
            if (decoded_png) png_free(&png_img);
            if (decoded_jpeg) jpeg_free(&jpeg_img);
            if (used_http) http_response_free(&http_resp);
#endif
            return -1;
        }
    }

    int sr = chrome_runtime_send_ipc_frame(rt, BROWSER_IPC_IMAGE_RESPONSE,
                                           g_image_response_scratch, n);
#ifndef UNIT_TEST
    if (decoded_png) png_free(&png_img);
    if (decoded_jpeg) jpeg_free(&jpeg_img);
    if (used_http) http_response_free(&http_resp);
#endif
    if (sr != 0) {
        rt->engine_alive = 0;
        return -1;
    }
    capyc_audit_record(&rt->audit, (uint8_t)CAPYC_AUDIT_FETCH,
                       (uint16_t)status);
    return 1;
}
