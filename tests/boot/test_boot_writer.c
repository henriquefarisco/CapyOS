#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boot/boot_manifest.h"
#include "boot/boot_writer.h"
#include "fs/block.h"

struct mem_disk {
  uint32_t block_size;
  uint32_t block_count;
  uint8_t *data;
};

static int mem_read(void *ctx, uint32_t block_no, void *buffer) {
  struct mem_disk *m = (struct mem_disk *)ctx;
  if (block_no >= m->block_count)
    return -1;
  memcpy(buffer, m->data + block_no * m->block_size, m->block_size);
  return 0;
}
static int mem_write(void *ctx, uint32_t block_no, const void *buffer) {
  struct mem_disk *m = (struct mem_disk *)ctx;
  if (block_no >= m->block_count)
    return -1;
  memcpy(m->data + block_no * m->block_size, buffer, m->block_size);
  return 0;
}

static struct block_device_ops mem_ops = {
    .read_block = mem_read,
    .write_block = mem_write,
};

static int test_bootwriter_basic(void) {
  struct mem_disk mem;
  mem.block_size = 512;
  mem.block_count = 8192;
  mem.data = (uint8_t *)calloc(mem.block_count, mem.block_size);
  if (!mem.data)
    return 1;

  struct block_device disk = {
      .name = "mem",
      .block_size = 512,
      .block_count = mem.block_count,
      .ctx = &mem,
      .ops = &mem_ops,
  };

  struct boot_payload_set set = {0};
  uint8_t stage1[512];
  memset(stage1, 0, sizeof(stage1));
  /* place LBA placeholder (0xDEADBEEF) little-endian at offset 0 */
  stage1[0] = 0xEF;
  stage1[1] = 0xBE;
  stage1[2] = 0xAD;
  stage1[3] = 0xDE;
  /* place sectors placeholder (0xBEEF) little-endian at offset 4 */
  stage1[4] = 0xEF;
  stage1[5] = 0xBE;
  stage1[510] = 0x55;
  stage1[511] = 0xAA;
  uint8_t stage2[1024];
  memset(stage2, 0, sizeof(stage2));
  /* Replicate real stage2 header layout (offsets 4/8/12/16) */
  *(uint32_t *)(stage2 + 4) = 0xBADC0FFE;   // kernel_sectors
  *(uint32_t *)(stage2 + 8) = 0xFEEDFACE;   // kernel_lba
  *(uint32_t *)(stage2 + 12) = 0xDEADBEEF;  // stage2_lba
  *(uint32_t *)(stage2 + 16) = 0xCAFEBABE;  // stage2_sectors
  uint8_t kmain[600];
  for (size_t i = 0; i < sizeof(kmain); ++i)
    kmain[i] = (uint8_t)i;
  /* fake ELF magic so bootwriter verification passes */
  kmain[0] = 0x7F;
  kmain[1] = 'E';
  kmain[2] = 'L';
  kmain[3] = 'F';
  uint8_t krec[512];
  memset(krec, 0xAA, sizeof(krec));

  set.stage1.data = stage1;
  set.stage1.size = sizeof(stage1);
  set.stage2.data = stage2;
  set.stage2.size = sizeof(stage2);
  set.kernel_main.data = kmain;
  set.kernel_main.size = sizeof(kmain);
  set.kernel_recovery.data = krec;
  set.kernel_recovery.size = sizeof(krec);

  struct mbr_partition bootp = {0};
  bootp.lba_start = 2048;
  bootp.sector_count = 4096;

  int rc = bootwriter_write_payloads(&disk, &bootp, &set);
  if (rc != 0) {
    printf("[bootwriter] retorno %d\n", rc);
    free(mem.data);
    return 1;
  }
  uint8_t mbr[512];
  mem_read(&mem, 0, mbr);
  uint32_t patched = (uint32_t)mbr[0] | ((uint32_t)mbr[1] << 8) |
                     ((uint32_t)mbr[2] << 16) | ((uint32_t)mbr[3] << 24);
  if (patched != bootp.lba_start) {
    printf("[bootwriter] stage1 nao recebeu LBA (0x%x)\n", patched);
    free(mem.data);
    return 1;
  }

  /* Verify Stage2 Kernel Patching (Direct Boot) */
  uint8_t stage2_read[1024];
  mem_read(&mem, bootp.lba_start, stage2_read);

  uint32_t k_sectors =
      (uint32_t)stage2_read[4] | ((uint32_t)stage2_read[5] << 8) |
      ((uint32_t)stage2_read[6] << 16) | ((uint32_t)stage2_read[7] << 24);
  uint32_t k_lba = (uint32_t)stage2_read[8] | ((uint32_t)stage2_read[9] << 8) |
                   ((uint32_t)stage2_read[10] << 16) |
                   ((uint32_t)stage2_read[11] << 24);
  uint32_t s2_lba = (uint32_t)stage2_read[12] |
                    ((uint32_t)stage2_read[13] << 8) |
                    ((uint32_t)stage2_read[14] << 16) |
                    ((uint32_t)stage2_read[15] << 24);
  uint32_t s2_secs =
      (uint32_t)stage2_read[16] | ((uint32_t)stage2_read[17] << 8) |
      ((uint32_t)stage2_read[18] << 16) | ((uint32_t)stage2_read[19] << 24);

  uint32_t expected_klba = bootp.lba_start + (sizeof(stage2) + 511) / 512;
  uint32_t manifest_secs = (sizeof(struct boot_manifest) + 511) / 512;
  expected_klba += manifest_secs;
  uint32_t expected_ksectors = (sizeof(kmain) + 511) / 512;

  if (k_lba != expected_klba) {
    printf("[bootwriter] kernel LBA incorrect: %u != %u\n", k_lba,
           expected_klba);
    free(mem.data);
    return 1;
  }
  if (k_sectors != expected_ksectors) {
    printf("[bootwriter] kernel sectors incorrect: %u != %u\n", k_sectors,
           expected_ksectors);
    free(mem.data);
    return 1;
  }
  if (s2_lba != bootp.lba_start || s2_secs != (sizeof(stage2) + 511) / 512) {
    printf("[bootwriter] stage2 header mismatch\n");
    free(mem.data);
    return 1;
  }

  /* Verify manifest */
  uint8_t manifest_buf[512];
  uint32_t manifest_lba = bootp.lba_start + (sizeof(stage2) + 511) / 512;
  mem_read(&mem, manifest_lba, manifest_buf);
  struct boot_manifest *mf = (struct boot_manifest *)manifest_buf;
  if (mf->magic != BOOT_MANIFEST_MAGIC || mf->entry_count == 0) {
    printf("[bootwriter] manifest missing or invalid\n");
    free(mem.data);
    return 1;
  }
  if (mf->entries[0].lba_start != expected_klba ||
      mf->entries[0].sector_count != expected_ksectors) {
    printf("[bootwriter] manifest entry mismatch\n");
    free(mem.data);
    return 1;
  }

  /* Verify Kernel Was Written */
  uint8_t kverify[512];
  mem_read(&mem, k_lba, kverify);
  if (kverify[0] != 0x7F || kverify[1] != 'E' || kverify[2] != 'L' ||
      kverify[3] != 'F') {
    printf("[bootwriter] kernel data verify failed\n");
    free(mem.data);
    return 1;
  }
  free(mem.data);
  return 0;
}

