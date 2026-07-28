#include "boot/boot_slot_store.h"

#include "internal/boot_slot_internal.h"
#include "security/sha256.h"

static const uint8_t store_magic[8] = {'C', 'A', 'P', 'Y',
                                        'S', 'L', 'T', '0'};

#define STORE_HEADER_VERSION 0u
#define STORE_HEADER_CRC_OFFSET 508u

struct boot_slot_store_binding {
  struct boot_slot_store *owner;
  struct boot_slot_layout layout;
  boot_slot_store_read_fn reader;
  boot_slot_store_write_fn writer;
  boot_slot_store_flush_fn flusher;
  void *ctx;
  uint64_t epoch;
  uint64_t lease_epoch;
  int provider_backed;
  int active;
};

struct boot_slot_store_registration {
  struct boot_slot_store *owner;
  struct boot_slot_layout layout;
  boot_slot_store_read_fn reader;
  boot_slot_store_write_fn writer;
  boot_slot_store_flush_fn flusher;
  void *ctx;
  uint64_t lease_epoch;
  int active;
};

struct boot_slot_store_io {
  struct boot_slot_layout layout;
  boot_slot_store_read_fn reader;
  boot_slot_store_write_fn writer;
  boot_slot_store_flush_fn flusher;
  void *ctx;
  uint64_t lease_epoch;
  int provider_backed;
  int ready;
};

struct boot_slot_stage_authorization {
  uint64_t generation;
  uint64_t authority_epoch;
  uint64_t lease_epoch;
  uint32_t slot;
};

static struct boot_slot_store_binding store_binding;
static struct boot_slot_store_registration store_registration;
static struct boot_slot_store *store_lease_owner;
static uint64_t store_next_epoch = 1u;

static void store_zero(void *ptr, size_t len) {
  uint8_t *bytes = ptr;
  if (!bytes)
    return;
  for (size_t i = 0u; i < len; ++i)
    bytes[i] = 0u;
}

static void store_copy(uint8_t *dst, const uint8_t *src, size_t len) {
  if (!dst || !src)
    return;
  for (size_t i = 0u; i < len; ++i)
    dst[i] = src[i];
}

static int store_equal(const uint8_t *a, const uint8_t *b, size_t len) {
  uint8_t diff = 0u;
  if (!a || !b)
    return 0;
  for (size_t i = 0u; i < len; ++i)
    diff |= (uint8_t)(a[i] ^ b[i]);
  return diff == 0u;
}

static int store_string_equal(const char *a, const char *b) {
  size_t i = 0u;
  if (!a || !b)
    return 0;
  while (i < BOOT_SLOT_VERSION_MAX && a[i] && b[i]) {
    if (a[i] != b[i])
      return 0;
    ++i;
  }
  return i < BOOT_SLOT_VERSION_MAX && a[i] == b[i];
}

static int store_zero_range(const uint8_t *data, size_t start, size_t end) {
  if (!data || start > end || end > BOOT_SLOT_STORE_SECTOR_SIZE)
    return 0;
  for (size_t i = start; i < end; ++i) {
    if (data[i] != 0u)
      return 0;
  }
  return 1;
}

static void store_put_u32(uint8_t *dst, uint32_t value) {
  dst[0] = (uint8_t)value;
  dst[1] = (uint8_t)(value >> 8);
  dst[2] = (uint8_t)(value >> 16);
  dst[3] = (uint8_t)(value >> 24);
}

