#include "memory/vmm.h"
#include "memory/vmm_cow.h"
#include "memory/pmm.h"
#include "memory/kmem.h"
#include <stddef.h>
#include <stdint.h>

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

/* M4 phase 7c: copy-on-write clone of an address space.
 *
 * Walks the user half (PML4 indices 0..255) of `src` and rebuilds an
 * isomorphic structure in a fresh AS. For each present user-mapped
 * 4 KiB PTE:
 *
 *   1. Bump the underlying frame's refcount (the dst AS becomes a
 *      new sharer).
 *   2. If the src PTE is writable, flip both src and dst PTEs to
 *      VMM_PAGE_COW + cleared VMM_PAGE_WRITE so the next write from
 *      either side faults into vmm_handle_page_fault.
 *   3. If the src PTE was already read-only (text segment, mprotect
 *      RO), copy it as-is. The frame is still shared but no flag
 *      flip is needed; the refcount bump is enough so destroy_as
 *      can free the frame on the LAST sharer.
 *
 * Huge-page (HUGE bit) PTEs are not yet supported by the CoW path
 * because the existing kernel never installs them in user space; we
 * defensively skip them and leave them mapped only in `src` (which
 * keeps the contract that the caller's address space is unchanged
 * for huge mappings).
 *
 * Returns NULL on allocation failure. The partial work is cleaned
 * up via `vmm_destroy_address_space` so the dst's refcount drops
 * back to 0 and any frame refcount bumps are reverted by the
 * destroy walker (which calls `pmm_frame_refcount_dec`). */
static int clone_pt_into(uint64_t *dst_pdpt_or_pd_or_pt,
                         uint64_t *src_pdpt_or_pd_or_pt,
                         int level);

struct vmm_address_space *vmm_clone_address_space(
    const struct vmm_address_space *src) {
  if (!src) return NULL;
  struct vmm_address_space *dst = vmm_create_address_space();
  if (!dst) return NULL;

  /* Walk the lower half (user space) only. Kernel half was already
   * shared by vmm_create_address_space. */
  uint64_t *src_pml4 = src->pml4_virt;
  uint64_t *dst_pml4 = dst->pml4_virt;
  for (int i = 0; i < 256; ++i) {
    uint64_t src_pml4e = src_pml4[i];
    if (!(src_pml4e & VMM_PAGE_PRESENT)) continue;

    uint64_t pdpt_phys = pmm_alloc_page();
    if (!pdpt_phys) {
      vmm_destroy_address_space(dst);
      return NULL;
    }
    uint64_t *dst_pdpt = (uint64_t *)(uintptr_t)pdpt_phys;
    for (int z = 0; z < 512; ++z) dst_pdpt[z] = 0;
    dst_pml4[i] = pdpt_phys | (src_pml4e & 0xFFFULL);

    uint64_t *src_pdpt = (uint64_t *)(uintptr_t)(src_pml4e & ~0xFFFULL);
    if (clone_pt_into(dst_pdpt, src_pdpt, 1) != 0) {
      vmm_destroy_address_space(dst);
      return NULL;
    }
  }

  /* Mirror RSS at the end (each user PTE we shared bumped one
   * resident page in dst as well). The simplest correct accounting
   * is to copy src's count straight across; the destroy path will
   * decrement-per-PTE walking the page tables. */
  dst->rss_pages = src->rss_pages;
  return dst;
}

/* Recursive helper that walks PDPT -> PD -> PT, mirroring `src` into
 * `dst` and applying the CoW flag flip at the leaf (4 KiB) layer.
 *
 * level encoding:
 *   1 = PDPT (each entry points at a PD)
 *   2 = PD   (each entry points at a PT, or is a 2 MiB HUGE leaf)
 *   3 = PT   (each entry is a 4 KiB leaf)
 *
 * Returns 0 on success, -1 on allocation failure. The destroy
 * walker in vmm_destroy_address_space cleans up partial work. */
