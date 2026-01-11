

#include "boot/boot_writer.h"
#include "boot/boot_manifest.h"
#include "fs/block.h"
#include "fs/storage/partition.h"
#include "memory/kmem.h"

#define STAGE1_LBA_PLACEHOLDER 0xDEADBEEF
#define STAGE1_SECTORS_PLACEHOLDER 0xBEEF
#define STAGE2_LBA_PLACEHOLDER 0xDEADBEEF
#define STAGE2_SECTORS_PLACEHOLDER 0xCAFEBABE
#define KERNEL_LBA_PLACEHOLDER 0xFEEDFACE
#define KERNEL_SECTORS_PLACEHOLDER 0xBADC0FFE
/* Internal helper to patch a 32-bit placeholder in a binary buffer. */
static int patch_u32(uint8_t *buf, size_t len, uint32_t placeholder,
                     uint32_t value) {
  if (!buf || len < 4) {
    return -1;
  }
  size_t found = 0;
  for (size_t i = 0; i + 4 <= len; ++i) {
    uint32_t v = (uint32_t)buf[i] | ((uint32_t)buf[i + 1] << 8) |
                 ((uint32_t)buf[i + 2] << 16) | ((uint32_t)buf[i + 3] << 24);
    if (v == placeholder) {
      buf[i] = (uint8_t)(value & 0xFF);
      buf[i + 1] = (uint8_t)((value >> 8) & 0xFF);
      buf[i + 2] = (uint8_t)((value >> 16) & 0xFF);
      buf[i + 3] = (uint8_t)((value >> 24) & 0xFF);
      found++;
    }
  }
  return found > 0 ? 0 : -1;
}

/* Internal helper to patch a 16-bit placeholder in a binary buffer. */
static int patch_u16(uint8_t *buf, size_t len, uint16_t placeholder,
                     uint16_t value) {
  if (!buf || len < 2) {
    return -1;
  }
  size_t found = 0;
  for (size_t i = 0; i + 2 <= len; ++i) {
    uint16_t v = (uint16_t)buf[i] | ((uint16_t)buf[i + 1] << 8);
    if (v == placeholder) {
      buf[i] = (uint8_t)(value & 0xFF);
      buf[i + 1] = (uint8_t)((value >> 8) & 0xFF);
      found++;
    }
  }
  return found > 0 ? 0 : -1;
}

static uint32_t sectors_for_size(uint32_t size) { return (size + 511) / 512; }

/* Internal helper to write data ensuring sector alignment blocks. */
static int write_sector_aligned(struct block_device *disk, uint32_t lba,
                                const uint8_t *data, uint32_t size) {
  if (!disk || !data || size == 0) {
    return -1;
  }
  uint32_t sectors = sectors_for_size(size);
  uint8_t *tmp = (uint8_t *)kalloc(512);
  if (!tmp)
    return -1;
  uint32_t remaining = size;
  const uint8_t *ptr = data;
  for (uint32_t i = 0; i < sectors; ++i) {
    for (size_t j = 0; j < 512; ++j) {
      tmp[j] = (i * 512 + j < size) ? ptr[i * 512 + j] : 0;
    }
    if (block_device_write(disk, lba + i, tmp) != 0) {
      kfree(tmp);
      return -1;
    }
    remaining = (remaining > 512) ? (remaining - 512) : 0;
  }
  kfree(tmp);
  return 0;
}

/*
 * bootwriter_write_stage1
 *
 * Writes the Stage1 bootloader into the MBR (Sector 0).
 * Preserves the partition table if present, patches LBA/Sector counts for
 * Stage2.
 */
