#include <stdio.h>
#include <string.h>
#include "boot/boot_slot.h"
#include "boot/internal/boot_slot_internal.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST_BOOT_SECTORS 524288u

#define TEST(name) \
  do { tests_run++; printf("  %-40s ", name); } while (0)
#define PASS() \
  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(msg) \
  do { printf("FAIL: %s\n", msg); } while (0)

struct fake_boot_slot_store {
  uint8_t copies[BOOT_SLOT_PERSIST_COPY_COUNT][BOOT_SLOT_PERSIST_RECORD_SIZE];
  int present[BOOT_SLOT_PERSIST_COPY_COUNT];
  uint32_t fail_read_mask;
  int fail_write;
  int write_then_error;
  int fail_flush;
  int corrupt_write;
  uint32_t write_count;
};

static void fake_store_reset(struct fake_boot_slot_store *store) {
  memset(store, 0, sizeof(*store));
}

static void fake_put_u32(uint8_t *dst, uint32_t value) {
  dst[0] = (uint8_t)value;
  dst[1] = (uint8_t)(value >> 8);
  dst[2] = (uint8_t)(value >> 16);
  dst[3] = (uint8_t)(value >> 24);
}

static uint32_t fake_crc32(const uint8_t *data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0u; i < len; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^
            (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
  }
  return ~crc;
}

static void fake_store_set_generation(struct fake_boot_slot_store *store,
                                      uint32_t copy_index,
                                      uint64_t generation) {
  uint8_t *record = store->copies[copy_index];
  fake_put_u32(record + 16u, (uint32_t)generation);
  fake_put_u32(record + 20u, (uint32_t)(generation >> 32));
  fake_put_u32(record + 508u, fake_crc32(record, 508u));
}

static int fake_store_read(void *ctx, uint32_t copy_index, uint8_t *record,
                           size_t record_size) {
  struct fake_boot_slot_store *store = ctx;
  if (!store || !record || copy_index >= BOOT_SLOT_PERSIST_COPY_COUNT ||
      record_size != BOOT_SLOT_PERSIST_RECORD_SIZE ||
      (store->fail_read_mask & (1u << copy_index)) != 0u)
    return BOOT_SLOT_ERR_IO;
  if (!store->present[copy_index])
    return BOOT_SLOT_PERSIST_EMPTY;
  memcpy(record, store->copies[copy_index], record_size);
  return 0;
}

static int fake_store_write(void *ctx, uint32_t copy_index,
                            const uint8_t *record, size_t record_size) {
  struct fake_boot_slot_store *store = ctx;
  if (!store || !record || copy_index >= BOOT_SLOT_PERSIST_COPY_COUNT ||
      record_size != BOOT_SLOT_PERSIST_RECORD_SIZE || store->fail_write)
    return -1;
  store->write_count++;
  memcpy(store->copies[copy_index], record, record_size);
  store->present[copy_index] = 1;
  if (store->corrupt_write)
    store->copies[copy_index][0] ^= 1u;
  return store->write_then_error ? BOOT_SLOT_ERR_IO : 0;
}

static int fake_store_flush(void *ctx) {
  struct fake_boot_slot_store *store = ctx;
  return store && !store->fail_flush ? 0 : -1;
}

static void fake_image_init(struct boot_slot_image *image, const char *version,
                            uint32_t payload_size, uint8_t digest_seed) {
  memset(image, 0, sizeof(*image));
  strncpy(image->version, version, sizeof(image->version) - 1u);
  image->payload_size = payload_size;
  for (size_t i = 0u; i < sizeof(image->payload_sha256); ++i)
    image->payload_sha256[i] = (uint8_t)(digest_seed + (uint8_t)i);
}

static int fake_stage_existing_image(uint32_t slot,
                                     const struct boot_slot_image *image) {
  return boot_slot_test_publish_metadata(slot, image);
}

static int fake_stage_image(uint32_t slot, const char *version,
                            uint32_t payload_size, uint8_t digest_seed) {
  struct boot_slot_image image;
  fake_image_init(&image, version, payload_size, digest_seed);
  return fake_stage_existing_image(slot, &image);
}

static int fake_store_open(struct fake_boot_slot_store *store) {
  struct boot_slot_layout layout;
  boot_slot_init();
  if (boot_slot_layout_plan(TEST_BOOT_SECTORS, &layout) != 0)
    return BOOT_SLOT_ERR_IO;
  return boot_slot_set_persistence(fake_store_read, fake_store_write,
                                   fake_store_flush, store, &layout);
}

