#ifndef KERNEL_TASK_ITER_H
#define KERNEL_TASK_ITER_H

/*
 * Public iteration and stats API for the kernel task table.
 *
 * The underlying table (`task_table` in src/kernel/task.c) holds a fixed
 * number of slots, including UNUSED ones. This header provides a small
 * iterator that hides the slot bookkeeping from callers (shell perf
 * commands, the GUI task manager, tests) and a snapshot stats struct that
 * decouples observers from the internal `struct task` layout.
 *
 * Usage:
 *
 *   struct task_iter it;
 *   struct task_stats stats;
 *   for (int ok = task_iter_first(&it, &stats); ok; ok = task_iter_next(&it, &stats)) {
 *       // stats.pid, stats.name, ... are populated for each active task.
 *   }
 *
 * The iteration order is the same as the underlying table index (which is
 * stable for the lifetime of a slot). Tasks created during iteration may
 * or may not be observed; tasks freed during iteration are skipped.
 *
 * The stats values are sampled at the moment of the call and never alias
 * the live task. Callers can read stats safely without holding any lock
 * (single-CPU runtime today; M4.1 will revisit when preemption is real).
 */

#include "kernel/task.h"

#include <stddef.h>
#include <stdint.h>

struct task_stats {
    uint32_t pid;
    uint32_t ppid;
    enum task_state state;
    enum task_priority priority;
    uint32_t uid;
    uint32_t gid;
    uint64_t cpu_time_ns;
    uint64_t wake_tick;
    int exit_code;
    char name[TASK_NAME_MAX];
};

struct task_iter {
    /* Implementation detail: index into the underlying table. */
    size_t next_index;
};

/* Populate `stats` for the first active task in iteration order.
 * Returns 1 if a task was found, 0 if the table is empty. */
int task_iter_first(struct task_iter *it, struct task_stats *stats);

/* Populate `stats` for the next active task. Returns 1 on success, 0
 * when no more tasks remain. Calling next() before first() is an
 * error and yields 0. */
int task_iter_next(struct task_iter *it, struct task_stats *stats);

/* Snapshot stats for a specific PID. Returns 0 on success and -1 if
 * the PID is unknown or the task is in TASK_STATE_UNUSED. */
int task_stats_get(uint32_t pid, struct task_stats *stats);

/* Stable, lower-case, short label for a task state. NULL-safe and
 * defensive against out-of-range values. Returns a pointer to a static
 * string, never NULL. */
const char *task_state_label(enum task_state state);

/* Stable, lower-case label for a task priority. */
const char *task_priority_label(enum task_priority priority);

#endif /* KERNEL_TASK_ITER_H */
