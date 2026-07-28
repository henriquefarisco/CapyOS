// UEFI → kernel handoff structure (64-bit).
#ifndef BOOT_HANDOFF_H
#define BOOT_HANDOFF_H

#include <stddef.h>
#include <stdint.h>

#define BOOT_HANDOFF_MAGIC 0x48464F4E /* "NOFH" */
#define BOOT_HANDOFF_VERSION 10
#define BOOT_HANDOFF_DISK_IDENTITY_VALID 0x00000001u
#define BOOT_HANDOFF_DISK_IDENTITY_LEGACY_VALID 0x00000002u

#define BOOT_HANDOFF_RUNTIME_BOOT_SERVICES_ACTIVE 0x00000001u
#define BOOT_HANDOFF_RUNTIME_FIRMWARE_INPUT 0x00000002u
#define BOOT_HANDOFF_RUNTIME_FIRMWARE_BLOCK_IO 0x00000004u
#define BOOT_HANDOFF_RUNTIME_HYBRID_BOOT 0x00000008u

#define BOOT_MEDIA_UNKNOWN  0
#define BOOT_MEDIA_ISO      1
#define BOOT_MEDIA_HDD      2
#define BOOT_MEDIA_NVME     3
#define BOOT_MEDIA_VHDX     4
#define BOOT_MEDIA_USB      5

#define BOOT_MODE_NORMAL    0
#define BOOT_MODE_RECOVERY  1
#define BOOT_MODE_DIAG      2

struct boot_disk_identity {
  uint32_t flags;
  uint32_t reserved;
  uint64_t esp_lba_start;
  uint64_t esp_lba_count;
  uint64_t boot_lba_start;
  uint64_t boot_lba_count;
  uint64_t data_lba_start;
  uint64_t data_lba_count;
  uint8_t disk_guid[16];
  uint8_t esp_partition_guid[16];
  uint8_t boot_partition_guid[16];
  uint8_t data_partition_guid[16];
};

#define BOOT_HANDOFF_SLOT_ATTEMPT_VALID 0x00000001u
#define BOOT_HANDOFF_SLOT_ATTEMPT_PENDING 0x00000002u
#define BOOT_HANDOFF_SLOT_ATTEMPT_ROLLBACK 0x00000004u

struct boot_slot_attempt_handoff {
  uint32_t flags;
  uint32_t slot;
  uint64_t generation;
};

struct boot_handoff_fb {
  uint64_t base;
  uint32_t size;
  uint32_t width;
  uint32_t height;
  uint32_t pitch;
  uint32_t bpp;
};

struct boot_handoff {
  uint32_t magic;
  uint32_t version;
  uint64_t rsdp;
  struct boot_handoff_fb fb;
  uint64_t memmap;
  uint32_t memmap_desc_size;
  uint32_t memmap_entries;
  uint64_t memmap_size;
  uint64_t memmap_capacity;
  uint64_t efi_system_table;
  uint64_t efi_block_io;
  uint64_t efi_disk_last_lba;
  uint64_t data_lba_start;
  uint64_t data_lba_count;
  uint32_t efi_block_size;
  uint32_t efi_media_id;
  uint64_t efi_block_io_raw;
  uint64_t efi_disk_last_lba_raw;
  uint64_t data_lba_start_raw;
  uint64_t data_lba_count_raw;
  uint32_t efi_media_id_raw;
  uint32_t runtime_flags;
  uint32_t boot_cfg_flags;
  char boot_keyboard_layout[16];
  char boot_language[16];
  char boot_volume_key[64];
  char boot_hostname[32];
  char boot_theme[16];
  char boot_admin_username[32];
  char boot_admin_password[64];
  uint8_t boot_splash_enabled;
  uint64_t efi_image_handle;
  uint64_t efi_map_key;
  uint8_t boot_media;
  uint8_t boot_mode;
  uint8_t _reserved_pad[5];
  struct boot_disk_identity disk_identity;
  struct boot_slot_attempt_handoff slot_attempt;
};

#define BOOT_HANDOFF_V8_SIZE 440u
#define BOOT_HANDOFF_V9_SIZE 560u
#define BOOT_HANDOFF_V10_SIZE 576u
typedef char boot_handoff_v8_prefix_size_must_stay_440[
    offsetof(struct boot_handoff, disk_identity) == BOOT_HANDOFF_V8_SIZE ? 1 : -1];
typedef char boot_disk_identity_size_must_stay_120[
    sizeof(struct boot_disk_identity) == 120u ? 1 : -1];
typedef char boot_handoff_v9_prefix_size_must_stay_560[
    offsetof(struct boot_handoff, slot_attempt) == BOOT_HANDOFF_V9_SIZE ? 1 : -1];
typedef char boot_slot_attempt_handoff_size_must_stay_16[
    sizeof(struct boot_slot_attempt_handoff) == 16u ? 1 : -1];
typedef char boot_handoff_v10_size_must_stay_576[
    sizeof(struct boot_handoff) == BOOT_HANDOFF_V10_SIZE ? 1 : -1];

#endif // BOOT_HANDOFF_H
