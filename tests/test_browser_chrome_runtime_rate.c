/*
 * test_browser_chrome_runtime_rate.c (Etapa 5 hardening 2026-05-03)
 *
 * Sub-suite extraida de test_browser_chrome_runtime.c quando a soma
 * dos testes ultrapassou o line-limit do audit_source_layout (900).
 * Cobre o rate limiter incoming do chrome_runtime:
 *   1. init zera os contadores;
 *   2. burst > MAX gera RATE_LIMITED para os excedentes;
 *   3. tick reseta a janela e admite eventos pendentes.
 *
 * O runner principal do test_browser_chrome_runtime invoca
 * `test_browser_chrome_runtime_rate_run` e propaga os contadores.
 */

#include "test_browser_chrome_runtime_mock.h"
#include "apps/browser_chrome_runtime.h"
#include "apps/browser_ipc.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define be_put_u16   mock_be_put_u16
#define be_put_u32   mock_be_put_u32
#define inject_event mock_inject_event

static int g_passed = 0;
static int g_failed = 0;

#define R_OK(cond, label)                                                 \
    do {                                                                  \
        if (cond) ++g_passed;                                             \
        else { ++g_failed;                                                \
               printf("    [FAIL] %s:%d %s\n", __FILE__, __LINE__, label); } \
    } while (0)

static void inject_title(int pipe_id, uint32_t seq) {
    uint8_t pl[6];
    be_put_u16(&pl[0], 4u);
    memcpy(&pl[2], "abcd", 4);
    inject_event(pipe_id, BROWSER_IPC_EVENT_TITLE, seq, pl, 6u);
}

static void test_rate_init_zeroed(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);
    R_OK(rt.incoming_in_window == 0u, "rate: window=0 apos init");
    R_OK(rt.total_incoming_drops == 0u, "rate: drops=0 apos init");
}

static void test_rate_burst_drops_after_max(void) {
    /* Injeta MAX + 4 eventos sem tick. Os primeiros MAX devem ser
     * admitidos; os restantes 4 retornam RATE_LIMITED. */
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);

    const uint32_t MAX = CHROME_RUNTIME_INCOMING_RATE_MAX;
    const uint32_t EXTRA = 4u;
    for (uint32_t i = 0; i < MAX + EXTRA; ++i) inject_title(1, i + 1u);

    uint32_t actions = 0u;
    uint32_t admitted = 0u, rate_limited = 0u;
    for (uint32_t i = 0; i < MAX + EXTRA; ++i) {
        int s = chrome_runtime_poll_event(&rt, 0u, &actions);
        if (s == CHROME_RUNTIME_POLL_EVENT_HANDLED) admitted++;
        else if (s == CHROME_RUNTIME_POLL_RATE_LIMITED) rate_limited++;
    }
    R_OK(admitted == MAX, "rate: admitiu exatamente MAX eventos");
    R_OK(rate_limited == EXTRA, "rate: rate_limited == EXTRA");
    R_OK(rt.total_incoming_drops == EXTRA,
         "rate: total_incoming_drops conta drops");
    R_OK(rt.total_events_polled == MAX,
         "rate: total_events_polled = MAX (drops nao contam)");
}

static void test_rate_tick_resets_window(void) {
    /* Esgota budget, tick, depois admite mais novamente. */
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);

    const uint32_t MAX = CHROME_RUNTIME_INCOMING_RATE_MAX;
    for (uint32_t i = 0; i < MAX + 1u; ++i) inject_title(1, i + 1u);

    uint32_t actions = 0u;
    for (uint32_t i = 0; i < MAX; ++i) {
        chrome_runtime_poll_event(&rt, 0u, &actions);
    }
    int s = chrome_runtime_poll_event(&rt, 0u, &actions);
    R_OK(s == CHROME_RUNTIME_POLL_RATE_LIMITED,
         "rate: round1 ultimo evento = RATE_LIMITED");

    /* Tick reseta. */
    chrome_runtime_tick(&rt, 100u);
    R_OK(rt.incoming_in_window == 0u, "rate: tick zerou window");

    /* Round 2: o evento MAX+1 ainda esta no buffer; admite agora. */
    s = chrome_runtime_poll_event(&rt, 100u, &actions);
    R_OK(s == CHROME_RUNTIME_POLL_EVENT_HANDLED,
         "rate: pos-tick admite evento pendente");
}

/* === Etapa 5 hardening (2026-05-03): URL whitelist policy ============ */

/* Politica de teste: nega tudo que comece com "deny://"; permite o
 * resto. Mantem global para o callback nao precisar de closure. */
static int test_policy_deny_prefix(const char *url, uint32_t len) {
    if (len >= 7u && memcmp(url, "deny://", 7) == 0) return 0;
    return 1;
}

static int test_policy_deny_all(const char *url, uint32_t len) {
    (void)url; (void)len;
    return 0;
}

static void test_url_policy_init_zeroed(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);
    R_OK(rt.total_url_blocked == 0u, "policy: total_url_blocked=0 init");
}

static void test_url_policy_deny_blocks_navigate(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);
    chrome_runtime_set_url_policy(test_policy_deny_all);

    int rc = chrome_runtime_send_navigate(&rt, "http://x", 8);
    R_OK(rc == -1, "policy deny-all: navigate returns -1");
    R_OK(rt.total_url_blocked == 1u, "policy: contador incrementa");
    R_OK(rt.total_requests_sent == 0u, "policy: pipe nao foi tocado");

    /* Reset policy para nao vazar para outros tests. */
    chrome_runtime_set_url_policy((chrome_runtime_url_policy_fn)0);
}

