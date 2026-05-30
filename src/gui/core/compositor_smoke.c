#include "gui/compositor_smoke.h"

#include <stddef.h>

void compositor_damage_smoke_state_reset(
    struct compositor_damage_smoke_state *state) {
  if (!state) return;
  state->partial_frames_observed = 0u;
  state->dirty_rects_observed = 0u;
  state->emitted = 0u;
}

int compositor_damage_smoke_gate_observed(uint64_t partial_frames_observed,
                                          uint64_t dirty_rects_observed) {
  return partial_frames_observed >=
             COMPOSITOR_DAMAGE_SMOKE_REQUIRED_PARTIAL_FRAMES &&
         dirty_rects_observed >= 1u;
}

int compositor_damage_smoke_observe(
    struct compositor_damage_smoke_state *state,
    uint32_t dirty_rect_count,
    int partial_frame) {
  if (!state || !partial_frame || dirty_rect_count == 0u) return 0;
  state->partial_frames_observed++;
  state->dirty_rects_observed += dirty_rect_count;
  if (state->emitted) return 0;
  if (!compositor_damage_smoke_gate_observed(
          state->partial_frames_observed, state->dirty_rects_observed)) {
    return 0;
  }
  state->emitted = 1u;
  return 1;
}

static struct compositor_damage_smoke_state g_global_state;

void compositor_damage_smoke_global_reset(void) {
  compositor_damage_smoke_state_reset(&g_global_state);
}

int compositor_damage_smoke_try_latch_global(uint32_t dirty_rect_count,
                                             int partial_frame) {
  return compositor_damage_smoke_observe(&g_global_state, dirty_rect_count,
                                         partial_frame);
}
