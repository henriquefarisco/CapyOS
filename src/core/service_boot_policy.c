#include "core/service_boot_policy.h"

#include "core/service_manager.h"

static uint32_t normalize_target(uint32_t requested_target) {
  if (requested_target >= SYSTEM_SERVICE_TARGET_COUNT) {
    return SYSTEM_SERVICE_TARGET_NETWORK;
  }
  return requested_target;
}

void service_boot_policy_evaluate(
    const struct system_service_boot_policy_input *input,
    struct system_service_boot_policy_decision *out) {
  uint32_t requested_target = SYSTEM_SERVICE_TARGET_NETWORK;

  if (!out) {
    return;
  }

  out->bootstrap_target = SYSTEM_SERVICE_TARGET_CORE;
  out->requested_target = SYSTEM_SERVICE_TARGET_NETWORK;
  out->final_target = SYSTEM_SERVICE_TARGET_NETWORK;
  out->degraded = 0u;
  out->forced_maintenance = 0u;
  out->forced_core = 0u;
  out->reason = SYSTEM_SERVICE_BOOT_POLICY_REQUESTED;

  if (!input) {
    return;
  }

  requested_target = normalize_target(input->requested_target);
  out->requested_target = requested_target;
  out->final_target = requested_target;

  if (!input->shell_runtime_ready &&
      requested_target != SYSTEM_SERVICE_TARGET_MAINTENANCE) {
    out->final_target = SYSTEM_SERVICE_TARGET_MAINTENANCE;
    out->degraded = 1u;
    out->forced_maintenance = 1u;
    out->reason = SYSTEM_SERVICE_BOOT_POLICY_STORAGE_RUNTIME_UNAVAILABLE;
    return;
  }

  if (!input->validated_storage_ready &&
      requested_target != SYSTEM_SERVICE_TARGET_MAINTENANCE) {
    out->final_target = SYSTEM_SERVICE_TARGET_MAINTENANCE;
    out->degraded = 1u;
    out->forced_maintenance = 1u;
    out->reason = SYSTEM_SERVICE_BOOT_POLICY_VALIDATED_STORAGE_UNAVAILABLE;
    return;
  }

  if ((requested_target == SYSTEM_SERVICE_TARGET_NETWORK ||
       requested_target == SYSTEM_SERVICE_TARGET_FULL) &&
      !input->network_status_available) {
    out->final_target = SYSTEM_SERVICE_TARGET_CORE;
    out->degraded = 1u;
    out->forced_core = 1u;
    out->reason = SYSTEM_SERVICE_BOOT_POLICY_NETWORK_STATUS_UNAVAILABLE;
    return;
  }

  if ((requested_target == SYSTEM_SERVICE_TARGET_NETWORK ||
       requested_target == SYSTEM_SERVICE_TARGET_FULL) &&
      !input->validated_network_supported) {
    out->final_target = SYSTEM_SERVICE_TARGET_CORE;
    out->degraded = 1u;
    out->forced_core = 1u;
    out->reason = SYSTEM_SERVICE_BOOT_POLICY_VALIDATED_NETWORK_UNAVAILABLE;
    return;
  }
}

const char *service_boot_policy_reason_label(uint8_t reason) {
  switch (reason) {
  case SYSTEM_SERVICE_BOOT_POLICY_STORAGE_RUNTIME_UNAVAILABLE:
    return "storage-runtime-unavailable";
  case SYSTEM_SERVICE_BOOT_POLICY_VALIDATED_STORAGE_UNAVAILABLE:
    return "validated-storage-unavailable";
  case SYSTEM_SERVICE_BOOT_POLICY_NETWORK_STATUS_UNAVAILABLE:
    return "network-status-unavailable";
  case SYSTEM_SERVICE_BOOT_POLICY_VALIDATED_NETWORK_UNAVAILABLE:
    return "validated-network-unavailable";
  default:
    return "requested";
  }
}

const char *service_boot_policy_reason_summary(uint8_t reason) {
  switch (reason) {
  case SYSTEM_SERVICE_BOOT_POLICY_STORAGE_RUNTIME_UNAVAILABLE:
    return "Boot policy forced maintenance because the storage runtime is unavailable";
  case SYSTEM_SERVICE_BOOT_POLICY_VALIDATED_STORAGE_UNAVAILABLE:
    return "Boot policy forced maintenance because no validated storage backend was found";
  case SYSTEM_SERVICE_BOOT_POLICY_NETWORK_STATUS_UNAVAILABLE:
    return "Boot policy downgraded to core because network status is unavailable";
  case SYSTEM_SERVICE_BOOT_POLICY_VALIDATED_NETWORK_UNAVAILABLE:
    return "Boot policy downgraded to core because no validated network runtime was found";
  default:
    return "Boot policy preserved the requested target";
  }
}
