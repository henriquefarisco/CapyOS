#pragma GCC optimize("O0")
#include "kernel/scheduler.h"
#include "kernel/scheduler_smoke.h"
#include "kernel/task.h"
#include "kernel/arch_sched_hooks.h"
#include <stddef.h>

#ifdef CAPYOS_THREAD_CRASH_SURVIVES_SMOKE
#include "kernel/thread_crash_smoke.h"
#endif

static struct task *run_queue_head = NULL;
static struct task *run_queue_tail = NULL;
static struct task *idle_task = NULL;
static enum scheduler_policy current_policy = SCHED_POLICY_COOPERATIVE;
static struct scheduler_stats stats;
static int sched_running = 0;
#ifdef CAPYOS_SCHEDULER_FAIRNESS_SMOKE
static int fairness_smoke_helpers_started = 0;
#endif

/* SCHED_DEFAULT_QUANTUM is defined in include/kernel/scheduler.h so that
 * task_create() can reuse the same constant when initialising
 * quantum_remaining (M4 phase 8a). */

extern void context_switch(struct task_context *old, struct task_context *new_ctx);
extern void task_set_current(struct task *t);

void scheduler_init(enum scheduler_policy policy) {
  run_queue_head = NULL;
  run_queue_tail = NULL;
  idle_task = NULL;
  current_policy = policy;
  stats.total_switches = 0;
  stats.total_ticks = 0;
  stats.idle_ticks = 0;
  stats.runnable_count = 0;
  stats.blocked_count = 0;
  stats.sleeping_count = 0;
  sched_running = 0;
#ifdef CAPYOS_SCHEDULER_FAIRNESS_SMOKE
  fairness_smoke_helpers_started = 0;
#endif
}

static void idle_entry(void *arg) {
  (void)arg;
  for (;;) {
#if defined(UNIT_TEST) || !defined(__x86_64__)
    /* Host build cannot emit `hlt`. The idle entry is never called by
     * unit tests; this break stops the otherwise infinite loop should
     * a misuse ever invoke it. */
    break;
#else
    __asm__ volatile("hlt");
#endif
  }
}

void scheduler_add(struct task *t) {
  if (!t) return;
  t->next = NULL;
  t->prev = run_queue_tail;
  if (run_queue_tail) {
    run_queue_tail->next = t;
  } else {
    run_queue_head = t;
  }
  run_queue_tail = t;
}

void scheduler_remove(struct task *t) {
  struct task *cursor = NULL;
  if (!t) return;
  for (cursor = run_queue_head; cursor; cursor = cursor->next) {
    if (cursor == t) break;
  }
  if (!cursor) {
    t->next = NULL;
    t->prev = NULL;
    return;
  }
  if (t->prev) t->prev->next = t->next;
  else run_queue_head = t->next;
  if (t->next) t->next->prev = t->prev;
  else run_queue_tail = t->prev;
  t->next = NULL;
  t->prev = NULL;
}

struct task *scheduler_pick_next(void) {
  struct task *best = NULL;

  if (current_policy == SCHED_POLICY_PRIORITY) {
    int best_pri = -1;
    for (struct task *t = run_queue_head; t; t = t->next) {
      if (t->state == TASK_STATE_READY && (int)t->priority > best_pri) {
        best = t;
        best_pri = (int)t->priority;
      }
    }
  } else {
    for (struct task *t = run_queue_head; t; t = t->next) {
      if (t->state == TASK_STATE_READY) {
        best = t;
        break;
      }
    }
  }

  return best ? best : idle_task;
}

static struct task *scheduler_pick_next_after(struct task *current) {
  struct task *t = NULL;
  if (!current) return scheduler_pick_next();
  for (t = current->next; t; t = t->next) {
    if (t->state == TASK_STATE_READY) return t;
  }
  for (t = run_queue_head; t && t != current; t = t->next) {
    if (t->state == TASK_STATE_READY) return t;
  }
  return idle_task;
}

