#ifndef BOOT_CONFIG_H
#define BOOT_CONFIG_H

#include <stdint.h>

#define BOOT_CONFIG_MAGIC 0xB001CF61
#define BOOT_CONFIG_LBA 1

struct boot_config_sector {
  uint32_t magic;           // 0xB00TCFG1
  char keyboard_layout[16]; // "us", "br-abnt2", etc.
  uint8_t reserved[492];    // Padding to 512 bytes
} __attribute__((packed));

#endif
