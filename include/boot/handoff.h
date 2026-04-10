// UEFI → kernel handoff structure (64-bit).
#ifndef BOOT_HANDOFF_H
#define BOOT_HANDOFF_H

#include <stdint.h>

#define BOOT_HANDOFF_MAGIC 0x48464F4E /* "NOFH" */
#define BOOT_HANDOFF_VERSION 7

#define BOOT_HANDOFF_RUNTIME_BOOT_SERVICES_ACTIVE 0x00000001u
#define BOOT_HANDOFF_RUNTIME_FIRMWARE_INPUT 0x00000002u
#define BOOT_HANDOFF_RUNTIME_FIRMWARE_BLOCK_IO 0x00000004u
#define BOOT_HANDOFF_RUNTIME_HYBRID_BOOT 0x00000008u

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
  uint64_t efi_image_handle;
  uint64_t efi_map_key;
};

#endif // BOOT_HANDOFF_H
