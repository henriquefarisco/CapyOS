#include "memory/vmm.h"
#include "memory/pmm.h"
#include "memory/kmem.h"
#include <stddef.h>

static void vmm_dbgcon_putc(uint8_t c) {
#if defined(__x86_64__) && !defined(UNIT_TEST)
  __asm__ volatile("outb %0, $0xE9" : : "a"(c));
#else
  (void)c;
#endif
}

static struct vmm_address_space kernel_as;
static struct vmm_stats vmm_global_stats;

static inline void invlpg(uint64_t addr) {
  __asm__ volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

static inline uint64_t read_cr3(void) {
  uint64_t val;
  __asm__ volatile("movq %%cr3, %0" : "=r"(val));
  return val;
}

static inline void write_cr3(uint64_t val) {
  __asm__ volatile("movq %0, %%cr3" : : "r"(val) : "memory");
}

static uint64_t *get_or_create_table(uint64_t *table, uint32_t index,
                                      uint64_t flags) {
  if (!(table[index] & VMM_PAGE_PRESENT)) {
    uint64_t phys = pmm_alloc_page();
    if (!phys) return NULL;
    uint8_t *virt = (uint8_t *)(uintptr_t)phys;
    for (int i = 0; i < 4096; i++) virt[i] = 0;
    table[index] = phys | flags | VMM_PAGE_PRESENT;
  }
  return (uint64_t *)(uintptr_t)(table[index] & ~0xFFFULL);
}

void vmm_init(void) {
  kernel_as.pml4_phys = read_cr3() & ~0xFFFULL;
  kernel_as.pml4_virt = (uint64_t *)(uintptr_t)kernel_as.pml4_phys;
  kernel_as.refcount = 1;
  vmm_global_stats.kernel_mapped_pages = 0;
  vmm_global_stats.user_mapped_pages = 0;
  vmm_global_stats.page_faults = 0;
  vmm_global_stats.cow_faults = 0;
}

struct vmm_address_space *vmm_create_address_space(void) {
  struct vmm_address_space *as = (struct vmm_address_space *)kmalloc(sizeof(*as));
  if (!as) {
    vmm_dbgcon_putc('K');
    return NULL;
  }

  uint64_t pml4_phys = pmm_alloc_page();
  if (!pml4_phys) {
    vmm_dbgcon_putc('M');
    kfree(as);
    return NULL;
  }

  uint64_t *pml4 = (uint64_t *)(uintptr_t)pml4_phys;
  for (int i = 0; i < 512; i++) pml4[i] = 0;

  uint64_t *kernel_pml4 = kernel_as.pml4_virt;
  for (int i = 256; i < 512; i++) {
    pml4[i] = kernel_pml4[i];
  }

  as->pml4_phys = pml4_phys;
  as->pml4_virt = pml4;
  as->refcount = 1;
  /* Phase 7b: zero-init the demand-paging registry and RSS counter.
   * Without this, a freshly-allocated AS would carry stale anon
   * region pointers from kmalloc-uninitialized memory. */
  as->rss_pages = 0;
  as->anon_regions = NULL;
  return as;
}

void vmm_destroy_address_space(struct vmm_address_space *as) {
  if (!as || as == &kernel_as) return;
  as->refcount--;
  if (as->refcount > 0) return;

  /* Phase 7b: drop the demand-paging registry first. The list is
   * just kmalloc-backed nodes; the actual mapped pages (if any)
   * live in the page tables walked below. */
  vmm_clear_anon_regions(as);

  uint64_t *pml4 = as->pml4_virt;
  for (int i = 0; i < 256; i++) {
    if (!(pml4[i] & VMM_PAGE_PRESENT)) continue;
    uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4[i] & ~0xFFFULL);
    for (int j = 0; j < 512; j++) {
      if (!(pdpt[j] & VMM_PAGE_PRESENT)) continue;
      uint64_t *pd = (uint64_t *)(uintptr_t)(pdpt[j] & ~0xFFFULL);
      for (int k = 0; k < 512; k++) {
        if (!(pd[k] & VMM_PAGE_PRESENT)) continue;
        if (pd[k] & VMM_PAGE_HUGE) {
          pmm_free_page(pd[k] & ~0xFFFULL);
          continue;
        }
        uint64_t *pt = (uint64_t *)(uintptr_t)(pd[k] & ~0xFFFULL);
        for (int l = 0; l < 512; l++) {
          if (pt[l] & VMM_PAGE_PRESENT) {
            pmm_free_page(pt[l] & ~0xFFFULL);
          }
        }
        pmm_free_page(pd[k] & ~0xFFFULL);
      }
      pmm_free_page(pdpt[j] & ~0xFFFULL);
    }
    pmm_free_page(pml4[i] & ~0xFFFULL);
  }

  pmm_free_page(as->pml4_phys);
  kfree(as);
}

struct vmm_address_space *vmm_kernel_address_space(void) {
  return &kernel_as;
}

void vmm_switch_address_space(struct vmm_address_space *as) {
  if (!as) return;
  uint64_t current = read_cr3() & ~0xFFFULL;
  if (current != as->pml4_phys) {
    write_cr3(as->pml4_phys);
  }
}

