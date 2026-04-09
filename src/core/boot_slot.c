#include "core/boot_slot.h"
#include <stddef.h>

static struct boot_slot_manager bsm;
static int bsm_initialized = 0;

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
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= p[i];
    for (int j = 0; j < 8; j++)
      crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
  }
  return ~crc;
}

void boot_slot_init(void) {
  bs_memset(&bsm, 0, sizeof(bsm));
  bs_strcpy(bsm.slots[0].name, "A", BOOT_SLOT_NAME_MAX);
  bs_strcpy(bsm.slots[1].name, "B", BOOT_SLOT_NAME_MAX);
  bsm.slots[0].state = BOOT_SLOT_EMPTY;
  bsm.slots[1].state = BOOT_SLOT_EMPTY;
  bsm.active_slot = 0;
  bsm.next_slot = 0;
  bsm.rollback_pending = 0;
  bsm_initialized = 1;
}

int boot_slot_get_active(struct boot_slot *out) {
  if (!bsm_initialized || !out) return -1;
  *out = bsm.slots[bsm.active_slot];
  return 0;
}

int boot_slot_get(uint32_t index, struct boot_slot *out) {
  if (!bsm_initialized || !out || index >= BOOT_SLOT_COUNT) return -1;
  *out = bsm.slots[index];
  return 0;
}

int boot_slot_stage(uint32_t slot, const char *version, uint32_t checksum) {
  if (!bsm_initialized || slot >= BOOT_SLOT_COUNT || !version) return -1;
  struct boot_slot *s = &bsm.slots[slot];
  bs_strcpy(s->version, version, BOOT_SLOT_VERSION_MAX);
  s->checksum = checksum;
  s->state = BOOT_SLOT_VALID;
  s->health_confirmed = 0;
  s->boot_count = 0;
  s->success_count = 0;
  s->fail_count = 0;
  bsm.next_slot = slot;
  return 0;
}

int boot_slot_activate(uint32_t slot) {
  if (!bsm_initialized || slot >= BOOT_SLOT_COUNT) return -1;
  struct boot_slot *s = &bsm.slots[slot];
  if (s->state != BOOT_SLOT_VALID && s->state != BOOT_SLOT_ACTIVE) return -1;

  if (bsm.active_slot != slot) {
    bsm.slots[bsm.active_slot].state = BOOT_SLOT_ROLLBACK;
  }
  s->state = BOOT_SLOT_ACTIVE;
  s->boot_count++;
  bsm.active_slot = slot;
  bsm.rollback_pending = 1;
  return 0;
}

int boot_slot_confirm_health(void) {
  if (!bsm_initialized) return -1;
  struct boot_slot *s = &bsm.slots[bsm.active_slot];
  s->health_confirmed = 1;
  s->success_count++;
  bsm.rollback_pending = 0;
  return 0;
}

int boot_slot_rollback(void) {
  if (!bsm_initialized) return -1;
  uint32_t other = (bsm.active_slot == 0) ? 1 : 0;
  struct boot_slot *other_slot = &bsm.slots[other];

  if (other_slot->state == BOOT_SLOT_ROLLBACK ||
      other_slot->state == BOOT_SLOT_VALID) {
    bsm.slots[bsm.active_slot].state = BOOT_SLOT_FAILED;
    bsm.slots[bsm.active_slot].fail_count++;
    other_slot->state = BOOT_SLOT_ACTIVE;
    bsm.active_slot = other;
    bsm.rollback_pending = 0;
    return 0;
  }
  return -1;
}

int boot_slot_needs_rollback(void) {
  if (!bsm_initialized) return 0;
  return bsm.rollback_pending;
}

static void bs_print_u32(void (*print)(const char *), uint32_t v) {
  char buf[12];
  int pos = 0;
  if (v == 0) { buf[0] = '0'; buf[1] = 0; print(buf); return; }
  char tmp[12]; int tp = 0;
  while (v > 0) { tmp[tp++] = '0' + (v % 10); v /= 10; }
  for (int i = tp - 1; i >= 0; i--) buf[pos++] = tmp[i];
  buf[pos] = 0;
  print(buf);
}

void boot_slot_status(void (*print)(const char *)) {
  if (!print || !bsm_initialized) return;
  for (uint32_t i = 0; i < BOOT_SLOT_COUNT; i++) {
    struct boot_slot *s = &bsm.slots[i];
    print("Slot "); print(s->name);
    print(": version="); print(s->version[0] ? s->version : "(empty)");
    print(" state=");
    const char *st = "unknown";
    switch (s->state) {
      case BOOT_SLOT_EMPTY: st = "empty"; break;
      case BOOT_SLOT_VALID: st = "valid"; break;
      case BOOT_SLOT_ACTIVE: st = "active"; break;
      case BOOT_SLOT_ROLLBACK: st = "rollback"; break;
      case BOOT_SLOT_FAILED: st = "failed"; break;
    }
    print(st);
    print(" boots="); bs_print_u32(print, s->boot_count);
    print(" ok="); bs_print_u32(print, s->success_count);
    print(" fail="); bs_print_u32(print, s->fail_count);
    print(" health="); print(s->health_confirmed ? "confirmed" : "pending");
    if (i == bsm.active_slot) print(" [ACTIVE]");
    print("\n");
  }
  if (bsm.rollback_pending) print("Rollback pending: yes\n");
}
