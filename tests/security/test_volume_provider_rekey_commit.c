#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs/block.h"
#include "fs/capyfs.h"
#include "security/crypt.h"
#include "security/volume_header.h"
#include "security/volume_provider.h"

#define REKEY_COMMIT_BLOCK_SIZE 4096u
#define REKEY_COMMIT_PASSWORD "alpha-230-passphrase"
#define REKEY_COMMIT_FAIL_LBA 0xFFFFFFFFu

struct rekey_commit_ram_dev {
  struct block_device dev;
  uint8_t *storage;
  uint32_t fail_write_lba;
  uint32_t corrupt_write_lba;
};

static int commit_ram_read(void *ctx, uint32_t lba, void *buffer) {
  struct rekey_commit_ram_dev *r = (struct rekey_commit_ram_dev *)ctx;
  if (!r || !buffer || lba >= r->dev.block_count) return -1;
  memcpy(buffer, r->storage + (size_t)lba * r->dev.block_size,
         r->dev.block_size);
  return 0;
}

static int commit_ram_write(void *ctx, uint32_t lba, const void *buffer) {
  struct rekey_commit_ram_dev *r = (struct rekey_commit_ram_dev *)ctx;
  if (!r || !buffer || lba >= r->dev.block_count) return -1;
  if (lba == r->fail_write_lba) return -1;
  memcpy(r->storage + (size_t)lba * r->dev.block_size, buffer,
         r->dev.block_size);
  if (lba == r->corrupt_write_lba) {
    r->storage[(size_t)lba * r->dev.block_size + 72u] ^= 0x5Au;
  }
  return 0;
}

static struct block_device_ops g_commit_ram_ops = {
    .read_block = commit_ram_read,
    .write_block = commit_ram_write,
};

static struct rekey_commit_ram_dev *commit_ram_alloc(uint32_t count) {
  struct rekey_commit_ram_dev *r =
      (struct rekey_commit_ram_dev *)calloc(1, sizeof(*r));
  if (!r) return NULL;
  r->storage = (uint8_t *)calloc((size_t)count, REKEY_COMMIT_BLOCK_SIZE);
  if (!r->storage) {
    free(r);
    return NULL;
  }
  r->dev.name = "rekey-commit-test-ram";
  r->dev.block_size = REKEY_COMMIT_BLOCK_SIZE;
  r->dev.block_count = count;
  r->dev.ctx = r;
  r->dev.ops = &g_commit_ram_ops;
  r->fail_write_lba = REKEY_COMMIT_FAIL_LBA;
  r->corrupt_write_lba = REKEY_COMMIT_FAIL_LBA;
  return r;
}

static void commit_ram_free(struct rekey_commit_ram_dev *r) {
  if (!r) return;
  free(r->storage);
  free(r);
}

static int expect_int(int got, int want, const char *what) {
  if (got != want) {
    printf("[tests] volume_provider_rekey_commit: %s expected %d, got %d\n",
           what, want, got);
    return 1;
  }
  return 0;
}

static int expect_true(int cond, const char *msg) {
  if (!cond) {
    printf("[tests] volume_provider_rekey_commit: %s\n", msg);
    return 1;
  }
  return 0;
}

