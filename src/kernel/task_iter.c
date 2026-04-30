/*
 * Implementation of the public task iterator and stats snapshot.
 *
 * The kernel task table is private to src/kernel/task.c (file-static
 * `task_table[TASK_MAX_COUNT]`). Rather than expose the array directly
 * we walk it through `task_at_index`, which is already part of the
 * public task API. That way the underlying storage can change later
 * without breaking observers.
 */
#include "kernel/task_iter.h"

#include "kernel/task.h"

#include <stddef.h>
#include <stdint.h>

static void task_iter_copy_name(char *dst, const char *src) {
    size_t i = 0;
    if (!dst) return;
    if (src) {
        while (i + 1u < TASK_NAME_MAX && src[i]) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = '\0';
}

static void task_iter_fill_stats(struct task_stats *stats,
                                 const struct task *t) {
    if (!stats) return;
    if (!t) {
        stats->pid = 0;
        stats->ppid = 0;
        stats->state = TASK_STATE_UNUSED;
        stats->priority = TASK_PRIORITY_IDLE;
        stats->uid = 0;
        stats->gid = 0;
        stats->cpu_time_ns = 0;
        stats->wake_tick = 0;
        stats->exit_code = 0;
        stats->name[0] = '\0';
        return;
    }
    stats->pid = t->pid;
    stats->ppid = t->ppid;
    stats->state = t->state;
    stats->priority = t->priority;
    stats->uid = t->uid;
    stats->gid = t->gid;
    stats->cpu_time_ns = t->cpu_time_ns;
    stats->wake_tick = t->wake_tick;
    stats->exit_code = t->exit_code;
    task_iter_copy_name(stats->name, t->name);
}

static int task_iter_advance(struct task_iter *it, struct task_stats *stats) {
    if (!it || !stats) return 0;
    while (it->next_index < TASK_MAX_COUNT) {
        struct task *t = task_at_index(it->next_index);
        it->next_index++;
        if (!t) {
            /* Defensive: out-of-range guard inside task_at_index returns
             * NULL when index >= TASK_MAX_COUNT. We already checked the
             * bound but keep the NULL check so future refactors that
             * sparsify the table do not silently miss slots. */
            continue;
        }
        if (t->state == TASK_STATE_UNUSED) {
            continue;
        }
        task_iter_fill_stats(stats, t);
        return 1;
    }
    return 0;
}

int task_iter_first(struct task_iter *it, struct task_stats *stats) {
    if (!it) return 0;
    it->next_index = 0;
    return task_iter_advance(it, stats);
}

int task_iter_next(struct task_iter *it, struct task_stats *stats) {
    return task_iter_advance(it, stats);
}

int task_stats_get(uint32_t pid, struct task_stats *stats) {
    struct task *t;
    if (!stats || pid == 0) return -1;
    t = task_by_pid(pid);
    if (!t || t->state == TASK_STATE_UNUSED) return -1;
    task_iter_fill_stats(stats, t);
    return 0;
}

const char *task_state_label(enum task_state state) {
    switch (state) {
    case TASK_STATE_UNUSED:   return "unused";
    case TASK_STATE_CREATED:  return "created";
    case TASK_STATE_READY:    return "ready";
    case TASK_STATE_RUNNING:  return "running";
    case TASK_STATE_BLOCKED:  return "blocked";
    case TASK_STATE_SLEEPING: return "sleeping";
    case TASK_STATE_ZOMBIE:   return "zombie";
    case TASK_STATE_DEAD:     return "dead";
    default:                  return "?";
    }
}

const char *task_priority_label(enum task_priority priority) {
    switch (priority) {
    case TASK_PRIORITY_IDLE:     return "idle";
    case TASK_PRIORITY_LOW:      return "low";
    case TASK_PRIORITY_NORMAL:   return "normal";
    case TASK_PRIORITY_HIGH:     return "high";
    case TASK_PRIORITY_REALTIME: return "rt";
    default:                     return "?";
    }
}
