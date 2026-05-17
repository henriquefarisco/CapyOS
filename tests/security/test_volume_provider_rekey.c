/*
 * Host-side tests for the volume provider rekey orchestration surface:
 *
 *   - volume_provider_rekey_preflight
 *   - volume_provider_rekey_plan
 *
 * Sibling test TU of `test_volume_provider.c`, extracted in the
 * 2026-05-16 preventive refactor when the parent reached 867/900 LOC.
 * Each test TU carries its own private copy of the in-RAM block-device
 * fixture infrastructure and assertion helpers so the two files can
 * grow independently without re-crossing the audit ceiling. The
 * source-side rekey orchestration lives in
 * `src/security/volume_provider_rekey.c` and the install/open path
 * stays in `src/security/volume_provider.c`.
 *
 * The runner exports `run_volume_provider_rekey_tests`, declared and
 * invoked from `tests/test_runner.c` directly after the existing
 * `run_volume_provider_tests` entry.
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

#define TEST_BLOCK_SIZE 4096u
#define TEST_BLOCK_COUNT 32u
#define TEST_PASSWORD "alpha-222-passphrase"
#define TEST_BAD_PASSWORD "wrong-pass-on-purpose"

/* ---- in-memory block device backend ----------------------------- */

struct ram_dev {
  struct block_device dev;
  uint8_t *storage;
  int force_read_fail_lba;
};

static int ram_read_block(void *ctx, uint32_t lba, void *buffer) {
  struct ram_dev *r = (struct ram_dev *)ctx;
  if (!r || !buffer) return -1;
  if (r->force_read_fail_lba >= 0 && (uint32_t)r->force_read_fail_lba == lba) {
    return -1;
  }
  if (lba >= r->dev.block_count) return -1;
  memcpy(buffer, r->storage + (size_t)lba * r->dev.block_size,
         r->dev.block_size);
  return 0;
}

static int ram_write_block(void *ctx, uint32_t lba, const void *buffer) {
  struct ram_dev *r = (struct ram_dev *)ctx;
  if (!r || !buffer) return -1;
  if (lba >= r->dev.block_count) return -1;
  memcpy(r->storage + (size_t)lba * r->dev.block_size, buffer,
         r->dev.block_size);
  return 0;
}

static struct block_device_ops g_ram_ops = {
    .read_block = ram_read_block,
    .write_block = ram_write_block,
};

static struct ram_dev *ram_alloc(uint32_t count) {
  struct ram_dev *r = (struct ram_dev *)calloc(1, sizeof(*r));
  if (!r) return NULL;
  r->storage = (uint8_t *)calloc((size_t)count, TEST_BLOCK_SIZE);
  if (!r->storage) {
    free(r);
    return NULL;
  }
  r->dev.name = "test-ram-rekey";
  r->dev.block_size = TEST_BLOCK_SIZE;
  r->dev.block_count = count;
  r->dev.ctx = r;
  r->dev.ops = &g_ram_ops;
  r->force_read_fail_lba = -1;
  return r;
}

static void ram_free(struct ram_dev *r) {
  if (!r) return;
  free(r->storage);
  free(r);
}

/* ---- assertion helpers ------------------------------------------ */

static int expect_int(int got, int want, const char *what) {
  if (got != want) {
    printf("[tests] volume_provider_rekey: %s expected %d, got %d\n", what,
           want, got);
    return 1;
  }
  return 0;
}

static int expect_true(int cond, const char *msg) {
  if (!cond) {
    printf("[tests] volume_provider_rekey: %s\n", msg);
    return 1;
  }
  return 0;
}

