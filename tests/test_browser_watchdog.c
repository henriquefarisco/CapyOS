/*
 * test_browser_watchdog.c (F3.3d)
 *
 * Cobre a maquina de estado pura do watchdog do browser engine.
 * Estado e tempo sao injetados; nao ha syscall envolvido.
 *
 * Cenarios:
 *   - init deixa watchdog idle e pronto para o primeiro ping.
 *   - should_send_ping respeita PING_INTERVAL.
 *   - record_ping move para PING_IN_FLIGHT.
 *   - record_pong com nonce certo libera para idle.
 *   - record_pong com nonce errado e ignorado.
 *   - record_pong sem ping em voo e ignorado.
 *   - tick sem timeout nao avanca consecutive_misses.
 *   - tick com timeout incrementa consecutive_misses.
 *   - apos MAX_MISSED_PONGS, watchdog pede kill.
 *   - record_restart limpa estado e incrementa total_kills.
 *   - alloc_nonce e monotonico e pula 0.
 *   - apos timeout, novo PING so e autorizado depois do intervalo.
 *   - nonce wrap-around respeita 0 reservado.
 *   - PONG correto interrompe sequencia de misses.
 */

#include "apps/browser_watchdog.h"
#include <stdint.h>
#include <stdio.h>

static int g_w_passed = 0;
static int g_w_failed = 0;

#define WCHECK(cond, label)                                              \
    do {                                                                 \
        if (cond) {                                                      \
            ++g_w_passed;                                                \
        } else {                                                         \
            ++g_w_failed;                                                \
            printf("    [FAIL] %s:%d %s\n", __FILE__, __LINE__, label);  \
        }                                                                \
    } while (0)

static void test_init_state(void) {
    struct browser_watchdog w;
    browser_watchdog_init(&w, 1000u);
    WCHECK(w.state == BROWSER_WATCHDOG_IDLE, "init -> IDLE");
    WCHECK(w.consecutive_misses == 0u, "init misses=0");
    WCHECK(w.total_kills == 0u, "init kills=0");
    WCHECK(!browser_watchdog_should_kill(&w), "init !should_kill");
    WCHECK(browser_watchdog_total_kills(&w) == 0u, "init total_kills accessor");
}

static void test_should_send_ping_respects_interval(void) {
    struct browser_watchdog w;
    browser_watchdog_init(&w, 1000u);
    /* Imediato apos init: last_ping_tick = now, intervalo nao passou */
    WCHECK(!browser_watchdog_should_send_ping(&w, 1000u),
           "no ping at t=0");
    WCHECK(!browser_watchdog_should_send_ping(&w, 1000u + BROWSER_WATCHDOG_PING_INTERVAL_TICKS - 1u),
           "no ping just before interval");
    WCHECK(browser_watchdog_should_send_ping(&w, 1000u + BROWSER_WATCHDOG_PING_INTERVAL_TICKS),
           "ping at exact interval");
    WCHECK(browser_watchdog_should_send_ping(&w, 1000u + BROWSER_WATCHDOG_PING_INTERVAL_TICKS + 100u),
           "ping after interval");
}

static void test_record_ping_blocks_further_pings(void) {
    struct browser_watchdog w;
    browser_watchdog_init(&w, 0u);
    uint64_t t = BROWSER_WATCHDOG_PING_INTERVAL_TICKS;
    WCHECK(browser_watchdog_should_send_ping(&w, t), "first ping authorized");
    uint32_t n = browser_watchdog_alloc_nonce(&w);
    browser_watchdog_record_ping(&w, n, t);
    WCHECK(w.state == BROWSER_WATCHDOG_PING_IN_FLIGHT,
           "record_ping -> PING_IN_FLIGHT");
    WCHECK(!browser_watchdog_should_send_ping(&w, t + 1000u),
           "no ping while one is in flight");
}

static void test_record_pong_matching_nonce(void) {
    struct browser_watchdog w;
    browser_watchdog_init(&w, 0u);
    uint32_t n = browser_watchdog_alloc_nonce(&w);
    browser_watchdog_record_ping(&w, n, BROWSER_WATCHDOG_PING_INTERVAL_TICKS);
    browser_watchdog_record_pong(&w, n, BROWSER_WATCHDOG_PING_INTERVAL_TICKS + 5u);
    WCHECK(w.state == BROWSER_WATCHDOG_IDLE, "matched pong -> IDLE");
    WCHECK(w.consecutive_misses == 0u, "pong clears misses");
    WCHECK(w.last_pong_tick == BROWSER_WATCHDOG_PING_INTERVAL_TICKS + 5u,
           "last_pong_tick updated");
}

static void test_record_pong_wrong_nonce_ignored(void) {
    struct browser_watchdog w;
    browser_watchdog_init(&w, 0u);
    uint32_t n = browser_watchdog_alloc_nonce(&w);
    browser_watchdog_record_ping(&w, n, BROWSER_WATCHDOG_PING_INTERVAL_TICKS);
    browser_watchdog_record_pong(&w, n + 999u, BROWSER_WATCHDOG_PING_INTERVAL_TICKS + 5u);
    WCHECK(w.state == BROWSER_WATCHDOG_PING_IN_FLIGHT,
           "wrong-nonce pong: still PING_IN_FLIGHT");
}

