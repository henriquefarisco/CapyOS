/*
 * Anonymous-region registry for the demand-paging seam (M4 phase 7b).
 *
 * This file is intentionally separate from src/memory/vmm.c so it can
 * be linked into the host unit-test binary: vmm.c uses x86_64 inline
 * asm (movq cr3, ..., invlpg) that does not assemble on non-x86 hosts
 * (notably Apple Silicon arm64). The registry, by contrast, is just
 * linked-list manipulation backed by kmalloc/kfree, so it builds and
 * runs cleanly on the host alongside the other host-side test files.
 *
 * Public API lives in include/memory/vmm.h. The functions defined
 * here are:
 *
 *   - vmm_register_anon_region : append a non-overlapping region
 *   - vmm_clear_anon_regions   : free the full list
 *   - vmm_anon_region_find     : look up the region containing `virt`
 *   - vmm_address_space_rss    : read the per-AS RSS counter
 *
 * The actual page-fault servicing (allocate physical page, zero, map)
 * lives in src/memory/vmm.c::vmm_handle_page_fault, which calls the
 * find/rss helpers exposed here. Splitting this way keeps the
 * arch-dependent parts (cr3, invlpg, PTE walking) in vmm.c while the
 * host-testable parts (registry, RSS counter access) live here.
 */
#include "kernel/process.h"
#include "memory/kmem.h"
#include "memory/vmm.h"

#include <stddef.h>
#include <stdint.h>

/* Half-open range overlap test: [a_start, a_end) vs [b_start, b_end).
 * Returns non-zero if they share at least one byte. The registry uses
 * this to reject overlapping registrations. */
static int ranges_overlap(uint64_t a_start, uint64_t a_end,
                          uint64_t b_start, uint64_t b_end) {
    return a_start < b_end && b_start < a_end;
}

int vmm_register_anon_region(struct vmm_address_space *as, uint64_t start,
                             size_t page_count, uint64_t flags) {
    if (!as) return -1;
    if (page_count == 0) return -1;

    uint64_t end = start + (uint64_t)page_count * (uint64_t)VMM_PAGE_SIZE;
    if (end <= start) return -1;  /* overflow guard */

    /* Reject overlap with any existing region. The list is short in
     * practice (a process has a handful of regions: stack expansion,
     * heap, optional mmap'd anon mappings) so a linear scan is fine. */
    for (struct vmm_anon_region *r = as->anon_regions; r; r = r->next) {
        if (ranges_overlap(start, end, r->start, r->end)) {
            return -1;
        }
    }

    struct vmm_anon_region *node =
        (struct vmm_anon_region *)kmalloc(sizeof(*node));
    if (!node) return -1;

    node->start = start;
    node->end = end;
    node->flags = flags;
    /* Head insertion: O(1) and the order is documented as not
     * contractual. Tests must not depend on it. */
    node->next = as->anon_regions;
    as->anon_regions = node;
    return 0;
}

void vmm_clear_anon_regions(struct vmm_address_space *as) {
    if (!as) return;
    struct vmm_anon_region *r = as->anon_regions;
    while (r) {
        struct vmm_anon_region *next = r->next;
        kfree(r);
        r = next;
    }
    as->anon_regions = NULL;
}

struct vmm_anon_region *vmm_anon_region_find(
    const struct vmm_address_space *as, uint64_t virt) {
    if (!as) return NULL;
    for (struct vmm_anon_region *r = as->anon_regions; r; r = r->next) {
        if (virt >= r->start && virt < r->end) {
            return r;
        }
    }
    return NULL;
}

uint64_t vmm_address_space_rss(const struct vmm_address_space *as) {
    if (!as) return 0;
    return as->rss_pages;
}

/* `vmm_current_address_space` lives here (not in vmm.c) because it is
 * pure C with no inline asm and the host unit tests need to link
 * against it without dragging the cr3/invlpg helpers from vmm.c. */
struct vmm_address_space *vmm_current_address_space(void) {
    struct process *p = process_current();
    if (!p) return NULL;
    return p->address_space;
}
