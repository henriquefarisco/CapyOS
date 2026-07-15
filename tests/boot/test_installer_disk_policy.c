#include "boot/installer_disk_policy.h"

#include <stdint.h>
#include <stdio.h>

static int expect_int(int actual, int expected, const char *name) {
  if (actual != expected) {
    printf("[FAIL] installer_disk_policy: %s (got %d, expected %d)\n", name,
           actual, expected);
    return 1;
  }
  return 0;
}

static int expect_u64(uint64_t actual, uint64_t expected, const char *name) {
  if (actual != expected) {
    printf("[FAIL] installer_disk_policy: %s (got %llu, expected %llu)\n",
           name, (unsigned long long)actual, (unsigned long long)expected);
    return 1;
  }
  return 0;
}

static struct installer_disk_geometry valid_geometry(uint64_t block_count) {
  struct installer_disk_geometry geometry;
  geometry.block_count = block_count;
  geometry.block_size = INSTALLER_DISK_BLOCK_SIZE;
  geometry.media_present = 1u;
  geometry.logical_partition = 0u;
  geometry.read_only = 0u;
  geometry.removable = 0u;
  return geometry;
}

static int layout_is_zero(const struct installer_disk_layout *layout) {
  return layout && layout->required_bytes == 0u && layout->total_sectors == 0u &&
         layout->first_usable_lba == 0u && layout->last_usable_lba == 0u &&
         layout->backup_entries_lba == 0u && layout->esp_lba == 0u &&
         layout->esp_sectors == 0u && layout->boot_lba == 0u &&
         layout->boot_sectors == 0u && layout->data_lba == 0u &&
         layout->data_sectors == 0u;
}

static int test_path_hash_kat(void) {
  static const uint8_t hello[] = {'h', 'e', 'l', 'l', 'o'};
  int fails = 0;
  fails += expect_u64(installer_disk_path_hash_init(),
                      14695981039346656037ull, "FNV-1a offset basis");
  fails += expect_u64(installer_disk_path_hash_update(
                          installer_disk_path_hash_init(), hello, sizeof(hello)),
                      0xa430d84680aabd0bull, "FNV-1a hello KAT");
  fails += expect_u64(installer_disk_path_hash_update(
                          installer_disk_path_hash_init(), NULL, 0u),
                      installer_disk_path_hash_init(), "FNV-1a empty input");
  fails += expect_u64(installer_disk_path_hash_update(
                          installer_disk_path_hash_init(), NULL, 1u),
                      0u, "FNV-1a null input rejected");
  return fails;
}

static int test_plan_accepts_exact_minimum(void) {
  struct installer_disk_geometry geometry = valid_geometry(4194304ull);
  struct installer_disk_layout layout;
  int fails = 0;

  fails += expect_int(installer_disk_plan(&geometry, &layout),
                      INSTALLER_DISK_PREFLIGHT_OK, "2 GiB disk accepted");
  fails += expect_u64(installer_disk_minimum_bytes(), 1880113664ull,
                      "minimum required bytes");
  fails += expect_u64(layout.required_bytes, 1880113664ull,
                      "planned required bytes");
  fails += expect_u64(layout.total_sectors, 4194304ull, "total sectors");
  fails += expect_u64(layout.first_usable_lba, 34ull, "first usable LBA");
  fails += expect_u64(layout.last_usable_lba, 4194270ull,
                      "last usable LBA");
  fails += expect_u64(layout.backup_entries_lba, 4194271ull,
                      "backup entries LBA");
  fails += expect_u64(layout.esp_lba, 2048ull, "ESP start");
  fails += expect_u64(layout.esp_sectors, 1048576ull, "ESP sectors");
  fails += expect_u64(layout.boot_lba, 1050624ull, "BOOT start");
  fails += expect_u64(layout.boot_sectors, 524288ull, "BOOT sectors");
  fails += expect_u64(layout.data_lba, 1574912ull, "DATA start");
  fails += expect_u64(layout.data_sectors, 2619359ull, "DATA sectors");

  geometry.block_count =
      installer_disk_minimum_bytes() / INSTALLER_DISK_BLOCK_SIZE;
  fails += expect_int(installer_disk_plan(&geometry, &layout),
                      INSTALLER_DISK_PREFLIGHT_OK,
                      "exact minimum disk accepted");
  fails += expect_u64(layout.data_sectors, 2097152ull,
                      "minimum DATA sectors preserved");
  geometry.block_count--;
  fails += expect_int(installer_disk_plan(&geometry, &layout),
                      INSTALLER_DISK_PREFLIGHT_TOO_SMALL,
                      "one-block-short disk rejected");
  fails += expect_int(layout_is_zero(&layout), 1,
                      "failed plan clears layout");
  return fails;
}

