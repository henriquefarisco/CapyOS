#ifndef CORE_BOOT_SLOT_H
#define CORE_BOOT_SLOT_H

#include <stddef.h>
#include <stdint.h>

#define BOOT_SLOT_COUNT 2
#define BOOT_SLOT_NAME_MAX 8
#define BOOT_SLOT_VERSION_MAX 40
#define BOOT_SLOT_SHA256_SIZE 32u
#define BOOT_SLOT_PERSIST_RECORD_SIZE 512u
#define BOOT_SLOT_PERSIST_COPY_COUNT 2u
#define BOOT_SLOT_NONE UINT32_MAX
#define BOOT_SLOT_DEFAULT_TRIES 1u
#define BOOT_SLOT_MAX_TRIES 1u
#define BOOT_SLOT_PERSIST_EMPTY 1
#define BOOT_SLOT_ERR_IO (-1)
#define BOOT_SLOT_ERR_COMMIT_UNKNOWN (-2)
#define BOOT_SLOT_ERR_STALE (-3)
#define BOOT_SLOT_ERR_BUSY (-4)

enum boot_slot_state {
  BOOT_SLOT_EMPTY = 0,
  BOOT_SLOT_VALID,
  BOOT_SLOT_ACTIVE,
  BOOT_SLOT_ROLLBACK,
  BOOT_SLOT_FAILED
};

struct boot_slot_region {
  uint32_t header_lba;
  uint32_t payload_lba;
  uint32_t payload_capacity_sectors;
};

struct boot_slot_layout {
  uint32_t boot_sectors;
  struct boot_slot_region slots[BOOT_SLOT_COUNT];
  uint32_t control_lba[BOOT_SLOT_PERSIST_COPY_COUNT];
};

struct boot_slot_image {
  char version[BOOT_SLOT_VERSION_MAX];
  uint32_t payload_size;
  uint8_t payload_sha256[BOOT_SLOT_SHA256_SIZE];
};

struct boot_slot {
  char name[BOOT_SLOT_NAME_MAX];
  char version[BOOT_SLOT_VERSION_MAX];
  enum boot_slot_state state;
  uint32_t boot_count;
  uint32_t success_count;
  uint32_t fail_count;
  uint32_t header_lba;
  uint32_t payload_lba;
  uint32_t payload_capacity_sectors;
  uint32_t payload_size;
  uint32_t checksum;
  uint8_t payload_sha256[BOOT_SLOT_SHA256_SIZE];
  int health_confirmed;
};

struct boot_slot_manager {
  struct boot_slot slots[BOOT_SLOT_COUNT];
  uint32_t active_slot;
  uint32_t next_slot;
  int rollback_pending;
  uint32_t confirmed_slot;
  uint32_t pending_slot;
  uint32_t tries_remaining;
  uint32_t boot_sectors;
  uint32_t control_lba[BOOT_SLOT_PERSIST_COPY_COUNT];
};

#define BOOT_SLOT_SNAPSHOT_VERSION 2u

struct boot_slot_snapshot {
  uint32_t version;
  uint32_t size;
  uint64_t generation;
  uint64_t authority_epoch;
  uint64_t lease_epoch;
  struct boot_slot_manager manager;
};

typedef int (*boot_slot_persist_read_fn)(void *ctx, uint32_t copy_index,
                                         uint8_t *record, size_t record_size);
typedef int (*boot_slot_persist_write_fn)(void *ctx, uint32_t copy_index,
                                          const uint8_t *record,
                                          size_t record_size);
typedef int (*boot_slot_persist_flush_fn)(void *ctx);

int boot_slot_init(void);
int boot_slot_layout_plan(uint32_t boot_sectors,
                          struct boot_slot_layout *out);
#ifdef UNIT_TEST
int boot_slot_test_reset_uninitialized(void);
int boot_slot_set_persistence(boot_slot_persist_read_fn reader,
                              boot_slot_persist_write_fn writer,
                              boot_slot_persist_flush_fn flusher, void *ctx,
                              const struct boot_slot_layout *layout);
#endif
#ifdef UNIT_TEST
int boot_slot_initialize_persistent(const struct boot_slot_layout *layout,
                                    const struct boot_slot_image *image);
#endif
int boot_slot_persistence_ready(void);
uint64_t boot_slot_persistence_generation(void);
#ifdef UNIT_TEST
int boot_slot_repair_mirror(void);
#endif
int boot_slot_snapshot_get(struct boot_slot_snapshot *out);
int boot_slot_manager_get(struct boot_slot_manager *out);
int boot_slot_manager_validate(const struct boot_slot_manager *manager);
int boot_slot_get_active(struct boot_slot *out);
int boot_slot_get(uint32_t index, struct boot_slot *out);
#ifdef UNIT_TEST
int boot_slot_stage(uint32_t slot, const char *version, uint32_t checksum);
int boot_slot_stage_image(uint32_t slot, const struct boot_slot_image *image);
#endif
#if defined(UNIT_TEST) || defined(CAPYOS_UEFI_LOADER)
int boot_slot_activate(uint32_t slot);
int boot_slot_select_for_boot(uint32_t *out_slot, uint64_t *out_generation);
#ifdef UNIT_TEST
int boot_slot_confirm_health(void);
#endif
int boot_slot_confirm_health_verified(uint32_t slot, uint64_t generation,
                                      uint64_t authority_epoch,
                                      uint64_t lease_epoch);
int boot_slot_rollback(void);
#endif
int boot_slot_needs_rollback(void);
void boot_slot_status(void (*print)(const char *));

#endif /* CORE_BOOT_SLOT_H */
