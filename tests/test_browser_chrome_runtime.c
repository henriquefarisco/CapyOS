/*
 * test_browser_chrome_runtime.c (F3.3d)
 *
 * Cobre a runtime do chrome do browser usando pipes mockados em
 * memoria. A infraestrutura dos pipes mock vive em
 * `test_browser_chrome_runtime_mock.{h,c}` (2026-05-03 split para
 * ficar sob o limite de 900 linhas do layout audit).
 */

#include "test_browser_chrome_runtime_mock.h"
#include "apps/browser_chrome_runtime.h"
#include "apps/browser_ipc.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Aliases locais para preservar os nomes usados historicamente nos
 * testes (be_put_u16/u32 e inject_event). */
#define be_put_u16 mock_be_put_u16
#define be_put_u32 mock_be_put_u32
#define inject_event mock_inject_event

static int g_rt_passed = 0;
static int g_rt_failed = 0;

#define RTCHECK(cond, label)                                              \
    do {                                                                  \
        if (cond) ++g_rt_passed;                                          \
        else { ++g_rt_failed;                                             \
               printf("    [FAIL] %s:%d %s\n", __FILE__, __LINE__, label); } \
    } while (0)

/* === Tests ============================================================== */

static void test_init_resets_state(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    /* 2026-05-02: static to keep the ~8 MiB struct off the test
     * binary's stack after the EVENT_BUF_MAX bump to 4 MiB.
     * Each test resets all relevant fields via chrome_runtime_init
     * so re-entering the same static is safe. */
    static struct chrome_runtime rt;
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
    /* 2026-05-02: static to keep the ~8 MiB struct off the test
     * binary's stack after the EVENT_BUF_MAX bump to 4 MiB.
     * Each test resets all relevant fields via chrome_runtime_init
     * so re-entering the same static is safe. */
    static struct chrome_runtime rt;
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
    /* 2026-05-02: static to keep the ~8 MiB struct off the test
     * binary's stack after the EVENT_BUF_MAX bump to 4 MiB.
     * Each test resets all relevant fields via chrome_runtime_init
     * so re-entering the same static is safe. */
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 0u, 0u); /* engine_alive=0 */
    int rc = chrome_runtime_send_navigate(&rt, "x", 1);
    RTCHECK(rc == -1, "send com engine morto -> -1");
    RTCHECK(rt.total_requests_sent == 0u, "requests=0");
}

static void test_send_broken_pipe_fails(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    /* 2026-05-02: static to keep the ~8 MiB struct off the test
     * binary's stack after the EVENT_BUF_MAX bump to 4 MiB.
     * Each test resets all relevant fields via chrome_runtime_init
     * so re-entering the same static is safe. */
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);
    g_pipes[0].read_open = 0; /* engine fechou seu fd 0 */
    int rc = chrome_runtime_send_cancel(&rt);
    RTCHECK(rc == -1, "broken pipe -> -1");
}

/* 2026-05-02: locks the BROWSER_IPC_RESIZE encoding contract that
 * the engine ring-3 handler depends on to know the new viewport.
 * Without this test, a subtle byte-order or payload-size regression
 * would silently make the engine clamp width/height to garbage and
 * the rasterizer would emit content at the wrong size after every
 * window resize. */
static void test_send_resize_writes_frame(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    /* 2026-05-02: static to keep the ~8 MiB struct off the test
     * binary's stack after the EVENT_BUF_MAX bump to 4 MiB.
     * Each test resets all relevant fields via chrome_runtime_init
     * so re-entering the same static is safe. */
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);

    int rc = chrome_runtime_send_resize(&rt, 0x0320u /*800*/,
                                              0x0258u /*600*/);
    RTCHECK(rc == 0, "send_resize ok");
    /* header(12) + payload(4) = 16 bytes total */
    RTCHECK(g_pipes[0].count == 16u, "pipe[0] count=16");
    RTCHECK(g_pipes[0].buf[2] == 0x00u
            && g_pipes[0].buf[3] == BROWSER_IPC_RESIZE,
            "kind=RESIZE");
    /* Payload: width BE = 0x03, 0x20; height BE = 0x02, 0x58 */
    RTCHECK(g_pipes[0].buf[12] == 0x03u
            && g_pipes[0].buf[13] == 0x20u,
            "payload width BE=800");
    RTCHECK(g_pipes[0].buf[14] == 0x02u
            && g_pipes[0].buf[15] == 0x58u,
            "payload height BE=600");
}

