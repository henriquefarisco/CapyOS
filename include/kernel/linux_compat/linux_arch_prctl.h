#ifndef KERNEL_LINUX_COMPAT_LINUX_ARCH_PRCTL_H
#define KERNEL_LINUX_COMPAT_LINUX_ARCH_PRCTL_H

/* Linux ABI `arch_prctl(2)` -- x86_64-specific TLS register
 * setup. This is THE most critical syscall musl issues during
 * `__libc_start_main`: before any TLS-allocated variable is
 * touched, musl calls `arch_prctl(ARCH_SET_FS, tls_base)` to
 * tell the kernel where the thread-local storage block lives.
 *
 * Kernel side semantics on x86_64:
 *
 *   ARCH_SET_FS  (0x1002): write `addr` to the IA32_FS_BASE MSR
 *                          (0xC0000100). %fs:0 thereafter resolves
 *                          to `addr`.
 *   ARCH_GET_FS  (0x1003): read the IA32_FS_BASE MSR; copy to
 *                          `*(uint64_t *)addr`.
 *   ARCH_SET_GS  (0x1001): write to IA32_GS_BASE MSR.
 *   ARCH_GET_GS  (0x1004): read IA32_GS_BASE MSR.
 *
 * On CapyOS today we have a single user task (no thread groups
 * yet), so this module stores fs/gs base in module-local state
 * and writes the MSR via an injected callback. When S1.4 thread
 * groups land, the storage migrates to per-task and the value
 * is reapplied via `wrmsr` on context switch.
 *
 * Marco M1: musl shell + SpiderMonkey single-thread runs do
 * exactly one ARCH_SET_FS at startup; that's enough to unblock
 * the TLS access pattern.
 *
 * NOTE on GS: Linux's arch_prctl ARCH_SET_GS writes to
 * IA32_GS_BASE (the user gs base). CapyOS uses GS for the cpu-
 * local pointer in kernel mode (via `swapgs` on syscall entry),
 * so the user GS is what we control here. On the syscall
 * boundary we already swap to kernel GS, so writing user GS
 * here is safe. */

#include <stdint.h>

/* Linux x86_64 op codes (from <asm/prctl.h>). */
#define LINUX_ARCH_SET_GS  0x1001
#define LINUX_ARCH_SET_FS  0x1002
#define LINUX_ARCH_GET_FS  0x1003
#define LINUX_ARCH_GET_GS  0x1004

/* Hardware abstraction. Tests inject fakes; production wires
 * to wrmsr/rdmsr against IA32_FS_BASE / IA32_GS_BASE. */
struct linux_arch_prctl_ops {
    void     (*set_fs_base)(uint64_t addr);
    uint64_t (*get_fs_base)(void);
    void     (*set_gs_base)(uint64_t addr);
    uint64_t (*get_gs_base)(void);
};

void linux_arch_prctl_install_ops(const struct linux_arch_prctl_ops *ops);
void linux_arch_prctl_reset_for_tests(void);

/* Returns 0 on success, negative Linux errno on failure:
 *   -LINUX_EINVAL  unknown op
 *   -LINUX_EFAULT  bad addr ptr (only for GET ops)
 *   -LINUX_ENOSYS  op recognised but no callback installed
 *
 * The Linux signature is `arch_prctl(int op, unsigned long arg)`.
 * For SET_*, `arg` is the value. For GET_*, `arg` is a pointer
 * to a `uint64_t` where the result is stored. */
int64_t linux_arch_prctl(int op, uint64_t arg);

void linux_arch_prctl_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_ARCH_PRCTL_H */
