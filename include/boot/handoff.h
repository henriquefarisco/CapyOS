// UEFI → kernel handoff structure (64-bit).
#ifndef BOOT_HANDOFF_H
#define BOOT_HANDOFF_H

#include <stdint.h>

#define BOOT_HANDOFF_MAGIC 0x48464F4E /* "NOFH" */
#define BOOT_HANDOFF_VERSION 4

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
  uint32_t _pad_raw;
  uint32_t boot_cfg_flags;
  char boot_keyboard_layout[16];
  char boot_volume_key[64];
};

#endif // BOOT_HANDOFF_H