static int clone_pt_into(uint64_t *dst, uint64_t *src, int level) {
  for (int i = 0; i < 512; ++i) {
    uint64_t s = src[i];
    if (!(s & VMM_PAGE_PRESENT)) continue;

    if (level < 3 && !(s & VMM_PAGE_HUGE)) {
      /* Intermediate level: allocate a fresh table and recurse. */
      uint64_t child_phys = pmm_alloc_page();
      if (!child_phys) return -1;
      uint64_t *child = (uint64_t *)(uintptr_t)child_phys;
      for (int z = 0; z < 512; ++z) child[z] = 0;
      dst[i] = child_phys | (s & 0xFFFULL);

      uint64_t *src_child = (uint64_t *)(uintptr_t)(s & ~0xFFFULL);
      if (clone_pt_into(child, src_child, level + 1) != 0) {
        return -1;
      }
      continue;
    }

    /* Leaf entry. The 4 KiB PT layer is the common case. The 2 MiB
     * HUGE leaf in the PD layer is conservatively duplicated as-is
     * with no CoW flag flip; the kernel does not currently install
     * user huge pages so this branch is mostly defensive. */
    uint64_t frame_phys = s & ~0xFFFULL;
    pmm_frame_refcount_inc(frame_phys);

    if ((s & VMM_PAGE_USER) && (s & VMM_PAGE_WRITE) && !(s & VMM_PAGE_HUGE)) {
      /* Writable 4 KiB user mapping - this is a CoW candidate. Flip
       * BOTH the destination AND the source so a write from either
       * AS faults into the recovery path. The src cast away the
       * const-ness of the AS via the parent function; this is the
       * intentional in-place mutation that makes CoW symmetric. */
      uint64_t cow_pte = (s | VMM_PAGE_COW) & ~VMM_PAGE_WRITE;
      dst[i] = cow_pte;
      src[i] = cow_pte;
    } else {
      /* RO mapping (text), or huge leaf, or kernel/non-user. Copy as
       * is; refcount bump above is enough to keep the frame alive
       * for the dst until destroy. */
      dst[i] = s;
    }
  }
  return 0;
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
          /* HUGE leaves at PD level are not refcounted today (the
           * cloner skips them; phase 7c only CoWs 4 KiB PTs). Free
           * straight to the PMM. */
          pmm_free_page(pd[k] & ~0xFFFULL);
          continue;
        }
        uint64_t *pt = (uint64_t *)(uintptr_t)(pd[k] & ~0xFFFULL);
        for (int l = 0; l < 512; l++) {
          if (pt[l] & VMM_PAGE_PRESENT) {
            /* M4 phase 7c: a frame may be shared via CoW with one or
             * more sibling AS. Decrement the refcount first; only
             * release the physical frame to the PMM when we are the
             * last sharer. Frames that were never refcount-touched
             * (single-AS user mappings, demand-paged anon pages)
             * report 0 from pmm_frame_refcount_dec and we free them
             * directly, preserving the pre-7c semantics. */
            uint64_t leaf_phys = pt[l] & ~0xFFFULL;
            uint16_t pre = pmm_frame_refcount_get(leaf_phys);
            if (pre == 0u) {
              pmm_free_page(leaf_phys);
            } else {
              uint16_t after = pmm_frame_refcount_dec(leaf_phys);
              if (after == 0u) pmm_free_page(leaf_phys);
            }
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
  uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4[pml4_idx] & ~0xFFFULL);
  if (!(pdpt[pdpt_idx] & VMM_PAGE_PRESENT)) return NULL;
  uint64_t *pd = (uint64_t *)(uintptr_t)(pdpt[pdpt_idx] & ~0xFFFULL);
  if (!(pd[pd_idx] & VMM_PAGE_PRESENT)) return NULL;
  if (pd[pd_idx] & VMM_PAGE_HUGE) return NULL; /* phase 7c: skip 2 MiB */
  uint64_t *pt = (uint64_t *)(uintptr_t)(pd[pd_idx] & ~0xFFFULL);
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

  uint64_t old_phys = pte & ~0xFFFULL;

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

void vmm_stats_get(struct vmm_stats *out) {
  if (out) *out = vmm_global_stats;
}
