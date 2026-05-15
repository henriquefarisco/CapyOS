#include "kernel/linux_compat/linux_arch_prctl.h"

/* Boot wiring for `linux_arch_prctl` against real x86_64 MSRs.
 * Excluded from host tests via UNIT_TEST.
 *
 * On the Linux x86_64 ABI, ARCH_SET_FS/SET_GS write to the
 * IA32_FS_BASE / IA32_GS_BASE MSRs respectively. wrmsr expects
 * MSR id in %ecx, low 32 bits of the value in %eax, high 32
 * bits in %edx. rdmsr is the inverse.
 *
 * IMPORTANT: on syscall entry the kernel runs with `swapgs`
 * already executed -> the current GS_BASE in CPU is the kernel
 * cpu-local pointer. We must NOT overwrite that on
 * ARCH_SET_GS; instead we write the IA32_KERNEL_GS_BASE MSR
 * (the shadow), and the next `swapgs` returning to userland
 * will install it. The kernel keeps using its cpu-local through
 * the syscall window. */

#if !defined(UNIT_TEST) && defined(__x86_64__)

#include <stdint.h>

#define MSR_FS_BASE         0xC0000100u
#define MSR_GS_BASE         0xC0000101u
#define MSR_KERNEL_GS_BASE  0xC0000102u

static inline void wrmsr64(uint32_t msr, uint64_t value) {
    uint32_t low  = (uint32_t)(value & 0xFFFFFFFFu);
    uint32_t high = (uint32_t)(value >> 32);
    __asm__ volatile("wrmsr"
                     :
                     : "c"(msr), "a"(low), "d"(high)
                     : "memory");
}

static inline uint64_t rdmsr64(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | (uint64_t)low;
}

static void wrap_set_fs(uint64_t addr) { wrmsr64(MSR_FS_BASE, addr); }
static uint64_t wrap_get_fs(void)      { return rdmsr64(MSR_FS_BASE); }

/* On the syscall path, kernel GS is already loaded; userland's
 * GS lives in IA32_KERNEL_GS_BASE waiting for swapgs. */
static void wrap_set_gs(uint64_t addr) { wrmsr64(MSR_KERNEL_GS_BASE, addr); }
static uint64_t wrap_get_gs(void)      { return rdmsr64(MSR_KERNEL_GS_BASE); }

void linux_arch_prctl_init_boot(void) {
    static const struct linux_arch_prctl_ops ops = {
        .set_fs_base = wrap_set_fs,
        .get_fs_base = wrap_get_fs,
        .set_gs_base = wrap_set_gs,
        .get_gs_base = wrap_get_gs,
    };
    linux_arch_prctl_install_ops(&ops);
}

#else /* UNIT_TEST or non-x86_64 host */

void linux_arch_prctl_init_boot(void) {}

#endif
