/* Etapa 3 — Slice 3E.4 external validation gate.
 * Pure gate logic for STORAGE_SMOKE_MARKER emission.
 * No I/O, no allocation, no globals — host-testable. */

#include "drivers/storage/storage_smoke.h"

#include <stddef.h>

void storage_smoke_state_reset(struct storage_smoke_state *state) {
    if (!state) return;
    state->ahci_ok_count = 0u;
    state->nvme_ok_count = 0u;
    state->emitted = 0u;
}

int storage_smoke_gate_observed(uint32_t ahci_ok_count,
                                uint32_t nvme_ok_count) {
    return (ahci_ok_count >= 1u || nvme_ok_count >= 1u) ? 1 : 0;
}

int storage_smoke_observe(struct storage_smoke_state *state,
                          enum storage_smoke_source source) {
    if (!state) return 0;
    switch (source) {
        case STORAGE_SMOKE_SRC_AHCI:
            state->ahci_ok_count++;
            break;
        case STORAGE_SMOKE_SRC_NVME:
            state->nvme_ok_count++;
            break;
        default:
            return 0;
    }
    if (state->emitted) return 0;
    if (!storage_smoke_gate_observed(state->ahci_ok_count,
                                     state->nvme_ok_count)) {
        return 0;
    }
    state->emitted = 1u;
    return 1;
}

/* alpha.252 audit fix — BUG #1.
 *
 * Process-wide single-emission state shared between AHCI and NVMe.
 * Each driver used to carry its own file-scope `storage_smoke_state`
 * (alpha.250 design), which meant a dual-storage VM (AHCI + NVMe
 * both present) emitted the marker twice — once when AHCI's first
 * OK flipped its latch, again when NVMe's first OK flipped its
 * own. The harness uses `markers_in_order` which tolerates
 * repetition, so the gate still passes; but the contract
 * documented in storage_smoke.h says "exactly once per boot".
 *
 * Moving the latch here makes it a single source of truth: the
 * first OK from any driver wins; everything else returns 0. */
static struct storage_smoke_state g_global_state;

void storage_smoke_global_reset(void) {
    storage_smoke_state_reset(&g_global_state);
}

int storage_smoke_try_latch_global(enum storage_smoke_source source) {
    return storage_smoke_observe(&g_global_state, source);
}