int vmm_map_page(struct vmm_address_space *as, uint64_t virt, uint64_t phys,
                 uint64_t flags) {
  if (!as) return -1;
  uint64_t *pml4 = as->pml4_virt;

  uint32_t pml4_idx = (virt >> 39) & 0x1FF;
  uint32_t pdpt_idx = (virt >> 30) & 0x1FF;
  uint32_t pd_idx   = (virt >> 21) & 0x1FF;
  uint32_t pt_idx   = (virt >> 12) & 0x1FF;

  uint64_t table_flags = VMM_PAGE_PRESENT | VMM_PAGE_WRITE;
  if (flags & VMM_PAGE_USER) table_flags |= VMM_PAGE_USER;

  uint64_t *pdpt = get_or_create_table(pml4, pml4_idx, table_flags);
  if (!pdpt) return -1;

  uint64_t *pd = get_or_create_table(pdpt, pdpt_idx, table_flags);
  if (!pd) return -1;

  uint64_t *pt = get_or_create_table(pd, pd_idx, table_flags);
  if (!pt) return -1;

  /* Phase 7b: per-AS RSS bookkeeping. Only count user mappings; a
   * fresh map of an already-present PTE (re-map) does NOT double
   * count because the PTE was either already user (in which case we
   * are overwriting it; the prior count stays) or kernel (in which
   * case the global counter handled it). The simple invariant kept
   * here is: each transition from "not present" to "present + user"
   * bumps the AS's RSS by 1 page. */
  int was_present = (pt[pt_idx] & VMM_PAGE_PRESENT) != 0;
  pt[pt_idx] = (phys & ~0xFFFULL) | flags | VMM_PAGE_PRESENT;
  invlpg(virt);

  if (flags & VMM_PAGE_USER) {
    vmm_global_stats.user_mapped_pages++;
    if (!was_present) as->rss_pages++;
  } else {
    vmm_global_stats.kernel_mapped_pages++;
  }

  return 0;
}

int vmm_unmap_page(struct vmm_address_space *as, uint64_t virt) {
  if (!as) return -1;
  uint64_t *pml4 = as->pml4_virt;

  uint32_t pml4_idx = (virt >> 39) & 0x1FF;
  uint32_t pdpt_idx = (virt >> 30) & 0x1FF;
  uint32_t pd_idx   = (virt >> 21) & 0x1FF;
  uint32_t pt_idx   = (virt >> 12) & 0x1FF;

  if (!(pml4[pml4_idx] & VMM_PAGE_PRESENT)) return -1;
  uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4[pml4_idx] & ~0xFFFULL);
  if (!(pdpt[pdpt_idx] & VMM_PAGE_PRESENT)) return -1;
  uint64_t *pd = (uint64_t *)(uintptr_t)(pdpt[pdpt_idx] & ~0xFFFULL);
  if (!(pd[pd_idx] & VMM_PAGE_PRESENT)) return -1;
  uint64_t *pt = (uint64_t *)(uintptr_t)(pd[pd_idx] & ~0xFFFULL);

  /* Phase 7b: read the PTE BEFORE clearing so we can decide whether
   * to decrement the per-AS RSS counter. Only user pages contribute
   * to RSS; kernel pages are tracked globally elsewhere. */
  uint64_t old_pte = pt[pt_idx];
  pt[pt_idx] = 0;
  invlpg(virt);
  if ((old_pte & VMM_PAGE_PRESENT) && (old_pte & VMM_PAGE_USER)) {
    if (as->rss_pages > 0) as->rss_pages--;
  }
  return 0;
}

int vmm_map_range(struct vmm_address_space *as, uint64_t virt, uint64_t phys,
                  size_t count, uint64_t flags) {
  for (size_t i = 0; i < count; i++) {
    int r = vmm_map_page(as, virt + i * VMM_PAGE_SIZE,
                         phys + i * VMM_PAGE_SIZE, flags);
    if (r != 0) return r;
  }
  return 0;
}

int vmm_unmap_range(struct vmm_address_space *as, uint64_t virt, size_t count) {
  for (size_t i = 0; i < count; i++) {
    vmm_unmap_page(as, virt + i * VMM_PAGE_SIZE);
  }
  return 0;
}

uint64_t vmm_virt_to_phys(struct vmm_address_space *as, uint64_t virt) {
  if (!as) return 0;
  uint64_t *pml4 = as->pml4_virt;
  uint32_t pml4_idx = (virt >> 39) & 0x1FF;
  uint32_t pdpt_idx = (virt >> 30) & 0x1FF;
  uint32_t pd_idx   = (virt >> 21) & 0x1FF;
  uint32_t pt_idx   = (virt >> 12) & 0x1FF;

  if (!(pml4[pml4_idx] & VMM_PAGE_PRESENT)) return 0;
  uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4[pml4_idx] & ~0xFFFULL);
  if (!(pdpt[pdpt_idx] & VMM_PAGE_PRESENT)) return 0;
  uint64_t *pd = (uint64_t *)(uintptr_t)(pdpt[pdpt_idx] & ~0xFFFULL);
  if (!(pd[pd_idx] & VMM_PAGE_PRESENT)) return 0;
  uint64_t *pt = (uint64_t *)(uintptr_t)(pd[pd_idx] & ~0xFFFULL);
  if (!(pt[pt_idx] & VMM_PAGE_PRESENT)) return 0;

  return (pt[pt_idx] & ~0xFFFULL) | (virt & 0xFFF);
}

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
int vmm_handle_page_fault(uint64_t fault_addr, uint64_t error_code) {
  (void)error_code;
  vmm_global_stats.page_faults++;

  struct vmm_address_space *as = vmm_current_address_space();
  if (!as) return -1;

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

void vmm_stats_get(struct vmm_stats *out) {
  if (out) *out = vmm_global_stats;
}
