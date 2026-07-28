#include <stdio.h>
#include <string.h>

#include "boot/boot_slot.h"
#include "boot/boot_slot_store.h"
#include "security/sha256.h"

#define STORE_TEST_SECTORS 524288u
#define STORE_TEST_PAYLOAD_SIZE 777u

struct store_sector {
  uint32_t lba;
  uint8_t bytes[BOOT_SLOT_STORE_SECTOR_SIZE];
  int present;
};

struct store_mem {
  struct store_sector stored[10];
  uint32_t fail_read_lba;
  uint32_t fail_write_lba;
  int fail_flush;
  int corrupt_payload_read;
  int corrupt_header_read;
  int probe_snapshot_on_write;
  int snapshot_probe_rc;
  int init_probe_rc;
  int rebind_probe_rc;
  uint32_t read_count;
  uint32_t write_count;
  uint32_t flush_count;
  uint32_t event_count;
  char event_kind[32];
  uint32_t event_lba[32];
};

static int store_expect(int condition, const char *name) {
  if (!condition) {
    fprintf(stderr, "[boot-slot-store] %s\n", name);
    return 1;
  }
  return 0;
}

static void store_event(struct store_mem *mem, char kind, uint32_t lba) {
  if (!mem || mem->event_count >= sizeof(mem->event_kind))
    return;
  mem->event_kind[mem->event_count] = kind;
  mem->event_lba[mem->event_count] = lba;
  mem->event_count++;
}

static struct store_sector *store_find(struct store_mem *mem, uint32_t lba,
                                       int create) {
  struct store_sector *empty = NULL;
  if (!mem)
    return NULL;
  for (size_t i = 0u; i < sizeof(mem->stored) / sizeof(mem->stored[0]); ++i) {
    if (mem->stored[i].present && mem->stored[i].lba == lba)
      return &mem->stored[i];
    if (!mem->stored[i].present && !empty)
      empty = &mem->stored[i];
  }
  if (create && empty) {
    empty->present = 1;
    empty->lba = lba;
    return empty;
  }
  return NULL;
}

static int store_read(void *ctx, uint32_t lba,
                      uint8_t sector[BOOT_SLOT_STORE_SECTOR_SIZE]) {
  struct store_mem *mem = ctx;
  struct store_sector *stored;
  if (!mem || !sector || lba >= STORE_TEST_SECTORS ||
      lba == mem->fail_read_lba)
    return -1;
  stored = store_find(mem, lba, 0);
  if (stored)
    memcpy(sector, stored->bytes, BOOT_SLOT_STORE_SECTOR_SIZE);
  else
    memset(sector, 0, BOOT_SLOT_STORE_SECTOR_SIZE);
  if (mem->corrupt_payload_read && lba == 262144u)
    sector[0] ^= 1u;
  if (mem->corrupt_header_read && lba == 262143u)
    sector[0] ^= 1u;
  mem->read_count++;
  store_event(mem, 'R', lba);
  return 0;
}

static int probe_control_read(void *ctx, uint32_t copy_index, uint8_t *record,
                              size_t record_size) {
  (void)ctx;
  (void)copy_index;
  (void)record;
  (void)record_size;
  return BOOT_SLOT_PERSIST_EMPTY;
}

static int probe_control_write(void *ctx, uint32_t copy_index,
                               const uint8_t *record, size_t record_size) {
  (void)ctx;
  (void)copy_index;
  (void)record;
  (void)record_size;
  return 0;
}

static int probe_control_flush(void *ctx) {
  (void)ctx;
  return 0;
}

static int store_write(void *ctx, uint32_t lba,
                       const uint8_t sector[BOOT_SLOT_STORE_SECTOR_SIZE]) {
  struct store_mem *mem = ctx;
  struct store_sector *stored;
  if (!mem || !sector || lba >= STORE_TEST_SECTORS ||
      lba == mem->fail_write_lba)
    return -1;
  stored = store_find(mem, lba, 1);
  if (!stored)
    return -1;
  if (mem->probe_snapshot_on_write) {
    struct boot_slot_snapshot probe;
    struct boot_slot_layout layout;
    mem->snapshot_probe_rc = boot_slot_snapshot_get(&probe);
    mem->init_probe_rc = boot_slot_init();
    boot_slot_layout_plan(STORE_TEST_SECTORS, &layout);
    mem->rebind_probe_rc = boot_slot_set_persistence(
        probe_control_read, probe_control_write, probe_control_flush, mem,
        &layout);
    mem->probe_snapshot_on_write = 0;
  }
  memcpy(stored->bytes, sector, BOOT_SLOT_STORE_SECTOR_SIZE);
  mem->write_count++;
  store_event(mem, 'W', lba);
  return 0;
}