static int fake_store_seed(struct fake_boot_slot_store *store) {
  struct boot_slot_layout layout;
  struct boot_slot_image image;
  if (fake_store_open(store) != BOOT_SLOT_PERSIST_EMPTY ||
      boot_slot_layout_plan(TEST_BOOT_SECTORS, &layout) != 0)
    return -1;
  fake_image_init(&image, "1.0.0", 4096u, 0x10u);
  return boot_slot_initialize_persistent(&layout, &image);
}

void test_boot_slot_init_and_get(void) {
  boot_slot_init();

  TEST("boot_slot_get_active returns slot A");
  struct boot_slot s;
  if (boot_slot_get_active(&s) == 0 && s.state == BOOT_SLOT_EMPTY) { PASS(); }
  else { FAIL("bad initial state"); }

  TEST("boot_slot_get both slots");
  struct boot_slot a, b;
  if (boot_slot_get(0, &a) == 0 && boot_slot_get(1, &b) == 0 &&
      a.state == BOOT_SLOT_EMPTY && b.state == BOOT_SLOT_EMPTY) { PASS(); }
  else { FAIL("slots not empty"); }
}

void test_boot_slot_stage_activate(void) {
  boot_slot_init();

  TEST("boot_slot_stage sets version");
  if (boot_slot_stage(0, "0.9.0", 0xDEAD) == 0) {
    struct boot_slot s;
    boot_slot_get(0, &s);
    if (s.state == BOOT_SLOT_VALID && strcmp(s.version, "0.9.0") == 0) { PASS(); }
    else { FAIL("stage did not set correctly"); }
  } else { FAIL("stage returned error"); }

  TEST("boot_slot_activate changes state");
  if (boot_slot_activate(0) == 0) {
    struct boot_slot s;
    boot_slot_get(0, &s);
    if (s.state == BOOT_SLOT_ACTIVE && s.boot_count == 1) { PASS(); }
    else { FAIL("activate did not work"); }
  } else { FAIL("activate returned error"); }

  TEST("active RAM slot cannot be restaged");
  {
    struct boot_slot before;
    struct boot_slot after;
    boot_slot_get(0u, &before);
    if (boot_slot_stage(0u, "1.0.0", 0u) == BOOT_SLOT_ERR_IO &&
        boot_slot_get(0u, &after) == 0 &&
        memcmp(&before, &after, sizeof(before)) == 0) { PASS(); }
    else { FAIL("active RAM slot was mutated"); }
  }
}

void test_boot_slot_health_rollback(void) {
  boot_slot_init();
  boot_slot_stage(0, "0.9.0", 0xAA);
  boot_slot_stage(1, "0.8.0", 0xBB);
  boot_slot_activate(0);

  TEST("boot_slot_needs_rollback after activate");
  if (boot_slot_needs_rollback() == 1) { PASS(); }
  else { FAIL("should need rollback"); }

  TEST("boot_slot_confirm_health clears rollback");
  boot_slot_confirm_health();
  if (boot_slot_needs_rollback() == 0) { PASS(); }
  else { FAIL("should not need rollback after confirm"); }

  /* Reset and test rollback */
  boot_slot_init();
  boot_slot_stage(0, "0.9.0", 0xAA);
  boot_slot_stage(1, "0.8.0", 0xBB);
  boot_slot_activate(0);

  TEST("boot_slot_rollback switches to slot B");
  if (boot_slot_rollback() == 0) {
    struct boot_slot s;
    boot_slot_get_active(&s);
    if (strcmp(s.version, "0.8.0") == 0 && s.state == BOOT_SLOT_ACTIVE) { PASS(); }
    else { FAIL("rollback did not switch"); }
  } else { FAIL("rollback failed"); }
}

