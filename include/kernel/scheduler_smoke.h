#ifndef KERNEL_SCHEDULER_SMOKE_H
#define KERNEL_SCHEDULER_SMOKE_H

#include <stdint.h>

#define SCHEDULER_FAIRNESS_SMOKE_GATE_VERSION 1
#define SCHEDULER_FAIRNESS_SMOKE_MARKER "[smoke] scheduler-fairness ready"
#define SCHEDULER_FAIRNESS_SMOKE_REQUIRED_TASKS 3u
#define SCHEDULER_FAIRNESS_SMOKE_REQUIRED_DISPATCHES_PER_TASK 2u

struct scheduler_fairness_smoke_state {
  uint32_t first_pid;
  uint32_t second_pid;
  uint32_t third_pid;
  uint32_t first_dispatches;
  uint32_t second_dispatches;
  uint32_t third_dispatches;
  uint32_t distinct_tasks;
  uint32_t switches_observed;
  uint8_t emitted;
};

void scheduler_fairness_smoke_state_reset(
    struct scheduler_fairness_smoke_state *state);
int scheduler_fairness_smoke_gate_observed(uint32_t distinct_tasks,
                                           uint32_t first_dispatches,
                                           uint32_t second_dispatches,
                                           uint32_t third_dispatches);
int scheduler_fairness_smoke_observe(
    struct scheduler_fairness_smoke_state *state,
    uint32_t old_pid,
    uint32_t new_pid);
void scheduler_fairness_smoke_emit_marker(void);
int scheduler_fairness_smoke_try_latch_global(uint32_t old_pid,
                                              uint32_t new_pid);
void scheduler_fairness_smoke_global_reset(void);

#endif /* KERNEL_SCHEDULER_SMOKE_H */
