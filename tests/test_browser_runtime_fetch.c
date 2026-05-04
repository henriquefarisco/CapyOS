/* tests/test_browser_runtime_fetch.c -- F3.3c slice 5c host coverage.
 *
 * Validates `chrome_runtime_dispatch_pending_fetch()`:
 *
 *   - Returns 0 when no fetch is pending (no-op, no pipe writes).
 *   - When EVENT_FETCH_REQUEST is staged via the chrome dispatcher,
 *     calling dispatch_pending_fetch:
 *         * drains the chrome's pending slot;
 *         * writes a FETCH_RESPONSE frame on the request pipe;
 *         * the framed payload roundtrips back to a valid response
 *           with the resolved (status, content_type, body).
 *   - For a known URL ("file://capyos/welcome") status=200 + body
 *     starts with "<h1>".
 *   - For an unknown URL, status=404 and body matches the resolver's
 *     fallback.
 *   - Two consecutive fetch requests both resolve correctly when
 *     drained in order (the chrome's single-shot slot is freed
 *     before the second request is dispatched).
 *   - When the request pipe is broken (read end closed), the
 *     helper returns -1 and clears `engine_alive`.
 */

#include "apps/browser_chrome_runtime.h"
#include "apps/browser_chrome_fetch_resolver.h"
#include "apps/browser_ipc.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_passed = 0;
static int g_failed = 0;

#define F_OK(cond, msg) do {                                              \
    if (cond) { g_passed++; }                                             \
    else { g_failed++; printf("  FAIL %s\n", msg); }                      \
} while (0)

/* === Mock pipes (small ring buffers, 4 pipes) =========================== */

#define MP_BUF 4096u
#define MP_N    4

struct mp { uint8_t b[MP_BUF]; uint32_t r, w, c; int ro, wo; };
static struct mp g[MP_N];

static void mp_reset(void) {
    for (int i = 0; i < MP_N; ++i) {
        memset(&g[i], 0, sizeof(g[i]));
        g[i].ro = 1; g[i].wo = 1;
    }
}

static int mp_write(int id, const void *buf, size_t len) {
    if (id < 0 || id >= MP_N) return -1;
    struct mp *p = &g[id];
    if (!p->wo || !p->ro) return -1;
    uint32_t space = MP_BUF - p->c;
    if (space == 0u) return -1;
    uint32_t to = (uint32_t)len;
    if (to > space) to = space;
    const uint8_t *s = buf;
    for (uint32_t i = 0; i < to; ++i)
        p->b[(p->w + i) % MP_BUF] = s[i];
    p->w = (p->w + to) % MP_BUF;
    p->c += to;
    return (int)to;
}

static int mp_read(int id, void *buf, size_t len) {
    if (id < 0 || id >= MP_N) return -1;
    struct mp *p = &g[id];
    if (!p->ro) return -1;
    if (p->c == 0u) return p->wo ? -1 : 0;
    uint32_t take = p->c;
    if (take > len) take = (uint32_t)len;
    uint8_t *d = buf;
    for (uint32_t i = 0; i < take; ++i)
        d[i] = p->b[(p->r + i) % MP_BUF];
    p->r = (p->r + take) % MP_BUF;
    p->c -= take;
    return (int)take;
}

/* === Helpers ============================================================ */

static void be_put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v;
}
static void be_put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

/* Stage a fetch request in the chrome by feeding an
 * EVENT_FETCH_REQUEST through the dispatcher. */
static void stage_fetch_request(struct chrome_runtime *rt,
                                uint32_t seq, uint32_t nav_id,
                                const char *url) {
    uint16_t ulen = (uint16_t)strlen(url);
    uint8_t pl[1024];
    /* fetch_request payload: seq u32, nav_id u32, method u8,
     * url_len u16, url bytes */
    be_put_u32(&pl[0], seq);
    be_put_u32(&pl[4], nav_id);
    pl[8] = BROWSER_IPC_FETCH_GET;
    be_put_u16(&pl[9], ulen);
    memcpy(&pl[11], url, ulen);
    uint32_t plen = 11u + ulen;

    struct browser_ipc_header hdr = {
        .magic = BROWSER_IPC_MAGIC,
        .kind = BROWSER_IPC_EVENT_FETCH_REQUEST,
        .seq = 0u,
        .payload_len = plen
    };
    browser_chrome_dispatch_event(&rt->chrome, &hdr, pl, 0u);
}

/* Read one full IPC frame from the request pipe (id 0) into
 * out_pl. Returns the (kind, seq) plus payload length. */
