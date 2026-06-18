#include "kernel/apps_roundtrip_smoke.h"

#include <stddef.h>

/* Pure latch implementation; mirrors src/kernel/capybrowse_text_smoke.c (and
 * the thread_crash/tls latches) so the smoke-marker-pattern stays consistent.
 * No allocation, no I/O, no kernel deps — fully host-testable. */

void apps_roundtrip_smoke_state_reset(struct apps_roundtrip_smoke_state *state) {
  if (!state) return;
  state->apps_completed = 0u;
  state->emitted = 0u;
}

int apps_roundtrip_smoke_gate_observed(uint32_t apps_completed) {
  return apps_completed >= APPS_ROUNDTRIP_SMOKE_REQUIRED_APPS;
}

int apps_roundtrip_smoke_observe_exit(struct apps_roundtrip_smoke_state *state,
                                      int32_t exit_code) {
  if (!state) return 0;
  if (exit_code != APPS_ROUNDTRIP_SMOKE_CLEAN_EXIT_CODE) {
    /* A crashed/aborted app (non-zero, e.g. a fault-killed >= 128 exit) breaks
     * the clean roundtrip: it is not counted and must not fire the marker. */
    return 0;
  }
  if (state->apps_completed < 0xFFFFFFFFu) {
    state->apps_completed++;
  }
  if (state->emitted) return 0;
  if (!apps_roundtrip_smoke_gate_observed(state->apps_completed)) {
    return 0;
  }
  state->emitted = 1u;
  return 1;
}

/* Global latch for the live build. Same pattern as
 * capybrowse_text_smoke_try_latch_exit_global. */
static struct apps_roundtrip_smoke_state g_global_state;

void apps_roundtrip_smoke_global_reset(void) {
  apps_roundtrip_smoke_state_reset(&g_global_state);
}

int apps_roundtrip_smoke_try_latch_exit_global(int32_t exit_code) {
  return apps_roundtrip_smoke_observe_exit(&g_global_state, exit_code);
}
