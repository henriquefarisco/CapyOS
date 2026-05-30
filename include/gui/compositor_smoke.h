#ifndef GUI_COMPOSITOR_SMOKE_H
#define GUI_COMPOSITOR_SMOKE_H

#include <stdint.h>

#define COMPOSITOR_DAMAGE_SMOKE_GATE_VERSION 1
#define COMPOSITOR_DAMAGE_SMOKE_MARKER "[smoke] compositor-damage-track ready"
#define COMPOSITOR_DAMAGE_SMOKE_REQUIRED_PARTIAL_FRAMES 2u

struct compositor_damage_smoke_state {
  uint64_t partial_frames_observed;
  uint64_t dirty_rects_observed;
  uint8_t emitted;
};

void compositor_damage_smoke_state_reset(
    struct compositor_damage_smoke_state *state);
int compositor_damage_smoke_gate_observed(uint64_t partial_frames_observed,
                                          uint64_t dirty_rects_observed);
int compositor_damage_smoke_observe(
    struct compositor_damage_smoke_state *state,
    uint32_t dirty_rect_count,
    int partial_frame);
void compositor_damage_smoke_emit_marker(void);
int compositor_damage_smoke_try_latch_global(uint32_t dirty_rect_count,
                                             int partial_frame);
void compositor_damage_smoke_global_reset(void);

#endif /* GUI_COMPOSITOR_SMOKE_H */
