/* tests/test_browser_chrome_audit.c
 *
 * Etapa 5 hardening (2026-05-03): testes diretos do ring buffer de
 * audit log (`include/apps/browser_chrome_audit.h`), antes de testar
 * a integracao com chrome_runtime (essa fica em
 * test_browser_chrome_runtime_rate.c).
 */

#include "apps/browser_chrome_audit.h"
#include <stdio.h>
#include <stdint.h>

static int g_passed = 0;
static int g_failed = 0;

#define A_OK(cond, label)                                                 \
    do {                                                                  \
        if (cond) ++g_passed;                                             \
        else { ++g_failed;                                                \
               printf("    [FAIL] %s:%d %s\n", __FILE__, __LINE__, label); } \
    } while (0)

static void test_init_zeros_state(void) {
    struct chrome_audit_state st;
    /* Pre-fill com lixo para garantir que init zera de fato. */
    st.head = 0xDEADBEEFu;
    st.total = 0xCAFEBABEu;
    st.entries[0].category = 99u;
    capyc_audit_init(&st);
    A_OK(st.head == 0u, "init: head=0");
    A_OK(st.total == 0u, "init: total=0");
    A_OK(st.entries[0].category == (uint8_t)CAPYC_AUDIT_UNKNOWN,
         "init: entries zeradas");
    A_OK(capyc_audit_count(&st) == 0u, "init: count=0");
    A_OK(capyc_audit_visible(&st) == 0u, "init: visible=0");
    A_OK(capyc_audit_at(&st, 0) == (const struct chrome_audit_entry *)0,
         "init: at(0) = NULL");
}

static void test_record_appends_in_order(void) {
    struct chrome_audit_state st;
    capyc_audit_init(&st);

    capyc_audit_record(&st, (uint8_t)CAPYC_AUDIT_NAV, 100u);
    capyc_audit_record(&st, (uint8_t)CAPYC_AUDIT_FETCH, 200u);
    capyc_audit_record(&st, (uint8_t)CAPYC_AUDIT_RATE_DROP, 300u);

    A_OK(capyc_audit_count(&st) == 3u, "record: count=3");
    A_OK(capyc_audit_visible(&st) == 3u, "record: visible=3");

    const struct chrome_audit_entry *e0 = capyc_audit_at(&st, 0);
    const struct chrome_audit_entry *e1 = capyc_audit_at(&st, 1);
    const struct chrome_audit_entry *e2 = capyc_audit_at(&st, 2);

    A_OK(e0 && e0->category == (uint8_t)CAPYC_AUDIT_NAV,
         "record: e0 = NAV");
    A_OK(e0 && e0->code == 100u, "record: e0 code=100");
    A_OK(e0 && e0->seq == 1u, "record: e0 seq=1");

    A_OK(e1 && e1->category == (uint8_t)CAPYC_AUDIT_FETCH,
         "record: e1 = FETCH");
    A_OK(e1 && e1->code == 200u, "record: e1 code=200");
    A_OK(e1 && e1->seq == 2u, "record: e1 seq=2");

    A_OK(e2 && e2->category == (uint8_t)CAPYC_AUDIT_RATE_DROP,
         "record: e2 = RATE_DROP");
    A_OK(e2 && e2->seq == 3u, "record: e2 seq=3");

    A_OK(capyc_audit_at(&st, 3) == (const struct chrome_audit_entry *)0,
         "record: at(visible) = NULL");
}

static void test_ring_wraparound_drops_oldest(void) {
    /* Preenche RING_SIZE + 5 entries; os 5 mais antigos devem ter sido
     * sobrescritos. visible = RING_SIZE; at(0) deve mostrar o
     * primeiro NAO-sobrescrito (i.e., entry com seq = 6). */
    struct chrome_audit_state st;
    capyc_audit_init(&st);
    const uint32_t N = CHROME_AUDIT_RING_SIZE + 5u;
    for (uint32_t i = 0; i < N; ++i) {
        capyc_audit_record(&st, (uint8_t)CAPYC_AUDIT_NAV, (uint16_t)i);
    }
    A_OK(capyc_audit_count(&st) == N, "wrap: count=N (monotonic)");
    A_OK(capyc_audit_visible(&st) == CHROME_AUDIT_RING_SIZE,
         "wrap: visible = RING_SIZE");

    const struct chrome_audit_entry *oldest = capyc_audit_at(&st, 0);
    A_OK(oldest && oldest->seq == 6u,
         "wrap: at(0) = primeiro NAO sobrescrito (seq=6)");
    A_OK(oldest && oldest->code == 5u, "wrap: at(0) code=5");

    const struct chrome_audit_entry *newest =
        capyc_audit_at(&st, CHROME_AUDIT_RING_SIZE - 1u);
    A_OK(newest && newest->seq == N, "wrap: at(end) = ultimo gravado");
    A_OK(newest && newest->code == (uint16_t)(N - 1u),
         "wrap: at(end) code = N-1");

    /* at(RING_SIZE) deve retornar NULL. */
    A_OK(capyc_audit_at(&st, CHROME_AUDIT_RING_SIZE) ==
            (const struct chrome_audit_entry *)0,
         "wrap: at(visible) = NULL");
}

