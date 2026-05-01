/*
 * test_browser_chrome_runtime.c (F3.3d)
 *
 * Cobre a runtime do chrome do browser usando pipes mockados em
 * memoria. Cada pipe e um ring buffer pequeno (rebatizado para
 * dar can/can't read em ordem deterministica).
 */

#include "apps/browser_chrome_runtime.h"
#include "apps/browser_ipc.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_rt_passed = 0;
static int g_rt_failed = 0;

#define RTCHECK(cond, label)                                              \
    do {                                                                  \
        if (cond) ++g_rt_passed;                                          \
        else { ++g_rt_failed;                                             \
               printf("    [FAIL] %s:%d %s\n", __FILE__, __LINE__, label); } \
    } while (0)

/* === Mock pipes ========================================================= */

#define MOCK_PIPE_BUF 8192u

struct mock_pipe {
    uint8_t buf[MOCK_PIPE_BUF];
    uint32_t r;
    uint32_t w;
    uint32_t count;
    int read_open;
    int write_open;
};

static struct mock_pipe g_pipes[8];

static void mock_pipe_reset_all(void) {
    for (int i = 0; i < 8; ++i) {
        memset(&g_pipes[i], 0, sizeof(g_pipes[i]));
        g_pipes[i].read_open = 1;
        g_pipes[i].write_open = 1;
    }
}

static int mock_pipe_write(int pipe_id, const void *buf, size_t len) {
    if (pipe_id < 0 || pipe_id >= 8) return -1;
    struct mock_pipe *p = &g_pipes[pipe_id];
    if (!p->write_open) return -1;
    if (!p->read_open) return -1; /* broken pipe */
    uint32_t space = MOCK_PIPE_BUF - p->count;
    if (space == 0u) return -1;
    uint32_t to = (uint32_t)len;
    if (to > space) to = space;
    const uint8_t *src = (const uint8_t *)buf;
    for (uint32_t i = 0; i < to; ++i) {
        p->buf[(p->w + i) % MOCK_PIPE_BUF] = src[i];
    }
    p->w = (p->w + to) % MOCK_PIPE_BUF;
    p->count += to;
    return (int)to;
}

static int mock_pipe_read(int pipe_id, void *buf, size_t len) {
    if (pipe_id < 0 || pipe_id >= 8) return -1;
    struct mock_pipe *p = &g_pipes[pipe_id];
    if (!p->read_open) return -1;
    if (p->count == 0u) {
        if (!p->write_open) return 0; /* EOF */
        return -1; /* would-block */
    }
    uint32_t avail = p->count;
    if (avail > len) avail = (uint32_t)len;
    uint8_t *dst = (uint8_t *)buf;
    for (uint32_t i = 0; i < avail; ++i) {
        dst[i] = p->buf[(p->r + i) % MOCK_PIPE_BUF];
    }
    p->r = (p->r + avail) % MOCK_PIPE_BUF;
    p->count -= avail;
    return (int)avail;
}

static void mock_pipe_install_ops(void) {
    chrome_runtime_set_pipe_ops(mock_pipe_write, mock_pipe_read);
}

/* Helpers para escrever um frame IPC inteiro no response pipe (i.e.
 * simular o engine emitindo um evento). */
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

static void inject_event(int response_pipe,
                         uint16_t kind,
                         uint32_t seq,
                         const uint8_t *payload,
                         uint32_t plen) {
    uint8_t hdr[BROWSER_IPC_HEADER_SIZE];
    be_put_u16(&hdr[0], (uint16_t)BROWSER_IPC_MAGIC);
    be_put_u16(&hdr[2], kind);
    be_put_u32(&hdr[4], seq);
    be_put_u32(&hdr[8], plen);
    /* mock_pipe_write tem que ser chamado diretamente porque o
     * teste e o "engine" injetando bytes no pipe que o chrome leria. */
    int wr = mock_pipe_write(response_pipe, hdr, sizeof(hdr));
    (void)wr;
    if (plen > 0u) {
        wr = mock_pipe_write(response_pipe, payload, plen);
        (void)wr;
    }
}

/* === Tests ============================================================== */

static void test_init_resets_state(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 42u, 100u);
    RTCHECK(rt.request_pipe_id == 0, "request_pipe_id");
    RTCHECK(rt.response_pipe_id == 1, "response_pipe_id");
    RTCHECK(rt.engine_pid == 42u, "engine_pid");
    RTCHECK(rt.engine_alive == 1, "engine_alive=1 com pid valido");
    RTCHECK(rt.total_requests_sent == 0u, "requests=0");
    RTCHECK(rt.chrome.status == BROWSER_CHROME_STATUS_IDLE, "chrome IDLE");

    /* pid=0 -> engine_alive=0 desde init. */
    chrome_runtime_init(&rt, 0, 1, 0u, 100u);
    RTCHECK(rt.engine_alive == 0, "pid=0 -> engine_alive=0");
}

