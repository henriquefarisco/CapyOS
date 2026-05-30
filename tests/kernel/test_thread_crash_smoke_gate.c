#include "kernel/thread_crash_smoke.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

static void fail(const char *msg) {
  printf("[thread-crash-smoke] FAIL: %s\n", msg);
  g_failures++;
}

static void test_state_reset_zeros_fields(void) {
  struct thread_crash_smoke_state state;
  memset(&state, 0xAA, sizeof(state));
  thread_crash_smoke_state_reset(&state);
  if (state.last_exit_code != 0 || state.crashes_observed != 0u ||
      state.ticks_after_crash != 0u || state.emitted != 0u) {
    fail("reset must zero all fields");
  }
}

static void test_state_reset_null_safe(void) {
  thread_crash_smoke_state_reset(NULL);
}

static void test_gate_predicate(void) {
  if (thread_crash_smoke_gate_observed(0u, 0u)) {
    fail("empty observation must not satisfy gate");
  }
  if (thread_crash_smoke_gate_observed(1u, 0u)) {
    fail("crash without ticks must not satisfy gate");
  }
  if (thread_crash_smoke_gate_observed(
          0u, THREAD_CRASH_SMOKE_REQUIRED_TICKS_AFTER_CRASH)) {
    fail("ticks without crash must not satisfy gate");
  }
  if (thread_crash_smoke_gate_observed(
          1u, THREAD_CRASH_SMOKE_REQUIRED_TICKS_AFTER_CRASH - 1u)) {
    fail("not enough ticks after crash must not satisfy gate");
  }
  if (!thread_crash_smoke_gate_observed(
          1u, THREAD_CRASH_SMOKE_REQUIRED_TICKS_AFTER_CRASH)) {
    fail("crash plus required ticks must satisfy gate");
  }
}

static void test_clean_exit_ignored(void) {
  struct thread_crash_smoke_state state;
  thread_crash_smoke_state_reset(&state);
  if (thread_crash_smoke_observe_exit(&state, 0) != 0 ||
      thread_crash_smoke_observe_exit(&state, 1) != 0 ||
      thread_crash_smoke_observe_exit(
          &state, THREAD_CRASH_SMOKE_FAULT_EXIT_THRESHOLD - 1) != 0) {
    fail("clean exits must not flip the gate");
  }
  if (state.crashes_observed != 0u || state.last_exit_code != 0) {
    fail("clean exits must not mutate counters");
  }
}

static void test_crash_observed_records_state(void) {
  struct thread_crash_smoke_state state;
  thread_crash_smoke_state_reset(&state);
  if (thread_crash_smoke_observe_exit(&state, 128 + 14) != 0) {
    fail("first crash must not flip gate without ticks");
  }
  if (state.crashes_observed != 1u ||
      state.last_exit_code != 128 + 14 ||
      state.emitted != 0u) {
    fail("crash observation must record exit_code and counter");
  }
}

static void test_ticks_before_crash_ignored(void) {
  struct thread_crash_smoke_state state;
  thread_crash_smoke_state_reset(&state);
  for (uint32_t i = 0; i < 10u; i++) {
    if (thread_crash_smoke_observe_tick(&state) != 0) {
      fail("tick before crash must not flip gate");
      return;
    }
  }
  if (state.ticks_after_crash != 0u) {
    fail("ticks before crash must not increment counter");
  }
}

static void test_crash_then_ticks_emits_once(void) {
  struct thread_crash_smoke_state state;
  uint32_t needed = THREAD_CRASH_SMOKE_REQUIRED_TICKS_AFTER_CRASH;
  thread_crash_smoke_state_reset(&state);
  (void)thread_crash_smoke_observe_exit(&state, 128 + 14);
  for (uint32_t i = 0; i + 1u < needed; i++) {
    if (thread_crash_smoke_observe_tick(&state) != 0) {
      fail("intermediate tick must not flip gate");
      return;
    }
  }
  if (thread_crash_smoke_observe_tick(&state) != 1) {
    fail("Nth tick after crash must flip gate exactly once");
  }
  if (state.emitted != 1u) fail("latch must be set after emission");
  if (thread_crash_smoke_observe_tick(&state) != 0) {
    fail("latched state must not re-emit on subsequent ticks");
  }
  if (thread_crash_smoke_observe_exit(&state, 128 + 13) != 0) {
    fail("latched state must not re-emit on further crashes");
  }
}

static void test_marker_constant_is_canonical(void) {
  if (strcmp(THREAD_CRASH_SMOKE_MARKER,
             "[smoke] thread-crash-survives ready") != 0) {
    fail("marker string drifted");
  }
  if (THREAD_CRASH_SMOKE_FAULT_EXIT_THRESHOLD != 128) {
    fail("fault exit threshold drifted");
  }
}

static void drive_global_latch_to_emit(void) {
  (void)thread_crash_smoke_try_latch_exit_global(128 + 14);
  uint32_t needed = THREAD_CRASH_SMOKE_REQUIRED_TICKS_AFTER_CRASH;
  for (uint32_t i = 0; i + 1u < needed; i++) {
    (void)thread_crash_smoke_try_latch_tick_global();
  }
}

static void test_global_latch_single_emission(void) {
  thread_crash_smoke_global_reset();
  drive_global_latch_to_emit();
  if (thread_crash_smoke_try_latch_tick_global() != 1) {
    fail("global latch must emit on first complete observation");
  }
  if (thread_crash_smoke_try_latch_tick_global() != 0) {
    fail("global latch must not re-emit");
  }
  if (thread_crash_smoke_try_latch_exit_global(128 + 14) != 0) {
    fail("global latch must not re-emit on further crashes");
  }
}

static void test_global_reset_isolates_tests(void) {
  thread_crash_smoke_global_reset();
  drive_global_latch_to_emit();
  (void)thread_crash_smoke_try_latch_tick_global();
  thread_crash_smoke_global_reset();
  if (thread_crash_smoke_try_latch_tick_global() != 0) {
    fail("global reset must clear emitted latch and counters");
  }
}

int run_thread_crash_smoke_gate_tests(void) {
  g_failures = 0;
  test_state_reset_zeros_fields();
  test_state_reset_null_safe();
  test_gate_predicate();
  test_clean_exit_ignored();
  test_crash_observed_records_state();
  test_ticks_before_crash_ignored();
  test_crash_then_ticks_emits_once();
  test_marker_constant_is_canonical();
  test_global_latch_single_emission();
  test_global_reset_isolates_tests();
  thread_crash_smoke_global_reset();
  if (g_failures == 0) printf("[tests] thread_crash_smoke_gate OK\n");
  return g_failures;
}
