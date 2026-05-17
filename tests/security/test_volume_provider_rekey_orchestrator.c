#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs/block.h"
#include "fs/capyfs.h"
#include "security/crypt.h"
#include "security/volume_header.h"
#include "security/volume_provider.h"

#define REKEY_ORCH_BLOCK_SIZE 4096u
#define REKEY_ORCH_PASSWORD "alpha-232-passphrase"
#define REKEY_ORCH_BAD_PASSWORD "alpha-232-bad-passphrase"
#define REKEY_ORCH_FAIL_LBA 0xFFFFFFFFu

struct rekey_orch_ram_dev {
  struct block_device dev;
  uint8_t *storage;
  uint32_t fail_write_lba;
};

static const uint8_t g_rekey_orch_legacy_salt[16] = {
    0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
    0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};

static int orch_ram_read(void *ctx, uint32_t lba, void *buffer) {
  struct rekey_orch_ram_dev *r = (struct rekey_orch_ram_dev *)ctx;
  if (!r || !buffer || lba >= r->dev.block_count) return -1;
  memcpy(buffer, r->storage + (size_t)lba * r->dev.block_size,
         r->dev.block_size);
  return 0;
}

static int orch_ram_write(void *ctx, uint32_t lba, const void *buffer) {
  struct rekey_orch_ram_dev *r = (struct rekey_orch_ram_dev *)ctx;
  if (!r || !buffer || lba >= r->dev.block_count) return -1;
  if (lba == r->fail_write_lba) return -1;
  memcpy(r->storage + (size_t)lba * r->dev.block_size, buffer,
         r->dev.block_size);
  return 0;
}

static struct block_device_ops g_orch_ram_ops = {
    .read_block = orch_ram_read,
    .write_block = orch_ram_write,
};

static struct rekey_orch_ram_dev *orch_ram_alloc(uint32_t count) {
  struct rekey_orch_ram_dev *r =
      (struct rekey_orch_ram_dev *)calloc(1, sizeof(*r));
  if (!r) return NULL;
  r->storage = (uint8_t *)calloc((size_t)count, REKEY_ORCH_BLOCK_SIZE);
  if (!r->storage) {
    free(r);
    return NULL;
  }
  r->dev.name = "rekey-orch-test-ram";
  r->dev.block_size = REKEY_ORCH_BLOCK_SIZE;
  r->dev.block_count = count;
  r->dev.ctx = r;
  r->dev.ops = &g_orch_ram_ops;
  r->fail_write_lba = REKEY_ORCH_FAIL_LBA;
  return r;
}

static void orch_ram_free(struct rekey_orch_ram_dev *r) {
  if (!r) return;
  free(r->storage);
  free(r);
}

static int expect_int(int got, int want, const char *what) {
  if (got != want) {
    printf("[tests] volume_provider_rekey_orchestrator: %s expected %d, got %d\n",
           what, want, got);
    return 1;
  }
  return 0;
}

