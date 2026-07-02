#include "kernel/capymultifetch_smoke.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

static void fail(const char *msg) {
  printf("[browser-multifetch-smoke] FAIL: %s\n", msg);
  g_failures++;
}

static void test_state_reset_zeros_fields(void) {
  struct capymultifetch_smoke_state state;
  memset(&state, 0xAA, sizeof(state));
  capymultifetch_smoke_state_reset(&state);
  if (state.successes_observed != 0u || state.emitted != 0u) {
    fail("reset must zero all fields");
  }
}

static void test_state_reset_null_safe(void) {
  capymultifetch_smoke_state_reset(NULL);
}

static void test_gate_predicate(void) {
  if (capymultifetch_smoke_gate_observed(0u)) {
    fail("zero successes must not satisfy gate");
  }
  if (!capymultifetch_smoke_gate_observed(1u)) {
    fail("one success must satisfy gate");
  }
  if (!capymultifetch_smoke_gate_observed(5u)) {
    fail("many successes must satisfy gate");
  }
}

static void test_nonzero_exit_ignored(void) {
  struct capymultifetch_smoke_state state;
  capymultifetch_smoke_state_reset(&state);
  /* Any non-zero exit (transport/cache/budget failure, or a fault-killed
   * >= 128 exit) must not flip the gate. */
  if (capymultifetch_smoke_observe_exit(&state, 1) != 0 ||
      capymultifetch_smoke_observe_exit(&state, 142) != 0 ||
      capymultifetch_smoke_observe_exit(&state, -1) != 0) {
    fail("non-zero exits must not flip the gate");
  }
  if (state.successes_observed != 0u || state.emitted != 0u) {
    fail("non-zero exits must not mutate counters");
  }
}

static void test_success_exit_latches_once(void) {
  struct capymultifetch_smoke_state state;
  capymultifetch_smoke_state_reset(&state);
  if (capymultifetch_smoke_observe_exit(
          &state, CAPYMULTIFETCH_SMOKE_SUCCESS_EXIT_CODE) != 1) {
    fail("first success exit must flip the gate");
  }
  if (state.emitted != 1u || state.successes_observed != 1u) {
    fail("success exit must latch and count");
  }
  /* Subsequent success exits must NOT re-emit (idempotency). */
  if (capymultifetch_smoke_observe_exit(
          &state, CAPYMULTIFETCH_SMOKE_SUCCESS_EXIT_CODE) != 0) {
    fail("second success exit must not re-emit");
  }
}

static void test_observe_null_state(void) {
  if (capymultifetch_smoke_observe_exit(NULL, 0) != 0) {
    fail("NULL state must return 0");
  }
}

static void test_marker_constant_is_canonical(void) {
  if (strcmp(CAPYMULTIFETCH_SMOKE_MARKER, "[smoke] browser-multifetch ready") !=
      0) {
    fail("marker constant drifted from canonical string");
  }
}

static void test_global_latch_single_emission(void) {
  /* Regression for smoke-marker-pattern Invariant #5: the global latch fires
   * exactly once across the boot even if several processes exit cleanly. */
  capymultifetch_smoke_global_reset();
  if (capymultifetch_smoke_try_latch_exit_global(0) != 1) {
    fail("global latch must fire on first success exit");
  }
  if (capymultifetch_smoke_try_latch_exit_global(0) != 0) {
    fail("global latch must not re-emit on a second success exit");
  }
  /* Reset isolation: after a reset the latch can fire again (next boot). */
  capymultifetch_smoke_global_reset();
  if (capymultifetch_smoke_try_latch_exit_global(0) != 1) {
    fail("global latch must fire again after reset");
  }
  capymultifetch_smoke_global_reset();
}

int run_capymultifetch_smoke_gate_tests(void) {
  g_failures = 0;
  test_state_reset_zeros_fields();
  test_state_reset_null_safe();
  test_gate_predicate();
  test_nonzero_exit_ignored();
  test_success_exit_latches_once();
  test_observe_null_state();
  test_marker_constant_is_canonical();
  test_global_latch_single_emission();
  if (g_failures == 0) {
    printf("[browser-multifetch-smoke] all gate tests passed\n");
  }
  return g_failures;
}
