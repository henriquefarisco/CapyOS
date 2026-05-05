/* tests/test_browser_runtime_image.c -- Etapa 3 secao a fetch+decode
 * (2026-05-05): host coverage para o pipeline IMAGE_REQUEST/RESPONSE.
 *
 * Em UNIT_TEST, o chrome_runtime_dispatch_pending_image NAO chama
 * o decoder real (png/jpeg precisam de kalloc do kernel). Em vez
 * disso, sempre retorna status=UNSUPPORTED. Estes tests validam
 * apenas o I/O do IPC e a logica de stage/drain do chrome:
 *
 *   - dispatch_event(EVENT_IMAGE_REQUEST) levanta
 *     ACTION_IMAGE_REQUESTED e stages (img_id, nav_id, url).
 *   - take_pending_image drena e zera o slot.
 *   - Segunda IMAGE_REQUEST sem drenar -> protocol error.
 *   - dispatch_pending_image escreve IMAGE_RESPONSE no pipe com
 *     img_id/nav_id ecoados e status=UNSUPPORTED + zero pixels.
 *   - Sem pending: dispatch_pending_image retorna 0 sem tocar pipe.
 *   - Pipe quebrado faz dispatch_pending_image retornar -1 e
 *     limpar engine_alive.
 */

#include "apps/browser_chrome_runtime.h"
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

/* === Mock pipes (simples, 4 pipes de 4 KiB) ============================= */

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

/* === Helpers BE ========================================================= */

static void be_put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v;
}
static void be_put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

/* Stage IMAGE_REQUEST via dispatcher. */
static void stage_image_request(struct chrome_runtime *rt,
                                uint32_t img_id, uint32_t nav_id,
                                const char *url) {
    uint16_t ulen = (uint16_t)strlen(url);
    uint8_t pl[1024];
    be_put_u32(&pl[0], img_id);
    be_put_u32(&pl[4], nav_id);
    be_put_u16(&pl[8], ulen);
    memcpy(&pl[10], url, ulen);
    uint32_t plen = 10u + ulen;
    struct browser_ipc_header hdr = {
        .magic = BROWSER_IPC_MAGIC,
        .kind = BROWSER_IPC_EVENT_IMAGE_REQUEST,
        .seq = 0u,
        .payload_len = plen
    };
    browser_chrome_dispatch_event(&rt->chrome, &hdr, pl, 0u);
}

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

/* === Tests: chrome handler + drain ====================================== */

static void test_handler_stages_request(void) {
    mp_reset();
    chrome_runtime_set_pipe_ops(mp_write, mp_read);
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);

    stage_image_request(&rt, 0xAABBCCDDu, 7u, "http://example/img.png");

    I_OK(rt.chrome.pending_image_active == 1u, "pending image staged");
    I_OK(rt.chrome.pending_image_id == 0xAABBCCDDu,
         "pending img_id stored");
    I_OK(rt.chrome.pending_image_nav_id == 7u,
         "pending nav_id stored");
    I_OK(rt.chrome.pending_image_url_len == strlen("http://example/img.png"),
         "pending url_len stored");
    I_OK(strcmp(rt.chrome.pending_image_url, "http://example/img.png") == 0,
         "pending url bytes stored");
}

static void test_take_pending_drains_and_clears(void) {
    mp_reset();
    chrome_runtime_set_pipe_ops(mp_write, mp_read);
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);
    stage_image_request(&rt, 1u, 2u, "u");

    struct browser_ipc_image_request out;
    int got = browser_chrome_take_pending_image(&rt.chrome, &out);
    I_OK(got == 1, "take_pending returns 1");
    I_OK(out.img_id == 1u, "drained img_id");
    I_OK(out.nav_id == 2u, "drained nav_id");
    I_OK(rt.chrome.pending_image_active == 0u,
         "slot cleared after drain");

    int got2 = browser_chrome_take_pending_image(&rt.chrome, &out);
    I_OK(got2 == 0, "take_pending on empty -> 0");
}

static void test_take_pending_null_safe(void) {
    struct browser_ipc_image_request out;
    I_OK(browser_chrome_take_pending_image(NULL, &out) == 0,
         "NULL chrome -> 0");
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);
    I_OK(browser_chrome_take_pending_image(&rt.chrome, NULL) == 0,
         "NULL out -> 0");
}

