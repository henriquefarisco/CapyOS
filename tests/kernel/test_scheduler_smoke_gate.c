#include "kernel/scheduler_smoke.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

static void fail(const char *msg) {
  printf("[scheduler-smoke] FAIL: %s\n", msg);
  g_failures++;
}

static void test_state_reset_zeros_fields(void) {
  struct scheduler_fairness_smoke_state state;
  memset(&state, 0xAA, sizeof(state));
  scheduler_fairness_smoke_state_reset(&state);
  if (state.first_pid != 0u || state.second_pid != 0u ||
      state.third_pid != 0u ||
      state.first_dispatches != 0u || state.second_dispatches != 0u ||
      state.third_dispatches != 0u || state.distinct_tasks != 0u ||
      state.switches_observed != 0u || state.emitted != 0u) {
    fail("reset must zero all fields");
  }
}

static void test_state_reset_null_safe(void) {
  scheduler_fairness_smoke_state_reset(NULL);
}

static void test_gate_predicate(void) {
  if (scheduler_fairness_smoke_gate_observed(0u, 0u, 0u, 0u)) {
    fail("empty observation must not satisfy gate");
  }
  if (scheduler_fairness_smoke_gate_observed(
          SCHEDULER_FAIRNESS_SMOKE_REQUIRED_TASKS - 1u,
          SCHEDULER_FAIRNESS_SMOKE_REQUIRED_DISPATCHES_PER_TASK,
          SCHEDULER_FAIRNESS_SMOKE_REQUIRED_DISPATCHES_PER_TASK,
          SCHEDULER_FAIRNESS_SMOKE_REQUIRED_DISPATCHES_PER_TASK)) {
    fail("too few distinct tasks must not satisfy gate");
  }
  if (scheduler_fairness_smoke_gate_observed(
          SCHEDULER_FAIRNESS_SMOKE_REQUIRED_TASKS,
          SCHEDULER_FAIRNESS_SMOKE_REQUIRED_DISPATCHES_PER_TASK - 1u,
          SCHEDULER_FAIRNESS_SMOKE_REQUIRED_DISPATCHES_PER_TASK,
          SCHEDULER_FAIRNESS_SMOKE_REQUIRED_DISPATCHES_PER_TASK)) {
    fail("too few dispatches for one task must not satisfy gate");
  }
  if (!scheduler_fairness_smoke_gate_observed(
          SCHEDULER_FAIRNESS_SMOKE_REQUIRED_TASKS,
          SCHEDULER_FAIRNESS_SMOKE_REQUIRED_DISPATCHES_PER_TASK,
          SCHEDULER_FAIRNESS_SMOKE_REQUIRED_DISPATCHES_PER_TASK,
          SCHEDULER_FAIRNESS_SMOKE_REQUIRED_DISPATCHES_PER_TASK)) {
    fail("required tasks and dispatch counts must satisfy gate");
  }
}

static void test_observe_tracks_distinct_tasks(void) {
  struct scheduler_fairness_smoke_state state;
  scheduler_fairness_smoke_state_reset(&state);
  (void)scheduler_fairness_smoke_observe(&state, 1u, 2u);
  (void)scheduler_fairness_smoke_observe(&state, 2u, 3u);
  if (state.distinct_tasks != 3u ||
      state.first_pid != 1u ||
      state.second_pid != 2u ||
      state.third_pid != 3u ||
      state.second_dispatches != 1u ||
      state.third_dispatches != 1u) {
    fail("observe must track the first three distinct pids");
  }
}

static void test_observe_rejects_invalid_switches(void) {
  struct scheduler_fairness_smoke_state state;
  scheduler_fairness_smoke_state_reset(&state);
  if (scheduler_fairness_smoke_observe(&state, 0u, 2u) != 0 ||
      scheduler_fairness_smoke_observe(&state, 2u, 0u) != 0 ||
      scheduler_fairness_smoke_observe(&state, 2u, 2u) != 0) {
    fail("invalid switches must not emit");
  }
  if (state.switches_observed != 0u || state.distinct_tasks != 0u) {
    fail("invalid switches must not mutate counters");
  }
}

static void test_first_complete_rounds_emit_once(void) {
  struct scheduler_fairness_smoke_state state;
  scheduler_fairness_smoke_state_reset(&state);
  if (scheduler_fairness_smoke_observe(&state, 1u, 2u) != 0 ||
      scheduler_fairness_smoke_observe(&state, 2u, 3u) != 0 ||
      scheduler_fairness_smoke_observe(&state, 3u, 1u) != 0 ||
      scheduler_fairness_smoke_observe(&state, 1u, 2u) != 0 ||
      scheduler_fairness_smoke_observe(&state, 2u, 3u) != 0 ||
      scheduler_fairness_smoke_observe(&state, 3u, 1u) != 1) {
    fail("six switches across three tasks must emit exactly on completion");
  }
  if (state.emitted != 1u) fail("latch must be set after emission");
  if (scheduler_fairness_smoke_observe(&state, 1u, 2u) != 0) {
    fail("latched state must not re-emit");
  }
}

static void test_marker_constant_is_canonical(void) {
  if (strcmp(SCHEDULER_FAIRNESS_SMOKE_MARKER,
             "[smoke] scheduler-fairness ready") != 0) {
    fail("marker string drifted");
  }
}

static void test_global_latch_single_emission(void) {
  scheduler_fairness_smoke_global_reset();
  (void)scheduler_fairness_smoke_try_latch_global(1u, 2u);
  (void)scheduler_fairness_smoke_try_latch_global(2u, 3u);
  (void)scheduler_fairness_smoke_try_latch_global(3u, 1u);
  (void)scheduler_fairness_smoke_try_latch_global(1u, 2u);
  (void)scheduler_fairness_smoke_try_latch_global(2u, 3u);
  if (scheduler_fairness_smoke_try_latch_global(3u, 1u) != 1) {
    fail("global latch must emit on first complete observation");
  }
  if (scheduler_fairness_smoke_try_latch_global(1u, 2u) != 0) {
    fail("global latch must not re-emit");
  }
}

static void test_global_reset_isolates_tests(void) {
  scheduler_fairness_smoke_global_reset();
  (void)scheduler_fairness_smoke_try_latch_global(1u, 2u);
  (void)scheduler_fairness_smoke_try_latch_global(2u, 3u);
  (void)scheduler_fairness_smoke_try_latch_global(3u, 1u);
  (void)scheduler_fairness_smoke_try_latch_global(1u, 2u);
  (void)scheduler_fairness_smoke_try_latch_global(2u, 3u);
  (void)scheduler_fairness_smoke_try_latch_global(3u, 1u);
  scheduler_fairness_smoke_global_reset();
  if (scheduler_fairness_smoke_try_latch_global(1u, 2u) != 0) {
    fail("global reset must clear emitted latch and counters");
  }
}

int run_scheduler_smoke_gate_tests(void) {
  g_failures = 0;
  test_state_reset_zeros_fields();
  test_state_reset_null_safe();
  test_gate_predicate();
  test_observe_tracks_distinct_tasks();
  test_observe_rejects_invalid_switches();
  test_first_complete_rounds_emit_once();
  test_marker_constant_is_canonical();
  test_global_latch_single_emission();
  test_global_reset_isolates_tests();
  scheduler_fairness_smoke_global_reset();
  if (g_failures == 0) printf("[tests] scheduler_smoke_gate OK\n");
  return g_failures;
}