static void test_send_navigate_writes_frame(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 99u, 0u);

    int rc = chrome_runtime_send_navigate(&rt, "http://x", 8);
    RTCHECK(rc == 0, "send_navigate ok");
    RTCHECK(rt.total_requests_sent == 1u, "requests=1");
    RTCHECK(rt.chrome.status == BROWSER_CHROME_STATUS_LOADING,
            "chrome -> LOADING");
    /* Verifica conteudo do request_pipe: header(12) + payload(2 + 8) = 22 */
    RTCHECK(g_pipes[0].count == 22u, "pipe[0] count=22");
    /* Magic */
    RTCHECK(g_pipes[0].buf[0] == 0xCBu && g_pipes[0].buf[1] == 0x1Bu,
            "magic BE no pipe");
    /* Kind = NAVIGATE */
    RTCHECK(g_pipes[0].buf[2] == 0x00u
            && g_pipes[0].buf[3] == BROWSER_IPC_NAVIGATE,
            "kind=NAVIGATE");
}

static void test_send_when_engine_dead_fails(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 0u, 0u); /* engine_alive=0 */
    int rc = chrome_runtime_send_navigate(&rt, "x", 1);
    RTCHECK(rc == -1, "send com engine morto -> -1");
    RTCHECK(rt.total_requests_sent == 0u, "requests=0");
}

static void test_send_broken_pipe_fails(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);
    g_pipes[0].read_open = 0; /* engine fechou seu fd 0 */
    int rc = chrome_runtime_send_cancel(&rt);
    RTCHECK(rc == -1, "broken pipe -> -1");
}

static void test_poll_no_data_returns_no_data(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);
    uint32_t actions = 0xDEADBEEFu;
    int s = chrome_runtime_poll_event(&rt, 0u, &actions);
    RTCHECK(s == CHROME_RUNTIME_POLL_NO_DATA, "NO_DATA");
    RTCHECK(actions == 0u, "actions=0 em NO_DATA");
}

static void test_poll_eof_marks_engine_dead(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);
    g_pipes[1].write_open = 0; /* engine fechou stdout */
    uint32_t actions = 0u;
    int s = chrome_runtime_poll_event(&rt, 0u, &actions);
    RTCHECK(s == CHROME_RUNTIME_POLL_ENGINE_EOF, "ENGINE_EOF");
    RTCHECK(rt.engine_alive == 0, "engine_alive=0 apos EOF");
    RTCHECK(rt.total_engine_eofs == 1u, "eofs=1");
}

static void test_poll_event_dispatches(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);

    /* Engine emite NAV_STARTED para nav 7 */
    uint8_t pl[64];
    be_put_u32(&pl[0], 7u);
    be_put_u16(&pl[4], 4u);
    memcpy(&pl[6], "test", 4);
    inject_event(1, BROWSER_IPC_EVENT_NAV_STARTED, 1, pl, 10u);

    uint32_t actions = 0u;
    int s = chrome_runtime_poll_event(&rt, 0u, &actions);
    RTCHECK(s == CHROME_RUNTIME_POLL_EVENT_HANDLED, "EVENT_HANDLED");
    RTCHECK((actions & BROWSER_CHROME_ACTION_UPDATE_STATUS) != 0u,
            "UPDATE_STATUS");
    RTCHECK(rt.chrome.current_nav_id == 7u, "nav_id atualizado");
    RTCHECK(rt.total_events_polled == 1u, "events_polled=1");
}

static void test_poll_pong_routes_to_watchdog(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 100u);

    /* Chrome envia PING (alocando nonce no watchdog). */
    int rc = chrome_runtime_send_ping(&rt, 100u);
    RTCHECK(rc == 0, "send_ping ok");
    RTCHECK(rt.chrome.watchdog.state == BROWSER_WATCHDOG_PING_IN_FLIGHT,
            "watchdog PING_IN_FLIGHT");

    /* Engine "responde" com PONG do mesmo nonce. */
    uint32_t nonce = rt.chrome.watchdog.in_flight_nonce;
    uint8_t pl[4];
    be_put_u32(pl, nonce);
    inject_event(1, BROWSER_IPC_EVENT_PONG, 1, pl, 4u);

    uint32_t actions = 0u;
    int s = chrome_runtime_poll_event(&rt, 200u, &actions);
    RTCHECK(s == CHROME_RUNTIME_POLL_EVENT_HANDLED, "PONG -> HANDLED");
    RTCHECK(rt.chrome.watchdog.state == BROWSER_WATCHDOG_IDLE,
            "watchdog IDLE apos PONG");
    RTCHECK(rt.total_pongs_received == 1u, "pongs=1");
}