static int store_flush(void *ctx) {
  struct store_mem *mem = ctx;
  if (!mem)
    return -1;
  mem->flush_count++;
  store_event(mem, 'F', UINT32_MAX);
  return mem->fail_flush ? -1 : 0;
}

static void store_reset_events(struct store_mem *mem) {
  if (!mem)
    return;
  mem->read_count = 0u;
  mem->write_count = 0u;
  mem->flush_count = 0u;
  mem->event_count = 0u;
  memset(mem->event_kind, 0, sizeof(mem->event_kind));
  memset(mem->event_lba, 0, sizeof(mem->event_lba));
}

static void store_image(struct boot_slot_image *image, const char *version,
                        const uint8_t *payload, size_t payload_len) {
  memset(image, 0, sizeof(*image));
  strncpy(image->version, version, sizeof(image->version) - 1u);
  image->payload_size = (uint32_t)payload_len;
  sha256_hash(payload, payload_len, image->payload_sha256);
}

static int store_prepare(struct boot_slot_store *store,
                         struct boot_slot_layout *layout,
                         struct store_mem *mem,
                         struct boot_slot_snapshot *snapshot) {
  struct boot_slot_image active;
  uint8_t digest_payload[32];
  memset(mem, 0, sizeof(*mem));
  mem->fail_read_lba = UINT32_MAX;
  mem->fail_write_lba = UINT32_MAX;
  for (size_t i = 0u; i < sizeof(digest_payload); ++i)
    digest_payload[i] = (uint8_t)(0x40u + i);
  boot_slot_init();
  if (boot_slot_layout_plan(STORE_TEST_SECTORS, layout) != 0 ||
      boot_slot_store_init(store, layout, store_read, store_write, store_flush,
                           mem) != 0)
    return -1;
  if (boot_slot_store_bind_control(store, 0u) != BOOT_SLOT_PERSIST_EMPTY)
    return -1;
  store_image(&active, "1.0.0", digest_payload, sizeof(digest_payload));
  active.payload_size = 4096u;
  if (boot_slot_initialize_persistent(layout, &active) != 0 ||
      boot_slot_snapshot_get(snapshot) != 0)
    return -1;
  store_reset_events(mem);
  return 0;
}