static void put_u32_le(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFFu);
  p[1] = (uint8_t)((v >> 8) & 0xFFu);
  p[2] = (uint8_t)((v >> 16) & 0xFFu);
  p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint32_t get_u32_le(const uint8_t *p) {
  return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int write_legacy_super(struct rekey_commit_ram_dev *r,
                              const uint8_t *legacy_salt,
                              size_t legacy_salt_len,
                              uint32_t legacy_iter,
                              uint32_t capyfs_blocks) {
  uint8_t k1[CRYPT_KEY_SIZE], k2[CRYPT_KEY_SIZE];
  uint8_t super[REKEY_COMMIT_BLOCK_SIZE];
  struct block_device *legacy_crypt = NULL;
  uint32_t bits_per_block = CAPYFS_BLOCK_SIZE * 8u;
  uint32_t inode_count = 128u;
  uint32_t block_bitmap_blocks =
      (capyfs_blocks + bits_per_block - 1u) / bits_per_block;
  uint32_t inode_bitmap_blocks =
      (inode_count + bits_per_block - 1u) / bits_per_block;
  uint32_t inode_table_blocks =
      (inode_count * (uint32_t)sizeof(struct capy_inode_disk) +
       CAPYFS_BLOCK_SIZE - 1u) /
      CAPYFS_BLOCK_SIZE;
  uint32_t bmap_start = 1u;
  uint32_t imap_start = bmap_start + block_bitmap_blocks;
  uint32_t inode_start = imap_start + inode_bitmap_blocks;
  uint32_t data_start = inode_start + inode_table_blocks;
  int rc = -1;
  if (!r || data_start >= capyfs_blocks) return -1;
  memset(super, 0, sizeof(super));
  put_u32_le(super + 0, CAPYFS_MAGIC);
  put_u32_le(super + 4, CAPYFS_VERSION);
  put_u32_le(super + 8, CAPYFS_BLOCK_SIZE);
  put_u32_le(super + 12, capyfs_blocks);
  put_u32_le(super + 16, inode_count);
  put_u32_le(super + 20, bmap_start);
  put_u32_le(super + 24, imap_start);
  put_u32_le(super + 28, inode_start);
  put_u32_le(super + 32, data_start);
  crypt_derive_xts_keys(REKEY_COMMIT_PASSWORD, legacy_salt, legacy_salt_len,
                        legacy_iter, k1, k2);
  legacy_crypt = crypt_init(&r->dev, k1, k2);
  if (legacy_crypt && legacy_crypt != &r->dev &&
      block_device_write(legacy_crypt, 0u, super) == 0) {
    rc = 0;
  }
  if (legacy_crypt && legacy_crypt != &r->dev) crypt_free(legacy_crypt);
  memset(k1, 0, sizeof(k1));
  memset(k2, 0, sizeof(k2));
  memset(super, 0, sizeof(super));
  return rc;
}

static int setup_complete_copy(struct rekey_commit_ram_dev **out_r,
                               uint32_t capyfs_blocks) {
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct volume_provider_rekey_execution_report report;
  struct rekey_commit_ram_dev *r = commit_ram_alloc(capyfs_blocks + 2u);
  if (!out_r || !r) return -1;
  *out_r = NULL;
  if (write_legacy_super(r, legacy_salt, sizeof(legacy_salt), 1000u,
                         capyfs_blocks) != 0) {
    commit_ram_free(r);
    return -1;
  }
  if (volume_provider_rekey_execute_stage_header(
          &r->dev, REKEY_COMMIT_PASSWORD, legacy_salt, sizeof(legacy_salt),
          1000u, VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_STAGED_HEADER_WRITE,
          &report) != 0) {
    commit_ram_free(r);
    return -1;
  }
  for (uint32_t i = 0u; i < capyfs_blocks; ++i) {
    if (volume_provider_rekey_execute_copy_step(
            &r->dev, REKEY_COMMIT_PASSWORD, legacy_salt, sizeof(legacy_salt),
            1000u, VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_COPY_STEP,
            &report) != 0) {
      commit_ram_free(r);
      return -1;
    }
  }
  *out_r = r;
  return report.status == VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_COMPLETE
             ? 0
             : -1;
}

static int test_commit_header_success_and_open(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct rekey_commit_ram_dev *r = NULL;
  struct volume_provider_rekey_execution_report report;
  struct volume_provider_rekey_checkpoint checkpoint;
  struct block_device *opened = NULL;
  uint8_t block[REKEY_COMMIT_BLOCK_SIZE];
  if (setup_complete_copy(&r, 7u) != 0) return 1;
  fails += expect_int(volume_provider_rekey_execute_commit_header(
                          &r->dev, REKEY_COMMIT_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_COMMIT_HEADER,
                          &report),
                      0, "commit rc");
  fails += expect_int((int)report.status,
                      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_COMMIT_HEADER_WRITTEN,
                      "commit status");
  fails += expect_int((int)report.blocks_completed, 7,
                      "commit blocks completed");
  fails += expect_int((int)report.blocks_remaining, 0,
                      "commit blocks remaining");
  fails += expect_int(block_device_read(&r->dev, 0u, block), 0,
                      "commit read lba0");
  fails += expect_true(capyos_volume_header_looks_valid(block) == 1,
                       "committed lba0 has valid header");
  fails += expect_int(volume_provider_open(&r->dev, REKEY_COMMIT_PASSWORD,
                                           NULL, 0u, 0u, &opened),
                      0, "commit open header-managed");
  fails += expect_true(opened != NULL, "commit opened crypt device");
  if (opened) {
    memset(block, 0, sizeof(block));
    fails += expect_int(block_device_read(opened, 0u, block), 0,
                        "commit read opened super");
    fails += expect_int((int)get_u32_le(block + 0), (int)CAPYFS_MAGIC,
                        "commit opened capyfs magic");
    crypt_free(opened);
    opened = NULL;
  }
  memset(block, 0, sizeof(block));
  fails += expect_int(block_device_read(&r->dev, 8u, block), 0,
                      "commit read scratch");
  fails += expect_int(volume_provider_rekey_checkpoint_parse(
                          block + VOLUME_PROVIDER_REKEY_STAGE_CHECKPOINT_OFFSET,
                          VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE, &checkpoint),
                      0, "commit parse checkpoint");
  fails += expect_int((int)checkpoint.flags,
                      (int)VOLUME_PROVIDER_REKEY_CHECKPOINT_FLAG_COMPLETED,
                      "commit checkpoint completed flag");
  commit_ram_free(r);
  return fails;
}

static int test_commit_header_requires_flag(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct volume_provider_rekey_execution_report report;
  fails += expect_int(volume_provider_rekey_execute_commit_header(
                          NULL, REKEY_COMMIT_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_COPY_STEP,
                          &report),
                      0, "commit wrong flag rc");
  fails += expect_int((int)report.status,
                      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_WRITES_DISABLED,
                      "commit wrong flag status");
  return fails;
}

static int test_commit_header_rejects_incomplete_copy(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct rekey_commit_ram_dev *r = commit_ram_alloc(9u);
  struct volume_provider_rekey_execution_report report;
  if (!r) return 1;
  fails += expect_int(write_legacy_super(r, legacy_salt, sizeof(legacy_salt),
                                        1000u, 7u),
                      0, "commit incomplete legacy setup");
  fails += expect_int(volume_provider_rekey_execute_stage_header(
                          &r->dev, REKEY_COMMIT_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_STAGED_HEADER_WRITE,
                          &report),
                      0, "commit incomplete stage");
  fails += expect_int(volume_provider_rekey_execute_commit_header(
                          &r->dev, REKEY_COMMIT_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_COMMIT_HEADER,
                          &report),
                      0, "commit incomplete rc");
  fails += expect_int(
      (int)report.status,
      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_COMMIT_HEADER_BUILD_FAILED,
      "commit incomplete status");
  commit_ram_free(r);
  return fails;
}

static int test_commit_header_write_failure_status(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct rekey_commit_ram_dev *r = NULL;
  struct volume_provider_rekey_execution_report report;
  if (setup_complete_copy(&r, 7u) != 0) return 1;
  r->fail_write_lba = 0u;
  fails += expect_int(volume_provider_rekey_execute_commit_header(
                          &r->dev, REKEY_COMMIT_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_COMMIT_HEADER,
                          &report),
                      0, "commit write fail rc");
  fails += expect_int(
      (int)report.status,
      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_COMMIT_HEADER_WRITE_FAILED,
      "commit write fail status");
  commit_ram_free(r);
  return fails;
}

static int test_commit_header_verify_failure_status(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct rekey_commit_ram_dev *r = NULL;
  struct volume_provider_rekey_execution_report report;
  if (setup_complete_copy(&r, 7u) != 0) return 1;
  r->corrupt_write_lba = 0u;
  fails += expect_int(volume_provider_rekey_execute_commit_header(
                          &r->dev, REKEY_COMMIT_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_COMMIT_HEADER,
                          &report),
                      0, "commit verify fail rc");
  fails += expect_int(
      (int)report.status,
      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_COMMIT_HEADER_VERIFY_FAILED,
      "commit verify fail status");
  commit_ram_free(r);
  return fails;
}

int run_volume_provider_rekey_commit_tests(void) {
  int fails = 0;
  fails += test_commit_header_success_and_open();
  fails += test_commit_header_requires_flag();
  fails += test_commit_header_rejects_incomplete_copy();
  fails += test_commit_header_write_failure_status();
  fails += test_commit_header_verify_failure_status();
  if (fails == 0) {
    printf("[tests] volume_provider_rekey_commit OK\n");
  } else {
    printf("[tests] volume_provider_rekey_commit FAILED %d\n", fails);
  }
  return fails;
}