static void test_tick_sends_ping_when_due(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);

    /* Antes do intervalo: tick nao envia. */
    int r = chrome_runtime_tick(&rt, 100u);
    RTCHECK(r == 0, "tick antes do intervalo: 0");
    RTCHECK(rt.total_requests_sent == 0u, "no PING sent yet");

    /* Apos o intervalo: tick envia PING. */
    r = chrome_runtime_tick(&rt, BROWSER_WATCHDOG_PING_INTERVAL_TICKS);
    RTCHECK(r == 0, "tick apos intervalo: 0 (sem kill)");
    RTCHECK(rt.total_requests_sent == 1u, "PING enviado");
    RTCHECK(rt.chrome.watchdog.state == BROWSER_WATCHDOG_PING_IN_FLIGHT,
            "watchdog PING_IN_FLIGHT");
}

static void test_tick_returns_kill_after_timeouts(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);

    /* Forca ciclos de timeout sem PONG ate watchdog pedir kill. */
    uint64_t t = 0u;
    int killed = 0;
    for (uint32_t i = 0; i < BROWSER_WATCHDOG_MAX_MISSED_PONGS + 1u; ++i) {
        t += BROWSER_WATCHDOG_PING_INTERVAL_TICKS;
        int r = chrome_runtime_tick(&rt, t);
        if (r == 1) { killed = 1; break; }
        t += BROWSER_WATCHDOG_PONG_TIMEOUT_TICKS;
        r = chrome_runtime_tick(&rt, t);
        if (r == 1) { killed = 1; break; }
    }
    RTCHECK(killed == 1, "tick acabou retornando 1 (kill)");
    RTCHECK(rt.engine_alive == 0, "engine_alive=0 apos kill");
}

static void test_tick_returns_minus_one_on_broken_pipe(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);
    /* Simula que engine fechou seu read end -> PING vai falhar. */
    g_pipes[0].read_open = 0;
    int r = chrome_runtime_tick(&rt, BROWSER_WATCHDOG_PING_INTERVAL_TICKS);
    RTCHECK(r == -1, "tick com broken pipe -> -1");
    RTCHECK(rt.engine_alive == 0, "engine_alive=0 apos broken pipe");
}

static void test_record_restart_clears_frame(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);
    /* Simula um frame antigo no chrome. */
    rt.chrome.last_frame.nav_id = 5u;
    rt.chrome.last_frame.width = 100u;
    rt.chrome.last_frame.pixels = (const uint8_t *)0xDEADBEEFul;
    rt.engine_alive = 0;

    chrome_runtime_record_restart(&rt, 2, 3, 77u, 1000u);
    RTCHECK(rt.request_pipe_id == 2, "novo request_pipe");
    RTCHECK(rt.response_pipe_id == 3, "novo response_pipe");
    RTCHECK(rt.engine_pid == 77u, "novo pid");
    RTCHECK(rt.engine_alive == 1, "engine_alive=1 apos restart");
    RTCHECK(rt.chrome.last_frame.nav_id == 0u, "frame antigo limpo");
    RTCHECK(rt.chrome.last_frame.pixels == (const uint8_t *)0,
            "frame pixels NULL");
    RTCHECK(rt.chrome.watchdog.total_kills == 1u, "watchdog kills=1");
}

static void test_send_payload_too_large_fails(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);
    char big_url[BROWSER_CHROME_URL_MAX + 8];
    for (size_t i = 0; i < sizeof(big_url); ++i) big_url[i] = 'a';
    int rc = chrome_runtime_send_navigate(&rt, big_url, sizeof(big_url));
    RTCHECK(rc == -1, "URL maior que limite -> -1");
}

static void test_poll_protocol_err_on_bad_magic(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);
    /* Injeta header com magic invalido. */
    uint8_t bad[BROWSER_IPC_HEADER_SIZE] = {0};
    mock_pipe_write(1, bad, sizeof(bad));
    uint32_t actions = 0u;
    int s = chrome_runtime_poll_event(&rt, 0u, &actions);
    RTCHECK(s == CHROME_RUNTIME_POLL_PROTOCOL_ERR, "PROTOCOL_ERR");
}

