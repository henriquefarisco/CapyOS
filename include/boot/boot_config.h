#ifndef BOOT_CONFIG_H
#define BOOT_CONFIG_H

#include <stdint.h>

#define BOOT_CONFIG_MAGIC 0xB001CF61
#define BOOT_CONFIG_VERSION 3
#define BOOT_CONFIG_LBA 1

#define BOOT_CONFIG_FLAG_HAS_VOLUME_KEY 0x0001u

struct boot_config_sector {
  uint32_t magic;           // BOOT_CONFIG_MAGIC
  uint16_t version;         // BOOT_CONFIG_VERSION
  uint16_t flags;           // BOOT_CONFIG_FLAG_*
  char keyboard_layout[16]; // "us", "br-abnt2", etc.
  char language[16];        // "en", "pt-BR", "es", etc.
  char volume_key[64];      // Canonical: A-Z0-9, no hyphens
  uint8_t reserved[408];    // Padding to 512 bytes
} __attribute__((packed));

#endif
