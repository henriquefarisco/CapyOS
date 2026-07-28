#include <stdio.h>
#include <string.h>

#include "boot/boot_slot_block_provider.h"
#include "boot/gpt_types.h"
#include "boot/gpt_identity.h"
#include "security/sha256.h"

#define PROVIDER_DISK_SECTORS 200u
#define PROVIDER_ESP_LBA 40u
#define PROVIDER_ESP_SECTORS 10u
#define PROVIDER_BOOT_LBA 50u
#define PROVIDER_BOOT_SECTORS 20u
#define PROVIDER_DATA_LBA 70u
#define PROVIDER_DATA_SECTORS 91u

struct provider_sector {
  uint32_t lba;
  uint8_t bytes[512];
  int present;
};

struct provider_mem {
  struct provider_sector sectors[80];
  uint32_t read_count;
  uint32_t write_count;
  uint32_t flush_count;
  int fail_flush;
};

static uint8_t provider_entries[128u * 128u];

static int provider_expect(int condition, const char *name) {
  if (!condition) {
    fprintf(stderr, "[boot-slot-block-provider] %s\n", name);
    return 1;
  }
  return 0;
}

static struct provider_sector *provider_find(struct provider_mem *mem,
                                             uint32_t lba, int create) {
  struct provider_sector *empty = NULL;
  if (!mem)
    return NULL;
  for (size_t i = 0u; i < sizeof(mem->sectors) / sizeof(mem->sectors[0]); ++i) {
    if (mem->sectors[i].present && mem->sectors[i].lba == lba)
      return &mem->sectors[i];
    if (!mem->sectors[i].present && !empty)
      empty = &mem->sectors[i];
  }
  if (create && empty) {
    empty->present = 1;
    empty->lba = lba;
    return empty;
  }
  return NULL;
}

static int provider_read(void *ctx, uint32_t block_no, void *buffer) {
  struct provider_mem *mem = ctx;
  struct provider_sector *sector = NULL;
  if (!mem || !buffer || block_no >= PROVIDER_DISK_SECTORS)
    return -1;
  sector = provider_find(mem, block_no, 0);
  if (sector)
    memcpy(buffer, sector->bytes, 512u);
  else
    memset(buffer, 0, 512u);
  mem->read_count++;
  return 0;
}

static int provider_write(void *ctx, uint32_t block_no, const void *buffer) {
  struct provider_mem *mem = ctx;
  struct provider_sector *sector = NULL;
  if (!mem || !buffer || block_no >= PROVIDER_DISK_SECTORS)
    return -1;
  sector = provider_find(mem, block_no, 1);
  if (!sector)
    return -1;
  memcpy(sector->bytes, buffer, 512u);
  mem->write_count++;
  return 0;
}

static int provider_flush(void *ctx);

static const struct block_device_ops provider_ops = {
    .read_block = provider_read,
    .write_block = provider_write,
    .flush = provider_flush,
};

static enum block_io_error_class provider_read_ex(void *ctx, uint32_t block_no,
                                                   void *buffer) {
  return provider_read(ctx, block_no, buffer) == 0
             ? BLOCK_IO_OK
             : BLOCK_IO_ERR_PERMANENT;
}

static enum block_io_error_class provider_write_ex(void *ctx, uint32_t block_no,
                                                    const void *buffer) {
  return provider_write(ctx, block_no, buffer) == 0
             ? BLOCK_IO_OK
             : BLOCK_IO_ERR_PERMANENT;
}

static const struct block_device_ops provider_no_flush_ops = {
    .read_block = provider_read,
    .write_block = provider_write,
};

static const struct block_device_ops provider_ex_ops = {
    .read_block_ex = provider_read_ex,
    .write_block_ex = provider_write_ex,
    .flush = provider_flush,
};

static int provider_flush(void *ctx) {
  struct provider_mem *mem = ctx;
  if (!mem || mem->fail_flush)
    return -1;
  mem->flush_count++;
  return 0;
}

static int provider_identity_read(
    void *ctx, uint32_t lba,
    uint8_t sector[CAPYOS_GPT_IDENTITY_SECTOR_SIZE]) {
  return provider_read(ctx, lba, sector);
}

