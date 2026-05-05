/* tests/test_browser_ipc_image.c -- Etapa 3 secao a fetch+decode (2026-05-05).
 *
 * Validates the encode/decode helpers for the image IPC payloads:
 *
 *   - Round-trip of EVENT_IMAGE_REQUEST (with and without URL).
 *   - Round-trip of IMAGE_RESPONSE (with BGRA pixels and error status).
 *   - Argument validation (NULL pointers, undersized output buffer).
 *   - Self-consistency: status=OK + format=BGRA32 must have
 *     pixel_bytes = w*h*4 (encoder rejects mismatch; decoder rejects
 *     truncated payload).
 *   - Codec direction predicates: IMAGE_RESPONSE counts as request
 *     (chrome->engine), EVENT_IMAGE_REQUEST counts as event
 *     (engine->chrome).
 *   - Min-payload table reports the right fixed-size prefix.
 */

#include "apps/browser_ipc.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_passed = 0;
static int g_failed = 0;

#define I_OK(cond, msg) do {                                              \
    if (cond) { g_passed++; }                                             \
    else { g_failed++; printf("  FAIL %s\n", msg); }                      \
} while (0)

/* === EVENT_IMAGE_REQUEST ============================================ */

static void test_request_roundtrip_with_url(void) {
    const uint8_t url[] = "http://capyos/img/logo.png";
    struct browser_ipc_image_request req = {
        .img_id = 0x11223344u,
        .nav_id = 42u,
        .url_len = (uint16_t)(sizeof(url) - 1u),
        .url = url
    };
    uint8_t buf[64];
    uint32_t n = 0;
    int rc = browser_ipc_image_request_encode(&req, buf, sizeof(buf), &n);
    I_OK(rc == BROWSER_IPC_OK, "IMAGE_REQUEST encode ok");
    I_OK(n == 10u + sizeof(url) - 1u, "IMAGE_REQUEST size = fixed + url_len");

    struct browser_ipc_image_request out;
    memset(&out, 0xAA, sizeof(out));
    rc = browser_ipc_image_request_decode(buf, n, &out);
    I_OK(rc == BROWSER_IPC_OK, "IMAGE_REQUEST decode ok");
    I_OK(out.img_id == 0x11223344u, "img_id round-tripped");
    I_OK(out.nav_id == 42u, "nav_id round-tripped");
    I_OK(out.url_len == sizeof(url) - 1u, "url_len round-tripped");
    I_OK(out.url != NULL && memcmp(out.url, url, sizeof(url) - 1u) == 0,
         "url bytes round-tripped");
}

static void test_request_roundtrip_empty_url(void) {
    struct browser_ipc_image_request req = {0};
    req.img_id = 1u;
    req.nav_id = 2u;
    uint8_t buf[16];
    uint32_t n = 0;
    int rc = browser_ipc_image_request_encode(&req, buf, sizeof(buf), &n);
    I_OK(rc == BROWSER_IPC_OK, "empty url encode ok");
    I_OK(n == 10u, "empty url -> fixed-size only");

    struct browser_ipc_image_request out;
    rc = browser_ipc_image_request_decode(buf, n, &out);
    I_OK(rc == BROWSER_IPC_OK, "empty url decode ok");
    I_OK(out.url_len == 0u, "decoded url_len=0");
    I_OK(out.url == NULL, "decoded url ptr NULL when empty");
}

static void test_request_validation(void) {
    struct browser_ipc_image_request req = {0};
    uint8_t buf[64];
    I_OK(browser_ipc_image_request_encode(NULL, buf, sizeof(buf), NULL)
         == BROWSER_IPC_ERR_INVAL, "NULL req rejected");
    I_OK(browser_ipc_image_request_encode(&req, NULL, 64, NULL)
         == BROWSER_IPC_ERR_INVAL, "NULL out rejected");

    /* url_len > 0 but url ptr NULL -> INVAL */
    req.url_len = 5u;
    req.url = NULL;
    I_OK(browser_ipc_image_request_encode(&req, buf, sizeof(buf), NULL)
         == BROWSER_IPC_ERR_INVAL, "url_len>0 sem url -> INVAL");

    /* Buffer too small */
    const uint8_t url[] = "abc";
    req.url_len = 3u;
    req.url = url;
    I_OK(browser_ipc_image_request_encode(&req, buf, 5u, NULL)
         == BROWSER_IPC_ERR_SHORT, "out_size<need -> SHORT");

    /* Decode: SHORT quando in_size < FIXED */
    uint8_t small[5] = {0};
    struct browser_ipc_image_request out;
    I_OK(browser_ipc_image_request_decode(small, sizeof(small), &out)
         == BROWSER_IPC_ERR_SHORT, "decode in_size<FIXED -> SHORT");

    /* Decode: PAYLOAD quando url_len overflow in_size */
    uint8_t buf2[12] = {0};
    /* Fixed prefix declarando url_len = 100 mas so temos 2 bytes
     * de tail. */
    buf2[8] = 0u; buf2[9] = 100u; /* url_len BE = 100 */
    I_OK(browser_ipc_image_request_decode(buf2, sizeof(buf2), &out)
         == BROWSER_IPC_ERR_PAYLOAD, "url_len overflow -> PAYLOAD");
}

