/*
 * x86_64 implementation of arch-side scheduler hooks (M4 phase 8f.2).
 *
 * Single function: `arch_sched_apply_kernel_stack`. Updates the
 * cpu-local kernel RSP slot (consumed by the syscall fast path via
 * IA32_GS_BASE) AND the TSS RSP0 (consumed by the CPU on ring 3 ->
 * ring 0 IRQ entry) to the same value: the top of the about-to-run
 * task's kernel stack.
 *
 * Pulling both updates into a single seam keeps the syscall path
 * and IRQ path on the same per-task kernel stack so a tick that
 * fires while the task is mid-syscall lands on the in-progress
 * syscall frame's stack (the IRQ pushes its IRET frame ABOVE the
 * existing frames, the handler runs, iretq returns the CPU to the
 * exact instruction it was at, and the syscall continues on the
 * same stack with no clobbering).
 */

#include "kernel/arch_sched_hooks.h"
#include "kernel/task.h"
#include "arch/x86_64/cpu_local.h"
#include "arch/x86_64/tss.h"

#include <stdint.h>

void arch_sched_apply_kernel_stack(const struct task *next) {
    if (!next || !next->kernel_stack || next->kernel_stack_size == 0u) {
        return;
    }
    uint64_t top =
        (uint64_t)(uintptr_t)(next->kernel_stack + next->kernel_stack_size);

    /* Both writes target globals that are already locked by their
     * own host tests (test_cpu_local + test_tss_layout). The order
     * is documented in the header: cpu_local first because the
     * syscall path consumes it via %gs:0x00, then TSS RSP0 because
     * the CPU consumes it on the next IRQ from ring 3. */
    cpu_local_set_kernel_rsp(top);
    tss_set_rsp0(top);
}
