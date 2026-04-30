#include "arch/x86_64/cpu_local.h"

#include <stdint.h>

/* Boot-CPU per-CPU area. M4 phase 3.5 only supports a single CPU; a
 * future SMP step will replace this static with a per-logical-CPU
 * array indexed by APIC ID. Nothing outside this TU may take its
 * address - all access goes through `%gs:offset` from asm or through
 * the public accessors here for tests. */
static struct cpu_local g_boot_cpu_local;
static int g_cpu_local_initialized;

/* Compile-time pin of the layout. If anyone reorders the struct the
 * asm offsets in syscall_entry.S would silently start writing to the
 * wrong slot; better to fail the build. */
_Static_assert(sizeof(struct cpu_local) == CPU_LOCAL_SIZE,
               "cpu_local struct size drifted from CPU_LOCAL_SIZE");
_Static_assert(__builtin_offsetof(struct cpu_local, kernel_rsp) ==
                   CPU_LOCAL_KERNEL_RSP_OFFSET,
               "kernel_rsp offset drifted from CPU_LOCAL_KERNEL_RSP_OFFSET");
_Static_assert(__builtin_offsetof(struct cpu_local, user_rsp_scratch) ==
                   CPU_LOCAL_USER_RSP_SCRATCH_OFFSET,
               "user_rsp_scratch offset drifted from "
               "CPU_LOCAL_USER_RSP_SCRATCH_OFFSET");

#if defined(__x86_64__) && !defined(UNIT_TEST)
static inline void wrmsr64(uint32_t msr, uint64_t value) {
  uint32_t low = (uint32_t)(value & 0xFFFFFFFFu);
  uint32_t high = (uint32_t)(value >> 32);
  __asm__ volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high) : "memory");
}
#else
/* Host stub: tests do not actually touch the MSR. */
static inline void wrmsr64(uint32_t msr, uint64_t value) {
  (void)msr;
  (void)value;
}
#endif

void cpu_local_init(uint64_t kernel_rsp) {
  g_boot_cpu_local.kernel_rsp = kernel_rsp;
  g_boot_cpu_local.user_rsp_scratch = 0;

  if (!g_cpu_local_initialized) {
    /* Point IA32_GS_BASE at the cpu_local area so that any kernel-mode
     * `%gs:offset` load reaches the right memory. The shadow
     * IA32_KERNEL_GS_BASE is left at zero for now; once we add
     * `swapgs` on the syscall boundary the shadow will hold the user
     * GS the program wants to keep. */
    wrmsr64(IA32_GS_BASE_MSR, (uint64_t)(uintptr_t)&g_boot_cpu_local);
    wrmsr64(IA32_KERNEL_GS_BASE_MSR, 0);
    g_cpu_local_initialized = 1;
  }
}

void cpu_local_set_kernel_rsp(uint64_t kernel_rsp) {
  g_boot_cpu_local.kernel_rsp = kernel_rsp;
}

uint64_t cpu_local_get_kernel_rsp(void) {
  return g_boot_cpu_local.kernel_rsp;
}

int cpu_local_is_initialized(void) { return g_cpu_local_initialized; }
