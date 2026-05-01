/*
 * test_browser_chrome.c (F3.3d)
 *
 * Cobre o dispatcher de eventos do chrome do browser. Pure logic;
 * sem GUI nem syscalls.
 */

#include "apps/browser_chrome.h"
#include "apps/browser_ipc.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_c_passed = 0;
static int g_c_failed = 0;

#define CCHECK(cond, label)                                              \
    do {                                                                 \
        if (cond) {                                                      \
            ++g_c_passed;                                                \
        } else {                                                         \
            ++g_c_failed;                                                \
            printf("    [FAIL] %s:%d %s\n", __FILE__, __LINE__, label);  \
        }                                                                \
    } while (0)

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

static struct browser_ipc_header make_hdr(uint16_t kind, uint32_t plen) {
    struct browser_ipc_header h = {
        .magic = BROWSER_IPC_MAGIC,
        .kind = kind,
        .seq = 1,
        .payload_len = plen
    };
    return h;
}

static void test_init_state(void) {
    struct browser_chrome c;
    browser_chrome_init(&c, 1000u);
    CCHECK(c.status == BROWSER_CHROME_STATUS_IDLE, "init -> IDLE");
    CCHECK(c.current_nav_id == 0u, "init nav_id=0");
    CCHECK(c.current_url[0] == '\0', "init url empty");
    CCHECK(c.current_title[0] == '\0', "init title empty");
    CCHECK(c.next_request_seq == 1u, "init seq=1");
    CCHECK(c.total_events_handled == 0u, "init events=0");
    CCHECK(c.total_protocol_errors == 0u, "init errors=0");
}

static void test_record_navigate_sent_sets_loading(void) {
    struct browser_chrome c;
    browser_chrome_init(&c, 0u);
    /* Simula titulo e erro pre-existentes que precisam ser limpos. */
    c.current_title[0] = 'X'; c.current_title_len = 1;
    c.last_error_reason[0] = 'E'; c.last_error_reason_len = 1;
    browser_chrome_record_navigate_sent(&c, "https://example.org", 19);
    CCHECK(c.status == BROWSER_CHROME_STATUS_LOADING, "status LOADING");
    CCHECK(c.current_url_len == 19, "url_len=19");
    CCHECK(memcmp(c.current_url, "https://example.org", 19) == 0,
           "url copiado");
    CCHECK(c.current_url[19] == '\0', "url NUL-terminada");
    CCHECK(c.current_title_len == 0u, "titulo limpo");
    CCHECK(c.last_error_reason_len == 0u, "erro limpo");
}

static void test_alloc_request_seq_monotonic(void) {
    struct browser_chrome c;
    browser_chrome_init(&c, 0u);
    uint32_t a = browser_chrome_alloc_request_seq(&c);
    uint32_t b = browser_chrome_alloc_request_seq(&c);
    CCHECK(a == 1u, "seq inicial=1");
    CCHECK(b == 2u, "seq monotonico");
    /* Forca wrap. */
    c.next_request_seq = 0xFFFFFFFFu;
    uint32_t pre = browser_chrome_alloc_request_seq(&c);
    uint32_t post = browser_chrome_alloc_request_seq(&c);
    CCHECK(pre == 0xFFFFFFFFu, "alloca UINT32_MAX");
    CCHECK(post == 1u, "wrap pula 0");
}

static void test_build_navigate_payload(void) {
    uint8_t buf[BROWSER_CHROME_URL_MAX + 16];
    uint32_t n = browser_chrome_build_navigate_payload(
        "http://x", 8, buf, sizeof(buf));
    CCHECK(n == 10u, "payload len = 2 + 8");
    CCHECK(buf[0] == 0x00 && buf[1] == 0x08, "url_len BE = 8");
    CCHECK(memcmp(buf + 2, "http://x", 8) == 0, "url body");

    /* out_size insuficiente */
    uint8_t small[5];
    CCHECK(browser_chrome_build_navigate_payload("http://x", 8, small, 5) == 0u,
           "rejeita buffer pequeno");
    CCHECK(browser_chrome_build_navigate_payload(NULL, 0, buf, sizeof(buf)) == 0u,
           "rejeita url NULL");
    CCHECK(browser_chrome_build_navigate_payload("x", 1, NULL, 16) == 0u,
           "rejeita out NULL");
}

