#include "boot/gpt_identity.h"

#include <stddef.h>

#include "boot/gpt_types.h"

#define GPT_IDENTITY_SIGNATURE 0x5452415020494645ULL
#define GPT_IDENTITY_REVISION 0x00010000u
#define GPT_IDENTITY_HEADER_SIZE 92u
#define GPT_IDENTITY_ENTRY_SIZE 128u
#define GPT_IDENTITY_ENTRY_COUNT 128u

struct identity_partition_state {
  struct capyos_gpt_partition_identity value;
  int found;
};

static void identity_zero(void *ptr, size_t len) {
  uint8_t *bytes = ptr;
  if (!bytes)
    return;
  for (size_t i = 0u; i < len; ++i)
    bytes[i] = 0u;
}

static uint32_t identity_u32(const uint8_t *src) {
  return (uint32_t)src[0] | ((uint32_t)src[1] << 8) |
         ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

static uint64_t identity_u64(const uint8_t *src) {
  return (uint64_t)identity_u32(src) |
         ((uint64_t)identity_u32(src + 4u) << 32);
}

static uint32_t identity_crc_update(uint32_t crc, const uint8_t *data,
                                    size_t len) {
  for (size_t i = 0u; i < len; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^
            (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
  }
  return crc;
}

static uint32_t identity_crc(const uint8_t *data, size_t len) {
  return ~identity_crc_update(0xFFFFFFFFu, data, len);
}

static int identity_equal(const uint8_t *a, const uint8_t *b, size_t len) {
  uint8_t diff = 0u;
  if (!a || !b)
    return 0;
  for (size_t i = 0u; i < len; ++i)
    diff |= (uint8_t)(a[i] ^ b[i]);
  return diff == 0u;
}

static int identity_present(const uint8_t *bytes, size_t len) {
  uint8_t any = 0u;
  if (!bytes)
    return 0;
  for (size_t i = 0u; i < len; ++i)
    any |= bytes[i];
  return any != 0u;
}

static int identity_name(const uint8_t *name, const char *ascii) {
  size_t i = 0u;
  if (!name || !ascii)
    return 0;
  while (i < 36u && ascii[i]) {
    if (name[i * 2u] != (uint8_t)ascii[i] || name[i * 2u + 1u] != 0u)
      return 0;
    ++i;
  }
  if (i == 36u || ascii[i])
    return 0;
  for (; i < 36u; ++i) {
    if (name[i * 2u] != 0u || name[i * 2u + 1u] != 0u)
      return 0;
  }
  return 1;
}

static int identity_set(struct identity_partition_state *state,
                        const uint8_t *entry, uint64_t first, uint64_t last,
                        uint64_t first_usable, uint64_t last_usable) {
  uint64_t count = 0u;
  if (!state || !entry || state->found || first < first_usable ||
      last > last_usable || last < first || first > UINT32_MAX ||
      identity_u64(entry + 48u) != 0u ||
      !identity_present(entry + 16u, 16u))
    return CAPYOS_GPT_IDENTITY_ERR;
  count = last - first + 1u;
  if (count == 0u || count > UINT32_MAX)
    return CAPYOS_GPT_IDENTITY_ERR;
  state->value.lba = (uint32_t)first;
  state->value.sectors = (uint32_t)count;
  for (size_t i = 0u; i < 16u; ++i)
    state->value.guid[i] = entry[16u + i];
  state->found = 1;
  return 0;
}

static int identity_entry(const uint8_t *entry, uint64_t first_usable,
                          uint64_t last_usable,
                          struct identity_partition_state *esp,
                          struct identity_partition_state *boot,
                          struct identity_partition_state *data) {
  static const uint8_t esp_type[16] = CAPYOS_GPT_TYPE_ESP_BYTES;
  static const uint8_t boot_type[16] = CAPYOS_GPT_TYPE_BOOT_BYTES;
  static const uint8_t data_type[16] = CAPYOS_GPT_TYPE_DATA_BYTES;
  struct identity_partition_state *target = NULL;
  const char *name = NULL;
  if (!entry || !esp || !boot || !data)
    return CAPYOS_GPT_IDENTITY_ERR;
  if (!identity_present(entry, 16u))
    return 0;
  if (identity_equal(entry, esp_type, 16u)) {
    target = esp;
    name = "ESP";
  } else if (identity_equal(entry, boot_type, 16u)) {
    target = boot;
    name = "BOOT";
  } else if (identity_equal(entry, data_type, 16u)) {
    target = data;
    name = "DATA";
  } else {
    return CAPYOS_GPT_IDENTITY_ERR;
  }
  if (!identity_name(entry + 56u, name))
    return CAPYOS_GPT_IDENTITY_ERR;
  return identity_set(target, entry, identity_u64(entry + 32u),
                      identity_u64(entry + 40u), first_usable, last_usable);
}

static int identity_overlap(const struct capyos_gpt_partition_identity *a,
                            const struct capyos_gpt_partition_identity *b) {
  uint64_t a_end = 0u;
  uint64_t b_end = 0u;
  if (!a || !b || a->sectors == 0u || b->sectors == 0u)
    return 1;
  a_end = (uint64_t)a->lba + a->sectors - 1u;
  b_end = (uint64_t)b->lba + b->sectors - 1u;
  return !((a_end < b->lba) || (b_end < a->lba));
}

static int identity_backup(capyos_gpt_read_fn reader, void *ctx,
                           uint32_t block_count, uint64_t backup_lba,
                           uint64_t first_usable, uint64_t last_usable,
                           uint32_t entries_crc, const uint8_t disk_guid[16]) {
  uint8_t header[512];
  uint8_t primary_sector[512];
  uint8_t sector[512];
  uint32_t crc = 0xFFFFFFFFu;
  uint32_t stored_header_crc = 0u;
  uint32_t entries_read = 0u;
  uint32_t entry_sectors = 32u;
  uint32_t backup_entries_lba = (uint32_t)backup_lba - entry_sectors;
  if (!reader || !disk_guid || backup_lba != (uint64_t)block_count - 1u ||
      backup_lba > UINT32_MAX || reader(ctx, (uint32_t)backup_lba, header) != 0 ||
      identity_u64(header) != GPT_IDENTITY_SIGNATURE ||
      identity_u32(header + 8u) != GPT_IDENTITY_REVISION ||
      identity_u32(header + 12u) != GPT_IDENTITY_HEADER_SIZE ||
      identity_u32(header + 20u) != 0u ||
      identity_u64(header + 24u) != backup_lba ||
      identity_u64(header + 32u) != 1u ||
      identity_u64(header + 40u) != first_usable ||
      identity_u64(header + 48u) != last_usable ||
      !identity_equal(header + 56u, disk_guid, 16u) ||
      identity_u64(header + 72u) != backup_entries_lba ||
      identity_u32(header + 80u) != GPT_IDENTITY_ENTRY_COUNT ||
      identity_u32(header + 84u) != GPT_IDENTITY_ENTRY_SIZE ||
      identity_u32(header + 88u) != entries_crc)
    return CAPYOS_GPT_IDENTITY_ERR;
  stored_header_crc = identity_u32(header + 16u);
  header[16u] = header[17u] = header[18u] = header[19u] = 0u;
  if (identity_crc(header, GPT_IDENTITY_HEADER_SIZE) != stored_header_crc)
    return CAPYOS_GPT_IDENTITY_ERR;
  while (entries_read < GPT_IDENTITY_ENTRY_COUNT) {
    if (reader(ctx, backup_entries_lba + entries_read / 4u, sector) != 0 ||
        reader(ctx, 2u + entries_read / 4u, primary_sector) != 0 ||
        !identity_equal(sector, primary_sector, sizeof(sector)))
      return CAPYOS_GPT_IDENTITY_ERR;
    crc = identity_crc_update(crc, sector, 512u);
    entries_read += 4u;
  }
  return ~crc == entries_crc ? 0 : CAPYOS_GPT_IDENTITY_ERR;
}

static int identity_read_mode(capyos_gpt_read_fn reader, void *ctx,
                              uint32_t block_size, uint32_t block_count,
                              int allow_duplicate_guids,
                              struct capyos_gpt_identity *out) {
  uint8_t header[512];
  uint8_t sector[512];
  struct identity_partition_state esp;
  struct identity_partition_state boot;
  struct identity_partition_state data;
  uint64_t backup_lba = 0u;
  uint64_t first_usable = 0u;
  uint64_t last_usable = 0u;
  uint32_t entries_crc = 0xFFFFFFFFu;
  uint32_t stored_header_crc = 0u;
  uint32_t stored_entries_crc = 0u;
  uint32_t entries_read = 0u;
  int guid_collision = 0;
  if (out)
    identity_zero(out, sizeof(*out));
  if (!reader || !out || block_size != 512u || block_count < 68u ||
      reader(ctx, 1u, header) != 0 ||
      identity_u64(header) != GPT_IDENTITY_SIGNATURE ||
      identity_u32(header + 8u) != GPT_IDENTITY_REVISION ||
      identity_u32(header + 12u) != GPT_IDENTITY_HEADER_SIZE ||
      identity_u32(header + 20u) != 0u || identity_u64(header + 24u) != 1u ||
      identity_u64(header + 72u) != 2u ||
      identity_u32(header + 80u) != GPT_IDENTITY_ENTRY_COUNT ||
      identity_u32(header + 84u) != GPT_IDENTITY_ENTRY_SIZE ||
      !identity_present(header + 56u, 16u))
    return CAPYOS_GPT_IDENTITY_ERR;
  backup_lba = identity_u64(header + 32u);
  first_usable = identity_u64(header + 40u);
  last_usable = identity_u64(header + 48u);
  stored_header_crc = identity_u32(header + 16u);
  stored_entries_crc = identity_u32(header + 88u);
  header[16u] = header[17u] = header[18u] = header[19u] = 0u;
  if (backup_lba != (uint64_t)block_count - 1u || first_usable != 34u ||
      last_usable != backup_lba - 33u ||
      identity_crc(header, GPT_IDENTITY_HEADER_SIZE) != stored_header_crc)
    return CAPYOS_GPT_IDENTITY_ERR;
  identity_zero(&esp, sizeof(esp));
  identity_zero(&boot, sizeof(boot));
  identity_zero(&data, sizeof(data));
  while (entries_read < GPT_IDENTITY_ENTRY_COUNT) {
    if (reader(ctx, 2u + entries_read / 4u, sector) != 0)
      return CAPYOS_GPT_IDENTITY_ERR;
    entries_crc = identity_crc_update(entries_crc, sector, 512u);
    for (uint32_t i = 0u; i < 4u; ++i) {
      if (identity_entry(sector + i * GPT_IDENTITY_ENTRY_SIZE,
                         first_usable, last_usable, &esp, &boot, &data) != 0)
        return CAPYOS_GPT_IDENTITY_ERR;
    }
    entries_read += 4u;
  }
  entries_crc = ~entries_crc;
  guid_collision = identity_equal(esp.value.guid, boot.value.guid, 16u) ||
                   identity_equal(esp.value.guid, data.value.guid, 16u) ||
                   identity_equal(boot.value.guid, data.value.guid, 16u) ||
                   identity_equal(header + 56u, esp.value.guid, 16u) ||
                   identity_equal(header + 56u, boot.value.guid, 16u) ||
                   identity_equal(header + 56u, data.value.guid, 16u);
  if (entries_crc != stored_entries_crc || !esp.found || !boot.found ||
      !data.found || (allow_duplicate_guids && !guid_collision) ||
      (!allow_duplicate_guids &&
       (identity_equal(esp.value.guid, boot.value.guid, 16u) ||
        identity_equal(esp.value.guid, data.value.guid, 16u) ||
        identity_equal(boot.value.guid, data.value.guid, 16u) ||
        identity_equal(header + 56u, esp.value.guid, 16u) ||
        identity_equal(header + 56u, boot.value.guid, 16u) ||
        identity_equal(header + 56u, data.value.guid, 16u))) ||
      identity_overlap(&esp.value, &boot.value) ||
      identity_overlap(&esp.value, &data.value) ||
      identity_overlap(&boot.value, &data.value) ||
      identity_backup(reader, ctx, block_count, backup_lba, first_usable,
                      last_usable, stored_entries_crc, header + 56u) != 0)
    return CAPYOS_GPT_IDENTITY_ERR;
  out->disk_sectors = block_count;
  for (size_t i = 0u; i < 16u; ++i)
    out->disk_guid[i] = header[56u + i];
  out->esp = esp.value;
  out->boot = boot.value;
  out->data = data.value;
  return 0;
}

int capyos_gpt_identity_read(capyos_gpt_read_fn reader, void *ctx,
                             uint32_t block_size, uint32_t block_count,
                             struct capyos_gpt_identity *out) {
  return identity_read_mode(reader, ctx, block_size, block_count, 0, out);
}

int capyos_gpt_identity_read_legacy(capyos_gpt_read_fn reader, void *ctx,
                                    uint32_t block_size, uint32_t block_count,
                                    struct capyos_gpt_identity *out) {
  return identity_read_mode(reader, ctx, block_size, block_count, 1, out);
}