static int read_request_frame(uint16_t *kind, uint32_t *seq,
                              uint8_t *out_pl, uint32_t out_cap,
                              uint32_t *out_plen) {
    uint8_t hdr[BROWSER_IPC_HEADER_SIZE];
    int rh = mp_read(0, hdr, sizeof(hdr));
    if (rh != (int)sizeof(hdr)) return 0;
    *kind = (uint16_t)((uint16_t)hdr[2] << 8 | hdr[3]);
    *seq  = ((uint32_t)hdr[4] << 24) | ((uint32_t)hdr[5] << 16)
          | ((uint32_t)hdr[6] << 8)  | hdr[7];
    uint32_t plen = ((uint32_t)hdr[8] << 24) | ((uint32_t)hdr[9] << 16)
                  | ((uint32_t)hdr[10] << 8) | hdr[11];
    if (plen > out_cap) return 0;
    if (plen > 0u) {
        int rp = mp_read(0, out_pl, plen);
        if ((uint32_t)rp != plen) return 0;
    }
    *out_plen = plen;
    return 1;
}

/* === Tests ============================================================== */

static void test_no_pending_returns_zero(void) {
    mp_reset();
    chrome_runtime_set_pipe_ops(mp_write, mp_read);
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 42u, 0u);
    int rc = chrome_runtime_dispatch_pending_fetch(&rt);
    F_OK(rc == 0, "no pending -> 0");
    F_OK(g[0].c == 0u, "no bytes written");
}

static void test_known_url_dispatched(void) {
    mp_reset();
    chrome_runtime_set_pipe_ops(mp_write, mp_read);
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 42u, 0u);

    stage_fetch_request(&rt, 77u, 9u, "file://capyos/welcome");
    F_OK(rt.chrome.pending_fetch_active == 1u, "request staged");

    int rc = chrome_runtime_dispatch_pending_fetch(&rt);
    F_OK(rc == 1, "dispatch returns 1 on success");
    F_OK(rt.chrome.pending_fetch_active == 0u, "slot drained");

    uint16_t kind = 0; uint32_t seq = 0; uint32_t plen = 0;
    uint8_t pl[2048];
    int got = read_request_frame(&kind, &seq, pl, sizeof(pl), &plen);
    F_OK(got == 1, "frame read back from request pipe");
    F_OK(kind == BROWSER_IPC_FETCH_RESPONSE, "kind = FETCH_RESPONSE");

    struct browser_ipc_fetch_response resp;
    int dec = browser_ipc_fetch_response_decode(pl, plen, &resp);
    F_OK(dec == BROWSER_IPC_OK, "response decodes");
    F_OK(resp.seq == 77u, "seq echoes request");
    F_OK(resp.nav_id == 9u, "nav_id echoes request");
    F_OK(resp.status == BROWSER_IPC_FETCH_OK, "status = 200");
    F_OK(resp.body_len >= 4u && memcmp(resp.body, "<h1>", 4u) == 0,
         "body starts with <h1>");
}

static void test_unknown_url_returns_404(void) {
    mp_reset();
    chrome_runtime_set_pipe_ops(mp_write, mp_read);
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 42u, 0u);

    stage_fetch_request(&rt, 1u, 1u, "file://capyos/no-such-page");
    int rc = chrome_runtime_dispatch_pending_fetch(&rt);
    F_OK(rc == 1, "dispatch ok for unknown URL");

    uint16_t kind; uint32_t seq, plen;
    uint8_t pl[2048];
    F_OK(read_request_frame(&kind, &seq, pl, sizeof(pl), &plen) == 1,
         "frame read");
    struct browser_ipc_fetch_response resp;
    F_OK(browser_ipc_fetch_response_decode(pl, plen, &resp) == BROWSER_IPC_OK,
         "decode ok");
    F_OK(resp.status == BROWSER_IPC_FETCH_NOT_FOUND, "404 returned");
    F_OK(resp.body_len > 0u, "404 body non-empty");
}

static void test_two_sequential_dispatches(void) {
    mp_reset();
    chrome_runtime_set_pipe_ops(mp_write, mp_read);
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 42u, 0u);

    stage_fetch_request(&rt, 10u, 1u, "file://capyos/welcome");
    F_OK(chrome_runtime_dispatch_pending_fetch(&rt) == 1, "first ok");
    /* Drain the first response from the pipe to keep the buffer
     * clean for the second one. */
    uint16_t kind; uint32_t seq, plen; uint8_t pl[2048];
    F_OK(read_request_frame(&kind, &seq, pl, sizeof(pl), &plen) == 1,
         "first frame read");

    stage_fetch_request(&rt, 11u, 2u, "file://capyos/about");
    F_OK(chrome_runtime_dispatch_pending_fetch(&rt) == 1, "second ok");
    F_OK(read_request_frame(&kind, &seq, pl, sizeof(pl), &plen) == 1,
         "second frame read");
    struct browser_ipc_fetch_response resp;
    browser_ipc_fetch_response_decode(pl, plen, &resp);
    F_OK(resp.status == BROWSER_IPC_FETCH_OK, "second is 200");
    F_OK(resp.seq == 11u, "second seq matches");
}