static int expect_true(int cond, const char *msg) {
  if (!cond) {
    printf("[tests] volume_provider_rekey_orchestrator: %s\n", msg);
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
  for (size_t i = 0u; i < REKEY_ORCH_BLOCK_SIZE; ++i) {
    block[i] = (uint8_t)(0xC3u ^ (uint8_t)lba ^ (uint8_t)i);
  }
}

static int write_legacy_fs(struct rekey_orch_ram_dev *r,
                           uint32_t capyfs_blocks) {
  uint8_t k1[CRYPT_KEY_SIZE], k2[CRYPT_KEY_SIZE];
  uint8_t block[REKEY_ORCH_BLOCK_SIZE];
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
  memset(block, 0, sizeof(block));
  put_u32_le(block + 0, CAPYFS_MAGIC);
  put_u32_le(block + 4, CAPYFS_VERSION);
  put_u32_le(block + 8, CAPYFS_BLOCK_SIZE);
  put_u32_le(block + 12, capyfs_blocks);
  put_u32_le(block + 16, inode_count);
  put_u32_le(block + 20, bmap_start);
  put_u32_le(block + 24, imap_start);
  put_u32_le(block + 28, inode_start);
  put_u32_le(block + 32, data_start);
  crypt_derive_xts_keys(REKEY_ORCH_PASSWORD, g_rekey_orch_legacy_salt,
                        sizeof(g_rekey_orch_legacy_salt), 1000u, k1, k2);
  legacy_crypt = crypt_init(&r->dev, k1, k2);
  if (legacy_crypt && legacy_crypt != &r->dev &&
      block_device_write(legacy_crypt, 0u, block) == 0) {
    rc = 0;
    for (uint32_t lba = 1u; lba < capyfs_blocks && rc == 0; ++lba) {
      fill_data_block(block, lba);
      if (block_device_write(legacy_crypt, lba, block) != 0) rc = -1;
    }
  }
  if (legacy_crypt && legacy_crypt != &r->dev) crypt_free(legacy_crypt);
  memset(k1, 0, sizeof(k1));
  memset(k2, 0, sizeof(k2));
  memset(block, 0, sizeof(block));
  return rc;
}

static int setup_legacy(struct rekey_orch_ram_dev **out_r,
                        uint32_t capyfs_blocks) {
  struct rekey_orch_ram_dev *r = NULL;
  if (!out_r) return -1;
  *out_r = NULL;
  r = orch_ram_alloc(capyfs_blocks + 2u);
  if (!r) return -1;
  if (write_legacy_fs(r, capyfs_blocks) != 0) {
    orch_ram_free(r);
    return -1;
  }
  *out_r = r;
  return 0;
}

static int open_header_super(struct rekey_orch_ram_dev *r,
                             const char *password,
                             uint8_t out[REKEY_ORCH_BLOCK_SIZE]) {
  struct block_device *opened = NULL;
  int rc = -1;
  if (!r || !out) return -1;
  if (volume_provider_open(&r->dev, password, g_rekey_orch_legacy_salt,
                           sizeof(g_rekey_orch_legacy_salt), 1000u,
                           &opened) == 0 &&
      opened && block_device_read(opened, 0u, out) == 0) {
    rc = 0;
  }
  if (opened && opened != &r->dev) crypt_free(opened);
  return rc;
}

static int drive_orchestrator(struct rekey_orch_ram_dev *r,
                              uint32_t max_steps,
                              uint32_t expected_final_status,
                              struct volume_provider_rekey_execution_report *out) {
  int rc = -1;
  if (!r || !out) return -1;
  for (uint32_t i = 0u; i < max_steps; ++i) {
    rc = volume_provider_rekey_execute_orchestrated_step(
        &r->dev, REKEY_ORCH_PASSWORD, g_rekey_orch_legacy_salt,
        sizeof(g_rekey_orch_legacy_salt), 1000u,
        VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_ORCHESTRATE, out);
    if (rc != 0) return rc;
    if (out->status == expected_final_status) return 0;
  }
  return -1;
}

static int test_rejects_missing_or_unknown_flags(void) {
  int fails = 0;
  struct rekey_orch_ram_dev *r = NULL;
  struct volume_provider_rekey_execution_report report;
  if (setup_legacy(&r, 7u) != 0) return 1;
  fails += expect_int(volume_provider_rekey_execute_orchestrated_step(
                          &r->dev, REKEY_ORCH_PASSWORD,
                          g_rekey_orch_legacy_salt,
                          sizeof(g_rekey_orch_legacy_salt), 1000u, 0u,
                          &report),
                      0, "missing flag rc");
  fails += expect_int((int)report.status,
                      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_WRITES_DISABLED,
                      "missing flag status");
  fails += expect_int(volume_provider_rekey_execute_orchestrated_step(
                          &r->dev, REKEY_ORCH_PASSWORD,
                          g_rekey_orch_legacy_salt,
                          sizeof(g_rekey_orch_legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_ORCHESTRATE |
                              VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_COPY_STEP,
                          &report),
                      0, "unknown combo rc");
  fails += expect_int((int)report.status,
                      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_WRITES_DISABLED,
                      "unknown combo status");
  orch_ram_free(r);
  return fails;
}

static int test_orchestrates_stage_copy_commit_cleanup(void) {
  int fails = 0;
  struct rekey_orch_ram_dev *r = NULL;
  struct volume_provider_rekey_execution_report report;
  struct volume_provider_rekey_checkpoint checkpoint;
  uint8_t scratch[REKEY_ORCH_BLOCK_SIZE];
  uint8_t super[REKEY_ORCH_BLOCK_SIZE];
  if (setup_legacy(&r, 7u) != 0) return 1;

  fails += expect_int(volume_provider_rekey_execute_orchestrated_step(
                          &r->dev, REKEY_ORCH_PASSWORD,
                          g_rekey_orch_legacy_salt,
                          sizeof(g_rekey_orch_legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_ORCHESTRATE,
                          &report),
                      0, "stage rc");
  fails += expect_int(
      (int)report.status,
      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_STAGED_HEADER_WRITTEN,
      "stage status");
  fails += expect_int(block_device_read(&r->dev, 8u, scratch), 0,
                      "stage scratch read");
  fails += expect_int(volume_provider_rekey_checkpoint_parse(
                          scratch + VOLUME_PROVIDER_REKEY_STAGE_CHECKPOINT_OFFSET,
                          VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE, &checkpoint),
                      0, "stage checkpoint parse");
  fails += expect_int((int)checkpoint.blocks_completed, 0,
                      "stage checkpoint progress");

  fails += expect_int(drive_orchestrator(
                          r, 8u,
                          VOLUME_PROVIDER_REKEY_EXEC_STATUS_COMMIT_HEADER_WRITTEN,
                          &report),
                      0, "drive to commit");
  fails += expect_int((int)report.blocks_completed, 7,
                      "commit completed blocks");
  fails += expect_int(open_header_super(r, REKEY_ORCH_PASSWORD, super), 0,
                      "open header after commit");
  fails += expect_int((int)get_u32_le(super + 0), (int)CAPYFS_MAGIC,
                      "header super magic");

  fails += expect_int(volume_provider_rekey_execute_orchestrated_step(
                          &r->dev, REKEY_ORCH_PASSWORD,
                          g_rekey_orch_legacy_salt,
                          sizeof(g_rekey_orch_legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_ORCHESTRATE,
                          &report),
                      0, "cleanup rc");
  fails += expect_int((int)report.status,
                      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_CLEANUP_SCRATCH_DONE,
                      "cleanup status");
  fails += expect_int(block_device_read(&r->dev, 8u, scratch), 0,
                      "cleanup scratch read");
  fails += expect_true(block_is_zero(scratch, sizeof(scratch)),
                       "cleanup zeroes scratch");

  orch_ram_free(r);
  return fails;
}

static int test_abort_rolls_back_incrementally(void) {
  int fails = 0;
  struct rekey_orch_ram_dev *r = NULL;
  struct volume_provider_rekey_execution_report report;
  struct block_device *legacy_crypt = NULL;
  uint8_t scratch[REKEY_ORCH_BLOCK_SIZE];
  uint8_t super[REKEY_ORCH_BLOCK_SIZE];
  uint8_t k1[CRYPT_KEY_SIZE], k2[CRYPT_KEY_SIZE];
  if (setup_legacy(&r, 7u) != 0) return 1;

  for (uint32_t i = 0u; i < 3u; ++i) {
    fails += expect_int(volume_provider_rekey_execute_orchestrated_step(
                            &r->dev, REKEY_ORCH_PASSWORD,
                            g_rekey_orch_legacy_salt,
                            sizeof(g_rekey_orch_legacy_salt), 1000u,
                            VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_ORCHESTRATE,
                            &report),
                        0, "abort setup step rc");
  }
  fails += expect_int((int)report.status,
                      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_DONE,
                      "abort setup copied data");

  for (uint32_t i = 0u; i < 8u &&
                       report.status !=
                           VOLUME_PROVIDER_REKEY_EXEC_STATUS_ROLLBACK_COMPLETE;
       ++i) {
    fails += expect_int(volume_provider_rekey_execute_orchestrated_step(
                            &r->dev, REKEY_ORCH_PASSWORD,
                            g_rekey_orch_legacy_salt,
                            sizeof(g_rekey_orch_legacy_salt), 1000u,
                            VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_ORCHESTRATE |
                                VOLUME_PROVIDER_REKEY_EXEC_FLAG_ORCHESTRATE_ABORT,
                            &report),
                        0, "abort step rc");
  }
  fails += expect_int((int)report.status,
                      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_ROLLBACK_COMPLETE,
                      "abort complete status");
  fails += expect_int(block_device_read(&r->dev, 8u, scratch), 0,
                      "abort scratch read");
  fails += expect_true(block_is_zero(scratch, sizeof(scratch)),
                       "abort clears scratch");
  crypt_derive_xts_keys(REKEY_ORCH_PASSWORD, g_rekey_orch_legacy_salt,
                        sizeof(g_rekey_orch_legacy_salt), 1000u, k1, k2);
  legacy_crypt = crypt_init(&r->dev, k1, k2);
  fails += expect_true(legacy_crypt != NULL, "abort legacy crypt opens");
  if (legacy_crypt) {
    fails += expect_int(block_device_read(legacy_crypt, 0u, super), 0,
                        "abort legacy read super");
    fails += expect_int((int)get_u32_le(super + 0), (int)CAPYFS_MAGIC,
                        "abort legacy magic");
    crypt_free(legacy_crypt);
  }
  memset(k1, 0, sizeof(k1));
  memset(k2, 0, sizeof(k2));
  orch_ram_free(r);
  return fails;
}

static int test_wrong_password_fails_closed(void) {
  int fails = 0;
  struct rekey_orch_ram_dev *r = NULL;
  struct volume_provider_rekey_execution_report report;
  if (setup_legacy(&r, 7u) != 0) return 1;
  fails += expect_int(volume_provider_rekey_execute_orchestrated_step(
                          &r->dev, REKEY_ORCH_BAD_PASSWORD,
                          g_rekey_orch_legacy_salt,
                          sizeof(g_rekey_orch_legacy_salt), 1000u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_ORCHESTRATE,
                          &report),
                      -1, "wrong password rc");
  fails += expect_int((int)report.status,
                      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_UNKNOWN,
                      "wrong password zeroed status");
  orch_ram_free(r);
  return fails;
}

int run_volume_provider_rekey_orchestrator_tests(void) {
  int fails = 0;
  fails += test_rejects_missing_or_unknown_flags();
  fails += test_orchestrates_stage_copy_commit_cleanup();
  fails += test_abort_rolls_back_incrementally();
  fails += test_wrong_password_fails_closed();
  if (fails == 0) {
    printf("[tests] volume_provider_rekey_orchestrator: ok\n");
  }
  return fails;
}
