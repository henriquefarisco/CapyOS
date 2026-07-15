#ifndef BOOT_INSTALLER_DISK_POLICY_H
#define BOOT_INSTALLER_DISK_POLICY_H

#include <stddef.h>
#include <stdint.h>

#define INSTALLER_DISK_BLOCK_SIZE 512u
#define INSTALLER_DISK_ALIGN_LBA 2048ull
#define INSTALLER_DISK_ESP_SIZE_MIB 512ull
#define INSTALLER_DISK_BOOT_SIZE_MIB 256ull
#define INSTALLER_DISK_MIN_DATA_SIZE_MIB 1024ull
#define INSTALLER_DISK_GPT_ENTRIES_SECTORS 32ull

enum installer_disk_preflight_result {
  INSTALLER_DISK_PREFLIGHT_OK = 0,
  INSTALLER_DISK_PREFLIGHT_INVALID = -1,
  INSTALLER_DISK_PREFLIGHT_NO_MEDIA = -2,
  INSTALLER_DISK_PREFLIGHT_LOGICAL = -3,
  INSTALLER_DISK_PREFLIGHT_READ_ONLY = -4,
  INSTALLER_DISK_PREFLIGHT_REMOVABLE = -5,
  INSTALLER_DISK_PREFLIGHT_BLOCK_SIZE = -6,
  INSTALLER_DISK_PREFLIGHT_TOO_SMALL = -7,
  INSTALLER_DISK_PREFLIGHT_OVERFLOW = -8,
};

struct installer_disk_geometry {
  uint64_t block_count;
  uint32_t block_size;
  uint8_t media_present;
  uint8_t logical_partition;
  uint8_t read_only;
  uint8_t removable;
};

struct installer_disk_layout {
  uint64_t required_bytes;
  uint64_t total_sectors;
  uint64_t first_usable_lba;
  uint64_t last_usable_lba;
  uint64_t backup_entries_lba;
  uint64_t esp_lba;
  uint64_t esp_sectors;
  uint64_t boot_lba;
  uint64_t boot_sectors;
  uint64_t data_lba;
  uint64_t data_sectors;
};

uint64_t installer_disk_path_hash_init(void);
uint64_t installer_disk_path_hash_update(uint64_t state, const uint8_t *data,
                                         size_t length);
uint64_t installer_disk_minimum_bytes(void);
int installer_disk_plan(const struct installer_disk_geometry *geometry,
                        struct installer_disk_layout *out_layout);
int installer_disk_parse_selection(const char *text, size_t candidate_count,
                                   size_t *out_index);
int installer_disk_confirmation_valid(const char *text);

#endif