static void test_send_resize_zero_dim_rejected(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    /* 2026-05-02: static to keep the ~8 MiB struct off the test
     * binary's stack after the EVENT_BUF_MAX bump to 4 MiB.
     * Each test resets all relevant fields via chrome_runtime_init
     * so re-entering the same static is safe. */
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);

    /* Either axis at 0 must fail without writing to the pipe so the
     * engine never sees a degenerate viewport (would div-by-zero in
     * the rasterizer or produce a 0-byte payload). */
    int rc1 = chrome_runtime_send_resize(&rt, 0u, 100u);
    int rc2 = chrome_runtime_send_resize(&rt, 100u, 0u);
    RTCHECK(rc1 == -1, "width=0 -> -1");
    RTCHECK(rc2 == -1, "height=0 -> -1");
    RTCHECK(g_pipes[0].count == 0u, "pipe untouched on rejected resize");
}

static void test_send_resize_engine_dead_fails(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    /* 2026-05-02: static to keep the ~8 MiB struct off the test
     * binary's stack after the EVENT_BUF_MAX bump to 4 MiB.
     * Each test resets all relevant fields via chrome_runtime_init
     * so re-entering the same static is safe. */
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 0u, 0u); /* engine_alive=0 */
    int rc = chrome_runtime_send_resize(&rt, 800u, 600u);
    RTCHECK(rc == -1, "send_resize com engine morto -> -1");
    RTCHECK(g_pipes[0].count == 0u,
            "pipe untouched quando engine morto");
}

/* Etapa 3 seção b (2026-05-02): send_click emite BROWSER_IPC_CLICK
 * com payload (x:u16 BE, y:u16 BE, button:u8). Validamos tamanho
 * total (header 12 + payload 5 = 17 bytes), kind correto, e cada
 * byte do payload em ordem big-endian. Se o encode virar little
 * endian por engano, o engine no ring 3 interpreta x/y trocados e
 * o hit-test falha em 100% dos clicks -- este teste pega. */
static void test_send_click_writes_frame(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);

    int rc = chrome_runtime_send_click(&rt, 0x1234u, 0x5678u, 1u);
    RTCHECK(rc == 0, "send_click ok");
    RTCHECK(g_pipes[0].count == 12u + 5u, "pipe[0] count=17 (hdr+payload)");
    RTCHECK(g_pipes[0].buf[2] == 0x00u
            && g_pipes[0].buf[3] == BROWSER_IPC_CLICK,
            "kind=CLICK");
    RTCHECK(g_pipes[0].buf[12] == 0x12u && g_pipes[0].buf[13] == 0x34u,
            "x BE=0x1234");
    RTCHECK(g_pipes[0].buf[14] == 0x56u && g_pipes[0].buf[15] == 0x78u,
            "y BE=0x5678");
    RTCHECK(g_pipes[0].buf[16] == 1u, "button=1 (LMB)");
}

static void test_send_click_engine_dead_fails(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 0u, 0u); /* engine_alive=0 */
    int rc = chrome_runtime_send_click(&rt, 10u, 20u, 1u);
    RTCHECK(rc == -1, "send_click com engine morto -> -1");
    RTCHECK(g_pipes[0].count == 0u, "pipe untouched quando engine morto");
}