static void test_record_pong_without_ping_ignored(void) {
    struct browser_watchdog w;
    browser_watchdog_init(&w, 0u);
    browser_watchdog_record_pong(&w, 42u, 100u);
    WCHECK(w.state == BROWSER_WATCHDOG_IDLE, "pong sem ping: ignorado");
    WCHECK(w.consecutive_misses == 0u, "pong sem ping: misses=0");
}

static void test_tick_no_timeout(void) {
    struct browser_watchdog w;
    browser_watchdog_init(&w, 0u);
    uint32_t n = browser_watchdog_alloc_nonce(&w);
    browser_watchdog_record_ping(&w, n, 1000u);
    browser_watchdog_tick(&w, 1000u + BROWSER_WATCHDOG_PONG_TIMEOUT_TICKS - 1u);
    WCHECK(w.state == BROWSER_WATCHDOG_PING_IN_FLIGHT,
           "tick antes do timeout: ainda PING_IN_FLIGHT");
    WCHECK(w.consecutive_misses == 0u, "tick antes do timeout: misses=0");
}

static void test_tick_timeout_increments_misses(void) {
    struct browser_watchdog w;
    browser_watchdog_init(&w, 0u);
    uint32_t n = browser_watchdog_alloc_nonce(&w);
    browser_watchdog_record_ping(&w, n, 1000u);
    browser_watchdog_tick(&w, 1000u + BROWSER_WATCHDOG_PONG_TIMEOUT_TICKS);
    WCHECK(w.consecutive_misses == 1u, "primeiro timeout: misses=1");
    /* Apos timeout < MAX, watchdog volta para IDLE para permitir
     * proximo PING quando intervalo passar. */
    if (BROWSER_WATCHDOG_MAX_MISSED_PONGS > 1u) {
        WCHECK(w.state == BROWSER_WATCHDOG_IDLE,
               "timeout sob MAX -> IDLE");
        WCHECK(!browser_watchdog_should_kill(&w),
               "timeout sob MAX !should_kill");
    }
}

static void test_kill_after_max_misses(void) {
    struct browser_watchdog w;
    browser_watchdog_init(&w, 0u);
    uint64_t t = 0u;
    /* Forca MAX_MISSED_PONGS timeouts consecutivos. */
    for (uint32_t i = 0; i < BROWSER_WATCHDOG_MAX_MISSED_PONGS; ++i) {
        t += BROWSER_WATCHDOG_PING_INTERVAL_TICKS;
        WCHECK(browser_watchdog_should_send_ping(&w, t),
               "should_send_ping em t=interval*N");
        uint32_t n = browser_watchdog_alloc_nonce(&w);
        browser_watchdog_record_ping(&w, n, t);
        t += BROWSER_WATCHDOG_PONG_TIMEOUT_TICKS;
        browser_watchdog_tick(&w, t);
    }
    WCHECK(w.consecutive_misses == BROWSER_WATCHDOG_MAX_MISSED_PONGS,
           "consecutive_misses == MAX");
    WCHECK(w.state == BROWSER_WATCHDOG_KILL_REQUESTED,
           "state == KILL_REQUESTED");
    WCHECK(browser_watchdog_should_kill(&w), "should_kill true");
    WCHECK(!browser_watchdog_should_send_ping(&w, t + 100000u),
           "no ping while kill pending");
}

static void test_record_restart_clears_state(void) {
    struct browser_watchdog w;
    browser_watchdog_init(&w, 0u);
    /* Atinge kill */
    uint64_t t = 0u;
    for (uint32_t i = 0; i < BROWSER_WATCHDOG_MAX_MISSED_PONGS; ++i) {
        t += BROWSER_WATCHDOG_PING_INTERVAL_TICKS;
        uint32_t n = browser_watchdog_alloc_nonce(&w);
        browser_watchdog_record_ping(&w, n, t);
        t += BROWSER_WATCHDOG_PONG_TIMEOUT_TICKS;
        browser_watchdog_tick(&w, t);
    }
    WCHECK(browser_watchdog_should_kill(&w), "kill armed pre-restart");

    browser_watchdog_record_restart(&w, t + 1u);
    WCHECK(w.state == BROWSER_WATCHDOG_IDLE, "restart -> IDLE");
    WCHECK(w.consecutive_misses == 0u, "restart clears misses");
    WCHECK(w.total_kills == 1u, "restart increments total_kills");
    WCHECK(!browser_watchdog_should_kill(&w), "restart !should_kill");
    WCHECK(browser_watchdog_total_kills(&w) == 1u,
           "total_kills accessor");
}

