#include "kernel/thread_crash_smoke.h"

#include <stddef.h>

#ifdef CAPYOS_THREAD_CRASH_SURVIVES_SMOKE
#include "kernel/task.h"
#include "kernel/scheduler.h"
#endif

/* Pure latch implementation; mirrors src/kernel/scheduler_smoke.c
 * structure so the smoke-marker-pattern stays consistent across
 * Fases C/D/E of Etapa 4. No allocation, no I/O, no kernel deps. */

void thread_crash_smoke_state_reset(struct thread_crash_smoke_state *state) {
  if (!state) return;
  state->last_exit_code = 0;
  state->crashes_observed = 0u;
  state->ticks_after_crash = 0u;
  state->emitted = 0u;
}

int thread_crash_smoke_gate_observed(uint32_t crashes_observed,
                                     uint32_t ticks_after_crash) {
  return crashes_observed >= 1u &&
         ticks_after_crash >=
             THREAD_CRASH_SMOKE_REQUIRED_TICKS_AFTER_CRASH;
}

int thread_crash_smoke_observe_exit(struct thread_crash_smoke_state *state,
                                    int32_t exit_code) {
  if (!state) return 0;
  if (exit_code < THREAD_CRASH_SMOKE_FAULT_EXIT_THRESHOLD) {
    /* Clean exit; the gate is not interested in voluntary
     * termination because that cannot prove the kernel survived an
     * asynchronous fault. */
    return 0;
  }
  state->last_exit_code = exit_code;
  if (state->crashes_observed < 0xFFFFFFFFu) {
    state->crashes_observed++;
  }
  if (state->emitted) return 0;
  if (!thread_crash_smoke_gate_observed(state->crashes_observed,
                                        state->ticks_after_crash)) {
    return 0;
  }
  state->emitted = 1u;
  return 1;
}

int thread_crash_smoke_observe_tick(struct thread_crash_smoke_state *state) {
  if (!state) return 0;
  /* Ticks before any crash do not count: the gate must prove the
   * kernel kept dispatching AFTER the fault, not just that the
   * scheduler tick fires under steady state. */
  if (state->crashes_observed == 0u) return 0;
  if (state->ticks_after_crash < 0xFFFFFFFFu) {
    state->ticks_after_crash++;
  }
  if (state->emitted) return 0;
  if (!thread_crash_smoke_gate_observed(state->crashes_observed,
                                        state->ticks_after_crash)) {
    return 0;
  }
  state->emitted = 1u;
  return 1;
}

/* Global latch for the live build. Same pattern as
 * scheduler_fairness_smoke_try_latch_global / compositor_damage_
 * smoke_try_latch_global. */
static struct thread_crash_smoke_state g_global_state;

void thread_crash_smoke_global_reset(void) {
  thread_crash_smoke_state_reset(&g_global_state);
}

int thread_crash_smoke_try_latch_exit_global(int32_t exit_code) {
  return thread_crash_smoke_observe_exit(&g_global_state, exit_code);
}

int thread_crash_smoke_try_latch_tick_global(void) {
  return thread_crash_smoke_observe_tick(&g_global_state);
}

#ifdef CAPYOS_THREAD_CRASH_SURVIVES_SMOKE
/* Single-shot helper task that simulates the observable side of a
 * fault-killed process exit. The first dispatch feeds the exit
 * latch with `128 + 14` (page-fault vector), matching the
 * `process_exit(128 + (int)vector)` call site in the x86_64 fault
 * dispatcher; subsequent scheduler ticks then drive the tick latch
 * until the gate fires.
 *
 * The helper yields forever after firing so it never owns the CPU
 * and never contends with the fairness-smoke helpers when both
 * flags are co-enabled. The boolean guard ensures the simulated
 * exit is fed exactly once per boot, matching the smoke-marker
 * pattern of "emit once". */
static int g_helper_started = 0;

static void thread_crash_smoke_helper_entry(void *arg) {
  int signalled = 0;
  (void)arg;
  for (;;) {
    if (!signalled) {
      /* 14 is the x86_64 page-fault vector; the live fault path
       * encodes the death code as 128 + vector and we mirror that
       * here so the latch sees the same wire value it would observe
       * in a real fault-killed exit. */
      (void)thread_crash_smoke_try_latch_exit_global(128 + 14);
      signalled = 1;
    }
    task_yield();
  }
}

void thread_crash_smoke_maybe_start_helper(void) {
  struct task *t;
  if (g_helper_started || !task_current()) return;
  t = task_create_kernel("thread-crash-smoke",
                         thread_crash_smoke_helper_entry, NULL);
  if (!t) return;
  scheduler_add(t);
  g_helper_started = 1;
}
#else
void thread_crash_smoke_maybe_start_helper(void) {}
#endif
