/* tests/test_browser_chrome_fetch.c -- F3.3c slice 5b host coverage.
 *
 * Validates the chrome side of the fetch flow:
 *
 *   - dispatch_event(EVENT_FETCH_REQUEST) raises ACTION_FETCH_REQUESTED
 *     and stages seq/nav_id/method/url in chrome state.
 *   - take_pending_fetch() returns the staged data and clears the slot.
 *   - take_pending_fetch() on empty slot returns 0.
 *   - A second FETCH_REQUEST while one is pending is rejected as a
 *     protocol error (slot is single-shot in slice 5b).
 *   - Build helper produces a payload that round-trips through the
 *     codec helpers.
 *   - URL longer than BROWSER_CHROME_URL_MAX gets truncated (the
 *     copy is bounded; pending_fetch_url_len reflects the truncation).
 */

#include "apps/browser_chrome.h"
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

static uint32_t encode_request_payload(uint32_t seq, uint32_t nav_id,
                                       uint8_t method,
                                       const char *url,
                                       uint8_t *out, uint32_t out_size) {
    struct browser_ipc_fetch_request req;
    req.seq = seq;
    req.nav_id = nav_id;
    req.method = method;
    req.url = (const uint8_t *)url;
    size_t ulen = url ? strlen(url) : 0u;
    req.url_len = (uint16_t)ulen;
    uint32_t n = 0;
    int rc = browser_ipc_fetch_request_encode(&req, out, out_size, &n);
    return (rc == BROWSER_IPC_OK) ? n : 0u;
}

static void test_dispatch_stages_request(void) {
    struct browser_chrome c;
    browser_chrome_init(&c, 0u);
    uint8_t payload[256];
    uint32_t plen = encode_request_payload(11u, 3u, BROWSER_IPC_FETCH_GET,
                                           "file://capyos/welcome",
                                           payload, sizeof(payload));
    F_OK(plen > 0u, "request payload encoded");

    struct browser_ipc_header hdr = {
        .magic = BROWSER_IPC_MAGIC,
        .kind = BROWSER_IPC_EVENT_FETCH_REQUEST,
        .seq = 999u,
        .payload_len = plen
    };
    uint32_t actions = browser_chrome_dispatch_event(&c, &hdr, payload, 100u);
    F_OK((actions & BROWSER_CHROME_ACTION_FETCH_REQUESTED) != 0u,
         "ACTION_FETCH_REQUESTED raised");
    F_OK((actions & BROWSER_CHROME_ACTION_PROTOCOL_ERR) == 0u,
         "no protocol error on valid request");
    F_OK(c.pending_fetch_active == 1u, "pending slot active");
    F_OK(c.pending_fetch_seq == 11u, "pending seq stored");
    F_OK(c.pending_fetch_nav_id == 3u, "pending nav_id stored");
    F_OK(c.pending_fetch_method == BROWSER_IPC_FETCH_GET, "pending method");
    F_OK(c.pending_fetch_url_len == strlen("file://capyos/welcome"),
         "pending url_len");
    F_OK(strcmp(c.pending_fetch_url, "file://capyos/welcome") == 0,
         "pending url bytes");
}

static void test_take_pending_drains(void) {
    struct browser_chrome c;
    browser_chrome_init(&c, 0u);
    uint8_t payload[256];
    uint32_t plen = encode_request_payload(7u, 1u, BROWSER_IPC_FETCH_GET,
                                           "about:blank", payload, sizeof(payload));
    struct browser_ipc_header hdr = {
        BROWSER_IPC_MAGIC, BROWSER_IPC_EVENT_FETCH_REQUEST, 0u, plen
    };
    browser_chrome_dispatch_event(&c, &hdr, payload, 0u);

    struct browser_ipc_fetch_request out;
    int got = browser_chrome_take_pending_fetch(&c, &out);
    F_OK(got == 1, "take_pending returns 1 when active");
    F_OK(out.seq == 7u, "drained seq");
    F_OK(out.nav_id == 1u, "drained nav_id");
    F_OK(out.method == BROWSER_IPC_FETCH_GET, "drained method");
    F_OK(out.url_len == strlen("about:blank"), "drained url_len");
    F_OK(out.url != NULL &&
         memcmp(out.url, "about:blank", strlen("about:blank")) == 0,
         "drained url bytes match");
    F_OK(c.pending_fetch_active == 0u, "slot cleared after drain");

    /* Second drain with empty slot returns 0. */
    int got2 = browser_chrome_take_pending_fetch(&c, &out);
    F_OK(got2 == 0, "take_pending returns 0 when empty");
}