static void test_dispatch_nav_started(void) {
    struct browser_chrome c;
    browser_chrome_init(&c, 0u);
    uint8_t pl[64];
    be_put_u32(&pl[0], 42u); /* nav_id */
    be_put_u16(&pl[4], 11u); /* url_len */
    memcpy(&pl[6], "example.org", 11);
    struct browser_ipc_header h = make_hdr(BROWSER_IPC_EVENT_NAV_STARTED, 17u);
    uint32_t a = browser_chrome_dispatch_event(&c, &h, pl, 0);
    CCHECK((a & BROWSER_CHROME_ACTION_UPDATE_STATUS) != 0u,
           "NAV_STARTED -> UPDATE_STATUS");
    CCHECK(c.current_nav_id == 42u, "nav_id atualizado");
    CCHECK(c.status == BROWSER_CHROME_STATUS_LOADING, "status LOADING");
    CCHECK(c.current_url_len == 11u, "url copiado");
    CCHECK(memcmp(c.current_url, "example.org", 11) == 0, "url body");
}

static void test_dispatch_nav_progress(void) {
    struct browser_chrome c;
    browser_chrome_init(&c, 0u);
    /* Comeca uma nav_id 7 */
    uint8_t started[10];
    be_put_u32(&started[0], 7u);
    be_put_u16(&started[4], 4u);
    memcpy(&started[6], "test", 4);
    struct browser_ipc_header hs = make_hdr(BROWSER_IPC_EVENT_NAV_STARTED, 10u);
    (void)browser_chrome_dispatch_event(&c, &hs, started, 0);

    /* PROGRESS para nav 7 */
    uint8_t pp[6];
    be_put_u32(&pp[0], 7u);
    pp[4] = BROWSER_IPC_STAGE_PARSE;
    pp[5] = 50;
    struct browser_ipc_header hp = make_hdr(BROWSER_IPC_EVENT_NAV_PROGRESS, 6u);
    uint32_t a = browser_chrome_dispatch_event(&c, &hp, pp, 0);
    CCHECK((a & BROWSER_CHROME_ACTION_UPDATE_STATUS) != 0u,
           "PROGRESS -> UPDATE_STATUS");
    CCHECK(c.last_progress_stage == BROWSER_IPC_STAGE_PARSE,
           "stage atualizado");
    CCHECK(c.last_progress_percent == 50u, "percent=50");

    /* PROGRESS stale (nav antigo) e ignorado. */
    uint8_t pp_old[6];
    be_put_u32(&pp_old[0], 6u); /* nav < 7 */
    pp_old[4] = BROWSER_IPC_STAGE_RENDER;
    pp_old[5] = 99;
    struct browser_ipc_header hpo = make_hdr(BROWSER_IPC_EVENT_NAV_PROGRESS, 6u);
    uint32_t a2 = browser_chrome_dispatch_event(&c, &hpo, pp_old, 0);
    CCHECK(a2 == 0u, "PROGRESS stale -> 0");
    CCHECK(c.last_progress_stage == BROWSER_IPC_STAGE_PARSE,
           "stage nao mudou");
}

static void test_dispatch_nav_ready(void) {
    struct browser_chrome c;
    browser_chrome_init(&c, 0u);
    c.current_nav_id = 5;
    c.status = BROWSER_CHROME_STATUS_LOADING;
    uint8_t pl[4];
    be_put_u32(pl, 5u);
    struct browser_ipc_header h = make_hdr(BROWSER_IPC_EVENT_NAV_READY, 4u);
    uint32_t a = browser_chrome_dispatch_event(&c, &h, pl, 0);
    CCHECK((a & BROWSER_CHROME_ACTION_UPDATE_STATUS) != 0u,
           "READY -> UPDATE_STATUS");
    CCHECK(c.status == BROWSER_CHROME_STATUS_READY, "status READY");
}

