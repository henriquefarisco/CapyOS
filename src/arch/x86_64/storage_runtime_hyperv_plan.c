#include "arch/x86_64/storage_runtime_hyperv_plan.h"

#include "drivers/storage/storvsc_runtime.h"

void x64_storage_hyperv_plan_build(uint8_t present, uint8_t configured,
                                   uint8_t enabled, uint8_t phase,
                                   uint8_t bus_prepared,
                                   uint8_t bus_connected,
                                   uint8_t offer_cached,
                                   uint8_t hybrid_prepare_allowed,
                                   int boot_services_active,
                                   int uses_firmware,
                                   struct x64_storage_hyperv_plan *out) {
  if (!out) {
    return;
  }

  out->gate_state = X64_STORAGE_HYPERV_GATE_INVALID;
  out->next_action = X64_STORAGE_HYPERV_ACTION_INVALID;

  if (!present) {
    return;
  }
  if (!configured) {
    out->gate_state = X64_STORAGE_HYPERV_GATE_WAIT_RUNTIME;
    out->next_action = X64_STORAGE_HYPERV_ACTION_WAIT_RUNTIME;
    return;
  }
  if (boot_services_active) {
    if (hybrid_prepare_allowed && !bus_prepared) {
      out->gate_state = X64_STORAGE_HYPERV_GATE_PREPARE_BUS;
      out->next_action = X64_STORAGE_HYPERV_ACTION_PREPARE_BUS;
      return;
    }
    out->gate_state = X64_STORAGE_HYPERV_GATE_WAIT_PLATFORM;
    out->next_action = X64_STORAGE_HYPERV_ACTION_WAIT_PLATFORM;
    return;
  }
  if (uses_firmware && !hybrid_prepare_allowed) {
    out->gate_state = X64_STORAGE_HYPERV_GATE_WAIT_PLATFORM;
    out->next_action = X64_STORAGE_HYPERV_ACTION_WAIT_PLATFORM;
    return;
  }
  if (!bus_prepared) {
    out->gate_state = X64_STORAGE_HYPERV_GATE_PREPARE_BUS;
    out->next_action = X64_STORAGE_HYPERV_ACTION_PREPARE_BUS;
    return;
  }
  if (!bus_connected) {
    out->gate_state = X64_STORAGE_HYPERV_GATE_WAIT_BUS;
    out->next_action = X64_STORAGE_HYPERV_ACTION_WAIT_BUS;
    return;
  }
  if (!offer_cached) {
    out->gate_state = X64_STORAGE_HYPERV_GATE_WAIT_OFFER;
    out->next_action = X64_STORAGE_HYPERV_ACTION_WAIT_OFFER;
    return;
  }

  out->gate_state = X64_STORAGE_HYPERV_GATE_OPEN;
  if (!enabled) {
    out->next_action = X64_STORAGE_HYPERV_ACTION_ENABLE_PROBE;
    return;
  }
  if (phase == STORVSC_RUNTIME_PROBE) {
    out->next_action = X64_STORAGE_HYPERV_ACTION_STEP_PROBE;
    return;
  }
  if (phase == STORVSC_RUNTIME_CHANNEL || phase == STORVSC_RUNTIME_CONTROL) {
    out->next_action = X64_STORAGE_HYPERV_ACTION_STEP_RUNTIME;
    return;
  }
  out->next_action = X64_STORAGE_HYPERV_ACTION_NOOP;
}

const char *x64_storage_hyperv_gate_label(uint8_t gate_state) {
  switch (gate_state) {
  case X64_STORAGE_HYPERV_GATE_WAIT_PLATFORM:
    return "wait-platform";
  case X64_STORAGE_HYPERV_GATE_PREPARE_BUS:
    return "prepare-bus";
  case X64_STORAGE_HYPERV_GATE_WAIT_BUS:
    return "wait-bus";
  case X64_STORAGE_HYPERV_GATE_WAIT_OFFER:
    return "wait-offer";
  case X64_STORAGE_HYPERV_GATE_WAIT_RUNTIME:
    return "wait-runtime";
  case X64_STORAGE_HYPERV_GATE_OPEN:
    return "open";
  default:
    return "invalid";
  }
}

const char *x64_storage_hyperv_action_label(uint8_t action) {
  switch (action) {
  case X64_STORAGE_HYPERV_ACTION_WAIT_PLATFORM:
    return "wait-platform";
  case X64_STORAGE_HYPERV_ACTION_PREPARE_BUS:
    return "prepare-bus";
  case X64_STORAGE_HYPERV_ACTION_WAIT_BUS:
    return "wait-bus";
  case X64_STORAGE_HYPERV_ACTION_WAIT_OFFER:
    return "wait-offer";
  case X64_STORAGE_HYPERV_ACTION_WAIT_RUNTIME:
    return "wait-runtime";
  case X64_STORAGE_HYPERV_ACTION_ENABLE_PROBE:
    return "enable-probe";
  case X64_STORAGE_HYPERV_ACTION_STEP_PROBE:
    return "step-probe";
  case X64_STORAGE_HYPERV_ACTION_STEP_RUNTIME:
    return "step-runtime";
  case X64_STORAGE_HYPERV_ACTION_NOOP:
    return "noop";
  default:
    return "invalid";
  }
}