int bootwriter_write_stage1(struct block_device *disk,
                            const struct mbr_partition *boot_part,
                            uint32_t stage2_lba, uint32_t stage2_sectors,
                            const struct boot_payload *stage1) {
  if (!disk || !boot_part || !stage1 || !stage1->data || stage1->size < 512) {
    return -1;
  }
  uint8_t sector[512];
  for (size_t i = 0; i < 512; ++i) {
    sector[i] = stage1->data[i];
  }
  /* Preserva tabela de partições existente. */
  uint8_t mbr_existing[512];
  if (block_device_read(disk, 0, mbr_existing) != 0) {
    return -1;
  }
  for (size_t i = 0; i < 64; ++i) {
    sector[446 + i] = mbr_existing[446 + i];
  }
  /* Patch stage2 lba placeholder (0xDEADBEEF). */
  if (patch_u32(sector, sizeof(sector), STAGE1_LBA_PLACEHOLDER, stage2_lba) !=
      0) {
    return -1;
  }
  /* Patch stage2 sectors placeholder (0xBEEF). */
  if (patch_u16(sector, sizeof(sector), STAGE1_SECTORS_PLACEHOLDER,
                (uint16_t)stage2_sectors) != 0) {
    return -1;
  }
  /* Kernel placeholders are patched in stage2, not here */
  sector[510] = 0x55;
  sector[511] = 0xAA;

  /* Write MBR to disk */
  if (block_device_write(disk, 0, sector) != 0) {
    return -1;
  }

  /* Read back and verify critical bytes */
  uint8_t verify[512];
  if (block_device_read(disk, 0, verify) != 0) {
    return -1;
  }

  /* Check boot signature */
  if (verify[510] != 0x55 || verify[511] != 0xAA) {
    return -1;
  }

  /* Spot-check first 16 bytes of bootcode and partition table */
  for (size_t i = 0; i < 16; ++i) {
    if (verify[i] != sector[i]) {
      return -1;
    }
  }
  for (size_t i = 446; i < 510; ++i) {
    if (verify[i] != sector[i]) {
      return -1;
    }
  }

  return 0;
}

/*
 * bootwriter_write_payloads
 *
 * Writes all boot payloads (Stage1, Stage2, Kernel) to the disk.
 * Calculates necessary LBAs based on the provided Boot Partition.
 */
int bootwriter_write_payloads(struct block_device *disk,
                              const struct mbr_partition *boot_part,
                              const struct boot_payload_set *payloads) {
  if (!disk || !boot_part || !payloads) {
    return -1;
  }
  if (!payloads->stage1.data || !payloads->stage2.data ||
      !payloads->kernel_main.data) {
    return -1;
  }

  /* Calculate Layout */
  uint32_t stage2_lba = boot_part->lba_start;
  uint32_t stage2_sectors = sectors_for_size(payloads->stage2.size);

  /* Kernel follows stage2 directly */
  uint32_t kernel_main_sectors = sectors_for_size(payloads->kernel_main.size);
  uint32_t kernel_main_lba = stage2_lba + stage2_sectors;

  /* Recovery kernel follows main kernel */
  uint32_t kernel_recovery_sectors =
      sectors_for_size(payloads->kernel_recovery.size);
  uint32_t kernel_recovery_lba = kernel_main_lba + kernel_main_sectors;

  /* Verify Partition Size */
  uint32_t total_needed =
      stage2_sectors + kernel_main_sectors + kernel_recovery_sectors;
  if (total_needed > boot_part->sector_count) {
    return -1;
  }

  /* Prepare Stage2 Buffer (Pad to sector boundary) */
  uint32_t stage2_bytes = payloads->stage2.size;
  uint32_t stage2_padded = stage2_sectors * 512;
  uint8_t *stage2_buf = (uint8_t *)kalloc(stage2_padded);
  if (!stage2_buf) {
    return -1;
  }

  /* Copy original stage2 data */
  for (uint32_t i = 0; i < stage2_padded; ++i) {
    stage2_buf[i] = (i < stage2_bytes) ? payloads->stage2.data[i] : 0;
  }

  /* Patching: Stage2 needs to know:
     1. Its own location (STAGE2_LBA, STAGE2_SECTORS)
     2. Kernel location (KERNEL_LBA, KERNEL_SECTORS)
  */
  int patch_err = 0;

  if (patch_u32(stage2_buf, stage2_padded, STAGE2_LBA_PLACEHOLDER,
                stage2_lba) != 0)
    patch_err++;
  if (patch_u32(stage2_buf, stage2_padded, STAGE2_SECTORS_PLACEHOLDER,
                stage2_sectors) != 0)
    patch_err++;
  if (patch_u32(stage2_buf, stage2_padded, KERNEL_LBA_PLACEHOLDER,
                kernel_main_lba) != 0)
    patch_err++;
  if (patch_u32(stage2_buf, stage2_padded, KERNEL_SECTORS_PLACEHOLDER,
                kernel_main_sectors) != 0)
    patch_err++;

  if (patch_err != 0) {
    /* If zero patches found? No, patch_u32 returns 0 on success (found > 0) and
       -1 on failure (found == 0). So if ANY return -1, we have a problem. Wait,
       patch_u32 returns 0 on success. So if (patch_u32(...) != 0) means
       failure. Correct.
    */
    kfree(stage2_buf);
    return -1;
  }

  /* Write Stage2 */
  if (write_sector_aligned(disk, stage2_lba, stage2_buf, stage2_padded) != 0) {
    kfree(stage2_buf);
    return -1;
  }
  kfree(stage2_buf);

  /* Write Kernel Main */
  if (write_sector_aligned(disk, kernel_main_lba, payloads->kernel_main.data,
                           payloads->kernel_main.size) != 0) {
    return -1;
  }

  /* Write Kernel Recovery (if valid) */
  if (payloads->kernel_recovery.data && payloads->kernel_recovery.size > 0) {
    if (write_sector_aligned(disk, kernel_recovery_lba,
                             payloads->kernel_recovery.data,
                             payloads->kernel_recovery.size) != 0) {
      return -1;
    }
  }

  /* Write Stage1 (MBR) */
  return bootwriter_write_stage1(disk, boot_part, stage2_lba, stage2_sectors,
                                 &payloads->stage1);
}

