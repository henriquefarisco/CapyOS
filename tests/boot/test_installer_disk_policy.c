#include "boot/installer_disk_policy.h"

#include <stdint.h>
#include <string.h>

#include "boot/boot_manifest.h"
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
  fails += expect_int(installer_disk_io_alignment_valid(0u), 1,
                      "zero I/O alignment accepted");
  fails += expect_int(installer_disk_io_alignment_valid(1u), 1,
                      "unit I/O alignment accepted");
  fails += expect_int(installer_disk_io_alignment_valid(4096u), 1,
                      "power-of-two I/O alignment accepted");
  fails += expect_int(installer_disk_io_alignment_valid(3u), 0,
                      "non-power-of-two I/O alignment rejected");
  fails += expect_int(installer_disk_runtime_fallback_allowed(0, 1), 0,
                      "CD-ROM runtime fallback rejected");
  fails += expect_int(installer_disk_runtime_fallback_allowed(1, 1), 0,
                      "partial disk hint fallback rejected");
  fails += expect_int(installer_disk_runtime_fallback_allowed(0, 0), 0,
                      "unbound non-CD fallback rejected");
  fails += expect_int(installer_disk_runtime_binding_matches(1, 1, 1), 1,
                      "complete runtime binding accepted");
  fails += expect_int(installer_disk_runtime_binding_matches(1, 1, 0), 0,
                      "cloned runtime path rejected");
  fails += expect_int(installer_disk_runtime_binding_matches(1, 0, 1), 0,
                      "runtime GUID mismatch rejected");
  geometry = valid_geometry(2097152ull);
  fails += expect_int(installer_disk_plan(&geometry, &layout),
                      INSTALLER_DISK_PREFLIGHT_TOO_SMALL, "undersized disk");
  geometry = valid_geometry((uint64_t)UINT32_MAX + 1u);
  fails += expect_int(installer_disk_plan(&geometry, &layout),
                      INSTALLER_DISK_PREFLIGHT_OVERFLOW,
                      "block ABI overflow");
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

static int test_boot_manifest_policy(void) {
  struct boot_manifest manifest;
  int fails = 0;

  memset(&manifest, 0, sizeof(manifest));
  manifest.magic = BOOT_MANIFEST_MAGIC;
  manifest.version = BOOT_MANIFEST_VERSION;
  manifest.entry_count = 1u;
  manifest.entries[0].type = BOOT_ENTRY_NORMAL;
  manifest.entries[0].lba_start = 1u;
  manifest.entries[0].sector_count = 10u;
  manifest.entries[0].checksum32 = 0x12345678u;
  fails += expect_int(installer_disk_boot_manifest_valid(&manifest, 20u), 1,
                      "canonical BOOT manifest accepted");
  fails += expect_int(installer_disk_boot_manifest_valid(NULL, 20u), 0,
                      "null BOOT manifest rejected");
  manifest.version++;
  fails += expect_int(installer_disk_boot_manifest_valid(&manifest, 20u), 0,
                      "BOOT manifest version rejected");
  manifest.version = BOOT_MANIFEST_VERSION;
  manifest.entry_count = 5u;
  fails += expect_int(installer_disk_boot_manifest_valid(&manifest, 20u), 0,
                      "BOOT manifest entry overflow rejected");
  manifest.entry_count = 1u;
  manifest.entries[0].sector_count = 20u;
  fails += expect_int(installer_disk_boot_manifest_valid(&manifest, 20u), 0,
                      "BOOT manifest range rejected");
  manifest.entries[0].sector_count = 10u;
  manifest.entries[1].checksum32 = 1u;
  fails += expect_int(installer_disk_boot_manifest_valid(&manifest, 20u), 0,
                      "BOOT manifest trailing entry rejected");
  memset(&manifest.entries[1], 0, sizeof(manifest.entries[1]));
  manifest.entry_count = 2u;
  manifest.entries[1].type = BOOT_ENTRY_RECOVERY;
  manifest.entries[1].lba_start = 10u;
  manifest.entries[1].sector_count = 2u;
  fails += expect_int(installer_disk_boot_manifest_valid(&manifest, 20u), 0,
                      "BOOT manifest overlap rejected");
  manifest.entries[1].lba_start = 11u;
  fails += expect_int(installer_disk_boot_manifest_valid(&manifest, 20u), 1,
                      "disjoint recovery entry accepted");
  manifest.entries[1].type = BOOT_ENTRY_NORMAL;
  fails += expect_int(installer_disk_boot_manifest_valid(&manifest, 20u), 0,
                      "duplicate normal entry rejected");
  manifest.entries[0].type = BOOT_ENTRY_RECOVERY;
  manifest.entries[1].type = BOOT_ENTRY_RECOVERY;
  fails += expect_int(installer_disk_boot_manifest_valid(&manifest, 20u), 0,
                      "missing normal entry rejected");
  return fails;
}

