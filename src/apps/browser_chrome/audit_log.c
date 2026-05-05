/* src/apps/browser_chrome/audit_log.c
 *
 * Etapa 5 hardening (2026-05-03): implementacao do ring buffer de
 * auditoria do chrome. Pura: sem syscalls, sem libc.
 *
 * Contrato em include/apps/browser_chrome_audit.h.
 */
#include "apps/browser_chrome_audit.h"

void capyc_audit_init(struct chrome_audit_state *st) {
    if (!st) return;
    st->head = 0u;
    st->total = 0u;
    /* Etapa 5 hardening (2026-05-05 sessao 3): sink default = NULL. */
    st->sink = (capyc_audit_sink_fn)0;
    for (uint32_t i = 0; i < CHROME_AUDIT_RING_SIZE; ++i) {
        st->entries[i].category = (uint8_t)CAPYC_AUDIT_UNKNOWN;
        st->entries[i].reserved = 0u;
        st->entries[i].code = 0u;
        st->entries[i].seq = 0u;
    }
}

void capyc_audit_record(struct chrome_audit_state *st,
                        uint8_t category,
                        uint16_t code) {
    if (!st) return;
    /* Slot do proximo write; o ring substitui o mais antigo
     * automaticamente quando enche (pela natureza do indice & MASK). */
    uint32_t mask = CHROME_AUDIT_RING_SIZE - 1u;
    uint32_t slot = st->head & mask;
    st->entries[slot].category = category;
    st->entries[slot].reserved = 0u;
    st->entries[slot].code     = code;
    /* Sequencia comeca em 1 para deixar 0 reservado para "nenhum". */
    st->total++;
    st->entries[slot].seq = st->total;
    st->head = (st->head + 1u) & mask;
    /* Etapa 5 hardening (2026-05-05 sessao 3): notifica sink se
     * instalado. Chamada apos commit no ring para que um sink que
     * por sua vez chame capyc_audit_at(st, ...) veja a entry nova. */
    if (st->sink) {
        st->sink(category, code, st->total);
    }
}

void capyc_audit_set_sink(struct chrome_audit_state *st,
                          capyc_audit_sink_fn fn) {
    if (!st) return;
    st->sink = fn;
}

uint32_t capyc_audit_count(const struct chrome_audit_state *st) {
    if (!st) return 0u;
    return st->total;
}

uint32_t capyc_audit_visible(const struct chrome_audit_state *st) {
    if (!st) return 0u;
    if (st->total >= CHROME_AUDIT_RING_SIZE) return CHROME_AUDIT_RING_SIZE;
    return st->total;
}

const struct chrome_audit_entry *capyc_audit_at(
    const struct chrome_audit_state *st, uint32_t k) {
    if (!st) return (const struct chrome_audit_entry *)0;
    uint32_t vis = capyc_audit_visible(st);
    if (k >= vis) return (const struct chrome_audit_entry *)0;
    /* Quando ring nao envolveu (total <= RING_SIZE), entries[0..total)
     * estao em ordem de insercao. Quando envolveu (total > RING_SIZE),
     * o slot mais antigo e o que esta em (head) wraparound (porque
     * head aponta para o proximo write, que sera o que sobrescreveu o
     * mais antigo na proxima rodada... mas ja foi escrito por um mais
     * recente). Vamos calcular o slot da k-esima entry mais antiga. */
    uint32_t mask = CHROME_AUDIT_RING_SIZE - 1u;
    uint32_t oldest_slot;
    if (st->total <= CHROME_AUDIT_RING_SIZE) {
        oldest_slot = 0u;
    } else {
        /* Ring envolveu. O slot mais antigo eh head (proximo a ser
         * sobrescrito). */
        oldest_slot = st->head & mask;
    }
    uint32_t idx = (oldest_slot + k) & mask;
    return &st->entries[idx];
}
