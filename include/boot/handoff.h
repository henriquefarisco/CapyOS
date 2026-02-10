// UEFI → kernel handoff structure (64-bit).
#ifndef BOOT_HANDOFF_H
#define BOOT_HANDOFF_H

#include <stdint.h>

#define BOOT_HANDOFF_MAGIC 0x48464F4E /* "NOFH" */
#define BOOT_HANDOFF_VERSION 1

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
};

#endif // BOOT_HANDOFF_H
