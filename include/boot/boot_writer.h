#ifndef BOOT_WRITER_H
#define BOOT_WRITER_H

#include <stddef.h>
#include <stdint.h>

#define BOOT_CONFIG_MAGIC 0xB001CF61
#define BOOT_CONFIG_LBA 1

struct boot_config_sector {
  uint32_t magic;           // 0xB00TCFG1
  char keyboard_layout[16]; // "us", "br-abnt2", etc.
  uint8_t reserved[492];    // Padding to 512 bytes
} __attribute__((packed));

#include "boot/boot_manifest.h"
#include "fs/block.h"
#include "fs/storage/partition.h"

struct boot_payload {
  const uint8_t *data;
  uint32_t size;
};

struct boot_payload_set {
  struct boot_payload stage1;
  struct boot_payload stage2;
  struct boot_payload kernel_main;     /* ELF */
  struct boot_payload kernel_recovery; /* ELF (fallback) */
};

/* Retorna payloads embarcados no binário (gerados em
 * build/generated/boot_payloads.h). */
struct boot_payload_set boot_embedded_payloads(void);

/* Prepara setor 0 combinando stage1 + partições e patches de LBA/size. */
int bootwriter_write_stage1(struct block_device *disk,
                            const struct mbr_partition *boot_part,
                            uint32_t stage2_lba, uint32_t stage2_sectors,
                            const struct boot_payload *stage1);

/* Escreve stage2 + manifest + kernels na partição BOOT. */
int bootwriter_write_payloads(struct block_device *disk,
                              const struct mbr_partition *boot_part,
                              const struct boot_payload_set *payloads);

/* Saves boot configuration (keyboard layout) to the reserved Sector 1 (LBA 1).
   This allows the kernel to load the layout before mounting the encrypted root.
   @param disk Target disk (must have valid block_size=512)
   @param layout_name Keyboard layout name (e.g. "us", "br-abnt2"), max 15
   chars.
   @return 0 on success, -1 on failure. */
int bootwriter_write_config(struct block_device *disk, const char *layout_name);

/* Calculates and writes a fresh MBR partition
   table (Boot + Data).
   @param disk Target disk (must have valid
   block_size=512)
   @param boot_mb Desired size of boot partition
   in MiB
   @param out_boot_part Returns the created BOOT
   partition info
   @param out_data_part Returns the created DATA
   partition info
   @return 0 on success, -1 on failure. */
int bootwriter_partition_disk(struct block_device *disk, uint32_t boot_mb,
                              struct mbr_partition *out_boot_part,
                              struct mbr_partition *out_data_part);

/* Full installation: partitions disk, writes MBR,
   and installs bootloader payloads. This is the
   "autonomous" install function.
   @param disk Target disk
   @param boot_mb Desired boot partition size in
   MiB
   @param out_data_part Returns the DATA partition
   for subsequent formatting
   @return 0 on success, -1 on failure. */
int bootwriter_install_fresh(struct block_device *disk, uint32_t boot_mb,
                             struct mbr_partition *out_data_part,
                             const struct boot_payload_set *payloads);

#endif