static void test_alloc_nonce_monotonic_skips_zero(void) {
    struct browser_watchdog w;
    browser_watchdog_init(&w, 0u);
    uint32_t a = browser_watchdog_alloc_nonce(&w);
    uint32_t b = browser_watchdog_alloc_nonce(&w);
    uint32_t c = browser_watchdog_alloc_nonce(&w);
    WCHECK(a != 0u && b != 0u && c != 0u, "nonce nunca eh 0");
    WCHECK(b == a + 1u, "nonce monotonico");
    WCHECK(c == b + 1u, "nonce monotonico (cont)");

    /* Forca wrap-around: pega o estado interno e injeta proximo perto de UINT32_MAX */
    w.next_nonce = (uint32_t)0xFFFFFFFFu;
    uint32_t pre_wrap = browser_watchdog_alloc_nonce(&w);
    uint32_t post_wrap = browser_watchdog_alloc_nonce(&w);
    WCHECK(pre_wrap == 0xFFFFFFFFu, "alloca UINT32_MAX");
    WCHECK(post_wrap == 1u, "wrap pula 0 e retorna 1");
}

static void test_pong_breaks_miss_streak(void) {
    struct browser_watchdog w;
    browser_watchdog_init(&w, 0u);
    uint64_t t = BROWSER_WATCHDOG_PING_INTERVAL_TICKS;
    /* Primeiro miss */
    uint32_t n1 = browser_watchdog_alloc_nonce(&w);
    browser_watchdog_record_ping(&w, n1, t);
    t += BROWSER_WATCHDOG_PONG_TIMEOUT_TICKS;
    browser_watchdog_tick(&w, t);
    WCHECK(w.consecutive_misses == 1u, "1 miss");

    /* Pong valido: zera streak */
    t += BROWSER_WATCHDOG_PING_INTERVAL_TICKS;
    uint32_t n2 = browser_watchdog_alloc_nonce(&w);
    browser_watchdog_record_ping(&w, n2, t);
    browser_watchdog_record_pong(&w, n2, t + 5u);
    WCHECK(w.consecutive_misses == 0u, "pong zera streak");
    WCHECK(w.state == BROWSER_WATCHDOG_IDLE, "pong -> IDLE");
}

static void test_kill_state_blocks_pong(void) {
    struct browser_watchdog w;
    browser_watchdog_init(&w, 0u);
    /* Forca KILL_REQUESTED */
    uint64_t t = 0u;
    for (uint32_t i = 0; i < BROWSER_WATCHDOG_MAX_MISSED_PONGS; ++i) {
        t += BROWSER_WATCHDOG_PING_INTERVAL_TICKS;
        uint32_t n = browser_watchdog_alloc_nonce(&w);
        browser_watchdog_record_ping(&w, n, t);
        t += BROWSER_WATCHDOG_PONG_TIMEOUT_TICKS;
        browser_watchdog_tick(&w, t);
    }
    /* Pong tardio nao desfaz kill (politica firme: o engine ja estava
     * morto na visao do chrome). */
    browser_watchdog_record_pong(&w, 1u, t + 1u);
    WCHECK(browser_watchdog_should_kill(&w),
           "pong tardio nao desarma kill");

    /* record_ping enquanto KILL_REQUESTED tambem nao deve mudar nada. */
    browser_watchdog_record_ping(&w, 999u, t + 1u);
    WCHECK(w.state == BROWSER_WATCHDOG_KILL_REQUESTED,
           "record_ping em KILL_REQUESTED nao muda estado");
}

static void test_null_args_safe(void) {
    /* Todas as funcoes devem tolerar NULL. */
    browser_watchdog_init(NULL, 0u);
    browser_watchdog_record_ping(NULL, 1u, 0u);
    browser_watchdog_record_pong(NULL, 1u, 0u);
    browser_watchdog_record_restart(NULL, 0u);
    browser_watchdog_tick(NULL, 0u);
    WCHECK(browser_watchdog_should_send_ping(NULL, 0u) == 0,
           "should_send_ping(NULL) == 0");
    WCHECK(browser_watchdog_should_kill(NULL) == 0,
           "should_kill(NULL) == 0");
    WCHECK(browser_watchdog_alloc_nonce(NULL) == 0u,
           "alloc_nonce(NULL) == 0");
    WCHECK(browser_watchdog_total_kills(NULL) == 0u,
           "total_kills(NULL) == 0");
}

int test_browser_watchdog_run(void) {
    printf("[test_browser_watchdog]\n");
    g_w_passed = 0;
    g_w_failed = 0;
    test_init_state();
    test_should_send_ping_respects_interval();
    test_record_ping_blocks_further_pings();
    test_record_pong_matching_nonce();
    test_record_pong_wrong_nonce_ignored();
    test_record_pong_without_ping_ignored();
    test_tick_no_timeout();
    test_tick_timeout_increments_misses();
    test_kill_after_max_misses();
    test_record_restart_clears_state();
    test_alloc_nonce_monotonic_skips_zero();
    test_pong_breaks_miss_streak();
    test_kill_state_blocks_pong();
    test_null_args_safe();
    printf("  -> %d passed, %d failed\n", g_w_passed, g_w_failed);
    return g_w_failed;
}
