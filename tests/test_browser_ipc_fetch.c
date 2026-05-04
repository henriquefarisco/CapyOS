/* tests/test_browser_ipc_fetch.c -- F3.3c slice 5 host coverage.
 *
 * Validates the encode/decode helpers for the fetch IPC payloads:
 *
 *   - Round-trip of FETCH_REQUEST (with and without URL).
 *   - Round-trip of FETCH_RESPONSE (with and without body).
 *   - Argument validation (NULL pointers, missing buffers,
 *     undersized output buffers, invalid method).
 *   - Codec direction predicates: FETCH_RESPONSE counts as a
 *     request (chrome -> engine), EVENT_FETCH_REQUEST counts as
 *     an event (engine -> chrome).
 *   - Min-payload table reports the right fixed-size prefix.
 */

#include "apps/browser_ipc.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_passed = 0;
static int g_failed = 0;

#define F_OK(cond, msg) do {                                              \
    if (cond) { g_passed++; }                                             \
    else { g_failed++; printf("  FAIL %s\n", msg); }                      \
} while (0)

static void test_request_roundtrip_with_url(void) {
    const uint8_t url[] = "about:blank";
    struct browser_ipc_fetch_request req = {
        .seq = 0xCAFEBABEu,
        .nav_id = 7u,
        .method = BROWSER_IPC_FETCH_GET,
        .url_len = (uint16_t)(sizeof(url) - 1u),
        .url = url
    };
    uint8_t buf[64];
    uint32_t n = 0;
    int rc = browser_ipc_fetch_request_encode(&req, buf, sizeof(buf), &n);
    F_OK(rc == BROWSER_IPC_OK, "FETCH_REQUEST encode ok");
    F_OK(n == 11u + sizeof(url) - 1u, "FETCH_REQUEST encoded size");

    struct browser_ipc_fetch_request out;
    memset(&out, 0xAA, sizeof(out));
    rc = browser_ipc_fetch_request_decode(buf, n, &out);
    F_OK(rc == BROWSER_IPC_OK, "FETCH_REQUEST decode ok");
    F_OK(out.seq == 0xCAFEBABEu, "seq round-tripped");
    F_OK(out.nav_id == 7u, "nav_id round-tripped");
    F_OK(out.method == BROWSER_IPC_FETCH_GET, "method round-tripped");
    F_OK(out.url_len == sizeof(url) - 1u, "url_len round-tripped");
    F_OK(out.url != NULL && memcmp(out.url, url, sizeof(url) - 1u) == 0,
         "url bytes round-tripped");
}

static void test_request_roundtrip_empty_url(void) {
    struct browser_ipc_fetch_request req = {0};
    req.method = BROWSER_IPC_FETCH_GET;
    uint8_t buf[16];
    uint32_t n = 0;
    int rc = browser_ipc_fetch_request_encode(&req, buf, sizeof(buf), &n);
    F_OK(rc == BROWSER_IPC_OK, "empty url encode ok");
    F_OK(n == 11u, "empty url encoded as fixed-size only");
    struct browser_ipc_fetch_request out;
    rc = browser_ipc_fetch_request_decode(buf, n, &out);
    F_OK(rc == BROWSER_IPC_OK, "empty url decode ok");
    F_OK(out.url_len == 0u, "decoded url_len=0");
    F_OK(out.url == NULL, "decoded url ptr=NULL when empty");
}

static void test_request_validation(void) {
    struct browser_ipc_fetch_request req = {0};
    req.method = BROWSER_IPC_FETCH_GET;
    uint8_t buf[64];
    F_OK(browser_ipc_fetch_request_encode(NULL, buf, sizeof(buf), NULL)
         == BROWSER_IPC_ERR_INVAL, "NULL req rejected");
    F_OK(browser_ipc_fetch_request_encode(&req, NULL, 64, NULL)
         == BROWSER_IPC_ERR_INVAL, "NULL out rejected");
    /* Bad method */
    req.method = 99u;
    F_OK(browser_ipc_fetch_request_encode(&req, buf, sizeof(buf), NULL)
         == BROWSER_IPC_ERR_INVAL, "invalid method rejected");
    req.method = BROWSER_IPC_FETCH_GET;
    /* Missing url with non-zero url_len */
    req.url_len = 5u;
    req.url = NULL;
    F_OK(browser_ipc_fetch_request_encode(&req, buf, sizeof(buf), NULL)
         == BROWSER_IPC_ERR_INVAL, "url_len>0 with NULL url rejected");
    /* Output buffer too small */
    const uint8_t url[] = "x";
    req.url = url;
    req.url_len = 1u;
    F_OK(browser_ipc_fetch_request_encode(&req, buf, 5, NULL)
         == BROWSER_IPC_ERR_SHORT, "small out buffer rejected");
}

