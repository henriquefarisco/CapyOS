/* AHCI command-slot allocator (Slice 3E.3).
 *
 * AHCI controllers expose up to 32 command slots per port; the
 * runtime today hard-codes slot 0 (`AHCI_CMD_SLOT = 0`), which
 * serialises every command on the host side regardless of what
 * the controller is capable of. This module introduces a pure
 * bitmap allocator that:
 *   - tracks which slots (0..NCS-1) are currently inflight;
 *   - hands out the lowest-numbered free slot;
 *   - releases a slot on completion or error;
 *   - reports inflight count for diagnostics.
 *
 * Multiple concurrent dispatch will be plumbed in a later slice
 * (after the kernel scheduler exposes async clients in Etapa 4);
 * for now the runtime still spin-waits on one slot at a time, but
 * uses the allocator interface so the infrastructure is ready.
 *
 * The allocator is pure: no MMIO, no klog, no kmalloc. Host tests
 * exercise it directly.
 */
#ifndef DRIVERS_STORAGE_AHCI_SLOT_ALLOCATOR_H
#define DRIVERS_STORAGE_AHCI_SLOT_ALLOCATOR_H

#include <stdint.h>

/* AHCI 1.3.1 §3.1.1 CAP.NCS: maximum 32 command slots per port. */
#define AHCI_MAX_SLOTS 32

struct ahci_slot_allocator {
    /* Bitmap of free slots; bit `i` set means slot `i` is available.
     * Slots beyond `slot_count` are always treated as in-use. */
    uint32_t free_mask;
    /* Number of slots this allocator manages, in [1, AHCI_MAX_SLOTS].
     * Comes from the controller's CAP.NCS at init time. */
    uint8_t slot_count;
};

/* Initialise the allocator for a port that exposes `slot_count`
 * slots. Behaviour for invalid `slot_count` (0 or > 32): the
 * allocator is left in a state where every allocate returns -1. */
void ahci_slot_allocator_init(struct ahci_slot_allocator *alloc,
                              uint8_t slot_count);

/* Allocate the lowest-numbered free slot. Returns the slot index
 * in [0, slot_count) on success, or -1 if no slot is free or
 * `alloc` is NULL. */
int ahci_slot_alloc(struct ahci_slot_allocator *alloc);

/* Release a slot previously returned by `ahci_slot_alloc`. Returns
 * 0 on success, -1 if `alloc` is NULL, `slot` is out of range, or
 * the slot was already free (double-release is a bug worth
 * surfacing rather than silently accepting). */
int ahci_slot_release(struct ahci_slot_allocator *alloc, int slot);

/* Number of slots currently inflight (allocated but not released).
 * Returns 0 if `alloc` is NULL. */
uint8_t ahci_slot_inflight_count(const struct ahci_slot_allocator *alloc);

/* Bitmask of slots currently inflight (allocated but not released).
 * Bit `i` is set iff slot `i` has been handed out by
 * `ahci_slot_alloc` and not yet released by `ahci_slot_release`.
 * Bits at positions >= `slot_count` are always zero so the result
 * is safe to feed into `ahci_dispatch_completed_slots` (declared
 * in `drivers/storage/ahci_dispatch.h`) without further masking.
 *
 * Returns 0 if `alloc` is NULL or the allocator is unconfigured
 * (`slot_count == 0`). Pure: no allocations, no side effects.
 *
 * Slice 3F prep: the future multi-slot dispatch path will sample
 * this mask before each CI register read so the completion fan-in
 * can filter out spurious clears the host never owned. */
uint32_t ahci_slot_inflight_mask(const struct ahci_slot_allocator *alloc);

/* True (1) if `slot` is currently free, 0 otherwise. Returns 0
 * for any invalid input. */
int ahci_slot_is_free(const struct ahci_slot_allocator *alloc, int slot);

/* Mark every slot as free again — used by COMRESET recovery after
 * the controller dropped all inflight commands. No-op on NULL. */
void ahci_slot_allocator_reset(struct ahci_slot_allocator *alloc);

#endif /* DRIVERS_STORAGE_AHCI_SLOT_ALLOCATOR_H */