int bootwriter_write_config(struct block_device *disk,
                            const char *layout_name) {
  if (!disk || !layout_name)
    return -1;

  struct boot_config_sector cfg;
  for (size_t i = 0; i < sizeof(cfg); ++i)
    ((uint8_t *)&cfg)[i] = 0;

  cfg.magic = BOOT_CONFIG_MAGIC;
  for (size_t i = 0; i < sizeof(cfg.keyboard_layout) - 1 && layout_name[i];
       ++i) {
    cfg.keyboard_layout[i] = layout_name[i];
  }

  /* Write to LBA 1 (Sector 1) */
  return block_device_write(disk, BOOT_CONFIG_LBA, (const uint8_t *)&cfg);
}

/* Helper to write MBR with specific partition entries and correct signatures */
static int write_mbr_with_partitions(struct block_device *dev,
                                     const struct mbr_partition *boot,
                                     const struct mbr_partition *data) {
  uint8_t mbr[512];
  for (size_t i = 0; i < 512; i++)
    mbr[i] = 0;

  /* Partition 1: BOOT */
  /* Offset 446 (0x1BE) */
  uint8_t *p1 = &mbr[446];
  p1[0] = boot->bootable;
  p1[4] = boot->type; // System ID

  /* LBA Start (4 bytes) at offset 8 */
  p1[8] = (uint8_t)(boot->lba_start & 0xFF);
  p1[9] = (uint8_t)((boot->lba_start >> 8) & 0xFF);
  p1[10] = (uint8_t)((boot->lba_start >> 16) & 0xFF);
  p1[11] = (uint8_t)((boot->lba_start >> 24) & 0xFF);

  /* Sector Count (4 bytes) at offset 12 */
  p1[12] = (uint8_t)(boot->sector_count & 0xFF);
  p1[13] = (uint8_t)((boot->sector_count >> 8) & 0xFF);
  p1[14] = (uint8_t)((boot->sector_count >> 16) & 0xFF);
  p1[15] = (uint8_t)((boot->sector_count >> 24) & 0xFF);

  /* Partition 2: DATA */
  /* Offset 462 (0x1CE) */
  uint8_t *p2 = &mbr[462];
  p2[0] = data->bootable;
  p2[4] = data->type;

  p2[8] = (uint8_t)(data->lba_start & 0xFF);
  p2[9] = (uint8_t)((data->lba_start >> 8) & 0xFF);
  p2[10] = (uint8_t)((data->lba_start >> 16) & 0xFF);
  p2[11] = (uint8_t)((data->lba_start >> 24) & 0xFF);

  p2[12] = (uint8_t)(data->sector_count & 0xFF);
  p2[13] = (uint8_t)((data->sector_count >> 8) & 0xFF);
  p2[14] = (uint8_t)((data->sector_count >> 16) & 0xFF);
  p2[15] = (uint8_t)((data->sector_count >> 24) & 0xFF);

  /* MBR Signature */
  mbr[510] = 0x55;
  mbr[511] = 0xAA;

  if (block_device_write(dev, 0, mbr) != 0) {
    return -1;
  }
  return 0;
}

