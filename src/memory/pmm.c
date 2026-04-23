#include "memory/pmm.h"
#include <stddef.h>

#define PMM_BITMAP_SIZE (64 * 1024 / 8)

static uint8_t pmm_bitmap[PMM_BITMAP_SIZE];
static uint64_t pmm_total_pages = 0;
static uint64_t pmm_free_count = 0;
static uint64_t pmm_used_pages = 0;
static uint64_t pmm_reserved_pages = 0;
static uint64_t pmm_base_addr = 0;

static inline void bitmap_set(uint64_t page) {
  uint64_t idx = page / 8;
  uint8_t bit = (uint8_t)(1 << (page % 8));
  if (idx < PMM_BITMAP_SIZE) pmm_bitmap[idx] |= bit;
}

static inline void bitmap_clear(uint64_t page) {
  uint64_t idx = page / 8;
  uint8_t bit = (uint8_t)(1 << (page % 8));
  if (idx < PMM_BITMAP_SIZE) pmm_bitmap[idx] &= ~bit;
}

static inline int bitmap_test(uint64_t page) {
  uint64_t idx = page / 8;
  uint8_t bit = (uint8_t)(1 << (page % 8));
  if (idx >= PMM_BITMAP_SIZE) return 1;
  return (pmm_bitmap[idx] & bit) ? 1 : 0;
}

void pmm_init(const struct pmm_region *regions, size_t count) {
  for (size_t i = 0; i < PMM_BITMAP_SIZE; i++) pmm_bitmap[i] = 0xFF;
  pmm_total_pages = 0;
  pmm_free_count = 0;
  pmm_used_pages = 0;
  pmm_reserved_pages = 0;
  pmm_base_addr = 0;

  uint64_t max_addr = 0;
  for (size_t i = 0; i < count; i++) {
    uint64_t end = regions[i].base + regions[i].length;
    if (end > max_addr) max_addr = end;
    if (pmm_base_addr == 0 || regions[i].base < pmm_base_addr)
      pmm_base_addr = regions[i].base;
  }

  pmm_total_pages = max_addr / PMM_PAGE_SIZE;
  if (pmm_total_pages > PMM_BITMAP_SIZE * 8)
    pmm_total_pages = PMM_BITMAP_SIZE * 8;

  for (size_t i = 0; i < count; i++) {
    if (regions[i].type != 1) continue;
    uint64_t start = (regions[i].base + PMM_PAGE_SIZE - 1) & ~(PMM_PAGE_SIZE - 1);
    uint64_t end = (regions[i].base + regions[i].length) & ~(PMM_PAGE_SIZE - 1);
    for (uint64_t addr = start; addr < end; addr += PMM_PAGE_SIZE) {
      uint64_t page = addr / PMM_PAGE_SIZE;
      if (page < pmm_total_pages) {
        bitmap_clear(page);
        pmm_free_count++;
      }
    }
  }

  pmm_reserve_range(0, PMM_PAGE_SIZE);
  pmm_reserve_range(PMM_PAGE_SIZE, PMM_PAGE_SIZE * 256);
}

uint64_t pmm_alloc_page(void) {
  for (uint64_t i = 0; i < pmm_total_pages; i++) {
    if (!bitmap_test(i)) {
      bitmap_set(i);
      pmm_free_count--;
      pmm_used_pages++;
      return i * PMM_PAGE_SIZE;
    }
  }
  return 0;
}

void pmm_free_page(uint64_t phys_addr) {
  uint64_t page = phys_addr / PMM_PAGE_SIZE;
  if (page >= pmm_total_pages) return;
  if (bitmap_test(page)) {
    bitmap_clear(page);
    pmm_free_count++;
    if (pmm_used_pages > 0) pmm_used_pages--;
  }
}

uint64_t pmm_alloc_pages(size_t count) {
  if (count == 0) return 0;
  for (uint64_t i = 0; i <= pmm_total_pages - count; i++) {
    int found = 1;
    for (size_t j = 0; j < count; j++) {
      if (bitmap_test(i + j)) { found = 0; i += j; break; }
    }
    if (found) {
      for (size_t j = 0; j < count; j++) {
        bitmap_set(i + j);
      }
      pmm_free_count -= count;
      pmm_used_pages += count;
      return i * PMM_PAGE_SIZE;
    }
  }
  return 0;
}

void pmm_free_pages(uint64_t phys_addr, size_t count) {
  uint64_t page = phys_addr / PMM_PAGE_SIZE;
  for (size_t i = 0; i < count; i++) {
    if (page + i < pmm_total_pages && bitmap_test(page + i)) {
      bitmap_clear(page + i);
      pmm_free_count++;
      if (pmm_used_pages > 0) pmm_used_pages--;
    }
  }
}

void pmm_reserve_range(uint64_t start, uint64_t length) {
  uint64_t page_start = start / PMM_PAGE_SIZE;
  uint64_t page_end = (start + length + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
  for (uint64_t p = page_start; p < page_end && p < pmm_total_pages; p++) {
    if (!bitmap_test(p)) {
      bitmap_set(p);
      if (pmm_free_count > 0) pmm_free_count--;
      pmm_reserved_pages++;
    }
  }
}

void pmm_stats_get(struct pmm_stats *out) {
  if (!out) return;
  out->total_pages = pmm_total_pages;
  out->free_pages = pmm_free_count;
  out->used_pages = pmm_used_pages;
  out->reserved_pages = pmm_reserved_pages;
  out->total_bytes = pmm_total_pages * PMM_PAGE_SIZE;
  out->free_bytes = pmm_free_count * PMM_PAGE_SIZE;
}

int pmm_is_free(uint64_t phys_addr) {
  uint64_t page = phys_addr / PMM_PAGE_SIZE;
  if (page >= pmm_total_pages) return 0;
  return bitmap_test(page) ? 0 : 1;
}

int pmm_low_memory(void) {
  uint64_t threshold = pmm_total_pages / 16;
  if (threshold < 32) threshold = 32;
  return pmm_free_count < threshold;
}

static void (*pmm_reclaim_cb)(void) = NULL;

void pmm_set_reclaim_callback(void (*cb)(void)) {
  pmm_reclaim_cb = cb;
}

uint64_t pmm_alloc_page_reclaim(void) {
  uint64_t page = pmm_alloc_page();
  if (page) return page;
  if (pmm_reclaim_cb) {
    pmm_reclaim_cb();
    page = pmm_alloc_page();
  }
  return page;
}