static void test_put_u32_le(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFFu);
  p[1] = (uint8_t)((v >> 8) & 0xFFu);
  p[2] = (uint8_t)((v >> 16) & 0xFFu);
  p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static int write_legacy_capyfs_super(struct ram_dev *r,
                                     const uint8_t *legacy_salt,
                                     size_t legacy_salt_len,
                                     uint32_t legacy_iter,
                                     uint32_t capyfs_blocks) {
  uint8_t k1[CRYPT_KEY_SIZE], k2[CRYPT_KEY_SIZE];
  uint8_t super[TEST_BLOCK_SIZE];
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
  if (data_start >= capyfs_blocks) return -1;
  for (size_t i = 0; i < sizeof(super); ++i) super[i] = 0;
  test_put_u32_le(super + 0, CAPYFS_MAGIC);
  test_put_u32_le(super + 4, CAPYFS_VERSION);
  test_put_u32_le(super + 8, CAPYFS_BLOCK_SIZE);
  test_put_u32_le(super + 12, capyfs_blocks);
  test_put_u32_le(super + 16, inode_count);
  test_put_u32_le(super + 20, bmap_start);
  test_put_u32_le(super + 24, imap_start);
  test_put_u32_le(super + 28, inode_start);
  test_put_u32_le(super + 32, data_start);
  crypt_derive_xts_keys(TEST_PASSWORD, legacy_salt, legacy_salt_len,
                        legacy_iter, k1, k2);
  legacy_crypt = crypt_init(&r->dev, k1, k2);
  if (legacy_crypt && legacy_crypt != &r->dev &&
      block_device_write(legacy_crypt, 0u, super) == 0) {
    rc = 0;
  }
  if (legacy_crypt && legacy_crypt != &r->dev) crypt_free(legacy_crypt);
  for (size_t i = 0; i < CRYPT_KEY_SIZE; ++i) {
    k1[i] = 0;
    k2[i] = 0;
  }
  for (size_t i = 0; i < sizeof(super); ++i) super[i] = 0;
  return rc;
}

static int test_rekey_preflight_header_managed_blocks_rekey(void) {
  int fails = 0;
  struct ram_dev *r = ram_alloc(TEST_BLOCK_COUNT);
  if (!r) return 1;
  struct block_device *crypt_dev = NULL;
  fails += expect_int(volume_provider_install(&r->dev, TEST_PASSWORD,
                                              &crypt_dev),
                      0, "install header-managed rc");
  struct volume_provider_rekey_preflight pf;
  fails += expect_int(volume_provider_rekey_preflight(&r->dev, TEST_PASSWORD,
                                                      NULL, 0, 0u, &pf),
                      0, "preflight header-managed rc");
  fails += expect_int((int)pf.status,
                      (int)VOLUME_PROVIDER_REKEY_STATUS_ALREADY_HEADER_MANAGED,
                      "header-managed status");
  fails += expect_int((int)pf.source_layout,
                      (int)VOLUME_PROVIDER_LAYOUT_HEADER_MANAGED,
                      "header-managed source layout");
  fails += expect_int((int)pf.blocker_flags,
                      (int)VOLUME_PROVIDER_REKEY_BLOCK_ALREADY_HEADER_MANAGED,
                      "header-managed blocker");
  fails += expect_int((int)pf.target_visible_blocks,
                      (int)(TEST_BLOCK_COUNT - 1u),
                      "header-managed visible blocks");
  ram_free(r);
  return fails;
}