/* Etapa 3 seção e (2026-05-02): send_scroll emite
 * BROWSER_IPC_SCROLL com delta_y:i32 em BE. Testamos tanto valores
 * positivos (rolar para baixo) quanto negativos (rolar para cima)
 * porque o truque do cast uint32/int32 na codificacao BE precisa
 * preservar o bit de sinal -- sem este teste, um delta negativo
 * poderia virar um numero gigante positivo ao ser decodificado. */
static void test_send_scroll_writes_frame(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);

    int rc = chrome_runtime_send_scroll(&rt, 0x12345678);
    RTCHECK(rc == 0, "send_scroll positive ok");
    RTCHECK(g_pipes[0].count == 12u + 4u, "pipe[0] count=16");
    RTCHECK(g_pipes[0].buf[2] == 0x00u
            && g_pipes[0].buf[3] == BROWSER_IPC_SCROLL,
            "kind=SCROLL");
    RTCHECK(g_pipes[0].buf[12] == 0x12u && g_pipes[0].buf[13] == 0x34u
            && g_pipes[0].buf[14] == 0x56u && g_pipes[0].buf[15] == 0x78u,
            "delta_y BE=0x12345678");
}

static void test_send_scroll_negative_delta(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);

    /* -1 em complemento-2 = 0xFFFFFFFF */
    int rc = chrome_runtime_send_scroll(&rt, -1);
    RTCHECK(rc == 0, "send_scroll negative ok");
    RTCHECK(g_pipes[0].buf[12] == 0xFFu && g_pipes[0].buf[13] == 0xFFu
            && g_pipes[0].buf[14] == 0xFFu && g_pipes[0].buf[15] == 0xFFu,
            "delta_y BE=-1 (0xFFFFFFFF)");
}

static void test_send_scroll_engine_dead_fails(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 0u, 0u);
    int rc = chrome_runtime_send_scroll(&rt, 100);
    RTCHECK(rc == -1, "send_scroll com engine morto -> -1");
    RTCHECK(g_pipes[0].count == 0u, "pipe untouched");
}

/* Etapa 3 seção c (2026-05-03): send_key emite BROWSER_IPC_KEY com
 * payload BE = keycode:u32 + mods:u8. Pin-test garante que a ordem
 * dos bytes nao inverteu (caso contrario o engine receberia 0x78563412
 * em vez de 0x12345678 e rotearia chars errados ao input focado). */
static void test_send_key_writes_frame(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);

    int rc = chrome_runtime_send_key(&rt, 0x12345678u, 0xABu);
    RTCHECK(rc == 0, "send_key ok");
    RTCHECK(g_pipes[0].count == 12u + 5u, "pipe[0] count=17");
    RTCHECK(g_pipes[0].buf[2] == 0x00u
            && g_pipes[0].buf[3] == BROWSER_IPC_KEY,
            "kind=KEY");
    /* payload_len u32 BE = 5 */
    RTCHECK(g_pipes[0].buf[8] == 0u && g_pipes[0].buf[9] == 0u
            && g_pipes[0].buf[10] == 0u && g_pipes[0].buf[11] == 5u,
            "payload_len=5");
    /* keycode BE */
    RTCHECK(g_pipes[0].buf[12] == 0x12u && g_pipes[0].buf[13] == 0x34u
            && g_pipes[0].buf[14] == 0x56u && g_pipes[0].buf[15] == 0x78u,
            "keycode BE=0x12345678");
    /* mods */
    RTCHECK(g_pipes[0].buf[16] == 0xABu, "mods=0xAB");
}

