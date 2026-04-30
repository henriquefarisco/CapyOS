#ifndef ARCH_X86_64_CPU_LOCAL_H
#define ARCH_X86_64_CPU_LOCAL_H

/* Per-CPU area accessed via the GS segment base.
 *
 * The x86_64 syscall path in src/arch/x86_64/syscall/syscall_entry.S
 * needs a small piece of memory that is reachable from kernel mode
 * with a single `%gs:offset` load even before the kernel has finished
 * unwinding the SYSCALL state. That memory holds:
 *
 *   - the kernel stack pointer for the active CPU
 *     (kernel_rsp, offset 0x00)
 *   - a one-slot scratch where syscall entry stashes the user RSP
 *     while it switches to the kernel stack
 *     (user_rsp_scratch, offset 0x08)
 *
 * The two offsets are exposed as preprocessor constants because the
 * asm sources reference them directly with `%gs:CPU_LOCAL_..._OFFSET`.
 * The C struct with the same layout is locked by tests/test_cpu_local.c
 * via offsetof and sizeof so a future field addition cannot silently
 * drift the asm offsets.
 *
 * The loader writes IA32_GS_BASE (MSR 0xC0000101) so that GS:0 lands
 * at the cpu_local area while running in kernel mode. A future SMP
 * step (M5+) will introduce one cpu_local per logical CPU and use
 * `swapgs` on the syscall boundary; today we only support a single
 * boot CPU so a static cpu_local + IA32_GS_BASE write is enough. */

/* Offsets used directly from .S sources. Must match struct cpu_local
 * declared below. tests/test_cpu_local.c locks both at host build
 * time. */
#define CPU_LOCAL_KERNEL_RSP_OFFSET 0x00
#define CPU_LOCAL_USER_RSP_SCRATCH_OFFSET 0x08
#define CPU_LOCAL_SIZE 0x10

/* IA32_GS_BASE - kernel-mode GS base address.
 * IA32_KERNEL_GS_BASE - shadow value used by `swapgs`.
 * Vol. 4, Section 2.16.1 of the Intel SDM. */
#define IA32_GS_BASE_MSR 0xC0000101u
#define IA32_KERNEL_GS_BASE_MSR 0xC0000102u

#ifndef __ASSEMBLER__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct cpu_local {
  /* Kernel stack pointer for the active CPU. syscall_entry loads this
   * into %rsp via `mov %gs:0x00, %rsp` immediately after stashing the
   * user RSP. */
  uint64_t kernel_rsp;
  /* Scratch slot for the user RSP during a syscall. Owned by the
   * syscall path; nothing else may reuse it without coordinating with
   * syscall_entry.S. */
  uint64_t user_rsp_scratch;
};

/* Initialize the boot CPU's cpu_local area and write IA32_GS_BASE so
 * `%gs:0x00` reaches it. Must be called once early in
 * `kernel_main`, before `syscall_init` enables the syscall path.
 * Idempotent: a second call updates the kernel_rsp slot but does not
 * re-write the MSR. */
void cpu_local_init(uint64_t kernel_rsp);

/* Update only the kernel_rsp field for the active CPU. This is what
 * a future scheduler hook will call when the kernel-side stack of the
 * about-to-run kernel thread changes; today there is exactly one
 * kernel stack and this stays unused outside of tests. */
void cpu_local_set_kernel_rsp(uint64_t kernel_rsp);

/* Read the kernel_rsp slot. Test-only accessor; production code is
 * expected to consume this through `%gs:CPU_LOCAL_KERNEL_RSP_OFFSET`. */
uint64_t cpu_local_get_kernel_rsp(void);

/* Returns 1 if cpu_local_init() has run successfully, 0 otherwise. */
int cpu_local_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif /* __ASSEMBLER__ */

#endif /* ARCH_X86_64_CPU_LOCAL_H */