static int test_rekey_preflight_legacy_requires_relocation_and_shrink(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  const uint32_t legacy_iter = 1000u;
  struct ram_dev *r = ram_alloc(TEST_BLOCK_COUNT);
  if (!r) return 1;
  fails += expect_int(write_legacy_capyfs_super(r, legacy_salt,
                                                sizeof(legacy_salt),
                                                legacy_iter,
                                                TEST_BLOCK_COUNT),
                      0, "write legacy capyfs super");
  uint8_t before[TEST_BLOCK_SIZE];
  uint8_t after[TEST_BLOCK_SIZE];
  fails += expect_int(block_device_read(&r->dev, 0u, before), 0,
                      "read legacy raw before");
  struct volume_provider_rekey_preflight pf;
  fails += expect_int(volume_provider_rekey_preflight(&r->dev, TEST_PASSWORD,
                                                      legacy_salt,
                                                      sizeof(legacy_salt),
                                                      legacy_iter, &pf),
                      0, "preflight legacy rc");
  fails += expect_int(block_device_read(&r->dev, 0u, after), 0,
                      "read legacy raw after");
  fails += expect_true(memcmp(before, after, sizeof(before)) == 0,
                       "preflight must be read-only");
  fails += expect_int((int)pf.status,
                      (int)VOLUME_PROVIDER_REKEY_STATUS_LEGACY_RELOCATION_REQUIRED,
                      "legacy status");
  fails += expect_int((int)pf.source_layout,
                      (int)VOLUME_PROVIDER_LAYOUT_LEGACY_FULL_DEVICE,
                      "legacy source layout");
  fails += expect_int((int)pf.source_visible_blocks, (int)TEST_BLOCK_COUNT,
                      "legacy source visible blocks");
  fails += expect_int((int)pf.target_visible_blocks,
                      (int)(TEST_BLOCK_COUNT - 1u),
                      "legacy target visible blocks");
  fails += expect_int((int)pf.capyfs_block_count, (int)TEST_BLOCK_COUNT,
                      "legacy capyfs block count");
  fails += expect_true((pf.action_flags &
                        VOLUME_PROVIDER_REKEY_ACTION_RESERVE_HEADER_LBA0) != 0,
                       "legacy preflight reserves header LBA");
  fails += expect_true((pf.action_flags &
                        VOLUME_PROVIDER_REKEY_ACTION_SHIFT_FS_TO_LBA1) != 0,
                       "legacy preflight shifts fs");
  fails += expect_true(
      (pf.action_flags &
       VOLUME_PROVIDER_REKEY_ACTION_REENCRYPT_WITH_HEADER_KEYS) != 0,
      "legacy preflight reencrypts");
  fails += expect_true(
      (pf.action_flags &
       VOLUME_PROVIDER_REKEY_ACTION_UPDATE_CAPYFS_GEOMETRY) != 0,
      "legacy full-disk capyfs needs geometry update");
  fails += expect_true(
      (pf.blocker_flags &
       VOLUME_PROVIDER_REKEY_BLOCK_RELOCATION_ENGINE_REQUIRED) != 0,
      "legacy relocation blocker");
  fails += expect_true(
      (pf.blocker_flags & VOLUME_PROVIDER_REKEY_BLOCK_CAPYFS_SHRINK_REQUIRED) !=
          0,
      "legacy shrink blocker");
  ram_free(r);
  return fails;
}

static int test_rekey_preflight_legacy_partial_volume_no_shrink(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  const uint32_t legacy_iter = 1000u;
  struct ram_dev *r = ram_alloc(TEST_BLOCK_COUNT);
  if (!r) return 1;
  fails += expect_int(write_legacy_capyfs_super(r, legacy_salt,
                                                sizeof(legacy_salt),
                                                legacy_iter,
                                                TEST_BLOCK_COUNT - 1u),
                      0, "write partial legacy capyfs super");
  struct volume_provider_rekey_preflight pf;
  fails += expect_int(volume_provider_rekey_preflight(&r->dev, TEST_PASSWORD,
                                                      legacy_salt,
                                                      sizeof(legacy_salt),
                                                      legacy_iter, &pf),
                      0, "preflight partial legacy rc");
  fails += expect_true(
      (pf.action_flags &
       VOLUME_PROVIDER_REKEY_ACTION_UPDATE_CAPYFS_GEOMETRY) == 0,
      "partial legacy capyfs does not need shrink");
  fails += expect_true(
      (pf.blocker_flags & VOLUME_PROVIDER_REKEY_BLOCK_CAPYFS_SHRINK_REQUIRED) ==
          0,
      "partial legacy capyfs has no shrink blocker");
  ram_free(r);
  return fails;
}