/* === IMAGE_RESPONSE ================================================= */

static void test_response_roundtrip_ok_pixels(void) {
    /* 2x2 BGRA imagem: 16 bytes de pixels. */
    static const uint8_t pixels[16] = {
        0x10, 0x20, 0x30, 0xFF,   /* B G R A */
        0x40, 0x50, 0x60, 0xFF,
        0x70, 0x80, 0x90, 0xFF,
        0xA0, 0xB0, 0xC0, 0xFF
    };
    struct browser_ipc_image_response resp = {
        .img_id = 0xDEADBEEFu,
        .nav_id = 9u,
        .status = BROWSER_IPC_IMAGE_OK,
        .format = BROWSER_IPC_IMAGE_FMT_BGRA32,
        .width = 2u,
        .height = 2u,
        .pixel_bytes = sizeof(pixels),
        .pixels = pixels
    };
    uint8_t buf[64];
    uint32_t n = 0;
    int rc = browser_ipc_image_response_encode(&resp, buf, sizeof(buf), &n);
    I_OK(rc == BROWSER_IPC_OK, "IMAGE_RESPONSE encode ok");
    I_OK(n == 18u + sizeof(pixels), "IMAGE_RESPONSE size = fixed + pixel_bytes");

    struct browser_ipc_image_response out;
    memset(&out, 0xAA, sizeof(out));
    rc = browser_ipc_image_response_decode(buf, n, &out);
    I_OK(rc == BROWSER_IPC_OK, "IMAGE_RESPONSE decode ok");
    I_OK(out.img_id == 0xDEADBEEFu, "img_id round-tripped");
    I_OK(out.nav_id == 9u, "nav_id round-tripped");
    I_OK(out.status == BROWSER_IPC_IMAGE_OK, "status round-tripped");
    I_OK(out.format == BROWSER_IPC_IMAGE_FMT_BGRA32, "format round-tripped");
    I_OK(out.width == 2u && out.height == 2u, "w/h round-tripped");
    I_OK(out.pixel_bytes == sizeof(pixels), "pixel_bytes round-tripped");
    I_OK(out.pixels != NULL && memcmp(out.pixels, pixels, sizeof(pixels)) == 0,
         "pixels round-tripped");
}

static void test_response_roundtrip_error_status(void) {
    /* Status de erro: w=h=pixel_bytes=0; pixels=NULL valido. */
    struct browser_ipc_image_response resp = {
        .img_id = 1u,
        .nav_id = 2u,
        .status = BROWSER_IPC_IMAGE_TRANSPORT_ERR,
        .format = BROWSER_IPC_IMAGE_FMT_BGRA32,
        .width = 0u,
        .height = 0u,
        .pixel_bytes = 0u,
        .pixels = NULL
    };
    uint8_t buf[32];
    uint32_t n = 0;
    int rc = browser_ipc_image_response_encode(&resp, buf, sizeof(buf), &n);
    I_OK(rc == BROWSER_IPC_OK, "error status encode ok");
    I_OK(n == 18u, "error status -> fixed size only");

    struct browser_ipc_image_response out;
    rc = browser_ipc_image_response_decode(buf, n, &out);
    I_OK(rc == BROWSER_IPC_OK, "error status decode ok");
    I_OK(out.status == BROWSER_IPC_IMAGE_TRANSPORT_ERR,
         "transport_err status preserved");
    I_OK(out.pixels == NULL, "error status -> pixels NULL");
}

static void test_response_inconsistent_size_rejected(void) {
    /* Status OK + format BGRA32 mas pixel_bytes != w*h*4 -> INVAL no
     * encode (falha rapida) e PAYLOAD no decode (de payload externo
     * malicioso). */
    static const uint8_t pixels[8] = {0};
    struct browser_ipc_image_response resp = {
        .img_id = 1u,
        .nav_id = 2u,
        .status = BROWSER_IPC_IMAGE_OK,
        .format = BROWSER_IPC_IMAGE_FMT_BGRA32,
        .width = 2u,
        .height = 2u, /* expect 16 bytes */
        .pixel_bytes = 8u, /* WRONG */
        .pixels = pixels
    };
    uint8_t buf[32];
    int rc = browser_ipc_image_response_encode(&resp, buf, sizeof(buf), NULL);
    I_OK(rc == BROWSER_IPC_ERR_INVAL,
         "encode rejeita pixel_bytes != w*h*4 com status OK");

    /* Decode: forja um buffer com header dizendo w=h=2 mas
     * pixel_bytes=8 e suficiente bytes presentes. Decoder deve
     * rejeitar como protocol error. */
    uint8_t bad[32] = {0};
    bad[8] = (uint8_t)BROWSER_IPC_IMAGE_OK;
    bad[9] = (uint8_t)BROWSER_IPC_IMAGE_FMT_BGRA32;
    bad[10] = 0; bad[11] = 2; /* width = 2 */
    bad[12] = 0; bad[13] = 2; /* height = 2 */
    bad[14] = 0; bad[15] = 0; bad[16] = 0; bad[17] = 8; /* pixel_bytes = 8 */
    /* tail 8 bytes ja zerados */
    struct browser_ipc_image_response out;
    rc = browser_ipc_image_response_decode(bad, 18u + 8u, &out);
    I_OK(rc == BROWSER_IPC_ERR_PAYLOAD,
         "decode rejeita inconsistencia de tamanho");
}

