#include "internal/boot_slot_internal.h"

static int operation_begin(void) {
  return boot_slot_internal_operation_begin();
}

#ifdef UNIT_TEST
int boot_slot_test_reset_uninitialized(void) {
  int rc = operation_begin();
  if (rc != 0)
    return rc;
  if (bsp.lease_owner) {
    boot_slot_internal_operation_end();
    return BOOT_SLOT_ERR_BUSY;
  }
  boot_slot_internal_init_locked();
  bsm_initialized = 0;
  boot_slot_phase = BOOT_SLOT_PHASE_UNINITIALIZED;
  boot_slot_internal_operation_end();
  return 0;
}
#endif

int boot_slot_init(void) {
  int rc = operation_begin();
  if (rc != 0)
    return rc;
  if (bsp.lease_owner) {
    boot_slot_internal_operation_end();
    return BOOT_SLOT_ERR_BUSY;
  }
  boot_slot_internal_init_locked();
  boot_slot_phase = BOOT_SLOT_PHASE_RAM_TEST;
  boot_slot_internal_operation_end();
  return 0;
}

int boot_slot_persistence_ready(void) {
  int ready;
  if (operation_begin() != 0)
    return 0;
  ready = boot_slot_internal_persistence_ready_locked();
  boot_slot_internal_operation_end();
  return ready;
}

uint64_t boot_slot_persistence_generation(void) {
  uint64_t generation;
  if (operation_begin() != 0)
    return 0u;
  generation = boot_slot_internal_persistence_generation_locked();
  boot_slot_internal_operation_end();
  return generation;
}

#ifdef UNIT_TEST
int boot_slot_repair_mirror(void) {
  uint32_t target_copy;
  uint64_t generation;
  int rc = operation_begin();
  if (rc != 0)
    return rc;
  if (!boot_slot_internal_persistence_ready_locked() || !bsp.degraded ||
      bsp.stage_claimed || bsp.generation == UINT64_MAX) {
    rc = BOOT_SLOT_ERR_IO;
  } else {
    target_copy = bsp.active_copy ^ 1u;
    generation = bsp.generation + 1u;
    rc = bs_write_verified(&bsm, target_copy, generation);
    if (rc == 0) {
      bsp.active_copy = target_copy;
      bsp.generation = generation;
      bsp.degraded = 0;
    } else if (rc == BOOT_SLOT_ERR_COMMIT_UNKNOWN) {
      bsp.ready = 0;
      boot_slot_phase = BOOT_SLOT_PHASE_BOUND_UNKNOWN;
    }
  }
  boot_slot_internal_operation_end();
  return rc;
}

#endif
int boot_slot_manager_get(struct boot_slot_manager *out) {
  int rc;
  if (out)
    *out = (struct boot_slot_manager){0};
  if (!out)
    return BOOT_SLOT_ERR_IO;
  rc = operation_begin();
  if (rc != 0)
    return rc;
  rc = boot_slot_internal_manager_get_locked(out);
  boot_slot_internal_operation_end();
  return rc;
}

int boot_slot_get_active(struct boot_slot *out) {
  int rc;
  if (out)
    *out = (struct boot_slot){0};
  if (!out)
    return BOOT_SLOT_ERR_IO;
  rc = operation_begin();
  if (rc != 0)
    return rc;
  rc = boot_slot_internal_get_active_locked(out);
  boot_slot_internal_operation_end();
  return rc;
}

int boot_slot_get(uint32_t index, struct boot_slot *out) {
  int rc;
  if (out)
    *out = (struct boot_slot){0};
  if (!out)
    return BOOT_SLOT_ERR_IO;
  rc = operation_begin();
  if (rc != 0)
    return rc;
  rc = boot_slot_internal_get_locked(index, out);
  boot_slot_internal_operation_end();
  return rc;
}

int boot_slot_internal_manager_get_locked(struct boot_slot_manager *out) {
  if (!boot_slot_internal_observable_locked() || !out)
    return BOOT_SLOT_ERR_IO;
  *out = bsm;
  return 0;
}

int boot_slot_internal_get_active_locked(struct boot_slot *out) {
  if (!boot_slot_internal_observable_locked() || !out)
    return BOOT_SLOT_ERR_IO;
  *out = bsm.slots[bsm.active_slot];
  return 0;
}

int boot_slot_internal_get_locked(uint32_t index, struct boot_slot *out) {
  if (!boot_slot_internal_observable_locked() || !out ||
      index >= BOOT_SLOT_COUNT)
    return BOOT_SLOT_ERR_IO;
  *out = bsm.slots[index];
  return 0;
}

int boot_slot_internal_needs_rollback_locked(void) {
  return boot_slot_internal_observable_locked() ? bsm.rollback_pending
                                                : BOOT_SLOT_ERR_IO;
}

int boot_slot_needs_rollback(void) {
  int rc = operation_begin();
  if (rc != 0)
    return rc;
  rc = boot_slot_internal_needs_rollback_locked();
  boot_slot_internal_operation_end();
  return rc;
}

#ifdef UNIT_TEST
int boot_slot_stage(uint32_t slot, const char *version, uint32_t checksum) {
  int rc = operation_begin();
  if (rc != 0)
    return rc;
  rc = boot_slot_internal_stage_locked(slot, version, checksum);
  boot_slot_internal_operation_end();
  return rc;
}

#endif
#if defined(UNIT_TEST) || defined(CAPYOS_UEFI_LOADER)
int boot_slot_activate(uint32_t slot) {
  int rc = operation_begin();
  if (rc != 0)
    return rc;
  rc = boot_slot_internal_activate_locked(slot);
  boot_slot_internal_operation_end();
  return rc;
}

int boot_slot_select_for_boot(uint32_t *out_slot, uint64_t *out_generation) {
  int rc;
  if (out_slot)
    *out_slot = BOOT_SLOT_NONE;
  if (out_generation)
    *out_generation = 0u;
  rc = operation_begin();
  if (rc != 0)
    return rc;
  rc = boot_slot_internal_select_locked(out_slot, out_generation);
  boot_slot_internal_operation_end();
  return rc;
}

#ifdef UNIT_TEST
int boot_slot_confirm_health(void) {
  int rc = operation_begin();
  if (rc != 0)
    return rc;
  rc = boot_slot_internal_confirm_health_locked();
  boot_slot_internal_operation_end();
  return rc;
}

#endif
int boot_slot_confirm_health_verified(uint32_t slot, uint64_t generation,
                                      uint64_t authority_epoch,
                                      uint64_t lease_epoch) {
  int rc = operation_begin();
  if (rc != 0)
    return rc;
  rc = boot_slot_internal_confirm_verified_locked(
      slot, generation, authority_epoch, lease_epoch);
  boot_slot_internal_operation_end();
  return rc;
}

int boot_slot_rollback(void) {
  int rc = operation_begin();
  if (rc != 0)
    return rc;
  rc = boot_slot_internal_rollback_locked();
  boot_slot_internal_operation_end();
  return rc;
}
#endif