static void test_broken_pipe_clears_engine_alive(void) {
    mp_reset();
    chrome_runtime_set_pipe_ops(mp_write, mp_read);
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 42u, 0u);
    /* Close the read side -> mp_write returns -1 (broken pipe). */
    g[0].ro = 0;

    stage_fetch_request(&rt, 1u, 1u, "file://capyos/welcome");
    int rc = chrome_runtime_dispatch_pending_fetch(&rt);
    F_OK(rc == -1, "broken pipe -> -1");
    F_OK(rt.engine_alive == 0, "engine_alive cleared");
}

/* F3.3g regression: http:// and https:// URLs must be routed through
 * the HTTP bridge instead of the local `file://capyos/xyz` resolver.
 * Under UNIT_TEST the kernel HTTP stack isn't linked, so the bridge
 * short-circuits with status = BROWSER_IPC_FETCH_TRANSPORT_ERR and
 * an empty body. The test's real purpose is to pin the routing: any
 * future regression that makes the HTTP branch fall back to
 * `browser_chrome_resolve_local()` would return 404 instead. */
static void test_http_url_routes_to_transport_error_in_tests(void) {
    mp_reset();
    chrome_runtime_set_pipe_ops(mp_write, mp_read);
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 42u, 0u);

    stage_fetch_request(&rt, 5u, 2u, "http://example.com/");
    int rc = chrome_runtime_dispatch_pending_fetch(&rt);
    F_OK(rc == 1, "http:// dispatched ok");

    uint16_t kind; uint32_t seq, plen; uint8_t pl[2048];
    F_OK(read_request_frame(&kind, &seq, pl, sizeof(pl), &plen) == 1,
         "http frame read");
    struct browser_ipc_fetch_response resp;
    F_OK(browser_ipc_fetch_response_decode(pl, plen, &resp) == BROWSER_IPC_OK,
         "http response decodes");
    F_OK(resp.status == BROWSER_IPC_FETCH_TRANSPORT_ERR,
         "http URL -> status=0 transport err in host tests");
    F_OK(resp.body_len == 0u,
         "http URL in host tests has empty body");
}

static void test_https_url_routes_to_transport_error_in_tests(void) {
    mp_reset();
    chrome_runtime_set_pipe_ops(mp_write, mp_read);
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 42u, 0u);

    stage_fetch_request(&rt, 9u, 4u, "https://example.com/foo?bar=baz");
    int rc = chrome_runtime_dispatch_pending_fetch(&rt);
    F_OK(rc == 1, "https:// dispatched ok");

    uint16_t kind; uint32_t seq, plen; uint8_t pl[2048];
    F_OK(read_request_frame(&kind, &seq, pl, sizeof(pl), &plen) == 1,
         "https frame read");
    struct browser_ipc_fetch_response resp;
    F_OK(browser_ipc_fetch_response_decode(pl, plen, &resp) == BROWSER_IPC_OK,
         "https response decodes");
    F_OK(resp.status == BROWSER_IPC_FETCH_TRANSPORT_ERR,
         "https URL -> status=0 in host tests");
    F_OK(resp.seq == 9u && resp.nav_id == 4u,
         "https seq/nav roundtrip");
}

/* F3.3g regression: URLs that look like `http` but aren't (ex.
 * `httpx://`, `http`, `httpabc.com`) must NOT be treated as HTTP,
 * so they fall back to the local resolver which returns 404. This
 * pins the prefix check to `http://` / `https://` exactly. */
static void test_lookalike_scheme_still_routes_to_resolver(void) {
    mp_reset();
    chrome_runtime_set_pipe_ops(mp_write, mp_read);
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 42u, 0u);

    stage_fetch_request(&rt, 1u, 1u, "httpx://example.com/");
    int rc = chrome_runtime_dispatch_pending_fetch(&rt);
    F_OK(rc == 1, "lookalike dispatched ok");

    uint16_t kind; uint32_t seq, plen; uint8_t pl[2048];
    F_OK(read_request_frame(&kind, &seq, pl, sizeof(pl), &plen) == 1,
         "lookalike frame read");
    struct browser_ipc_fetch_response resp;
    F_OK(browser_ipc_fetch_response_decode(pl, plen, &resp) == BROWSER_IPC_OK,
         "decode ok");
    F_OK(resp.status == BROWSER_IPC_FETCH_NOT_FOUND,
         "lookalike URL -> 404 via local resolver");
}

int test_browser_runtime_fetch_run(void) {
    printf("[test_browser_runtime_fetch]\n");
    g_passed = 0;
    g_failed = 0;
    test_no_pending_returns_zero();
    test_known_url_dispatched();
    test_unknown_url_returns_404();
    test_two_sequential_dispatches();
    test_broken_pipe_clears_engine_alive();
    test_http_url_routes_to_transport_error_in_tests();
    test_https_url_routes_to_transport_error_in_tests();
    test_lookalike_scheme_still_routes_to_resolver();
    printf("  -> %d/%d passed\n", g_passed, g_passed + g_failed);
    return g_failed;
}
