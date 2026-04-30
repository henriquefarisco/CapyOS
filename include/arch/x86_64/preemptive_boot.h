#ifndef ARCH_X86_64_PREEMPTIVE_BOOT_H
#define ARCH_X86_64_PREEMPTIVE_BOOT_H

/*
 * Preemptive scheduler boot wiring helpers (M4 phase 8b/c/d).
 *
 * All four entry points are no-ops when CAPYOS_PREEMPTIVE_SCHEDULER is
 * undefined so kernel_main can call them unconditionally without
 * sprinkling #ifdefs. See src/arch/x86_64/preemptive_boot.c for the
 * full design rationale.
 *
 * Call order from kernel_main (right around the existing APIC arm):
 *
 *   capyos_preemptive_install_policy();   // phase 8b
 *   if (apic_available() && !boot_services) {
 *       apic_timer_set_callback(scheduler_tick);
 *       capyos_preemptive_install_irq0();  // phase 8c
 *       apic_timer_start(100);
 *       capyos_preemptive_mark_running();  // phase 8b/c markers
 *       capyos_preemptive_observe_ticks(); // phase 8d soak
 *   }
 */

void capyos_preemptive_install_policy(void);
void capyos_preemptive_install_irq0(void);
void capyos_preemptive_mark_running(void);
void capyos_preemptive_observe_ticks(void);

#endif /* ARCH_X86_64_PREEMPTIVE_BOOT_H */