static int test_device_path_parent_binding(void) {
  uint8_t parent[16] = {0};
  uint8_t partition[58] = {0};
  int fails = 0;

  parent[0] = 0x01u;
  parent[1] = 0x01u;
  parent[2] = 0x06u;
  parent[4] = 0x10u;
  parent[5] = 0x20u;
  parent[6] = 0x03u;
  parent[7] = 0x12u;
  parent[8] = 0x06u;
  parent[10] = 0x30u;
  parent[11] = 0x40u;
  parent[12] = 0x7Fu;
  parent[13] = 0xFFu;
  parent[14] = 0x04u;
  memcpy(partition, parent, 12u);
  partition[12] = 0x04u;
  partition[13] = 0x01u;
  partition[14] = 0x2Au;
  partition[54] = 0x7Fu;
  partition[55] = 0xFFu;
  partition[56] = 0x04u;
  fails += expect_int(
      installer_disk_device_path_parent_matches(parent, partition), 1,
      "physical Device Path parent accepted");
  partition[11] ^= 1u;
  fails += expect_int(
      installer_disk_device_path_parent_matches(parent, partition), 0,
      "cloned GUID on different physical path rejected");
  partition[11] ^= 1u;
  partition[12] = 0x7Fu;
  partition[13] = 0xFFu;
  partition[14] = 0x04u;
  partition[15] = 0u;
  fails += expect_int(
      installer_disk_device_path_parent_matches(parent, partition), 0,
      "partition path without HD node rejected");
  partition[12] = 0x04u;
  partition[13] = 0x01u;
  partition[14] = 0x2Au;
  parent[2] = 0x03u;
  fails += expect_int(
      installer_disk_device_path_parent_matches(parent, partition), 0,
      "malformed parent path rejected");
  fails += expect_int(
      installer_disk_device_path_parent_matches(NULL, partition), 0,
      "null parent path rejected");
  return fails;
}

static int test_fat32_plan_converges(void) {
  uint32_t fat_sectors = 0u;
  uint32_t cluster_count = 0u;
  int fails = 0;

  fails += expect_int(
      installer_disk_fat32_plan(525336u, 8u, 32u, 2u, &fat_sectors,
                                &cluster_count),
      0, "oscillating FAT geometry converges");
  fails += expect_u64(fat_sectors, 513u, "conservative FAT span");
  fails += expect_u64(cluster_count, 65534u, "conservative cluster count");
  fails += expect_int(
      installer_disk_fat32_plan(1048576u, 8u, 32u, 2u, &fat_sectors,
                                &cluster_count),
      0, "official ESP FAT geometry");
  fails += expect_u64(fat_sectors, 1024u, "official ESP FAT span");
  fails += expect_int(
      installer_disk_fat32_plan(10u, 8u, 32u, 2u, &fat_sectors,
                                &cluster_count),
      -1, "undersized FAT geometry rejected");
  fails += expect_int(fat_sectors == 0u && cluster_count == 0u, 1,
                      "failed FAT plan clears outputs");
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
  fails += test_boot_manifest_policy();
  fails += test_device_path_parent_binding();
  fails += test_fat32_plan_converges();
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
