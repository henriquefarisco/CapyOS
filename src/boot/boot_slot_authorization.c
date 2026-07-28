#include "internal/boot_slot_internal.h"

#include <stddef.h>

static void snapshot_zero(void *ptr, size_t len) {
  uint8_t *bytes = ptr;
  if (!bytes)
    return;
  for (size_t i = 0u; i < len; ++i)
    bytes[i] = 0u;
}

int boot_slot_snapshot_get(struct boot_slot_snapshot *out) {
  int rc;
  if (out)
    snapshot_zero(out, sizeof(*out));
  if (!out)
    return BOOT_SLOT_ERR_IO;
  rc = boot_slot_internal_operation_begin();
  if (rc != 0)
    return rc;
  if (!boot_slot_internal_persistence_ready_locked() || bsp.degraded ||
      bsp.authority_epoch == 0u) {
    rc = BOOT_SLOT_ERR_IO;
  } else if (bsp.stage_claimed) {
    rc = BOOT_SLOT_ERR_BUSY;
  } else {
    out->version = BOOT_SLOT_SNAPSHOT_VERSION;
    out->size = sizeof(*out);
    out->generation = bsp.generation;
    out->authority_epoch = bsp.authority_epoch;
    out->lease_epoch = bsp.lease_epoch;
    out->manager = bsm;
    rc = 0;
  }
  boot_slot_internal_operation_end();
  return rc;
}

#ifdef UNIT_TEST
int boot_slot_test_publish_metadata(uint32_t slot,
                                    const struct boot_slot_image *image) {
  struct boot_slot_manager candidate;
  uint32_t first_copy;
  uint32_t second_copy;
  uint32_t publish_copy;
  int rc = boot_slot_internal_operation_begin();
  if (rc != 0)
    return rc;
  if (!boot_slot_internal_persistence_ready_locked() || bsp.stage_claimed || bsp.degraded ||
      slot >= BOOT_SLOT_COUNT || slot == bsm.active_slot ||
      slot == bsm.confirmed_slot || bsm.pending_slot != BOOT_SLOT_NONE ||
      bsm.rollback_pending || !bs_image_valid(
          image, bsm.slots[slot].payload_capacity_sectors) ||
      bsp.generation > UINT64_MAX - 3u) {
    rc = BOOT_SLOT_ERR_STALE;
  } else {
    bsp.stage_claimed = 1;
    candidate = bsm;
    bs_slot_apply_image(&candidate.slots[slot], slot, image, BOOT_SLOT_FAILED);
    candidate.next_slot = candidate.confirmed_slot;
    first_copy = bsp.active_copy ^ 1u;
    second_copy = bsp.active_copy;
    rc = bs_write_verified(&candidate, first_copy, bsp.generation + 1u);
    if (rc == 0)
      rc = bs_write_verified(&candidate, second_copy, bsp.generation + 2u);
    if (rc == 0) {
      bsm = candidate;
      bsp.active_copy = second_copy;
      bsp.generation += 2u;
      bs_slot_apply_image(&candidate.slots[slot], slot, image, BOOT_SLOT_VALID);
      candidate.next_slot = slot;
      publish_copy = bsp.active_copy ^ 1u;
      rc = bs_write_verified(&candidate, publish_copy, bsp.generation + 1u);
      if (rc == 0) {
        bsm = candidate;
        bsp.active_copy = publish_copy;
        bsp.generation++;
      }
    }
    if (rc == BOOT_SLOT_ERR_COMMIT_UNKNOWN) {
      bsp.ready = 0;
      boot_slot_phase = BOOT_SLOT_PHASE_BOUND_UNKNOWN;
    }
    bsp.stage_claimed = 0;
  }
  boot_slot_internal_operation_end();
  return rc;
}
#endif
