/*
 * Two-task kernel-mode preemption demo (M4 phase 8e).
 *
 * Gated on CAPYOS_PREEMPTIVE_DEMO (which itself implies
 * CAPYOS_PREEMPTIVE_SCHEDULER, otherwise the scheduler is still
 * cooperative and the demo cannot demonstrate preemption). Default
 * builds compile this file as a no-op so production kernels are
 * byte-for-byte unchanged.
 *
 * What the demo proves:
 *   1. The APIC tick (phase 8c IRQ install) actually drives
 *      scheduler_tick() in the kernel.
 *   2. scheduler_tick decrements quantum and calls schedule()
 *      exactly on the quantum boundary (phase 8a/8d wiring).
 *   3. context_switch correctly swaps two kernel-mode tasks back
 *      and forth, with each task ACTUALLY executing its entry
 *      body (phase 2 contract + phase 8e first-task trampoline).
 *
 * Architecture:
 *   - Two infinite-loop tasks (busy_a_entry, busy_b_entry) emit
 *     a unique marker every N iterations to debugcon. The smoke
 *     watches for both markers within a wall-clock window.
 *   - capyos_preemptive_demo_run() spawns both, sets the first as
 *     current, and one-way-jumps into it via context_switch_into_first.
 *   - From there the boot stack is abandoned. Subsequent quantum
 *     exhaustions drive context_switch (the conventional one) which
 *     correctly preserves both tasks' contexts so they alternate
 *     forever.
 *
 * The demo intentionally does not call scheduler_start() - that
 * function creates an idle task and enters its own hlt loop on the
 * boot stack, which would steal a slot from the demo and skew
 * scheduling decisions. The phase 8e flow is more direct.
 */

#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/kernel_main_internal.h" /* dbgcon_putc */
#include "kernel/scheduler.h"
#include "kernel/task.h"

#ifdef CAPYOS_PREEMPTIVE_DEMO

#ifndef CAPYOS_PREEMPTIVE_SCHEDULER
#error "CAPYOS_PREEMPTIVE_DEMO requires CAPYOS_PREEMPTIVE_SCHEDULER"
#endif

/* Marker frequency: every N busy-loop iterations one task emits its
 * marker. Picked so each task gets at least one marker visible in a
 * QEMU TCG run inside a few seconds without flooding the debugcon. */
#define DEMO_MARKER_PERIOD 0x40000ULL

extern void context_switch_into_first(struct task_context *new_ctx);

static void busy_a_entry(void *arg) {
    (void)arg;
    uint64_t i = 0;
    for (;;) {
        if ((i++ & (DEMO_MARKER_PERIOD - 1ULL)) == 0ULL) {
            dbgcon_putc('[');
            dbgcon_putc('b');
            dbgcon_putc('u');
            dbgcon_putc('s');
            dbgcon_putc('y');
            dbgcon_putc('A');
            dbgcon_putc(']');
            dbgcon_putc('\n');
        }
        __asm__ volatile("pause");
    }
}

static void busy_b_entry(void *arg) {
    (void)arg;
    uint64_t i = 0;
    for (;;) {
        if ((i++ & (DEMO_MARKER_PERIOD - 1ULL)) == 0ULL) {
            dbgcon_putc('[');
            dbgcon_putc('b');
            dbgcon_putc('u');
            dbgcon_putc('s');
            dbgcon_putc('y');
            dbgcon_putc('B');
            dbgcon_putc(']');
            dbgcon_putc('\n');
        }
        __asm__ volatile("pause");
    }
}

extern void task_set_current(struct task *t);

void capyos_preemptive_demo_run(void) {
    /* Spawn the two demo tasks. task_create initialises
     * quantum_remaining=SCHED_DEFAULT_QUANTUM (phase 8a) so neither
     * task immediately context-switches away on its first tick. */
    struct task *a = task_create_kernel("busy_a", busy_a_entry, (void *)0);
    struct task *b = task_create_kernel("busy_b", busy_b_entry, (void *)0);
    if (!a || !b) {
        /* Allocation failure. Emit a debug marker and bail out so the
         * boot continues normally (the smoke will report the missing
         * markers as a failure). */
        dbgcon_putc('[');
        dbgcon_putc('d');
        dbgcon_putc('e');
        dbgcon_putc('m');
        dbgcon_putc('o');
        dbgcon_putc(':');
        dbgcon_putc('a');
        dbgcon_putc('l');
        dbgcon_putc('l');
        dbgcon_putc('o');
        dbgcon_putc('c');
        dbgcon_putc(']');
        dbgcon_putc('\n');
        return;
    }
    a->state = TASK_STATE_READY;
    b->state = TASK_STATE_READY;
    scheduler_add(a);
    scheduler_add(b);

    /* The trampoline expects the new task's state to already be
     * RUNNING and task_current() to point at it, so scheduler_tick
     * (which fires from inside the running task) sees a coherent
     * view from the very first tick. */
    a->state = TASK_STATE_RUNNING;
    task_set_current(a);

    dbgcon_putc('[');
    dbgcon_putc('d');
    dbgcon_putc('e');
    dbgcon_putc('m');
    dbgcon_putc('o');
    dbgcon_putc(':');
    dbgcon_putc('e');
    dbgcon_putc('n');
    dbgcon_putc('t');
    dbgcon_putc('e');
    dbgcon_putc('r');
    dbgcon_putc(']');
    dbgcon_putc('\n');

    /* Noreturn: the boot stack is abandoned and execution continues
     * inside busy_a_entry. From here on the scheduler tick alternates
     * between busy_a and busy_b at quantum boundaries. */
    context_switch_into_first(&a->context);
    /* Defensive halt in case the trampoline ever returns (it does
     * not in production builds). */
    for (;;) __asm__ volatile("hlt");
}

#else /* !CAPYOS_PREEMPTIVE_DEMO */

void capyos_preemptive_demo_run(void) {
    /* No-op when the demo flag is off. */
}

#endif /* CAPYOS_PREEMPTIVE_DEMO */