static void test_dispatch_nav_failed_with_reason(void) {
    struct browser_chrome c;
    browser_chrome_init(&c, 0u);
    c.current_nav_id = 9;
    uint8_t pl[64];
    be_put_u32(&pl[0], 9u);
    be_put_u16(&pl[4], 7u);
    memcpy(&pl[6], "timeout", 7);
    struct browser_ipc_header h = make_hdr(BROWSER_IPC_EVENT_NAV_FAILED, 13u);
    (void)browser_chrome_dispatch_event(&c, &h, pl, 0);
    CCHECK(c.status == BROWSER_CHROME_STATUS_FAILED, "status FAILED");
    CCHECK(c.last_error_reason_len == 7u, "reason_len=7");
    CCHECK(memcmp(c.last_error_reason, "timeout", 7) == 0, "reason body");
    CCHECK(c.last_error_reason[7] == '\0', "reason NUL-terminado");
}

static void test_dispatch_nav_cancelled(void) {
    struct browser_chrome c;
    browser_chrome_init(&c, 0u);
    c.current_nav_id = 3;
    uint8_t pl[4];
    be_put_u32(pl, 3u);
    struct browser_ipc_header h = make_hdr(BROWSER_IPC_EVENT_NAV_CANCELLED, 4u);
    uint32_t a = browser_chrome_dispatch_event(&c, &h, pl, 0);
    CCHECK((a & BROWSER_CHROME_ACTION_UPDATE_STATUS) != 0u,
           "CANCELLED -> UPDATE_STATUS");
    CCHECK(c.status == BROWSER_CHROME_STATUS_CANCELLED, "status CANCELLED");
}

static void test_dispatch_title(void) {
    struct browser_chrome c;
    browser_chrome_init(&c, 0u);
    uint8_t pl[32];
    be_put_u16(&pl[0], 13u);
    memcpy(&pl[2], "Example Title", 13);
    struct browser_ipc_header h = make_hdr(BROWSER_IPC_EVENT_TITLE, 15u);
    uint32_t a = browser_chrome_dispatch_event(&c, &h, pl, 0);
    CCHECK((a & BROWSER_CHROME_ACTION_UPDATE_TITLE) != 0u,
           "TITLE -> UPDATE_TITLE");
    CCHECK(c.current_title_len == 13u, "title_len=13");
    CCHECK(memcmp(c.current_title, "Example Title", 13) == 0,
           "title body");
}

static void test_dispatch_frame(void) {
    struct browser_chrome c;
    browser_chrome_init(&c, 0u);
    c.current_nav_id = 1;
    /* Frame 2x2 BGRA = 16 bytes pixels + 12 bytes header = 28 */
    uint8_t pl[12 + 16];
    be_put_u32(&pl[0], 1u);   /* nav_id */
    be_put_u16(&pl[4], 2u);   /* w */
    be_put_u16(&pl[6], 2u);   /* h */
    be_put_u32(&pl[8], 8u);   /* stride = 2*4 = 8 */
    for (int i = 0; i < 16; ++i) pl[12 + i] = (uint8_t)i;
    struct browser_ipc_header h = make_hdr(BROWSER_IPC_EVENT_FRAME, 28u);
    uint32_t a = browser_chrome_dispatch_event(&c, &h, pl, 0);
    CCHECK((a & BROWSER_CHROME_ACTION_REPAINT_FRAME) != 0u,
           "FRAME -> REPAINT_FRAME");
    CCHECK(c.last_frame.nav_id == 1u, "nav_id");
    CCHECK(c.last_frame.width == 2u, "w");
    CCHECK(c.last_frame.height == 2u, "h");
    CCHECK(c.last_frame.stride == 8u, "stride");
    CCHECK(c.last_frame.pixel_bytes == 16u, "pixel_bytes");
    CCHECK(c.last_frame.pixels == pl + 12u, "pixels alias");
}

