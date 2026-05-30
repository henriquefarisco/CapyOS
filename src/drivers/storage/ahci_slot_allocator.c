/* AHCI slot allocator implementation — pure, host-testable. */

#include "drivers/storage/ahci_slot_allocator.h"

#include <stddef.h>

void ahci_slot_allocator_init(struct ahci_slot_allocator *alloc,
                              uint8_t slot_count) {
    if (!alloc) {
        return;
    }
    if (slot_count == 0u || slot_count > AHCI_MAX_SLOTS) {
        /* Defensive: leave allocator unusable rather than guess a
         * default. Callers that observe -1 from `ahci_slot_alloc`
         * will surface the misconfig via klog (the runtime path)
         * or via test assertions. */
        alloc->slot_count = 0u;
        alloc->free_mask = 0u;
        return;
    }
    alloc->slot_count = slot_count;
    /* Mark slots [0, slot_count) free. Bits above are kept zero so
     * the allocator never hands them out. */
    if (slot_count == AHCI_MAX_SLOTS) {
        alloc->free_mask = 0xFFFFFFFFu;
    } else {
        alloc->free_mask = (1u << slot_count) - 1u;
    }
}

int ahci_slot_alloc(struct ahci_slot_allocator *alloc) {
    uint32_t mask;
    int slot;
    if (!alloc || alloc->slot_count == 0u || alloc->free_mask == 0u) {
        return -1;
    }
    mask = alloc->free_mask;
    /* Lowest set bit. Equivalent to __builtin_ctz but written long-
     * hand to keep the TU portable across compilers used by the
     * host runner and the cross toolchain. */
    slot = 0;
    while ((mask & 1u) == 0u) {
        mask >>= 1;
        slot++;
    }
    alloc->free_mask &= ~(1u << slot);
    return slot;
}

int ahci_slot_release(struct ahci_slot_allocator *alloc, int slot) {
    uint32_t bit;
    if (!alloc || slot < 0 || slot >= (int)alloc->slot_count) {
        return -1;
    }
    bit = 1u << slot;
    /* Double-release is a programmer error (slot was already free).
     * Surface it so the caller can fix the lifecycle instead of
     * leaking a phantom completion. */
    if (alloc->free_mask & bit) {
        return -1;
    }
    alloc->free_mask |= bit;
    return 0;
}

uint32_t ahci_slot_inflight_mask(const struct ahci_slot_allocator *alloc) {
    if (!alloc || alloc->slot_count == 0u) {
        return 0u;
    }
    /* Inflight = slots in [0, slot_count) that are NOT in the free
     * mask. Bits at positions >= slot_count are explicitly cleared
     * so the result is a clean bitmask that downstream helpers
     * (`ahci_dispatch_completed_slots`) can consume directly. */
    if (alloc->slot_count == AHCI_MAX_SLOTS) {
        return ~alloc->free_mask;
    }
    return ((1u << alloc->slot_count) - 1u) & ~alloc->free_mask;
}

uint8_t ahci_slot_inflight_count(const struct ahci_slot_allocator *alloc) {
    /* Reuse the new mask accessor + Brian Kernighan popcount. The
     * old inline computation was bitwise-identical; this version
     * just shares the masking logic with `ahci_slot_inflight_mask`
     * so a future fix to one fixes both. */
    uint32_t inflight_mask = ahci_slot_inflight_mask(alloc);
    uint8_t count = 0;
    while (inflight_mask) {
        inflight_mask &= inflight_mask - 1u; /* clear lowest set bit */
        count++;
    }
    return count;
}

int ahci_slot_is_free(const struct ahci_slot_allocator *alloc, int slot) {
    if (!alloc || slot < 0 || slot >= (int)alloc->slot_count) {
        return 0;
    }
    return (alloc->free_mask & (1u << slot)) ? 1 : 0;
}

void ahci_slot_allocator_reset(struct ahci_slot_allocator *alloc) {
    if (!alloc || alloc->slot_count == 0u) {
        return;
    }
    if (alloc->slot_count == AHCI_MAX_SLOTS) {
        alloc->free_mask = 0xFFFFFFFFu;
    } else {
        alloc->free_mask = (1u << alloc->slot_count) - 1u;
    }
}
