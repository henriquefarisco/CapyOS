/* boot_writer.c: disk layout and boot payload writer used by installer. */
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

static void dbg_print_u32(const char *label, uint32_t v) {
  extern void vga_write(const char *s);
  char t[16];
  int k = 0;
  if (!v)
    t[k++] = '0';
  while (v) {
    t[k++] = (v % 10) + '0';
    v /= 10U;
  }
  vga_write(label);
  while (k > 0) {
    char c[2] = {t[--k], 0};
    vga_write(c);
  }
  vga_write("\n");
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

/* Patch a fixed offset (little-endian u32). Used for stage2 header to avoid
 * altering placeholder immediates in the code section. */
static int patch_header_u32(uint8_t *buf, size_t len, size_t offset,
                            uint32_t value) {
  if (!buf || offset + 4 > len) {
    return -1;
  }
  buf[offset] = (uint8_t)(value & 0xFF);
  buf[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
  buf[offset + 2] = (uint8_t)((value >> 16) & 0xFF);
  buf[offset + 3] = (uint8_t)((value >> 24) & 0xFF);
  return 0;
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

/* Zero a contiguous range of sectors, clamping to disk size. */
static int wipe_range(struct block_device *disk, uint32_t start_lba,
                      uint32_t sectors) {
  if (!disk || sectors == 0) {
    return 0;
  }
  if (start_lba >= disk->block_count || disk->block_size == 0) {
    return -1;
  }
  uint32_t max_sectors = disk->block_count - start_lba;
  if (sectors > max_sectors) {
    sectors = max_sectors;
  }
  uint8_t *zero = (uint8_t *)kalloc(disk->block_size);
  if (!zero) {
    return -1;
  }
  for (uint32_t i = 0; i < disk->block_size; ++i) {
    zero[i] = 0;
  }
  for (uint32_t i = 0; i < sectors; ++i) {
    if (block_device_write(disk, start_lba + i, zero) != 0) {
      kfree(zero);
      return -1;
    }
  }
  kfree(zero);
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

  /* DEBUG: Show Stage1 patching params */
  dbg_print_u32("[s1] LBA: ", stage2_lba);
  dbg_print_u32("[s1] Sec: ", stage2_sectors);
  /* Preserva tabela de partiÃ§Ãµes existente. */
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

  /* DEBUG: Verify Stage1 has correct LBA on disk */
  {
    int found = 0;
    for (int i = 0; i < 512 - 4; ++i) {
      uint32_t v = (uint32_t)verify[i] | ((uint32_t)verify[i + 1] << 8) |
                   ((uint32_t)verify[i + 2] << 16) |
                   ((uint32_t)verify[i + 3] << 24);
      if (v == stage2_lba)
        found++;
    }
    dbg_print_u32("[s1] Disk LBA found: ", found);
  }

  /* DEBUG: Verify Stage1 has correct sector count on disk */
  {
    int found = 0;
    for (int i = 0; i < 512 - 2; ++i) {
      uint16_t v = (uint16_t)verify[i] | ((uint16_t)verify[i + 1] << 8);
      if (v == (uint16_t)stage2_sectors)
        found++;
    }
    dbg_print_u32("[s1] Disk Sec found: ", found);
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
  uint32_t manifest_sectors = sectors_for_size(sizeof(struct boot_manifest));
  uint32_t manifest_lba = stage2_lba + stage2_sectors;

  /* Kernel follows manifest (stage2 + manifest) */
  uint32_t kernel_main_sectors = sectors_for_size(payloads->kernel_main.size);

  /* DEBUG: Check for zero size */
  dbg_print_u32("[wr] Kernel Size: ", payloads->kernel_main.size);
  if (kernel_main_sectors == 0) {
    dbg_print_u32("[wr] WARNING: 0 Sectors! Forcing 1.", 0);
    kernel_main_sectors = 1;
  }

  uint32_t kernel_main_lba = manifest_lba + manifest_sectors;

  /* Recovery kernel follows main kernel */
  uint32_t kernel_recovery_sectors =
      sectors_for_size(payloads->kernel_recovery.size);
  uint32_t kernel_recovery_lba = kernel_main_lba + kernel_main_sectors;

  /* Verify Partition Size */
  uint32_t total_needed = stage2_sectors + manifest_sectors +
                          kernel_main_sectors + kernel_recovery_sectors;
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

  /* DEBUG: Trace values before patching */
  /* DEBUG: Trace values before patching */
  dbg_print_u32("[wr] S2 LBA: ", stage2_lba);
  dbg_print_u32("[wr] S2 Sec: ", stage2_sectors);
  dbg_print_u32("[wr] Mf LBA: ", manifest_lba);
  dbg_print_u32("[wr] Mf Sec: ", manifest_sectors);
  dbg_print_u32("[wr] Kr LBA: ", kernel_main_lba);
  dbg_print_u32("[wr] Kr Sec: ", kernel_main_sectors);

  /* Patching: Stage2 needs to know:
     1. Its own location (STAGE2_LBA, STAGE2_SECTORS)
     2. Kernel location (KERNEL_LBA, KERNEL_SECTORS)
   */
  if (patch_header_u32(stage2_buf, stage2_padded, 0x04, kernel_main_sectors) !=
          0 ||
      patch_header_u32(stage2_buf, stage2_padded, 0x08, kernel_main_lba) != 0 ||
      patch_header_u32(stage2_buf, stage2_padded, 0x0C, stage2_lba) != 0 ||
      patch_header_u32(stage2_buf, stage2_padded, 0x10, stage2_sectors) != 0) {
    kfree(stage2_buf);
    return -1;
  }

  /* DEBUG: Verify patching actually happened in memory */
  int verify_ok = 0;
  for (uint32_t i = 0; i < stage2_padded - 4; ++i) {
    uint32_t v = (uint32_t)stage2_buf[i] | ((uint32_t)stage2_buf[i + 1] << 8) |
                 ((uint32_t)stage2_buf[i + 2] << 16) |
                 ((uint32_t)stage2_buf[i + 3] << 24);
    if (v == kernel_main_lba)
      verify_ok++;
  }
  if (verify_ok)
    dbg_print_u32("[wr] Verify OK count: ", verify_ok);
  else
    dbg_print_u32("[wr] Verify FAILED for val: ", kernel_main_lba);

  /* Write Stage2 */
  if (write_sector_aligned(disk, stage2_lba, stage2_buf, stage2_padded) != 0) {
    kfree(stage2_buf);
    return -1;
  }

  /* DEBUG: Read back Stage2 SECTOR 0 from disk (Header at offset 0-16) */
  {
    uint8_t disk_check[512];
    if (block_device_read(disk, stage2_lba, disk_check) == 0) {
      int found_hdr = 0;
      /* Check kernel_sectors at offset 4 */
      uint32_t ks = (uint32_t)disk_check[4] | ((uint32_t)disk_check[5] << 8) |
                    ((uint32_t)disk_check[6] << 16) |
                    ((uint32_t)disk_check[7] << 24);
      /* Check kernel_lba at offset 8 */
      uint32_t kl = (uint32_t)disk_check[8] | ((uint32_t)disk_check[9] << 8) |
                    ((uint32_t)disk_check[10] << 16) |
                    ((uint32_t)disk_check[11] << 24);

      dbg_print_u32("[wr] Dsk: KS=", ks);
      dbg_print_u32("[wr] Dsk: KL=", kl);

      if (ks == kernel_main_sectors && kl == kernel_main_lba) {
        found_hdr = 1;
      }
      dbg_print_u32("[wr] Disk Header Valid: ", found_hdr);
    }
  }

  /* Build and write boot manifest right after stage2. */
  struct boot_manifest manifest;
  boot_manifest_init(&manifest);
  uint32_t main_sum =
      boot_checksum32(payloads->kernel_main.data, payloads->kernel_main.size);
  boot_manifest_add(&manifest, BOOT_ENTRY_NORMAL, kernel_main_lba,
                    kernel_main_sectors, main_sum);
  if (payloads->kernel_recovery.data && payloads->kernel_recovery.size > 0) {
    uint32_t rec_sum = boot_checksum32(payloads->kernel_recovery.data,
                                       payloads->kernel_recovery.size);
    boot_manifest_add(&manifest, BOOT_ENTRY_RECOVERY, kernel_recovery_lba,
                      kernel_recovery_sectors, rec_sum);
  }

  if (write_sector_aligned(disk, manifest_lba, (const uint8_t *)&manifest,
                           sizeof(manifest)) != 0) {
    kfree(stage2_buf);
    return -1;
  }

  kfree(stage2_buf);

  /* Write Kernel Main */
  if (write_sector_aligned(disk, kernel_main_lba, payloads->kernel_main.data,
                           payloads->kernel_main.size) != 0) {
    return -1;
  }

  /* Verify Kernel Write (Magic Check) */
  uint8_t verify_buf[512];
  if (block_device_read(disk, kernel_main_lba, verify_buf) != 0) {
    return -1;
  }
  if (verify_buf[0] != 0x7F || verify_buf[1] != 'E' || verify_buf[2] != 'L' ||
      verify_buf[3] != 'F') {
    /* Verification Failed: Written data corrupted or empty */
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
  for (size_t i = 0; i < sizeof(cfg); ++i) {
    ((uint8_t *)&cfg)[i] = 0;
  }

  /* Preserve previously provisioned fields (e.g. volume key) when possible. */
  struct boot_config_sector existing;
  int has_existing = 0;
  if (block_device_read(disk, BOOT_CONFIG_LBA, (uint8_t *)&existing) == 0 &&
      existing.magic == BOOT_CONFIG_MAGIC) {
    has_existing = 1;
    for (size_t i = 0; i < sizeof(cfg); ++i) {
      ((uint8_t *)&cfg)[i] = ((const uint8_t *)&existing)[i];
    }
  }

  cfg.magic = BOOT_CONFIG_MAGIC;
  cfg.version = BOOT_CONFIG_VERSION;
  if (!has_existing) {
    cfg.flags = 0;
  }
  for (size_t i = 0; i < sizeof(cfg.keyboard_layout); ++i) {
    cfg.keyboard_layout[i] = 0;
  }
  for (size_t i = 0; i < sizeof(cfg.keyboard_layout) - 1 && layout_name[i];
       ++i) {
    cfg.keyboard_layout[i] = layout_name[i];
  }

  /* Write to LBA 1 (Sector 1) */
  return block_device_write(disk, BOOT_CONFIG_LBA, (const uint8_t *)&cfg);
}

/* Helper to write MBR with specific partition entries and correct signatures */
/* Helper to write MBR with specific partition entries and correct signatures */
static int write_mbr_with_partitions(struct block_device *dev,
                                     const struct mbr_partition *p1,
                                     const struct mbr_partition *p2,
                                     const struct mbr_partition *p3,
                                     const struct mbr_partition *p4) {
  uint8_t mbr[512];
  for (size_t i = 0; i < 512; i++)
    mbr[i] = 0;

  /* Helper lambda-like macro or just loop */
  const struct mbr_partition *parts[4] = {p1, p2, p3, p4};

  for (int i = 0; i < 4; ++i) {
    uint8_t *entry = &mbr[446 + (i * 16)];
    if (parts[i]) {
      entry[0] = parts[i]->bootable;
      entry[4] = parts[i]->type;

      entry[8] = (uint8_t)(parts[i]->lba_start & 0xFF);
      entry[9] = (uint8_t)((parts[i]->lba_start >> 8) & 0xFF);
      entry[10] = (uint8_t)((parts[i]->lba_start >> 16) & 0xFF);
      entry[11] = (uint8_t)((parts[i]->lba_start >> 24) & 0xFF);

      entry[12] = (uint8_t)(parts[i]->sector_count & 0xFF);
      entry[13] = (uint8_t)((parts[i]->sector_count >> 8) & 0xFF);
      entry[14] = (uint8_t)((parts[i]->sector_count >> 16) & 0xFF);
      entry[15] = (uint8_t)((parts[i]->sector_count >> 24) & 0xFF);
    }
  }

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
                              uint32_t system_mb,
                              struct mbr_partition *out_boot_part,
                              struct mbr_partition *out_system_part,
                              struct mbr_partition *out_data_part) {
  if (!disk || disk->block_size != 512 || disk->block_count < 8192) {
    // Enforce min size (~4MB for safety)
    return -1;
  }

  const uint32_t align = 2048;          // 1MiB alignment
  const uint32_t min_boot_secs = 32768; // 16MiB min boot
  // System partition optional, but if present enforce min size? No, user might
  // want small one. Data partition needs reasonable space
  const uint32_t min_data_secs = 32768;

  uint32_t total = disk->block_count;

  /* 1. Boot Partition */
  uint32_t boot_secs = (boot_mb * 1024 * 1024) / 512;
  if (boot_secs < min_boot_secs)
    boot_secs = min_boot_secs;

  uint32_t boot_start = align;
  uint32_t system_start = boot_start + boot_secs;

  // Align System Start
  if (system_start % align != 0) {
    system_start += (align - (system_start % align));
  }

  /* 2. System Partition */
  uint32_t system_secs = 0;
  if (system_mb > 0) {
    system_secs = (system_mb * 1024 * 1024) / 512;
  }
  uint32_t data_start = system_start + system_secs;

  // Align Data Start
  if (data_start % align != 0) {
    data_start += (align - (data_start % align));
  }

  /* 3. Data Partition check */
  if (data_start + min_data_secs > total) {
    return -1; // Not enough space
  }
  uint32_t data_secs = total - data_start;

  /* Fill Output Structures */
  struct mbr_partition p_boot = {0};
  p_boot.bootable = 0x80;
  p_boot.type = PARTITION_TYPE_CAPYOS_BOOT;
  p_boot.lba_start = boot_start;
  p_boot.sector_count = boot_secs;

  struct mbr_partition p_system = {0};
  if (system_secs > 0) {
    p_system.bootable = 0x00;
    p_system.type = PARTITION_TYPE_LINUX; // Or custom
    p_system.lba_start = system_start;
    p_system.sector_count = system_secs;
  }

  struct mbr_partition p_data = {0};
  p_data.bootable = 0x00;
  p_data.type =
      PARTITION_TYPE_LINUX; // Or usually encrypted custom type if needed
  p_data.lba_start = data_start;
  p_data.sector_count = data_secs;

  if (out_boot_part)
    *out_boot_part = p_boot;
  if (out_system_part)
    *out_system_part = p_system;
  if (out_data_part)
    *out_data_part = p_data;

  /* Commit MBR (P1=Boot, P2=System, P3=Data, P4=Empty) */
  return write_mbr_with_partitions(
      disk, &p_boot, (system_secs > 0) ? &p_system : NULL, &p_data, NULL);
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
                             uint32_t system_mb,
                             struct mbr_partition *out_data_part,
                             const struct boot_payload_set *payloads) {
  if (!disk || !payloads)
    return -1;

  /* Wipe head/tail sectors to remove stale bootloaders/GPT backups before
   * partitioning. */
  const uint32_t wipe_span = 4096; // 2 MiB @ 512B
  uint32_t head_wipe = (disk->block_count < wipe_span) ? disk->block_count
                                                       : wipe_span;
  if (wipe_range(disk, 0, head_wipe) != 0) {
    return -1;
  }
  if (disk->block_count > head_wipe) {
    uint32_t tail_wipe =
        (disk->block_count < wipe_span) ? disk->block_count : wipe_span;
    uint32_t tail_start = disk->block_count - tail_wipe;
    if (tail_start >= head_wipe) {
      if (wipe_range(disk, tail_start, tail_wipe) != 0) {
        return -1;
      }
    }
  }

  struct mbr_partition boot_part;
  struct mbr_partition system_part; // unused for now but we get it
  struct mbr_partition data_part;

  /* 1. Partition the disk */
  if (bootwriter_partition_disk(disk, boot_mb, system_mb, &boot_part,
                                &system_part, &data_part) != 0) {
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
