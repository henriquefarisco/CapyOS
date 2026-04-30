#ifndef KERNEL_ARCH_SCHED_HOOKS_H
#define KERNEL_ARCH_SCHED_HOOKS_H

/*
 * Architecture-specific scheduler hooks (M4 phase 8f.2).
 *
 * The kernel scheduler is arch-agnostic by design (src/kernel/scheduler.c
 * has no x86_64 inline asm); when a switch from `current` to `next`
 * is about to happen the scheduler delegates "arch-side preparation"
 * to a single weak-coupling seam declared here. That keeps the
 * arch glue where it belongs (src/arch/x86_64) and gives the host
 * unit tests a single symbol to stub.
 *
 * Today there is exactly one hook and it serves two related but
 * distinct purposes:
 *
 *   1. Update IA32_GS_BASE-backed `cpu_local.kernel_rsp` so the
 *      next syscall from `next` (when it is a ring-3 task) lands
 *      on its own per-task kernel stack. Without this, two ring-3
 *      tasks would clobber each other's syscall frames on the
 *      shared `g_syscall_kernel_stack`.
 *
 *   2. Update the active TSS's RSP0 so the next IRQ from `next`
 *      (when it is a ring-3 task) lands on the same per-task
 *      kernel stack used by the syscall path. Without this, the
 *      first APIC tick after a context switch into a ring-3 task
 *      would push the IRET frame onto the shared stack and
 *      corrupt any prior frame still living there.
 *
 * Tasks without a kernel stack (e.g. the very first idle task
 * spawned by scheduler_start before its kmalloc completes) are a
 * no-op for the hook. Kernel-only tasks that never drop to ring 3
 * benefit too: their RSP0 is set per-task, but since they never
 * cause a ring-3 -> ring-0 transition the only observable effect
 * is the cpu_local update, which is harmless because their syscall
 * path is never taken.
 *
 * Host tests in `tests/test_context_switch.c` lock the contract
 * via the stub in `tests/stub_arch_sched_hooks.c` which records
 * every invocation in a small log.
 */

struct task;

/* Apply the arch-side preparation for the about-to-run task `next`.
 *
 * Must be called by `schedule()` AFTER `task_set_current(next)` and
 * BEFORE `context_switch(...)`. The order matters: the new RSP0
 * must already be programmed when the CPU later transitions back
 * into kernel mode for the new task. Calling it before
 * `task_set_current` is also safe (the hook only needs `next`),
 * but the existing post-`task_set_current` placement is what tests
 * lock today.
 *
 * NULL `next` is a no-op so the hook can be invoked unconditionally
 * even if `pick_next` returned nothing useful. */
void arch_sched_apply_kernel_stack(const struct task *next);

#endif /* KERNEL_ARCH_SCHED_HOOKS_H */