static struct task *scheduler_pick_priority_after(struct task *current) {
  struct task *best = NULL;
  struct task *t = NULL;
  if (!current) return scheduler_pick_next();
  for (t = current->next; t; t = t->next) {
    if (t->state != TASK_STATE_READY) continue;
    if (!best || t->priority > best->priority) best = t;
  }
  for (t = run_queue_head; t && t != current; t = t->next) {
    if (t->state != TASK_STATE_READY) continue;
    if (!best || t->priority > best->priority) best = t;
  }
  return best ? best : idle_task;
}

static struct task *scheduler_pick_next_for_current(struct task *current) {
  if (current_policy == SCHED_POLICY_PRIORITY) {
    return scheduler_pick_priority_after(current);
  }
  return scheduler_pick_next_after(current);
}

static void schedule(void) {
  struct task *current = task_current();
  struct task *next = scheduler_pick_next_for_current(current);

  if (!next || next == current) return;

  if (current && current->state == TASK_STATE_RUNNING) {
    current->state = TASK_STATE_READY;
  }

  next->state = TASK_STATE_RUNNING;
  task_set_current(next);
  stats.total_switches++;
  if (current &&
      scheduler_fairness_smoke_try_latch_global(current->pid, next->pid)) {
    scheduler_fairness_smoke_emit_marker();
  }

  if (next == idle_task) {
    stats.idle_ticks++;
  }

  /* M4 phase 8f.2: arch-side preparation for `next`. On x86_64 this
   * updates IA32_GS_BASE-backed cpu_local.kernel_rsp AND the TSS
   * RSP0 to point at the new task's per-task kernel stack so any
   * subsequent syscall or IRQ from ring 3 lands on the right
   * stack. The host stub in tests/stub_arch_sched_hooks.c records
   * the call so test_context_switch can lock the contract. */
  arch_sched_apply_kernel_stack(next);

  if (current) {
    context_switch(&current->context, &next->context);
  }
}

void scheduler_yield(void) {
  if (!sched_running) return;
  schedule();
}

#ifdef CAPYOS_SCHEDULER_FAIRNESS_SMOKE
static void fairness_smoke_helper_entry(void *arg) {
  (void)arg;
  for (;;) scheduler_yield();
}

static void fairness_smoke_maybe_start_helpers(void) {
  struct task *a = NULL;
  struct task *b = NULL;
  if (fairness_smoke_helpers_started || !task_current()) return;
  a = task_create_kernel("sched-fair-1", fairness_smoke_helper_entry, NULL);
  b = task_create_kernel("sched-fair-2", fairness_smoke_helper_entry, NULL);
  if (!a || !b) {
    if (a) task_kill(a->pid);
    if (b) task_kill(b->pid);
    return;
  }
  scheduler_add(a);
  scheduler_add(b);
  fairness_smoke_helpers_started = 1;
}
#endif

void scheduler_block_current(void *channel) {
  struct task *current = task_current();
  if (!current) return;
  current->state = TASK_STATE_BLOCKED;
  current->wait_channel = channel;
  stats.blocked_count++;
  schedule();
}

void scheduler_unblock(void *channel) {
  for (struct task *t = run_queue_head; t; t = t->next) {
    if (t->state == TASK_STATE_BLOCKED && t->wait_channel == channel) {
      t->state = TASK_STATE_READY;
      t->wait_channel = NULL;
      if (stats.blocked_count > 0) stats.blocked_count--;
    }
  }
}

void scheduler_sleep_current(uint64_t ticks) {
  struct task *current = task_current();
  if (!current) return;
  current->state = TASK_STATE_SLEEPING;
  current->wake_tick = stats.total_ticks + ticks;
  stats.sleeping_count++;
  schedule();
}