static int test_bootwriter_config(void) {
  struct mem_disk mem;
  mem.block_size = 512;
  mem.block_count = 100;
  mem.data = (uint8_t *)calloc(mem.block_count, mem.block_size);
  if (!mem.data)
    return 1;

  struct block_device disk = {
      .name = "memtest",
      .block_size = 512,
      .block_count = mem.block_count,
      .ctx = &mem,
      .ops = &mem_ops,
  };

  const char *layout = "br-abnt2";
  if (bootwriter_write_config(&disk, layout) != 0) {
    printf("[test_config] write failed\n");
    free(mem.data);
    return 1;
  }

  uint8_t sector[512];
  mem_read(&mem, 1, sector); // LBA 1
  const struct boot_config_sector *cfg =
      (const struct boot_config_sector *)sector;
  uint32_t magic = cfg->magic;

  if (magic != 0xB001CF61) {
    printf("[test_config] magic mismatch: 0x%x\n", magic);
    free(mem.data);
    return 1;
  }

  if (cfg->version != BOOT_CONFIG_VERSION) {
    printf("[test_config] version mismatch: %u != %u\n", (unsigned)cfg->version,
           (unsigned)BOOT_CONFIG_VERSION);
    free(mem.data);
    return 1;
  }

  if (strncmp(cfg->keyboard_layout, layout, strlen(layout)) != 0) {
    printf("[test_config] layout string mismatch\n");
    free(mem.data);
    return 1;
  }

  free(mem.data);
  return 0;
}

