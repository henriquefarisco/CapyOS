#include "kernel/scheduler_smoke.h"

#include <stddef.h>

static uint32_t scheduler_fairness_smoke_pid_slot(
    const struct scheduler_fairness_smoke_state *state,
    uint32_t pid) {
  if (!state || pid == 0u) return 0u;
  if (state->first_pid == pid) return 1u;
  if (state->second_pid == pid) return 2u;
  if (state->third_pid == pid) return 3u;
  return 0u;
}

static void scheduler_fairness_smoke_note_pid(
    struct scheduler_fairness_smoke_state *state,
    uint32_t pid) {
  if (!state || pid == 0u ||
      scheduler_fairness_smoke_pid_slot(state, pid) != 0u) {
    return;
  }
  if (state->distinct_tasks == 0u) state->first_pid = pid;
  else if (state->distinct_tasks == 1u) state->second_pid = pid;
  else if (state->distinct_tasks == 2u) state->third_pid = pid;
  if (state->distinct_tasks < SCHEDULER_FAIRNESS_SMOKE_REQUIRED_TASKS) {
    state->distinct_tasks++;
  }
}

static void scheduler_fairness_smoke_note_dispatch(
    struct scheduler_fairness_smoke_state *state,
    uint32_t pid) {
  switch (scheduler_fairness_smoke_pid_slot(state, pid)) {
    case 1u:
      state->first_dispatches++;
      break;
    case 2u:
      state->second_dispatches++;
      break;
    case 3u:
      state->third_dispatches++;
      break;
    default:
      break;
  }
}

void scheduler_fairness_smoke_state_reset(
    struct scheduler_fairness_smoke_state *state) {
  if (!state) return;
  state->first_pid = 0u;
  state->second_pid = 0u;
  state->third_pid = 0u;
  state->first_dispatches = 0u;
  state->second_dispatches = 0u;
  state->third_dispatches = 0u;
  state->distinct_tasks = 0u;
  state->switches_observed = 0u;
  state->emitted = 0u;
}

int scheduler_fairness_smoke_gate_observed(uint32_t distinct_tasks,
                                           uint32_t first_dispatches,
                                           uint32_t second_dispatches,
                                           uint32_t third_dispatches) {
  return distinct_tasks >= SCHEDULER_FAIRNESS_SMOKE_REQUIRED_TASKS &&
         first_dispatches >=
             SCHEDULER_FAIRNESS_SMOKE_REQUIRED_DISPATCHES_PER_TASK &&
         second_dispatches >=
             SCHEDULER_FAIRNESS_SMOKE_REQUIRED_DISPATCHES_PER_TASK &&
         third_dispatches >=
             SCHEDULER_FAIRNESS_SMOKE_REQUIRED_DISPATCHES_PER_TASK;
}

int scheduler_fairness_smoke_observe(
    struct scheduler_fairness_smoke_state *state,
    uint32_t old_pid,
    uint32_t new_pid) {
  if (!state || old_pid == 0u || new_pid == 0u ||
      old_pid == new_pid) return 0;
  scheduler_fairness_smoke_note_pid(state, old_pid);
  scheduler_fairness_smoke_note_pid(state, new_pid);
  scheduler_fairness_smoke_note_dispatch(state, new_pid);
  state->switches_observed++;
  if (state->emitted) return 0;
  if (!scheduler_fairness_smoke_gate_observed(
          state->distinct_tasks, state->first_dispatches,
          state->second_dispatches, state->third_dispatches)) {
    return 0;
  }
  state->emitted = 1u;
  return 1;
}

static struct scheduler_fairness_smoke_state g_global_state;

void scheduler_fairness_smoke_global_reset(void) {
  scheduler_fairness_smoke_state_reset(&g_global_state);
}

int scheduler_fairness_smoke_try_latch_global(uint32_t old_pid,
                                              uint32_t new_pid) {
  return scheduler_fairness_smoke_observe(&g_global_state, old_pid, new_pid);
}