static uint32_t store_get_u32(const uint8_t *src) {
  return (uint32_t)src[0] | ((uint32_t)src[1] << 8) |
         ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

static uint32_t store_crc32(const uint8_t *data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0u; i < len; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^
            (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
  }
  return ~crc;
}

static int store_layout_equal(const struct boot_slot_layout *a,
                              const struct boot_slot_layout *b) {
  if (!a || !b || a->boot_sectors != b->boot_sectors ||
      a->control_lba[0] != b->control_lba[0] ||
      a->control_lba[1] != b->control_lba[1])
    return 0;
  for (uint32_t i = 0u; i < BOOT_SLOT_COUNT; ++i) {
    if (a->slots[i].header_lba != b->slots[i].header_lba ||
        a->slots[i].payload_lba != b->slots[i].payload_lba ||
        a->slots[i].payload_capacity_sectors !=
            b->slots[i].payload_capacity_sectors)
      return 0;
  }
  return 1;
}

static int store_manager_matches(const struct boot_slot_manager *manager,
                                 const struct boot_slot_layout *layout) {
  if (!manager || !layout || !boot_slot_manager_validate(manager) ||
      manager->pending_slot != BOOT_SLOT_NONE ||
      manager->active_slot != manager->confirmed_slot ||
      manager->boot_sectors != layout->boot_sectors ||
      manager->control_lba[0] != layout->control_lba[0] ||
      manager->control_lba[1] != layout->control_lba[1])
    return 0;
  for (uint32_t i = 0u; i < BOOT_SLOT_COUNT; ++i) {
    if (manager->slots[i].header_lba != layout->slots[i].header_lba ||
        manager->slots[i].payload_lba != layout->slots[i].payload_lba ||
        manager->slots[i].payload_capacity_sectors !=
            layout->slots[i].payload_capacity_sectors)
      return 0;
  }
  return 1;
}

static int store_version_valid(const char *version) {
  size_t len = 0u;
  if (!version)
    return 0;
  while (len < BOOT_SLOT_VERSION_MAX && version[len]) {
    uint8_t ch = (uint8_t)version[len];
    if (ch < 0x20u || ch > 0x7Eu)
      return 0;
    ++len;
  }
  if (len == 0u || len >= BOOT_SLOT_VERSION_MAX)
    return 0;
  for (size_t i = len + 1u; i < BOOT_SLOT_VERSION_MAX; ++i) {
    if (version[i] != '\0')
      return 0;
  }
  return 1;
}

static int store_digest_present(const uint8_t *digest) {
  uint8_t any = 0u;
  if (!digest)
    return 0;
  for (size_t i = 0u; i < BOOT_SLOT_SHA256_SIZE; ++i)
    any |= digest[i];
  return any != 0u;
}

static int store_image_valid(const struct boot_slot_image *image,
                             uint32_t capacity_sectors) {
  uint64_t capacity = (uint64_t)capacity_sectors *
                      BOOT_SLOT_STORE_SECTOR_SIZE;
  return image && store_version_valid(image->version) &&
         image->payload_size > 0u &&
         (uint64_t)image->payload_size <= capacity &&
         store_digest_present(image->payload_sha256);
}

static void store_encode_header(const struct boot_slot_store_io *store,
                                uint32_t slot,
                                const struct boot_slot_image *image,
                                uint8_t header[BOOT_SLOT_STORE_SECTOR_SIZE]) {
  size_t version_len = 0u;
  store_zero(header, BOOT_SLOT_STORE_SECTOR_SIZE);
  store_copy(header, store_magic, sizeof(store_magic));
  store_put_u32(header + 8u, STORE_HEADER_VERSION);
  store_put_u32(header + 12u, BOOT_SLOT_STORE_SECTOR_SIZE);
  store_put_u32(header + 16u, slot);
  store_put_u32(header + 20u, store->layout.boot_sectors);
  store_put_u32(header + 24u, store->layout.slots[slot].header_lba);
  store_put_u32(header + 28u, store->layout.slots[slot].payload_lba);
  store_put_u32(header + 32u,
                store->layout.slots[slot].payload_capacity_sectors);
  store_put_u32(header + 36u, image->payload_size);
  while (version_len + 1u < BOOT_SLOT_VERSION_MAX &&
         image->version[version_len]) {
    header[40u + version_len] = (uint8_t)image->version[version_len];
    ++version_len;
  }
  store_copy(header + 80u, image->payload_sha256, BOOT_SLOT_SHA256_SIZE);
  store_put_u32(header + STORE_HEADER_CRC_OFFSET,
                store_crc32(header, STORE_HEADER_CRC_OFFSET));
}

static int store_decode_header(const struct boot_slot_store_io *store,
                                uint32_t slot, const uint8_t *header,
                                struct boot_slot_image *out) {
  size_t version_len = 0u;
  if (!store || !header || !out || slot >= BOOT_SLOT_COUNT ||
      !store_equal(header, store_magic, sizeof(store_magic)) ||
      store_get_u32(header + 8u) != STORE_HEADER_VERSION ||
      store_get_u32(header + 12u) != BOOT_SLOT_STORE_SECTOR_SIZE ||
      store_get_u32(header + 16u) != slot ||
      store_get_u32(header + 20u) != store->layout.boot_sectors ||
      store_get_u32(header + 24u) != store->layout.slots[slot].header_lba ||
      store_get_u32(header + 28u) != store->layout.slots[slot].payload_lba ||
      store_get_u32(header + 32u) !=
          store->layout.slots[slot].payload_capacity_sectors ||
      store_crc32(header, STORE_HEADER_CRC_OFFSET) !=
          store_get_u32(header + STORE_HEADER_CRC_OFFSET) ||
      !store_zero_range(header, 112u, STORE_HEADER_CRC_OFFSET))
    return BOOT_SLOT_STORE_ERR_VERIFY;
  store_zero(out, sizeof(*out));
  while (version_len < BOOT_SLOT_VERSION_MAX && header[40u + version_len]) {
    uint8_t ch = header[40u + version_len];
    if (ch < 0x20u || ch > 0x7Eu)
      return BOOT_SLOT_STORE_ERR_VERIFY;
    out->version[version_len] = (char)ch;
    ++version_len;
  }
  if (version_len == 0u || version_len >= BOOT_SLOT_VERSION_MAX ||
      !store_zero_range(header, 40u + version_len + 1u, 80u))
    return BOOT_SLOT_STORE_ERR_VERIFY;
  out->payload_size = store_get_u32(header + 36u);
  store_copy(out->payload_sha256, header + 80u, BOOT_SLOT_SHA256_SIZE);
  if (!store_image_valid(out,
                         store->layout.slots[slot].payload_capacity_sectors))
    return BOOT_SLOT_STORE_ERR_VERIFY;
  return 0;
}

int boot_slot_store_internal_open_locked(
    struct boot_slot_store *store, const struct boot_slot_layout *layout,
    boot_slot_store_read_fn reader, boot_slot_store_write_fn writer,
    boot_slot_store_flush_fn flusher, void *ctx, uint64_t lease_epoch) {
  struct boot_slot_layout expected;
  if (store_binding.active && store_binding.owner == store && bsp.configured)
    return BOOT_SLOT_STORE_ERR_BUSY;
  if (store_binding.active && store_binding.owner == store)
    store_zero(&store_binding, sizeof(store_binding));
  if (store_registration.active || store_lease_owner)
    return BOOT_SLOT_STORE_ERR_BUSY;
  if (store)
    store_zero(store, sizeof(*store));
  if (!store || !layout || !reader || !writer || !flusher || lease_epoch == 0u ||
      boot_slot_layout_plan(layout->boot_sectors, &expected) != 0 ||
      !store_layout_equal(layout, &expected))
    return BOOT_SLOT_STORE_ERR_IO;
  store_zero(&store_registration, sizeof(store_registration));
  store_registration.owner = store;
  store_registration.layout = *layout;
  store_registration.reader = reader;
  store_registration.writer = writer;
  store_registration.flusher = flusher;
  store_registration.ctx = ctx;
  store_registration.lease_epoch = lease_epoch;
  store_registration.active = 1;
  store_lease_owner = store;
  store->opaque_epoch = lease_epoch;
  return 0;
}

#if defined(UNIT_TEST) || defined(CAPYOS_UEFI_LOADER)
int boot_slot_store_init(struct boot_slot_store *store,
                         const struct boot_slot_layout *layout,
                         boot_slot_store_read_fn reader,
                         boot_slot_store_write_fn writer,
                         boot_slot_store_flush_fn flusher, void *ctx) {
  struct boot_slot_layout expected;
  int rc = boot_slot_internal_operation_begin();
  if (rc != 0)
    return BOOT_SLOT_STORE_ERR_BUSY;
  if (store_binding.active && store_binding.owner == store && bsp.configured) {
    rc = BOOT_SLOT_STORE_ERR_BUSY;
  } else if (!store || store_lease_owner == store || !layout || !reader ||
             !writer || !flusher ||
             boot_slot_layout_plan(layout->boot_sectors, &expected) != 0 ||
             !store_layout_equal(layout, &expected)) {
    rc = BOOT_SLOT_STORE_ERR_IO;
  } else {
    if (store_binding.active && store_binding.owner == store)
      store_zero(&store_binding, sizeof(store_binding));
    store_zero(store, sizeof(*store));
    store->test_layout = *layout;
    store->test_reader = reader;
    store->test_writer = writer;
    store->test_flusher = flusher;
    store->test_ctx = ctx;
    store->test_ready = 1;
    rc = 0;
  }
  boot_slot_internal_operation_end();
  return rc;
}
#endif

int boot_slot_store_internal_revoke_locked(struct boot_slot_store *store,
                                           uint64_t lease_epoch) {
  if (!store || lease_epoch == 0u || store_lease_owner != store ||
      !store_registration.active || store_registration.owner != store ||
      store_registration.lease_epoch != lease_epoch ||
      store->opaque_epoch != lease_epoch || bsp.configured || bsp.stage_claimed)
    return BOOT_SLOT_STORE_ERR_BUSY;
  if (store_binding.active && store_binding.owner == store)
    store_zero(&store_binding, sizeof(store_binding));
  store_zero(&store_registration, sizeof(store_registration));
  store_zero(store, sizeof(*store));
  store_lease_owner = NULL;
  return 0;
}

static int store_io_get_locked(struct boot_slot_store *store,
                               uint64_t expected_lease_epoch,
                               struct boot_slot_store_io *io) {
  if (io)
    store_zero(io, sizeof(*io));
  if (!store || !io)
    return BOOT_SLOT_STORE_ERR_IO;
  if (store_registration.active && store_registration.owner == store) {
    if (expected_lease_epoch == 0u || store_lease_owner != store ||
        store->opaque_epoch != expected_lease_epoch ||
        expected_lease_epoch != store_registration.lease_epoch ||
        bsp.lease_owner != store || bsp.lease_epoch != store->opaque_epoch)
      return BOOT_SLOT_STORE_ERR_IO;
    io->layout = store_registration.layout;
    io->reader = store_registration.reader;
    io->writer = store_registration.writer;
    io->flusher = store_registration.flusher;
    io->ctx = store_registration.ctx;
    io->lease_epoch = store_registration.lease_epoch;
    io->provider_backed = 1;
    io->ready = 1;
    return 0;
  }
#if defined(UNIT_TEST) || defined(CAPYOS_UEFI_LOADER)
  if (expected_lease_epoch == 0u && store->opaque_epoch == 0u &&
      store->test_ready) {
    io->layout = store->test_layout;
    io->reader = store->test_reader;
    io->writer = store->test_writer;
    io->flusher = store->test_flusher;
    io->ctx = store->test_ctx;
    io->ready = 1;
    return 0;
  }
#endif
  return BOOT_SLOT_STORE_ERR_IO;
}

static int store_control_read(void *ctx, uint32_t copy_index, uint8_t *record,
                              size_t record_size) {
  struct boot_slot_store_binding *binding = ctx;
  if (!binding || !binding->active || !record ||
      record_size != BOOT_SLOT_PERSIST_RECORD_SIZE ||
      copy_index >= BOOT_SLOT_PERSIST_COPY_COUNT)
    return BOOT_SLOT_STORE_ERR_IO;
  return binding->reader(binding->ctx, binding->layout.control_lba[copy_index],
                         record);
}

static int store_control_write(void *ctx, uint32_t copy_index,
                               const uint8_t *record, size_t record_size) {
  struct boot_slot_store_binding *binding = ctx;
  if (!binding || !binding->active || !record ||
      record_size != BOOT_SLOT_PERSIST_RECORD_SIZE ||
      copy_index >= BOOT_SLOT_PERSIST_COPY_COUNT)
    return BOOT_SLOT_STORE_ERR_IO;
  return binding->writer(binding->ctx, binding->layout.control_lba[copy_index],
                         record);
}

static int store_control_flush(void *ctx) {
  struct boot_slot_store_binding *binding = ctx;
  return binding && binding->active ? binding->flusher(binding->ctx)
                                    : BOOT_SLOT_STORE_ERR_IO;
}

static int store_binding_matches_io(
    const struct boot_slot_store_binding *binding,
    const struct boot_slot_store_io *io, struct boot_slot_store *owner);

int boot_slot_store_bind_control(struct boot_slot_store *store,
                                 uint64_t expected_lease_epoch) {
  struct boot_slot_store_io io;
  int rc = boot_slot_internal_operation_begin();
  if (rc != 0)
    return BOOT_SLOT_STORE_ERR_BUSY;
  rc = store_io_get_locked(store, expected_lease_epoch, &io);
  if (rc != 0 || !io.ready || bsp.configured || store_next_epoch == 0u ||
      (store_lease_owner && store_lease_owner != store)) {
    boot_slot_internal_operation_end();
    return BOOT_SLOT_STORE_ERR_BUSY;
  }
  store_zero(&store_binding, sizeof(store_binding));
  store_binding.owner = store;
  store_binding.layout = io.layout;
  store_binding.reader = io.reader;
  store_binding.writer = io.writer;
  store_binding.flusher = io.flusher;
  store_binding.ctx = io.ctx;
  store_binding.epoch = store_next_epoch++;
  store_binding.lease_epoch = io.lease_epoch;
  store_binding.provider_backed = io.provider_backed;
  store_binding.active = 1;
  if (!bsm_initialized)
    boot_slot_internal_init_locked();
  rc = boot_slot_internal_set_persistence_locked(
      store_control_read, store_control_write, store_control_flush,
      &store_binding, &store_binding.layout);
  if (rc == 0 || rc == BOOT_SLOT_PERSIST_EMPTY) {
    bsp.authority_epoch = store_binding.epoch;
    boot_slot_phase = rc == 0 ? BOOT_SLOT_PHASE_BOUND_READY
                              : BOOT_SLOT_PHASE_BOUND_EMPTY;
  } else {
    store_binding.active = 0;
    boot_slot_phase = BOOT_SLOT_PHASE_BOUND_UNKNOWN;
  }
  boot_slot_internal_operation_end();
  return rc;
}

int boot_slot_store_initialize_persistent(
    struct boot_slot_store *store, uint64_t expected_lease_epoch,
    const struct boot_slot_image *image) {
  struct boot_slot_store_io io;
  int rc = boot_slot_internal_operation_begin();
  if (rc != 0)
    return BOOT_SLOT_STORE_ERR_BUSY;
  rc = store_io_get_locked(store, expected_lease_epoch, &io);
  if (rc == 0 && !store_binding_matches_io(&store_binding, &io, store))
    rc = BOOT_SLOT_STORE_ERR_STALE;
  if (rc == 0)
    rc = boot_slot_internal_initialize_persistent_locked(&io.layout, image);
  if (rc == 0)
    boot_slot_phase = BOOT_SLOT_PHASE_BOUND_READY;
  boot_slot_internal_operation_end();
  return rc;
}

static int store_slot_equal(const struct boot_slot *a,
                            const struct boot_slot *b) {
  return a && b &&
         store_equal((const uint8_t *)a->name, (const uint8_t *)b->name,
                     sizeof(a->name)) &&
         store_equal((const uint8_t *)a->version, (const uint8_t *)b->version,
                     sizeof(a->version)) &&
         a->state == b->state && a->boot_count == b->boot_count &&
         a->success_count == b->success_count && a->fail_count == b->fail_count &&
         a->header_lba == b->header_lba && a->payload_lba == b->payload_lba &&
         a->payload_capacity_sectors == b->payload_capacity_sectors &&
         a->payload_size == b->payload_size && a->checksum == b->checksum &&
         store_equal(a->payload_sha256, b->payload_sha256,
                     BOOT_SLOT_SHA256_SIZE) &&
         a->health_confirmed == b->health_confirmed;
}

static int store_manager_equal(const struct boot_slot_manager *a,
                               const struct boot_slot_manager *b) {
  if (!a || !b || a->active_slot != b->active_slot ||
      a->next_slot != b->next_slot ||
      a->rollback_pending != b->rollback_pending ||
      a->confirmed_slot != b->confirmed_slot ||
      a->pending_slot != b->pending_slot ||
      a->tries_remaining != b->tries_remaining ||
      a->boot_sectors != b->boot_sectors ||
      a->control_lba[0] != b->control_lba[0] ||
      a->control_lba[1] != b->control_lba[1])
    return 0;
  for (uint32_t i = 0u; i < BOOT_SLOT_COUNT; ++i) {
    if (!store_slot_equal(&a->slots[i], &b->slots[i]))
      return 0;
  }
  return 1;
}

static int store_binding_matches_io(
    const struct boot_slot_store_binding *binding,
    const struct boot_slot_store_io *io, struct boot_slot_store *owner) {
  return binding && io && owner && binding->active && io->ready &&
         binding->owner == owner &&
         store_layout_equal(&binding->layout, &io->layout) &&
         binding->reader == io->reader && binding->writer == io->writer &&
         binding->flusher == io->flusher && binding->ctx == io->ctx &&
         binding->lease_epoch == io->lease_epoch &&
         binding->provider_backed == io->provider_backed;
}

static int store_stage_preflight(const struct boot_slot_store_binding *binding,
                                 uint32_t slot,
                                 const struct boot_slot_image *image,
                                 const uint8_t *payload, size_t payload_len) {
  uint8_t digest[SHA256_DIGEST_SIZE];
  if (!binding || !binding->active || !image || !payload ||
      slot >= BOOT_SLOT_COUNT || payload_len != image->payload_size ||
      !store_image_valid(image,
                         binding->layout.slots[slot].payload_capacity_sectors))
    return BOOT_SLOT_STORE_ERR_IO;
  sha256_hash(payload, payload_len, digest);
  return store_equal(digest, image->payload_sha256, sizeof(digest))
             ? 0
             : BOOT_SLOT_STORE_ERR_VERIFY;
}

static int store_stage_authorize(
    const struct boot_slot_snapshot *expected, uint32_t slot,
    const struct boot_slot_image *image,
    struct boot_slot_stage_authorization *authorization) {
  struct boot_slot_manager candidate;
  uint64_t base_generation;
  uint32_t base_copy;
  int rc;
  if (authorization)
    store_zero(authorization, sizeof(*authorization));
  if (!expected || !authorization || !boot_slot_internal_persistence_ready_locked() ||
      expected->version != BOOT_SLOT_SNAPSHOT_VERSION ||
      expected->size != sizeof(*expected) || bsp.degraded ||
      expected->generation != bsp.generation ||
      expected->authority_epoch != bsp.authority_epoch ||
      expected->authority_epoch != store_binding.epoch ||
      expected->lease_epoch != bsp.lease_epoch ||
      expected->lease_epoch != store_binding.lease_epoch ||
      !store_manager_equal(&expected->manager, &bsm) ||
      !store_binding.active || slot >= BOOT_SLOT_COUNT ||
      slot == bsm.active_slot || slot == bsm.confirmed_slot ||
      bsm.pending_slot != BOOT_SLOT_NONE || bsm.rollback_pending ||
      bsm.slots[slot].state == BOOT_SLOT_ACTIVE ||
      bsm.slots[slot].state == BOOT_SLOT_ROLLBACK ||
      !bs_image_valid(image, bsm.slots[slot].payload_capacity_sectors) ||
      bsp.generation > UINT64_MAX - 3u)
    return BOOT_SLOT_STORE_ERR_STALE;
  if (bsp.stage_claimed)
    return BOOT_SLOT_STORE_ERR_BUSY;
  bsp.stage_claimed = 1;
  base_generation = bsp.generation;
  base_copy = bsp.active_copy;
  candidate = bsm;
  bs_slot_apply_image(&candidate.slots[slot], slot, image, BOOT_SLOT_FAILED);
  candidate.next_slot = candidate.confirmed_slot;
  rc = bs_write_verified(&candidate, base_copy ^ 1u, base_generation + 1u);
  if (rc == 0)
    rc = bs_write_verified(&candidate, base_copy, base_generation + 2u);
  if (rc != 0) {
    if (rc == BOOT_SLOT_ERR_COMMIT_UNKNOWN) {
      bsp.ready = 0;
      boot_slot_phase = BOOT_SLOT_PHASE_BOUND_UNKNOWN;
    }
    bsp.stage_claimed = 0;
    return rc == BOOT_SLOT_ERR_COMMIT_UNKNOWN
               ? BOOT_SLOT_STORE_ERR_COMMIT_UNKNOWN
               : BOOT_SLOT_STORE_ERR_IO;
  }
  bsm = candidate;
  bsp.active_copy = base_copy;
  bsp.generation = base_generation + 2u;
  authorization->generation = bsp.generation;
  authorization->authority_epoch = bsp.authority_epoch;
  authorization->lease_epoch = bsp.lease_epoch;
  authorization->slot = slot;
  return 0;
}

static int store_stage_publish(
    const struct boot_slot_stage_authorization *authorization,
    const struct boot_slot_image *image, uint64_t *out_generation) {
  struct boot_slot_manager candidate;
  uint32_t target_copy;
  uint64_t generation;
  int rc;
  if (out_generation)
    *out_generation = 0u;
  if (!authorization || !image || !out_generation || !bsp.stage_claimed ||
      !boot_slot_internal_persistence_ready_locked() || authorization->slot >= BOOT_SLOT_COUNT ||
      authorization->generation != bsp.generation ||
      authorization->authority_epoch != bsp.authority_epoch ||
      authorization->authority_epoch != store_binding.epoch ||
      authorization->lease_epoch != bsp.lease_epoch ||
      authorization->lease_epoch != store_binding.lease_epoch ||
      !store_binding.active ||
      !store_equal((const uint8_t *)bsm.slots[authorization->slot].version,
                   (const uint8_t *)image->version, BOOT_SLOT_VERSION_MAX) ||
      bsm.slots[authorization->slot].state != BOOT_SLOT_FAILED ||
      bsm.slots[authorization->slot].payload_size != image->payload_size ||
      !store_equal(bsm.slots[authorization->slot].payload_sha256,
                   image->payload_sha256, BOOT_SLOT_SHA256_SIZE) ||
      bsp.generation == UINT64_MAX) {
    bsp.stage_claimed = 0;
    return BOOT_SLOT_STORE_ERR_STALE;
  }
  candidate = bsm;
  bs_slot_apply_image(&candidate.slots[authorization->slot],
                      authorization->slot, image, BOOT_SLOT_VALID);
  candidate.next_slot = authorization->slot;
  target_copy = bsp.active_copy ^ 1u;
  generation = bsp.generation + 1u;
  rc = bs_write_verified(&candidate, target_copy, generation);
  if (rc != 0) {
    if (rc == BOOT_SLOT_ERR_COMMIT_UNKNOWN) {
      bsp.ready = 0;
      boot_slot_phase = BOOT_SLOT_PHASE_BOUND_UNKNOWN;
    }
    bsp.stage_claimed = 0;
    return rc == BOOT_SLOT_ERR_COMMIT_UNKNOWN
               ? BOOT_SLOT_STORE_ERR_COMMIT_UNKNOWN
               : BOOT_SLOT_STORE_ERR_IO;
  }
  bsm = candidate;
  bsp.active_copy = target_copy;
  bsp.generation = generation;
  bsp.stage_claimed = 0;
  *out_generation = generation;
  return 0;
}

static int store_stage_payload(
    const struct boot_slot_store_binding *binding,
    const struct boot_slot_manager *manager, uint32_t slot,
    const struct boot_slot_image *image, const uint8_t *payload,
    size_t payload_len) {
  struct boot_slot_store_io stable_store;
  uint8_t sector[BOOT_SLOT_STORE_SECTOR_SIZE];
  uint8_t expected_digest[SHA256_DIGEST_SIZE];
  uint8_t actual_digest[SHA256_DIGEST_SIZE];
  struct sha256_ctx sha;
  struct boot_slot_image decoded;
  struct boot_slot_store_io *store = &stable_store;
  uint32_t sectors = 0u;
  size_t offset = 0u;
  store_zero(&stable_store, sizeof(stable_store));
  if (binding) {
    stable_store.layout = binding->layout;
    stable_store.reader = binding->reader;
    stable_store.writer = binding->writer;
    stable_store.flusher = binding->flusher;
    stable_store.ctx = binding->ctx;
    stable_store.ready = binding->active;
  }
  if (!binding || !store->ready || !manager || !image ||
      slot >= BOOT_SLOT_COUNT || slot == manager->active_slot ||
      slot == manager->confirmed_slot ||
      (manager->slots[slot].state != BOOT_SLOT_EMPTY &&
       manager->slots[slot].state != BOOT_SLOT_FAILED) ||
      manager->pending_slot != BOOT_SLOT_NONE ||
      !store_manager_matches(manager, &store->layout) || !payload ||
      payload_len != image->payload_size ||
      !store_image_valid(image,
                         store->layout.slots[slot].payload_capacity_sectors))
    return BOOT_SLOT_STORE_ERR_IO;
  sha256_hash(payload, payload_len, expected_digest);
  if (!store_equal(expected_digest, image->payload_sha256,
                   BOOT_SLOT_SHA256_SIZE))
    return BOOT_SLOT_STORE_ERR_VERIFY;
  sectors = image->payload_size / BOOT_SLOT_STORE_SECTOR_SIZE;
  if ((image->payload_size % BOOT_SLOT_STORE_SECTOR_SIZE) != 0u)
    sectors++;
  if (sectors == 0u ||
      sectors > store->layout.slots[slot].payload_capacity_sectors)
    return BOOT_SLOT_STORE_ERR_IO;
  for (uint32_t i = 0u; i < sectors; ++i) {
    size_t chunk = payload_len - offset;
    if (chunk > BOOT_SLOT_STORE_SECTOR_SIZE)
      chunk = BOOT_SLOT_STORE_SECTOR_SIZE;
    store_zero(sector, sizeof(sector));
    store_copy(sector, payload + offset, chunk);
    if (store->writer(store->ctx,
                      store->layout.slots[slot].payload_lba + i,
                      sector) != 0)
      return BOOT_SLOT_STORE_ERR_COMMIT_UNKNOWN;
    offset += chunk;
  }
  if (store->flusher(store->ctx) != 0)
    return BOOT_SLOT_STORE_ERR_COMMIT_UNKNOWN;
  sha256_init(&sha);
  offset = 0u;
  for (uint32_t i = 0u; i < sectors; ++i) {
    size_t chunk = payload_len - offset;
    if (chunk > BOOT_SLOT_STORE_SECTOR_SIZE)
      chunk = BOOT_SLOT_STORE_SECTOR_SIZE;
    if (store->reader(store->ctx,
                      store->layout.slots[slot].payload_lba + i,
                      sector) != 0) {
      sha256_clear(&sha);
      return BOOT_SLOT_STORE_ERR_VERIFY;
    }
    sha256_update(&sha, sector, chunk);
    if (chunk < BOOT_SLOT_STORE_SECTOR_SIZE &&
        !store_zero_range(sector, chunk, BOOT_SLOT_STORE_SECTOR_SIZE)) {
      sha256_clear(&sha);
      return BOOT_SLOT_STORE_ERR_VERIFY;
    }
    offset += chunk;
  }
  sha256_final(&sha, actual_digest);
  sha256_clear(&sha);
  if (!store_equal(actual_digest, image->payload_sha256,
                   BOOT_SLOT_SHA256_SIZE))
    return BOOT_SLOT_STORE_ERR_VERIFY;
  store_encode_header(store, slot, image, sector);
  if (store->writer(store->ctx, store->layout.slots[slot].header_lba,
                    sector) != 0 || store->flusher(store->ctx) != 0)
    return BOOT_SLOT_STORE_ERR_COMMIT_UNKNOWN;
  if (store->reader(store->ctx, store->layout.slots[slot].header_lba,
                    sector) != 0 ||
      store_decode_header(store, slot, sector, &decoded) != 0 ||
      decoded.payload_size != image->payload_size ||
      !store_string_equal(decoded.version, image->version) ||
      !store_equal(decoded.payload_sha256, image->payload_sha256,
                   BOOT_SLOT_SHA256_SIZE))
    return BOOT_SLOT_STORE_ERR_COMMIT_UNKNOWN;
  return 0;
}

int boot_slot_store_confirm_health(
    struct boot_slot_store *store, uint64_t expected_lease_epoch,
    uint32_t slot, uint64_t generation) {
  struct boot_slot_store_io io;
  int rc = boot_slot_internal_operation_begin();
  if (rc != 0)
    return BOOT_SLOT_STORE_ERR_BUSY;
  rc = store_io_get_locked(store, expected_lease_epoch, &io);
  if (rc != 0 || !store_binding_matches_io(&store_binding, &io, store) ||
      !io.provider_backed || generation == 0u) {
    rc = BOOT_SLOT_STORE_ERR_STALE;
  } else {
    rc = boot_slot_internal_confirm_verified_locked(
        slot, generation, store_binding.epoch, expected_lease_epoch);
  }
  boot_slot_internal_operation_end();
  return rc;
}

int boot_slot_store_arm(struct boot_slot_store *store,
                        uint64_t expected_lease_epoch, uint32_t slot,
                        uint64_t expected_generation,
                        uint64_t *out_generation) {
  struct boot_slot_store_io io;
  int rc;
  if (out_generation)
    *out_generation = 0u;
  rc = boot_slot_internal_operation_begin();
  if (rc != 0)
    return BOOT_SLOT_STORE_ERR_BUSY;
  rc = store_io_get_locked(store, expected_lease_epoch, &io);
  if (rc != 0 || !out_generation || expected_generation == 0u ||
      bsp.stage_claimed || bsp.degraded || !io.provider_backed ||
      !store_binding_matches_io(&store_binding, &io, store)) {
    rc = BOOT_SLOT_STORE_ERR_STALE;
  } else {
    rc = boot_slot_internal_arm_provider_locked(slot, expected_generation,
                                                out_generation);
    if (rc == BOOT_SLOT_ERR_COMMIT_UNKNOWN)
      rc = BOOT_SLOT_STORE_ERR_COMMIT_UNKNOWN;
    else if (rc != 0)
      rc = BOOT_SLOT_STORE_ERR_STALE;
  }
  boot_slot_internal_operation_end();
  return rc;
}

int boot_slot_store_stage_inactive_authorized(
    struct boot_slot_store *store, uint64_t expected_lease_epoch,
    const struct boot_slot_snapshot *expected,
    uint32_t slot, const struct boot_slot_image *image, const uint8_t *payload,
    size_t payload_len, uint64_t *out_valid_generation) {
  struct boot_slot_stage_authorization authorization;
  struct boot_slot_manager manager;
  struct boot_slot_store_io io;
  int rc;
  if (out_valid_generation)
    *out_valid_generation = 0u;
  rc = boot_slot_internal_operation_begin();
  if (rc != 0)
    return BOOT_SLOT_STORE_ERR_BUSY;
  rc = store_io_get_locked(store, expected_lease_epoch, &io);
  if (rc != 0 || !expected || !image || !payload || !out_valid_generation ||
      !store_binding_matches_io(&store_binding, &io, store) ||
      expected->authority_epoch != store_binding.epoch ||
      expected->lease_epoch != expected_lease_epoch) {
    rc = BOOT_SLOT_STORE_ERR_STALE;
  } else {
    rc = store_stage_preflight(&store_binding, slot, image, payload, payload_len);
  }
  if (rc == 0)
    rc = store_stage_authorize(expected, slot, image, &authorization);
  if (rc == 0) {
    manager = bsm;
    rc = store_stage_payload(&store_binding, &manager, slot, image, payload,
                             payload_len);
  }
  if (rc == 0)
    rc = store_stage_publish(&authorization, image, out_valid_generation);
  if (rc != 0)
    bsp.stage_claimed = 0;
  boot_slot_internal_operation_end();
  return rc;
}

#if defined(UNIT_TEST) || defined(CAPYOS_UEFI_LOADER)
int boot_slot_store_stage_inactive(struct boot_slot_store *store,
                                   const struct boot_slot_manager *manager,
                                   uint32_t slot,
                                   const struct boot_slot_image *image,
                                   const uint8_t *payload, size_t payload_len) {
  (void)store;
  (void)manager;
  (void)slot;
  (void)image;
  (void)payload;
  (void)payload_len;
  return BOOT_SLOT_STORE_ERR_STALE;
}
#endif

#ifdef CAPYOS_UEFI_LOADER
int boot_slot_store_encode_header(
    const struct boot_slot_layout *layout, uint32_t slot,
    const struct boot_slot_image *image,
    uint8_t header[BOOT_SLOT_STORE_SECTOR_SIZE]) {
  struct boot_slot_store_io io;
  if (!layout || !image || !header || slot >= BOOT_SLOT_COUNT ||
      !store_image_valid(image,
                         layout->slots[slot].payload_capacity_sectors))
    return BOOT_SLOT_STORE_ERR_IO;
  store_zero(&io, sizeof(io));
  io.layout = *layout;
  store_encode_header(&io, slot, image, header);
  return 0;
}
#endif

int boot_slot_store_read_header(struct boot_slot_store *store,
                                uint64_t expected_lease_epoch, uint32_t slot,
                                struct boot_slot_image *out_image) {
  struct boot_slot_store_io io;
  uint8_t sector[BOOT_SLOT_STORE_SECTOR_SIZE];
  int rc;
  if (out_image)
    store_zero(out_image, sizeof(*out_image));
  rc = boot_slot_internal_operation_begin();
  if (rc != 0)
    return BOOT_SLOT_STORE_ERR_BUSY;
  rc = store_io_get_locked(store, expected_lease_epoch, &io);
  if (rc != 0 || !out_image || slot >= BOOT_SLOT_COUNT ||
      io.reader(io.ctx, io.layout.slots[slot].header_lba, sector) != 0)
    rc = BOOT_SLOT_STORE_ERR_IO;
  else
    rc = store_decode_header(&io, slot, sector, out_image);
  boot_slot_internal_operation_end();
  return rc;
}