static int test_bootwriter_config_preserves_volume_key(void) {
  struct mem_disk mem;
  mem.block_size = 512;
  mem.block_count = 100;
  mem.data = (uint8_t *)calloc(mem.block_count, mem.block_size);
  if (!mem.data)
    return 1;

  struct block_device disk = {
      .name = "memtest-preserve",
      .block_size = 512,
      .block_count = mem.block_count,
      .ctx = &mem,
      .ops = &mem_ops,
  };

  struct boot_config_sector seeded;
  memset(&seeded, 0, sizeof(seeded));
  seeded.magic = BOOT_CONFIG_MAGIC;
  seeded.version = BOOT_CONFIG_VERSION;
  seeded.flags = BOOT_CONFIG_FLAG_HAS_VOLUME_KEY;
  strncpy(seeded.keyboard_layout, "us", sizeof(seeded.keyboard_layout) - 1);
  strncpy(seeded.volume_key, "A1B2C3D4E5F60718293A4B5C",
          sizeof(seeded.volume_key) - 1);
  if (mem_write(&mem, BOOT_CONFIG_LBA, &seeded) != 0) {
    printf("[test_config_preserve] seed write failed\n");
    free(mem.data);
    return 1;
  }

  const char *layout = "br-abnt2";
  if (bootwriter_write_config(&disk, layout) != 0) {
    printf("[test_config_preserve] write failed\n");
    free(mem.data);
    return 1;
  }

  uint8_t sector[512];
  mem_read(&mem, BOOT_CONFIG_LBA, sector);
  const struct boot_config_sector *cfg =
      (const struct boot_config_sector *)sector;

  if (cfg->magic != BOOT_CONFIG_MAGIC ||
      cfg->version != BOOT_CONFIG_VERSION) {
    printf("[test_config_preserve] header mismatch\n");
    free(mem.data);
    return 1;
  }

  if ((cfg->flags & BOOT_CONFIG_FLAG_HAS_VOLUME_KEY) == 0) {
    printf("[test_config_preserve] key flag was cleared\n");
    free(mem.data);
    return 1;
  }

  if (strncmp(cfg->volume_key, seeded.volume_key, sizeof(cfg->volume_key)) != 0) {
    printf("[test_config_preserve] volume key changed unexpectedly\n");
    free(mem.data);
    return 1;
  }

  if (strncmp(cfg->keyboard_layout, layout, strlen(layout)) != 0) {
    printf("[test_config_preserve] layout was not updated\n");
    free(mem.data);
    return 1;
  }

  free(mem.data);
  return 0;
}

static int test_bootwriter_partitioning(void) {
  struct mem_disk mem;
  mem.block_size = 512;
  mem.block_count = 200000; // ~100 MB
  mem.data = (uint8_t *)calloc(mem.block_count, mem.block_size);
  if (!mem.data)
    return 1;

  struct block_device disk = {
      .name = "mempart",
      .block_size = 512,
      .block_count = mem.block_count,
      .ctx = &mem,
      .ops = &mem_ops,
  };

  struct mbr_partition boot_p, sys_p, data_p;
  // Request 20MB boot, 10MB system
  int rc = bootwriter_partition_disk(&disk, 20, 10, &boot_p, &sys_p, &data_p);
  if (rc != 0) {
    printf("[test_partitioning] returned %d\n", rc);
    free(mem.data);
    return 1;
  }

  // Check Boot (20MB = 40960 sectors)
  // min_boot_secs is 32768 (16MB). 20MB > 16MB.
  uint32_t expected_boot = (20 * 1024 * 1024) / 512;
  if (boot_p.sector_count != expected_boot) {
    printf("[test_partitioning] boot size mismatch: %u != %u\n",
           boot_p.sector_count, expected_boot);
    free(mem.data);
    return 1;
  }
  if (boot_p.type != PARTITION_TYPE_CAPYOS_BOOT || boot_p.bootable != 0x80) {
    printf("[test_partitioning] boot flags/type malformed\n");
    free(mem.data);
    return 1;
  }

  // Check System (10MB = 20480 sectors)
  uint32_t expected_sys = (10 * 1024 * 1024) / 512;
  if (sys_p.sector_count != expected_sys) {
    printf("[test_partitioning] sys size mismatch: %u != %u\n",
           sys_p.sector_count, expected_sys);
    free(mem.data);
    return 1;
  }
  if (sys_p.type != PARTITION_TYPE_LINUX) {
    printf("[test_partitioning] sys type mismatch\n");
    free(mem.data);
    return 1;
  }

  // Verify MBR content
  uint8_t mbr[512];
  mem_read(&mem, 0, mbr);

  // P1 (Offset 446)
  if (mbr[446 + 4] != PARTITION_TYPE_CAPYOS_BOOT) {
    printf("[test_partitioning] MBR P1 type wrong\n");
    free(mem.data);
    return 1;
  }
  // P2 (Offset 462)
  if (mbr[462 + 4] != PARTITION_TYPE_LINUX) {
    printf("[test_partitioning] MBR P2 type wrong\n");
    free(mem.data);
    return 1;
  }
  // P3 (Offset 478)
  if (mbr[478 + 4] != PARTITION_TYPE_LINUX) {
    printf("[test_partitioning] MBR P3 type wrong\n");
    free(mem.data);
    return 1;
  }

  free(mem.data);
  return 0;
}