static int test_rekey_preflight_fail_closed(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  const uint32_t legacy_iter = 1000u;
  struct ram_dev *r = ram_alloc(TEST_BLOCK_COUNT);
  if (!r) return 1;
  fails += expect_int(write_legacy_capyfs_super(r, legacy_salt,
                                                sizeof(legacy_salt),
                                                legacy_iter,
                                                TEST_BLOCK_COUNT),
                      0, "write legacy for fail-closed preflight");
  struct volume_provider_rekey_preflight pf;
  memset(&pf, 0xA5, sizeof(pf));
  fails += expect_int(volume_provider_rekey_preflight(NULL, TEST_PASSWORD,
                                                      legacy_salt,
                                                      sizeof(legacy_salt),
                                                      legacy_iter, &pf),
                      -1, "preflight NULL device");
  fails += expect_int((int)pf.status, 0,
                      "preflight NULL device clears status");
  fails += expect_int((int)pf.source_layout, 0,
                      "preflight NULL device clears layout");
  memset(&pf, 0xA5, sizeof(pf));
  fails += expect_int(volume_provider_rekey_preflight(&r->dev, TEST_BAD_PASSWORD,
                                                      legacy_salt,
                                                      sizeof(legacy_salt),
                                                      legacy_iter, &pf),
                      -1, "preflight bad password");
  fails += expect_int((int)pf.status, 0, "preflight bad password clears status");
  fails += expect_int((int)pf.source_layout, 0,
                      "preflight bad password clears layout");
  ram_free(r);
  return fails;
}

/* ---- alpha.224 transaction planner ----------------------------- */

static int test_rekey_plan_header_managed_noop(void) {
  int fails = 0;
  struct ram_dev *r = ram_alloc(TEST_BLOCK_COUNT);
  if (!r) return 1;
  struct block_device *crypt_dev = NULL;
  fails += expect_int(volume_provider_install(&r->dev, TEST_PASSWORD,
                                              &crypt_dev),
                      0, "install before plan noop");
  struct volume_provider_rekey_plan plan;
  fails += expect_int(volume_provider_rekey_plan(&r->dev, TEST_PASSWORD, NULL,
                                                 0, 0u, &plan),
                      0, "plan header-managed rc");
  fails += expect_int(
      (int)plan.status,
      (int)VOLUME_PROVIDER_REKEY_PLAN_STATUS_ALREADY_HEADER_MANAGED,
      "plan header-managed status");
  fails += expect_true(
      (plan.blocker_flags &
       VOLUME_PROVIDER_REKEY_BLOCK_ALREADY_HEADER_MANAGED) != 0,
      "plan header-managed blocker");
  fails += expect_int((int)plan.blocks_to_reencrypt, 0,
                      "plan header-managed blocks");
  fails += expect_int((int)plan.copy_direction,
                      (int)VOLUME_PROVIDER_REKEY_COPY_DIRECTION_NONE,
                      "plan header-managed copy direction");
  ram_free(r);
  return fails;
}

static int test_rekey_plan_ready_with_transaction_scratch(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  const uint32_t legacy_iter = 1000u;
  const uint32_t capyfs_blocks = TEST_BLOCK_COUNT - 2u;
  struct ram_dev *r = ram_alloc(TEST_BLOCK_COUNT);
  if (!r) return 1;
  fails += expect_int(write_legacy_capyfs_super(r, legacy_salt,
                                                sizeof(legacy_salt),
                                                legacy_iter, capyfs_blocks),
                      0, "write legacy for ready plan");
  struct volume_provider_rekey_plan plan;
  fails += expect_int(volume_provider_rekey_plan(&r->dev, TEST_PASSWORD,
                                                 legacy_salt,
                                                 sizeof(legacy_salt),
                                                 legacy_iter, &plan),
                      0, "ready plan rc");
  fails += expect_int((int)plan.status,
                      (int)VOLUME_PROVIDER_REKEY_PLAN_STATUS_READY,
                      "ready plan status");
  fails += expect_int((int)plan.source_first_lba, 0, "source first");
  fails += expect_int((int)plan.source_last_lba, (int)(capyfs_blocks - 1u),
                      "source last");
  fails += expect_int((int)plan.target_first_lba, 1, "target first");
  fails += expect_int((int)plan.target_last_lba, (int)capyfs_blocks,
                      "target last");
  fails += expect_int((int)plan.scratch_first_lba,
                      (int)(capyfs_blocks + 1u), "scratch first");
  fails += expect_int((int)plan.scratch_available_blocks, 1,
                      "scratch available");
  fails += expect_int((int)plan.copy_direction,
                      (int)VOLUME_PROVIDER_REKEY_COPY_DIRECTION_REVERSE,
                      "copy direction reverse");
  fails += expect_int((int)plan.blocks_to_reencrypt, (int)capyfs_blocks,
                      "blocks to reencrypt");
  fails += expect_int((int)plan.blocker_flags, 0, "ready plan blockers");
  fails += expect_int((int)plan.estimated_read_ops, (int)capyfs_blocks,
                      "ready plan estimated reads");
  fails += expect_int((int)plan.estimated_write_ops,
                      (int)((capyfs_blocks * 2u) + 1u),
                      "ready plan estimated writes");
  ram_free(r);
  return fails;
}