static void test_dispatch_frame_protocol_errors(void) {
    struct browser_chrome c;
    browser_chrome_init(&c, 0u);
    c.current_nav_id = 1;

    /* stride < w*4 */
    uint8_t bad_stride[12 + 16];
    be_put_u32(&bad_stride[0], 1u);
    be_put_u16(&bad_stride[4], 2u);
    be_put_u16(&bad_stride[6], 2u);
    be_put_u32(&bad_stride[8], 4u); /* stride muito pequeno */
    struct browser_ipc_header hb = make_hdr(BROWSER_IPC_EVENT_FRAME, 28u);
    uint32_t a = browser_chrome_dispatch_event(&c, &hb, bad_stride, 0);
    CCHECK((a & BROWSER_CHROME_ACTION_PROTOCOL_ERR) != 0u,
           "stride pequeno -> PROTOCOL_ERR");
    CCHECK(c.total_protocol_errors == 1u, "errors=1");

    /* total > payload */
    uint8_t bad_total[12 + 4];
    be_put_u32(&bad_total[0], 1u);
    be_put_u16(&bad_total[4], 4u);   /* w=4 */
    be_put_u16(&bad_total[6], 4u);   /* h=4 -> precisa 64 bytes */
    be_put_u32(&bad_total[8], 16u);
    struct browser_ipc_header ht = make_hdr(BROWSER_IPC_EVENT_FRAME, 16u); /* so 4 bytes pos header */
    uint32_t a2 = browser_chrome_dispatch_event(&c, &ht, bad_total, 0);
    CCHECK((a2 & BROWSER_CHROME_ACTION_PROTOCOL_ERR) != 0u,
           "total>payload -> PROTOCOL_ERR");
}

static void test_dispatch_frame_stale_nav(void) {
    struct browser_chrome c;
    browser_chrome_init(&c, 0u);
    c.current_nav_id = 5;
    uint8_t pl[12 + 16];
    be_put_u32(&pl[0], 4u); /* stale */
    be_put_u16(&pl[4], 2u);
    be_put_u16(&pl[6], 2u);
    be_put_u32(&pl[8], 8u);
    struct browser_ipc_header h = make_hdr(BROWSER_IPC_EVENT_FRAME, 28u);
    uint32_t a = browser_chrome_dispatch_event(&c, &h, pl, 0);
    CCHECK(a == 0u, "FRAME stale -> 0 (no repaint)");
    CCHECK(c.last_frame.nav_id == 0u, "last_frame nao atualizado");
}

static void test_dispatch_pong_routes_to_watchdog(void) {
    struct browser_chrome c;
    browser_chrome_init(&c, 0u);
    /* Manda um ping primeiro pelo watchdog para criar in_flight. */
    uint32_t nonce = browser_watchdog_alloc_nonce(&c.watchdog);
    browser_watchdog_record_ping(&c.watchdog, nonce, 100u);

    uint8_t pl[4];
    be_put_u32(pl, nonce);
    struct browser_ipc_header h = make_hdr(BROWSER_IPC_EVENT_PONG, 4u);
    uint32_t a = browser_chrome_dispatch_event(&c, &h, pl, 200u);
    CCHECK(a == 0u, "PONG -> 0 (sem acao visivel)");
    CCHECK(c.watchdog.state == BROWSER_WATCHDOG_IDLE,
           "PONG roteado: watchdog IDLE");
    CCHECK(c.watchdog.consecutive_misses == 0u, "watchdog misses=0");
}

static void test_dispatch_log(void) {
    struct browser_chrome c;
    browser_chrome_init(&c, 0u);
    uint8_t pl[16];
    pl[0] = BROWSER_IPC_LOG_INFO;
    be_put_u16(&pl[1], 5u);
    memcpy(&pl[3], "hello", 5);
    struct browser_ipc_header h = make_hdr(BROWSER_IPC_EVENT_LOG, 8u);
    uint32_t a = browser_chrome_dispatch_event(&c, &h, pl, 0);
    CCHECK((a & BROWSER_CHROME_ACTION_LOG_FORWARD) != 0u,
           "LOG -> LOG_FORWARD");
}

