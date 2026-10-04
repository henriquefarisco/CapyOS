/* vmm_fault.c — demand-paging and copy-on-write fault servicing.
 *
 * Extracted verbatim from vmm.c on 2026-09-21 to keep that file under the
 * source-layout size budget. Shares VMM_PTE_PHYS_MASK, invlpg() and the
 * global counters through src/memory/internal/vmm_internal.h; no logic
 * changed. Entry point: vmm_handle_page_fault() (declared in memory/vmm.h).
 */
#include "memory/vmm.h"
#include "memory/vmm_cow.h"
#include "memory/pmm.h"
#include "memory/internal/vmm_internal.h"
#include <stddef.h>
#include <stdint.h>

/* Phase 7b: real demand-paging handler.
 *
 * Called by `x64_exception_dispatch` when `arch_fault_classify`
 * returns ARCH_FAULT_RECOVERABLE (vec=14, user CPL, error code with
 * P=0 / RSVD=0 / PK=0). The dispatcher already discarded faults that
 * the classifier flagged as KILL_PROCESS or KERNEL_PANIC; this body
 * therefore only needs to attempt service.
 *
 * Service flow:
 *   1. Look up the current process's address space. The page fault
 *      entry path knows cr2 and the error code but not who owns the
 *      AS, so we walk through `process_current()`.
 *   2. Search the AS's anonymous-region registry for a region
 *      containing `fault_addr`. No region match -> -1 (the dispatcher
 *      then escalates to KILL_PROCESS).
 *   3. Allocate a fresh physical page from the PMM. Zero-fill it so
 *      the user observes the standard "new memory is zeroed"
 *      contract (POSIX, Linux, ...).
 *   4. Install a PTE for the page-aligned virtual page covering
 *      `fault_addr`, using the region's flags. `vmm_map_page` bumps
 *      the per-AS RSS counter.
 *   5. Return 0; the dispatcher resumes user-mode execution.
 *
 * Error paths (any failure) return -1 and the dispatcher escalates
 * through the kill path. The PMM allocation can fail under heavy
 * pressure; we deliberately do NOT free the partial work here
 * because we never installed a PTE if the alloc failed. If the
 * mapping itself fails (e.g. out of page-table memory) we free the
 * just-allocated frame to avoid leaking it. */
/* Walk an AS's page tables down to the leaf 4 KiB PTE that maps
 * `virt`. Returns the address of the slot inside the leaf PT or NULL
 * if any intermediate level is not present. Used by phase 7c CoW
 * service to read/write the faulting PTE in place. */
static uint64_t *vmm_walk_to_leaf(struct vmm_address_space *as,
                                  uint64_t virt) {
  if (!as) return NULL;
  uint64_t *pml4 = as->pml4_virt;
  uint32_t pml4_idx = (virt >> 39) & 0x1FF;
  uint32_t pdpt_idx = (virt >> 30) & 0x1FF;
  uint32_t pd_idx   = (virt >> 21) & 0x1FF;
  uint32_t pt_idx   = (virt >> 12) & 0x1FF;

  if (!(pml4[pml4_idx] & VMM_PAGE_PRESENT)) return NULL;
  uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4[pml4_idx] & VMM_PTE_PHYS_MASK);
  if (!(pdpt[pdpt_idx] & VMM_PAGE_PRESENT)) return NULL;
  uint64_t *pd = (uint64_t *)(uintptr_t)(pdpt[pdpt_idx] & VMM_PTE_PHYS_MASK);
  if (!(pd[pd_idx] & VMM_PAGE_PRESENT)) return NULL;
  if (pd[pd_idx] & VMM_PAGE_HUGE) return NULL; /* phase 7c: skip 2 MiB */
  uint64_t *pt = (uint64_t *)(uintptr_t)(pd[pd_idx] & VMM_PTE_PHYS_MASK);
  return &pt[pt_idx];
}

/* Phase 7c: copy-on-write fault servicing.
 *
 * Returns 0 on success (PTE rewritten, ready to retry the user
 * instruction), -1 if the fault is not a CoW share and should be
 * escalated to KILL_PROCESS by the dispatcher.
 *
 * The decision matrix (vmm_cow_decide) lives in src/memory/vmm_cow.c
 * so the host tests can lock its truth table; this function is the
 * thin x86_64 glue that does the actual frame allocation, byte
 * copy, PTE rewrite and TLB invalidation. */
