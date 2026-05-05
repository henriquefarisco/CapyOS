/* include/apps/browser_chrome_audit.h
 *
 * Etapa 5 hardening (2026-05-03): subsistema dedicado de audit log
 * para o chrome do browser. Captura eventos relevantes a seguranca
 * (navegacoes, rate limits, deny por policy, lifecycle do engine,
 * fetches) em um ring buffer in-memory + opcional callback do caller
 * (kernel pode wirar para debugcon, testes pegam direto do struct).
 *
 * Por que um modulo separado:
 *   - Categorias bem definidas (enum) ao inves de strings ad-hoc no
 *     debugcon scattered pelo codigo.
 *   - Ring buffer de tamanho fixo => sem alocacao dinamica e
 *     visibilidade O(1) para inspecao em testes/forensics.
 *   - Permite politica futura de "audit log de seguranca" exportada
 *     ao usuario sem misturar com o codigo de I/O.
 *
 * Contrato:
 *   - capyc_audit_init(state) zera o ring.
 *   - capyc_audit_record(state, cat, code) anexa uma entrada (overwrite
 *     mais antiga se ring cheio).
 *   - capyc_audit_count(state) retorna numero de entradas anexadas
 *     desde init (monotonico; pode exceder CHROME_AUDIT_RING_SIZE).
 *   - capyc_audit_at(state, k) retorna a k-esima entrada do ring
 *     (ordenada por insercao, mais antiga primeiro), NULL se k >= used.
 *
 * Uso esperado (chrome runtime):
 *   capyc_audit_record(&rt->audit, CAPYC_AUDIT_NAV, 0);
 *   capyc_audit_record(&rt->audit, CAPYC_AUDIT_RATE_DROP, 1);
 *   capyc_audit_record(&rt->audit, CAPYC_AUDIT_POLICY_DENY, 2);
 */
#ifndef APPS_BROWSER_CHROME_AUDIT_H
#define APPS_BROWSER_CHROME_AUDIT_H

#include <stdint.h>

/* Tamanho do ring; potencia de 2 para evitar mod. 32 entradas
 * cobrem facilmente um log de sessao para inspecao manual. */
#define CHROME_AUDIT_RING_SIZE      32u

/* Categorias de eventos. Sao estaveis (nao reordene; valores
 * podem aparecer em telemetria persistida). */
enum chrome_audit_category {
    CAPYC_AUDIT_UNKNOWN          = 0u,
    CAPYC_AUDIT_NAV              = 1u, /* NAVIGATE enviado */
    CAPYC_AUDIT_RATE_DROP        = 2u, /* poll_event excedeu budget */
    CAPYC_AUDIT_POLICY_DENY      = 3u, /* URL bloqueada por whitelist */
    CAPYC_AUDIT_ENGINE_EOF       = 4u, /* engine fechou pipe */
    CAPYC_AUDIT_PROTOCOL         = 5u, /* protocolo violado pelo engine */
    CAPYC_AUDIT_FETCH            = 6u, /* fetch despachado */
    /* Etapa 5 hardening (2026-05-05): engine excedeu o budget de bytes
     * IPC permitido por navegacao. `code` carrega o KiB acumulados
     * truncados (uint16). Vem acompanhado de engine_alive=0 e
     * total_nav_budget_kills++ para o caller respawnar. */
    CAPYC_AUDIT_BUDGET_EXCEEDED  = 7u
};

struct chrome_audit_entry {
    uint8_t  category;  /* enum chrome_audit_category */
    uint8_t  reserved;
    uint16_t code;      /* parametro especifico da categoria */
    uint32_t seq;       /* numero monotonico desde init */
};

/* Etapa 5 hardening (2026-05-05 sessao 3): callback opcional invocado
 * a cada `capyc_audit_record` apos a entry estar gravada no ring.
 * O caller (kernel) tipicamente liga isso a um sink de debugcon
 * (`outb 0xE9`) para tornar o audit log visivel sem precisar
 * scrappar o struct. Recebe (category, code, seq) ja preenchidos.
 *
 * Contrato:
 *  - Pode ser NULL (default = no-op). NULL e seguro.
 *  - Chamado SEMPRE no contexto da chamada `capyc_audit_record`,
 *    apos write completo. Nao chama de volta no audit log.
 *  - Deve ser leve (sem alocacao); o caller esta normalmente em
 *    fast path do dispatcher.
 */
typedef void (*capyc_audit_sink_fn)(uint8_t category,
                                    uint16_t code,
                                    uint32_t seq);

struct chrome_audit_state {
    /* Ring circular. `head` aponta para o slot do PROXIMO write; ao
     * escrever, head = (head + 1) & (RING_SIZE - 1). */
    struct chrome_audit_entry entries[CHROME_AUDIT_RING_SIZE];
    uint32_t head;
    /* Quantos foram apendados desde init, monotonico. Quando
     * total > RING_SIZE, ring envolve. */
    uint32_t total;
    /* Etapa 5 hardening (2026-05-05 sessao 3): sink opcional. NULL
     * por default depois de capyc_audit_init. */
    capyc_audit_sink_fn sink;
};

/* Zera o ring. */
void capyc_audit_init(struct chrome_audit_state *st);

/* Adiciona uma entrada. Overwrite o mais antigo se ring cheio.
 * `category` deve ser um valor de chrome_audit_category. */
void capyc_audit_record(struct chrome_audit_state *st,
                        uint8_t category,
                        uint16_t code);

/* Etapa 5 hardening (2026-05-05 sessao 3): instala/remove sink
 * de notificacao por record. Passar NULL desabilita. Sobrescreve
 * sink anterior sem aviso. Idempotente. */
void capyc_audit_set_sink(struct chrome_audit_state *st,
                          capyc_audit_sink_fn fn);

/* Retorna o numero de entradas adicionadas desde init (pode
 * exceder RING_SIZE). */
uint32_t capyc_audit_count(const struct chrome_audit_state *st);

/* Numero de entradas ainda visiveis no ring (min(total, RING_SIZE)). */
uint32_t capyc_audit_visible(const struct chrome_audit_state *st);

/* Retorna a k-esima entrada (ordenada por insercao: 0 = mais antiga
 * disponivel). NULL se k >= visible. Ponteiro estavel ate o proximo
 * record() que o sobrescreva. */
const struct chrome_audit_entry *capyc_audit_at(
    const struct chrome_audit_state *st, uint32_t k);

#endif /* APPS_BROWSER_CHROME_AUDIT_H */