void test_boot_slot_persistence_reboot(void) {
  struct fake_boot_slot_store store;
  struct boot_slot slot;
  fake_store_reset(&store);

  TEST("persistent format creates mirrors");
  if (fake_store_seed(&store) == 0 && boot_slot_persistence_ready() &&
      boot_slot_persistence_generation() == 2u && store.present[0] &&
      store.present[1] && boot_slot_get_active(&slot) == 0 &&
      strcmp(slot.version, "1.0.0") == 0 &&
      slot.state == BOOT_SLOT_ACTIVE && slot.health_confirmed) { PASS(); }
  else { FAIL("persistent format invalid"); }

  TEST("persistent activation survives reboot");
  if (fake_stage_image(1u, "2.0.0", 8192u, 0x30u) == 0 &&
      boot_slot_activate(1u) == 0 && boot_slot_needs_rollback() == 1 &&
      boot_slot_persistence_generation() == 6u) {
    if (fake_store_open(&store) == 0 &&
        boot_slot_get_active(&slot) == 0 &&
        strcmp(slot.version, "2.0.0") == 0 &&
        slot.state == BOOT_SLOT_ACTIVE && !slot.health_confirmed &&
        boot_slot_needs_rollback() == 1 &&
        boot_slot_persistence_generation() == 6u) { PASS(); }
    else { FAIL("activation not restored"); }
  } else { FAIL("activation did not persist"); }

  TEST("persistent rollback survives reboot");
  if (boot_slot_rollback() == 0 && boot_slot_needs_rollback() == 0 &&
      boot_slot_persistence_generation() == 7u) {
    if (fake_store_open(&store) == 0 &&
        boot_slot_get_active(&slot) == 0 &&
        strcmp(slot.version, "1.0.0") == 0 &&
        slot.state == BOOT_SLOT_ACTIVE && slot.health_confirmed &&
        boot_slot_needs_rollback() == 0) { PASS(); }
    else { FAIL("rollback not restored"); }
  } else { FAIL("rollback did not persist"); }
}

