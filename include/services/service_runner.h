/*
 * Service runner - kernel task that drives the service_manager and
 * work_queue periodic polls.
 *
 * Phase 1 of the M4 finalization rolls service polling into a dedicated
 * kernel task so that:
 *   - the runner is observable through task_iter (PID, name "service-runner",
 *     state, scheduling priority);
 *   - the existing cooperative call path (kernel_service_poll) keeps working
 *     by delegating to service_runner_step until the scheduler is flipped to
 *     preemptive in phase 8;
 *   - host tests can drive the runner step-by-step without booting a real
 *     scheduler, while production still benefits from the same body.
 *
 * The runner does NOT perform any service work of its own. It is a thin
 * orchestrator that pulls due polls from service_manager_poll_due() and
 * work_queue_poll_due() so that all kernel-side cadence flows through a
 * single, visible task. Once preemptive scheduling lands, the cooperative
 * delegation in kernel_service_poll() is removed and the runner runs
 * naturally as part of the scheduler dispatch loop.
 */
#ifndef SERVICES_SERVICE_RUNNER_H
#define SERVICES_SERVICE_RUNNER_H

#include <stdint.h>

#define SERVICE_RUNNER_NAME "service-runner"

/* Default cooperative interval (in scheduler ticks) used when the runner
 * task body sleeps between polls. Tests do not depend on this value. */
#define SERVICE_RUNNER_DEFAULT_INTERVAL_TICKS 10u

struct service_runner_stats {
    uint32_t pid;                     /* PID of the kernel task; 0 if absent */
    uint64_t step_count;              /* total invocations of step()       */
    uint64_t services_polled_total;   /* sum of service_manager_poll_due   */
    uint64_t last_tick;               /* tick value passed to last step()  */
    /* Phase 6.6: cumulative count of zombie orphans reaped by the
     * `process_reap_orphans()` sweep that runs at the end of every
     * step(). Stays at 0 in the common case (no orphans). Used by
     * tests/test_service_runner.c to confirm the reaper is wired
     * and by observability tools to flag pathological reap rates. */
    uint64_t orphans_reaped_total;
};

/* Idempotent. Spawns the kernel task if it does not exist yet. After a
 * successful call the task is registered in the task table and the PID
 * is stable for the lifetime of the kernel. Calling this multiple times
 * is safe and a no-op. Designed to be called once during kernel bring-up
 * (after task system zero-init and after service_manager_init/work_queue_init). */
void service_runner_init(void);

/* Test-only: drop the runner task handle and zero counters. Production
 * never calls this; host tests call it between scenarios. */
void service_runner_reset(void);

/* Drive one polling step. Returns the number of services that the
 * service_manager actually polled, or 0 if none were due. Counters and
 * last_tick are updated regardless. Safe to call from either the
 * cooperative kernel_service_poll() path or the runner task body. */
int service_runner_step(uint64_t now_ticks);

/* Returns the PID assigned to the runner task. 0 means the task has
 * not been spawned yet (e.g. service_runner_init() was never called or
 * task_create returned NULL because the table is full). */
uint32_t service_runner_pid(void);

/* Returns 0 on success and writes a snapshot to *out. Returns -1 if
 * out is NULL. Stats reflect the state observed by previous step()
 * calls; reading them does not advance any counter. */
int service_runner_stats_get(struct service_runner_stats *out);

#endif /* SERVICES_SERVICE_RUNNER_H */
