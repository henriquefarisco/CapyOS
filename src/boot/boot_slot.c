#include "boot/boot_slot.h"
#include "internal/boot_slot_internal.h"
#include <stddef.h>

struct boot_slot_manager bsm;
int bsm_initialized = 0;
int boot_slot_operation_busy = 0;
struct boot_slot_persistence_state bsp;
enum boot_slot_lifecycle_phase boot_slot_phase = BOOT_SLOT_PHASE_UNINITIALIZED;

int boot_slot_internal_operation_begin(void) {
  return __sync_lock_test_and_set(&boot_slot_operation_busy, 1)
             ? BOOT_SLOT_ERR_BUSY
             : 0;
}

void boot_slot_internal_operation_end(void) {
  __sync_lock_release(&boot_slot_operation_busy);
}
static const uint8_t bs_record_magic[8] = {'C', 'A', 'P', 'Y',
                                           'A', 'B', '0', '0'};

#define BS_RECORD_FORMAT_VERSION 0u
#define BS_RECORD_SLOT0_OFFSET 64u
#define BS_RECORD_SLOT_SIZE 128u
#define BS_RECORD_RESERVED_OFFSET 320u
#define BS_RECORD_CRC_OFFSET 508u

static void bs_memset(void *dst, int val, size_t len) {
  uint8_t *d = (uint8_t *)dst;
  for (size_t i = 0; i < len; i++) d[i] = (uint8_t)val;
}
static void bs_strcpy(char *dst, const char *src, size_t max) {
  size_t i = 0;
  while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
  dst[i] = '\0';
}
static uint32_t bs_crc32(const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) {
    crc ^= p[i];
    for (int j = 0; j < 8; j++)
      crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
  }
  return ~crc;
}
static void bs_put_u32(uint8_t *dst, uint32_t value) {
  dst[0] = (uint8_t)(value & 0xFFu);
  dst[1] = (uint8_t)((value >> 8) & 0xFFu);
  dst[2] = (uint8_t)((value >> 16) & 0xFFu);
  dst[3] = (uint8_t)((value >> 24) & 0xFFu);
}
static uint32_t bs_get_u32(const uint8_t *src) {
  return (uint32_t)src[0] | ((uint32_t)src[1] << 8) |
         ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}