static void test_overlap_request_protocol_err(void) {
    mp_reset();
    chrome_runtime_set_pipe_ops(mp_write, mp_read);
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);

    /* Stage first request directly. */
    stage_image_request(&rt, 1u, 1u, "first");

    /* Stage second BEFORE drain -> protocol error. Re-encode payload
     * by hand and call dispatch (the helper above just calls
     * dispatch_event). */
    uint16_t ulen = 6u;
    uint8_t pl[32];
    be_put_u32(&pl[0], 2u);
    be_put_u32(&pl[4], 1u);
    be_put_u16(&pl[8], ulen);
    memcpy(&pl[10], "second", ulen);
    struct browser_ipc_header hdr = {
        .magic = BROWSER_IPC_MAGIC,
        .kind = BROWSER_IPC_EVENT_IMAGE_REQUEST,
        .seq = 0u,
        .payload_len = 10u + ulen
    };
    uint32_t a2 = browser_chrome_dispatch_event(&rt.chrome, &hdr, pl, 0u);
    I_OK((a2 & BROWSER_CHROME_ACTION_PROTOCOL_ERR) != 0u,
         "second image request -> protocol error");
    I_OK(rt.chrome.pending_image_id == 1u,
         "first request preserved on overlap");
}

/* === Tests: dispatch_pending_image ====================================== */

static void test_no_pending_returns_zero(void) {
    mp_reset();
    chrome_runtime_set_pipe_ops(mp_write, mp_read);
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);

    int rc = chrome_runtime_dispatch_pending_image(&rt);
    I_OK(rc == 0, "no pending image -> 0");
    I_OK(g[0].c == 0u, "no bytes written");
}

static void test_dispatch_emits_unsupported_in_unit_test(void) {
    /* Em UNIT_TEST nao temos decoder; status sempre = UNSUPPORTED. */
    mp_reset();
    chrome_runtime_set_pipe_ops(mp_write, mp_read);
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);
    stage_image_request(&rt, 100u, 5u, "http://example/img.png");

    int rc = chrome_runtime_dispatch_pending_image(&rt);
    I_OK(rc == 1, "dispatch ok");
    I_OK(rt.chrome.pending_image_active == 0u, "slot drained");

    uint16_t kind = 0; uint32_t seq = 0; uint32_t plen = 0;
    uint8_t pl[64];
    int got = read_request_frame(&kind, &seq, pl, sizeof(pl), &plen);
    I_OK(got == 1, "IMAGE_RESPONSE frame written");
    I_OK(kind == BROWSER_IPC_IMAGE_RESPONSE, "frame kind = IMAGE_RESPONSE");

    struct browser_ipc_image_response resp;
    int dec = browser_ipc_image_response_decode(pl, plen, &resp);
    I_OK(dec == BROWSER_IPC_OK, "response decodes");
    I_OK(resp.img_id == 100u, "img_id echoed");
    I_OK(resp.nav_id == 5u, "nav_id echoed");
    I_OK(resp.status == BROWSER_IPC_IMAGE_UNSUPPORTED,
         "status = UNSUPPORTED in UNIT_TEST");
    I_OK(resp.width == 0u && resp.height == 0u && resp.pixel_bytes == 0u,
         "no pixels on UNSUPPORTED");
}

static void test_broken_pipe_clears_engine_alive(void) {
    mp_reset();
    chrome_runtime_set_pipe_ops(mp_write, mp_read);
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);
    stage_image_request(&rt, 1u, 1u, "u");
    /* Quebra o pipe: zera "ro" do request pipe. */
    g[0].wo = 0;
    g[0].ro = 0;
    int rc = chrome_runtime_dispatch_pending_image(&rt);
    I_OK(rc == -1, "broken pipe -> -1");
    I_OK(rt.engine_alive == 0, "engine_alive cleared on broken pipe");
}

int test_browser_runtime_image_run(void) {
    printf("[test_browser_runtime_image]\n");
    g_passed = 0;
    g_failed = 0;
    test_handler_stages_request();
    test_take_pending_drains_and_clears();
    test_take_pending_null_safe();
    test_overlap_request_protocol_err();
    test_no_pending_returns_zero();
    test_dispatch_emits_unsupported_in_unit_test();
    test_broken_pipe_clears_engine_alive();
    printf("  -> %d/%d passed\n", g_passed, g_passed + g_failed);
    return g_failed;
}