static void test_null_safety(void) {
    /* Nao deve crash. */
    capyc_audit_init((struct chrome_audit_state *)0);
    capyc_audit_record((struct chrome_audit_state *)0, 0u, 0u);
    capyc_audit_set_sink((struct chrome_audit_state *)0,
                         (capyc_audit_sink_fn)0);
    A_OK(capyc_audit_count((const struct chrome_audit_state *)0) == 0u,
         "null: count=0");
    A_OK(capyc_audit_visible((const struct chrome_audit_state *)0) == 0u,
         "null: visible=0");
    A_OK(capyc_audit_at((const struct chrome_audit_state *)0, 0) ==
            (const struct chrome_audit_entry *)0,
         "null: at = NULL");
}

/* Etapa 5 hardening (2026-05-05 sessao 3): callback sink recebe cada
 * record. Mockamos via variaveis estaticas porque o sink eh ABI sem
 * void* user_data por design (manter API minima; callers que precisam
 * de contexto fazem global ou wrappers). */
static uint32_t g_sink_calls = 0u;
static uint8_t  g_sink_last_category = 0u;
static uint16_t g_sink_last_code = 0u;
static uint32_t g_sink_last_seq = 0u;

static void test_sink(uint8_t category, uint16_t code, uint32_t seq) {
    g_sink_calls++;
    g_sink_last_category = category;
    g_sink_last_code = code;
    g_sink_last_seq = seq;
}

static void test_sink_default_is_null(void) {
    struct chrome_audit_state st;
    capyc_audit_init(&st);
    /* Sem sink instalado: record nao deve crashar nem invocar
     * nada. Assertimos que o ring foi atualizado mesmo assim. */
    capyc_audit_record(&st, (uint8_t)CAPYC_AUDIT_NAV, 1u);
    A_OK(capyc_audit_count(&st) == 1u, "sink default: count=1");
    A_OK(st.sink == (capyc_audit_sink_fn)0,
         "sink default: ponteiro = NULL");
}

static void test_sink_called_per_record(void) {
    struct chrome_audit_state st;
    capyc_audit_init(&st);
    g_sink_calls = 0u;
    g_sink_last_category = 0u;
    g_sink_last_code = 0u;
    g_sink_last_seq = 0u;
    capyc_audit_set_sink(&st, test_sink);

    capyc_audit_record(&st, (uint8_t)CAPYC_AUDIT_NAV, 100u);
    A_OK(g_sink_calls == 1u, "sink: 1 call apos 1 record");
    A_OK(g_sink_last_category == (uint8_t)CAPYC_AUDIT_NAV,
         "sink: category propagado");
    A_OK(g_sink_last_code == 100u, "sink: code propagado");
    A_OK(g_sink_last_seq == 1u, "sink: seq=1 (primeira entry)");

    capyc_audit_record(&st, (uint8_t)CAPYC_AUDIT_FETCH, 200u);
    A_OK(g_sink_calls == 2u, "sink: 2 calls apos 2 records");
    A_OK(g_sink_last_category == (uint8_t)CAPYC_AUDIT_FETCH,
         "sink: segundo category");
    A_OK(g_sink_last_code == 200u, "sink: segundo code");
    A_OK(g_sink_last_seq == 2u, "sink: seq=2");
}

static void test_sink_can_be_replaced_and_disabled(void) {
    struct chrome_audit_state st;
    capyc_audit_init(&st);
    g_sink_calls = 0u;
    capyc_audit_set_sink(&st, test_sink);
    capyc_audit_record(&st, (uint8_t)CAPYC_AUDIT_NAV, 1u);
    A_OK(g_sink_calls == 1u, "replace: 1 call enquanto sink ativo");

    /* Desabilita. */
    capyc_audit_set_sink(&st, (capyc_audit_sink_fn)0);
    capyc_audit_record(&st, (uint8_t)CAPYC_AUDIT_NAV, 2u);
    A_OK(g_sink_calls == 1u,
         "disable: count nao incrementa apos sink=NULL");
    A_OK(capyc_audit_count(&st) == 2u,
         "disable: ring continua aceitando records");

    /* Re-habilita. */
    capyc_audit_set_sink(&st, test_sink);
    capyc_audit_record(&st, (uint8_t)CAPYC_AUDIT_NAV, 3u);
    A_OK(g_sink_calls == 2u, "re-enable: sink chamado de novo");
}

int test_browser_chrome_audit_run(void) {
    printf("[test_browser_chrome_audit]\n");
    g_passed = 0;
    g_failed = 0;
    test_init_zeros_state();
    test_record_appends_in_order();
    test_ring_wraparound_drops_oldest();
    test_null_safety();
    test_sink_default_is_null();
    test_sink_called_per_record();
    test_sink_can_be_replaced_and_disabled();
    printf("  -> %d/%d passed\n", g_passed, g_passed + g_failed);
    return g_failed;
}