static int vmm_handle_cow_fault(struct vmm_address_space *as,
                                uint64_t fault_addr) {
  uint64_t *pte_slot = vmm_walk_to_leaf(as, fault_addr);
  if (!pte_slot) return -1;

  uint64_t pte = *pte_slot;
  if (!(pte & VMM_PAGE_PRESENT)) return -1;

  uint64_t old_phys = pte & VMM_PTE_PHYS_MASK;

  /* Decrement BEFORE the policy decision so vmm_cow_decide observes
   * the post-dec count. Frames that were never refcounted (e.g. text
   * pages mapped by elf_load before CoW lands) report 0 here; the
   * decision module then sees refcount_after_dec=0 and selects REUSE
   * which is exactly what we want for a single-AS write. */
  uint16_t pre = pmm_frame_refcount_get(old_phys);
  uint16_t after_dec =
      (pre > 0u) ? pmm_frame_refcount_dec(old_phys) : 0u;

  struct vmm_cow_decision d = vmm_cow_decide(pte, after_dec);

  if (d.action == VMM_COW_NOT_COW) {
    /* PTE wasn't actually a CoW share. The dispatcher will escalate
     * to KILL_PROCESS. Restore the refcount we just decremented so
     * the destroy walker still sees a consistent count when the
     * process is reaped. */
    if (pre > 0u) pmm_frame_refcount_inc(old_phys);
    return -1;
  }

  if (d.action == VMM_COW_REUSE) {
    /* Last sharer: just flip flags in place. The frame stays where
     * it is; refcount is now 0 (single-AS owner) which matches what
     * the destroy walker expects for a non-shared user mapping. */
    *pte_slot = (pte | d.new_set) & ~d.new_clr;
    invlpg(fault_addr);
    vmm_global_stats.cow_faults++;
    return 0;
  }

  /* VMM_COW_COPY: still shared. Allocate a fresh frame, copy 4 KiB,
   * point the PTE at the new frame with WRITE set / COW cleared.
   * The new frame starts with refcount=0 (single-AS owner) which
   * matches the contract used by single-AS user mappings. */
  uint64_t new_phys = pmm_alloc_page();
  if (!new_phys) {
    /* Allocation failure: undo the refcount decrement so destroy
     * still sees a consistent count and let the dispatcher kill the
     * process. */
    if (pre > 0u) pmm_frame_refcount_inc(old_phys);
    return -1;
  }

  /* Identity-mapped low-memory copy. Same pattern used by demand
   * paging zero-fill above. */
  const uint8_t *src_bytes = (const uint8_t *)(uintptr_t)old_phys;
  uint8_t *dst_bytes = (uint8_t *)(uintptr_t)new_phys;
  for (size_t i = 0; i < VMM_PAGE_SIZE; ++i) dst_bytes[i] = src_bytes[i];

  uint64_t new_pte =
      ((pte & 0xFFFULL) | new_phys | d.new_set) & ~d.new_clr;
  *pte_slot = new_pte;
  invlpg(fault_addr);
  vmm_global_stats.cow_faults++;
  return 0;
}

int vmm_handle_page_fault(uint64_t fault_addr, uint64_t error_code) {
  vmm_global_stats.page_faults++;

  struct vmm_address_space *as = vmm_current_address_space();
  if (!as) return -1;

  /* M4 phase 7c: present-page write fault is the CoW arm. Dispatch
   * to the CoW handler before falling into the demand-paging path,
   * which only services not-present (P=0) faults. */
  if (error_code & 0x1u /* P=1 */) {
    if (error_code & 0x2u /* W=1 */) {
      return vmm_handle_cow_fault(as, fault_addr);
    }
    /* Present + non-write fault is not recoverable today. */
    return -1;
  }

  /* P=0 path: demand paging via anonymous-region registry. */
  struct vmm_anon_region *region = vmm_anon_region_find(as, fault_addr);
  if (!region) return -1;

  uint64_t phys = pmm_alloc_page();
  if (!phys) return -1;

  /* Zero-fill via the kernel's identity-mapped (or low-memory direct)
   * view of physical RAM. The same pattern is used by
   * `vmm_create_address_space` to scrub fresh PML4 pages. */
  uint8_t *page_virt = (uint8_t *)(uintptr_t)phys;
  for (size_t i = 0; i < VMM_PAGE_SIZE; i++) page_virt[i] = 0;

  uint64_t page_aligned_virt = fault_addr & ~((uint64_t)VMM_PAGE_SIZE - 1);
  if (vmm_map_page(as, page_aligned_virt, phys, region->flags) != 0) {
    pmm_free_page(phys);
    return -1;
  }
  return 0;
}