static void test_url_policy_allow_passes_through(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);
    chrome_runtime_set_url_policy(test_policy_deny_prefix);

    int rc = chrome_runtime_send_navigate(&rt, "http://ok", 9);
    R_OK(rc == 0, "policy allow: navigate returns 0");
    R_OK(rt.total_url_blocked == 0u, "policy allow: contador 0");
    R_OK(rt.total_requests_sent == 1u, "policy allow: pipe foi tocado");

    rc = chrome_runtime_send_navigate(&rt, "deny://blocked", 14);
    R_OK(rc == -1, "policy deny match: navigate -1");
    R_OK(rt.total_url_blocked == 1u, "policy: deny match incrementa");
    R_OK(rt.total_requests_sent == 1u, "policy: deny nao toca pipe");

    chrome_runtime_set_url_policy((chrome_runtime_url_policy_fn)0);
}

/* === Etapa 5 hardening observability (2026-05-03): bytes counter ===== */

static void test_event_bytes_observability(void) {
    /* total_event_bytes_received = soma de (HEADER_SIZE + payload_len)
     * para cada evento admitido. Util para detectar engine spam. */
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);
    R_OK(rt.total_event_bytes_received == 0u, "bytes: 0 apos init");

    /* Inject 3 EVENT_TITLE com payload de 6 bytes cada. */
    for (uint32_t i = 0; i < 3u; ++i) inject_title(1, i + 1u);

    uint32_t actions = 0u;
    for (int i = 0; i < 3; ++i) {
        chrome_runtime_poll_event(&rt, 0u, &actions);
    }
    /* 3 eventos * (12 + 6) = 54 bytes esperados. */
    R_OK(rt.total_event_bytes_received ==
            3u * (BROWSER_IPC_HEADER_SIZE + 6u),
         "bytes: header+payload acumulado");
}

/* === Etapa 5 hardening (2026-05-03): audit log integration ============= */

#include "apps/browser_chrome_audit.h"

static int audit_policy_deny_all(const char *u, uint32_t l) {
    (void)u; (void)l; return 0;
}

static void test_audit_navigate_records_nav_entry(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);
    R_OK(capyc_audit_count(&rt.audit) == 0u, "audit: 0 apos init");

    int rc = chrome_runtime_send_navigate(&rt, "http://x", 8);
    R_OK(rc == 0, "audit nav: send ok");
    R_OK(capyc_audit_count(&rt.audit) == 1u,
         "audit nav: gravou 1 entry");
    const struct chrome_audit_entry *e = capyc_audit_at(&rt.audit, 0);
    R_OK(e && e->category == (uint8_t)CAPYC_AUDIT_NAV,
         "audit nav: categoria = NAV");
    R_OK(e && e->code == 8u, "audit nav: code = url_len");
}

static void test_audit_policy_deny_records_deny(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);
    chrome_runtime_set_url_policy(audit_policy_deny_all);

    int rc = chrome_runtime_send_navigate(&rt, "evil://x", 8);
    R_OK(rc == -1, "audit deny: send -1");
    R_OK(capyc_audit_count(&rt.audit) == 1u,
         "audit deny: 1 entry gravada");
    const struct chrome_audit_entry *e = capyc_audit_at(&rt.audit, 0);
    R_OK(e && e->category == (uint8_t)CAPYC_AUDIT_POLICY_DENY,
         "audit deny: categoria = POLICY_DENY");

    chrome_runtime_set_url_policy((chrome_runtime_url_policy_fn)0);
}

static void test_audit_rate_drop_records_rate(void) {
    mock_pipe_reset_all();
    mock_pipe_install_ops();
    static struct chrome_runtime rt;
    chrome_runtime_init(&rt, 0, 1, 1u, 0u);

    const uint32_t MAX = CHROME_RUNTIME_INCOMING_RATE_MAX;
    /* Inject MAX + 2 events. */
    for (uint32_t i = 0; i < MAX + 2u; ++i) inject_title(1, i + 1u);
    uint32_t actions = 0u;
    for (uint32_t i = 0; i < MAX + 2u; ++i) {
        chrome_runtime_poll_event(&rt, 0u, &actions);
    }
    /* Os 2 ultimos viram RATE_LIMITED, gerando 2 entries de RATE_DROP. */
    int rate_count = 0;
    uint32_t vis = capyc_audit_visible(&rt.audit);
    for (uint32_t k = 0; k < vis; ++k) {
        const struct chrome_audit_entry *e = capyc_audit_at(&rt.audit, k);
        if (e && e->category == (uint8_t)CAPYC_AUDIT_RATE_DROP) rate_count++;
    }
    R_OK(rate_count == 2, "audit rate: 2 entries de RATE_DROP");
}

int test_browser_chrome_runtime_rate_run(int *out_passed, int *out_failed) {
    g_passed = 0;
    g_failed = 0;
    test_rate_init_zeroed();
    test_rate_burst_drops_after_max();
    test_rate_tick_resets_window();
    test_url_policy_init_zeroed();
    test_url_policy_deny_blocks_navigate();
    test_url_policy_allow_passes_through();
    test_event_bytes_observability();
    test_audit_navigate_records_nav_entry();
    test_audit_policy_deny_records_deny();
    test_audit_rate_drop_records_rate();
    if (out_passed) *out_passed = g_passed;
    if (out_failed) *out_failed = g_failed;
    return g_failed;
}
