#ifndef CORE_SERVICE_BOOT_POLICY_H
#define CORE_SERVICE_BOOT_POLICY_H

#include <stdint.h>

enum system_service_boot_policy_reason {
  SYSTEM_SERVICE_BOOT_POLICY_REQUESTED = 0,
  SYSTEM_SERVICE_BOOT_POLICY_STORAGE_RUNTIME_UNAVAILABLE,
  SYSTEM_SERVICE_BOOT_POLICY_VALIDATED_STORAGE_UNAVAILABLE,
  SYSTEM_SERVICE_BOOT_POLICY_NETWORK_STATUS_UNAVAILABLE,
  SYSTEM_SERVICE_BOOT_POLICY_VALIDATED_NETWORK_UNAVAILABLE
};

struct system_service_boot_policy_input {
  uint32_t requested_target;
  uint8_t shell_runtime_ready;
  uint8_t validated_storage_ready;
  uint8_t network_status_available;
  uint8_t validated_network_supported;
};

struct system_service_boot_policy_decision {
  uint32_t bootstrap_target;
  uint32_t requested_target;
  uint32_t final_target;
  uint8_t degraded;
  uint8_t forced_maintenance;
  uint8_t forced_core;
  uint8_t reason;
};

void service_boot_policy_evaluate(
    const struct system_service_boot_policy_input *input,
    struct system_service_boot_policy_decision *out);
const char *service_boot_policy_reason_label(uint8_t reason);
const char *service_boot_policy_reason_summary(uint8_t reason);

#endif