static void provider_put_u32(uint8_t *dst, uint32_t value) {
  dst[0] = (uint8_t)value;
  dst[1] = (uint8_t)(value >> 8);
  dst[2] = (uint8_t)(value >> 16);
  dst[3] = (uint8_t)(value >> 24);
}

static uint32_t provider_get_u32(const uint8_t *src) {
  return (uint32_t)src[0] | ((uint32_t)src[1] << 8) |
         ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

static void provider_put_u64(uint8_t *dst, uint64_t value) {
  provider_put_u32(dst, (uint32_t)value);
  provider_put_u32(dst + 4u, (uint32_t)(value >> 32));
}

static uint32_t provider_crc32(const uint8_t *data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0u; i < len; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^
            (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
  }
  return ~crc;
}

static void provider_entry(uint32_t index, const uint8_t type[16],
                           uint32_t first, uint32_t last, const char *name) {
  uint8_t *entry = provider_entries + index * 128u;
  size_t name_len = strlen(name);
  memset(entry, 0, 128u);
  memcpy(entry, type, 16u);
  for (size_t i = 0u; i < 16u; ++i)
    entry[16u + i] = (uint8_t)(index * 17u + i + 1u);
  provider_put_u64(entry + 32u, first);
  provider_put_u64(entry + 40u, last);
  for (size_t i = 0u; i < name_len && i < 36u; ++i) {
    entry[56u + i * 2u] = (uint8_t)name[i];
    entry[57u + i * 2u] = 0u;
  }
}

static void provider_publish_gpt(struct provider_mem *mem) {
  uint8_t header[512];
  uint32_t entries_crc = provider_crc32(provider_entries,
                                         sizeof(provider_entries));
  memset(header, 0, sizeof(header));
  provider_put_u64(header, 0x5452415020494645ULL);
  provider_put_u32(header + 8u, 0x00010000u);
  provider_put_u32(header + 12u, 92u);
  provider_put_u64(header + 24u, 1u);
  provider_put_u64(header + 32u, PROVIDER_DISK_SECTORS - 1u);
  provider_put_u64(header + 40u, 34u);
  provider_put_u64(header + 48u, 166u);
  for (size_t i = 0u; i < 16u; ++i)
    header[56u + i] = (uint8_t)(0x80u + i);
  provider_put_u64(header + 72u, 2u);
  provider_put_u32(header + 80u, 128u);
  provider_put_u32(header + 84u, 128u);
  provider_put_u32(header + 88u, entries_crc);
  provider_put_u32(header + 16u, provider_crc32(header, 92u));
  provider_write(mem, 1u, header);
  for (uint32_t i = 0u; i < 32u; ++i) {
    provider_write(mem, 2u + i, provider_entries + i * 512u);
    provider_write(mem, 167u + i, provider_entries + i * 512u);
  }
  memset(header, 0, sizeof(header));
  provider_put_u64(header, 0x5452415020494645ULL);
  provider_put_u32(header + 8u, 0x00010000u);
  provider_put_u32(header + 12u, 92u);
  provider_put_u64(header + 24u, PROVIDER_DISK_SECTORS - 1u);
  provider_put_u64(header + 32u, 1u);
  provider_put_u64(header + 40u, 34u);
  provider_put_u64(header + 48u, 166u);
  for (size_t i = 0u; i < 16u; ++i)
    header[56u + i] = (uint8_t)(0x80u + i);
  provider_put_u64(header + 72u, 167u);
  provider_put_u32(header + 80u, 128u);
  provider_put_u32(header + 84u, 128u);
  provider_put_u32(header + 88u, entries_crc);
  provider_put_u32(header + 16u, provider_crc32(header, 92u));
  provider_write(mem, PROVIDER_DISK_SECTORS - 1u, header);
  mem->write_count = 0u;
}

static void provider_build_gpt(struct provider_mem *mem) {
  static const uint8_t esp[16] = {
      0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
      0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B};
  static const uint8_t boot[16] = {
      0x76, 0x0b, 0x98, 0x04, 0x42, 0x10, 0x4c, 0x9b,
      0x86, 0x1f, 0x11, 0xe0, 0x29, 0xea, 0xc1, 0x01};
  static const uint8_t data[16] = {
      0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
      0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4};
  memset(mem, 0, sizeof(*mem));
  memset(provider_entries, 0, sizeof(provider_entries));
  provider_entry(0u, esp, PROVIDER_ESP_LBA,
                 PROVIDER_ESP_LBA + PROVIDER_ESP_SECTORS - 1u, "ESP");
  provider_entry(1u, boot, PROVIDER_BOOT_LBA,
                 PROVIDER_BOOT_LBA + PROVIDER_BOOT_SECTORS - 1u, "BOOT");
  provider_entry(2u, data, PROVIDER_DATA_LBA,
                 PROVIDER_DATA_LBA + PROVIDER_DATA_SECTORS - 1u, "DATA");
  provider_publish_gpt(mem);
}

int test_boot_slot_block_provider_run(void) {
  static const uint8_t boot_guid[16] = {
      0x76, 0x0b, 0x98, 0x04, 0x42, 0x10, 0x4c, 0x9b,
      0x86, 0x1f, 0x11, 0xe0, 0x29, 0xea, 0xc1, 0x01};
  uint8_t payload[700];
  struct provider_mem mem;
  struct block_device raw = {
      .name = "provider-test",
      .block_size = 512u,
      .block_count = PROVIDER_DISK_SECTORS,
      .ctx = &mem,
      .ops = &provider_ops,
  };
  struct boot_slot_disk_binding binding = {
      .esp_lba = PROVIDER_ESP_LBA,
      .esp_sectors = PROVIDER_ESP_SECTORS,
      .boot_lba = PROVIDER_BOOT_LBA,
      .boot_sectors = PROVIDER_BOOT_SECTORS,
      .data_lba = PROVIDER_DATA_LBA,
      .data_sectors = PROVIDER_DATA_SECTORS,
  };
  struct boot_slot_block_provider provider;
  struct boot_slot_block_provider second_provider;
  struct boot_slot_store store;
  struct boot_slot_store active_store_copy;
  struct boot_slot_store stale_store;
  struct boot_slot_block_provider_test_io stale_io;
  struct boot_slot_snapshot snapshot;
  struct boot_slot_snapshot unchanged_snapshot;
  struct boot_slot_manager observed_manager;
  struct boot_slot_layout layout;
  struct boot_slot_image active_image;
  struct boot_slot_image image;
  struct provider_sector *written = NULL;
  uint64_t registration_epoch = 0u;
  uint64_t second_registration_epoch = 0u;
  uint64_t valid_generation = 0u;
  uint64_t lease_epoch = 0u;
  uint64_t stale_lease_epoch = 0u;
  int fails = 0;

  for (size_t i = 0u; i < sizeof(payload); ++i)
    payload[i] = (uint8_t)(i * 13u + 7u);
  provider_build_gpt(&mem);
  {
    struct capyos_gpt_identity strict_identity;
    fails += capyos_gpt_identity_read(
                 provider_identity_read, &mem, 512u, PROVIDER_DISK_SECTORS,
                 &strict_identity) == 0 &&
                     strict_identity.boot.lba == PROVIDER_BOOT_LBA
                 ? 0
                 : 1;
  }
  {
    struct provider_sector *primary_header = provider_find(&mem, 1u, 0);
    struct provider_sector *backup_first = provider_find(&mem, 167u, 0);
    struct provider_sector *backup_last = provider_find(&mem, 198u, 0);
    struct capyos_gpt_identity mismatched_identity;
    uint32_t colliding_crc = 0u;
    if (!primary_header || !backup_first || !backup_last) {
      fails += provider_expect(0, "colliding CRC fixture sectors present");
    } else {
      provider_entries[272u] ^= 0x80u;
      provider_entries[sizeof(provider_entries) - 4u] = 0xD3u;
      provider_entries[sizeof(provider_entries) - 3u] = 0x4Du;
      provider_entries[sizeof(provider_entries) - 2u] = 0x00u;
      provider_entries[sizeof(provider_entries) - 1u] = 0x5Bu;
      colliding_crc = provider_crc32(provider_entries, sizeof(provider_entries));
      provider_entries[272u] ^= 0x80u;
      memset(provider_entries + sizeof(provider_entries) - 4u, 0, 4u);
      backup_first->bytes[272u] ^= 0x80u;
      backup_last->bytes[508u] = 0xD3u;
      backup_last->bytes[509u] = 0x4Du;
      backup_last->bytes[510u] = 0x00u;
      backup_last->bytes[511u] = 0x5Bu;
      fails += provider_expect(
          colliding_crc == provider_get_u32(primary_header->bytes + 88u) &&
              capyos_gpt_identity_read(
                  provider_identity_read, &mem, 512u, PROVIDER_DISK_SECTORS,
                  &mismatched_identity) != 0 &&
              mismatched_identity.disk_sectors == 0u,
          "backup entries with colliding CRC rejected");
    }
  }
  provider_build_gpt(&mem);
  for (size_t i = 0u; i < 16u; ++i) {
    binding.disk_guid[i] = (uint8_t)(0x80u + i);
    binding.esp_guid[i] = (uint8_t)(i + 1u);
    binding.boot_guid[i] = (uint8_t)(18u + i);
    binding.data_guid[i] = (uint8_t)(35u + i);
  }
  fails += provider_expect(
      boot_slot_test_reset_uninitialized() == 0 &&
          boot_slot_layout_plan(PROVIDER_BOOT_SECTORS, &layout) == 0 &&
          boot_slot_manager_get(&observed_manager) != 0,
      "provider test starts with unavailable core");
  fails += provider_expect(
      boot_slot_block_provider_init(&provider, &raw, &binding, provider_flush,
                                    &mem, &registration_epoch) == 0 &&
          registration_epoch != 0u &&
          provider.opaque_epoch == registration_epoch,
      "provider binds exact GPT disk with registration epoch");
  provider.opaque_epoch ^= 1u;
  fails += provider_expect(
      boot_slot_block_provider_open_store(&provider, registration_epoch, &store,
                                          &lease_epoch) ==
              BOOT_SLOT_BLOCK_PROVIDER_ERR &&
          lease_epoch == 0u && boot_slot_manager_get(&observed_manager) != 0,
      "mutated provider handle fails before core initialization");
  provider.opaque_epoch = registration_epoch;
  provider_put_u64(provider_entries + 256u + 32u, PROVIDER_DATA_LBA + 1u);
  provider_publish_gpt(&mem);
  fails += provider_expect(
      boot_slot_block_provider_open_store(&provider, registration_epoch, &store,
                                          &lease_epoch) ==
              BOOT_SLOT_BLOCK_PROVIDER_ERR &&
          lease_epoch == 0u,
      "valid GPT geometry change rejected at open");
  provider_build_gpt(&mem);
  fails += provider_expect(
      boot_slot_block_provider_open_store(&provider, registration_epoch, &store,
                                          &lease_epoch) == 0 &&
          lease_epoch != 0u && boot_slot_manager_get(&observed_manager) != 0,
      "provider opens unbound store with observers unavailable");
  active_store_copy = store;
  {
    uint32_t reads_before = mem.read_count;
    fails += provider_expect(
        boot_slot_store_read_header(&active_store_copy, lease_epoch, 1u,
                                    &image) ==
                BOOT_SLOT_STORE_ERR_IO &&
            mem.read_count == reads_before,
        "active copied store cannot reach private callbacks");
  }
  store.opaque_epoch ^= 1u;
  fails += provider_expect(
      boot_slot_store_bind_control(&store, lease_epoch) == BOOT_SLOT_STORE_ERR_BUSY,
      "mutated store handle cannot bind control");
  store.opaque_epoch = lease_epoch;
  fails += provider_expect(
      boot_slot_block_provider_init(
          &second_provider, &raw, &binding, provider_flush, &mem,
          &second_registration_epoch) == BOOT_SLOT_BLOCK_PROVIDER_ERR &&
          second_registration_epoch == 0u,
      "active registration and lease reject a second provider");
  memset(&active_image, 0, sizeof(active_image));
  memcpy(active_image.version, "1.0.0", 6u);
  active_image.payload_size = 512u;
  active_image.payload_sha256[0] = 1u;
  fails += provider_expect(
      boot_slot_store_bind_control(&store, lease_epoch) == BOOT_SLOT_PERSIST_EMPTY &&
          boot_slot_store_initialize_persistent(&store, lease_epoch,
                                                &active_image) == 0 &&
          boot_slot_snapshot_get(&snapshot) == 0,
      "provider binds control mirrors to same raw BOOT");
  unchanged_snapshot = snapshot;
  active_store_copy = store;
  {
    uint32_t reads_before = mem.read_count;
    uint32_t writes_before = mem.write_count;
    uint32_t flushes_before = mem.flush_count;
    fails += provider_expect(
        boot_slot_store_initialize_persistent(NULL, lease_epoch,
                                              &active_image) != 0 &&
            boot_slot_store_initialize_persistent(
                &active_store_copy, lease_epoch, &active_image) != 0 &&
            boot_slot_store_initialize_persistent(
                &store, lease_epoch - 1u, &active_image) != 0 &&
            boot_slot_store_initialize_persistent(
                &store, lease_epoch, &active_image) != 0 &&
            boot_slot_snapshot_get(&snapshot) == 0 &&
            snapshot.generation == unchanged_snapshot.generation &&
            snapshot.authority_epoch == unchanged_snapshot.authority_epoch &&
            snapshot.lease_epoch == unchanged_snapshot.lease_epoch &&
            mem.read_count == reads_before && mem.write_count == writes_before &&
            mem.flush_count == flushes_before,
        "rejected initialization preserves ready lifecycle without I/O");
  }
  mem.flush_count = 0u;
  memset(&image, 0, sizeof(image));
  memcpy(image.version, "2.0.0", 6u);
  image.payload_size = sizeof(payload);
  sha256_hash(payload, sizeof(payload), image.payload_sha256);
  snapshot.lease_epoch ^= 1u;
  {
    uint32_t writes_before = mem.write_count;
    fails += provider_expect(
        boot_slot_store_stage_inactive_authorized(
            &store, lease_epoch, &snapshot, 1u, &image, payload,
            sizeof(payload), &valid_generation) == BOOT_SLOT_STORE_ERR_STALE &&
            mem.write_count == writes_before,
        "snapshot with stale lease epoch fails before I/O");
  }
  snapshot.lease_epoch ^= 1u;
  fails += provider_expect(
      boot_slot_store_stage_inactive_authorized(
          &store, lease_epoch, &snapshot, 1u, &image, payload, sizeof(payload),
          &valid_generation) == 0 &&
          valid_generation == 5u && mem.flush_count == 5u,
      "provider stages authorized raw slot with mirrored control");
  written = provider_find(&mem, PROVIDER_BOOT_LBA +
                                    layout.slots[1].payload_lba, 0);
  fails += provider_expect(written &&
                           memcmp(written->bytes, payload, 512u) == 0,
                           "relative payload maps to raw BOOT");
  fails += provider_expect(
      provider_find(&mem, PROVIDER_BOOT_LBA, 0) == NULL &&
          provider_find(&mem, PROVIDER_BOOT_LBA + 1u, 0) == NULL,
      "raw active slot remains untouched");

  fails += provider_expect(boot_slot_snapshot_get(&snapshot) == 0,
                           "published provider snapshot available");
  memcpy(image.version, "3.0.0", 6u);
  mem.fail_flush = 1;
  fails += provider_expect(
      boot_slot_store_stage_inactive_authorized(
          &store, lease_epoch, &snapshot, 1u, &image, payload, sizeof(payload),
          &valid_generation) == BOOT_SLOT_STORE_ERR_COMMIT_UNKNOWN,
      "backend control flush failure propagates");
  mem.fail_flush = 0;
  fails += provider_expect(
      boot_slot_block_provider_init(
          &second_provider, &raw, &binding, provider_flush, &mem,
          &second_registration_epoch) == BOOT_SLOT_BLOCK_PROVIDER_ERR,
      "leased provider cannot be rebound to another backend");
  fails += provider_expect(boot_slot_init() == BOOT_SLOT_ERR_BUSY,
                           "global reset cannot bypass provider lease");
  stale_lease_epoch = lease_epoch;
  stale_store = store;
  fails += provider_expect(
      boot_slot_block_provider_test_capture_io(&stale_io) == 0,
      "active private callback tuple captured for ABA regression");
  fails += provider_expect(
      boot_slot_block_provider_close_store(
          &provider, registration_epoch, &store, lease_epoch) == 0 &&
          store.opaque_epoch == 0u &&
          boot_slot_manager_get(&observed_manager) != 0,
      "provider lease releases after control shutdown");
  provider_build_gpt(&mem);
  fails += provider_expect(
      boot_slot_block_provider_open_store(&provider, registration_epoch, &store,
                                          &lease_epoch) == 0 &&
          lease_epoch > stale_lease_epoch &&
          boot_slot_block_provider_close_store(
              &provider, registration_epoch, &store, stale_lease_epoch) ==
              BOOT_SLOT_BLOCK_PROVIDER_ERR &&
          boot_slot_store_read_header(&stale_store, stale_lease_epoch,
                                      1u, &image) ==
              BOOT_SLOT_STORE_ERR_IO,
      "stale lease token and copied store cannot reach reopened provider");
  {
    uint32_t reads_before = mem.read_count;
    fails += provider_expect(
        boot_slot_store_bind_control(&store, stale_lease_epoch) ==
                BOOT_SLOT_STORE_ERR_BUSY &&
            boot_slot_store_read_header(&store, stale_lease_epoch, 1u,
                                        &image) == BOOT_SLOT_STORE_ERR_IO &&
            mem.read_count == reads_before,
        "reused store pointer rejects the previous lease epoch");
  }
  {
    uint8_t sector[BOOT_SLOT_STORE_SECTOR_SIZE] = {0};
    uint32_t reads_before = mem.read_count;
    uint32_t writes_before = mem.write_count;
    uint32_t flushes_before = mem.flush_count;
    fails += provider_expect(
        stale_io.reader(stale_io.ctx, 0u, sector) != 0 &&
            stale_io.writer(stale_io.ctx, 0u, sector) != 0 &&
            stale_io.flusher(stale_io.ctx) != 0 &&
            mem.read_count == reads_before && mem.write_count == writes_before &&
            mem.flush_count == flushes_before,
        "stale private callback tuple cannot reach reopened backend");
  }
  fails += provider_expect(
      boot_slot_block_provider_close_store(
          &provider, registration_epoch, &store, lease_epoch) == 0,
      "current lease token revokes reopened provider");
  {
    uint64_t previous_epoch = lease_epoch;
    int cycles_ok = 1;
    for (uint32_t i = 0u; i < 12u; ++i) {
      if (boot_slot_block_provider_open_store(
              &provider, registration_epoch, &store, &lease_epoch) != 0 ||
          lease_epoch <= previous_epoch ||
          boot_slot_block_provider_close_store(
              &provider, registration_epoch, &store, lease_epoch) != 0) {
        cycles_ok = 0;
        break;
      }
      previous_epoch = lease_epoch;
    }
    fails += provider_expect(cycles_ok,
                             "provider lease capacity does not exhaust");
  }
  fails += provider_expect(
      boot_slot_block_provider_unregister(&provider,
                                          registration_epoch + 1u) ==
              BOOT_SLOT_BLOCK_PROVIDER_ERR &&
          boot_slot_block_provider_unregister(&provider,
                                              registration_epoch) == 0,
      "registration requires exact token and explicit unregister");

  provider_build_gpt(&mem);
  binding.data_lba++;
  fails += provider_expect(
      boot_slot_block_provider_init(&provider, &raw, &binding,
                                    provider_flush, &mem, &registration_epoch) ==
          BOOT_SLOT_BLOCK_PROVIDER_ERR && mem.write_count == 0u,
      "wrong DATA binding fails closed");
  binding.data_lba--;

  provider_build_gpt(&mem);
  provider_find(&mem, 1u, 0)->bytes[16u] ^= 1u;
  fails += provider_expect(
      boot_slot_block_provider_init(&provider, &raw, &binding,
                                    provider_flush, &mem, &registration_epoch) ==
          BOOT_SLOT_BLOCK_PROVIDER_ERR,
      "header CRC corruption rejected");

  provider_build_gpt(&mem);
  provider_find(&mem, 2u, 0)->bytes[16u] ^= 1u;
  fails += provider_expect(
      boot_slot_block_provider_init(&provider, &raw, &binding,
                                    provider_flush, &mem, &registration_epoch) ==
          BOOT_SLOT_BLOCK_PROVIDER_ERR,
      "entries CRC corruption rejected");

  provider_build_gpt(&mem);
  provider_find(&mem, PROVIDER_DISK_SECTORS - 1u, 0)->bytes[16u] ^= 1u;
  fails += provider_expect(
      boot_slot_block_provider_init(&provider, &raw, &binding,
                                    provider_flush, &mem, &registration_epoch) ==
          BOOT_SLOT_BLOCK_PROVIDER_ERR,
      "backup header corruption rejected");

  provider_build_gpt(&mem);
  provider_entries[128u + 48u] = 1u;
  provider_publish_gpt(&mem);
  fails += provider_expect(
      boot_slot_block_provider_init(&provider, &raw, &binding,
                                    provider_flush, &mem, &registration_epoch) ==
          BOOT_SLOT_BLOCK_PROVIDER_ERR,
      "partition attributes rejected");

  provider_build_gpt(&mem);
  for (size_t i = 0u; i < 16u; ++i) {
    provider_entries[16u + i] = (uint8_t)(0x80u + i);
    provider_entries[128u + 16u + i] = (uint8_t)(0x80u + i);
    provider_entries[256u + 16u + i] = (uint8_t)(0x80u + i);
  }
  provider_publish_gpt(&mem);
  {
    struct capyos_gpt_identity legacy_identity;
    fails += provider_expect(
        capyos_gpt_identity_read(provider_identity_read, &mem, 512u,
                                 PROVIDER_DISK_SECTORS,
                                 &legacy_identity) != 0 &&
            capyos_gpt_identity_read_legacy(
                provider_identity_read, &mem, 512u, PROVIDER_DISK_SECTORS,
                &legacy_identity) == 0,
        "legacy duplicate GUID layout remains readable without identity");
  }

  provider_build_gpt(&mem);
  provider_entry(3u, boot_guid, 161u, 166u, "BOOT");
  provider_publish_gpt(&mem);
  fails += provider_expect(
      boot_slot_block_provider_init(&provider, &raw, &binding,
                                    provider_flush, &mem, &registration_epoch) ==
          BOOT_SLOT_BLOCK_PROVIDER_ERR,
      "duplicate BOOT rejected");

  provider_build_gpt(&mem);
  fails += provider_expect(
      boot_slot_block_provider_init(&provider, &raw, &binding, NULL, NULL,
                                    &registration_epoch) ==
          BOOT_SLOT_BLOCK_PROVIDER_ERR,
      "missing flush rejected");
  raw.block_size = 4096u;
  fails += provider_expect(
      boot_slot_block_provider_init(&provider, &raw, &binding,
                                    provider_flush, &mem, &registration_epoch) ==
          BOOT_SLOT_BLOCK_PROVIDER_ERR,
      "non-512 block device rejected");
  raw.block_size = 512u;
  raw.ops = &provider_no_flush_ops;
  fails += provider_expect(
      boot_slot_block_provider_init_from_block_device(
          &provider, &raw, &binding, &registration_epoch) ==
          BOOT_SLOT_BLOCK_PROVIDER_ERR,
      "block provider requires raw durable flush capability");
  raw.ops = &provider_ex_ops;
  fails += provider_expect(
      boot_slot_block_provider_init_from_block_device(
          &provider, &raw, &binding, &registration_epoch) == 0 &&
          boot_slot_block_provider_unregister(&provider,
                                              registration_epoch) == 0,
      "extended-only block device with durable flush accepted");

  /* End-to-end A/B lifecycle over a pristine provider-backed disk: stage the
   * inactive slot, arm exactly one attempt, spend it the way the loader does,
   * confirm health durably, then prove that an unconfirmed attempt rolls back
   * to the confirmed slot. */
  provider_build_gpt(&mem);
  raw.ops = &provider_ops;
  fails += provider_expect(
      boot_slot_block_provider_init_from_block_device(
          &provider, &raw, &binding, &registration_epoch) == 0 &&
          boot_slot_block_provider_open_store(&provider, registration_epoch,
                                              &store, &lease_epoch) == 0 &&
          boot_slot_store_bind_control(&store, lease_epoch) ==
              BOOT_SLOT_PERSIST_EMPTY &&
          boot_slot_store_initialize_persistent(&store, lease_epoch,
                                                &active_image) == 0 &&
          boot_slot_snapshot_get(&snapshot) == 0,
      "lifecycle fixture binds a fresh provider-backed control");
  memcpy(image.version, "4.0.0", 6u);
  image.payload_size = sizeof(payload);
  sha256_hash(payload, sizeof(payload), image.payload_sha256);
  fails += provider_expect(
      boot_slot_store_stage_inactive_authorized(
          &store, lease_epoch, &snapshot, 1u, &image, payload, sizeof(payload),
          &valid_generation) == 0 &&
          valid_generation == 5u,
      "lifecycle stages the inactive slot");
  {
    uint64_t armed_generation = 0u;
    uint64_t attempt_generation = 0u;
    uint32_t selected = BOOT_SLOT_NONE;
    fails += provider_expect(
        boot_slot_store_arm(&store, lease_epoch, 1u, valid_generation - 1u,
                            &armed_generation) == BOOT_SLOT_STORE_ERR_STALE &&
            boot_slot_store_arm(&store, lease_epoch ^ 1u, 1u, valid_generation,
                                &armed_generation) ==
                BOOT_SLOT_STORE_ERR_STALE &&
            boot_slot_store_arm(&store, lease_epoch, 0u, valid_generation,
                                &armed_generation) ==
                BOOT_SLOT_STORE_ERR_STALE &&
            armed_generation == 0u,
        "arm requires the exact lease, generation and staged slot");
    fails += provider_expect(
        boot_slot_store_arm(&store, lease_epoch, 1u, valid_generation,
                            &armed_generation) == 0 &&
            armed_generation == valid_generation + 1u &&
            boot_slot_manager_get(&observed_manager) == 0 &&
            observed_manager.pending_slot == 1u &&
            observed_manager.tries_remaining == BOOT_SLOT_DEFAULT_TRIES &&
            observed_manager.rollback_pending == 1 &&
            observed_manager.confirmed_slot == 0u,
        "arm publishes exactly one durable boot attempt");
    attempt_generation = armed_generation;
    fails += provider_expect(
        boot_slot_store_arm(&store, lease_epoch, 1u, attempt_generation,
                            &armed_generation) == BOOT_SLOT_STORE_ERR_STALE,
        "a second arm cannot stack attempts");
    armed_generation = attempt_generation;
    fails += provider_expect(
        boot_slot_select_for_boot(&selected, &attempt_generation) == 1 &&
            selected == 1u && attempt_generation == armed_generation + 1u,
        "loader select consumes the single attempt");
    fails += provider_expect(
        boot_slot_store_confirm_health(&store, lease_epoch, 0u,
                                       attempt_generation) != 0 &&
            boot_slot_store_confirm_health(&store, lease_epoch, 1u,
                                           attempt_generation) == 0 &&
            boot_slot_manager_get(&observed_manager) == 0 &&
            observed_manager.confirmed_slot == 1u &&
            observed_manager.pending_slot == BOOT_SLOT_NONE &&
            observed_manager.rollback_pending == 0 &&
            observed_manager.slots[1].health_confirmed &&
            observed_manager.slots[0].state == BOOT_SLOT_VALID,
        "durable confirmation commits the attempted slot");
    fails += provider_expect(boot_slot_snapshot_get(&snapshot) == 0,
                             "confirmed lifecycle exposes a fresh snapshot");
    memcpy(image.version, "5.0.0", 6u);
    fails += provider_expect(
        boot_slot_store_stage_inactive_authorized(
            &store, lease_epoch, &snapshot, 0u, &image, payload,
            sizeof(payload), &valid_generation) == 0 &&
            boot_slot_store_arm(&store, lease_epoch, 0u, valid_generation,
                                &armed_generation) == 0 &&
            boot_slot_select_for_boot(&selected, &attempt_generation) == 1 &&
            selected == 0u,
        "rollback cycle stages the other slot and spends its only attempt");
    fails += provider_expect(
        boot_slot_select_for_boot(&selected, &attempt_generation) == 2 &&
            selected == 1u && boot_slot_manager_get(&observed_manager) == 0 &&
            observed_manager.slots[0].state == BOOT_SLOT_FAILED &&
            observed_manager.confirmed_slot == 1u &&
            observed_manager.pending_slot == BOOT_SLOT_NONE &&
            observed_manager.rollback_pending == 0,
        "an unconfirmed attempt rolls back to the confirmed slot");
  }
  fails += provider_expect(
      boot_slot_block_provider_close_store(&provider, registration_epoch,
                                           &store, lease_epoch) == 0 &&
          boot_slot_block_provider_unregister(&provider,
                                              registration_epoch) == 0,
      "lifecycle fixture releases the provider");

  if (fails == 0)
    printf("[test_boot_slot_block_provider] all passed\n");
  return fails;
}
