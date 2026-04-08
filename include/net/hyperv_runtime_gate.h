#ifndef NET_HYPERV_RUNTIME_GATE_H
#define NET_HYPERV_RUNTIME_GATE_H

#include "core/system_init.h"
#include "drivers/net/net_probe.h"

#include <stdint.h>

enum net_hyperv_runtime_gate_state {
  NET_HYPERV_RUNTIME_GATE_INVALID = 0,
  NET_HYPERV_RUNTIME_GATE_WAIT_PLATFORM,
  NET_HYPERV_RUNTIME_GATE_WAIT_STORAGE,
  NET_HYPERV_RUNTIME_GATE_WAIT_BUS,
  NET_HYPERV_RUNTIME_GATE_WAIT_RUNTIME,
  NET_HYPERV_RUNTIME_GATE_OPEN,
};

uint8_t net_hyperv_runtime_gate_state_for(
    uint8_t nic_found, uint8_t nic_kind, uint8_t runtime_configured,
    uint8_t bus_connected, const struct system_runtime_platform *platform);
const char *net_hyperv_runtime_gate_label(uint8_t gate_state);

#endif /* NET_HYPERV_RUNTIME_GATE_H */
