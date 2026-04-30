#ifndef KERNEL_PROCESS_ITER_H
#define KERNEL_PROCESS_ITER_H

/*
 * Public iteration and stats API for the kernel process table.
 *
 * Mirrors include/kernel/task_iter.h but for `struct process`. The
 * underlying `proc_table` array is file-static in src/kernel/process.c;
 * we walk it through `process_at_index`, defined in this module's
 * implementation as a thin extension of the existing process API.
 *
 * Usage:
 *
 *   struct process_iter it;
 *   struct process_stats stats;
 *   for (int ok = process_iter_first(&it, &stats); ok; ok = process_iter_next(&it, &stats)) {
 *       // stats.pid, stats.name, ... are populated for each active process.
 *   }
 *
 * The returned `rss_pages` is left at zero in this phase. M4 phase 7
 * fills it via `vmm_address_space_rss(as)`.
 */

#include "kernel/process.h"

#include <stddef.h>
#include <stdint.h>

struct process_stats {
    uint32_t pid;
    uint32_t ppid;
    enum process_state state;
    uint32_t uid;
    uint32_t gid;
    uint64_t brk;
    uint64_t heap_start;
    uint64_t stack_top;
    /* Resident set in 4 KiB pages. Always 0 until M4 phase 7 wires
     * `vmm_address_space_rss`. Observers must accept zero as "unknown
     * yet" rather than "empty address space". */
    uint64_t rss_pages;
    int exit_code;
    /* PID of the main thread, or 0 if the process has no thread bound. */
    uint32_t main_thread_pid;
    char name[PROCESS_NAME_MAX];
};

struct process_iter {
    size_t next_index;
};

/* First/next iteration over active processes. Same contract as the
 * task iterator. process_at_index is declared in include/kernel/process.h
 * and used internally to walk the table; observers should prefer the
 * iter API below so the slot bookkeeping stays inside the kernel. */
int process_iter_first(struct process_iter *it, struct process_stats *stats);
int process_iter_next(struct process_iter *it, struct process_stats *stats);

/* Snapshot stats for a specific PID. Returns 0 on success, -1 if the
 * PID is unknown or the process is in PROC_STATE_UNUSED. */
int process_stats_get(uint32_t pid, struct process_stats *stats);

/* Stable, lower-case, short label for a process state. */
const char *process_state_label(enum process_state state);

#endif /* KERNEL_PROCESS_ITER_H */
