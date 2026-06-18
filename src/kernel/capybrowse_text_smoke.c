#include "kernel/capybrowse_text_smoke.h"

#include <stddef.h>

/* Pure latch implementation; mirrors src/kernel/tls_handshake_smoke.c so the
 * smoke-marker-pattern stays consistent across the Etapa 4/5/6 gates. No
 * allocation, no I/O, no kernel deps — fully host-testable. */

void capybrowse_text_smoke_state_reset(
    struct capybrowse_text_smoke_state *state) {
  if (!state) return;
  state->successes_observed = 0u;
  state->emitted = 0u;
}

int capybrowse_text_smoke_gate_observed(uint32_t successes_observed) {
  return successes_observed >= 1u;
}

int capybrowse_text_smoke_observe_exit(
    struct capybrowse_text_smoke_state *state, int32_t exit_code) {
  if (!state) return 0;
  if (exit_code != CAPYBROWSE_TEXT_SMOKE_SUCCESS_EXIT_CODE) {
    /* capybrowse exits non-zero on any failure (transport, parse or render).
     * Such exits must not fire the readiness marker. */
    return 0;
  }
  if (state->successes_observed < 0xFFFFFFFFu) {
    state->successes_observed++;
  }
  if (state->emitted) return 0;
  if (!capybrowse_text_smoke_gate_observed(state->successes_observed)) {
    return 0;
  }
  state->emitted = 1u;
  return 1;
}

/* Global latch for the live build. Same pattern as
 * tls_handshake_smoke_try_latch_exit_global. */
static struct capybrowse_text_smoke_state g_global_state;

void capybrowse_text_smoke_global_reset(void) {
  capybrowse_text_smoke_state_reset(&g_global_state);
}

int capybrowse_text_smoke_try_latch_exit_global(int32_t exit_code) {
  return capybrowse_text_smoke_observe_exit(&g_global_state, exit_code);
}