static void test_send_key_printable_ascii(void) {
    /* Keycode 'a' = 0x61. Sem mods. Verifica que mesmo com keycode
     * pequeno (que cabe em 1 byte) os 4 bytes do payload BE estao
     * preenchidos corretamente com 3 zeros + 0x61. */
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);

    int rc = chrome_runtime_send_key(&rt, (uint32_t)'a', 0u);
    RTCHECK(rc == 0, "send_key 'a' ok");
    RTCHECK(g_pipes[0].buf[12] == 0x00u && g_pipes[0].buf[13] == 0x00u
            && g_pipes[0].buf[14] == 0x00u && g_pipes[0].buf[15] == 0x61u,
            "keycode 'a' BE = 0x00000061");
    RTCHECK(g_pipes[0].buf[16] == 0x00u, "mods=0");
}

static void test_send_key_engine_dead_fails(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 0u, 0u);
    int rc = chrome_runtime_send_key(&rt, (uint32_t)'a', 0u);
    RTCHECK(rc == -1, "send_key com engine morto -> -1");
    RTCHECK(g_pipes[0].count == 0u, "pipe untouched");
}

static void test_send_key_null_rt_fails(void) {
    /* NULL rt: -1 sem crash; nao toca pipes. */
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    int rc = chrome_runtime_send_key((struct chrome_runtime *)0,
                                      (uint32_t)'x', 0u);
    RTCHECK(rc == -1, "send_key NULL rt -> -1");
    RTCHECK(g_pipes[0].count == 0u, "pipe untouched on NULL rt");
}

/* Etapa 3 seção b (2026-05-02): send_reload emite BROWSER_IPC_RELOAD
 * com payload vazio. Testa que o header sai certo e payload_len=0. */
static void test_send_reload_writes_frame(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);

    int rc = chrome_runtime_send_reload(&rt);
    RTCHECK(rc == 0, "send_reload ok");
    RTCHECK(g_pipes[0].count == 12u, "pipe[0] count=12 (header only)");
    RTCHECK(g_pipes[0].buf[2] == 0x00u
            && g_pipes[0].buf[3] == BROWSER_IPC_RELOAD,
            "kind=RELOAD");
    /* payload_len u32 BE = 0 */
    RTCHECK(g_pipes[0].buf[8] == 0u && g_pipes[0].buf[9] == 0u
            && g_pipes[0].buf[10] == 0u && g_pipes[0].buf[11] == 0u,
            "payload_len=0");
}

static void test_send_reload_engine_dead_fails(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 0u, 0u);
    int rc = chrome_runtime_send_reload(&rt);
    RTCHECK(rc == -1, "send_reload com engine morto -> -1");
    RTCHECK(g_pipes[0].count == 0u, "pipe untouched");
}

/* Etapa 3 seção b-polish (2026-05-03): send_back/send_forward emitem
 * frames de 12 bytes (header puro, payload vazio). Pin-tests para a
 * ordem/kind corretos -- se o encoder trocar BACK com FORWARD, o
 * engine navegaria na direcao errada e seria muito dificil debugar
 * sem este teste. */
static void test_send_back_writes_frame(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);

    int rc = chrome_runtime_send_back(&rt);
    RTCHECK(rc == 0, "send_back ok");
    RTCHECK(g_pipes[0].count == 12u, "pipe[0] count=12 (header only)");
    RTCHECK(g_pipes[0].buf[2] == 0x00u
            && g_pipes[0].buf[3] == BROWSER_IPC_BACK,
            "kind=BACK");
    /* payload_len u32 BE = 0 */
    RTCHECK(g_pipes[0].buf[8] == 0u && g_pipes[0].buf[9] == 0u
            && g_pipes[0].buf[10] == 0u && g_pipes[0].buf[11] == 0u,
            "payload_len=0");
}

static void test_send_back_engine_dead_fails(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 0u, 0u);
    int rc = chrome_runtime_send_back(&rt);
    RTCHECK(rc == -1, "send_back com engine morto -> -1");
    RTCHECK(g_pipes[0].count == 0u, "pipe untouched");
}

