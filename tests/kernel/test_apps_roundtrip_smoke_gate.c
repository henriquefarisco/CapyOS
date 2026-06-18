#include "kernel/apps_roundtrip_smoke.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

static void fail(const char *msg) {
  printf("[apps-roundtrip-smoke] FAIL: %s\n", msg);
  g_failures++;
}

static void test_state_reset_zeros_fields(void) {
  struct apps_roundtrip_smoke_state state;
  memset(&state, 0xAA, sizeof(state));
  apps_roundtrip_smoke_state_reset(&state);
  if (state.apps_completed != 0u || state.emitted != 0u) {
    fail("reset must zero all fields");
  }
}

static void test_state_reset_null_safe(void) {
  apps_roundtrip_smoke_state_reset(NULL);
}

static void test_gate_predicate(void) {
  /* Below the threshold is not ready; at/above is ready. REQUIRED is >= 1. */
  if (apps_roundtrip_smoke_gate_observed(APPS_ROUNDTRIP_SMOKE_REQUIRED_APPS -
                                         1u)) {
    fail("below threshold must not satisfy gate");
  }
  if (!apps_roundtrip_smoke_gate_observed(APPS_ROUNDTRIP_SMOKE_REQUIRED_APPS)) {
    fail("threshold must satisfy gate");
  }
  if (!apps_roundtrip_smoke_gate_observed(APPS_ROUNDTRIP_SMOKE_REQUIRED_APPS +
                                          5u)) {
    fail("above threshold must satisfy gate");
  }
}

static void test_nonclean_exit_ignored(void) {
  struct apps_roundtrip_smoke_state state;
  apps_roundtrip_smoke_state_reset(&state);
  /* A crashed app (non-zero exit, incl. fault-killed >= 128) must neither
   * count toward the roundtrip nor fire the marker. */
  if (apps_roundtrip_smoke_observe_exit(&state, 1) != 0 ||
      apps_roundtrip_smoke_observe_exit(&state, 142) != 0 ||
      apps_roundtrip_smoke_observe_exit(&state, -1) != 0) {
    fail("non-clean exits must not flip the gate");
  }
  if (state.apps_completed != 0u || state.emitted != 0u) {
    fail("non-clean exits must not mutate counters");
  }
}

static void test_accumulate_and_latch_once(void) {
  struct apps_roundtrip_smoke_state state;
  uint32_t i;
  apps_roundtrip_smoke_state_reset(&state);
  /* The first REQUIRED-1 clean exits accumulate without firing. */
  for (i = 0u; i + 1u < APPS_ROUNDTRIP_SMOKE_REQUIRED_APPS; i++) {
    if (apps_roundtrip_smoke_observe_exit(
            &state, APPS_ROUNDTRIP_SMOKE_CLEAN_EXIT_CODE) != 0) {
      fail("must not fire before the threshold");
    }
  }
  /* The threshold-th clean exit flips the gate exactly once. */
  if (apps_roundtrip_smoke_observe_exit(
          &state, APPS_ROUNDTRIP_SMOKE_CLEAN_EXIT_CODE) != 1) {
    fail("threshold-th clean exit must flip the gate");
  }
  if (state.emitted != 1u ||
      state.apps_completed != APPS_ROUNDTRIP_SMOKE_REQUIRED_APPS) {
    fail("threshold exit must latch and count");
  }
  /* Subsequent clean exits must NOT re-emit (idempotency). */
  if (apps_roundtrip_smoke_observe_exit(
          &state, APPS_ROUNDTRIP_SMOKE_CLEAN_EXIT_CODE) != 0) {
    fail("post-threshold clean exit must not re-emit");
  }
}

static void test_observe_null_state(void) {
  if (apps_roundtrip_smoke_observe_exit(NULL, 0) != 0) {
    fail("NULL state must return 0");
  }
}

static void test_clean_exits_accumulate(void) {
  /* Clean exits always increment the counter (even past the threshold / after
   * emission), so the count-to-N logic is exercised independently of the
   * compile-time REQUIRED_APPS (which defaults to 1). */
  struct apps_roundtrip_smoke_state state;
  uint32_t after_one;
  apps_roundtrip_smoke_state_reset(&state);
  (void)apps_roundtrip_smoke_observe_exit(&state, 0);
  after_one = state.apps_completed;
  (void)apps_roundtrip_smoke_observe_exit(&state, 0);
  if (after_one != 1u || state.apps_completed != 2u) {
    fail("clean exits must accumulate the completed counter");
  }
}

static void test_marker_constant_is_canonical(void) {
  if (strcmp(APPS_ROUNDTRIP_SMOKE_MARKER, "[smoke] apps-basic-roundtrip ready") !=
      0) {
    fail("marker constant drifted from canonical string");
  }
}

static void test_global_latch_single_emission(void) {
  /* Regression for smoke-marker-pattern Invariant #5: the global latch fires
   * exactly once across the boot. Feed REQUIRED clean exits, expect a single
   * edge; then verify reset isolation (can fire again next boot). */
  uint32_t i;
  int edges = 0;
  apps_roundtrip_smoke_global_reset();
  for (i = 0u; i < APPS_ROUNDTRIP_SMOKE_REQUIRED_APPS; i++) {
    edges += apps_roundtrip_smoke_try_latch_exit_global(0);
  }
  if (edges != 1) {
    fail("global latch must fire exactly once reaching the threshold");
  }
  if (apps_roundtrip_smoke_try_latch_exit_global(0) != 0) {
    fail("global latch must not re-emit after firing");
  }
  apps_roundtrip_smoke_global_reset();
  edges = 0;
  for (i = 0u; i < APPS_ROUNDTRIP_SMOKE_REQUIRED_APPS; i++) {
    edges += apps_roundtrip_smoke_try_latch_exit_global(0);
  }
  if (edges != 1) {
    fail("global latch must fire again after reset");
  }
  apps_roundtrip_smoke_global_reset();
}

int run_apps_roundtrip_smoke_gate_tests(void) {
  g_failures = 0;
  test_state_reset_zeros_fields();
  test_state_reset_null_safe();
  test_gate_predicate();
  test_nonclean_exit_ignored();
  test_accumulate_and_latch_once();
  test_observe_null_state();
  test_clean_exits_accumulate();
  test_marker_constant_is_canonical();
  test_global_latch_single_emission();
  if (g_failures == 0) {
    printf("[apps-roundtrip-smoke] all gate tests passed\n");
  }
  return g_failures;
}
