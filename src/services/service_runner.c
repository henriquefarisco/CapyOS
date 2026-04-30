/*
 * Implementation of the kernel service runner. See header for design.
 *
 * Storage is intentionally module-private. The accessors and step()
 * function form the only public ABI; everything else is static.
 */
#include "services/service_runner.h"

#include "core/work_queue.h"
#include "kernel/process.h"
#include "kernel/task.h"
#include "services/service_manager.h"

/* The PIT driver is x86_64-only; tests provide a stub via tests/stub_vmm.c.
 * Declared extern here to avoid dragging the arch-specific header. */
extern uint64_t pit_ticks(void);

static struct task *g_runner_task = (struct task *)0;
static uint32_t g_runner_pid = 0u;
static uint64_t g_runner_step_count = 0u;
static uint64_t g_runner_services_polled_total = 0u;
static uint64_t g_runner_last_tick = 0u;
/* Phase 6.6: cumulative count of zombie orphans reaped by the
 * periodic `process_reap_orphans()` sweep. Exposed via
 * service_runner_stats so that tests and observers can verify the
 * tick is wired and the reaper is making progress. */
static uint64_t g_runner_orphans_reaped_total = 0u;

/*
 * Cooperative task body. Each iteration polls services and work queue
 * items, then sleeps for SERVICE_RUNNER_DEFAULT_INTERVAL_TICKS scheduler
 * ticks. Until the scheduler is flipped to preemptive (phase 8), this
 * body is not actively dispatched; the cooperative path in
 * kernel_service_poll() drives service_runner_step() directly. The
 * function is kept as the canonical task entry so that the flip in
 * phase 8 only requires removing the cooperative call.
 */
static void service_runner_entry(void *arg) {
    (void)arg;
    for (;;) {
        (void)service_runner_step(pit_ticks());
        task_sleep((uint64_t)SERVICE_RUNNER_DEFAULT_INTERVAL_TICKS);
    }
}

void service_runner_init(void) {
    if (g_runner_task != (struct task *)0) {
        return;
    }
    g_runner_task = task_create(SERVICE_RUNNER_NAME, service_runner_entry,
                                (void *)0, TASK_PRIORITY_NORMAL);
    if (g_runner_task != (struct task *)0) {
        g_runner_pid = g_runner_task->pid;
    }
}

void service_runner_reset(void) {
    g_runner_task = (struct task *)0;
    g_runner_pid = 0u;
    g_runner_step_count = 0u;
    g_runner_services_polled_total = 0u;
    g_runner_last_tick = 0u;
    g_runner_orphans_reaped_total = 0u;
}

int service_runner_step(uint64_t now_ticks) {
    int polled = service_manager_poll_due(now_ticks);
    if (polled < 0) {
        polled = 0;
    }
    g_runner_step_count++;
    g_runner_services_polled_total += (uint64_t)polled;
    g_runner_last_tick = now_ticks;
    /* work_queue is also driven from the runner so phase 1 keeps a
     * single cadence path; production behaviour is identical to the
     * pre-refactor kernel_service_poll(). */
    (void)work_queue_poll_due(now_ticks);
    /* Phase 6.6: reap zombie orphans (root processes whose parent
     * never existed, or children whose parent was destroyed first).
     * Cheap O(PROCESS_MAX) sweep that returns 0 in the steady state.
     * The reaper is intentionally driven from the cooperative tick
     * rather than from process_kill/process_exit themselves: those
     * sites either run on the dying process's stack (process_exit)
     * or are called with a parent live (the common case for
     * process_kill), so a deferred sweep is the simplest correct
     * place to release the slot. */
    g_runner_orphans_reaped_total += (uint64_t)process_reap_orphans();
    return polled;
}

uint32_t service_runner_pid(void) {
    return g_runner_pid;
}

int service_runner_stats_get(struct service_runner_stats *out) {
    if (out == (struct service_runner_stats *)0) {
        return -1;
    }
    out->pid = g_runner_pid;
    out->step_count = g_runner_step_count;
    out->services_polled_total = g_runner_services_polled_total;
    out->last_tick = g_runner_last_tick;
    out->orphans_reaped_total = g_runner_orphans_reaped_total;
    return 0;
}
