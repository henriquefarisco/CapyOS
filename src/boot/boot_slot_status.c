#include "boot/boot_slot.h"

static void status_print_u32(void (*print)(const char *), uint32_t value) {
  char buffer[12];
  char reverse[12];
  int length = 0;
  int out = 0;
  if (value == 0u) {
    print("0");
    return;
  }
  while (value > 0u) {
    reverse[length++] = (char)('0' + value % 10u);
    value /= 10u;
  }
  while (length > 0)
    buffer[out++] = reverse[--length];
  buffer[out] = '\0';
  print(buffer);
}

void boot_slot_status(void (*print)(const char *)) {
  struct boot_slot_manager manager;
  if (!print)
    return;
  if (boot_slot_manager_get(&manager) != 0) {
    print("Boot slot status unavailable: persistence outcome unknown\n");
    return;
  }
  for (uint32_t i = 0u; i < BOOT_SLOT_COUNT; ++i) {
    const struct boot_slot *slot = &manager.slots[i];
    const char *state = "unknown";
    switch (slot->state) {
    case BOOT_SLOT_EMPTY: state = "empty"; break;
    case BOOT_SLOT_VALID: state = "valid"; break;
    case BOOT_SLOT_ACTIVE: state = "active"; break;
    case BOOT_SLOT_ROLLBACK: state = "rollback"; break;
    case BOOT_SLOT_FAILED: state = "failed"; break;
    }
    print("Slot "); print(slot->name);
    print(": version="); print(slot->version[0] ? slot->version : "(empty)");
    print(" state="); print(state);
    print(" boots="); status_print_u32(print, slot->boot_count);
    print(" ok="); status_print_u32(print, slot->success_count);
    print(" fail="); status_print_u32(print, slot->fail_count);
    print(" health="); print(slot->health_confirmed ? "confirmed" : "pending");
    if (i == manager.active_slot)
      print(" [ACTIVE]");
    print("\n");
  }
  if (manager.rollback_pending)
    print("Rollback pending: yes\n");
}
