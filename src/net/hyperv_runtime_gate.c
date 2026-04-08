#include "net/hyperv_runtime_gate.h"

static int net_hyperv_runtime_gate_platform_blocked(
    const struct system_runtime_platform *platform) {
  if (!platform) {
    return 0;
  }
  return platform->boot_services_active || platform->firmware_block_io_active ||
         platform->hybrid_boot;
}

uint8_t net_hyperv_runtime_gate_state_for(
    uint8_t nic_found, uint8_t nic_kind, uint8_t runtime_configured,
    uint8_t bus_connected, const struct system_runtime_platform *platform) {
  if (!nic_found || nic_kind != NET_NIC_KIND_HYPERV_NETVSC) {
    return NET_HYPERV_RUNTIME_GATE_INVALID;
  }
  if (!runtime_configured) {
    return NET_HYPERV_RUNTIME_GATE_WAIT_RUNTIME;
  }
  if (net_hyperv_runtime_gate_platform_blocked(platform)) {
    return NET_HYPERV_RUNTIME_GATE_WAIT_PLATFORM;
  }
  if (platform && !platform->native_storage_ready &&
      !platform->synthetic_storage_ready) {
    return NET_HYPERV_RUNTIME_GATE_WAIT_STORAGE;
  }
  if (!bus_connected) {
    return NET_HYPERV_RUNTIME_GATE_WAIT_BUS;
  }
  return NET_HYPERV_RUNTIME_GATE_OPEN;
}

const char *net_hyperv_runtime_gate_label(uint8_t gate_state) {
  switch (gate_state) {
  case NET_HYPERV_RUNTIME_GATE_WAIT_PLATFORM:
    return "wait-platform";
  case NET_HYPERV_RUNTIME_GATE_WAIT_STORAGE:
    return "wait-storage";
  case NET_HYPERV_RUNTIME_GATE_WAIT_BUS:
    return "wait-bus";
  case NET_HYPERV_RUNTIME_GATE_WAIT_RUNTIME:
    return "wait-runtime";
  case NET_HYPERV_RUNTIME_GATE_OPEN:
    return "open";
  default:
    return "invalid";
  }
}
