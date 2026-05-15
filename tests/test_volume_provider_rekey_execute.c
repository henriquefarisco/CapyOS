#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs/block.h"
#include "fs/capyfs.h"
#include "security/crypt.h"
#include "security/volume_header.h"
#include "security/volume_provider.h"

#define REKEY_EXEC_BLOCK_SIZE 4096u
#define REKEY_EXEC_BLOCK_COUNT 32u
#define REKEY_EXEC_PASSWORD "alpha-227-passphrase"
#define REKEY_EXEC_BAD_PASSWORD "alpha-227-bad-passphrase"
#define REKEY_EXEC_FAIL_LBA 0xFFFFFFFFu

struct rekey_exec_ram_dev {
  struct block_device dev;
  uint8_t *storage;
  uint32_t fail_write_lba;
  uint32_t corrupt_write_lba;
};

struct rekey_exec_offset_view {
  struct block_device *lower;
  uint32_t start_lba;
  uint32_t block_count;
};

static int rekey_exec_ram_read(void *ctx, uint32_t lba, void *buffer) {
  struct rekey_exec_ram_dev *r = (struct rekey_exec_ram_dev *)ctx;
  if (!r || !buffer || lba >= r->dev.block_count) return -1;
  memcpy(buffer, r->storage + (size_t)lba * r->dev.block_size,
         r->dev.block_size);
  return 0;
}

static int rekey_exec_ram_write(void *ctx, uint32_t lba, const void *buffer) {
  struct rekey_exec_ram_dev *r = (struct rekey_exec_ram_dev *)ctx;
  if (!r || !buffer || lba >= r->dev.block_count) return -1;
  if (lba == r->fail_write_lba) return -1;
  memcpy(r->storage + (size_t)lba * r->dev.block_size, buffer,
         r->dev.block_size);
  if (lba == r->corrupt_write_lba) {
    r->storage[(size_t)lba * r->dev.block_size + 72u] ^= 0x5Au;
  }
  return 0;
}

static struct block_device_ops g_rekey_exec_ram_ops = {
    .read_block = rekey_exec_ram_read,
    .write_block = rekey_exec_ram_write,
};

static int rekey_exec_offset_read(void *ctx, uint32_t lba, void *buffer) {
  struct rekey_exec_offset_view *v = (struct rekey_exec_offset_view *)ctx;
  if (!v || !v->lower || !buffer || lba >= v->block_count) return -1;
  return block_device_read(v->lower, v->start_lba + lba, buffer);
}

static int rekey_exec_offset_write(void *ctx, uint32_t lba,
                                   const void *buffer) {
  struct rekey_exec_offset_view *v = (struct rekey_exec_offset_view *)ctx;
  if (!v || !v->lower || !buffer || lba >= v->block_count) return -1;
  return block_device_write(v->lower, v->start_lba + lba, buffer);
}

static struct block_device_ops g_rekey_exec_offset_ops = {
    .read_block = rekey_exec_offset_read,
    .write_block = rekey_exec_offset_write,
};

static struct rekey_exec_ram_dev *rekey_exec_ram_alloc(uint32_t count) {
  struct rekey_exec_ram_dev *r =
      (struct rekey_exec_ram_dev *)calloc(1, sizeof(*r));
  if (!r) return NULL;
  r->storage = (uint8_t *)calloc((size_t)count, REKEY_EXEC_BLOCK_SIZE);
  if (!r->storage) {
    free(r);
    return NULL;
  }
  r->dev.name = "rekey-exec-test-ram";
  r->dev.block_size = REKEY_EXEC_BLOCK_SIZE;
  r->dev.block_count = count;
  r->dev.ctx = r;
  r->dev.ops = &g_rekey_exec_ram_ops;
  r->fail_write_lba = REKEY_EXEC_FAIL_LBA;
  r->corrupt_write_lba = REKEY_EXEC_FAIL_LBA;
  return r;
}

static void rekey_exec_ram_free(struct rekey_exec_ram_dev *r) {
  if (!r) return;
  free(r->storage);
  free(r);
}

