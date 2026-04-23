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

#endif /* MEMORY_PMM_H */