void scheduler_tick(void) {
  stats.total_ticks++;

#ifdef CAPYOS_THREAD_CRASH_SURVIVES_SMOKE
  /* Etapa 4 Fase E: every scheduler tick after the first observed
   * fault-killed process exit counts as evidence that the kernel
   * survived the crash. The latch only fires on the edge that flips
   * the gate from "not ready" to "ready"; subsequent ticks are
   * no-ops. The emit hook lives behind the same flag as the
   * process_exit hook to keep production builds at zero cost. */
  if (thread_crash_smoke_try_latch_tick_global()) {
    thread_crash_smoke_emit_marker();
  }
#endif

  for (struct task *t = run_queue_head; t; t = t->next) {
    if (t->state == TASK_STATE_SLEEPING && t->wake_tick <= stats.total_ticks) {
      t->state = TASK_STATE_READY;
      t->wake_tick = 0;
      if (stats.sleeping_count > 0) stats.sleeping_count--;
    }
  }

  {
    struct task *t = run_queue_head;
    while (t) {
      struct task *next = t->next;
      if ((t->state == TASK_STATE_ZOMBIE || t->state == TASK_STATE_DEAD) &&
          t != task_current()) {
        (void)task_kill(t->pid);
      }
      t = next;
    }
  }

  uint32_t runnable = 0;
  for (struct task *t = run_queue_head; t; t = t->next) {
    if (t->state == TASK_STATE_READY || t->state == TASK_STATE_RUNNING)
      runnable++;
  }
  stats.runnable_count = runnable;

  if (current_policy != SCHED_POLICY_COOPERATIVE) {
    struct task *current = task_current();
    if (current && current->state == TASK_STATE_RUNNING && current != idle_task) {
      if (current->quantum_remaining > 0)
        current->quantum_remaining--;
      if (current->quantum_remaining == 0) {
        current->quantum_remaining = SCHED_DEFAULT_QUANTUM;
        schedule();
        return;
      }
      /* Quantum still has budget: do NOT context-switch on this tick.
       * The previous unconditional schedule() here forced a switch on
       * every tick which made preemption indistinguishable from
       * gang-scheduling and starved the running task. */
      return;
    }
    /* No current task (or current is idle): pick someone to run. */
    schedule();
  }
}

void scheduler_set_running(int running) {
  sched_running = running ? 1 : 0;
#ifdef CAPYOS_SCHEDULER_FAIRNESS_SMOKE
  if (sched_running) fairness_smoke_maybe_start_helpers();
#endif
#ifdef CAPYOS_THREAD_CRASH_SURVIVES_SMOKE
  if (sched_running) thread_crash_smoke_maybe_start_helper();
#endif
}

void scheduler_set_policy(enum scheduler_policy policy) {
  current_policy = policy;
}

void scheduler_stats_get(struct scheduler_stats *out) {
  if (out) *out = stats;
}

int scheduler_running(void) {
  return sched_running;
}

int scheduler_can_sleep_current(void) {
  struct task *current = task_current();
  if (!sched_running || !current) return 0;
  if (stats.total_ticks == 0) return 0;
  if (current->state != TASK_STATE_RUNNING) return 0;
  for (struct task *t = run_queue_head; t; t = t->next) {
    if (t != current && t->state == TASK_STATE_READY) return 1;
  }
  if (idle_task && current_policy != SCHED_POLICY_COOPERATIVE) return 1;
  return 0;
}

void scheduler_start(void) {
  idle_task = task_create_kernel("idle", idle_entry, NULL);
  if (idle_task) {
    idle_task->priority = TASK_PRIORITY_IDLE;
    scheduler_add(idle_task);
  }

  sched_running = 1;

  struct task *first = scheduler_pick_next();
  if (first) {
    first->state = TASK_STATE_RUNNING;
    task_set_current(first);
  }

  for (;;) {
#if defined(UNIT_TEST) || !defined(__x86_64__)
    /* Host build cannot emit `hlt`. scheduler_start is declared noreturn
     * and is never called by unit tests; a plain infinite loop preserves
     * the noreturn contract without requiring an x86 instruction. */
    ;
#else
    __asm__ volatile("hlt");
#endif
  }
}
