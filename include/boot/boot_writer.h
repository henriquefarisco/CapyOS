#ifndef BOOT_WRITER_H
#define BOOT_WRITER_H

#include <stddef.h>
#include <stdint.h>

#include "boot/boot_config.h"

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

/* Retorna payloads embarcados no binÃ¡rio (gerados em
 * build/generated/boot_payloads.h). */
struct boot_payload_set boot_embedded_payloads(void);

/* Prepara setor 0 combinando stage1 + partiÃ§Ãµes e patches de LBA/size. */
int bootwriter_write_stage1(struct block_device *disk,
                            const struct mbr_partition *boot_part,
                            uint32_t stage2_lba, uint32_t stage2_sectors,
                            const struct boot_payload *stage1);

/* Escreve stage2 + manifest + kernels na partiÃ§Ã£o BOOT. */
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

/* Partition Types */
#define PARTITION_TYPE_CAPYOS_BOOT 0xDA
#define PARTITION_TYPE_LINUX 0x83
#define PARTITION_TYPE_EMPTY 0x00

/* Calculates and writes a fresh MBR partition table (Boot, System, Data).
   @param disk Target disk
   @param boot_mb Size of Boot partition in MiB
   @param system_mb Size of System partition in MiB (0 for none)
   @param out_boot_part Returns BOOT partition info
   @param out_system_part Returns SYSTEM partition info (can be NULL)
   @param out_data_part Returns DATA partition info
   @return 0 on success, -1 on failure. */
int bootwriter_partition_disk(struct block_device *disk, uint32_t boot_mb,
                              uint32_t system_mb,
                              struct mbr_partition *out_boot_part,
                              struct mbr_partition *out_system_part,
                              struct mbr_partition *out_data_part);

/* Full installation: partitions disk, writes MBR,
   and installs bootloader payloads. This is the
   "autonomous" install function.
   @param disk Target disk
   @param boot_mb Boot partition size in MiB
   @param system_mb System partition size in MiB
   @param out_data_part Returns DATA partition
   @return 0 on success, -1 on failure. */
int bootwriter_install_fresh(struct block_device *disk, uint32_t boot_mb,
                             uint32_t system_mb,
                             struct mbr_partition *out_data_part,
                             const struct boot_payload_set *payloads);

#endif
