#ifndef MEMORY_PMM_H
#define MEMORY_PMM_H

#include <stdint.h>
#include <stddef.h>

#define PMM_PAGE_SIZE 4096
#define PMM_MAX_REGIONS 32

struct pmm_region {
  uint64_t base;
  uint64_t length;
  uint32_t type;
};

struct pmm_stats {
  uint64_t total_pages;
  uint64_t free_pages;
  uint64_t used_pages;
  uint64_t reserved_pages;
  uint64_t total_bytes;
  uint64_t free_bytes;
};

void pmm_init(const struct pmm_region *regions, size_t count);
uint64_t pmm_alloc_page(void);
void pmm_free_page(uint64_t phys_addr);
uint64_t pmm_alloc_pages(size_t count);
void pmm_free_pages(uint64_t phys_addr, size_t count);
void pmm_reserve_range(uint64_t start, uint64_t length);
void pmm_stats_get(struct pmm_stats *out);
int pmm_is_free(uint64_t phys_addr);
int pmm_low_memory(void);
void pmm_set_reclaim_callback(void (*cb)(void));
uint64_t pmm_alloc_page_reclaim(void);

/* Phase 7c: per-frame reference counters used by copy-on-write.
 *
 * `vmm_clone_address_space` needs to know how many address spaces
 * share a given physical page so the page-fault handler can decide
 * between "we are the last sharer, flip RW back" and "another AS
 * still maps this page, allocate a copy". The counter is incremented
 * each time a frame becomes RO-shared via clone, and decremented as
 * each sharer either writes (and gets its own copy) or unmaps.
 *
 * The default count for a freshly-allocated frame is 1 (the
 * allocator hands ownership to a single caller). Frames that were
 * never refcount-touched return 0 from `pmm_frame_refcount_get` so
 * code paths that ignore CoW (kernel mappings, single-AS user
 * mappings) continue to work without bookkeeping overhead.
 *
 * Backed by a fixed-size uint16_t array indexed by PFN. The table
 * is sized to cover the same upper bound as the PMM bitmap
 * (PMM_BITMAP_SIZE * 8 frames). Out-of-range frames are silently
 * ignored to make the helpers safe to call without bounds checks
 * at the caller (notably from page-table walkers).
 *
 * The table lives in src/memory/pmm_refcount.c so it can be linked
 * into the host unit-test binary alongside vmm_regions.c without
 * pulling in pmm.c (which has no inline asm but does have global
 * state we do not want to share with the tests). */
#define PMM_BITMAP_SIZE_PAGES (64u * 1024u)
#define PMM_REFCOUNT_MAX_PAGES (PMM_BITMAP_SIZE_PAGES)

void pmm_frame_refcount_init(void);
void pmm_frame_refcount_inc(uint64_t phys_addr);
/* Decrement and return the new value. Returns 0 if the table is at
 * 0 already (idempotent / safe to call on never-refcounted frames). */
uint16_t pmm_frame_refcount_dec(uint64_t phys_addr);
uint16_t pmm_frame_refcount_get(uint64_t phys_addr);

#endif /* MEMORY_PMM_H */
