#include "kernel/capybrowse_text_smoke.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

static void fail(const char *msg) {
  printf("[capybrowse-text-smoke] FAIL: %s\n", msg);
  g_failures++;
}

static void test_state_reset_zeros_fields(void) {
  struct capybrowse_text_smoke_state state;
  memset(&state, 0xAA, sizeof(state));
  capybrowse_text_smoke_state_reset(&state);
  if (state.successes_observed != 0u || state.emitted != 0u) {
    fail("reset must zero all fields");
  }
}

static void test_state_reset_null_safe(void) {
  capybrowse_text_smoke_state_reset(NULL);
}

static void test_gate_predicate(void) {
  if (capybrowse_text_smoke_gate_observed(0u)) {
    fail("zero successes must not satisfy gate");
  }
  if (!capybrowse_text_smoke_gate_observed(1u)) {
    fail("one success must satisfy gate");
  }
  if (!capybrowse_text_smoke_gate_observed(5u)) {
    fail("many successes must satisfy gate");
  }
}

static void test_nonzero_exit_ignored(void) {
  struct capybrowse_text_smoke_state state;
  capybrowse_text_smoke_state_reset(&state);
  /* Any non-zero exit (transport/parse/render failure, or a fault-killed
   * >= 128 exit) must not flip the gate. */
  if (capybrowse_text_smoke_observe_exit(&state, 1) != 0 ||
      capybrowse_text_smoke_observe_exit(&state, 142) != 0 ||
      capybrowse_text_smoke_observe_exit(&state, -1) != 0) {
    fail("non-zero exits must not flip the gate");
  }
  if (state.successes_observed != 0u || state.emitted != 0u) {
    fail("non-zero exits must not mutate counters");
  }
}

static void test_success_exit_latches_once(void) {
  struct capybrowse_text_smoke_state state;
  capybrowse_text_smoke_state_reset(&state);
  if (capybrowse_text_smoke_observe_exit(
          &state, CAPYBROWSE_TEXT_SMOKE_SUCCESS_EXIT_CODE) != 1) {
    fail("first success exit must flip the gate");
  }
  if (state.emitted != 1u || state.successes_observed != 1u) {
    fail("success exit must latch and count");
  }
  /* Subsequent success exits must NOT re-emit (idempotency). */
  if (capybrowse_text_smoke_observe_exit(
          &state, CAPYBROWSE_TEXT_SMOKE_SUCCESS_EXIT_CODE) != 0) {
    fail("second success exit must not re-emit");
  }
}

static void test_observe_null_state(void) {
  if (capybrowse_text_smoke_observe_exit(NULL, 0) != 0) {
    fail("NULL state must return 0");
  }
}

static void test_marker_constant_is_canonical(void) {
  if (strcmp(CAPYBROWSE_TEXT_SMOKE_MARKER, "[smoke] capybrowse-text ready") !=
      0) {
    fail("marker constant drifted from canonical string");
  }
}

static void test_global_latch_single_emission(void) {
  /* Regression for smoke-marker-pattern Invariant #5: the global latch fires
   * exactly once across the boot even if several processes exit cleanly. */
  capybrowse_text_smoke_global_reset();
  if (capybrowse_text_smoke_try_latch_exit_global(0) != 1) {
    fail("global latch must fire on first success exit");
  }
  if (capybrowse_text_smoke_try_latch_exit_global(0) != 0) {
    fail("global latch must not re-emit on a second success exit");
  }
  /* Reset isolation: after a reset the latch can fire again (next boot). */
  capybrowse_text_smoke_global_reset();
  if (capybrowse_text_smoke_try_latch_exit_global(0) != 1) {
    fail("global latch must fire again after reset");
  }
  capybrowse_text_smoke_global_reset();
}

int run_capybrowse_text_smoke_gate_tests(void) {
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
    printf("[capybrowse-text-smoke] all gate tests passed\n");
  }
  return g_failures;
}
