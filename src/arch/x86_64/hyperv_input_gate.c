#include "arch/x86_64/hyperv_input_gate.h"

enum {
  HYPERV_INPUT_GATE_MAX_PROMOTION_ATTEMPTS = 3u,
};

uint8_t x64_hyperv_input_gate_state(const struct x64_input_runtime *runtime,
                                    int boot_services_active) {
  if (!runtime) {
    return SYSTEM_HYPERV_INPUT_GATE_UNKNOWN;
  }
  if (runtime->has_hyperv) {
    return SYSTEM_HYPERV_INPUT_GATE_ACTIVE;
  }
  if (!runtime->hyperv_deferred) {
    return SYSTEM_HYPERV_INPUT_GATE_OFF;
  }
  if (runtime->hyperv_promotion_attempts >=
      HYPERV_INPUT_GATE_MAX_PROMOTION_ATTEMPTS) {
    return SYSTEM_HYPERV_INPUT_GATE_FAILED;
  }
  if (boot_services_active) {
    return SYSTEM_HYPERV_INPUT_GATE_WAIT_BOOT_SERVICES;
  }
  if (runtime->hyperv_promotion_attempted) {
    return SYSTEM_HYPERV_INPUT_GATE_RETRY;
  }
  return SYSTEM_HYPERV_INPUT_GATE_READY;
}
