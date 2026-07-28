#ifndef CORE_BOOT_SLOT_BLOCK_PROVIDER_H
#define CORE_BOOT_SLOT_BLOCK_PROVIDER_H

#include <stdint.h>

#include "boot/boot_slot_store.h"
#include "fs/block.h"

#define BOOT_SLOT_BLOCK_PROVIDER_ERR (-1)

typedef int (*boot_slot_block_flush_fn)(void *ctx);

struct boot_slot_disk_binding {
  uint32_t esp_lba;
  uint32_t esp_sectors;
  uint32_t boot_lba;
  uint32_t boot_sectors;
  uint32_t data_lba;
  uint32_t data_sectors;
  uint8_t disk_guid[16];
  uint8_t esp_guid[16];
  uint8_t boot_guid[16];
  uint8_t data_guid[16];
};

struct boot_slot_block_provider {
  uint64_t opaque_epoch;
};

#ifdef UNIT_TEST
int boot_slot_block_provider_init(
    struct boot_slot_block_provider *provider, struct block_device *raw,
    const struct boot_slot_disk_binding *binding,
    boot_slot_block_flush_fn flusher, void *flush_ctx,
    uint64_t *out_registration_epoch);
#endif
#ifdef UNIT_TEST
int boot_slot_block_provider_init_from_block_device(
    struct boot_slot_block_provider *provider, struct block_device *raw,
    const struct boot_slot_disk_binding *binding,
    uint64_t *out_registration_epoch);
#endif
int boot_slot_block_provider_unregister(
    struct boot_slot_block_provider *provider, uint64_t registration_epoch);
int boot_slot_block_provider_open_store(
    struct boot_slot_block_provider *provider, uint64_t registration_epoch,
    struct boot_slot_store *store, uint64_t *out_lease_epoch);
int boot_slot_block_provider_close_store(
    struct boot_slot_block_provider *provider, uint64_t registration_epoch,
    struct boot_slot_store *store, uint64_t lease_epoch);

#ifdef UNIT_TEST
struct boot_slot_block_provider_test_io {
  boot_slot_store_read_fn reader;
  boot_slot_store_write_fn writer;
  boot_slot_store_flush_fn flusher;
  void *ctx;
};

int boot_slot_block_provider_test_capture_io(
    struct boot_slot_block_provider_test_io *out);
#endif

#endif /* CORE_BOOT_SLOT_BLOCK_PROVIDER_H */