static void test_response_roundtrip(void) {
    const uint8_t ctype[] = "text/html";
    const uint8_t body[]  = "<h1>hi</h1>";
    struct browser_ipc_fetch_response resp = {
        .seq = 42u,
        .nav_id = 9u,
        .status = BROWSER_IPC_FETCH_OK,
        .content_type_len = (uint16_t)(sizeof(ctype) - 1u),
        .body_len = (uint32_t)(sizeof(body) - 1u),
        .content_type = ctype,
        .body = body
    };
    uint8_t buf[128];
    uint32_t n = 0;
    int rc = browser_ipc_fetch_response_encode(&resp, buf, sizeof(buf), &n);
    F_OK(rc == BROWSER_IPC_OK, "FETCH_RESPONSE encode ok");
    F_OK(n == 16u + (sizeof(ctype) - 1u) + (sizeof(body) - 1u),
         "encoded size matches fixed + ctype + body");

    struct browser_ipc_fetch_response out;
    rc = browser_ipc_fetch_response_decode(buf, n, &out);
    F_OK(rc == BROWSER_IPC_OK, "FETCH_RESPONSE decode ok");
    F_OK(out.seq == 42u, "resp seq");
    F_OK(out.nav_id == 9u, "resp nav_id");
    F_OK(out.status == BROWSER_IPC_FETCH_OK, "resp status=200");
    F_OK(out.content_type_len == sizeof(ctype) - 1u, "ctype_len");
    F_OK(out.body_len == sizeof(body) - 1u, "body_len");
    F_OK(memcmp(out.content_type, ctype, sizeof(ctype) - 1u) == 0,
         "ctype bytes ok");
    F_OK(memcmp(out.body, body, sizeof(body) - 1u) == 0,
         "body bytes ok");
}

static void test_response_empty_body(void) {
    struct browser_ipc_fetch_response resp = {0};
    resp.status = BROWSER_IPC_FETCH_NOT_FOUND;
    uint8_t buf[32];
    uint32_t n = 0;
    int rc = browser_ipc_fetch_response_encode(&resp, buf, sizeof(buf), &n);
    F_OK(rc == BROWSER_IPC_OK, "404 with empty body encode ok");
    F_OK(n == 16u, "fixed-size only");
    struct browser_ipc_fetch_response out;
    rc = browser_ipc_fetch_response_decode(buf, n, &out);
    F_OK(rc == BROWSER_IPC_OK, "404 decode ok");
    F_OK(out.status == BROWSER_IPC_FETCH_NOT_FOUND, "status=404");
    F_OK(out.body == NULL, "body ptr NULL when body_len=0");
}

static void test_response_truncated_payload(void) {
    /* Build a response that claims body_len=10 but the buffer
     * only has 5 trailing bytes -- decode must reject. */
    uint8_t buf[32];
    memset(buf, 0, sizeof(buf));
    /* fixed prefix says ctype_len=0, body_len=10 */
    buf[12] = 0; buf[13] = 0; buf[14] = 0; buf[15] = 10;
    struct browser_ipc_fetch_response out;
    int rc = browser_ipc_fetch_response_decode(buf, 16u + 5u, &out);
    F_OK(rc == BROWSER_IPC_ERR_PAYLOAD, "truncated body rejected");
}

static void test_codec_direction_predicates(void) {
    F_OK(browser_ipc_kind_is_request(BROWSER_IPC_FETCH_RESPONSE) == 1,
         "FETCH_RESPONSE classified as request (chrome->engine)");
    F_OK(browser_ipc_kind_is_event(BROWSER_IPC_EVENT_FETCH_REQUEST) == 1,
         "EVENT_FETCH_REQUEST classified as event (engine->chrome)");
    F_OK(browser_ipc_kind_is_known(BROWSER_IPC_FETCH_RESPONSE) == 1,
         "FETCH_RESPONSE known to codec");
    F_OK(browser_ipc_kind_is_known(BROWSER_IPC_EVENT_FETCH_REQUEST) == 1,
         "EVENT_FETCH_REQUEST known to codec");
}

static void test_codec_min_payload(void) {
    F_OK(browser_ipc_kind_min_payload(BROWSER_IPC_EVENT_FETCH_REQUEST) == 11u,
         "FETCH_REQUEST min payload = 11 (fixed prefix)");
    F_OK(browser_ipc_kind_min_payload(BROWSER_IPC_FETCH_RESPONSE) == 16u,
         "FETCH_RESPONSE min payload = 16 (fixed prefix)");
}

int test_browser_ipc_fetch_run(void) {
    printf("[test_browser_ipc_fetch]\n");
    g_passed = 0;
    g_failed = 0;
    test_request_roundtrip_with_url();
    test_request_roundtrip_empty_url();
    test_request_validation();
    test_response_roundtrip();
    test_response_empty_body();
    test_response_truncated_payload();
    test_codec_direction_predicates();
    test_codec_min_payload();
    printf("  -> %d/%d passed\n", g_passed, g_passed + g_failed);
    return g_failed;
}