static int test_bootwriter_patching(void) {
  struct mem_disk mem;
  mem.block_size = 512;
  mem.block_count = 20000;
  mem.data = (uint8_t *)calloc(mem.block_count, mem.block_size);
  if (!mem.data)
    return 1;

  struct block_device disk = {
      .name = "mempatch",
      .block_size = 512,
      .block_count = mem.block_count,
      .ctx = &mem,
      .ops = &mem_ops,
  };

  // Mock Payloads
  // Mock Payloads
  uint8_t s1[512];
  memset(s1, 0x90, 512);              // NOPs
  *(uint32_t *)(s1 + 0) = 0xDEADBEEF; // Stage2 LBA placeholder
  *(uint16_t *)(s1 + 4) = 0xBEEF;     // Stage2 Sectors placeholder
  s1[510] = 0x55;
  s1[511] = 0xAA; // MBR Signature

  // Stage 2 needs to be large enough to contain placeholders?
  // boot_writer.c replaces placeholders. The test payloads must HAVE
  // placeholders. Real stage2 has them. Our mock payloads in tests usually
  // don't? We need to construct a stage2 buffer with placeholders.
  uint8_t s2[2048];
  memset(s2, 0, 2048);
  /* Placeholders in the same offsets used by stage2.asm header */
  *(uint32_t *)(s2 + 4) = 0xBADC0FFE;   // kernel_sectors
  *(uint32_t *)(s2 + 8) = 0xFEEDFACE;   // kernel_lba
  *(uint32_t *)(s2 + 12) = 0xDEADBEEF;  // stage2_lba
  *(uint32_t *)(s2 + 16) = 0xCAFEBABE;  // stage2_sectors

  uint8_t k[4096];
  memset(k, 0xCC, 4096); // Kernel
  k[0] = 0x7F; k[1] = 'E'; k[2] = 'L'; k[3] = 'F';

  struct boot_payload_set payloads;
  payloads.stage1.data = s1;
  payloads.stage1.size = 512;
  payloads.stage2.data = s2;
  payloads.stage2.size = 2048;
  payloads.kernel_main.data = k;
  payloads.kernel_main.size = 4096; // 8 sectors
  payloads.kernel_recovery.data = NULL;
  payloads.kernel_recovery.size = 0;

  struct mbr_partition boot_p;
  boot_p.lba_start = 2048;
  boot_p.sector_count = 1000;

  if (bootwriter_write_payloads(&disk, &boot_p, &payloads) != 0) {
    printf("[test_patching] write_payloads failed\n");
    free(mem.data);
    return 1;
  }

  // Verify Stage 2 at LBA 2048
  uint8_t read_s2[2048];
  for (int i = 0; i < 4; i++) {
    mem_read(&mem, 2048 + i, read_s2 + i * 512);
  }

  // Check offsets
  uint32_t val_k_secs = *(uint32_t *)(read_s2 + 4);
  uint32_t val_k_lba = *(uint32_t *)(read_s2 + 8);
  uint32_t val_s2_lba = *(uint32_t *)(read_s2 + 12);
  uint32_t val_s2_secs = *(uint32_t *)(read_s2 + 16);

  if (val_s2_lba != 2048) {
    printf("[test_patching] Stage2 LBA mismatch: %x != 2048\n", val_s2_lba);
    free(mem.data);
    return 1;
  }
  if (val_s2_secs != 4) { // 2048 bytes = 4 sectors
    printf("[test_patching] Stage2 Sectors mismatch: %x != 4\n", val_s2_secs);
    free(mem.data);
    return 1;
  }

  // Kernel follows Stage 2 + manifest. Start = 2048 + 4 + 1 = 2053.
  if (val_k_lba != 2053) {
    printf("[test_patching] Kernel LBA mismatch: %x != 2053\n", val_k_lba);
    free(mem.data);
    return 1;
  }
  if (val_k_secs != 8) { // 4096 bytes = 8 sectors
    printf("[test_patching] Kernel Sectors mismatch: %x != 8\n", val_k_secs);
    free(mem.data);
    return 1;
  }

  free(mem.data);
  return 0;
}

int run_boot_writer_tests(void) {
  int fails = 0;
  fails += test_bootwriter_basic();
  fails += test_bootwriter_config();
  fails += test_bootwriter_config_preserves_volume_key();
  fails += test_bootwriter_partitioning();
  fails += test_bootwriter_patching();
  if (fails == 0) {
    printf("[tests] boot_writer OK\n");
  }
  return fails;
}

/* Stub for linker dependency */
struct boot_payload_set boot_embedded_payloads(void) {
  struct boot_payload_set s = {0};
  return s;
}
