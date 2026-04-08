#ifndef ARCH_X86_64_STORAGE_RUNTIME_HYPERV_PLAN_H
#define ARCH_X86_64_STORAGE_RUNTIME_HYPERV_PLAN_H

#include <stdint.h>

enum x64_storage_hyperv_gate_state {
  X64_STORAGE_HYPERV_GATE_INVALID = 0,
  X64_STORAGE_HYPERV_GATE_WAIT_PLATFORM,
  X64_STORAGE_HYPERV_GATE_PREPARE_BUS,
  X64_STORAGE_HYPERV_GATE_WAIT_BUS,
  X64_STORAGE_HYPERV_GATE_WAIT_OFFER,
  X64_STORAGE_HYPERV_GATE_WAIT_RUNTIME,
  X64_STORAGE_HYPERV_GATE_OPEN,
};

enum x64_storage_hyperv_action {
  X64_STORAGE_HYPERV_ACTION_INVALID = 0,
  X64_STORAGE_HYPERV_ACTION_WAIT_PLATFORM,
  X64_STORAGE_HYPERV_ACTION_PREPARE_BUS,
  X64_STORAGE_HYPERV_ACTION_WAIT_BUS,
  X64_STORAGE_HYPERV_ACTION_WAIT_OFFER,
  X64_STORAGE_HYPERV_ACTION_WAIT_RUNTIME,
  X64_STORAGE_HYPERV_ACTION_ENABLE_PROBE,
  X64_STORAGE_HYPERV_ACTION_STEP_PROBE,
  X64_STORAGE_HYPERV_ACTION_STEP_RUNTIME,
  X64_STORAGE_HYPERV_ACTION_NOOP,
};

struct x64_storage_hyperv_plan {
  uint8_t gate_state;
  uint8_t next_action;
};

void x64_storage_hyperv_plan_build(uint8_t present, uint8_t configured,
                                   uint8_t enabled, uint8_t phase,
                                   uint8_t bus_prepared,
                                   uint8_t bus_connected,
                                   uint8_t offer_cached,
                                   uint8_t hybrid_prepare_allowed,
                                   int boot_services_active,
                                   int uses_firmware,
                                   struct x64_storage_hyperv_plan *out);
const char *x64_storage_hyperv_gate_label(uint8_t gate_state);
const char *x64_storage_hyperv_action_label(uint8_t action);

#endif /* ARCH_X86_64_STORAGE_RUNTIME_HYPERV_PLAN_H */
