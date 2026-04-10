#include <stdio.h>

#include "net/hyperv_runtime_gate.h"

static int expect_gate(const char *name, uint8_t expected, uint8_t nic_found,
                       uint8_t nic_kind, uint8_t runtime_configured,
                       uint8_t bus_connected,
                       const struct system_runtime_platform *platform) {
  uint8_t actual = net_hyperv_runtime_gate_state_for(
      nic_found, nic_kind, runtime_configured, bus_connected, platform);
  if (actual != expected) {
    printf("[hyperv_runtime_gate] %s expected %u got %u\n", name,
           (unsigned)expected, (unsigned)actual);
    return 1;
  }
  return 0;
}

int run_hyperv_runtime_gate_tests(void) {
  int fails = 0;
  struct system_runtime_platform platform = {0};

  fails += expect_gate("invalid", NET_HYPERV_RUNTIME_GATE_INVALID, 0, 0, 0, 0,
                       &platform);

  fails += expect_gate("wait-runtime", NET_HYPERV_RUNTIME_GATE_WAIT_RUNTIME, 1,
                       NET_NIC_KIND_HYPERV_NETVSC, 0, 0, &platform);

  platform.boot_services_active = 1;
  fails += expect_gate("wait-platform",
                       NET_HYPERV_RUNTIME_GATE_WAIT_PLATFORM, 1,
                       NET_NIC_KIND_HYPERV_NETVSC, 1, 0, &platform);

  platform.boot_services_active = 0;
  platform.native_storage_ready = 0;
  platform.synthetic_storage_ready = 0;
  fails += expect_gate("wait-storage",
                       NET_HYPERV_RUNTIME_GATE_WAIT_STORAGE, 1,
                       NET_NIC_KIND_HYPERV_NETVSC, 1, 0, &platform);

  platform.synthetic_storage_ready = 1;
  fails += expect_gate("wait-bus-synth-ready", NET_HYPERV_RUNTIME_GATE_WAIT_BUS,
                       1, NET_NIC_KIND_HYPERV_NETVSC, 1, 0, &platform);

  platform.native_storage_ready = 1;
  fails += expect_gate("wait-bus", NET_HYPERV_RUNTIME_GATE_WAIT_BUS, 1,
                       NET_NIC_KIND_HYPERV_NETVSC, 1, 0, &platform);

  fails += expect_gate("open", NET_HYPERV_RUNTIME_GATE_OPEN, 1,
                       NET_NIC_KIND_HYPERV_NETVSC, 1, 1, &platform);

  if (fails == 0) {
    printf("[tests] hyperv_runtime_gate OK\n");
  }
  return fails;
}