static void test_send_forward_writes_frame(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);

    int rc = chrome_runtime_send_forward(&rt);
    RTCHECK(rc == 0, "send_forward ok");
    RTCHECK(g_pipes[0].count == 12u, "pipe[0] count=12 (header only)");
    RTCHECK(g_pipes[0].buf[2] == 0x00u
            && g_pipes[0].buf[3] == BROWSER_IPC_FORWARD,
            "kind=FORWARD");
    /* payload_len u32 BE = 0 */
    RTCHECK(g_pipes[0].buf[8] == 0u && g_pipes[0].buf[9] == 0u
            && g_pipes[0].buf[10] == 0u && g_pipes[0].buf[11] == 0u,
            "payload_len=0");
}

static void test_send_forward_engine_dead_fails(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 0u, 0u);
    int rc = chrome_runtime_send_forward(&rt);
    RTCHECK(rc == -1, "send_forward com engine morto -> -1");
    RTCHECK(g_pipes[0].count == 0u, "pipe untouched");
}

/* Etapa 3 seção b-polish (2026-05-03): valida que EVENT_TITLE
 * emitido pelo engine produz UPDATE_TITLE no dispatcher do chrome
 * e preenche `chrome.current_title`. Sem este pin, uma regressao
 * em `handle_title` (chrome.c) passaria despercebida -- o caminho
 * de poll entrega o titulo para o browser_app e ele manda para o
 * compositor. */
static void test_poll_title_event_updates_current_title(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);

    /* Injeta EVENT_TITLE("Hello World") no response pipe. */
    static const char *title = "Hello World";
    uint16_t tlen = 11u;
    uint8_t payload[2 + 64];
    be_put_u16(&payload[0], tlen);
    for (uint16_t i = 0; i < tlen; ++i) payload[2 + i] = (uint8_t)title[i];
    inject_event(1, BROWSER_IPC_EVENT_TITLE, /*seq=*/1u, payload, 2u + tlen);

    uint32_t actions = 0;
    int s = chrome_runtime_poll_event(&rt, 0u, &actions);
    RTCHECK(s == CHROME_RUNTIME_POLL_EVENT_HANDLED, "poll EVENT_TITLE");
    RTCHECK((actions & BROWSER_CHROME_ACTION_UPDATE_TITLE) != 0u,
            "action UPDATE_TITLE raised");
    RTCHECK(rt.chrome.current_title_len == tlen, "current_title_len=11");
    RTCHECK(rt.chrome.current_title[0] == 'H'
            && rt.chrome.current_title[10] == 'd',
            "current_title echoes engine payload");
    RTCHECK(rt.chrome.current_title[tlen] == '\0',
            "current_title NUL-terminated");
}

static void test_poll_no_data_returns_no_data(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    /* 2026-05-02: static to keep the ~8 MiB struct off the test
     * binary's stack after the EVENT_BUF_MAX bump to 4 MiB.
     * Each test resets all relevant fields via chrome_runtime_init
     * so re-entering the same static is safe. */
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);
    uint32_t actions = 0xDEADBEEFu;
    int s = chrome_runtime_poll_event(&rt, 0u, &actions);
    RTCHECK(s == CHROME_RUNTIME_POLL_NO_DATA, "NO_DATA");
    RTCHECK(actions == 0u, "actions=0 em NO_DATA");
}