static void test_no_pipe_ops_returns_failure(void) {
    /* Sem injetar pipe ops, send_* deve falhar gracefully. */
    chrome_runtime_set_pipe_ops((chrome_runtime_pipe_write_fn)0,
                                 (chrome_runtime_pipe_read_fn)0);
    struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);
    int rc = chrome_runtime_send_cancel(&rt);
    RTCHECK(rc == -1, "send sem ops -> -1");
    uint32_t actions = 0u;
    int s = chrome_runtime_poll_event(&rt, 0u, &actions);
    RTCHECK(s == CHROME_RUNTIME_POLL_PROTOCOL_ERR,
            "poll sem ops -> PROTOCOL_ERR");
    /* Restaura para os outros testes. */
    chrome_runtime_set_pipe_ops(mock_pipe_write, mock_pipe_read);
}

static void test_poll_handles_frame_event(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);
    rt.chrome.current_nav_id = 1u;
    /* Frame 2x2 BGRA */
    uint8_t pl[12 + 16];
    be_put_u32(&pl[0], 1u);
    be_put_u16(&pl[4], 2u);
    be_put_u16(&pl[6], 2u);
    be_put_u32(&pl[8], 8u);
    for (int i = 0; i < 16; ++i) pl[12 + i] = (uint8_t)(i + 1);
    inject_event(1, BROWSER_IPC_EVENT_FRAME, 1, pl, sizeof(pl));

    uint32_t actions = 0u;
    int s = chrome_runtime_poll_event(&rt, 0u, &actions);
    RTCHECK(s == CHROME_RUNTIME_POLL_EVENT_HANDLED, "FRAME -> HANDLED");
    RTCHECK((actions & BROWSER_CHROME_ACTION_REPAINT_FRAME) != 0u,
            "REPAINT_FRAME");
    RTCHECK(rt.chrome.last_frame.width == 2u, "frame w=2");
    RTCHECK(rt.chrome.last_frame.height == 2u, "frame h=2");
    /* Pixels apontam para event_scratch[12..27]; conteudo deve bater. */
    RTCHECK(rt.chrome.last_frame.pixels != (const uint8_t *)0,
            "pixels nao NULL");
    RTCHECK(rt.chrome.last_frame.pixels[0] == 1u
            && rt.chrome.last_frame.pixels[15] == 16u,
            "pixels copiados corretamente");
}

static void test_multiple_polls_drain_pipe(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);

    /* Injeta 3 EVENT_TITLE consecutivos. */
    for (uint32_t i = 0; i < 3u; ++i) {
        uint8_t pl[6];
        be_put_u16(&pl[0], 4u);
        memcpy(&pl[2], "abcd", 4);
        inject_event(1, BROWSER_IPC_EVENT_TITLE, i + 1u, pl, 6u);
    }

    uint32_t actions = 0u;
    for (int i = 0; i < 3; ++i) {
        int s = chrome_runtime_poll_event(&rt, 0u, &actions);
        RTCHECK(s == CHROME_RUNTIME_POLL_EVENT_HANDLED,
                "poll N -> HANDLED");
    }
    int s = chrome_runtime_poll_event(&rt, 0u, &actions);
    RTCHECK(s == CHROME_RUNTIME_POLL_NO_DATA, "depois do 3o: NO_DATA");
    RTCHECK(rt.total_events_polled == 3u, "events=3");
}

int test_browser_chrome_runtime_run(void) {
    printf("[test_browser_chrome_runtime]\n");
    g_rt_passed = 0;
    g_rt_failed = 0;
    test_init_resets_state();
    test_send_navigate_writes_frame();
    test_send_when_engine_dead_fails();
    test_send_broken_pipe_fails();
    test_poll_no_data_returns_no_data();
    test_poll_eof_marks_engine_dead();
    test_poll_event_dispatches();
    test_poll_pong_routes_to_watchdog();
    test_tick_sends_ping_when_due();
    test_tick_returns_kill_after_timeouts();
    test_tick_returns_minus_one_on_broken_pipe();
    test_record_restart_clears_frame();
    test_send_payload_too_large_fails();
    test_poll_protocol_err_on_bad_magic();
    test_no_pipe_ops_returns_failure();
    test_poll_handles_frame_event();
    test_multiple_polls_drain_pipe();
    printf("  -> %d passed, %d failed\n", g_rt_passed, g_rt_failed);
    return g_rt_failed;
}
