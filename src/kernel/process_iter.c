/*
 * Implementation of the public process iterator and stats snapshot.
 *
 * The kernel process table is private to src/kernel/process.c
 * (`proc_table[PROCESS_MAX]`). We walk it through `process_at_index`,
 * exported from process.c specifically for this kind of observer.
 *
 * Phase 7b wired `rss_pages` to `vmm_address_space_rss(as)`. A NULL
 * address space (e.g. a slot caught mid-creation, or process_destroy
 * that already cleared it) still publishes 0 so observers can keep
 * treating 0 as "unknown yet / empty" without a special case.
 */
#include "kernel/process_iter.h"

#include "kernel/process.h"
#include "memory/vmm.h"

#include <stddef.h>
#include <stdint.h>

static void process_iter_copy_name(char *dst, const char *src) {
    size_t i = 0;
    if (!dst) return;
    if (src) {
        while (i + 1u < PROCESS_NAME_MAX && src[i]) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = '\0';
}

static void process_iter_fill_stats(struct process_stats *stats,
                                    const struct process *p) {
    if (!stats) return;
    if (!p) {
        stats->pid = 0;
        stats->ppid = 0;
        stats->state = PROC_STATE_UNUSED;
        stats->uid = 0;
        stats->gid = 0;
        stats->brk = 0;
        stats->heap_start = 0;
        stats->stack_top = 0;
        stats->rss_pages = 0;
        stats->exit_code = 0;
        stats->main_thread_pid = 0;
        stats->name[0] = '\0';
        return;
    }
    stats->pid = p->pid;
    stats->ppid = p->ppid;
    stats->state = p->state;
    stats->uid = p->uid;
    stats->gid = p->gid;
    stats->brk = p->brk;
    stats->heap_start = p->heap_start;
    stats->stack_top = p->stack_top;
    /* Phase 7b RSS wiring: vmm_address_space_rss returns 0 for NULL
     * which matches the previous "unknown yet" sentinel. The counter
     * is bumped by vmm_map_page on user mappings (eager + demand) and
     * decremented by vmm_unmap_page, so this number reflects the
     * actual page-table state at snapshot time. */
    stats->rss_pages = vmm_address_space_rss(p->address_space);
    stats->exit_code = p->exit_code;
    stats->main_thread_pid = p->main_thread ? p->main_thread->pid : 0;
    process_iter_copy_name(stats->name, p->name);
}

static int process_iter_advance(struct process_iter *it,
                                struct process_stats *stats) {
    if (!it || !stats) return 0;
    while (1) {
        struct process *p = process_at_index(it->next_index);
        if (!p) return 0;
        it->next_index++;
        if (p->state == PROC_STATE_UNUSED) {
            continue;
        }
        process_iter_fill_stats(stats, p);
        return 1;
    }
}

int process_iter_first(struct process_iter *it, struct process_stats *stats) {
    if (!it) return 0;
    it->next_index = 0;
    return process_iter_advance(it, stats);
}

int process_iter_next(struct process_iter *it, struct process_stats *stats) {
    return process_iter_advance(it, stats);
}

int process_stats_get(uint32_t pid, struct process_stats *stats) {
    struct process *p;
    if (!stats || pid == 0) return -1;
    p = process_by_pid(pid);
    if (!p || p->state == PROC_STATE_UNUSED) return -1;
    process_iter_fill_stats(stats, p);
    return 0;
}

const char *process_state_label(enum process_state state) {
    switch (state) {
    case PROC_STATE_UNUSED:   return "unused";
    case PROC_STATE_EMBRYO:   return "embryo";
    case PROC_STATE_RUNNING:  return "running";
    case PROC_STATE_SLEEPING: return "sleeping";
    case PROC_STATE_ZOMBIE:   return "zombie";
    default:                  return "?";
    }
}
