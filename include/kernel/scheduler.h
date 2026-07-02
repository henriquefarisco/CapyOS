#ifndef KERNEL_SCHEDULER_H
#define KERNEL_SCHEDULER_H

#include <stdint.h>
#include "kernel/task.h"

#define SCHEDULER_TICK_HZ 100
#define SCHEDULER_TIME_SLICE_MS 10
/* M4 phase 8a: default per-task quantum measured in scheduler ticks.
 * At SCHEDULER_TICK_HZ=100Hz this is 100ms of CPU time before the
 * preemptive scheduler considers another task. Tasks initialise their
 * quantum_remaining to this value in task_create so the first tick
 * after dispatch does not immediately context-switch away. */
#define SCHED_DEFAULT_QUANTUM 10

enum scheduler_policy {
  SCHED_POLICY_ROUND_ROBIN = 0,
  SCHED_POLICY_PRIORITY,
  SCHED_POLICY_COOPERATIVE
};

struct scheduler_stats {
  uint64_t total_switches;
  uint64_t total_ticks;
  uint64_t idle_ticks;
  uint32_t runnable_count;
  uint32_t blocked_count;
  uint32_t sleeping_count;
};

void scheduler_init(enum scheduler_policy policy);
void scheduler_start(void) __attribute__((noreturn));
void scheduler_tick(void);
void scheduler_add(struct task *t);
void scheduler_remove(struct task *t);
void scheduler_yield(void);
void scheduler_block_current(void *channel);
void scheduler_unblock(void *channel);
void scheduler_sleep_current(uint64_t ticks);
void scheduler_set_policy(enum scheduler_policy policy);
/* M4 phase 8a: marks the scheduler as running (sched_running=1) without
 * entering scheduler_start()'s noreturn idle loop. Required so kernel_main
 * can keep its boot flow while also accepting cooperative scheduler_yield
 * calls from tasks that were already added to the run queue. */
void scheduler_set_running(int running);
/* Etapa 7 / Slice 7.5 (alpha.309): preempt guard. While held (counted),
 * scheduler_tick defers IRQ-context zombie reaping and quantum-exhaustion
 * preemption; sleeper wake-ups and tick accounting still run. Voluntary
 * switches (task_yield/task_sleep) are unaffected. Used by the desktop
 * runtime to make each frame an atomic scheduling unit so ring-3 gfx
 * processes only mutate compositor state between frames. */
void scheduler_preempt_disable(void);
void scheduler_preempt_enable(void);
int scheduler_preempt_disabled(void);
void scheduler_stats_get(struct scheduler_stats *out);
struct task *scheduler_pick_next(void);
int scheduler_running(void);
int scheduler_can_sleep_current(void);

#endif /* KERNEL_SCHEDULER_H */
