#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs/block.h"
#include "fs/capyfs.h"
#include "security/crypt.h"
#include "security/volume_provider.h"

#define REKEY_RECOVERY_BLOCK_SIZE 4096u
#define REKEY_RECOVERY_PASSWORD "alpha-231-passphrase"
#define REKEY_RECOVERY_FAIL_LBA 0xFFFFFFFFu

struct rekey_recovery_ram_dev {
  struct block_device dev;
  uint8_t *storage;
  uint32_t fail_write_lba;
};

static int recovery_ram_read(void *ctx, uint32_t lba, void *buffer) {
  struct rekey_recovery_ram_dev *r = (struct rekey_recovery_ram_dev *)ctx;
  if (!r || !buffer || lba >= r->dev.block_count) return -1;
  memcpy(buffer, r->storage + (size_t)lba * r->dev.block_size,
         r->dev.block_size);
  return 0;
}

static int recovery_ram_write(void *ctx, uint32_t lba, const void *buffer) {
  struct rekey_recovery_ram_dev *r = (struct rekey_recovery_ram_dev *)ctx;
  if (!r || !buffer || lba >= r->dev.block_count) return -1;
  if (lba == r->fail_write_lba) return -1;
  memcpy(r->storage + (size_t)lba * r->dev.block_size, buffer,
         r->dev.block_size);
  return 0;
}

static struct block_device_ops g_recovery_ram_ops = {
    .read_block = recovery_ram_read,
    .write_block = recovery_ram_write,
};

static struct rekey_recovery_ram_dev *recovery_ram_alloc(uint32_t count) {
  struct rekey_recovery_ram_dev *r =
      (struct rekey_recovery_ram_dev *)calloc(1, sizeof(*r));
  if (!r) return NULL;
  r->storage = (uint8_t *)calloc((size_t)count, REKEY_RECOVERY_BLOCK_SIZE);
  if (!r->storage) {
    free(r);
    return NULL;
  }
  r->dev.name = "rekey-recovery-test-ram";
  r->dev.block_size = REKEY_RECOVERY_BLOCK_SIZE;
  r->dev.block_count = count;
  r->dev.ctx = r;
  r->dev.ops = &g_recovery_ram_ops;
  r->fail_write_lba = REKEY_RECOVERY_FAIL_LBA;
  return r;
}

static void recovery_ram_free(struct rekey_recovery_ram_dev *r) {
  if (!r) return;
  free(r->storage);
  free(r);
}

static int expect_int(int got, int want, const char *what) {
  if (got != want) {
    printf("[tests] volume_provider_rekey_recovery: %s expected %d, got %d\n",
           what, want, got);
    return 1;
  }
  return 0;
}