static void test_poll_eof_marks_engine_dead(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    /* 2026-05-02: static to keep the ~8 MiB struct off the test
     * binary's stack after the EVENT_BUF_MAX bump to 4 MiB.
     * Each test resets all relevant fields via chrome_runtime_init
     * so re-entering the same static is safe. */
    static struct chrome_runtime rt;
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
    /* 2026-05-02: static to keep the ~8 MiB struct off the test
     * binary's stack after the EVENT_BUF_MAX bump to 4 MiB.
     * Each test resets all relevant fields via chrome_runtime_init
     * so re-entering the same static is safe. */
    static struct chrome_runtime rt;
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
    /* 2026-05-02: static to keep the ~8 MiB struct off the test
     * binary's stack after the EVENT_BUF_MAX bump to 4 MiB.
     * Each test resets all relevant fields via chrome_runtime_init
     * so re-entering the same static is safe. */
    static struct chrome_runtime rt;
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
    /* 2026-05-02: static to keep the ~8 MiB struct off the test
     * binary's stack after the EVENT_BUF_MAX bump to 4 MiB.
     * Each test resets all relevant fields via chrome_runtime_init
     * so re-entering the same static is safe. */
    static struct chrome_runtime rt;
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
    /* 2026-05-02: static to keep the ~8 MiB struct off the test
     * binary's stack after the EVENT_BUF_MAX bump to 4 MiB.
     * Each test resets all relevant fields via chrome_runtime_init
     * so re-entering the same static is safe. */
    static struct chrome_runtime rt;
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
    /* 2026-05-02: static to keep the ~8 MiB struct off the test
     * binary's stack after the EVENT_BUF_MAX bump to 4 MiB.
     * Each test resets all relevant fields via chrome_runtime_init
     * so re-entering the same static is safe. */
    static struct chrome_runtime rt;
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
    /* 2026-05-02: static to keep the ~8 MiB struct off the test
     * binary's stack after the EVENT_BUF_MAX bump to 4 MiB.
     * Each test resets all relevant fields via chrome_runtime_init
     * so re-entering the same static is safe. */
    static struct chrome_runtime rt;
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
    /* 2026-05-02: static to keep the ~8 MiB struct off the test
     * binary's stack after the EVENT_BUF_MAX bump to 4 MiB.
     * Each test resets all relevant fields via chrome_runtime_init
     * so re-entering the same static is safe. */
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);
    char big_url[BROWSER_CHROME_URL_MAX + 8];
    for (size_t i = 0; i < sizeof(big_url); ++i) big_url[i] = 'a';
    int rc = chrome_runtime_send_navigate(&rt, big_url, sizeof(big_url));
    RTCHECK(rc == -1, "URL maior que limite -> -1");
}

static void test_poll_protocol_err_on_bad_magic(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    /* 2026-05-02: static to keep the ~8 MiB struct off the test
     * binary's stack after the EVENT_BUF_MAX bump to 4 MiB.
     * Each test resets all relevant fields via chrome_runtime_init
     * so re-entering the same static is safe. */
    static struct chrome_runtime rt;
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
    /* 2026-05-02: static to keep the ~8 MiB struct off the test
     * binary's stack after the EVENT_BUF_MAX bump to 4 MiB.
     * Each test resets all relevant fields via chrome_runtime_init
     * so re-entering the same static is safe. */
    static struct chrome_runtime rt;
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
    /* 2026-05-02: static to keep the ~8 MiB struct off the test
     * binary's stack after the EVENT_BUF_MAX bump to 4 MiB.
     * Each test resets all relevant fields via chrome_runtime_init
     * so re-entering the same static is safe. */
    static struct chrome_runtime rt;
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
    /* Pixels persistidos no last_frame_storage dedicado (nao mais
     * alias do event_scratch); conteudo deve bater. */
    RTCHECK(rt.chrome.last_frame.pixels != (const uint8_t *)0,
            "pixels nao NULL");
    RTCHECK(rt.chrome.last_frame.pixels == rt.last_frame_storage,
            "pixels apontam para storage dedicado");
    RTCHECK(rt.chrome.last_frame.pixels[0] == 1u
            && rt.chrome.last_frame.pixels[15] == 16u,
            "pixels copiados corretamente");
    RTCHECK(rt.total_frames_persisted == 1u, "frames persistidos=1");
    RTCHECK(rt.total_frames_dropped == 0u, "frames descartados=0");
}

