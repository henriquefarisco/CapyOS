/* vmm_internal.h — private helpers shared by the src/memory/vmm*.c TU group.
 *
 * Files that include this:
 *   - vmm.c        (address-space lifecycle, mapping, kernel tables)
 *   - vmm_fault.c  (demand-paging and copy-on-write fault servicing)
 *
 * Both TUs are kernel-only (x86_64 inline asm); host tests link
 * tests/stubs/stub_vmm.c instead and must NOT include this header.
 * Do NOT include this from files outside src/memory/.
 */
#ifndef MEMORY_INTERNAL_VMM_INTERNAL_H
#define MEMORY_INTERNAL_VMM_INTERNAL_H

#include <stdint.h>

#include "memory/vmm.h"

/* x86_64 PTE physical-frame mask: bits 12..51 hold the 4 KiB-aligned
 * frame address. Bits 0..11 are flags (P/W/U/A/D/PAT/G/avl), bits
 * 52..62 are reserved/avl/PKE, and bit 63 is NX. Using `~0xFFFULL`
 * only cleared bits 11..0 and consequently leaked the NX bit (and
 * any future PKE/avl bits) into the returned "physical" value. When
 * the caller cast the result to a kernel pointer for identity-map
 * writes (e.g. `elf_load` copying a non-executable .data segment),
 * the high bit produced a NON-CANONICAL address which #GP'd on the
 * very first store. The bug was masked by binaries whose only
 * PT_LOAD was executable (NX=0) -- hello and exectarget -- and
 * surfaced the moment a larger user binary introduced a separate writable
 * .rodata/.data segment with NX=1. */
#define VMM_PTE_PHYS_MASK 0x000FFFFFFFFFF000ULL
int vmm_prepare_device_window(uint64_t *pml4);

static inline void invlpg(uint64_t addr) {
  __asm__ volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

/* Defined in vmm.c. vmm_fault.c bumps page_faults/cow_faults in place;
 * vmm_stats_get() in vmm.c remains the only reader exposed to callers. */
extern struct vmm_stats vmm_global_stats;

#endif /* MEMORY_INTERNAL_VMM_INTERNAL_H */
