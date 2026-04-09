#include "net/hyperv_platform_diag.h"

#include "arch/x86_64/storage_runtime.h"
#include "drivers/net/net_probe.h"
#include "net/hyperv_runtime_gate.h"
#include "net/hyperv_runtime_policy.h"

int net_hyperv_platform_is_blocked(
    const struct system_runtime_platform *platform) {
  if (!platform) {
    return 0;
  }
  return platform->boot_services_active || platform->firmware_block_io_active ||
         platform->hybrid_boot;
}

int net_hyperv_platform_blocked(struct system_runtime_platform *platform) {
  if (!platform) {
    return 0;
  }
  system_runtime_platform_get(platform);
  return net_hyperv_platform_is_blocked(platform);
}

const char *net_hyperv_stage_label(const struct net_stack_status *st) {
  if (!st || st->nic.kind != NET_NIC_KIND_HYPERV_NETVSC) {
    return "none";
  }
  return hyperv_vmbus_stage_label(st->hyperv_stage);
}

const char *net_hyperv_bus_label(const struct net_stack_status *st) {
  if (!st || st->nic.kind != NET_NIC_KIND_HYPERV_NETVSC) {
    return "n/a";
  }
  return hyperv_vmbus_stage_label(st->hyperv_vmbus_stage);
}

const char *net_hyperv_runtime_phase_label(uint8_t phase) {
  switch (phase) {
  case 1:
    return "disabled";
  case 2:
    return "probe";
  case 3:
    return "channel";
  case 4:
    return "control";
  case 5:
    return "ready";
  case 6:
    return "failed";
  default:
    return "unconfigured";
  }
}

const char *net_hyperv_refresh_action_label(uint8_t action) {
  switch (action) {
  case NET_HYPERV_RUNTIME_ACTION_INVALID_UNINITIALIZED:
    return "invalid-init";
  case NET_HYPERV_RUNTIME_ACTION_SNAPSHOT_ONLY:
    return "noop";
  case NET_HYPERV_RUNTIME_ACTION_INVALID_KIND:
    return "n/a";
  case NET_HYPERV_RUNTIME_ACTION_INVALID_UNCONFIGURED:
    return "unconfigured";
  case NET_HYPERV_RUNTIME_ACTION_REFRESH_OFFER_CACHE:
    return "cache-offer";
  case NET_HYPERV_RUNTIME_ACTION_ENABLE_CONTROLLER:
    return "enable";
  case NET_HYPERV_RUNTIME_ACTION_STEP_CONTROLLER:
    return "step";
  default:
    return "unknown";
  }
}

const char *net_hyperv_effective_next_label(
    const struct net_stack_status *st,
    const struct system_runtime_platform *platform) {
  if (!st || st->nic.kind != NET_NIC_KIND_HYPERV_NETVSC) {
    return "n/a";
  }
  switch (st->hyperv_gate_state) {
  case NET_HYPERV_RUNTIME_GATE_WAIT_PLATFORM:
    if (platform && platform->exit_boot_services_gate !=
                        SYSTEM_EXIT_BOOT_SERVICES_GATE_UNKNOWN) {
      return system_exit_boot_services_gate_label(
          platform->exit_boot_services_gate);
    }
    return "wait-platform";
  case NET_HYPERV_RUNTIME_GATE_WAIT_STORAGE:
    return "wait-storage";
  case NET_HYPERV_RUNTIME_GATE_WAIT_BUS:
    return "wait-bus";
  case NET_HYPERV_RUNTIME_GATE_WAIT_RUNTIME:
    return "wait-runtime";
  case NET_HYPERV_RUNTIME_GATE_OPEN:
    return net_hyperv_refresh_action_label(st->hyperv_refresh_action);
  default:
    return "invalid";
  }
}

const char *net_hyperv_offer_cache_label(const struct net_stack_status *st) {
  if (!st || st->nic.kind != NET_NIC_KIND_HYPERV_NETVSC) {
    return "n/a";
  }
  return st->hyperv_offer_ready ? "ready" : "miss";
}

const char *net_hyperv_block_label(
    const struct net_stack_status *st,
    const struct system_runtime_platform *platform) {
  int platform_blocked = net_hyperv_platform_is_blocked(platform);

  if (!st || st->nic.kind != NET_NIC_KIND_HYPERV_NETVSC) {
    return "n/a";
  }
  if (platform_blocked) {
    if (platform && platform->boot_services_active) {
      switch (platform->exit_boot_services_gate) {
      case SYSTEM_EXIT_BOOT_SERVICES_GATE_WAIT_INPUT:
        return "boot-services-input";
      case SYSTEM_EXIT_BOOT_SERVICES_GATE_WAIT_STORAGE_DEVICE:
        return "storage-device-missing";
      case SYSTEM_EXIT_BOOT_SERVICES_GATE_WAIT_STORAGE_FIRMWARE:
        return "storage-firmware-active";
      case SYSTEM_EXIT_BOOT_SERVICES_GATE_WAIT_CONTRACT:
        return "exit-contract-missing";
      case SYSTEM_EXIT_BOOT_SERVICES_GATE_FAILED:
        return "exit-failed";
      case SYSTEM_EXIT_BOOT_SERVICES_GATE_READY:
        return "boot-services-active";
      default:
        break;
      }
    }
    if (platform && platform->firmware_block_io_active) {
      return "storage-firmware-active";
    }
    return "platform-hybrid";
  }
  if (platform && !platform->native_storage_ready &&
      !platform->synthetic_storage_ready) {
    return "storage-not-ready";
  }
  if (!st->hyperv_bus_prepared) {
    return "vmbus-unprepared";
  }
  if (!st->hyperv_bus_connected) {
    return "vmbus-disconnected";
  }
  if (!st->hyperv_offer_ready) {
    return "offer-miss";
  }
  if (!st->hyperv_runtime_enabled) {
    return "policy-disabled";
  }
  if (st->hyperv_runtime_phase == 5u) {
    return "none";
  }
  if (st->hyperv_runtime_phase == 6u) {
    return "failed";
  }
  return "in-progress";
}

const char *net_hyperv_storage_bus_label(void) {
  if (x64_storage_runtime_hyperv_bus_connected()) {
    return "connected";
  }
  if (x64_storage_runtime_hyperv_bus_prepared()) {
    return "prepared";
  }
  return "disconnected";
}

const char *net_hyperv_storage_cache_label(void) {
  return x64_storage_runtime_hyperv_offer_cached() ? "ready" : "miss";
}

const char *net_hyperv_storage_gate_label(
    const struct system_runtime_platform *runtime_platform) {
  if (!runtime_platform) {
    return "n/a";
  }
  return x64_storage_runtime_hyperv_gate_label(
      runtime_platform->boot_services_active ? 1 : 0);
}

const char *net_hyperv_storage_next_label(
    const struct system_runtime_platform *runtime_platform) {
  if (!runtime_platform) {
    return "n/a";
  }
  return x64_storage_runtime_hyperv_next_action_label(
      runtime_platform->boot_services_active ? 1 : 0);
}

const char *net_hyperv_storage_block_label(
    const struct system_runtime_platform *runtime_platform) {
  if (!runtime_platform) {
    return "n/a";
  }
  return x64_storage_runtime_hyperv_block_reason(
      runtime_platform->boot_services_active ? 1 : 0);
}
