#include "kernel/capygfx_smoke.h"

#include <stddef.h>

/* Etapa 7 / Slice 7.2.2 — pure latch implementation; mirrors
 * src/kernel/capybrowse_text_smoke.c so the smoke-marker pattern stays
 * consistent across the Etapa 4/5/6/7 gates. No allocation, no I/O, no kernel
 * deps — fully host-testable. */

void capygfx_smoke_state_reset(struct capygfx_smoke_state *state) {
  if (!state) return;
  state->successes_observed = 0u;
  state->emitted = 0u;
}

int capygfx_smoke_gate_observed(uint32_t successes_observed) {
  return successes_observed >= 1u;
}

int capygfx_smoke_observe_exit(struct capygfx_smoke_state *state,
                               int32_t exit_code) {
  if (!state) return 0;
  if (exit_code != CAPYGFX_SMOKE_SUCCESS_EXIT_CODE) {
    /* capygfx exits non-zero on any graphical-syscall failure. Such exits must
     * not fire the readiness marker. */
    return 0;
  }
  if (state->successes_observed < 0xFFFFFFFFu) {
    state->successes_observed++;
  }
  if (state->emitted) return 0;
  if (!capygfx_smoke_gate_observed(state->successes_observed)) {
    return 0;
  }
  state->emitted = 1u;
  return 1;
}

/* Global latch for the live build. Same pattern as
 * capybrowse_text_smoke_try_latch_exit_global. */
static struct capygfx_smoke_state g_global_state;

void capygfx_smoke_global_reset(void) {
  capygfx_smoke_state_reset(&g_global_state);
}

int capygfx_smoke_try_latch_exit_global(int32_t exit_code) {
  return capygfx_smoke_observe_exit(&g_global_state, exit_code);
}
