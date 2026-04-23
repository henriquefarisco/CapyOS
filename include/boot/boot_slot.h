#ifndef CORE_BOOT_SLOT_H
#define CORE_BOOT_SLOT_H

#include <stdint.h>
#include <stddef.h>

#define BOOT_SLOT_COUNT 2
#define BOOT_SLOT_NAME_MAX 8
#define BOOT_SLOT_VERSION_MAX 40

enum boot_slot_state {
  BOOT_SLOT_EMPTY = 0,
  BOOT_SLOT_VALID,
  BOOT_SLOT_ACTIVE,
  BOOT_SLOT_ROLLBACK,
  BOOT_SLOT_FAILED
};

struct boot_slot {
  char name[BOOT_SLOT_NAME_MAX];
  char version[BOOT_SLOT_VERSION_MAX];
  enum boot_slot_state state;
  uint32_t boot_count;
  uint32_t success_count;
  uint32_t fail_count;
  uint64_t installed_tick;
  uint64_t last_boot_tick;
  uint32_t checksum;
  int health_confirmed;
};

struct boot_slot_manager {
  struct boot_slot slots[BOOT_SLOT_COUNT];
  uint32_t active_slot;
  uint32_t next_slot;
  int rollback_pending;
};

void boot_slot_init(void);
int boot_slot_get_active(struct boot_slot *out);
int boot_slot_get(uint32_t index, struct boot_slot *out);
int boot_slot_stage(uint32_t slot, const char *version, uint32_t checksum);
int boot_slot_activate(uint32_t slot);
int boot_slot_confirm_health(void);
int boot_slot_rollback(void);
int boot_slot_needs_rollback(void);
void boot_slot_status(void (*print)(const char *));

#endif /* CORE_BOOT_SLOT_H */