/* F3.3f triagem 2026-05-02: regressao para o alias-corruption.
 * Antes do fix, o dispatcher do chrome aliasava `last_frame.pixels`
 * em `event_scratch + 12u`. Um poll posterior (mesmo de TITLE/STATUS)
 * sobrescrevia o scratch, corrompendo os pixels mostrados em uma
 * janela que ainda nao tinha blittado o frame. O fix copia para
 * `last_frame_storage` e re-aponta `pixels` para esse buffer. Esse
 * teste injeta FRAME -> TITLE e verifica que os pixels do frame
 * continuam intactos apos o segundo poll. */
static void test_frame_pixels_survive_subsequent_poll(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    /* 2026-05-02: static to keep the ~8 MiB struct off the test
     * binary's stack after the EVENT_BUF_MAX bump to 4 MiB.
     * Each test resets all relevant fields via chrome_runtime_init
     * so re-entering the same static is safe. */
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);
    rt.chrome.current_nav_id = 7u;

    uint8_t pl[12 + 16];
    be_put_u32(&pl[0], 7u);
    be_put_u16(&pl[4], 2u);
    be_put_u16(&pl[6], 2u);
    be_put_u32(&pl[8], 8u);
    for (int i = 0; i < 16; ++i) pl[12 + i] = (uint8_t)(0xA0 + i);
    inject_event(1, BROWSER_IPC_EVENT_FRAME, 1, pl, sizeof(pl));

    uint32_t actions = 0u;
    int s = chrome_runtime_poll_event(&rt, 0u, &actions);
    RTCHECK(s == CHROME_RUNTIME_POLL_EVENT_HANDLED,
            "survive: FRAME poll HANDLED");
    RTCHECK(rt.chrome.last_frame.pixels == rt.last_frame_storage,
            "survive: pixels no storage dedicado");
    RTCHECK(rt.chrome.last_frame.pixels[0] == 0xA0u,
            "survive: pixel[0] inicial OK");

    /* Agora injeta TITLE com payload bem maior para forcar
     * sobrescrita do scratch nas posicoes onde o FRAME estava. */
    uint8_t tl[2 + 64];
    be_put_u16(&tl[0], 64u);
    for (int i = 0; i < 64; ++i) tl[2 + i] = (uint8_t)(0xFF - i);
    inject_event(1, BROWSER_IPC_EVENT_TITLE, 2, tl, sizeof(tl));

    s = chrome_runtime_poll_event(&rt, 0u, &actions);
    RTCHECK(s == CHROME_RUNTIME_POLL_EVENT_HANDLED,
            "survive: TITLE poll HANDLED");
    /* Agora last_frame.pixels NAO mudou e ainda tem os bytes
     * originais do FRAME (0xA0..0xAF), nao os do TITLE. */
    RTCHECK(rt.chrome.last_frame.pixels == rt.last_frame_storage,
            "survive: pixels ainda apontam para storage");
    RTCHECK(rt.chrome.last_frame.pixels[0] == 0xA0u
            && rt.chrome.last_frame.pixels[15] == 0xAFu,
            "survive: pixels do FRAME nao corrompidos pelo TITLE");
    RTCHECK(rt.total_frames_persisted == 1u,
            "survive: persisted continua 1");
}

/* Regressao adicional: dois FRAMEs consecutivos sobrescrevem o
 * storage corretamente, e total_frames_persisted incrementa. */