static void bs_put_u64(uint8_t *dst, uint64_t value) {
  bs_put_u32(dst, (uint32_t)value);
  bs_put_u32(dst + 4, (uint32_t)(value >> 32));
}
static uint64_t bs_get_u64(const uint8_t *src) {
  return (uint64_t)bs_get_u32(src) | ((uint64_t)bs_get_u32(src + 4) << 32);
}
static int bs_bytes_equal(const uint8_t *a, const uint8_t *b, size_t len) {
  uint8_t diff = 0u;
  if (!a || !b)
    return 0;
  for (size_t i = 0; i < len; ++i)
    diff |= (uint8_t)(a[i] ^ b[i]);
  return diff == 0u;
}
static int bs_bytes_zero(const uint8_t *data, size_t start, size_t end) {
  if (!data || start > end || end > BOOT_SLOT_PERSIST_RECORD_SIZE)
    return 0;
  for (size_t i = start; i < end; ++i) {
    if (data[i] != 0u)
      return 0;
  }
  return 1;
}
static int bs_record_erased(const uint8_t *record) {
  int all_zero = 1;
  int all_ones = 1;
  if (!record)
    return 0;
  for (size_t i = 0u; i < BOOT_SLOT_PERSIST_RECORD_SIZE; ++i) {
    if (record[i] != 0u)
      all_zero = 0;
    if (record[i] != 0xFFu)
      all_ones = 0;
  }
  return all_zero || all_ones;
}
static int bs_string_fits(const char *text, size_t limit) {
  size_t len = 0u;
  if (!text || limit == 0u)
    return 0;
  while (len < limit && text[len]) {
    uint8_t ch = (uint8_t)text[len];
    if (ch < 0x20u || ch > 0x7Eu)
      return 0;
    ++len;
  }
  return len > 0u && len < limit;
}
int boot_slot_layout_plan(uint32_t boot_sectors,
                          struct boot_slot_layout *out) {
  uint32_t usable = 0u;
  uint32_t first_region = 0u;
  uint32_t second_region = 0u;
  if (out)
    bs_memset(out, 0, sizeof(*out));
  if (!out || boot_sectors < 6u)
    return BOOT_SLOT_ERR_IO;
  usable = boot_sectors - BOOT_SLOT_PERSIST_COPY_COUNT;
  first_region = usable / BOOT_SLOT_COUNT;
  second_region = usable - first_region;
  if (first_region < 2u || second_region < 2u)
    return BOOT_SLOT_ERR_IO;
  out->boot_sectors = boot_sectors;
  out->slots[0].header_lba = 0u;
  out->slots[0].payload_lba = 1u;
  out->slots[0].payload_capacity_sectors = first_region - 1u;
  out->slots[1].header_lba = first_region;
  out->slots[1].payload_lba = first_region + 1u;
  out->slots[1].payload_capacity_sectors = second_region - 1u;
  out->control_lba[0] = boot_sectors - 2u;
  out->control_lba[1] = boot_sectors - 1u;
  return 0;
}
static int bs_layout_equal(const struct boot_slot_layout *a,
                           const struct boot_slot_layout *b) {
  if (!a || !b || a->boot_sectors != b->boot_sectors)
    return 0;
  for (uint32_t i = 0u; i < BOOT_SLOT_COUNT; ++i) {
    if (a->slots[i].header_lba != b->slots[i].header_lba ||
        a->slots[i].payload_lba != b->slots[i].payload_lba ||
        a->slots[i].payload_capacity_sectors !=
            b->slots[i].payload_capacity_sectors)
      return 0;
  }
  return a->control_lba[0] == b->control_lba[0] &&
         a->control_lba[1] == b->control_lba[1];
}
static int bs_digest_present(const uint8_t *digest) {
  uint8_t any = 0u;
  if (!digest)
    return 0;
  for (size_t i = 0u; i < BOOT_SLOT_SHA256_SIZE; ++i)
    any |= digest[i];
  return any != 0u;
}
int bs_image_valid(const struct boot_slot_image *image,
                          uint32_t capacity_sectors) {
  uint64_t capacity_bytes = (uint64_t)capacity_sectors * 512u;
  return image && bs_string_fits(image->version, sizeof(image->version)) &&
         image->payload_size > 0u &&
         (uint64_t)image->payload_size <= capacity_bytes &&
         bs_digest_present(image->payload_sha256);
}
static void bs_copy_bytes(uint8_t *dst, const uint8_t *src, size_t len) {
  if (!dst || !src)
    return;
  for (size_t i = 0u; i < len; ++i)
    dst[i] = src[i];
}
static void bs_manager_defaults(struct boot_slot_manager *manager) {
  if (!manager)
    return;
  bs_memset(manager, 0, sizeof(*manager));
  bs_strcpy(manager->slots[0].name, "A", BOOT_SLOT_NAME_MAX);
  bs_strcpy(manager->slots[1].name, "B", BOOT_SLOT_NAME_MAX);
  manager->slots[0].state = BOOT_SLOT_EMPTY;
  manager->slots[1].state = BOOT_SLOT_EMPTY;
  manager->confirmed_slot = 0u;
  manager->pending_slot = BOOT_SLOT_NONE;
}
static void bs_encode_slot(uint8_t *record, size_t offset,
                           const struct boot_slot *slot) {
  size_t i = 0u;
  bs_put_u32(record + offset, (uint32_t)slot->state);
  bs_put_u32(record + offset + 4u, slot->boot_count);
  bs_put_u32(record + offset + 8u, slot->success_count);
  bs_put_u32(record + offset + 12u, slot->fail_count);
  bs_put_u32(record + offset + 16u, slot->header_lba);
  bs_put_u32(record + offset + 20u, slot->payload_lba);
  bs_put_u32(record + offset + 24u, slot->payload_capacity_sectors);
  bs_put_u32(record + offset + 28u, slot->payload_size);
  bs_put_u32(record + offset + 32u, slot->checksum);
  bs_put_u32(record + offset + 36u, slot->health_confirmed ? 1u : 0u);
  while (i + 1u < BOOT_SLOT_VERSION_MAX && slot->version[i]) {
    record[offset + 40u + i] = (uint8_t)slot->version[i];
    ++i;
  }
  bs_copy_bytes(record + offset + 80u, slot->payload_sha256,
                BOOT_SLOT_SHA256_SIZE);
}
static int bs_decode_version(char *out, const uint8_t *src) {
  size_t end = BOOT_SLOT_VERSION_MAX;
  if (!out || !src)
    return -1;
  for (size_t i = 0u; i < BOOT_SLOT_VERSION_MAX; ++i) {
    if (src[i] == 0u) {
      end = i;
      break;
    }
    if (src[i] < 0x20u || src[i] > 0x7Eu)
      return -1;
    out[i] = (char)src[i];
  }
  if (end == BOOT_SLOT_VERSION_MAX)
    return -1;
  out[end] = '\0';
  for (size_t i = end + 1u; i < BOOT_SLOT_VERSION_MAX; ++i) {
    if (src[i] != 0u)
      return -1;
  }
  return 0;
}
static int bs_decode_slot(const uint8_t *record, size_t offset,
                          struct boot_slot *slot) {
  uint32_t state = 0u;
  uint32_t health = 0u;
  if (!record || !slot)
    return -1;
  state = bs_get_u32(record + offset);
  health = bs_get_u32(record + offset + 36u);
  if (state > (uint32_t)BOOT_SLOT_FAILED || health > 1u ||
      !bs_bytes_zero(record, offset + 112u, offset + BS_RECORD_SLOT_SIZE))
    return -1;
  slot->state = (enum boot_slot_state)state;
  slot->boot_count = bs_get_u32(record + offset + 4u);
  slot->success_count = bs_get_u32(record + offset + 8u);
  slot->fail_count = bs_get_u32(record + offset + 12u);
  slot->header_lba = bs_get_u32(record + offset + 16u);
  slot->payload_lba = bs_get_u32(record + offset + 20u);
  slot->payload_capacity_sectors = bs_get_u32(record + offset + 24u);
  slot->payload_size = bs_get_u32(record + offset + 28u);
  slot->checksum = bs_get_u32(record + offset + 32u);
  slot->health_confirmed = health ? 1 : 0;
  if (bs_decode_version(slot->version, record + offset + 40u) != 0)
    return -1;
  bs_copy_bytes(slot->payload_sha256, record + offset + 80u,
                BOOT_SLOT_SHA256_SIZE);
  if (slot->state == BOOT_SLOT_EMPTY) {
    if (slot->version[0] || slot->boot_count || slot->success_count ||
        slot->fail_count || slot->payload_size || slot->checksum ||
        slot->health_confirmed ||
        !bs_bytes_zero(slot->payload_sha256, 0u, BOOT_SLOT_SHA256_SIZE))
      return -1;
  } else if (!slot->version[0] || slot->payload_size == 0u) {
    return -1;
  }
  return 0;
}
static int bs_slot_identity_valid(const struct boot_slot *slot) {
  uint64_t capacity_bytes = 0u;
  if (!slot || slot->state < BOOT_SLOT_EMPTY ||
      slot->state > BOOT_SLOT_FAILED ||
      (slot->health_confirmed != 0 && slot->health_confirmed != 1) ||
      slot->payload_capacity_sectors == 0u ||
      slot->payload_lba != slot->header_lba + 1u)
    return 0;
  if (slot->state == BOOT_SLOT_EMPTY)
    return slot->version[0] == '\0' && slot->payload_size == 0u &&
           slot->boot_count == 0u && slot->success_count == 0u &&
           slot->fail_count == 0u && slot->checksum == 0u &&
           !slot->health_confirmed &&
           !bs_digest_present(slot->payload_sha256);
  capacity_bytes = (uint64_t)slot->payload_capacity_sectors * 512u;
  return bs_string_fits(slot->version, sizeof(slot->version)) &&
         slot->payload_size > 0u &&
         (uint64_t)slot->payload_size <= capacity_bytes &&
         bs_digest_present(slot->payload_sha256);
}
static int bs_manager_matches_layout(const struct boot_slot_manager *manager,
                                     const struct boot_slot_layout *layout) {
  if (!manager || !layout || manager->boot_sectors != layout->boot_sectors ||
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
int boot_slot_manager_validate(const struct boot_slot_manager *manager) {
  struct boot_slot_layout expected;
  struct boot_slot_layout actual;
  uint32_t confirmed = 0u;
  uint32_t pending = 0u;
  if (!manager || manager->active_slot >= BOOT_SLOT_COUNT ||
      manager->next_slot >= BOOT_SLOT_COUNT ||
      manager->confirmed_slot >= BOOT_SLOT_COUNT ||
      (manager->pending_slot != BOOT_SLOT_NONE &&
       manager->pending_slot >= BOOT_SLOT_COUNT) ||
      manager->tries_remaining > BOOT_SLOT_MAX_TRIES ||
      (manager->rollback_pending != 0 && manager->rollback_pending != 1) ||
      boot_slot_layout_plan(manager->boot_sectors, &expected) != 0)
    return 0;
  bs_memset(&actual, 0, sizeof(actual));
  actual.boot_sectors = manager->boot_sectors;
  actual.control_lba[0] = manager->control_lba[0];
  actual.control_lba[1] = manager->control_lba[1];
  for (uint32_t i = 0u; i < BOOT_SLOT_COUNT; ++i) {
    actual.slots[i].header_lba = manager->slots[i].header_lba;
    actual.slots[i].payload_lba = manager->slots[i].payload_lba;
    actual.slots[i].payload_capacity_sectors =
        manager->slots[i].payload_capacity_sectors;
    if (!bs_slot_identity_valid(&manager->slots[i]) ||
        (manager->slots[i].state == BOOT_SLOT_FAILED &&
         manager->slots[i].health_confirmed))
      return 0;
  }
  if (!bs_layout_equal(&actual, &expected))
    return 0;
  confirmed = manager->confirmed_slot;
  pending = manager->pending_slot;
  if (pending == BOOT_SLOT_NONE) {
    uint32_t other = confirmed ^ 1u;
    enum boot_slot_state other_state = manager->slots[other].state;
    if (manager->rollback_pending || manager->tries_remaining != 0u ||
        manager->active_slot != confirmed ||
        manager->slots[confirmed].state != BOOT_SLOT_ACTIVE ||
        !manager->slots[confirmed].health_confirmed ||
        other_state == BOOT_SLOT_ACTIVE || other_state == BOOT_SLOT_ROLLBACK)
      return 0;
    return manager->next_slot == confirmed ||
           (manager->next_slot == other && other_state == BOOT_SLOT_VALID);
  }
  return pending != confirmed && manager->rollback_pending &&
         manager->active_slot == pending && manager->next_slot == pending &&
         manager->slots[pending].state == BOOT_SLOT_ACTIVE &&
         !manager->slots[pending].health_confirmed &&
         manager->slots[confirmed].state == BOOT_SLOT_ROLLBACK &&
         manager->slots[confirmed].health_confirmed;
}
static void bs_record_encode(const struct boot_slot_manager *manager,
                             uint64_t generation, uint8_t *record) {
  bs_memset(record, 0, BOOT_SLOT_PERSIST_RECORD_SIZE);
  for (size_t i = 0u; i < sizeof(bs_record_magic); ++i)
    record[i] = bs_record_magic[i];
  bs_put_u32(record + 8u, BS_RECORD_FORMAT_VERSION);
  bs_put_u32(record + 12u, BOOT_SLOT_PERSIST_RECORD_SIZE);
  bs_put_u64(record + 16u, generation);
  bs_put_u32(record + 24u, manager->active_slot);
  bs_put_u32(record + 28u, manager->next_slot);
  bs_put_u32(record + 32u, manager->rollback_pending ? 1u : 0u);
  bs_put_u32(record + 36u, manager->confirmed_slot);
  bs_put_u32(record + 40u, manager->pending_slot);
  bs_put_u32(record + 44u, manager->tries_remaining);
  bs_put_u32(record + 48u, manager->boot_sectors);
  bs_put_u32(record + 52u, manager->control_lba[0]);
  bs_put_u32(record + 56u, manager->control_lba[1]);
  bs_encode_slot(record, BS_RECORD_SLOT0_OFFSET, &manager->slots[0]);
  bs_encode_slot(record, BS_RECORD_SLOT0_OFFSET + BS_RECORD_SLOT_SIZE,
                 &manager->slots[1]);
  bs_put_u32(record + BS_RECORD_CRC_OFFSET,
             bs_crc32(record, BS_RECORD_CRC_OFFSET));
}
static int bs_record_decode(const uint8_t *record,
                            struct boot_slot_manager *manager,
                            uint64_t *generation) {
  struct boot_slot_manager decoded;
  uint32_t rollback = 0u;
  uint32_t other = 0u;
  if (!record || !manager || !generation ||
      !bs_bytes_equal(record, bs_record_magic, sizeof(bs_record_magic)) ||
      bs_get_u32(record + 8u) != BS_RECORD_FORMAT_VERSION ||
      bs_get_u32(record + 12u) != BOOT_SLOT_PERSIST_RECORD_SIZE ||
      bs_get_u64(record + 16u) == 0u ||
      bs_crc32(record, BS_RECORD_CRC_OFFSET) !=
          bs_get_u32(record + BS_RECORD_CRC_OFFSET) ||
      !bs_bytes_zero(record, 60u, BS_RECORD_SLOT0_OFFSET) ||
      !bs_bytes_zero(record, BS_RECORD_RESERVED_OFFSET,
                     BS_RECORD_CRC_OFFSET))
    return -1;
  bs_manager_defaults(&decoded);
  decoded.active_slot = bs_get_u32(record + 24u);
  decoded.next_slot = bs_get_u32(record + 28u);
  rollback = bs_get_u32(record + 32u);
  decoded.confirmed_slot = bs_get_u32(record + 36u);
  decoded.pending_slot = bs_get_u32(record + 40u);
  decoded.tries_remaining = bs_get_u32(record + 44u);
  decoded.boot_sectors = bs_get_u32(record + 48u);
  decoded.control_lba[0] = bs_get_u32(record + 52u);
  decoded.control_lba[1] = bs_get_u32(record + 56u);
  if (decoded.active_slot >= BOOT_SLOT_COUNT ||
      decoded.next_slot >= BOOT_SLOT_COUNT || rollback > 1u ||
      bs_decode_slot(record, BS_RECORD_SLOT0_OFFSET, &decoded.slots[0]) != 0 ||
      bs_decode_slot(record, BS_RECORD_SLOT0_OFFSET + BS_RECORD_SLOT_SIZE,
                     &decoded.slots[1]) != 0)
    return -1;
  decoded.rollback_pending = rollback ? 1 : 0;
  other = decoded.active_slot ^ 1u;
  if (other >= BOOT_SLOT_COUNT || !boot_slot_manager_validate(&decoded))
    return -1;
  *manager = decoded;
  *generation = bs_get_u64(record + 16u);
  return 0;
}
int bs_write_verified(const struct boot_slot_manager *manager,
                             uint32_t copy_index, uint64_t generation) {
  uint8_t record[BOOT_SLOT_PERSIST_RECORD_SIZE];
  uint8_t readback[BOOT_SLOT_PERSIST_RECORD_SIZE];
  struct boot_slot_manager decoded;
  uint64_t decoded_generation = 0u;
  if (!manager || !bsp.configured || copy_index >= BOOT_SLOT_PERSIST_COPY_COUNT)
    return BOOT_SLOT_ERR_IO;
  if (!boot_slot_manager_validate(manager))
    return BOOT_SLOT_ERR_IO;
  bs_record_encode(manager, generation, record);
  if (bsp.writer(bsp.ctx, copy_index, record, sizeof(record)) != 0)
    return BOOT_SLOT_ERR_COMMIT_UNKNOWN;
  if (bsp.flusher(bsp.ctx) != 0 ||
      bsp.reader(bsp.ctx, copy_index, readback, sizeof(readback)) != 0 ||
      !bs_bytes_equal(record, readback, sizeof(record)) ||
      bs_record_decode(readback, &decoded, &decoded_generation) != 0 ||
      decoded_generation != generation)
    return BOOT_SLOT_ERR_COMMIT_UNKNOWN;
  return 0;
}
int bs_commit_locked(const struct boot_slot_manager *candidate) {
  uint32_t target_copy;
  uint64_t generation;
  int rc;
  if (!candidate || !bsm_initialized || (bsp.configured && bsp.stage_claimed))
    return BOOT_SLOT_ERR_BUSY;
  if (!bsp.configured) {
    bsm = *candidate;
    return 0;
  }
  if (!bsp.ready || bsp.generation == UINT64_MAX)
    return BOOT_SLOT_ERR_IO;
  target_copy = bsp.active_copy ^ 1u;
  generation = bsp.generation + 1u;
  rc = bs_write_verified(candidate, target_copy, generation);
  if (rc == 0) {
    bsm = *candidate;
    bsp.active_copy = target_copy;
    bsp.generation = generation;
    bsp.degraded = 0;
  } else if (rc == BOOT_SLOT_ERR_COMMIT_UNKNOWN) {
    bsp.ready = 0;
    boot_slot_phase = BOOT_SLOT_PHASE_BOUND_UNKNOWN;
  }
  return rc;
}
void boot_slot_internal_init_locked(void) {
  if (bsp.stage_claimed)
    return;
  bs_manager_defaults(&bsm);
  bs_memset(&bsp, 0, sizeof(bsp));
  bsm_initialized = 1;
  if (boot_slot_phase == BOOT_SLOT_PHASE_UNINITIALIZED)
    boot_slot_phase = BOOT_SLOT_PHASE_RAM_TEST;
}
int boot_slot_internal_set_persistence_locked(boot_slot_persist_read_fn reader,
                              boot_slot_persist_write_fn writer,
                              boot_slot_persist_flush_fn flusher, void *ctx,
                              const struct boot_slot_layout *layout) {
  uint8_t records[BOOT_SLOT_PERSIST_COPY_COUNT][BOOT_SLOT_PERSIST_RECORD_SIZE];
  struct boot_slot_manager managers[BOOT_SLOT_PERSIST_COPY_COUNT];
  uint64_t generations[BOOT_SLOT_PERSIST_COPY_COUNT] = {0u, 0u};
  int read_rc[BOOT_SLOT_PERSIST_COPY_COUNT] = {BOOT_SLOT_ERR_IO,
                                               BOOT_SLOT_ERR_IO};
  int usable[BOOT_SLOT_PERSIST_COPY_COUNT] = {0, 0};
  struct boot_slot_layout expected;
  uint32_t selected = 0u;
  if (bsp.stage_claimed)
    return BOOT_SLOT_ERR_BUSY;
  if (!boot_slot_internal_bind_allowed_locked() || !reader || !writer ||
      !flusher || !layout ||
      boot_slot_layout_plan(layout->boot_sectors, &expected) != 0 ||
      !bs_layout_equal(layout, &expected))
    return BOOT_SLOT_ERR_IO;
  bs_manager_defaults(&bsm);
  bsp.reader = reader;
  bsp.writer = writer;
  bsp.flusher = flusher;
  bsp.ctx = ctx;
  bsp.generation = 0u;
  bsp.active_copy = 0u;
  bsp.configured = 1;
  bsp.ready = 0;
  bsp.blank = 0;
  bsp.degraded = 0;
  bsp.stage_claimed = 0;
  bsp.authority_epoch = 0u;
  boot_slot_phase = BOOT_SLOT_PHASE_BOUND_UNKNOWN;
  bsp.layout = *layout;
  for (uint32_t i = 0u; i < BOOT_SLOT_PERSIST_COPY_COUNT; ++i) {
    read_rc[i] = reader(ctx, i, records[i], sizeof(records[i]));
    if (read_rc[i] != 0 && read_rc[i] != BOOT_SLOT_PERSIST_EMPTY)
      return BOOT_SLOT_ERR_IO;
    if (read_rc[i] == 0 && bs_record_erased(records[i]))
      read_rc[i] = BOOT_SLOT_PERSIST_EMPTY;
    if (read_rc[i] == 0 &&
        bs_record_decode(records[i], &managers[i], &generations[i]) == 0) {
      usable[i] = bs_manager_matches_layout(&managers[i], layout);
    }
  }
  if (usable[0] && usable[1] && generations[0] == generations[1]) {
    if (!bs_bytes_equal(records[0], records[1], sizeof(records[0])))
      return BOOT_SLOT_ERR_IO;
    selected = 0u;
  } else if (usable[1] &&
             (!usable[0] || generations[1] > generations[0])) {
    selected = 1u;
  }
  if (usable[0] || usable[1]) {
    if ((usable[0] && read_rc[1] < 0) || (usable[1] && read_rc[0] < 0) ||
        (managers[selected].pending_slot != BOOT_SLOT_NONE &&
         (!usable[0] || !usable[1])))
      return BOOT_SLOT_ERR_IO;
    bsm = managers[selected];
    bsp.generation = generations[selected];
    bsp.active_copy = selected;
    bsp.degraded = usable[0] && usable[1] ? 0 : 1;
    bsp.ready = 1;
    boot_slot_phase = BOOT_SLOT_PHASE_BOUND_READY;
    return 0;
  }
  if (read_rc[0] == BOOT_SLOT_PERSIST_EMPTY &&
      read_rc[1] == BOOT_SLOT_PERSIST_EMPTY) {
    bsp.blank = 1;
    boot_slot_phase = BOOT_SLOT_PHASE_BOUND_EMPTY;
    return BOOT_SLOT_PERSIST_EMPTY;
  }
  return BOOT_SLOT_ERR_IO;
}
#ifdef UNIT_TEST
int boot_slot_set_persistence(boot_slot_persist_read_fn reader,
                              boot_slot_persist_write_fn writer,
                              boot_slot_persist_flush_fn flusher, void *ctx,
                              const struct boot_slot_layout *layout) {
  int rc = boot_slot_internal_operation_begin();
  if (rc != 0)
    return rc;
  if (!bsm_initialized)
    boot_slot_internal_init_locked();
  if (bsp.configured || bsp.lease_owner)
    rc = BOOT_SLOT_ERR_BUSY;
  else
    rc = boot_slot_internal_set_persistence_locked(reader, writer, flusher, ctx,
                                                   layout);
  boot_slot_internal_operation_end();
  return rc;
}
#endif

void bs_slot_apply_image(struct boot_slot *slot, uint32_t slot_index,
                                const struct boot_slot_image *image,
                                enum boot_slot_state state) {
  uint32_t header_lba = slot->header_lba;
  uint32_t payload_lba = slot->payload_lba;
  uint32_t payload_capacity = slot->payload_capacity_sectors;
  bs_memset(slot, 0, sizeof(*slot));
  bs_strcpy(slot->name, slot_index == 0u ? "A" : "B", BOOT_SLOT_NAME_MAX);
  slot->header_lba = header_lba;
  slot->payload_lba = payload_lba;
  slot->payload_capacity_sectors = payload_capacity;
  bs_strcpy(slot->version, image->version, BOOT_SLOT_VERSION_MAX);
  slot->payload_size = image->payload_size;
  bs_copy_bytes(slot->payload_sha256, image->payload_sha256,
                BOOT_SLOT_SHA256_SIZE);
  slot->state = state;
}
int boot_slot_internal_initialize_persistent_locked(const struct boot_slot_layout *layout,
                                    const struct boot_slot_image *image) {
  struct boot_slot_layout expected;
  struct boot_slot_manager candidate;
  int rc = 0;
  if (!boot_slot_internal_bind_allowed_locked() || !bsm_initialized ||
      !bsp.configured || bsp.ready || !bsp.blank ||
      !layout || boot_slot_layout_plan(layout->boot_sectors, &expected) != 0 ||
      !bs_layout_equal(layout, &expected) ||
      !bs_layout_equal(layout, &bsp.layout) ||
      !bs_image_valid(image, layout->slots[0].payload_capacity_sectors))
    return BOOT_SLOT_ERR_IO;
  bs_manager_defaults(&candidate);
  candidate.boot_sectors = layout->boot_sectors;
  candidate.control_lba[0] = layout->control_lba[0];
  candidate.control_lba[1] = layout->control_lba[1];
  for (uint32_t i = 0u; i < BOOT_SLOT_COUNT; ++i) {
    candidate.slots[i].header_lba = layout->slots[i].header_lba;
    candidate.slots[i].payload_lba = layout->slots[i].payload_lba;
    candidate.slots[i].payload_capacity_sectors =
        layout->slots[i].payload_capacity_sectors;
  }
  bs_slot_apply_image(&candidate.slots[0], 0u, image, BOOT_SLOT_ACTIVE);
  candidate.slots[0].boot_count = 1u;
  candidate.slots[0].success_count = 1u;
  candidate.slots[0].health_confirmed = 1;
  candidate.active_slot = 0u;
  candidate.next_slot = 0u;
  candidate.rollback_pending = 0;
  candidate.confirmed_slot = 0u;
  candidate.pending_slot = BOOT_SLOT_NONE;
  candidate.tries_remaining = 0u;
  rc = bs_write_verified(&candidate, 0u, 1u);
  if (rc != 0) {
    if (rc == BOOT_SLOT_ERR_COMMIT_UNKNOWN)
      bsp.blank = 0;
    boot_slot_phase = BOOT_SLOT_PHASE_BOUND_UNKNOWN;
    return rc;
  }
  bsp.blank = 0;
  rc = bs_write_verified(&candidate, 1u, 2u);
  if (rc != 0) {
    boot_slot_phase = BOOT_SLOT_PHASE_BOUND_UNKNOWN;
    return rc;
  }
  bsm = candidate;
  bsp.generation = 2u;
  bsp.active_copy = 1u;
  bsp.degraded = 0;
  bsp.ready = 1;
  boot_slot_phase = BOOT_SLOT_PHASE_BOUND_READY;
  return 0;
}

#ifdef UNIT_TEST
int boot_slot_initialize_persistent(const struct boot_slot_layout *layout,
                                    const struct boot_slot_image *image) {
  int rc = boot_slot_internal_operation_begin();
  if (rc != 0)
    return rc;
  rc = boot_slot_internal_initialize_persistent_locked(layout, image);
  boot_slot_internal_operation_end();
  return rc;
}

#endif
int boot_slot_internal_persistence_ready_locked(void) {
  return boot_slot_phase == BOOT_SLOT_PHASE_BOUND_READY && bsm_initialized &&
         bsp.configured && bsp.ready;
}

uint64_t boot_slot_internal_persistence_generation_locked(void) {
  return boot_slot_internal_persistence_ready_locked() ? bsp.generation : 0u;
}
#ifdef UNIT_TEST
int boot_slot_internal_stage_locked(uint32_t slot, const char *version,
                                    uint32_t checksum) {
  struct boot_slot_manager candidate;
  struct boot_slot *staged = NULL;
  if (!boot_slot_internal_ram_ready_locked() || slot >= BOOT_SLOT_COUNT ||
      !bs_string_fits(version, BOOT_SLOT_VERSION_MAX) ||
      bsm.rollback_pending || bsm.slots[slot].state == BOOT_SLOT_ACTIVE)
    return BOOT_SLOT_ERR_IO;
  candidate = bsm;
  staged = &candidate.slots[slot];
  bs_memset(staged, 0, sizeof(*staged));
  bs_strcpy(staged->name, slot == 0u ? "A" : "B", BOOT_SLOT_NAME_MAX);
  bs_strcpy(staged->version, version, BOOT_SLOT_VERSION_MAX);
  staged->checksum = checksum;
  staged->state = BOOT_SLOT_VALID;
  candidate.next_slot = slot;
  return bs_commit_locked(&candidate);
}
int boot_slot_stage_image(uint32_t slot, const struct boot_slot_image *image) {
  (void)slot;
  (void)image;
  return BOOT_SLOT_ERR_IO;
}
#endif