static int expect_true(int cond, const char *msg) {
  if (!cond) {
    printf("[tests] volume_provider_rekey_recovery: %s\n", msg);
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

static int block_is_zero(const uint8_t *block, size_t len) {
  uint8_t acc = 0u;
  if (!block) return 0;
  for (size_t i = 0u; i < len; ++i) acc |= block[i];
  return acc == 0u;
}

static void fill_data_block(uint8_t *block, uint32_t lba) {
  if (!block) return;
  for (size_t i = 0u; i < REKEY_RECOVERY_BLOCK_SIZE; ++i) {
    block[i] = (uint8_t)(0xA5u ^ (uint8_t)lba ^ (uint8_t)i);
  }
}

static int block_matches_data(const uint8_t *block, uint32_t lba) {
  if (!block) return 0;
  for (size_t i = 0u; i < REKEY_RECOVERY_BLOCK_SIZE; ++i) {
    if (block[i] != (uint8_t)(0xA5u ^ (uint8_t)lba ^ (uint8_t)i)) {
      return 0;
    }
  }
  return 1;
}

static int write_legacy_super(struct rekey_recovery_ram_dev *r,
                              const uint8_t *legacy_salt,
                              size_t legacy_salt_len,
                              uint32_t legacy_iter,
                              uint32_t capyfs_blocks) {
  uint8_t k1[CRYPT_KEY_SIZE], k2[CRYPT_KEY_SIZE];
  uint8_t super[REKEY_RECOVERY_BLOCK_SIZE];
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
  crypt_derive_xts_keys(REKEY_RECOVERY_PASSWORD, legacy_salt, legacy_salt_len,
                        legacy_iter, k1, k2);
  legacy_crypt = crypt_init(&r->dev, k1, k2);
  if (legacy_crypt && legacy_crypt != &r->dev &&
      block_device_write(legacy_crypt, 0u, super) == 0) {
    rc = 0;
    for (uint32_t lba = 1u; lba < capyfs_blocks && rc == 0; ++lba) {
      fill_data_block(super, lba);
      if (block_device_write(legacy_crypt, lba, super) != 0) {
        rc = -1;
      }
    }
  }
  if (legacy_crypt && legacy_crypt != &r->dev) crypt_free(legacy_crypt);
  memset(k1, 0, sizeof(k1));
  memset(k2, 0, sizeof(k2));
  memset(super, 0, sizeof(super));
  return rc;
}

static int setup_progress(struct rekey_recovery_ram_dev **out_r,
                          uint32_t capyfs_blocks,
                          uint32_t copy_steps) {
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct volume_provider_rekey_execution_report report;
  struct rekey_recovery_ram_dev *r = recovery_ram_alloc(capyfs_blocks + 2u);
  if (!out_r || !r) return -1;
  *out_r = NULL;
  if (write_legacy_super(r, legacy_salt, sizeof(legacy_salt), 1000u,
                         capyfs_blocks) != 0) {
    recovery_ram_free(r);
    return -1;
  }
  if (volume_provider_rekey_execute_stage_header(
          &r->dev, REKEY_RECOVERY_PASSWORD, legacy_salt, sizeof(legacy_salt),
          1000u, VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_STAGED_HEADER_WRITE,
          &report) != 0 ||
      report.status != VOLUME_PROVIDER_REKEY_EXEC_STATUS_STAGED_HEADER_WRITTEN) {
    recovery_ram_free(r);
    return -1;
  }
  for (uint32_t i = 0u; i < copy_steps; ++i) {
    if (volume_provider_rekey_execute_copy_step(
            &r->dev, REKEY_RECOVERY_PASSWORD, legacy_salt, sizeof(legacy_salt),
            1000u, VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_COPY_STEP,
            &report) != 0 ||
        (i + 1u == capyfs_blocks &&
         report.status != VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_COMPLETE) ||
        (i + 1u < capyfs_blocks &&
         report.status != VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_DONE)) {
      recovery_ram_free(r);
      return -1;
    }
  }
  *out_r = r;
  return 0;
}

static int test_rollback_requires_flag(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct volume_provider_rekey_execution_report report;
  fails += expect_int(volume_provider_rekey_execute_rollback_step(
                          NULL, REKEY_RECOVERY_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_COPY_STEP,
                          &report),
                      0, "rollback wrong flag rc");
  fails += expect_int((int)report.status,
                      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_WRITES_DISABLED,
                      "rollback wrong flag status");
  return fails;
}

static int test_cleanup_requires_flag(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct volume_provider_rekey_execution_report report;
  fails += expect_int(volume_provider_rekey_execute_cleanup_scratch(
                          NULL, REKEY_RECOVERY_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_COMMIT_HEADER,
                          &report),
                      0, "cleanup wrong flag rc");
  fails += expect_int((int)report.status,
                      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_WRITES_DISABLED,
                      "cleanup wrong flag status");
  return fails;
}

static int test_rollback_step_decrements_checkpoint(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct rekey_recovery_ram_dev *r = NULL;
  struct volume_provider_rekey_execution_report report;
  struct volume_provider_rekey_checkpoint checkpoint;
  struct block_device *legacy_crypt = NULL;
  uint8_t scratch[REKEY_RECOVERY_BLOCK_SIZE];
  uint8_t block[REKEY_RECOVERY_BLOCK_SIZE];
  uint8_t k1[CRYPT_KEY_SIZE], k2[CRYPT_KEY_SIZE];
  if (setup_progress(&r, 7u, 2u) != 0) return 1;
  fails += expect_int(volume_provider_rekey_execute_rollback_step(
                          &r->dev, REKEY_RECOVERY_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_ROLLBACK_STEP,
                          &report),
                      0, "rollback step rc");
  fails += expect_int((int)report.status,
                      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_ROLLBACK_STEP_DONE,
                      "rollback step status");
  fails += expect_int((int)report.blocks_completed, 1,
                      "rollback step completed");
  fails += expect_int(block_device_read(&r->dev, 8u, scratch), 0,
                      "rollback read scratch");
  fails += expect_int(volume_provider_rekey_checkpoint_parse(
                          scratch + VOLUME_PROVIDER_REKEY_STAGE_CHECKPOINT_OFFSET,
                          VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE, &checkpoint),
                      0, "rollback parse checkpoint");
  fails += expect_int((int)checkpoint.blocks_completed, 1,
                      "rollback checkpoint completed");
  crypt_derive_xts_keys(REKEY_RECOVERY_PASSWORD, legacy_salt,
                        sizeof(legacy_salt), 1000u, k1, k2);
  legacy_crypt = crypt_init(&r->dev, k1, k2);
  fails += expect_true(legacy_crypt != NULL, "rollback partial legacy opens");
  if (legacy_crypt) {
    fails += expect_int(block_device_read(legacy_crypt, 6u, block), 0,
                        "rollback partial reads restored tail");
    fails += expect_true(block_matches_data(block, 6u),
                         "rollback partial restores destroyed tail");
    crypt_free(legacy_crypt);
  }
  memset(k1, 0, sizeof(k1));
  memset(k2, 0, sizeof(k2));
  memset(block, 0, sizeof(block));
  recovery_ram_free(r);
  return fails;
}

static int test_rollback_abort_complete_cleans_scratch(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct rekey_recovery_ram_dev *r = NULL;
  struct volume_provider_rekey_execution_report report;
  struct block_device *legacy_crypt = NULL;
  uint8_t scratch[REKEY_RECOVERY_BLOCK_SIZE];
  uint8_t block[REKEY_RECOVERY_BLOCK_SIZE];
  uint8_t k1[CRYPT_KEY_SIZE], k2[CRYPT_KEY_SIZE];
  if (setup_progress(&r, 7u, 2u) != 0) return 1;
  fails += expect_int(volume_provider_rekey_execute_rollback_step(
                          &r->dev, REKEY_RECOVERY_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_ROLLBACK_STEP,
                          &report),
                      0, "rollback abort first rc");
  fails += expect_int(volume_provider_rekey_execute_rollback_step(
                          &r->dev, REKEY_RECOVERY_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_ROLLBACK_STEP,
                          &report),
                      0, "rollback abort second rc");
  fails += expect_int((int)report.status,
                      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_ROLLBACK_COMPLETE,
                      "rollback complete status");
  fails += expect_int(block_device_read(&r->dev, 8u, scratch), 0,
                      "rollback complete read scratch");
  fails += expect_true(block_is_zero(scratch, sizeof(scratch)),
                       "rollback complete clears scratch");
  crypt_derive_xts_keys(REKEY_RECOVERY_PASSWORD, legacy_salt,
                        sizeof(legacy_salt), 1000u, k1, k2);
  legacy_crypt = crypt_init(&r->dev, k1, k2);
  fails += expect_true(legacy_crypt != NULL, "rollback legacy crypt opens");
  if (legacy_crypt) {
    fails += expect_int(block_device_read(legacy_crypt, 0u, block), 0,
                        "rollback legacy read super");
    fails += expect_int((int)get_u32_le(block + 0), (int)CAPYFS_MAGIC,
                        "rollback legacy magic");
    crypt_free(legacy_crypt);
  }
  memset(k1, 0, sizeof(k1));
  memset(k2, 0, sizeof(k2));
  recovery_ram_free(r);
  return fails;
}

static int test_cleanup_after_commit_zeros_scratch(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct rekey_recovery_ram_dev *r = NULL;
  struct volume_provider_rekey_execution_report report;
  uint8_t scratch[REKEY_RECOVERY_BLOCK_SIZE];
  if (setup_progress(&r, 7u, 7u) != 0) return 1;
  fails += expect_int(volume_provider_rekey_execute_commit_header(
                          &r->dev, REKEY_RECOVERY_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_COMMIT_HEADER,
                          &report),
                      0, "cleanup commit rc");
  fails += expect_int(volume_provider_rekey_execute_cleanup_scratch(
                          &r->dev, REKEY_RECOVERY_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_CLEANUP_SCRATCH,
                          &report),
                      0, "cleanup rc");
  fails += expect_int((int)report.status,
                      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_CLEANUP_SCRATCH_DONE,
                      "cleanup status");
  fails += expect_int(block_device_read(&r->dev, 8u, scratch), 0,
                      "cleanup read scratch");
  fails += expect_true(block_is_zero(scratch, sizeof(scratch)),
                       "cleanup zero scratch");
  recovery_ram_free(r);
  return fails;
}

static int test_cleanup_write_failure_status(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct rekey_recovery_ram_dev *r = NULL;
  struct volume_provider_rekey_execution_report report;
  if (setup_progress(&r, 7u, 7u) != 0) return 1;
  fails += expect_int(volume_provider_rekey_execute_commit_header(
                          &r->dev, REKEY_RECOVERY_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_COMMIT_HEADER,
                          &report),
                      0, "cleanup fail commit rc");
  r->fail_write_lba = 8u;
  fails += expect_int(volume_provider_rekey_execute_cleanup_scratch(
                          &r->dev, REKEY_RECOVERY_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_CLEANUP_SCRATCH,
                          &report),
                      0, "cleanup fail rc");
  fails += expect_int(
      (int)report.status,
      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_CLEANUP_SCRATCH_WRITE_FAILED,
      "cleanup fail status");
  recovery_ram_free(r);
  return fails;
}

int run_volume_provider_rekey_recovery_tests(void) {
  int fails = 0;
  fails += test_rollback_requires_flag();
  fails += test_cleanup_requires_flag();
  fails += test_rollback_step_decrements_checkpoint();
  fails += test_rollback_abort_complete_cleans_scratch();
  fails += test_cleanup_after_commit_zeros_scratch();
  fails += test_cleanup_write_failure_status();
  if (fails == 0) {
    printf("[tests] volume_provider_rekey_recovery OK\n");
  } else {
    printf("[tests] volume_provider_rekey_recovery FAILED %d\n", fails);
  }
  return fails;
}