static int test_plan_rejects_ineligible_media(void) {
  struct installer_disk_geometry geometry = valid_geometry(4194304ull);
  struct installer_disk_layout layout;
  int fails = 0;

  fails += expect_int(installer_disk_plan(NULL, &layout),
                      INSTALLER_DISK_PREFLIGHT_INVALID, "null geometry");
  fails += expect_int(installer_disk_plan(&geometry, NULL),
                      INSTALLER_DISK_PREFLIGHT_INVALID, "null output");
  geometry.block_count = 0u;
  fails += expect_int(installer_disk_plan(&geometry, &layout),
                      INSTALLER_DISK_PREFLIGHT_INVALID, "zero blocks");
  geometry = valid_geometry(4194304ull);
  geometry.media_present = 0u;
  fails += expect_int(installer_disk_plan(&geometry, &layout),
                      INSTALLER_DISK_PREFLIGHT_NO_MEDIA, "missing media");
  geometry = valid_geometry(4194304ull);
  geometry.logical_partition = 1u;
  fails += expect_int(installer_disk_plan(&geometry, &layout),
                      INSTALLER_DISK_PREFLIGHT_LOGICAL, "logical partition");
  geometry = valid_geometry(4194304ull);
  geometry.read_only = 1u;
  fails += expect_int(installer_disk_plan(&geometry, &layout),
                      INSTALLER_DISK_PREFLIGHT_READ_ONLY, "read-only media");
  geometry = valid_geometry(4194304ull);
  geometry.removable = 1u;
  fails += expect_int(installer_disk_plan(&geometry, &layout),
                      INSTALLER_DISK_PREFLIGHT_REMOVABLE, "removable media");
  geometry = valid_geometry(4194304ull);
  geometry.block_size = 4096u;
  fails += expect_int(installer_disk_plan(&geometry, &layout),
                      INSTALLER_DISK_PREFLIGHT_BLOCK_SIZE,
                      "unsupported block size");
  geometry = valid_geometry(2097152ull);
  fails += expect_int(installer_disk_plan(&geometry, &layout),
                      INSTALLER_DISK_PREFLIGHT_TOO_SMALL, "undersized disk");
  geometry = valid_geometry(UINT64_MAX);
  fails += expect_int(installer_disk_plan(&geometry, &layout),
                      INSTALLER_DISK_PREFLIGHT_OVERFLOW, "byte overflow");
  fails += expect_int(layout_is_zero(&layout), 1,
                      "overflow clears layout");
  return fails;
}

static int test_selection_parser(void) {
  size_t index = 99u;
  int fails = 0;

  fails += expect_int(installer_disk_parse_selection("1", 16u, &index), 0,
                      "first selection accepted");
  fails += expect_u64(index, 0u, "first selection index");
  fails += expect_int(installer_disk_parse_selection("10", 16u, &index), 0,
                      "two-digit selection accepted");
  fails += expect_u64(index, 9u, "two-digit selection index");
  fails += expect_int(installer_disk_parse_selection("16", 16u, &index), 0,
                      "last selection accepted");
  fails += expect_u64(index, 15u, "last selection index");
  fails += expect_int(installer_disk_parse_selection("0", 3u, &index), -1,
                      "zero rejected");
  fails += expect_int(installer_disk_parse_selection("4", 3u, &index), -1,
                      "out-of-range rejected");
  fails += expect_int(installer_disk_parse_selection("01", 3u, &index), -1,
                      "leading zero rejected");
  fails += expect_int(installer_disk_parse_selection("+1", 3u, &index), -1,
                      "sign rejected");
  fails += expect_int(installer_disk_parse_selection(" 1", 3u, &index), -1,
                      "leading space rejected");
  fails += expect_int(installer_disk_parse_selection("1 ", 3u, &index), -1,
                      "trailing space rejected");
  fails += expect_int(installer_disk_parse_selection("1x", 3u, &index), -1,
                      "trailing garbage rejected");
  fails += expect_int(installer_disk_parse_selection("", 3u, &index), -1,
                      "empty selection rejected");
  fails += expect_int(installer_disk_parse_selection("1", 0u, &index), -1,
                      "empty candidate set rejected");
  fails += expect_int(installer_disk_parse_selection(
                          "999999999999999999999999999999999999", 16u, &index),
                      -1, "selection overflow rejected");
  return fails;
}

static int test_confirmation_token(void) {
  int fails = 0;
  fails += expect_int(installer_disk_confirmation_valid("ERASE"), 1,
                      "exact confirmation accepted");
  fails += expect_int(installer_disk_confirmation_valid("erase"), 0,
                      "lowercase confirmation rejected");
  fails += expect_int(installer_disk_confirmation_valid("ERASE "), 0,
                      "trailing space rejected");
  fails += expect_int(installer_disk_confirmation_valid(""), 0,
                      "empty confirmation rejected");
  fails += expect_int(installer_disk_confirmation_valid(NULL), 0,
                      "null confirmation rejected");
  return fails;
}

int run_installer_disk_policy_tests(void) {
  int fails = 0;
  fails += test_path_hash_kat();
  fails += test_plan_accepts_exact_minimum();
  fails += test_plan_rejects_ineligible_media();
  fails += test_selection_parser();
  fails += test_confirmation_token();
  if (fails == 0) {
    printf("[OK] installer_disk_policy\n");
  }
  return fails;
}

#if defined(INSTALLER_DISK_STANDALONE)
int main(void) {
  return run_installer_disk_policy_tests() == 0 ? 0 : 1;
}
#endif
