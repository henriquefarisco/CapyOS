/*
 * Preemptive scheduler boot wiring (M4 phase 8b/c/d).
 *
 * Extracted from src/arch/x86_64/kernel_main.c to keep the latter
 * under the 900-line monolith ceiling enforced by `make layout-audit`.
 * The whole file is gated on CAPYOS_PREEMPTIVE_SCHEDULER: a default
 * build without the flag compiles the helpers as no-ops so kernel_main
 * does not need its own #ifdefs around the call sites.
 *
 * Phase mapping:
 *   8b - capyos_preemptive_install_policy()  : flips scheduler policy
 *        from cooperative to PRIORITY before the APIC tick is armed.
 *   8c - capyos_preemptive_install_irq0()    : registers
 *        apic_timer_irq_handler at IRQ 0 so the IDT vector 32 stub
 *        actually dispatches the registered scheduler_tick callback.
 *   8b - capyos_preemptive_mark_running()    : sets sched_running=1
 *        without entering scheduler_start()'s noreturn idle loop.
 *   8d - capyos_preemptive_observe_ticks()   : enables interrupts,
 *        spins until apic_timer_ticks() reaches a small floor or the
 *        bounded busy loop exhausts, disables interrupts again, and
 *        emits the observed count to debugcon. The smoke regression
 *        guard parses the trailing decimal and asserts it is > 0.
 *
 * Why debugcon AND klog?
 *   The smoke harness reads the kernel debug-console (port 0xE9)
 *   from disk. klog() only buffers into a ring; nothing in the
 *   default boot path forwards that ring to debugcon, so klog-only
 *   markers are invisible to CI (this was the bug discovered while
 *   wiring up phase 8c). The pattern here mirrors phase 5d/5f.
 */

#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/apic.h"
#include "arch/x86_64/interrupts.h"
#include "arch/x86_64/kernel_main_internal.h" /* dbgcon_putc */
#include "kernel/log/klog.h"
#include "kernel/scheduler.h"

#ifdef CAPYOS_PREEMPTIVE_SCHEDULER

static void pb_dbgcon_write(const char *s) {
    if (!s) return;
    while (*s) {
        dbgcon_putc((uint8_t)*s++);
    }
}

void capyos_preemptive_install_policy(void) {
    scheduler_init(SCHED_POLICY_PRIORITY);
    klog(KLOG_INFO,
         "[scheduler] Policy=PRIORITY (preemptive flip enabled).");
    pb_dbgcon_write(
        "[scheduler] Policy=PRIORITY (preemptive flip enabled).\n");
}

void capyos_preemptive_install_irq0(void) {
    /* M4 phase 8c: close the latent bug where apic_timer_set_callback
     * had been called but g_irq_handlers[0] was still NULL, so the
     * IDT stub at vector 32 dispatched into x64_exception_dispatch's
     * IRQ branch, found no handler, sent a spurious PIC EOI, and
     * silently dropped the tick. */
    irq_install_handler(0, apic_timer_irq_handler);
}

void capyos_preemptive_mark_running(void) {
    /* Emit the canonical "Preemptive tick armed" marker through the
     * debugcon too. The matching klog() call still lives in kernel_main
     * because it is also useful for non-preemptive builds (it is the
     * existing INFO log and we do not want to duplicate it). */
    pb_dbgcon_write("[scheduler] Preemptive tick armed at 100Hz.\n");
    scheduler_set_running(1);
    klog(KLOG_INFO,
         "[scheduler] Marked as running (sched_running=1).");
    pb_dbgcon_write("[scheduler] Marked as running (sched_running=1).\n");
    klog(KLOG_INFO,
         "[scheduler] APIC IRQ handler installed at IRQ 0.");
    pb_dbgcon_write("[scheduler] APIC IRQ handler installed at IRQ 0.\n");
}

static void capyos_preemptive_emit_dec(uint64_t value) {
    /* Hand-rolled decimal -> debugcon. snprintf is intentionally
     * avoided to keep this freestanding-friendly with no libc dep. */
    char buf[24];
    size_t i = sizeof(buf);
    buf[--i] = '\0';
    if (value == 0ULL) {
        buf[--i] = '0';
    } else {
        while (value > 0ULL && i > 0u) {
            buf[--i] = (char)('0' + (value % 10ULL));
            value /= 10ULL;
        }
    }
    pb_dbgcon_write(&buf[i]);
}

void capyos_preemptive_observe_ticks(void) {
    /* M4 phase 8d: bounded soak. With PRIORITY policy active but the
     * run queue empty and task_current()==NULL, scheduler_tick is
     * safe: it increments stats.total_ticks, walks an empty run queue
     * (no sleeper wakeups, no zombie reaping), and the preemptive
     * schedule() call returns immediately because pick_next returns
     * NULL. No context_switch to a non-existent task. */
    x64_interrupts_enable();
    {
        uint64_t spin_budget = 0;
        while (apic_timer_ticks() < 3ULL && spin_budget < 50000000ULL) {
            __asm__ volatile("pause");
            ++spin_budget;
        }
    }
    x64_interrupts_disable();

    klog_dec(KLOG_INFO, "[scheduler] APIC ticks observed=",
             (uint32_t)apic_timer_ticks());
    pb_dbgcon_write("[scheduler] APIC ticks observed=");
    capyos_preemptive_emit_dec(apic_timer_ticks());
    pb_dbgcon_write("\n");
}

#else /* !CAPYOS_PREEMPTIVE_SCHEDULER */

void capyos_preemptive_install_policy(void) {}
void capyos_preemptive_install_irq0(void) {}
void capyos_preemptive_mark_running(void) {}
void capyos_preemptive_observe_ticks(void) {}

#endif /* CAPYOS_PREEMPTIVE_SCHEDULER */
