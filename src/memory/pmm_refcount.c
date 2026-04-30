/*
 * Per-frame reference counter table for copy-on-write (M4 phase 7c).
 *
 * Lives in its own translation unit so that the host unit-test binary
 * can link against it without pulling pmm.c (and its global allocator
 * state). The mechanics are simple: a fixed-size uint16_t array
 * indexed by Page Frame Number (PFN). Out-of-range PFNs are silently
 * ignored, matching the public-API contract documented in pmm.h.
 *
 * The 16-bit width is sized so that "up to 65535 sharers per frame"
 * is more headroom than CapyOS will ever exercise (we cap process
 * count well below that). If a refcount ever pegged at 0xFFFF the
 * table would saturate (the helpers do NOT wrap on overflow); future
 * phases that introduce CoW-of-CoW chains can extend this to 32 bits
 * without touching the public API.
 */
#include "memory/pmm.h"

#include <stddef.h>
#include <stdint.h>

#define PMM_REFCOUNT_PAGE_SIZE 4096u

static uint16_t g_pmm_refcounts[PMM_REFCOUNT_MAX_PAGES];

static uint64_t pfn_from_phys(uint64_t phys_addr) {
    return phys_addr / (uint64_t)PMM_REFCOUNT_PAGE_SIZE;
}

void pmm_frame_refcount_init(void) {
    for (size_t i = 0; i < PMM_REFCOUNT_MAX_PAGES; ++i) {
        g_pmm_refcounts[i] = 0u;
    }
}

void pmm_frame_refcount_inc(uint64_t phys_addr) {
    uint64_t pfn = pfn_from_phys(phys_addr);
    if (pfn >= PMM_REFCOUNT_MAX_PAGES) return;
    if (g_pmm_refcounts[pfn] == 0xFFFFu) return; /* saturate, no wrap */
    g_pmm_refcounts[pfn]++;
}

uint16_t pmm_frame_refcount_dec(uint64_t phys_addr) {
    uint64_t pfn = pfn_from_phys(phys_addr);
    if (pfn >= PMM_REFCOUNT_MAX_PAGES) return 0u;
    if (g_pmm_refcounts[pfn] == 0u) return 0u;
    g_pmm_refcounts[pfn]--;
    return g_pmm_refcounts[pfn];
}

uint16_t pmm_frame_refcount_get(uint64_t phys_addr) {
    uint64_t pfn = pfn_from_phys(phys_addr);
    if (pfn >= PMM_REFCOUNT_MAX_PAGES) return 0u;
    return g_pmm_refcounts[pfn];
}
