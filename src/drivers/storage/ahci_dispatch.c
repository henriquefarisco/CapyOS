#include "drivers/storage/ahci_dispatch.h"

/* Slice 3F (initial extraction, 2026-05-25) — pure logic for the
 * AHCI dispatch loop. See include/drivers/storage/ahci_dispatch.h
 * for the contract and rationale. */

/* AHCI register bit masks duplicated here so the pure TU does not
 * pull in the driver header (which carries MMIO, PCIe and HBA
 * struct declarations). Both bits are stable per the AHCI 1.3.1
 * specification: IS.TFES (Task File Error Status) at bit 30 of
 * PxIS, TFD.ERR (ATA Status ERR) at bit 0 of PxTFD. */
#define AHCI_DISPATCH_IS_TFES_MASK (1u << 30)
#define AHCI_DISPATCH_TFD_ERR_MASK 0x1u

enum ahci_dispatch_observation
ahci_dispatch_classify_tick(uint32_t ci, uint32_t is, uint32_t tfd,
                            uint32_t slot_bit) {
  if ((ci & slot_bit) == 0u) {
    /* CI cleared has higher precedence than IS.TFES: the controller
     * already retired the slot, so the host treats this as a
     * completion. The downstream classifier (block_io_classify_ahci)
     * decides whether the completion was clean or carried an error
     * class derived from the same IS/TFD bits. */
    return AHCI_DISPATCH_COMPLETED;
  }
  if ((is & AHCI_DISPATCH_IS_TFES_MASK) != 0u ||
      (tfd & AHCI_DISPATCH_TFD_ERR_MASK) != 0u) {
    return AHCI_DISPATCH_ABORTED;
  }
  return AHCI_DISPATCH_INFLIGHT;
}

uint32_t ahci_dispatch_completed_slots(uint32_t prev_ci, uint32_t cur_ci,
                                       uint32_t inflight_mask) {
  /* A slot completes when its bit was set in prev_ci and is now
   * cleared in cur_ci. The bitwise expression `prev & ~cur` selects
   * exactly those transitions; intersecting with `inflight_mask`
   * filters out spurious clears the host never owned. */
  return (prev_ci & ~cur_ci) & inflight_mask;
}

uint32_t ahci_dispatch_inflight_count(uint32_t inflight_mask) {
  /* Brian Kernighan's bit-count idiom: each iteration clears the
   * lowest set bit. Loops at most 32 times for a 32-bit mask; in
   * the AHCI inflight case (typically 0..NCS slots, NCS<=32) the
   * average iteration count is well below the worst case. No
   * platform builtin (__builtin_popcount) so the helper compiles
   * identically across clang/gcc and across the kernel + host
   * test toolchains. */
  uint32_t count = 0u;
  while (inflight_mask != 0u) {
    inflight_mask &= inflight_mask - 1u;
    count++;
  }
  return count;
}

int ahci_dispatch_can_admit(uint32_t inflight_mask, uint32_t concurrent_limit) {
  if (concurrent_limit == 0u) {
    /* "no limit" sentinel: admission is the allocator's
     * responsibility downstream. */
    return 1;
  }
  return ahci_dispatch_inflight_count(inflight_mask) < concurrent_limit ? 1 : 0;
}

int ahci_dispatch_first_slot(uint32_t mask) {
  /* Lowest-bit-index search by linear scan over 32 positions. The
   * loop terminates as soon as the lowest set bit is found, so the
   * common case (small mask) is fast even without builtins. */
  if (mask == 0u) {
    return -1;
  }
  for (int i = 0; i < 32; ++i) {
    if ((mask & (1u << i)) != 0u) {
      return i;
    }
  }
  /* Unreachable: mask != 0 implies at least one bit in [0, 31]. */
  return -1;
}
