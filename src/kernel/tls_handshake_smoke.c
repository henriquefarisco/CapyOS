#include "kernel/tls_handshake_smoke.h"

#include <stddef.h>

/* Pure latch implementation; mirrors src/kernel/thread_crash_smoke.c so the
 * smoke-marker-pattern stays consistent across the Etapa 4/5 gates. No
 * allocation, no I/O, no kernel deps — fully host-testable. */

void tls_handshake_smoke_state_reset(struct tls_handshake_smoke_state *state) {
  if (!state) return;
  state->successes_observed = 0u;
  state->emitted = 0u;
}

int tls_handshake_smoke_gate_observed(uint32_t successes_observed) {
  return successes_observed >= 1u;
}

int tls_handshake_smoke_observe_exit(struct tls_handshake_smoke_state *state,
                                     int32_t exit_code) {
  if (!state) return 0;
  if (exit_code != TLS_HANDSHAKE_SMOKE_SUCCESS_EXIT_CODE) {
    /* tls_smoke exits non-zero on any failure (the valid GET never
     * succeeded, or the bad-cert GET was NOT refused). Such exits must not
     * fire the readiness marker. */
    return 0;
  }
  if (state->successes_observed < 0xFFFFFFFFu) {
    state->successes_observed++;
  }
  if (state->emitted) return 0;
  if (!tls_handshake_smoke_gate_observed(state->successes_observed)) {
    return 0;
  }
  state->emitted = 1u;
  return 1;
}

/* Global latch for the live build. Same pattern as
 * thread_crash_smoke_try_latch_exit_global. */
static struct tls_handshake_smoke_state g_global_state;

void tls_handshake_smoke_global_reset(void) {
  tls_handshake_smoke_state_reset(&g_global_state);
}

int tls_handshake_smoke_try_latch_exit_global(int32_t exit_code) {
  return tls_handshake_smoke_observe_exit(&g_global_state, exit_code);
}
