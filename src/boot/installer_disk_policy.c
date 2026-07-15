#include "boot/installer_disk_policy.h"

#include <stdint.h>

#define INSTALLER_MIB_BYTES (1024ull * 1024ull)
#define INSTALLER_GPT_FIRST_USABLE_LBA 34ull

uint64_t installer_disk_path_hash_init(void) {
  return 14695981039346656037ULL;
}

uint64_t installer_disk_path_hash_update(uint64_t state, const uint8_t *data,
                                         size_t length) {
  if (!data && length != 0u) {
    return 0u;
  }
  for (size_t i = 0u; i < length; ++i) {
    state ^= data[i];
    state *= 1099511628211ULL;
  }
  return state;
}

static int align_up_checked(uint64_t value, uint64_t alignment,
                            uint64_t *out) {
  uint64_t remainder;
  uint64_t increment;
  if (!out || alignment == 0u) {
    return -1;
  }
  remainder = value % alignment;
  if (remainder == 0u) {
    *out = value;
    return 0;
  }
  increment = alignment - remainder;
  if (value > UINT64_MAX - increment) {
    return -1;
  }
  *out = value + increment;
  return 0;
}

static int add_span(uint64_t start, uint64_t count, uint64_t *out_end) {
  if (!out_end || count == 0u || start > UINT64_MAX - (count - 1u)) {
    return -1;
  }
  *out_end = start + count - 1u;
  return 0;
}

static int installer_disk_base_layout(struct installer_disk_layout *layout) {
  uint64_t esp_end;
  uint64_t boot_end;
  uint64_t min_data_sectors;
  uint64_t required_sectors;
  if (!layout) {
    return -1;
  }
  *layout = (struct installer_disk_layout){0};
  layout->first_usable_lba = INSTALLER_GPT_FIRST_USABLE_LBA;
  layout->esp_sectors =
      (INSTALLER_DISK_ESP_SIZE_MIB * INSTALLER_MIB_BYTES) /
      INSTALLER_DISK_BLOCK_SIZE;
  layout->boot_sectors =
      (INSTALLER_DISK_BOOT_SIZE_MIB * INSTALLER_MIB_BYTES) /
      INSTALLER_DISK_BLOCK_SIZE;
  min_data_sectors =
      (INSTALLER_DISK_MIN_DATA_SIZE_MIB * INSTALLER_MIB_BYTES) /
      INSTALLER_DISK_BLOCK_SIZE;
  if (align_up_checked(INSTALLER_DISK_ALIGN_LBA, INSTALLER_DISK_ALIGN_LBA,
                       &layout->esp_lba) != 0 ||
      add_span(layout->esp_lba, layout->esp_sectors, &esp_end) != 0 ||
      esp_end == UINT64_MAX ||
      align_up_checked(esp_end + 1u, INSTALLER_DISK_ALIGN_LBA,
                       &layout->boot_lba) != 0 ||
      add_span(layout->boot_lba, layout->boot_sectors, &boot_end) != 0 ||
      boot_end == UINT64_MAX ||
      align_up_checked(boot_end + 1u, INSTALLER_DISK_ALIGN_LBA,
                       &layout->data_lba) != 0 ||
      layout->data_lba < layout->first_usable_lba ||
      layout->data_lba > UINT64_MAX - min_data_sectors -
                             (INSTALLER_DISK_GPT_ENTRIES_SECTORS + 1u)) {
    *layout = (struct installer_disk_layout){0};
    return -1;
  }
  required_sectors = layout->data_lba + min_data_sectors +
                     INSTALLER_DISK_GPT_ENTRIES_SECTORS + 1u;
  if (required_sectors > UINT64_MAX / INSTALLER_DISK_BLOCK_SIZE) {
    *layout = (struct installer_disk_layout){0};
    return -1;
  }
  layout->required_bytes = required_sectors * INSTALLER_DISK_BLOCK_SIZE;
  return 0;
}

uint64_t installer_disk_minimum_bytes(void) {
  struct installer_disk_layout layout;
  if (installer_disk_base_layout(&layout) != 0) {
    return 0u;
  }
  return layout.required_bytes;
}

