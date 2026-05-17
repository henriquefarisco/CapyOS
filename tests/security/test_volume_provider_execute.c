#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs/block.h"
#include "fs/capyfs.h"
#include "security/crypt.h"
#include "security/volume_provider.h"

#define EXEC_TEST_BLOCK_SIZE 4096u
#define EXEC_TEST_BLOCK_COUNT 32u
#define EXEC_TEST_PASSWORD "alpha-225-passphrase"
#define EXEC_TEST_BAD_PASSWORD "alpha-225-bad-passphrase"

struct exec_ram_dev {
  struct block_device dev;
  uint8_t *storage;
};

static int exec_ram_read(void *ctx, uint32_t lba, void *buffer) {
  struct exec_ram_dev *r = (struct exec_ram_dev *)ctx;
  if (!r || !buffer || lba >= r->dev.block_count) return -1;
  memcpy(buffer, r->storage + (size_t)lba * r->dev.block_size,
         r->dev.block_size);
  return 0;
}

static int exec_ram_write(void *ctx, uint32_t lba, const void *buffer) {
  struct exec_ram_dev *r = (struct exec_ram_dev *)ctx;
  if (!r || !buffer || lba >= r->dev.block_count) return -1;
  memcpy(r->storage + (size_t)lba * r->dev.block_size, buffer,
         r->dev.block_size);
  return 0;
}

static struct block_device_ops g_exec_ram_ops = {
    .read_block = exec_ram_read,
    .write_block = exec_ram_write,
};

static struct exec_ram_dev *exec_ram_alloc(uint32_t count) {
  struct exec_ram_dev *r = (struct exec_ram_dev *)calloc(1, sizeof(*r));
  if (!r) return NULL;
  r->storage = (uint8_t *)calloc((size_t)count, EXEC_TEST_BLOCK_SIZE);
  if (!r->storage) {
    free(r);
    return NULL;
  }
  r->dev.name = "exec-test-ram";
  r->dev.block_size = EXEC_TEST_BLOCK_SIZE;
  r->dev.block_count = count;
  r->dev.ctx = r;
  r->dev.ops = &g_exec_ram_ops;
  return r;
}

static void exec_ram_free(struct exec_ram_dev *r) {
  if (!r) return;
  free(r->storage);
  free(r);
}

static int expect_int(int got, int want, const char *what) {
  if (got != want) {
    printf("[tests] volume_provider_execute: %s expected %d, got %d\n", what,
           want, got);
    return 1;
  }
  return 0;
}