void test_boot_slot_persistence_recovery(void) {
  struct fake_boot_slot_store store;
  struct boot_slot_layout layout;
  struct boot_slot_image image;
  struct boot_slot slot;
  boot_slot_layout_plan(TEST_BOOT_SECTORS, &layout);
  fake_image_init(&image, "1.0.0", 4096u, 0x10u);
  fake_store_reset(&store);
  fake_store_seed(&store);

  TEST("corrupt newest mirror falls back");
  if (fake_stage_image(1u, "2.0.0", 8192u, 0x40u) == 0) {
    store.copies[0][0] ^= 1u;
    if (fake_store_open(&store) == 0 &&
        boot_slot_persistence_generation() == 4u &&
        boot_slot_get(1u, &slot) == 0 && slot.state == BOOT_SLOT_FAILED) { PASS(); }
    else { FAIL("did not use older valid mirror"); }
  } else { FAIL("could not create newer mirror"); }

  fake_store_reset(&store);
  fake_store_seed(&store);
  store.fail_write = 1;
  TEST("write error is outcome unknown");
  if (fake_stage_image(1u, "2.0.0", 8192u, 0x40u) ==
          BOOT_SLOT_ERR_COMMIT_UNKNOWN &&
      !boot_slot_persistence_ready() &&
      boot_slot_get(1u, &slot) != 0 &&
      boot_slot_confirm_health() == BOOT_SLOT_ERR_IO) { PASS(); }
  else { FAIL("write error outcome mismatch"); }

  fake_store_reset(&store);
  fake_store_seed(&store);
  store.write_then_error = 1;
  TEST("write-then-error resolves on reload");
  if (fake_stage_image(1u, "2.0.0", 8192u, 0x40u) ==
          BOOT_SLOT_ERR_COMMIT_UNKNOWN &&
      !boot_slot_persistence_ready()) {
    store.write_then_error = 0;
    if (fake_store_open(&store) == 0 &&
        boot_slot_persistence_generation() == 3u &&
        boot_slot_get(1u, &slot) == 0 && slot.state == BOOT_SLOT_FAILED) { PASS(); }
    else { FAIL("written record not recovered"); }
  } else { FAIL("write-then-error not classified unknown"); }

  fake_store_reset(&store);
  fake_store_seed(&store);
  store.corrupt_write = 1;
  TEST("corrupt readback is outcome unknown");
  if (fake_stage_image(1u, "2.0.0", 8192u, 0x40u) ==
          BOOT_SLOT_ERR_COMMIT_UNKNOWN &&
      !boot_slot_persistence_ready() &&
      boot_slot_get(1u, &slot) != 0) {
    store.corrupt_write = 0;
    if (fake_store_open(&store) == 0 &&
        boot_slot_persistence_generation() == 2u) { PASS(); }
    else { FAIL("old mirror not recoverable"); }
  } else { FAIL("corrupt readback outcome mismatch"); }

  fake_store_reset(&store);
  fake_store_seed(&store);
  store.fail_flush = 1;
  TEST("flush failure is outcome unknown");
  if (fake_stage_image(1u, "2.0.0", 8192u, 0x40u) ==
          BOOT_SLOT_ERR_COMMIT_UNKNOWN &&
      !boot_slot_persistence_ready() &&
      boot_slot_get(1u, &slot) != 0) {
    store.fail_flush = 0;
    if (fake_store_open(&store) == 0 &&
        boot_slot_persistence_generation() == 3u &&
        boot_slot_get(1u, &slot) == 0 && slot.state == BOOT_SLOT_FAILED) { PASS(); }
    else { FAIL("uncertain write not resolved on reload"); }
  } else { FAIL("flush failure outcome mismatch"); }

  fake_store_reset(&store);
  fake_store_seed(&store);
  fake_store_set_generation(&store, 1u, 1u);
  fake_put_u32(store.copies[1] + 96u, 0xAABBCCDDu);
  fake_put_u32(store.copies[1] + 508u,
               fake_crc32(store.copies[1], 508u));
  boot_slot_init();
  TEST("equal generation divergence fails closed");
  if (boot_slot_set_persistence(fake_store_read, fake_store_write,
                                fake_store_flush, &store, &layout) == BOOT_SLOT_ERR_IO &&
      !boot_slot_persistence_ready()) { PASS(); }
  else { FAIL("split-brain records were accepted"); }

  boot_slot_init();
  TEST("configured blank store blocks RAM mutation");
  fake_store_reset(&store);
  if (boot_slot_set_persistence(fake_store_read, fake_store_write,
                                fake_store_flush, &store, &layout) ==
          BOOT_SLOT_PERSIST_EMPTY &&
      !boot_slot_persistence_ready() &&
      boot_slot_stage(0u, "2.0.0", 0u) != 0 &&
      boot_slot_get(0u, &slot) != 0) { PASS(); }
  else { FAIL("blank persistent store allowed mutation"); }

  boot_slot_init();
  TEST("zero-filled records count as blank");
  fake_store_reset(&store);
  store.present[0] = 1;
  store.present[1] = 1;
  if (boot_slot_set_persistence(fake_store_read, fake_store_write,
                                fake_store_flush, &store, &layout) ==
          BOOT_SLOT_PERSIST_EMPTY &&
      boot_slot_initialize_persistent(&layout, &image) == 0 &&
      boot_slot_persistence_ready()) { PASS(); }
  else { FAIL("zero-filled records not recognized as blank"); }

  boot_slot_init();
  TEST("read failure cannot initialize store");
  fake_store_reset(&store);
  store.fail_read_mask = 3u;
  if (boot_slot_set_persistence(fake_store_read, fake_store_write,
                                fake_store_flush, &store, &layout) == BOOT_SLOT_ERR_IO &&
      boot_slot_initialize_persistent(&layout, &image) == BOOT_SLOT_ERR_IO &&
      !boot_slot_persistence_ready()) { PASS(); }
  else { FAIL("read failure treated as blank media"); }

  fake_store_reset(&store);
  fake_store_seed(&store);
  boot_slot_init();
  store.fail_read_mask = 2u;
  TEST("valid plus unreadable mirror fails closed");
  if (boot_slot_set_persistence(fake_store_read, fake_store_write,
                                fake_store_flush, &store, &layout) == BOOT_SLOT_ERR_IO &&
      !boot_slot_persistence_ready()) { PASS(); }
  else { FAIL("stale valid mirror accepted over I/O error"); }

  fake_store_reset(&store);
  fake_store_seed(&store);
  store.present[1] = 0;
  boot_slot_init();
  TEST("valid plus empty mirror is recoverable");
  {
    if (boot_slot_set_persistence(fake_store_read, fake_store_write,
                                  fake_store_flush, &store, &layout) == 0 &&
        boot_slot_persistence_ready() && bsp.degraded &&
        boot_slot_persistence_generation() == 1u &&
        boot_slot_repair_mirror() == 0 && !bsp.degraded &&
        boot_slot_persistence_generation() == 2u) { PASS(); }
    else { FAIL("valid mirror not recovered and repaired over empty peer"); }
  }

  fake_store_reset(&store);
  fake_store_seed(&store);
  fake_stage_image(1u, "2.0.0", 8192u, 0x50u);
  boot_slot_activate(1u);
  TEST("stage during rollback pending is rejected");
  if (fake_stage_image(0u, "3.0.0", 8192u, 0x60u) == BOOT_SLOT_ERR_STALE &&
      boot_slot_persistence_ready() &&
      boot_slot_persistence_generation() == 6u &&
      boot_slot_needs_rollback()) { PASS(); }
  else { FAIL("invalid pending transition reached storage"); }
}