int installer_disk_plan(const struct installer_disk_geometry *geometry,
                        struct installer_disk_layout *out_layout) {
  struct installer_disk_layout layout;
  uint64_t boot_end;
  uint64_t minimum_sectors;
  uint64_t minimum_data_sectors;

  if (out_layout) {
    *out_layout = (struct installer_disk_layout){0};
  }
  if (!geometry || !out_layout || geometry->block_count == 0u ||
      geometry->block_size == 0u) {
    return INSTALLER_DISK_PREFLIGHT_INVALID;
  }
  if (!geometry->media_present) {
    return INSTALLER_DISK_PREFLIGHT_NO_MEDIA;
  }
  if (geometry->logical_partition) {
    return INSTALLER_DISK_PREFLIGHT_LOGICAL;
  }
  if (geometry->read_only) {
    return INSTALLER_DISK_PREFLIGHT_READ_ONLY;
  }
  if (geometry->removable) {
    return INSTALLER_DISK_PREFLIGHT_REMOVABLE;
  }
  if (geometry->block_size != INSTALLER_DISK_BLOCK_SIZE) {
    return INSTALLER_DISK_PREFLIGHT_BLOCK_SIZE;
  }
  if (geometry->block_count > UINT64_MAX / geometry->block_size) {
    return INSTALLER_DISK_PREFLIGHT_OVERFLOW;
  }
  if (installer_disk_base_layout(&layout) != 0) {
    return INSTALLER_DISK_PREFLIGHT_OVERFLOW;
  }
  minimum_sectors = layout.required_bytes / INSTALLER_DISK_BLOCK_SIZE;
  if (geometry->block_count < minimum_sectors) {
    return INSTALLER_DISK_PREFLIGHT_TOO_SMALL;
  }
  layout.total_sectors = geometry->block_count;
  layout.backup_entries_lba =
      geometry->block_count - 1u - INSTALLER_DISK_GPT_ENTRIES_SECTORS;
  layout.last_usable_lba = layout.backup_entries_lba - 1u;
  if (add_span(layout.boot_lba, layout.boot_sectors, &boot_end) != 0 ||
      boot_end >= layout.last_usable_lba ||
      layout.data_lba > layout.last_usable_lba) {
    return INSTALLER_DISK_PREFLIGHT_TOO_SMALL;
  }
  layout.data_sectors = layout.last_usable_lba - layout.data_lba + 1u;
  minimum_data_sectors =
      (INSTALLER_DISK_MIN_DATA_SIZE_MIB * INSTALLER_MIB_BYTES) /
      INSTALLER_DISK_BLOCK_SIZE;
  if (layout.data_sectors < minimum_data_sectors) {
    return INSTALLER_DISK_PREFLIGHT_TOO_SMALL;
  }
  *out_layout = layout;
  return INSTALLER_DISK_PREFLIGHT_OK;
}

int installer_disk_parse_selection(const char *text, size_t candidate_count,
                                   size_t *out_index) {
  size_t value = 0u;
  size_t i = 0u;
  if (!text || !out_index || candidate_count == 0u || text[0] < '1' ||
      text[0] > '9') {
    return -1;
  }
  while (text[i]) {
    size_t digit;
    if (text[i] < '0' || text[i] > '9') {
      return -1;
    }
    digit = (size_t)(text[i] - '0');
    if (value > (SIZE_MAX - digit) / 10u) {
      return -1;
    }
    value = value * 10u + digit;
    ++i;
  }
  if (value == 0u || value > candidate_count) {
    return -1;
  }
  *out_index = value - 1u;
  return 0;
}

int installer_disk_confirmation_valid(const char *text) {
  static const char expected[] = "ERASE";
  size_t i = 0u;
  if (!text) {
    return 0;
  }
  while (expected[i]) {
    if (text[i] != expected[i]) {
      return 0;
    }
    ++i;
  }
  return text[i] == '\0' ? 1 : 0;
}
