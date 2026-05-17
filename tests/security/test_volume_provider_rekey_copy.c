/*
 * Host-side tests for the write-enabled rekey copy step:
 *
 *   - volume_provider_rekey_execute_copy_step
 *
 * Mirrors test_volume_provider_rekey_execute.c (which still owns the
 * checkpoint / stage_header / stage_manifest tests) using its own
 * private copy of the in-RAM block-device fixture infrastructure so
 * each test translation unit can grow independently without crossing
 * the host audit ceiling. The source-side companion lives at
 * src/security/volume_provider_rekey_copy.c.
 *
 * The runner exports `run_volume_provider_rekey_copy_tests` which is
 * declared and called from tests/test_runner.c, sitting next to the
 * existing `run_volume_provider_rekey_execute_tests` entry.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs/block.h"
#include "fs/capyfs.h"
#include "security/crypt.h"
#include "security/volume_header.h"
#include "security/volume_provider.h"

#define REKEY_COPY_BLOCK_SIZE 4096u
#define REKEY_COPY_BLOCK_COUNT 32u
#define REKEY_COPY_PASSWORD "alpha-227-passphrase"
#define REKEY_COPY_FAIL_LBA 0xFFFFFFFFu

struct rekey_copy_ram_dev {
  struct block_device dev;
  uint8_t *storage;
  uint32_t fail_write_lba;
  uint32_t corrupt_write_lba;
};

struct rekey_copy_offset_view {
  struct block_device *lower;
  uint32_t start_lba;
  uint32_t block_count;
};

static int rekey_copy_ram_read(void *ctx, uint32_t lba, void *buffer) {
  struct rekey_copy_ram_dev *r = (struct rekey_copy_ram_dev *)ctx;
  if (!r || !buffer || lba >= r->dev.block_count) return -1;
  memcpy(buffer, r->storage + (size_t)lba * r->dev.block_size,
         r->dev.block_size);
  return 0;
}

static int rekey_copy_ram_write(void *ctx, uint32_t lba, const void *buffer) {
  struct rekey_copy_ram_dev *r = (struct rekey_copy_ram_dev *)ctx;
  if (!r || !buffer || lba >= r->dev.block_count) return -1;
  if (lba == r->fail_write_lba) return -1;
  memcpy(r->storage + (size_t)lba * r->dev.block_size, buffer,
         r->dev.block_size);
  if (lba == r->corrupt_write_lba) {
    r->storage[(size_t)lba * r->dev.block_size + 72u] ^= 0x5Au;
  }
  return 0;
}

static struct block_device_ops g_rekey_copy_ram_ops = {
    .read_block = rekey_copy_ram_read,
    .write_block = rekey_copy_ram_write,
};

static int rekey_copy_offset_read(void *ctx, uint32_t lba, void *buffer) {
  struct rekey_copy_offset_view *v = (struct rekey_copy_offset_view *)ctx;
  if (!v || !v->lower || !buffer || lba >= v->block_count) return -1;
  return block_device_read(v->lower, v->start_lba + lba, buffer);
}

static int rekey_copy_offset_write(void *ctx, uint32_t lba,
                                   const void *buffer) {
  struct rekey_copy_offset_view *v = (struct rekey_copy_offset_view *)ctx;
  if (!v || !v->lower || !buffer || lba >= v->block_count) return -1;
  return block_device_write(v->lower, v->start_lba + lba, buffer);
}

static struct block_device_ops g_rekey_copy_offset_ops = {
    .read_block = rekey_copy_offset_read,
    .write_block = rekey_copy_offset_write,
};

static struct rekey_copy_ram_dev *rekey_copy_ram_alloc(uint32_t count) {
  struct rekey_copy_ram_dev *r =
      (struct rekey_copy_ram_dev *)calloc(1, sizeof(*r));
  if (!r) return NULL;
  r->storage = (uint8_t *)calloc((size_t)count, REKEY_COPY_BLOCK_SIZE);
  if (!r->storage) {
    free(r);
    return NULL;
  }
  r->dev.name = "rekey-copy-test-ram";
  r->dev.block_size = REKEY_COPY_BLOCK_SIZE;
  r->dev.block_count = count;
  r->dev.ctx = r;
  r->dev.ops = &g_rekey_copy_ram_ops;
  r->fail_write_lba = REKEY_COPY_FAIL_LBA;
  r->corrupt_write_lba = REKEY_COPY_FAIL_LBA;
  return r;
}

static void rekey_copy_ram_free(struct rekey_copy_ram_dev *r) {
  if (!r) return;
  free(r->storage);
  free(r);
}

static int expect_int(int got, int want, const char *what) {
  if (got != want) {
    printf("[tests] volume_provider_rekey_copy: %s expected %d, got %d\n",
           what, want, got);
    return 1;
  }
  return 0;
}

static int expect_true(int cond, const char *msg) {
  if (!cond) {
    printf("[tests] volume_provider_rekey_copy: %s\n", msg);
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

static int write_rekey_copy_legacy_super(struct rekey_copy_ram_dev *r,
                                         const uint8_t *legacy_salt,
                                         size_t legacy_salt_len,
                                         uint32_t legacy_iter,
                                         uint32_t capyfs_blocks) {
  uint8_t k1[CRYPT_KEY_SIZE], k2[CRYPT_KEY_SIZE];
  uint8_t super[REKEY_COPY_BLOCK_SIZE];
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
  crypt_derive_xts_keys(REKEY_COPY_PASSWORD, legacy_salt, legacy_salt_len,
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

static int setup_ready_legacy(struct rekey_copy_ram_dev **out) {
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct rekey_copy_ram_dev *r = rekey_copy_ram_alloc(REKEY_COPY_BLOCK_COUNT);
  if (!r) return -1;
  if (write_rekey_copy_legacy_super(r, legacy_salt, sizeof(legacy_salt), 1000u,
                                    REKEY_COPY_BLOCK_COUNT - 2u) != 0) {
    rekey_copy_ram_free(r);
    return -1;
  }
  *out = r;
  return 0;
}

static int read_legacy_plain(struct rekey_copy_ram_dev *r, uint32_t lba,
                             uint8_t out[REKEY_COPY_BLOCK_SIZE]) {
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  uint8_t k1[CRYPT_KEY_SIZE], k2[CRYPT_KEY_SIZE];
  struct block_device *legacy_crypt = NULL;
  int rc = -1;
  if (!r || !out) return -1;
  crypt_derive_xts_keys(REKEY_COPY_PASSWORD, legacy_salt, sizeof(legacy_salt),
                        1000u, k1, k2);
  legacy_crypt = crypt_init(&r->dev, k1, k2);
  if (legacy_crypt && block_device_read(legacy_crypt, lba, out) == 0) rc = 0;
  if (legacy_crypt && legacy_crypt != &r->dev) crypt_free(legacy_crypt);
  memset(k1, 0, sizeof(k1));
  memset(k2, 0, sizeof(k2));
  return rc;
}

static int read_target_plain_from_scratch(struct rekey_copy_ram_dev *r,
                                          uint32_t logical_lba,
                                          uint8_t out[REKEY_COPY_BLOCK_SIZE]) {
  uint8_t scratch[REKEY_COPY_BLOCK_SIZE];
  uint8_t k1[CRYPT_KEY_SIZE], k2[CRYPT_KEY_SIZE];
  struct capyos_volume_header header;
  struct rekey_copy_offset_view view_ctx;
  struct block_device view;
  struct block_device *target_crypt = NULL;
  int rc = -1;
  if (!r || !out) return -1;
  if (block_device_read(&r->dev, 31u, scratch) != 0 ||
      capyos_volume_header_parse(
          scratch + VOLUME_PROVIDER_REKEY_STAGE_HEADER_OFFSET, &header) !=
          CAPYOS_VOLUME_HEADER_OK ||
      crypt_derive_xts_keys_argon2id(REKEY_COPY_PASSWORD, header.kdf_salt,
                                     header.kdf_salt_len, header.kdf_t_cost,
                                     header.kdf_m_cost, k1, k2) != 0) {
    goto out;
  }
  view_ctx.lower = &r->dev;
  view_ctx.start_lba = 1u;
  view_ctx.block_count = 30u;
  view.name = "test-rekey-target";
  view.block_size = REKEY_COPY_BLOCK_SIZE;
  view.block_count = 30u;
  view.ctx = &view_ctx;
  view.ops = &g_rekey_copy_offset_ops;
  target_crypt = crypt_init(&view, k1, k2);
  if (target_crypt && block_device_read(target_crypt, logical_lba, out) == 0) {
    rc = 0;
  }
out:
  if (target_crypt && target_crypt != &view) crypt_free(target_crypt);
  memset(k1, 0, sizeof(k1));
  memset(k2, 0, sizeof(k2));
  memset(scratch, 0, sizeof(scratch));
  return rc;
}

static int test_copy_step_success_one_block(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct rekey_copy_ram_dev *r = NULL;
  struct volume_provider_rekey_execution_report report;
  struct volume_provider_rekey_checkpoint checkpoint;
  uint8_t scratch[REKEY_COPY_BLOCK_SIZE];
  uint8_t legacy_plain[REKEY_COPY_BLOCK_SIZE];
  uint8_t target_plain[REKEY_COPY_BLOCK_SIZE];
  if (setup_ready_legacy(&r) != 0) return 1;
  fails += expect_int(volume_provider_rekey_execute_stage_header(
                          &r->dev, REKEY_COPY_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_STAGED_HEADER_WRITE,
                          &report),
                      0, "copy setup stage rc");
  fails += expect_int(volume_provider_rekey_execute_copy_step(
                          &r->dev, REKEY_COPY_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_COPY_STEP,
                          &report),
                      0, "copy step rc");
  fails += expect_int((int)report.status,
                      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_DONE,
                      "copy step status");
  fails += expect_int((int)report.blocks_completed, 1,
                      "copy step completed");
  fails += expect_int((int)report.blocks_remaining, 29,
                      "copy step remaining");
  fails += expect_int(block_device_read(&r->dev, 31u, scratch), 0,
                      "copy read scratch");
  fails += expect_int(volume_provider_rekey_checkpoint_parse(
                          scratch + VOLUME_PROVIDER_REKEY_STAGE_CHECKPOINT_OFFSET,
                          VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE, &checkpoint),
                      0, "copy parse checkpoint");
  fails += expect_int((int)checkpoint.blocks_completed, 1,
                      "copy checkpoint completed");
  fails += expect_int((int)checkpoint.next_source_lba, 28,
                      "copy next source");
  fails += expect_int(read_legacy_plain(r, 29u, legacy_plain), 0,
                      "copy legacy plain");
  fails += expect_int(read_target_plain_from_scratch(r, 29u, target_plain), 0,
                      "copy target plain");
  fails += expect_true(memcmp(legacy_plain, target_plain,
                              sizeof(legacy_plain)) == 0,
                       "copy target plaintext matches source");
  rekey_copy_ram_free(r);
  return fails;
}

static int test_copy_step_requires_flag(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct rekey_copy_ram_dev *r = NULL;
  struct volume_provider_rekey_execution_report report;
  if (setup_ready_legacy(&r) != 0) return 1;
  fails += expect_int(volume_provider_rekey_execute_copy_step(
                          &r->dev, REKEY_COPY_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_STAGED_HEADER_WRITE,
                          &report),
                      0, "copy wrong flag rc");
  fails += expect_int((int)report.status,
                      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_WRITES_DISABLED,
                      "copy wrong flag status");
  rekey_copy_ram_free(r);
  return fails;
}

static int test_copy_step_write_failure_status(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct rekey_copy_ram_dev *r = NULL;
  struct volume_provider_rekey_execution_report report;
  if (setup_ready_legacy(&r) != 0) return 1;
  fails += expect_int(volume_provider_rekey_execute_stage_header(
                          &r->dev, REKEY_COPY_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_STAGED_HEADER_WRITE,
                          &report),
                      0, "copy write setup stage rc");
  r->fail_write_lba = 30u;
  fails += expect_int(volume_provider_rekey_execute_copy_step(
                          &r->dev, REKEY_COPY_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_COPY_STEP,
                          &report),
                      0, "copy write fail rc");
  fails += expect_int(
      (int)report.status,
      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_WRITE_FAILED,
      "copy write fail status");
  rekey_copy_ram_free(r);
  return fails;
}

static int test_copy_step_verify_failure_status(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct rekey_copy_ram_dev *r = NULL;
  struct volume_provider_rekey_execution_report report;
  if (setup_ready_legacy(&r) != 0) return 1;
  fails += expect_int(volume_provider_rekey_execute_stage_header(
                          &r->dev, REKEY_COPY_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_STAGED_HEADER_WRITE,
                          &report),
                      0, "copy verify setup stage rc");
  r->corrupt_write_lba = 30u;
  fails += expect_int(volume_provider_rekey_execute_copy_step(
                          &r->dev, REKEY_COPY_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_COPY_STEP,
                          &report),
                      0, "copy verify fail rc");
  fails += expect_int(
      (int)report.status,
      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_VERIFY_FAILED,
      "copy verify fail status");
  rekey_copy_ram_free(r);
  return fails;
}

static int test_copy_step_completes_all_blocks(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct rekey_copy_ram_dev *r = rekey_copy_ram_alloc(9u);
  struct volume_provider_rekey_execution_report report;
  if (!r) return 1;
  fails += expect_int(write_rekey_copy_legacy_super(
                          r, legacy_salt, sizeof(legacy_salt), 1000u, 7u),
                      0, "copy complete legacy setup");
  fails += expect_int(volume_provider_rekey_execute_stage_header(
                          &r->dev, REKEY_COPY_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_STAGED_HEADER_WRITE,
                          &report),
                      0, "copy complete setup stage rc");
  for (uint32_t i = 0u; i < 7u; ++i) {
    fails += expect_int(volume_provider_rekey_execute_copy_step(
                            &r->dev, REKEY_COPY_PASSWORD, legacy_salt,
                            sizeof(legacy_salt), 1000u,
                            VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_COPY_STEP,
                            &report),
                        0, "copy complete step rc");
  }
  fails += expect_int(
      (int)report.status,
      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_COMPLETE,
      "copy complete status");
  fails += expect_int((int)report.blocks_completed, 7,
                      "copy complete completed");
  fails += expect_int((int)report.blocks_remaining, 0,
                      "copy complete remaining");
  rekey_copy_ram_free(r);
  return fails;
}

int run_volume_provider_rekey_copy_tests(void) {
  int fails = 0;
  fails += test_copy_step_success_one_block();
  fails += test_copy_step_requires_flag();
  fails += test_copy_step_write_failure_status();
  fails += test_copy_step_verify_failure_status();
  fails += test_copy_step_completes_all_blocks();
  if (fails == 0) {
    printf("[tests] volume_provider_rekey_copy OK\n");
  } else {
    printf("[tests] volume_provider_rekey_copy FAILED %d\n", fails);
  }
  return fails;
}
