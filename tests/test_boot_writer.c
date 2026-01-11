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
  /* place stage2 LBA placeholder (0xDEADBEEF) */
  stage2[0] = 0xEF;
  stage2[1] = 0xBE;
  stage2[2] = 0xAD;
  stage2[3] = 0xDE;
  /* place stage2 sectors placeholder (0xCAFEBABE) at offset 4 */
  stage2[4] = 0xBE;
  stage2[5] = 0xBA;
  stage2[6] = 0xFE;
  stage2[7] = 0xCA;
  /* place kernel LBA placeholder (0xFEEDFACE) at offset 8 */
  stage2[8] = 0xCE;
  stage2[9] = 0xFA;
  stage2[10] = 0xED;
  stage2[11] = 0xFE;
  /* place kernel sectors placeholder (0xBADC0FFE) at offset 12 */
  stage2[12] = 0xFE;
  stage2[13] = 0x0F;
  stage2[14] = 0xDC;
  stage2[15] = 0xBA;
  uint8_t kmain[600];
  for (size_t i = 0; i < sizeof(kmain); ++i)
    kmain[i] = (uint8_t)i;
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

  uint32_t k_lba = (uint32_t)stage2_read[8] | ((uint32_t)stage2_read[9] << 8) |
                   ((uint32_t)stage2_read[10] << 16) |
                   ((uint32_t)stage2_read[11] << 24);
  uint32_t k_sectors =
      (uint32_t)stage2_read[12] | ((uint32_t)stage2_read[13] << 8) |
      ((uint32_t)stage2_read[14] << 16) | ((uint32_t)stage2_read[15] << 24);

  uint32_t expected_klba = bootp.lba_start + (sizeof(stage2) + 511) / 512;
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

  /* Verify Kernel Was Written */
  uint8_t kverify[512];
  mem_read(&mem, k_lba, kverify);
  if (kverify[0] != 0x00 || kverify[1] != 0x01) {
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
  uint32_t magic = (uint32_t)sector[0] | ((uint32_t)sector[1] << 8) |
                   ((uint32_t)sector[2] << 16) | ((uint32_t)sector[3] << 24);

  if (magic != 0xB001CF61) {
    printf("[test_config] magic mismatch: 0x%x\n", magic);
    free(mem.data);
    return 1;
  }

  if (strncmp((char *)&sector[4], layout, strlen(layout)) != 0) {
    printf("[test_config] layout string mismatch\n");
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