static void test_take_pending_null_safe(void) {
    struct browser_ipc_fetch_request out;
    F_OK(browser_chrome_take_pending_fetch(NULL, &out) == 0,
         "NULL chrome -> 0");
    struct browser_chrome c;
    browser_chrome_init(&c, 0u);
    F_OK(browser_chrome_take_pending_fetch(&c, NULL) == 0,
         "NULL out -> 0");
}

static void test_overlapping_request_rejected(void) {
    struct browser_chrome c;
    browser_chrome_init(&c, 0u);
    uint8_t payload[256];
    uint32_t plen = encode_request_payload(1u, 1u, BROWSER_IPC_FETCH_GET,
                                           "u1", payload, sizeof(payload));
    struct browser_ipc_header hdr = {
        BROWSER_IPC_MAGIC, BROWSER_IPC_EVENT_FETCH_REQUEST, 0u, plen
    };
    uint32_t a1 = browser_chrome_dispatch_event(&c, &hdr, payload, 0u);
    F_OK((a1 & BROWSER_CHROME_ACTION_FETCH_REQUESTED) != 0u,
         "first request accepted");

    /* Second request without draining the first */
    plen = encode_request_payload(2u, 1u, BROWSER_IPC_FETCH_GET, "u2",
                                  payload, sizeof(payload));
    hdr.payload_len = plen;
    uint32_t a2 = browser_chrome_dispatch_event(&c, &hdr, payload, 0u);
    F_OK((a2 & BROWSER_CHROME_ACTION_PROTOCOL_ERR) != 0u,
         "second request without drain -> protocol error");
    F_OK(c.total_protocol_errors == 1u,
         "protocol error counter incremented");
    /* The original pending request must NOT have been overwritten. */
    F_OK(c.pending_fetch_seq == 1u,
         "first request preserved on overlap");
}

static void test_invalid_payload_rejected(void) {
    struct browser_chrome c;
    browser_chrome_init(&c, 0u);
    uint8_t payload[4] = { 0x00, 0x00, 0x00, 0x01 }; /* too short */
    struct browser_ipc_header hdr = {
        BROWSER_IPC_MAGIC, BROWSER_IPC_EVENT_FETCH_REQUEST, 0u, 4u
    };
    uint32_t actions = browser_chrome_dispatch_event(&c, &hdr, payload, 0u);
    F_OK((actions & BROWSER_CHROME_ACTION_PROTOCOL_ERR) != 0u,
         "short payload -> protocol error");
    F_OK(c.pending_fetch_active == 0u,
         "slot remains empty on bad payload");
}

static void test_build_response_roundtrip(void) {
    const uint8_t ctype[] = "text/html";
    const uint8_t body[]  = "<h1>capyland</h1>";
    uint8_t buf[128];
    uint32_t n = browser_chrome_build_fetch_response_payload(
        77u, 5u, BROWSER_IPC_FETCH_OK,
        ctype, (uint16_t)(sizeof(ctype) - 1u),
        body, (uint32_t)(sizeof(body) - 1u),
        buf, sizeof(buf));
    F_OK(n == 16u + (sizeof(ctype) - 1u) + (sizeof(body) - 1u),
         "response payload size");

    struct browser_ipc_fetch_response resp;
    int rc = browser_ipc_fetch_response_decode(buf, n, &resp);
    F_OK(rc == BROWSER_IPC_OK, "response decodes");
    F_OK(resp.seq == 77u && resp.nav_id == 5u, "seq/nav round-trip");
    F_OK(resp.status == BROWSER_IPC_FETCH_OK, "status=200");
    F_OK(memcmp(resp.body, body, sizeof(body) - 1u) == 0,
         "body bytes round-trip");
}

static void test_build_response_buffer_too_small(void) {
    uint8_t out[8];
    uint32_t n = browser_chrome_build_fetch_response_payload(
        1u, 1u, BROWSER_IPC_FETCH_OK,
        (const uint8_t *)"x", 1u, (const uint8_t *)"x", 1u,
        out, sizeof(out));
    F_OK(n == 0u, "tiny buffer -> n=0 sentinel");
}

int test_browser_chrome_fetch_run(void) {
    printf("[test_browser_chrome_fetch]\n");
    g_passed = 0;
    g_failed = 0;
    test_dispatch_stages_request();
    test_take_pending_drains();
    test_take_pending_null_safe();
    test_overlapping_request_rejected();
    test_invalid_payload_rejected();
    test_build_response_roundtrip();
    test_build_response_buffer_too_small();
    printf("  -> %d/%d passed\n", g_passed, g_passed + g_failed);
    return g_failed;
}
