#include "gui/compositor_smoke.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

static void fail(const char *msg) {
  printf("[compositor-smoke] FAIL: %s\n", msg);
  g_failures++;
}

static void test_state_reset_zeros_fields(void) {
  struct compositor_damage_smoke_state state;
  memset(&state, 0xAA, sizeof(state));
  compositor_damage_smoke_state_reset(&state);
  if (state.partial_frames_observed != 0u ||
      state.dirty_rects_observed != 0u ||
      state.emitted != 0u) {
    fail("reset must zero all fields");
  }
}

static void test_state_reset_null_safe(void) {
  compositor_damage_smoke_state_reset(NULL);
}

static void test_gate_predicate(void) {
  if (compositor_damage_smoke_gate_observed(0u, 0u)) {
    fail("empty observation must not satisfy gate");
  }
  if (compositor_damage_smoke_gate_observed(
          COMPOSITOR_DAMAGE_SMOKE_REQUIRED_PARTIAL_FRAMES - 1u, 2u)) {
    fail("too few partial frames must not satisfy gate");
  }
  if (compositor_damage_smoke_gate_observed(
          COMPOSITOR_DAMAGE_SMOKE_REQUIRED_PARTIAL_FRAMES, 0u)) {
    fail("zero dirty rects must not satisfy gate");
  }
  if (!compositor_damage_smoke_gate_observed(
          COMPOSITOR_DAMAGE_SMOKE_REQUIRED_PARTIAL_FRAMES, 1u)) {
    fail("required partial frames and dirty rects must satisfy gate");
  }
}

static void test_observe_rejects_non_partial_frames(void) {
  struct compositor_damage_smoke_state state;
  compositor_damage_smoke_state_reset(&state);
  if (compositor_damage_smoke_observe(&state, 1u, 0) != 0 ||
      compositor_damage_smoke_observe(&state, 0u, 1) != 0) {
    fail("non-partial or zero-dirty frames must not emit");
  }
  if (state.partial_frames_observed != 0u ||
      state.dirty_rects_observed != 0u) {
    fail("rejected frames must not mutate counters");
  }
}

static void test_second_partial_frame_emits_once(void) {
  struct compositor_damage_smoke_state state;
  compositor_damage_smoke_state_reset(&state);
  if (compositor_damage_smoke_observe(&state, 1u, 1) != 0) {
    fail("first partial frame must not emit");
  }
  if (compositor_damage_smoke_observe(&state, 2u, 1) != 1) {
    fail("second partial frame must emit");
  }
  if (state.partial_frames_observed != 2u ||
      state.dirty_rects_observed != 3u ||
      state.emitted != 1u) {
    fail("counters or latch drifted");
  }
  if (compositor_damage_smoke_observe(&state, 1u, 1) != 0) {
    fail("latched state must not re-emit");
  }
}

static void test_null_state_is_rejected(void) {
  if (compositor_damage_smoke_observe(NULL, 1u, 1) != 0) {
    fail("NULL state must return 0");
  }
}

static void test_marker_constant_is_canonical(void) {
  if (strcmp(COMPOSITOR_DAMAGE_SMOKE_MARKER,
             "[smoke] compositor-damage-track ready") != 0) {
    fail("marker string drifted");
  }
}

static void test_global_latch_single_emission(void) {
  compositor_damage_smoke_global_reset();
  if (compositor_damage_smoke_try_latch_global(1u, 1) != 0) {
    fail("first global partial frame must not emit");
  }
  if (compositor_damage_smoke_try_latch_global(1u, 1) != 1) {
    fail("second global partial frame must emit");
  }
  if (compositor_damage_smoke_try_latch_global(1u, 1) != 0) {
    fail("global latch must not re-emit");
  }
}

static void test_global_reset_isolates_tests(void) {
  compositor_damage_smoke_global_reset();
  (void)compositor_damage_smoke_try_latch_global(1u, 1);
  (void)compositor_damage_smoke_try_latch_global(1u, 1);
  compositor_damage_smoke_global_reset();
  if (compositor_damage_smoke_try_latch_global(1u, 1) != 0) {
    fail("global reset must clear emitted latch and counters");
  }
}

int test_compositor_smoke_gate_run(void) {
  g_failures = 0;
  test_state_reset_zeros_fields();
  test_state_reset_null_safe();
  test_gate_predicate();
  test_observe_rejects_non_partial_frames();
  test_second_partial_frame_emits_once();
  test_null_state_is_rejected();
  test_marker_constant_is_canonical();
  test_global_latch_single_emission();
  test_global_reset_isolates_tests();
  compositor_damage_smoke_global_reset();
  if (g_failures == 0) printf("[tests] compositor_smoke_gate OK\n");
  return g_failures;
}