static int test_rekey_plan_blocks_without_scratch(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  const uint32_t legacy_iter = 1000u;
  struct ram_dev *r = ram_alloc(TEST_BLOCK_COUNT);
  if (!r) return 1;
  fails += expect_int(write_legacy_capyfs_super(r, legacy_salt,
                                                sizeof(legacy_salt),
                                                legacy_iter,
                                                TEST_BLOCK_COUNT - 1u),
                      0, "write legacy no scratch");
  struct volume_provider_rekey_plan plan;
  fails += expect_int(volume_provider_rekey_plan(&r->dev, TEST_PASSWORD,
                                                 legacy_salt,
                                                 sizeof(legacy_salt),
                                                 legacy_iter, &plan),
                      0, "no scratch plan rc");
  fails += expect_int(
      (int)plan.status,
      (int)VOLUME_PROVIDER_REKEY_PLAN_STATUS_BLOCKED_SCRATCH_REQUIRED,
      "no scratch plan status");
  fails += expect_true(
      (plan.blocker_flags &
       VOLUME_PROVIDER_REKEY_BLOCK_TRANSACTION_SCRATCH_REQUIRED) != 0,
      "no scratch blocker");
  ram_free(r);
  return fails;
}

static int test_rekey_plan_blocks_full_device_shrink(void) {
  int fails = 0;
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  const uint32_t legacy_iter = 1000u;
  struct ram_dev *r = ram_alloc(TEST_BLOCK_COUNT);
  if (!r) return 1;
  fails += expect_int(write_legacy_capyfs_super(r, legacy_salt,
                                                sizeof(legacy_salt),
                                                legacy_iter,
                                                TEST_BLOCK_COUNT),
                      0, "write legacy shrink plan");
  struct volume_provider_rekey_plan plan;
  fails += expect_int(volume_provider_rekey_plan(&r->dev, TEST_PASSWORD,
                                                 legacy_salt,
                                                 sizeof(legacy_salt),
                                                 legacy_iter, &plan),
                      0, "shrink plan rc");
  fails += expect_int(
      (int)plan.status,
      (int)VOLUME_PROVIDER_REKEY_PLAN_STATUS_BLOCKED_SHRINK_REQUIRED,
      "shrink plan status");
  fails += expect_true(
      (plan.blocker_flags & VOLUME_PROVIDER_REKEY_BLOCK_CAPYFS_SHRINK_REQUIRED) !=
          0,
      "shrink blocker");
  ram_free(r);
  return fails;
}

int run_volume_provider_rekey_tests(void) {
  int fails = 0;
  fails += test_rekey_preflight_header_managed_blocks_rekey();
  fails += test_rekey_preflight_legacy_requires_relocation_and_shrink();
  fails += test_rekey_preflight_legacy_partial_volume_no_shrink();
  fails += test_rekey_preflight_fail_closed();
  fails += test_rekey_plan_header_managed_noop();
  fails += test_rekey_plan_ready_with_transaction_scratch();
  fails += test_rekey_plan_blocks_without_scratch();
  fails += test_rekey_plan_blocks_full_device_shrink();
  if (fails == 0) {
    printf("[tests] volume_provider_rekey OK\n");
  } else {
    printf("[tests] volume_provider_rekey FAILED %d\n", fails);
  }
  return fails;
}