static void test_two_consecutive_frames_overwrite_storage(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    /* 2026-05-02: static to keep the ~8 MiB struct off the test
     * binary's stack after the EVENT_BUF_MAX bump to 4 MiB.
     * Each test resets all relevant fields via chrome_runtime_init
     * so re-entering the same static is safe. */
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);
    rt.chrome.current_nav_id = 9u;

    uint8_t pl1[12 + 16];
    be_put_u32(&pl1[0], 9u);
    be_put_u16(&pl1[4], 2u); be_put_u16(&pl1[6], 2u);
    be_put_u32(&pl1[8], 8u);
    for (int i = 0; i < 16; ++i) pl1[12 + i] = 0x10u;
    inject_event(1, BROWSER_IPC_EVENT_FRAME, 1, pl1, sizeof(pl1));

    uint32_t actions = 0u;
    int s = chrome_runtime_poll_event(&rt, 0u, &actions);
    RTCHECK(s == CHROME_RUNTIME_POLL_EVENT_HANDLED, "two: poll1 OK");
    RTCHECK(rt.chrome.last_frame.pixels[0] == 0x10u, "two: F1 pixel");

    uint8_t pl2[12 + 16];
    be_put_u32(&pl2[0], 9u);
    be_put_u16(&pl2[4], 2u); be_put_u16(&pl2[6], 2u);
    be_put_u32(&pl2[8], 8u);
    for (int i = 0; i < 16; ++i) pl2[12 + i] = 0x77u;
    inject_event(1, BROWSER_IPC_EVENT_FRAME, 2, pl2, sizeof(pl2));

    s = chrome_runtime_poll_event(&rt, 0u, &actions);
    RTCHECK(s == CHROME_RUNTIME_POLL_EVENT_HANDLED, "two: poll2 OK");
    RTCHECK(rt.chrome.last_frame.pixels[0] == 0x77u,
            "two: F2 sobrescreveu F1");
    RTCHECK(rt.total_frames_persisted == 2u, "two: persisted=2");
}

static void test_multiple_polls_drain_pipe(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    /* 2026-05-02: static to keep the ~8 MiB struct off the test
     * binary's stack after the EVENT_BUF_MAX bump to 4 MiB.
     * Each test resets all relevant fields via chrome_runtime_init
     * so re-entering the same static is safe. */
    static struct chrome_runtime rt;
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

/* Rate limiter tests: test_browser_chrome_runtime_rate.c. */
extern int test_browser_chrome_runtime_rate_run(int *p, int *f);

int test_browser_chrome_runtime_run(void) {
    printf("[test_browser_chrome_runtime]\n");
    g_rt_passed = 0;
    g_rt_failed = 0;
    test_init_resets_state();
    test_send_navigate_writes_frame();
    test_send_when_engine_dead_fails();
    test_send_broken_pipe_fails();
    test_send_resize_writes_frame();
    test_send_resize_zero_dim_rejected();
    test_send_resize_engine_dead_fails();
    test_send_click_writes_frame();
    test_send_click_engine_dead_fails();
    test_send_scroll_writes_frame();
    test_send_scroll_negative_delta();
    test_send_scroll_engine_dead_fails();
    test_send_key_writes_frame();
    test_send_key_printable_ascii();
    test_send_key_engine_dead_fails();
    test_send_key_null_rt_fails();
    test_send_reload_writes_frame();
    test_send_reload_engine_dead_fails();
    test_send_back_writes_frame();
    test_send_back_engine_dead_fails();
    test_send_forward_writes_frame();
    test_send_forward_engine_dead_fails();
    test_poll_title_event_updates_current_title();
    test_poll_no_data_returns_no_data();
    test_poll_eof_marks_engine_dead();
    test_poll_event_dispatches();
    test_poll_pong_routes_to_watchdog();
    test_tick_sends_ping_when_due();
    test_tick_returns_kill_after_timeouts();
    test_tick_returns_minus_one_on_broken_pipe();
    test_record_restart_clears_frame();
    test_frame_pixels_survive_subsequent_poll();
    test_two_consecutive_frames_overwrite_storage();
    test_send_payload_too_large_fails();
    test_poll_protocol_err_on_bad_magic();
    test_no_pipe_ops_returns_failure();
    test_poll_handles_frame_event();
    test_multiple_polls_drain_pipe();
    {
        int rp = 0, rf = 0;
        test_browser_chrome_runtime_rate_run(&rp, &rf);
        g_rt_passed += rp;
        g_rt_failed += rf;
    }
    printf("  -> %d passed, %d failed\n", g_rt_passed, g_rt_failed);
    return g_rt_failed;
}