static void test_response_validation(void) {
    struct browser_ipc_image_response resp = {0};
    uint8_t buf[32];
    I_OK(browser_ipc_image_response_encode(NULL, buf, sizeof(buf), NULL)
         == BROWSER_IPC_ERR_INVAL, "NULL resp rejected");
    I_OK(browser_ipc_image_response_encode(&resp, NULL, 32, NULL)
         == BROWSER_IPC_ERR_INVAL, "NULL out rejected");

    /* pixel_bytes > 0 mas pixels NULL -> INVAL */
    resp.pixel_bytes = 4u;
    resp.pixels = NULL;
    I_OK(browser_ipc_image_response_encode(&resp, buf, sizeof(buf), NULL)
         == BROWSER_IPC_ERR_INVAL, "pixel_bytes>0 sem pixels -> INVAL");
}

static void test_response_payload_too_big_rejected(void) {
    /* Tenta codificar pixel_bytes ~= MAX_PAYLOAD pra forcar PAYLOAD.
     * Sem allocar 1 MiB no test: como pixels=NULL com pixel_bytes>0
     * dá INVAL antes, eu uso um pixels arr pequeno + status erro
     * + falsifico pixel_bytes (mas o encode primeiro faz consistency
     * check). Vou usar status nao-OK para escapar do consistency. */
    static const uint8_t dummy[4] = {0};
    struct browser_ipc_image_response resp = {
        .status = BROWSER_IPC_IMAGE_DECODE_ERR,
        .format = BROWSER_IPC_IMAGE_FMT_BGRA32,
        .width = 0u,
        .height = 0u,
        .pixel_bytes = (uint32_t)BROWSER_IPC_MAX_PAYLOAD, /* exato no limite */
        .pixels = dummy
    };
    uint8_t buf[32];
    int rc = browser_ipc_image_response_encode(&resp, buf, sizeof(buf), NULL);
    /* fixed (18) + max_payload (1 MiB) > MAX_PAYLOAD => PAYLOAD. */
    I_OK(rc == BROWSER_IPC_ERR_PAYLOAD,
         "pixel_bytes >= MAX_PAYLOAD -> PAYLOAD err");
}

/* === Codec integration ============================================== */

static void test_codec_direction_predicates(void) {
    I_OK(browser_ipc_kind_is_request(BROWSER_IPC_IMAGE_RESPONSE) == 1,
         "IMAGE_RESPONSE classified as request (chrome->engine)");
    I_OK(browser_ipc_kind_is_event(BROWSER_IPC_EVENT_IMAGE_REQUEST) == 1,
         "EVENT_IMAGE_REQUEST classified as event (engine->chrome)");
    I_OK(browser_ipc_kind_is_known(BROWSER_IPC_IMAGE_RESPONSE) == 1,
         "IMAGE_RESPONSE known to codec");
    I_OK(browser_ipc_kind_is_known(BROWSER_IPC_EVENT_IMAGE_REQUEST) == 1,
         "EVENT_IMAGE_REQUEST known to codec");
}

static void test_codec_min_payload(void) {
    I_OK(browser_ipc_kind_min_payload(BROWSER_IPC_EVENT_IMAGE_REQUEST) == 10u,
         "IMAGE_REQUEST min payload = 10 (fixed prefix)");
    I_OK(browser_ipc_kind_min_payload(BROWSER_IPC_IMAGE_RESPONSE) == 18u,
         "IMAGE_RESPONSE min payload = 18 (fixed prefix)");
}

int test_browser_ipc_image_run(void) {
    printf("[test_browser_ipc_image]\n");
    g_passed = 0;
    g_failed = 0;
    test_request_roundtrip_with_url();
    test_request_roundtrip_empty_url();
    test_request_validation();
    test_response_roundtrip_ok_pixels();
    test_response_roundtrip_error_status();
    test_response_inconsistent_size_rejected();
    test_response_validation();
    test_response_payload_too_big_rejected();
    test_codec_direction_predicates();
    test_codec_min_payload();
    printf("  -> %d/%d passed\n", g_passed, g_passed + g_failed);
    return g_failed;
}
