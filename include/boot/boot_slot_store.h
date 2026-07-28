#ifndef CORE_BOOT_SLOT_STORE_H
#define CORE_BOOT_SLOT_STORE_H

#include <stddef.h>
#include <stdint.h>

#include "boot/boot_slot.h"

#define BOOT_SLOT_STORE_SECTOR_SIZE 512u
#define BOOT_SLOT_STORE_ERR_IO (-1)
#define BOOT_SLOT_STORE_ERR_COMMIT_UNKNOWN (-2)
#define BOOT_SLOT_STORE_ERR_VERIFY (-3)
#define BOOT_SLOT_STORE_ERR_STALE (-4)
#define BOOT_SLOT_STORE_ERR_BUSY (-5)

typedef int (*boot_slot_store_read_fn)(void *ctx, uint32_t lba,
                                       uint8_t sector[BOOT_SLOT_STORE_SECTOR_SIZE]);
typedef int (*boot_slot_store_write_fn)(
    void *ctx, uint32_t lba,
    const uint8_t sector[BOOT_SLOT_STORE_SECTOR_SIZE]);
typedef int (*boot_slot_store_flush_fn)(void *ctx);

struct boot_slot_store {
  uint64_t opaque_epoch;
#if defined(UNIT_TEST) || defined(CAPYOS_UEFI_LOADER)
  struct boot_slot_layout test_layout;
  boot_slot_store_read_fn test_reader;
  boot_slot_store_write_fn test_writer;
  boot_slot_store_flush_fn test_flusher;
  void *test_ctx;
  int test_ready;
#endif
};

#if defined(UNIT_TEST) || defined(CAPYOS_UEFI_LOADER)
int boot_slot_store_init(struct boot_slot_store *store,
                         const struct boot_slot_layout *layout,
                         boot_slot_store_read_fn reader,
                         boot_slot_store_write_fn writer,
                         boot_slot_store_flush_fn flusher, void *ctx);
#endif
int boot_slot_store_bind_control(struct boot_slot_store *store,
                                 uint64_t expected_lease_epoch);
int boot_slot_store_initialize_persistent(
    struct boot_slot_store *store, uint64_t expected_lease_epoch,
    const struct boot_slot_image *image);
int boot_slot_store_arm(
    struct boot_slot_store *store, uint64_t expected_lease_epoch,
    uint32_t slot, uint64_t expected_generation,
    uint64_t *out_generation);
int boot_slot_store_confirm_health(
    struct boot_slot_store *store, uint64_t expected_lease_epoch,
    uint32_t slot, uint64_t generation);
int boot_slot_store_stage_inactive_authorized(
    struct boot_slot_store *store, uint64_t expected_lease_epoch,
    const struct boot_slot_snapshot *expected, uint32_t slot,
    const struct boot_slot_image *image, const uint8_t *payload,
    size_t payload_len, uint64_t *out_valid_generation);
#if defined(UNIT_TEST) || defined(CAPYOS_UEFI_LOADER)
int boot_slot_store_stage_inactive(struct boot_slot_store *store,
                                   const struct boot_slot_manager *manager,
                                   uint32_t slot,
                                   const struct boot_slot_image *image,
                                   const uint8_t *payload, size_t payload_len);
#endif
#ifdef CAPYOS_UEFI_LOADER
int boot_slot_store_encode_header(
    const struct boot_slot_layout *layout, uint32_t slot,
    const struct boot_slot_image *image,
    uint8_t header[BOOT_SLOT_STORE_SECTOR_SIZE]);
#endif
int boot_slot_store_read_header(struct boot_slot_store *store,
                                uint64_t expected_lease_epoch, uint32_t slot,
                                struct boot_slot_image *out_image);

#endif /* CORE_BOOT_SLOT_STORE_H */