static int expect_true(int cond, const char *msg) {
  if (!cond) {
    printf("[tests] volume_provider_execute: %s\n", msg);
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

static int write_exec_legacy_super(struct exec_ram_dev *r,
                                   const uint8_t *legacy_salt,
                                   size_t legacy_salt_len,
                                   uint32_t legacy_iter,
                                   uint32_t capyfs_blocks) {
  uint8_t k1[CRYPT_KEY_SIZE], k2[CRYPT_KEY_SIZE];
  uint8_t super[EXEC_TEST_BLOCK_SIZE];
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
  crypt_derive_xts_keys(EXEC_TEST_PASSWORD, legacy_salt, legacy_salt_len,
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

static int test_execute_dry_run_ready_reports_all_phases(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  const uint32_t legacy_iter = 1000u;
  const uint32_t capyfs_blocks = EXEC_TEST_BLOCK_COUNT - 2u;
  struct exec_ram_dev *r = exec_ram_alloc(EXEC_TEST_BLOCK_COUNT);
  if (!r) return 1;
  fails += expect_int(write_exec_legacy_super(r, legacy_salt,
                                             sizeof(legacy_salt), legacy_iter,
                                             capyfs_blocks),
                      0, "write legacy super");
  uint8_t before[EXEC_TEST_BLOCK_SIZE];
  uint8_t after[EXEC_TEST_BLOCK_SIZE];
  fails += expect_int(block_device_read(&r->dev, 0u, before), 0,
                      "read before dry-run");
  struct volume_provider_rekey_execution_report report;
  fails += expect_int(volume_provider_rekey_execute(
                          &r->dev, EXEC_TEST_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), legacy_iter,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_DRY_RUN, &report),
                      0, "dry-run execute rc");
  fails += expect_int(block_device_read(&r->dev, 0u, after), 0,
                      "read after dry-run");
  fails += expect_true(memcmp(before, after, sizeof(before)) == 0,
                       "dry-run must be read-only");
  fails += expect_int((int)report.status,
                      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_DRY_RUN_READY,
                      "dry-run status");
  fails += expect_int((int)report.plan_status,
                      (int)VOLUME_PROVIDER_REKEY_PLAN_STATUS_READY,
                      "dry-run plan status");
  fails += expect_true((report.phase_flags &
                        VOLUME_PROVIDER_REKEY_EXEC_PHASE_VALIDATE_PLAN) != 0,
                       "dry-run validate phase");
  fails += expect_true((report.phase_flags &
                        VOLUME_PROVIDER_REKEY_EXEC_PHASE_CHECKPOINT_SCRATCH) !=
                           0,
                       "dry-run scratch phase");
  fails += expect_true((report.phase_flags &
                        VOLUME_PROVIDER_REKEY_EXEC_PHASE_STAGE_HEADER) != 0,
                       "dry-run stage-header phase");
  fails += expect_true((report.phase_flags &
                        VOLUME_PROVIDER_REKEY_EXEC_PHASE_COPY_REVERSE) != 0,
                       "dry-run copy phase");
  fails += expect_true((report.phase_flags &
                        VOLUME_PROVIDER_REKEY_EXEC_PHASE_COMMIT_HEADER) != 0,
                       "dry-run commit phase");
  fails += expect_true((report.phase_flags &
                        VOLUME_PROVIDER_REKEY_EXEC_PHASE_VERIFY_OPEN) != 0,
                       "dry-run verify phase");
  fails += expect_int((int)report.next_source_lba, (int)(capyfs_blocks - 1u),
                      "dry-run next source");
  fails += expect_int((int)report.next_target_lba, (int)capyfs_blocks,
                      "dry-run next target");
  fails += expect_int((int)report.scratch_first_lba,
                      (int)(capyfs_blocks + 1u), "dry-run scratch lba");
  exec_ram_free(r);
  return fails;
}

static int test_execute_without_dry_run_refuses_writes(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  const uint32_t legacy_iter = 1000u;
  struct exec_ram_dev *r = exec_ram_alloc(EXEC_TEST_BLOCK_COUNT);
  if (!r) return 1;
  fails += expect_int(write_exec_legacy_super(r, legacy_salt,
                                             sizeof(legacy_salt), legacy_iter,
                                             EXEC_TEST_BLOCK_COUNT - 2u),
                      0, "write legacy for writes-disabled");
  uint8_t before[EXEC_TEST_BLOCK_SIZE];
  uint8_t after[EXEC_TEST_BLOCK_SIZE];
  fails += expect_int(block_device_read(&r->dev, 0u, before), 0,
                      "read before writes-disabled");
  struct volume_provider_rekey_execution_report report;
  fails += expect_int(volume_provider_rekey_execute(
                          &r->dev, EXEC_TEST_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), legacy_iter, 0u, &report),
                      0, "writes-disabled execute rc");
  fails += expect_int(block_device_read(&r->dev, 0u, after), 0,
                      "read after writes-disabled");
  fails += expect_true(memcmp(before, after, sizeof(before)) == 0,
                       "writes-disabled must be read-only");
  fails += expect_int((int)report.status,
                      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_WRITES_DISABLED,
                      "writes-disabled status");
  fails += expect_int((int)report.phase_flags, 0,
                      "writes-disabled has no phases");
  exec_ram_free(r);
  return fails;
}

static int test_execute_rejects_unknown_flags(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  const uint32_t legacy_iter = 1000u;
  struct exec_ram_dev *r = exec_ram_alloc(EXEC_TEST_BLOCK_COUNT);
  if (!r) return 1;
  fails += expect_int(write_exec_legacy_super(r, legacy_salt,
                                             sizeof(legacy_salt), legacy_iter,
                                             EXEC_TEST_BLOCK_COUNT - 2u),
                      0, "write legacy for unknown flags");
  struct volume_provider_rekey_execution_report report;
  fails += expect_int(
      volume_provider_rekey_execute(
          &r->dev, EXEC_TEST_PASSWORD, legacy_salt, sizeof(legacy_salt),
          legacy_iter, VOLUME_PROVIDER_REKEY_EXEC_FLAG_DRY_RUN | 0x80000000u,
          &report),
      0, "unknown flags execute rc");
  fails += expect_int((int)report.status,
                      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_WRITES_DISABLED,
                      "unknown flags writes disabled");
  exec_ram_free(r);
  return fails;
}

static int test_execute_blocked_by_plan(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  const uint32_t legacy_iter = 1000u;
  struct exec_ram_dev *r = exec_ram_alloc(EXEC_TEST_BLOCK_COUNT);
  if (!r) return 1;
  fails += expect_int(write_exec_legacy_super(r, legacy_salt,
                                             sizeof(legacy_salt), legacy_iter,
                                             EXEC_TEST_BLOCK_COUNT - 1u),
                      0, "write no-scratch legacy");
  struct volume_provider_rekey_execution_report report;
  fails += expect_int(volume_provider_rekey_execute(
                          &r->dev, EXEC_TEST_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), legacy_iter,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_DRY_RUN, &report),
                      0, "blocked execute rc");
  fails += expect_int((int)report.status,
                      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_BLOCKED_BY_PLAN,
                      "blocked execute status");
  fails += expect_int(
      (int)report.plan_status,
      (int)VOLUME_PROVIDER_REKEY_PLAN_STATUS_BLOCKED_SCRATCH_REQUIRED,
      "blocked plan status");
  fails += expect_true(
      (report.blocker_flags &
       VOLUME_PROVIDER_REKEY_BLOCK_TRANSACTION_SCRATCH_REQUIRED) != 0,
      "blocked scratch flag");
  exec_ram_free(r);
  return fails;
}

static int test_execute_already_header_managed(void) {
  int fails = 0;
  struct exec_ram_dev *r = exec_ram_alloc(EXEC_TEST_BLOCK_COUNT);
  if (!r) return 1;
  struct block_device *crypt_dev = NULL;
  fails += expect_int(volume_provider_install(&r->dev, EXEC_TEST_PASSWORD,
                                              &crypt_dev),
                      0, "install header-managed for execute");
  struct volume_provider_rekey_execution_report report;
  fails += expect_int(volume_provider_rekey_execute(
                          &r->dev, EXEC_TEST_PASSWORD, NULL, 0, 0u,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_DRY_RUN, &report),
                      0, "execute header-managed rc");
  fails += expect_int(
      (int)report.status,
      (int)VOLUME_PROVIDER_REKEY_EXEC_STATUS_ALREADY_HEADER_MANAGED,
      "execute header-managed status");
  fails += expect_int(
      (int)report.plan_status,
      (int)VOLUME_PROVIDER_REKEY_PLAN_STATUS_ALREADY_HEADER_MANAGED,
      "execute header-managed plan");
  exec_ram_free(r);
  return fails;
}

static int test_execute_fail_closed_bad_password(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  const uint32_t legacy_iter = 1000u;
  struct exec_ram_dev *r = exec_ram_alloc(EXEC_TEST_BLOCK_COUNT);
  if (!r) return 1;
  fails += expect_int(write_exec_legacy_super(r, legacy_salt,
                                             sizeof(legacy_salt), legacy_iter,
                                             EXEC_TEST_BLOCK_COUNT - 2u),
                      0, "write legacy for bad password");
  struct volume_provider_rekey_execution_report report;
  memset(&report, 0xA5, sizeof(report));
  fails += expect_int(volume_provider_rekey_execute(
                          &r->dev, EXEC_TEST_BAD_PASSWORD, legacy_salt,
                          sizeof(legacy_salt), legacy_iter,
                          VOLUME_PROVIDER_REKEY_EXEC_FLAG_DRY_RUN, &report),
                      -1, "bad password execute rc");
  fails += expect_int((int)report.status, 0, "bad password clears status");
  fails += expect_int((int)report.plan_status, 0,
                      "bad password clears plan status");
  exec_ram_free(r);
  return fails;
}

static struct volume_provider_rekey_plan make_checkpoint_ready_plan(void) {
  struct volume_provider_rekey_plan plan;
  memset(&plan, 0, sizeof(plan));
  plan.status = VOLUME_PROVIDER_REKEY_PLAN_STATUS_READY;
  plan.source_layout = VOLUME_PROVIDER_LAYOUT_LEGACY_FULL_DEVICE;
  plan.target_layout = VOLUME_PROVIDER_LAYOUT_HEADER_MANAGED;
  plan.raw_block_count = 32u;
  plan.capyfs_block_count = 30u;
  plan.source_first_lba = 0u;
  plan.source_last_lba = 29u;
  plan.target_first_lba = 1u;
  plan.target_last_lba = 30u;
  plan.scratch_first_lba = 31u;
  plan.scratch_available_blocks = 1u;
  plan.blocks_to_reencrypt = 30u;
  plan.copy_direction = VOLUME_PROVIDER_REKEY_COPY_DIRECTION_REVERSE;
  plan.estimated_read_ops = 30u;
  plan.estimated_write_ops = 61u;
  return plan;
}

static int test_checkpoint_roundtrip(void) {
  int fails = 0;
  struct volume_provider_rekey_plan plan = make_checkpoint_ready_plan();
  struct volume_provider_rekey_checkpoint cp;
  struct volume_provider_rekey_checkpoint parsed;
  uint8_t buf[VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE];
  fails += expect_int(volume_provider_rekey_checkpoint_init(&plan, 7u, &cp), 0,
                      "checkpoint init");
  fails += expect_int((int)cp.flags,
                      (int)VOLUME_PROVIDER_REKEY_CHECKPOINT_FLAG_IN_PROGRESS,
                      "checkpoint in progress");
  fails += expect_int((int)cp.next_source_lba, 22, "checkpoint next source");
  fails += expect_int((int)cp.next_target_lba, 23, "checkpoint next target");
  fails += expect_int(volume_provider_rekey_checkpoint_serialize(
                          &cp, buf, sizeof(buf)),
                      0, "checkpoint serialize");
  for (size_t i = 88u; i < 124u; ++i) {
    fails += expect_int((int)buf[i], 0, "checkpoint reserved zero");
  }
  fails += expect_int(volume_provider_rekey_checkpoint_parse(
                          buf, sizeof(buf), &parsed),
                      0, "checkpoint parse");
  fails += expect_int((int)parsed.blocks_completed, 7,
                      "checkpoint parsed completed");
  fails += expect_int((int)parsed.checkpoint_crc32 != 0, 1,
                      "checkpoint parsed crc");
  return fails;
}

static int test_checkpoint_completed_roundtrip(void) {
  int fails = 0;
  struct volume_provider_rekey_plan plan = make_checkpoint_ready_plan();
  struct volume_provider_rekey_checkpoint cp;
  uint8_t buf[VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE];
  fails += expect_int(volume_provider_rekey_checkpoint_init(
                          &plan, plan.blocks_to_reencrypt, &cp),
                      0, "checkpoint completed init");
  fails += expect_int((int)cp.flags,
                      (int)VOLUME_PROVIDER_REKEY_CHECKPOINT_FLAG_COMPLETED,
                      "checkpoint completed flag");
  fails += expect_true((cp.phase_flags &
                        VOLUME_PROVIDER_REKEY_EXEC_PHASE_COMMIT_HEADER) != 0,
                       "checkpoint completed commit phase");
  fails += expect_true((cp.phase_flags &
                        VOLUME_PROVIDER_REKEY_EXEC_PHASE_VERIFY_OPEN) != 0,
                       "checkpoint completed verify phase");
  fails += expect_int(volume_provider_rekey_checkpoint_serialize(
                          &cp, buf, sizeof(buf)),
                      0, "checkpoint completed serialize");
  return fails;
}

static int test_checkpoint_tamper_fails_closed(void) {
  int fails = 0;
  struct volume_provider_rekey_plan plan = make_checkpoint_ready_plan();
  struct volume_provider_rekey_checkpoint cp;
  struct volume_provider_rekey_checkpoint parsed;
  uint8_t buf[VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE];
  fails += expect_int(volume_provider_rekey_checkpoint_init(&plan, 0u, &cp), 0,
                      "checkpoint tamper init");
  fails += expect_int(volume_provider_rekey_checkpoint_serialize(
                          &cp, buf, sizeof(buf)),
                      0, "checkpoint tamper serialize");
  memset(&parsed, 0xA5, sizeof(parsed));
  buf[72] ^= 0x01u;
  fails += expect_int(volume_provider_rekey_checkpoint_parse(
                          buf, sizeof(buf), &parsed),
                      -1, "checkpoint crc tamper parse");
  fails += expect_int((int)parsed.magic0, 0, "checkpoint tamper clears out");
  fails += expect_int(volume_provider_rekey_checkpoint_serialize(
                          &cp, buf, sizeof(buf)),
                      0, "checkpoint reserved serialize");
  memset(&parsed, 0xA5, sizeof(parsed));
  buf[100] = 0x7Fu;
  fails += expect_int(volume_provider_rekey_checkpoint_parse(
                          buf, sizeof(buf), &parsed),
                      -1, "checkpoint reserved tamper parse");
  fails += expect_int((int)parsed.magic0, 0,
                      "checkpoint reserved tamper clears out");
  return fails;
}

static int test_checkpoint_rejects_invalid_plan(void) {
  int fails = 0;
  struct volume_provider_rekey_plan plan = make_checkpoint_ready_plan();
  struct volume_provider_rekey_checkpoint cp;
  plan.status = VOLUME_PROVIDER_REKEY_PLAN_STATUS_BLOCKED_SCRATCH_REQUIRED;
  fails += expect_int(volume_provider_rekey_checkpoint_init(&plan, 0u, &cp),
                      -1, "checkpoint rejects blocked plan");
  fails += expect_int((int)cp.magic0, 0, "checkpoint invalid clears out");
  plan = make_checkpoint_ready_plan();
  fails += expect_int(volume_provider_rekey_checkpoint_init(
                          &plan, plan.blocks_to_reencrypt + 1u, &cp),
                      -1, "checkpoint rejects overcomplete");
  return fails;
}

static int test_checkpoint_rejects_bad_semantics(void) {
  int fails = 0;
  struct volume_provider_rekey_plan plan = make_checkpoint_ready_plan();
  struct volume_provider_rekey_checkpoint cp;
  uint8_t buf[VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE];
  fails += expect_int(volume_provider_rekey_checkpoint_init(&plan, 3u, &cp), 0,
                      "checkpoint semantics init");
  cp.phase_flags |= 0x80000000u;
  fails += expect_int(volume_provider_rekey_checkpoint_serialize(
                          &cp, buf, sizeof(buf)),
                      -1, "checkpoint rejects unknown phase");
  fails += expect_int(volume_provider_rekey_checkpoint_init(&plan, 0u, &cp), 0,
                      "checkpoint stage phase init");
  cp.phase_flags |= VOLUME_PROVIDER_REKEY_EXEC_PHASE_STAGE_HEADER;
  fails += expect_int(volume_provider_rekey_checkpoint_serialize(
                          &cp, buf, sizeof(buf)),
                      0, "checkpoint accepts stage phase");
  fails += expect_int(volume_provider_rekey_checkpoint_init(&plan, 3u, &cp), 0,
                      "checkpoint semantics reinit");
  cp.phase_flags |= VOLUME_PROVIDER_REKEY_EXEC_PHASE_COMMIT_HEADER;
  fails += expect_int(volume_provider_rekey_checkpoint_serialize(
                          &cp, buf, sizeof(buf)),
                      -1, "checkpoint rejects early commit phase");
  plan = make_checkpoint_ready_plan();
  plan.target_last_lba = 31u;
  fails += expect_int(volume_provider_rekey_checkpoint_init(&plan, 0u, &cp),
                      -1, "checkpoint rejects bad target range");
  return fails;
}

int run_volume_provider_execute_tests(void) {
  int fails = 0;
  fails += test_execute_dry_run_ready_reports_all_phases();
  fails += test_execute_without_dry_run_refuses_writes();
  fails += test_execute_rejects_unknown_flags();
  fails += test_execute_blocked_by_plan();
  fails += test_execute_already_header_managed();
  fails += test_execute_fail_closed_bad_password();
  fails += test_checkpoint_roundtrip();
  fails += test_checkpoint_completed_roundtrip();
  fails += test_checkpoint_tamper_fails_closed();
  fails += test_checkpoint_rejects_invalid_plan();
  fails += test_checkpoint_rejects_bad_semantics();
  if (fails == 0) {
    printf("[tests] volume_provider_execute OK\n");
  } else {
    printf("[tests] volume_provider_execute FAILED %d\n", fails);
  }
  return fails;
}