/*
 * bootwriter_partition_disk
 *
 * Calculates a new partition table with a BOOT partition and the rest for DATA.
 * Returns the calculated partition structures.
 */
int bootwriter_partition_disk(struct block_device *disk, uint32_t boot_mb,
                              struct mbr_partition *out_boot_part,
                              struct mbr_partition *out_data_part) {
  if (!disk || disk->block_size != 512 || disk->block_count < 8192) {
    // Enforce min size (~4MB for safety, though FS needs more)
    return -1;
  }

  const uint32_t align = 2048;          // 1MiB check alignment
  const uint32_t min_boot_secs = 32768; // 16MiB min boot
  const uint32_t data_min_secs = 32768; // 16MiB min data

  uint32_t total = disk->block_count;

  /* Calculate Boot Partition Size */
  uint32_t boot_secs = (boot_mb * 1024 * 1024) / 512;
  if (boot_secs < min_boot_secs)
    boot_secs = min_boot_secs;

  /* Alignment checks */
  uint32_t boot_start = align;
  uint32_t data_start = boot_start + boot_secs;

  // Align data start to next 1MB boundary
  if (data_start % align != 0) {
    data_start += (align - (data_start % align));
  }

  /* Check Capacity */
  if (data_start + data_min_secs > total) {
    // Not enough space for data
    return -1;
  }

  uint32_t data_secs = total - data_start;

  /* Fill Output Structures */
  if (out_boot_part) {
    out_boot_part->bootable = 0x80; // Active
    out_boot_part->type = 0xDA;     // Non-FS / Custom
    out_boot_part->lba_start = boot_start;
    out_boot_part->sector_count = boot_secs;
  }
  if (out_data_part) {
    out_data_part->bootable = 0x00;
    out_data_part->type = 0x83; // Linux/Data
    out_data_part->lba_start = data_start;
    out_data_part->sector_count = data_secs;
  }

  /* Commit MBR to disk */
  // We create temporary local copies if caller passed NULL keys (unlikely but
  // safe)
  struct mbr_partition b = {0x80, 0xDA, boot_start, boot_secs};
  struct mbr_partition d = {0x00, 0x83, data_start, data_secs};

  return write_mbr_with_partitions(disk, &b, &d);
}

/*
 * bootwriter_install_fresh
 *
 * High-level function that performs a complete fresh installation:
 * 1. Partitions the disk.
 * 2. Installs all boot payloads with correct patching.
 * 3. Returns the DATA partition for formatting.
 */
int bootwriter_install_fresh(struct block_device *disk, uint32_t boot_mb,
                             struct mbr_partition *out_data_part,
                             const struct boot_payload_set *payloads) {
  if (!payloads)
    return -1;

  struct mbr_partition boot_part;
  struct mbr_partition data_part;

  /* 1. Partition the disk */
  if (bootwriter_partition_disk(disk, boot_mb, &boot_part, &data_part) != 0) {
    return -1;
  }

  /* 2. Install Bootloader */
  if (bootwriter_write_payloads(disk, &boot_part, payloads) != 0) {
    return -1;
  }

  /* 3. Return Data partition info for formatting */
  if (out_data_part) {
    *out_data_part = data_part;
  }

  return 0;
}