void test_boot_slot_layout_identity_attempts(void) {
  struct fake_boot_slot_store store;
  struct boot_slot_layout layout;
  struct boot_slot_image image;
  struct boot_slot_manager manager;
  struct boot_slot slot;
  uint32_t selected = BOOT_SLOT_NONE;
  uint64_t token = 0u;

  TEST("official BOOT layout plans A/B regions");
  if (boot_slot_layout_plan(TEST_BOOT_SECTORS, &layout) == 0 &&
      layout.boot_sectors == TEST_BOOT_SECTORS &&
      layout.slots[0].header_lba == 0u &&
      layout.slots[0].payload_lba == 1u &&
      layout.slots[0].payload_capacity_sectors == 262142u &&
      layout.slots[1].header_lba == 262143u &&
      layout.slots[1].payload_lba == 262144u &&
      layout.slots[1].payload_capacity_sectors == 262142u &&
      layout.control_lba[0] == 524286u &&
      layout.control_lba[1] == 524287u) { PASS(); }
  else { FAIL("official layout mismatch"); }

  TEST("undersized BOOT layout fails closed");
  memset(&layout, 0xA5, sizeof(layout));
  if (boot_slot_layout_plan(5u, &layout) == BOOT_SLOT_ERR_IO &&
      layout.boot_sectors == 0u) { PASS(); }
  else { FAIL("undersized layout accepted"); }

  fake_store_reset(&store);
  boot_slot_layout_plan(TEST_BOOT_SECTORS, &layout);
  boot_slot_init();
  boot_slot_set_persistence(fake_store_read, fake_store_write,
                            fake_store_flush, &store, &layout);
  fake_image_init(&image, "1.0.0", 4096u, 0x20u);
  TEST("persistent seed binds geometry and sha256");
  if (boot_slot_initialize_persistent(&layout, &image) == 0 &&
      fake_store_open(&store) == 0 &&
      boot_slot_get(0u, &slot) == 0 && slot.header_lba == 0u &&
      slot.payload_lba == 1u &&
      slot.payload_capacity_sectors == 262142u &&
      slot.payload_size == 4096u &&
      memcmp(slot.payload_sha256, image.payload_sha256,
             BOOT_SLOT_SHA256_SIZE) == 0 &&
      boot_slot_get(1u, &slot) == 0 && slot.state == BOOT_SLOT_EMPTY &&
      slot.header_lba == 262143u && slot.payload_lba == 262144u) { PASS(); }
  else { FAIL("persistent seed identity mismatch"); }

  {
    struct boot_slot_layout wrong_layout;
    boot_slot_layout_plan(TEST_BOOT_SECTORS + 2u, &wrong_layout);
    boot_slot_init();
    TEST("reopen rejects mismatched BOOT geometry");
    if (boot_slot_set_persistence(fake_store_read, fake_store_write,
                                  fake_store_flush, &store,
                                  &wrong_layout) == BOOT_SLOT_ERR_IO &&
        fake_store_open(&store) == 0) { PASS(); }
    else { FAIL("record accepted under wrong geometry"); }
  }

  fake_image_init(&image, "2.0.0", 8192u, 0x40u);
  TEST("pending attempt persists explicit state");
  if (fake_stage_existing_image(1u, &image) == 0 &&
      boot_slot_activate(1u) == 0 &&
      boot_slot_manager_get(&manager) == 0 &&
      manager.confirmed_slot == 0u && manager.pending_slot == 1u &&
      manager.tries_remaining == 1u &&
      boot_slot_select_for_boot(&selected, &token) == 1 && selected == 1u &&
      token == boot_slot_persistence_generation() &&
      boot_slot_manager_get(&manager) == 0 && manager.tries_remaining == 0u) {
    if (fake_store_open(&store) == 0 &&
        boot_slot_manager_get(&manager) == 0 && manager.pending_slot == 1u &&
        manager.tries_remaining == 0u) { PASS(); }
    else { FAIL("attempt state not restored"); }
  } else { FAIL("pending attempt state mismatch"); }

  TEST("health confirmation binds slot and token");
  if (boot_slot_confirm_health_verified(1u, token - 1u, 0u, 0u) ==
          BOOT_SLOT_ERR_IO &&
      boot_slot_confirm_health_verified(0u, token, 0u, 0u) == BOOT_SLOT_ERR_IO &&
      boot_slot_confirm_health_verified(1u, token, 1u, 0u) == BOOT_SLOT_ERR_IO &&
      boot_slot_confirm_health_verified(1u, token, 0u, 1u) == BOOT_SLOT_ERR_IO &&
      boot_slot_confirm_health_verified(1u, token, 0u, 0u) == 0 &&
      boot_slot_manager_get(&manager) == 0 && manager.confirmed_slot == 1u &&
      manager.pending_slot == BOOT_SLOT_NONE && manager.tries_remaining == 0u &&
      boot_slot_get_active(&slot) == 0 && slot.health_confirmed) { PASS(); }
  else { FAIL("health token binding mismatch"); }

  fake_store_reset(&store);
  fake_store_seed(&store);
  fake_image_init(&image, "2.0.0", 8192u, 0x60u);
  fake_stage_existing_image(1u, &image);
  boot_slot_activate(1u);
  boot_slot_select_for_boot(&selected, &token);
  boot_slot_init();
  boot_slot_set_persistence(fake_store_read, fake_store_write,
                            fake_store_flush, &store, &layout);
  TEST("exhausted attempt rolls back before boot");
  if (boot_slot_select_for_boot(&selected, &token) == 2 && selected == 0u &&
      boot_slot_manager_get(&manager) == 0 && manager.confirmed_slot == 0u &&
      manager.pending_slot == BOOT_SLOT_NONE &&
      boot_slot_get(1u, &slot) == 0 && slot.state == BOOT_SLOT_FAILED) { PASS(); }
  else { FAIL("attempt exhaustion did not roll back"); }

  fake_store_reset(&store);
  fake_store_seed(&store);
  fake_image_init(&image, "2.0.0", 8192u, 0x68u);
  fake_stage_existing_image(1u, &image);
  boot_slot_activate(1u);
  boot_slot_select_for_boot(&selected, &token);
  store.copies[0][0] ^= 1u;
  TEST("degraded pending mirror fails closed");
  if (fake_store_open(&store) == BOOT_SLOT_ERR_IO &&
      !boot_slot_persistence_ready()) { PASS(); }
  else { FAIL("pending try restored from stale mirror"); }

  fake_store_reset(&store);
  fake_store_seed(&store);
  fake_image_init(&image, "2.0.0", UINT32_MAX, 0x70u);
  TEST("oversized payload identity is rejected");
  if (fake_stage_existing_image(1u, &image) == BOOT_SLOT_ERR_STALE &&
      boot_slot_persistence_generation() == 2u &&
      boot_slot_stage(1u, "2.0.0", 0u) == BOOT_SLOT_ERR_IO) { PASS(); }
  else { FAIL("unverified payload identity accepted"); }

  fake_image_init(&image, "2.0.0", 4096u, 0u);
  memset(image.payload_sha256, 0, sizeof(image.payload_sha256));
  TEST("missing sha256 is rejected before write");
  if (fake_stage_existing_image(1u, &image) == BOOT_SLOT_ERR_STALE &&
      store.write_count == 2u && boot_slot_persistence_ready()) { PASS(); }
  else { FAIL("missing sha256 reached storage"); }

  fake_image_init(&image, "2.0\n0", 4096u, 0x80u);
  TEST("non-printable version is rejected before write");
  if (fake_stage_existing_image(1u, &image) == BOOT_SLOT_ERR_STALE &&
      store.write_count == 2u && boot_slot_persistence_ready()) { PASS(); }
  else { FAIL("non-printable version reached storage"); }
}

int test_boot_slot_run(void) {
  printf("[test_boot_slot]\n");
  tests_run = 0;
  tests_passed = 0;
  test_boot_slot_init_and_get();
  test_boot_slot_stage_activate();
  test_boot_slot_health_rollback();
  test_boot_slot_persistence_reboot();
  test_boot_slot_persistence_recovery();
  test_boot_slot_layout_identity_attempts();
  boot_slot_init();
  printf("  %d/%d passed\n", tests_passed, tests_run);
  return tests_run - tests_passed;
}