static int expect_int(int got, int want, const char *what) {
  if (got != want) {
    printf("[tests] volume_provider_rekey_execute: %s expected %d, got %d\n",
           what, want, got);
    return 1;
  }
  return 0;
}

static int expect_true(int cond, const char *msg) {
  if (!cond) {
    printf("[tests] volume_provider_rekey_execute: %s\n", msg);
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

static int write_rekey_exec_legacy_super(struct rekey_exec_ram_dev *r,
                                         const uint8_t *legacy_salt,
                                         size_t legacy_salt_len,
                                         uint32_t legacy_iter,
                                         uint32_t capyfs_blocks) {
  uint8_t k1[CRYPT_KEY_SIZE], k2[CRYPT_KEY_SIZE];
  uint8_t super[REKEY_EXEC_BLOCK_SIZE];
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
  crypt_derive_xts_keys(REKEY_EXEC_PASSWORD, legacy_salt, legacy_salt_len,
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

static int setup_ready_legacy(struct rekey_exec_ram_dev **out) {
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct rekey_exec_ram_dev *r = rekey_exec_ram_alloc(REKEY_EXEC_BLOCK_COUNT);
  if (!r) return -1;
  if (write_rekey_exec_legacy_super(r, legacy_salt, sizeof(legacy_salt), 1000u,
                                    REKEY_EXEC_BLOCK_COUNT - 2u) != 0) {
    rekey_exec_ram_free(r);
    return -1;
  }
  *out = r;
  return 0;
}

static int read_legacy_plain(struct rekey_exec_ram_dev *r, uint32_t lba,
                             uint8_t out[REKEY_EXEC_BLOCK_SIZE]) {
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  uint8_t k1[CRYPT_KEY_SIZE], k2[CRYPT_KEY_SIZE];
  struct block_device *legacy_crypt = NULL;
  int rc = -1;
  if (!r || !out) return -1;
  crypt_derive_xts_keys(REKEY_EXEC_PASSWORD, legacy_salt, sizeof(legacy_salt),
                        1000u, k1, k2);
  legacy_crypt = crypt_init(&r->dev, k1, k2);
  if (legacy_crypt && block_device_read(legacy_crypt, lba, out) == 0) rc = 0;
  if (legacy_crypt && legacy_crypt != &r->dev) crypt_free(legacy_crypt);
  memset(k1, 0, sizeof(k1));
  memset(k2, 0, sizeof(k2));
  return rc;
}

static int read_target_plain_from_scratch(struct rekey_exec_ram_dev *r,
                                          uint32_t logical_lba,
                                          uint8_t out[REKEY_EXEC_BLOCK_SIZE]) {
  uint8_t scratch[REKEY_EXEC_BLOCK_SIZE];
  uint8_t k1[CRYPT_KEY_SIZE], k2[CRYPT_KEY_SIZE];
  struct capyos_volume_header header;
  struct rekey_exec_offset_view view_ctx;
  struct block_device view;
  struct block_device *target_crypt = NULL;
  int rc = -1;
  if (!r || !out) return -1;
  if (block_device_read(&r->dev, 31u, scratch) != 0 ||
      capyos_volume_header_parse(
          scratch + VOLUME_PROVIDER_REKEY_STAGE_HEADER_OFFSET, &header) !=
          CAPYOS_VOLUME_HEADER_OK ||
      crypt_derive_xts_keys_argon2id(REKEY_EXEC_PASSWORD, header.kdf_salt,
                                     header.kdf_salt_len, header.kdf_t_cost,
                                     header.kdf_m_cost, k1, k2) != 0) {
    goto out;
  }
  view_ctx.lower = &r->dev;
  view_ctx.start_lba = 1u;
  view_ctx.block_count = 30u;
  view.name = "test-rekey-target";
  view.block_size = REKEY_EXEC_BLOCK_SIZE;
  view.block_count = 30u;
  view.ctx = &view_ctx;
  view.ops = &g_rekey_exec_offset_ops;
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

static int test_checkpoint_write_success(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct rekey_exec_ram_dev *r = NULL;
  struct volume_provider_rekey_execution_report report;
  struct volume_provider_rekey_checkpoint checkpoint;
  uint8_t scratch[REKEY_EXEC_BLOCK_SIZE];
  uint8_t source_before[REKEY_EXEC_BLOCK_SIZE];
  uint8_t source_after[REKEY_EXEC_BLOCK_SIZE];
  if (setup_ready_legacy(&r) != 0) return 1;
  fails += expect_int(block_device_read(&r->dev, 0u, source_before), 0,
                      "read source before checkpoint");
  fails += expect_int(volume_provider_rekey_execute_checkpoint(
                          &r->dev, REKEY_EXEC_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_SCRATCH_CHECKPOINT_WRITE,
                          &report),
                      0, "checkpoint execute rc");
  fails += expect_int((int)report.status,
                      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_CHECKPOINT_WRITTEN,
                      "checkpoint written status");
  fails += expect_int((int)report.scratch_first_lba, 31,
                      "checkpoint scratch lba");
  fails += expect_true((report.phase_flags &
                        VOLUME_PROVIDER_REKEY_EXEC_PHASE_CHECKPOINT_SCRATCH) !=
                           0,
                       "checkpoint scratch phase");
  fails += expect_int(block_device_read(&r->dev, 31u, scratch), 0,
                      "read written checkpoint");
  fails += expect_int(volume_provider_rekey_checkpoint_parse(
                          scratch, sizeof(scratch), &checkpoint),
                      0, "parse written checkpoint");
  fails += expect_int((int)checkpoint.blocks_completed, 0,
                      "written checkpoint starts at zero");
  fails += expect_int((int)checkpoint.next_source_lba, 29,
                      "written checkpoint next source");
  fails += expect_int((int)checkpoint.next_target_lba, 30,
                      "written checkpoint next target");
  fails += expect_int(block_device_read(&r->dev, 0u, source_after), 0,
                      "read source after checkpoint");
  fails += expect_true(memcmp(source_before, source_after,
                              sizeof(source_before)) == 0,
                       "checkpoint writer must preserve source block");
  rekey_exec_ram_free(r);
  return fails;
}

static int test_checkpoint_write_requires_flag(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct rekey_exec_ram_dev *r = NULL;
  struct volume_provider_rekey_execution_report report;
  uint8_t scratch[REKEY_EXEC_BLOCK_SIZE];
  if (setup_ready_legacy(&r) != 0) return 1;
  fails += expect_int(volume_provider_rekey_execute_checkpoint(
                          &r->dev, REKEY_EXEC_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_DRY_RUN, &report),
                      0, "checkpoint no flag rc");
  fails += expect_int((int)report.status,
                      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_WRITES_DISABLED,
                      "checkpoint no flag status");
  fails += expect_int(block_device_read(&r->dev, 31u, scratch), 0,
                      "read unwritten scratch");
  for (size_t i = 0; i < sizeof(scratch); ++i) {
    fails += expect_int((int)scratch[i], 0, "scratch remains zero");
  }
  rekey_exec_ram_free(r);
  return fails;
}

static int test_checkpoint_write_blocked_by_plan(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct rekey_exec_ram_dev *r = rekey_exec_ram_alloc(REKEY_EXEC_BLOCK_COUNT);
  struct volume_provider_rekey_execution_report report;
  if (!r) return 1;
  fails += expect_int(write_rekey_exec_legacy_super(
                          r, legacy_salt, sizeof(legacy_salt), 1000u,
                          REKEY_EXEC_BLOCK_COUNT - 1u),
                      0, "write no-scratch legacy");
  fails += expect_int(volume_provider_rekey_execute_checkpoint(
                          &r->dev, REKEY_EXEC_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_SCRATCH_CHECKPOINT_WRITE,
                          &report),
                      0, "checkpoint blocked rc");
  fails += expect_int((int)report.status,
                      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_BLOCKED_BY_PLAN,
                      "checkpoint blocked status");
  rekey_exec_ram_free(r);
  return fails;
}

static int test_checkpoint_write_header_managed_noop(void) {
  int fails = 0;
  struct rekey_exec_ram_dev *r = rekey_exec_ram_alloc(REKEY_EXEC_BLOCK_COUNT);
  struct block_device *crypt_dev = NULL;
  struct volume_provider_rekey_execution_report report;
  if (!r) return 1;
  fails += expect_int(volume_provider_install(&r->dev, REKEY_EXEC_PASSWORD,
                                              &crypt_dev),
                      0, "install header-managed");
  fails += expect_int(volume_provider_rekey_execute_checkpoint(
                          &r->dev, REKEY_EXEC_PASSWORD, NULL, 0, 0u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_SCRATCH_CHECKPOINT_WRITE,
                          &report),
                      0, "checkpoint header-managed rc");
  fails += expect_int(
      (int)report.status,
      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_ALREADY_HEADER_MANAGED,
      "checkpoint header-managed status");
  if (crypt_dev && crypt_dev != &r->dev) crypt_free(crypt_dev);
  rekey_exec_ram_free(r);
  return fails;
}

static int test_checkpoint_write_failure_status(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct rekey_exec_ram_dev *r = NULL;
  struct volume_provider_rekey_execution_report report;
  if (setup_ready_legacy(&r) != 0) return 1;
  r->fail_write_lba = 31u;
  fails += expect_int(volume_provider_rekey_execute_checkpoint(
                          &r->dev, REKEY_EXEC_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_SCRATCH_CHECKPOINT_WRITE,
                          &report),
                      0, "checkpoint write fail rc");
  fails += expect_int(
      (int)report.status,
      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_CHECKPOINT_WRITE_FAILED,
      "checkpoint write fail status");
  rekey_exec_ram_free(r);
  return fails;
}

static int test_checkpoint_verify_failure_status(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct rekey_exec_ram_dev *r = NULL;
  struct volume_provider_rekey_execution_report report;
  if (setup_ready_legacy(&r) != 0) return 1;
  r->corrupt_write_lba = 31u;
  fails += expect_int(volume_provider_rekey_execute_checkpoint(
                          &r->dev, REKEY_EXEC_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_SCRATCH_CHECKPOINT_WRITE,
                          &report),
                      0, "checkpoint verify fail rc");
  fails += expect_int(
      (int)report.status,
      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_CHECKPOINT_VERIFY_FAILED,
      "checkpoint verify fail status");
  rekey_exec_ram_free(r);
  return fails;
}

static int test_checkpoint_bad_password_fails_closed(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct rekey_exec_ram_dev *r = NULL;
  struct volume_provider_rekey_execution_report report;
  if (setup_ready_legacy(&r) != 0) return 1;
  memset(&report, 0xA5, sizeof(report));
  fails += expect_int(volume_provider_rekey_execute_checkpoint(
                          &r->dev, REKEY_EXEC_BAD_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_SCRATCH_CHECKPOINT_WRITE,
                          &report),
                      -1, "checkpoint bad password rc");
  fails += expect_int((int)report.status, 0,
                      "checkpoint bad password clears status");
  rekey_exec_ram_free(r);
  return fails;
}

static int test_stage_header_success(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct rekey_exec_ram_dev *r = NULL;
  struct volume_provider_rekey_execution_report report;
  struct volume_provider_rekey_checkpoint checkpoint;
  struct volume_provider_rekey_stage_manifest manifest;
  struct capyos_volume_header header;
  uint8_t scratch[REKEY_EXEC_BLOCK_SIZE];
  uint8_t source_before[REKEY_EXEC_BLOCK_SIZE];
  uint8_t source_after[REKEY_EXEC_BLOCK_SIZE];
  if (setup_ready_legacy(&r) != 0) return 1;
  fails += expect_int(block_device_read(&r->dev, 0u, source_before), 0,
                      "stage read source before");
  fails += expect_int(volume_provider_rekey_execute_stage_header(
                          &r->dev, REKEY_EXEC_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_STAGED_HEADER_WRITE,
                          &report),
                      0, "stage header rc");
  fails += expect_int(
      (int)report.status,
      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_STAGED_HEADER_WRITTEN,
      "stage header status");
  fails += expect_true((report.phase_flags &
                        VOLUME_PROVIDER_REKEY_EXEC_PHASE_STAGE_HEADER) != 0,
                       "stage header phase");
  fails += expect_int(block_device_read(&r->dev, 31u, scratch), 0,
                      "stage read scratch");
  fails += expect_int(volume_provider_rekey_checkpoint_parse(
                          scratch + VOLUME_PROVIDER_REKEY_STAGE_CHECKPOINT_OFFSET,
                          VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE, &checkpoint),
                      0, "stage parse checkpoint");
  fails += expect_int(capyos_volume_header_parse(
                          scratch + VOLUME_PROVIDER_REKEY_STAGE_HEADER_OFFSET,
                          &header),
                      CAPYOS_VOLUME_HEADER_OK, "stage parse header");
  fails += expect_int(volume_provider_rekey_stage_manifest_parse(
                          scratch + VOLUME_PROVIDER_REKEY_STAGE_MANIFEST_OFFSET,
                          VOLUME_PROVIDER_REKEY_STAGE_MANIFEST_SIZE, &manifest),
                      0, "stage parse manifest");
  fails += expect_int((int)checkpoint.blocks_completed, 0,
                      "stage checkpoint starts at zero");
  fails += expect_true((checkpoint.phase_flags &
                        VOLUME_PROVIDER_REKEY_EXEC_PHASE_STAGE_HEADER) != 0,
                       "stage checkpoint phase");
  fails += expect_int((int)header.kdf_algo_id,
                      (int)CAPYOS_VOLUME_KDF_ALGO_ARGON2ID,
                      "stage header argon2id");
  fails += expect_int((int)header.data_offset_lba, 1,
                      "stage header data offset");
  fails += expect_int((int)manifest.scratch_lba, 31,
                      "stage manifest scratch lba");
  fails += expect_true(
      manifest.checkpoint_crc32 ==
          capyos_volume_header_crc32(
              scratch + VOLUME_PROVIDER_REKEY_STAGE_CHECKPOINT_OFFSET,
              VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE),
      "stage manifest checkpoint crc");
  fails += expect_true(
      manifest.staged_header_crc32 ==
          capyos_volume_header_crc32(
              scratch + VOLUME_PROVIDER_REKEY_STAGE_HEADER_OFFSET,
              VOLUME_PROVIDER_REKEY_STAGE_HEADER_SIZE),
      "stage manifest header crc");
  fails += expect_int(block_device_read(&r->dev, 0u, source_after), 0,
                      "stage read source after");
  fails += expect_true(memcmp(source_before, source_after,
                              sizeof(source_before)) == 0,
                       "stage header must preserve source block");
  rekey_exec_ram_free(r);
  return fails;
}

static int test_stage_header_requires_flag(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct rekey_exec_ram_dev *r = NULL;
  struct volume_provider_rekey_execution_report report;
  if (setup_ready_legacy(&r) != 0) return 1;
  fails += expect_int(volume_provider_rekey_execute_stage_header(
                          &r->dev, REKEY_EXEC_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_SCRATCH_CHECKPOINT_WRITE,
                          &report),
                      0, "stage wrong flag rc");
  fails += expect_int((int)report.status,
                      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_WRITES_DISABLED,
                      "stage wrong flag status");
  rekey_exec_ram_free(r);
  return fails;
}

static int test_stage_header_blocked_by_plan(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct rekey_exec_ram_dev *r = rekey_exec_ram_alloc(REKEY_EXEC_BLOCK_COUNT);
  struct volume_provider_rekey_execution_report report;
  if (!r) return 1;
  fails += expect_int(write_rekey_exec_legacy_super(
                          r, legacy_salt, sizeof(legacy_salt), 1000u,
                          REKEY_EXEC_BLOCK_COUNT - 1u),
                      0, "stage write no-scratch legacy");
  fails += expect_int(volume_provider_rekey_execute_stage_header(
                          &r->dev, REKEY_EXEC_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_STAGED_HEADER_WRITE,
                          &report),
                      0, "stage blocked rc");
  fails += expect_int((int)report.status,
                      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_BLOCKED_BY_PLAN,
                      "stage blocked status");
  rekey_exec_ram_free(r);
  return fails;
}

static int test_stage_header_header_managed_noop(void) {
  int fails = 0;
  struct rekey_exec_ram_dev *r = rekey_exec_ram_alloc(REKEY_EXEC_BLOCK_COUNT);
  struct block_device *crypt_dev = NULL;
  struct volume_provider_rekey_execution_report report;
  if (!r) return 1;
  fails += expect_int(volume_provider_install(&r->dev, REKEY_EXEC_PASSWORD,
                                              &crypt_dev),
                      0, "stage install header-managed");
  fails += expect_int(volume_provider_rekey_execute_stage_header(
                          &r->dev, REKEY_EXEC_PASSWORD, NULL, 0, 0u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_STAGED_HEADER_WRITE,
                          &report),
                      0, "stage header-managed rc");
  fails += expect_int(
      (int)report.status,
      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_ALREADY_HEADER_MANAGED,
      "stage header-managed status");
  if (crypt_dev && crypt_dev != &r->dev) crypt_free(crypt_dev);
  rekey_exec_ram_free(r);
  return fails;
}

static int test_stage_header_write_failure_status(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct rekey_exec_ram_dev *r = NULL;
  struct volume_provider_rekey_execution_report report;
  if (setup_ready_legacy(&r) != 0) return 1;
  r->fail_write_lba = 31u;
  fails += expect_int(volume_provider_rekey_execute_stage_header(
                          &r->dev, REKEY_EXEC_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_STAGED_HEADER_WRITE,
                          &report),
                      0, "stage write fail rc");
  fails += expect_int(
      (int)report.status,
      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_STAGED_HEADER_WRITE_FAILED,
      "stage write fail status");
  rekey_exec_ram_free(r);
  return fails;
}

static int test_stage_header_verify_failure_status(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct rekey_exec_ram_dev *r = NULL;
  struct volume_provider_rekey_execution_report report;
  if (setup_ready_legacy(&r) != 0) return 1;
  r->corrupt_write_lba = 31u;
  fails += expect_int(volume_provider_rekey_execute_stage_header(
                          &r->dev, REKEY_EXEC_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_STAGED_HEADER_WRITE,
                          &report),
                      0, "stage verify fail rc");
  fails += expect_int(
      (int)report.status,
      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_STAGED_HEADER_VERIFY_FAILED,
      "stage verify fail status");
  rekey_exec_ram_free(r);
  return fails;
}

static int test_stage_header_bad_password_fails_closed(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct rekey_exec_ram_dev *r = NULL;
  struct volume_provider_rekey_execution_report report;
  if (setup_ready_legacy(&r) != 0) return 1;
  memset(&report, 0xA5, sizeof(report));
  fails += expect_int(volume_provider_rekey_execute_stage_header(
                          &r->dev, REKEY_EXEC_BAD_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_STAGED_HEADER_WRITE,
                          &report),
                      -1, "stage bad password rc");
  fails += expect_int((int)report.status, 0,
                      "stage bad password clears status");
  rekey_exec_ram_free(r);
  return fails;
}

static int test_copy_step_success_one_block(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct rekey_exec_ram_dev *r = NULL;
  struct volume_provider_rekey_execution_report report;
  struct volume_provider_rekey_checkpoint checkpoint;
  uint8_t scratch[REKEY_EXEC_BLOCK_SIZE];
  uint8_t legacy_plain[REKEY_EXEC_BLOCK_SIZE];
  uint8_t target_plain[REKEY_EXEC_BLOCK_SIZE];
  if (setup_ready_legacy(&r) != 0) return 1;
  fails += expect_int(volume_provider_rekey_execute_stage_header(
                          &r->dev, REKEY_EXEC_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_STAGED_HEADER_WRITE,
                          &report),
                      0, "copy setup stage rc");
  fails += expect_int(volume_provider_rekey_execute_copy_step(
                          &r->dev, REKEY_EXEC_PASSWORD, legacy_salt,
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
  rekey_exec_ram_free(r);
  return fails;
}

static int test_copy_step_requires_flag(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct rekey_exec_ram_dev *r = NULL;
  struct volume_provider_rekey_execution_report report;
  if (setup_ready_legacy(&r) != 0) return 1;
  fails += expect_int(volume_provider_rekey_execute_copy_step(
                          &r->dev, REKEY_EXEC_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_STAGED_HEADER_WRITE,
                          &report),
                      0, "copy wrong flag rc");
  fails += expect_int((int)report.status,
                      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_WRITES_DISABLED,
                      "copy wrong flag status");
  rekey_exec_ram_free(r);
  return fails;
}

static int test_copy_step_write_failure_status(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct rekey_exec_ram_dev *r = NULL;
  struct volume_provider_rekey_execution_report report;
  if (setup_ready_legacy(&r) != 0) return 1;
  fails += expect_int(volume_provider_rekey_execute_stage_header(
                          &r->dev, REKEY_EXEC_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_STAGED_HEADER_WRITE,
                          &report),
                      0, "copy write setup stage rc");
  r->fail_write_lba = 30u;
  fails += expect_int(volume_provider_rekey_execute_copy_step(
                          &r->dev, REKEY_EXEC_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_COPY_STEP,
                          &report),
                      0, "copy write fail rc");
  fails += expect_int(
      (int)report.status,
      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_WRITE_FAILED,
      "copy write fail status");
  rekey_exec_ram_free(r);
  return fails;
}

static int test_copy_step_verify_failure_status(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct rekey_exec_ram_dev *r = NULL;
  struct volume_provider_rekey_execution_report report;
  if (setup_ready_legacy(&r) != 0) return 1;
  fails += expect_int(volume_provider_rekey_execute_stage_header(
                          &r->dev, REKEY_EXEC_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_STAGED_HEADER_WRITE,
                          &report),
                      0, "copy verify setup stage rc");
  r->corrupt_write_lba = 30u;
  fails += expect_int(volume_provider_rekey_execute_copy_step(
                          &r->dev, REKEY_EXEC_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_COPY_STEP,
                          &report),
                      0, "copy verify fail rc");
  fails += expect_int(
      (int)report.status,
      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_VERIFY_FAILED,
      "copy verify fail status");
  rekey_exec_ram_free(r);
  return fails;
}

static int test_copy_step_completes_all_blocks(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  struct rekey_exec_ram_dev *r = rekey_exec_ram_alloc(9u);
  struct volume_provider_rekey_execution_report report;
  if (!r) return 1;
  fails += expect_int(write_rekey_exec_legacy_super(
                          r, legacy_salt, sizeof(legacy_salt), 1000u, 7u),
                      0, "copy complete legacy setup");
  fails += expect_int(volume_provider_rekey_execute_stage_header(
                          &r->dev, REKEY_EXEC_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_STAGED_HEADER_WRITE,
                          &report),
                      0, "copy complete setup stage rc");
  for (uint32_t i = 0u; i < 7u; ++i) {
    fails += expect_int(volume_provider_rekey_execute_copy_step(
                            &r->dev, REKEY_EXEC_PASSWORD, legacy_salt,
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
  rekey_exec_ram_free(r);
  return fails;
}

static int test_stage_manifest_roundtrip_and_tamper(void) {
  int fails = 0;
  struct volume_provider_rekey_stage_manifest manifest;
  struct volume_provider_rekey_stage_manifest parsed;
  uint8_t buf[VOLUME_PROVIDER_REKEY_STAGE_MANIFEST_SIZE];
  memset(&manifest, 0, sizeof(manifest));
  manifest.magic0 = VOLUME_PROVIDER_REKEY_STAGE_MAGIC0;
  manifest.magic1 = VOLUME_PROVIDER_REKEY_STAGE_MAGIC1;
  manifest.version = VOLUME_PROVIDER_REKEY_STAGE_VERSION;
  manifest.checkpoint_offset = VOLUME_PROVIDER_REKEY_STAGE_CHECKPOINT_OFFSET;
  manifest.checkpoint_size = VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE;
  manifest.staged_header_offset = VOLUME_PROVIDER_REKEY_STAGE_HEADER_OFFSET;
  manifest.staged_header_size = VOLUME_PROVIDER_REKEY_STAGE_HEADER_SIZE;
  manifest.manifest_offset = VOLUME_PROVIDER_REKEY_STAGE_MANIFEST_OFFSET;
  manifest.manifest_size = VOLUME_PROVIDER_REKEY_STAGE_MANIFEST_SIZE;
  manifest.scratch_lba = 31u;
  manifest.raw_block_count = 32u;
  manifest.capyfs_block_count = 30u;
  manifest.source_first_lba = 0u;
  manifest.source_last_lba = 29u;
  manifest.target_first_lba = 1u;
  manifest.target_last_lba = 30u;
  manifest.blocks_total = 30u;
  manifest.checkpoint_crc32 = 0xAABBCCDDu;
  manifest.staged_header_crc32 = 0x11223344u;
  fails += expect_int(volume_provider_rekey_stage_manifest_serialize(
                          &manifest, buf, sizeof(buf)),
                      0, "manifest serialize");
  fails += expect_int(volume_provider_rekey_stage_manifest_parse(
                          buf, sizeof(buf), &parsed),
                      0, "manifest parse");
  fails += expect_int((int)parsed.scratch_lba, 31,
                      "manifest parsed scratch");
  memset(&parsed, 0xA5, sizeof(parsed));
  buf[72] ^= 0x01u;
  fails += expect_int(volume_provider_rekey_stage_manifest_parse(
                          buf, sizeof(buf), &parsed),
                      -1, "manifest crc tamper");
  fails += expect_int((int)parsed.magic0, 0, "manifest crc tamper clears");
  fails += expect_int(volume_provider_rekey_stage_manifest_serialize(
                          &manifest, buf, sizeof(buf)),
                      0, "manifest reserialize");
  memset(&parsed, 0xA5, sizeof(parsed));
  buf[80] = 0x7Fu;
  put_u32_le(buf + 124, capyos_volume_header_crc32(buf, 124u));
  fails += expect_int(volume_provider_rekey_stage_manifest_parse(
                          buf, sizeof(buf), &parsed),
                      -1, "manifest reserved tamper");
  fails += expect_int((int)parsed.magic0, 0,
                      "manifest reserved tamper clears");
  return fails;
}

int run_volume_provider_rekey_execute_tests(void) {
  int fails = 0;
  fails += test_checkpoint_write_success();
  fails += test_checkpoint_write_requires_flag();
  fails += test_checkpoint_write_blocked_by_plan();
  fails += test_checkpoint_write_header_managed_noop();
  fails += test_checkpoint_write_failure_status();
  fails += test_checkpoint_verify_failure_status();
  fails += test_checkpoint_bad_password_fails_closed();
  fails += test_stage_header_success();
  fails += test_stage_header_requires_flag();
  fails += test_stage_header_blocked_by_plan();
  fails += test_stage_header_header_managed_noop();
  fails += test_stage_header_write_failure_status();
  fails += test_stage_header_verify_failure_status();
  fails += test_stage_header_bad_password_fails_closed();
  fails += test_copy_step_success_one_block();
  fails += test_copy_step_requires_flag();
  fails += test_copy_step_write_failure_status();
  fails += test_copy_step_verify_failure_status();
  fails += test_copy_step_completes_all_blocks();
  fails += test_stage_manifest_roundtrip_and_tamper();
  if (fails == 0) {
    printf("[tests] volume_provider_rekey_execute OK\n");
  } else {
    printf("[tests] volume_provider_rekey_execute FAILED %d\n", fails);
  }
  return fails;
}