static void test_dispatch_request_kind_is_protocol_error(void) {
    /* Engine nunca envia kind de request; chrome trata como erro. */
    struct browser_chrome c;
    browser_chrome_init(&c, 0u);
    struct browser_ipc_header h = make_hdr(BROWSER_IPC_NAVIGATE, 0u);
    uint32_t a = browser_chrome_dispatch_event(&c, &h, NULL, 0);
    CCHECK((a & BROWSER_CHROME_ACTION_PROTOCOL_ERR) != 0u,
           "NAVIGATE como evento -> PROTOCOL_ERR");
    CCHECK(c.total_protocol_errors == 1u, "errors incrementado");
}

static void test_dispatch_short_payload_is_error(void) {
    struct browser_chrome c;
    browser_chrome_init(&c, 0u);
    /* TITLE com plen=1 e curto demais (precisa >= 2). */
    uint8_t pl[2];
    pl[0] = 0;
    struct browser_ipc_header h = make_hdr(BROWSER_IPC_EVENT_TITLE, 1u);
    uint32_t a = browser_chrome_dispatch_event(&c, &h, pl, 0);
    CCHECK((a & BROWSER_CHROME_ACTION_PROTOCOL_ERR) != 0u,
           "TITLE curto -> PROTOCOL_ERR");
}

static void test_dispatch_null_args(void) {
    struct browser_chrome c;
    browser_chrome_init(&c, 0u);
    CCHECK(browser_chrome_dispatch_event(NULL, NULL, NULL, 0)
               == BROWSER_CHROME_ACTION_PROTOCOL_ERR,
           "NULL args -> PROTOCOL_ERR");
    /* Header valido mas payload NULL com plen>0 deve falhar. */
    struct browser_ipc_header h = make_hdr(BROWSER_IPC_EVENT_TITLE, 4u);
    uint32_t a = browser_chrome_dispatch_event(&c, &h, NULL, 0);
    CCHECK((a & BROWSER_CHROME_ACTION_PROTOCOL_ERR) != 0u,
           "payload NULL com plen>0 -> PROTOCOL_ERR");
}

static void test_total_events_handled_increments(void) {
    struct browser_chrome c;
    browser_chrome_init(&c, 0u);
    uint8_t pl[6];
    be_put_u16(&pl[0], 4u);
    memcpy(&pl[2], "abcd", 4);
    struct browser_ipc_header h = make_hdr(BROWSER_IPC_EVENT_TITLE, 6u);
    (void)browser_chrome_dispatch_event(&c, &h, pl, 0);
    (void)browser_chrome_dispatch_event(&c, &h, pl, 0);
    (void)browser_chrome_dispatch_event(&c, &h, pl, 0);
    CCHECK(c.total_events_handled == 3u, "events handled=3");
}

int test_browser_chrome_run(void) {
    printf("[test_browser_chrome]\n");
    g_c_passed = 0;
    g_c_failed = 0;
    test_init_state();
    test_record_navigate_sent_sets_loading();
    test_alloc_request_seq_monotonic();
    test_build_navigate_payload();
    test_dispatch_nav_started();
    test_dispatch_nav_progress();
    test_dispatch_nav_ready();
    test_dispatch_nav_failed_with_reason();
    test_dispatch_nav_cancelled();
    test_dispatch_title();
    test_dispatch_frame();
    test_dispatch_frame_protocol_errors();
    test_dispatch_frame_stale_nav();
    test_dispatch_pong_routes_to_watchdog();
    test_dispatch_log();
    test_dispatch_request_kind_is_protocol_error();
    test_dispatch_short_payload_is_error();
    test_dispatch_null_args();
    test_total_events_handled_increments();
    printf("  -> %d passed, %d failed\n", g_c_passed, g_c_failed);
    return g_c_failed;
}
