/* Active address-space guard.
 *
 * Kept separate from the page-table builder/walker so teardown safety does
 * not grow the already dense vmm.c module.  The process reaper uses this
 * query to defer destruction while the CPU still has an address space loaded
 * in CR3.
 */
#include "memory/vmm.h"

/* CR3 carries the page-table root in bits 12..51; lower bits are control
 * flags (PCID/PWT/PCD) and must not participate in an address-space identity
 * comparison. Keep this local because it is a CR3 contract, not a public PTE
 * flag consumed by VMM callers. */
#define VMM_CR3_PHYS_MASK 0x000FFFFFFFFFF000ULL

int vmm_address_space_is_active(const struct vmm_address_space *as) {
  if (!as || !as->pml4_phys) return 0;
#if defined(__x86_64__) && !defined(UNIT_TEST)
  uint64_t cr3;
  __asm__ volatile("movq %%cr3, %0" : "=r"(cr3));
  return (cr3 & VMM_CR3_PHYS_MASK) ==
         (as->pml4_phys & VMM_CR3_PHYS_MASK);
#else
  return 0;
#endif
}
