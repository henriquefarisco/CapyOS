/* Host tests for the CAPYFS superblock geometry validator (regressive
 * hardening 2026-05-29). Exercises src/fs/fsck/fsck_geometry.c — no
 * I/O, runnable under the standard host runner.
 *
 * Locks the fail-closed contract that protects fsck_check/fsck_repair
 * from a corrupted or hostile superblock: any geometry that would make
 * fsck's uint32 size arithmetic wrap (and thus under-allocate the
 * inode/block bitmaps it then writes into) must be rejected before any
 * allocation happens. */

#include "fs/fsck_geometry.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

static void fail(const char *msg) {
  printf("[fsck-geometry] FAIL: %s\n", msg);
  g_failures++;
}

/* A small, internally consistent superblock for a 1024-block device. */
static void make_valid(struct capy_super *sb) {
  memset(sb, 0, sizeof(*sb));
  sb->magic = CAPYFS_MAGIC;
  sb->version = CAPYFS_VERSION;
  sb->block_size = 4096u;
  sb->block_count = 1024u;
  sb->inode_count = 256u;
  sb->imap_start = 1u;
  sb->bmap_start = 2u;
  sb->inode_start = 3u;
  sb->data_start = 16u;
}

#define DEV_BC 1024u
#define DEV_BS 4096u

static void test_valid_baseline(void) {
  struct capy_super sb;
  make_valid(&sb);
  if (!fsck_super_geometry_valid(&sb, DEV_BC, DEV_BS))
    fail("baseline valid superblock must pass");
}

static void test_null_rejected(void) {
  if (fsck_super_geometry_valid(NULL, DEV_BC, DEV_BS))
    fail("NULL superblock must be rejected");
}

static void test_zero_device_rejected(void) {
  struct capy_super sb;
  make_valid(&sb);
  if (fsck_super_geometry_valid(&sb, 0u, DEV_BS))
    fail("zero dev_block_count must be rejected");
}

static void test_tiny_block_size_rejected(void) {
  struct capy_super sb;
  make_valid(&sb);
  sb.block_size = 4u;
  if (fsck_super_geometry_valid(&sb, DEV_BC, 4u))
    fail("block_size < 8 must be rejected");
}

static void test_block_size_mismatch_rejected(void) {
  struct capy_super sb;
  make_valid(&sb);
  sb.block_size = 2048u; /* != device 4096 */
  if (fsck_super_geometry_valid(&sb, DEV_BC, DEV_BS))
    fail("superblock block_size != device block_size must be rejected");
}

static void test_zero_counts_rejected(void) {
  struct capy_super sb;
  make_valid(&sb);
  sb.inode_count = 0u;
  if (fsck_super_geometry_valid(&sb, DEV_BC, DEV_BS))
    fail("inode_count==0 must be rejected");
  make_valid(&sb);
  sb.block_count = 0u;
  if (fsck_super_geometry_valid(&sb, DEV_BC, DEV_BS))
    fail("block_count==0 must be rejected");
}

static void test_block_count_exceeds_device_rejected(void) {
  struct capy_super sb;
  make_valid(&sb);
  sb.block_count = DEV_BC + 1u;
  if (fsck_super_geometry_valid(&sb, DEV_BC, DEV_BS))
    fail("block_count > device capacity must be rejected");
}

static void test_inode_count_exceeds_capacity_rejected(void) {
  struct capy_super sb;
  make_valid(&sb);
  /* More inodes than block_count * inodes_per_block could ever hold. */
  uint64_t ipb = DEV_BS / sizeof(struct capy_inode_disk);
  sb.block_count = 1024u;
  sb.inode_count = (uint32_t)(1024u * ipb + 1u);
  if (fsck_super_geometry_valid(&sb, DEV_BC, DEV_BS))
    fail("inode_count beyond device capacity must be rejected");
}

static void test_offsets_outside_device_rejected(void) {
  struct capy_super sb;
  make_valid(&sb);
  sb.imap_start = DEV_BC; /* == count -> out of range */
  if (fsck_super_geometry_valid(&sb, DEV_BC, DEV_BS))
    fail("imap_start outside device must be rejected");
  make_valid(&sb);
  sb.bmap_start = DEV_BC + 5u;
  if (fsck_super_geometry_valid(&sb, DEV_BC, DEV_BS))
    fail("bmap_start outside device must be rejected");
  make_valid(&sb);
  sb.inode_start = DEV_BC;
  if (fsck_super_geometry_valid(&sb, DEV_BC, DEV_BS))
    fail("inode_start outside device must be rejected");
  make_valid(&sb);
  sb.data_start = sb.block_count + 1u;
  if (fsck_super_geometry_valid(&sb, DEV_BC, DEV_BS))
    fail("data_start beyond block_count must be rejected");
}

static void test_bitmap_region_overruns_device_rejected(void) {
  struct capy_super sb;
  make_valid(&sb);
  /* Place imap so close to the end that imap_blocks pushes past it. */
  sb.inode_count = 1024u * (DEV_BS / sizeof(struct capy_inode_disk));
  sb.imap_start = DEV_BC - 1u; /* imap_blocks >= 1 -> start+blocks > devbc */
  if (fsck_super_geometry_valid(&sb, DEV_BC, DEV_BS))
    fail("imap region overrunning the device must be rejected");
}

static void test_count_overflow_rejected(void) {
  struct capy_super sb;
  make_valid(&sb);
  /* The historical bug: inode_count near UINT32_MAX wraps (inode_count+7)
   * to a tiny value in uint32. Pair it with a huge (claimed) device so
   * the capacity check does not short-circuit first. */
  sb.block_count = 0xFFFFFFFFu;
  sb.inode_count = 0xFFFFFFFFu;
  if (fsck_super_geometry_valid(&sb, 0xFFFFFFFFu, DEV_BS))
    fail("inode_count near UINT32_MAX must be rejected (overflow)");
  make_valid(&sb);
  sb.block_count = 0xFFFFFFFFu;
  if (fsck_super_geometry_valid(&sb, 0xFFFFFFFFu, DEV_BS))
    fail("block_count near UINT32_MAX must be rejected (overflow)");
}

int run_fsck_geometry_tests(void) {
  g_failures = 0;
  test_valid_baseline();
  test_null_rejected();
  test_zero_device_rejected();
  test_tiny_block_size_rejected();
  test_block_size_mismatch_rejected();
  test_zero_counts_rejected();
  test_block_count_exceeds_device_rejected();
  test_inode_count_exceeds_capacity_rejected();
  test_offsets_outside_device_rejected();
  test_bitmap_region_overruns_device_rejected();
  test_count_overflow_rejected();
  if (g_failures == 0) printf("[tests] fsck_geometry OK\n");
  return g_failures;
}
