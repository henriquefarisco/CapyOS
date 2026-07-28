#ifndef CORE_BOOT_SLOT_INTERNAL_H
#define CORE_BOOT_SLOT_INTERNAL_H

#include "boot/boot_slot.h"
#include "boot/boot_slot_store.h"

enum boot_slot_lifecycle_phase {
  BOOT_SLOT_PHASE_UNINITIALIZED = 0,
  BOOT_SLOT_PHASE_RAM_TEST,
  BOOT_SLOT_PHASE_LEASED_UNBOUND,
  BOOT_SLOT_PHASE_BOUND_EMPTY,
  BOOT_SLOT_PHASE_BOUND_READY,
  BOOT_SLOT_PHASE_BOUND_UNKNOWN,
  BOOT_SLOT_PHASE_CLOSED,
};

struct boot_slot_persistence_state {
  boot_slot_persist_read_fn reader;
  boot_slot_persist_write_fn writer;
  boot_slot_persist_flush_fn flusher;
  void *ctx;
  uint64_t generation;
  uint64_t authority_epoch;
  uint64_t lease_epoch;
  void *lease_owner;
  uint32_t active_copy;
  int configured;
  int ready;
  int blank;
  int degraded;
  int stage_claimed;
  struct boot_slot_layout layout;
};

extern struct boot_slot_manager bsm;
extern int bsm_initialized;
extern int boot_slot_operation_busy;
extern struct boot_slot_persistence_state bsp;
extern enum boot_slot_lifecycle_phase boot_slot_phase;

static inline int boot_slot_internal_observable_locked(void) {
  if (boot_slot_phase == BOOT_SLOT_PHASE_BOUND_READY)
    return bsm_initialized && bsp.configured && bsp.ready;
#ifdef UNIT_TEST
  return boot_slot_phase == BOOT_SLOT_PHASE_RAM_TEST && bsm_initialized &&
         !bsp.lease_owner && !bsp.configured;
#else
  return 0;
#endif
}

static inline int boot_slot_internal_transition_ready_locked(void) {
  if (boot_slot_phase == BOOT_SLOT_PHASE_BOUND_READY)
    return bsm_initialized && bsp.configured && bsp.ready;
#ifdef UNIT_TEST
  return boot_slot_phase == BOOT_SLOT_PHASE_RAM_TEST && bsm_initialized &&
         !bsp.lease_owner && !bsp.configured;
#else
  return 0;
#endif
}

static inline int boot_slot_internal_ram_ready_locked(void) {
#ifdef UNIT_TEST
  return boot_slot_phase == BOOT_SLOT_PHASE_RAM_TEST && bsm_initialized &&
         !bsp.lease_owner && !bsp.configured;
#else
  return 0;
#endif
}

static inline int boot_slot_internal_bind_allowed_locked(void) {
#if defined(UNIT_TEST) || defined(CAPYOS_UEFI_LOADER)
  return boot_slot_phase == BOOT_SLOT_PHASE_RAM_TEST ||
         boot_slot_phase == BOOT_SLOT_PHASE_LEASED_UNBOUND ||
         boot_slot_phase == BOOT_SLOT_PHASE_BOUND_EMPTY ||
         boot_slot_phase == BOOT_SLOT_PHASE_BOUND_READY ||
         boot_slot_phase == BOOT_SLOT_PHASE_BOUND_UNKNOWN;
#else
  return bsp.lease_owner != NULL &&
         boot_slot_phase != BOOT_SLOT_PHASE_UNINITIALIZED &&
         boot_slot_phase != BOOT_SLOT_PHASE_RAM_TEST &&
         boot_slot_phase != BOOT_SLOT_PHASE_CLOSED;
#endif
}

int boot_slot_internal_operation_begin(void);
void boot_slot_internal_operation_end(void);
void boot_slot_internal_init_locked(void);
int boot_slot_internal_set_persistence_locked(
    boot_slot_persist_read_fn reader, boot_slot_persist_write_fn writer,
    boot_slot_persist_flush_fn flusher, void *ctx,
    const struct boot_slot_layout *layout);
int boot_slot_store_internal_open_locked(
    struct boot_slot_store *store, const struct boot_slot_layout *layout,
    boot_slot_store_read_fn reader, boot_slot_store_write_fn writer,
    boot_slot_store_flush_fn flusher, void *ctx, uint64_t lease_epoch);
int boot_slot_store_internal_revoke_locked(struct boot_slot_store *store,
                                           uint64_t lease_epoch);
int bs_image_valid(const struct boot_slot_image *image,
                   uint32_t capacity_sectors);
void bs_slot_apply_image(struct boot_slot *slot, uint32_t slot_index,
                         const struct boot_slot_image *image,
                         enum boot_slot_state state);
int bs_write_verified(const struct boot_slot_manager *manager,
                      uint32_t copy_index, uint64_t generation);
/* Single mutation point of the manager: writes the candidate to the inactive
 * mirror at generation+1, verifies the readback and only then publishes it in
 * RAM. Shared with boot_slot_lifecycle.c. */
int bs_commit_locked(const struct boot_slot_manager *candidate);
int boot_slot_internal_initialize_persistent_locked(
    const struct boot_slot_layout *layout,
    const struct boot_slot_image *image);
int boot_slot_internal_persistence_ready_locked(void);
uint64_t boot_slot_internal_persistence_generation_locked(void);
int boot_slot_internal_manager_get_locked(struct boot_slot_manager *out);
int boot_slot_internal_get_active_locked(struct boot_slot *out);
int boot_slot_internal_get_locked(uint32_t index, struct boot_slot *out);
int boot_slot_internal_needs_rollback_locked(void);
#ifdef UNIT_TEST
int boot_slot_internal_stage_locked(uint32_t slot, const char *version,
                                    uint32_t checksum);
int boot_slot_internal_confirm_health_locked(void);
#endif
#if defined(UNIT_TEST) || defined(CAPYOS_UEFI_LOADER)
int boot_slot_internal_activate_locked(uint32_t slot);
int boot_slot_internal_select_locked(uint32_t *out_slot,
                                     uint64_t *out_generation);

int boot_slot_internal_rollback_locked(void);
#endif
int boot_slot_internal_arm_provider_locked(uint32_t slot,
                                           uint64_t expected_generation,
                                           uint64_t *out_generation);
int boot_slot_internal_confirm_verified_locked(uint32_t slot,
                                               uint64_t generation,
                                               uint64_t authority_epoch,
                                               uint64_t lease_epoch);

#ifdef UNIT_TEST
int boot_slot_test_publish_metadata(uint32_t slot,
                                    const struct boot_slot_image *image);
#endif

#endif