int test_boot_slot_store_run(void) {
  uint8_t payload[STORE_TEST_PAYLOAD_SIZE];
  struct boot_slot_layout layout;
  struct boot_slot_image image;
  struct boot_slot_image decoded;
  struct boot_slot_snapshot snapshot;
  struct boot_slot_snapshot published;
  struct boot_slot_manager manager;
  struct boot_slot_store store;
  struct store_mem mem;
  uint64_t valid_generation = 0u;
  int fails = 0;

  for (size_t i = 0u; i < sizeof(payload); ++i)
    payload[i] = (uint8_t)(i * 17u + 3u);
  store_image(&image, "2.0.0", payload, sizeof(payload));

  fails += store_expect(store_prepare(&store, &layout, &mem, &snapshot) == 0,
                        "prepare bound control store");
  fails += store_expect(
      boot_slot_store_stage_inactive(&store, &snapshot.manager, 1u, &image,
                                     payload, sizeof(payload)) ==
              BOOT_SLOT_STORE_ERR_STALE &&
          mem.event_count == 0u,
      "legacy unbound stage refused without io");
  decoded = image;
  decoded.payload_sha256[0] ^= 1u;
  fails += store_expect(
      boot_slot_store_stage_inactive_authorized(
          &store, 0u, &snapshot, 1u, &decoded, payload, sizeof(payload),
          &valid_generation) == BOOT_SLOT_STORE_ERR_VERIFY &&
          mem.event_count == 0u && boot_slot_persistence_generation() == 2u,
      "payload preflight precedes mirror invalidation");
  mem.probe_snapshot_on_write = 1;
  fails += store_expect(
      boot_slot_store_stage_inactive_authorized(
          &store, 0u, &snapshot, 1u, &image, payload, sizeof(payload),
          &valid_generation) == 0 &&
          valid_generation == 5u &&
          mem.snapshot_probe_rc == BOOT_SLOT_ERR_BUSY &&
          mem.init_probe_rc == BOOT_SLOT_ERR_BUSY &&
          mem.rebind_probe_rc == BOOT_SLOT_ERR_BUSY && mem.write_count == 6u &&
          mem.read_count == 6u && mem.flush_count == 5u &&
          mem.event_count == 17u,
      "authorized mirrored stage publishes generation");
  fails += store_expect(
      mem.event_kind[0] == 'W' &&
          mem.event_lba[0] == layout.control_lba[0] &&
          mem.event_kind[1] == 'F' && mem.event_kind[2] == 'R' &&
          mem.event_lba[2] == layout.control_lba[0] &&
          mem.event_kind[3] == 'W' &&
          mem.event_lba[3] == layout.control_lba[1] &&
          mem.event_kind[6] == 'W' &&
          mem.event_lba[6] == layout.slots[1].payload_lba &&
          mem.event_kind[11] == 'W' &&
          mem.event_lba[11] == layout.slots[1].header_lba &&
          mem.event_kind[14] == 'W' &&
          mem.event_lba[14] == layout.control_lba[0],
      "both invalid mirrors precede payload and header publish");
  fails += store_expect(
      boot_slot_snapshot_get(&published) == 0 &&
          published.generation == valid_generation &&
          published.manager.slots[1].state == BOOT_SLOT_VALID &&
          published.manager.next_slot == 1u &&
          boot_slot_store_read_header(&store, 0u, 1u, &decoded) == 0 &&
          decoded.payload_size == image.payload_size &&
          memcmp(decoded.payload_sha256, image.payload_sha256,
                 BOOT_SLOT_SHA256_SIZE) == 0,
      "published manager matches verified header");

  store_reset_events(&mem);
  fails += store_expect(
      boot_slot_store_stage_inactive_authorized(
          &store, 0u, &snapshot, 1u, &image, payload, sizeof(payload),
          &valid_generation) == BOOT_SLOT_STORE_ERR_STALE &&
          mem.event_count == 0u,
      "stale snapshot refused without io");
  {
    struct boot_slot_store replacement;
    fails += store_expect(
        boot_slot_store_init(&replacement, &layout, store_read, store_write,
                             store_flush, &mem) == 0 &&
            boot_slot_store_stage_inactive_authorized(
                &replacement, 0u, &published, 1u, &image, payload,
                sizeof(payload), &valid_generation) ==
                BOOT_SLOT_STORE_ERR_STALE &&
            mem.event_count == 0u,
        "snapshot cannot cross store binding");
  }

  for (size_t i = 0u; i < sizeof(payload); ++i)
    payload[i] ^= 0x5Au;
  store_image(&image, "3.0.0", payload, sizeof(payload));
  fails += store_expect(
      boot_slot_store_stage_inactive_authorized(
          &store, 0u, &published, 1u, &image, payload, sizeof(payload),
          &valid_generation) == 0 &&
          valid_generation == 8u,
      "valid inactive slot is durably invalidated before restage");
  fails += store_expect(
      boot_slot_store_init(&store, &layout, store_read, store_write, store_flush,
                           &mem) == BOOT_SLOT_STORE_ERR_BUSY,
      "bound store cannot be reinitialized in place");

  store_image(&image, "2.0.0", payload, sizeof(payload));
  fails += store_expect(store_prepare(&store, &layout, &mem, &snapshot) == 0,
                        "prepare payload corruption case");
  mem.corrupt_payload_read = 1;
  fails += store_expect(
      boot_slot_store_stage_inactive_authorized(
          &store, 0u, &snapshot, 1u, &image, payload, sizeof(payload),
          &valid_generation) == BOOT_SLOT_STORE_ERR_VERIFY &&
          boot_slot_manager_get(&manager) == 0 &&
          manager.slots[1].state == BOOT_SLOT_FAILED &&
          store_find(&mem, layout.slots[1].header_lba, 0) == NULL,
      "payload mismatch leaves manager invalid and header absent");

  fails += store_expect(store_prepare(&store, &layout, &mem, &snapshot) == 0,
                        "prepare header uncertainty case");
  mem.corrupt_header_read = 1;
  fails += store_expect(
      boot_slot_store_stage_inactive_authorized(
          &store, 0u, &snapshot, 1u, &image, payload, sizeof(payload),
          &valid_generation) == BOOT_SLOT_STORE_ERR_COMMIT_UNKNOWN &&
          boot_slot_manager_get(&manager) == 0 &&
          manager.slots[1].state == BOOT_SLOT_FAILED,
      "header uncertainty never publishes valid manager");

  fails += store_expect(store_prepare(&store, &layout, &mem, &snapshot) == 0,
                        "prepare control flush failure case");
  mem.fail_flush = 1;
  fails += store_expect(
      boot_slot_store_stage_inactive_authorized(
          &store, 0u, &snapshot, 1u, &image, payload, sizeof(payload),
          &valid_generation) == BOOT_SLOT_STORE_ERR_COMMIT_UNKNOWN &&
          !boot_slot_persistence_ready() && mem.event_count == 2u &&
          store_find(&mem, layout.slots[1].payload_lba, 0) == NULL,
      "control flush failure precedes every payload write");

  if (fails == 0)
    printf("[test_boot_slot_store] all passed\n");
  return fails;
}
