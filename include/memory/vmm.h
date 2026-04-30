#ifndef MEMORY_VMM_H
#define MEMORY_VMM_H

#include <stdint.h>
#include <stddef.h>

#define VMM_PAGE_SIZE 4096
#define VMM_PAGE_PRESENT  (1ULL << 0)
#define VMM_PAGE_WRITE    (1ULL << 1)
#define VMM_PAGE_USER     (1ULL << 2)
#define VMM_PAGE_PWT      (1ULL << 3)
#define VMM_PAGE_PCD      (1ULL << 4)
#define VMM_PAGE_ACCESSED (1ULL << 5)
#define VMM_PAGE_DIRTY    (1ULL << 6)
#define VMM_PAGE_HUGE     (1ULL << 7)
#define VMM_PAGE_GLOBAL   (1ULL << 8)
#define VMM_PAGE_NX       (1ULL << 63)

#define VMM_KERNEL_BASE 0xFFFF800000000000ULL
#define VMM_USER_BASE   0x0000000000400000ULL
#define VMM_USER_TOP    0x00007FFFFFFFF000ULL
#define VMM_USER_STACK  0x00007FFFFFF00000ULL

/* Phase 7b: anonymous (zero-fill on demand) region descriptor.
 *
 * An anonymous region is a half-open virtual address interval
 * `[start, end)` that is NOT eagerly mapped. When the user (or a
 * future kernel-side mmap consumer) faults on a page inside the
 * interval, `vmm_handle_page_fault` allocates a fresh physical page,
 * zero-fills it, and installs a PTE with `flags`. The page becomes
 * part of the address space's RSS.
 *
 * Regions are page-aligned by contract (the public registration API
 * rounds outwards) and do not overlap each other. The list is
 * singly-linked through `next`; insertion order is implementation
 * detail and tests must not depend on it. */
struct vmm_anon_region {
  uint64_t start;       /* page-aligned, inclusive       */
  uint64_t end;         /* page-aligned, exclusive       */
  uint64_t flags;       /* VMM_PAGE_USER, _WRITE, _NX... */
  struct vmm_anon_region *next;
};

struct vmm_address_space {
  uint64_t pml4_phys;
  uint64_t *pml4_virt;
  uint32_t refcount;
  /* Phase 7b: per-AS resident-set counter. Incremented when a user
   * page is mapped via vmm_map_page() / demand-paged via
   * vmm_handle_page_fault(); decremented when a user page is
   * unmapped via vmm_unmap_page(). Read by `vmm_address_space_rss`
   * which feeds `process_stats.rss_pages` for observability. */
  uint64_t rss_pages;
  /* Phase 7b: head of the demand-paging anonymous region list. NULL
   * for an address space with no registered regions (the default).
   * Owned by the AS; freed by `vmm_destroy_address_space`. */
  struct vmm_anon_region *anon_regions;
};

struct vmm_mapping {
  uint64_t virt_start;
  uint64_t phys_start;
  size_t page_count;
  uint64_t flags;
  struct vmm_mapping *next;
};

struct vmm_stats {
  uint64_t kernel_mapped_pages;
  uint64_t user_mapped_pages;
  uint64_t page_faults;
  uint64_t cow_faults;
};

void vmm_init(void);
struct vmm_address_space *vmm_create_address_space(void);
void vmm_destroy_address_space(struct vmm_address_space *as);
struct vmm_address_space *vmm_kernel_address_space(void);
void vmm_switch_address_space(struct vmm_address_space *as);
int vmm_map_page(struct vmm_address_space *as, uint64_t virt, uint64_t phys,
                 uint64_t flags);
int vmm_unmap_page(struct vmm_address_space *as, uint64_t virt);
int vmm_map_range(struct vmm_address_space *as, uint64_t virt, uint64_t phys,
                  size_t count, uint64_t flags);
int vmm_unmap_range(struct vmm_address_space *as, uint64_t virt, size_t count);
uint64_t vmm_virt_to_phys(struct vmm_address_space *as, uint64_t virt);
int vmm_handle_page_fault(uint64_t fault_addr, uint64_t error_code);
void vmm_stats_get(struct vmm_stats *out);

/* Phase 7b: anonymous-region registry API.
 *
 * `vmm_register_anon_region(as, start, page_count, flags)` reserves a
 * page-aligned virtual range `[start, start + page_count*PAGE_SIZE)`
 * inside `as` for demand paging. `start` and `page_count` are NOT
 * rounded by the helper; the caller is responsible for alignment
 * (callers should typically pass `VMM_PAGE_SIZE`-aligned values).
 * Returns 0 on success, -1 if `as` is NULL, the range is empty,
 * the kmalloc fails, or the new range overlaps an existing region.
 *
 * `vmm_clear_anon_regions(as)` frees the entire region list. Called
 * from `vmm_destroy_address_space`; safe to call directly when an AS
 * needs to drop its demand-paging map without being destroyed (e.g.
 * before re-registering a different layout from `process_exec`).
 *
 * `vmm_address_space_rss(as)` returns the per-AS resident-set count
 * in 4 KiB pages. Returns 0 for a NULL `as`.
 *
 * `vmm_current_address_space()` returns the address space of
 * `process_current()` if any, else NULL. Used by
 * `vmm_handle_page_fault` to look up the faulting AS without taking
 * a parameter (the page-fault entry path has access to cr2 and the
 * error code only).
 *
 * `vmm_anon_region_find(as, virt)` returns a pointer to the region
 * containing `virt` (a byte address; not required to be page-aligned)
 * or NULL if no region matches. Exposed primarily for tests; the
 * fault handler uses it internally. The pointer becomes invalid the
 * moment `vmm_clear_anon_regions` runs on the same AS. */
int vmm_register_anon_region(struct vmm_address_space *as, uint64_t start,
                             size_t page_count, uint64_t flags);
void vmm_clear_anon_regions(struct vmm_address_space *as);
uint64_t vmm_address_space_rss(const struct vmm_address_space *as);
struct vmm_address_space *vmm_current_address_space(void);
struct vmm_anon_region *vmm_anon_region_find(
    const struct vmm_address_space *as, uint64_t virt);

#endif /* MEMORY_VMM_H */
