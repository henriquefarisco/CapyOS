#pragma GCC optimize("O0")
#include "kernel/scheduler.h"
#include "kernel/task.h"
#include <stddef.h>

static struct task *run_queue_head = NULL;
static struct task *run_queue_tail = NULL;
static struct task *idle_task = NULL;
static enum scheduler_policy current_policy = SCHED_POLICY_COOPERATIVE;
static struct scheduler_stats stats;
static int sched_running = 0;

#define SCHED_DEFAULT_QUANTUM 10

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
}

static void idle_entry(void *arg) {
  (void)arg;
  for (;;) {
    __asm__ volatile("hlt");
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
  if (!t) return;
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

static void schedule(void) {
  struct task *current = task_current();
  struct task *next = scheduler_pick_next();

  if (!next || next == current) return;

  if (current && current->state == TASK_STATE_RUNNING) {
    current->state = TASK_STATE_READY;
  }

  next->state = TASK_STATE_RUNNING;
  task_set_current(next);
  stats.total_switches++;

  if (next == idle_task) {
    stats.idle_ticks++;
  }

  if (current) {
    context_switch(&current->context, &next->context);
  }
}

void scheduler_yield(void) {
  if (!sched_running) return;
  schedule();
}

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

  for (struct task *t = run_queue_head; t; t = t->next) {
    if (t->state == TASK_STATE_SLEEPING && t->wake_tick <= stats.total_ticks) {
      t->state = TASK_STATE_READY;
      t->wake_tick = 0;
      if (stats.sleeping_count > 0) stats.sleeping_count--;
    }
  }

  for (struct task *t = run_queue_head; t; t = t->next) {
    if (t->state == TASK_STATE_ZOMBIE || t->state == TASK_STATE_DEAD) {
      struct task *next = t->next;
      scheduler_remove(t);
      t->state = TASK_STATE_UNUSED;
      t = next;
      if (!t) break;
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
    }
    schedule();
  }
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
    __asm__ volatile("hlt");
  }
}
