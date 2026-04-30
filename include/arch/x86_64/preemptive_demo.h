#ifndef ARCH_X86_64_PREEMPTIVE_DEMO_H
#define ARCH_X86_64_PREEMPTIVE_DEMO_H

/*
 * Two-task kernel-mode preemption demo (M4 phase 8e).
 *
 * Single entry point. No-op when CAPYOS_PREEMPTIVE_DEMO is undefined
 * so kernel_main can call it unconditionally.
 *
 * The function is "noreturn-like" in the demo build: it spawns two
 * busy kernel tasks and one-way-jumps into the first via
 * context_switch_into_first. Once the trampoline jumps, the original
 * boot stack is abandoned and the boot flow never resumes - the
 * demo build is intentionally not meant to reach the kernel shell.
 */
void capyos_preemptive_demo_run(void);

#endif /* ARCH_X86_64_PREEMPTIVE_DEMO_H */
