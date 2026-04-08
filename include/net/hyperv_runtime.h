#ifndef NET_HYPERV_RUNTIME_H
#define NET_HYPERV_RUNTIME_H

#include "drivers/net/net_probe.h"
#include "drivers/net/netvsc_runtime.h"

#include <stdint.h>

struct net_hyperv_runtime_state {
  uint8_t vmbus_stage;
  uint8_t stage;
  uint8_t offer_ready;
  uint8_t bus_prepared;
  uint8_t channel_ready;
  uint8_t bus_connected;
  uint8_t runtime_configured;
  uint8_t runtime_enabled;
  uint8_t runtime_phase;
  uint8_t last_action;
  int32_t last_error;
  int32_t last_result;
  uint32_t refresh_attempts;
  uint32_t refresh_changes;
  struct vmbus_offer_info offer;
  struct netvsc_runtime_state controller;
};

struct net_hyperv_runtime_snapshot {
  uint8_t vmbus_stage;
  uint8_t stage;
  uint8_t offer_ready;
  uint8_t bus_prepared;
  uint8_t channel_ready;
  uint8_t bus_connected;
  uint8_t runtime_configured;
  uint8_t runtime_enabled;
  uint8_t runtime_phase;
  int32_t last_error;
  struct vmbus_offer_info offer;
};

void net_hyperv_runtime_state_init(struct net_hyperv_runtime_state *state);
int net_hyperv_runtime_state_configure(const struct net_nic_probe *nic,
                                       struct net_hyperv_runtime_state *state);
void net_hyperv_runtime_state_apply_nic(
    const struct net_hyperv_runtime_state *state, struct net_nic_probe *nic);
void net_hyperv_runtime_snapshot(
    const struct net_nic_probe *nic,
    const struct net_hyperv_runtime_state *state,
    struct net_hyperv_runtime_snapshot *out);
int net_hyperv_runtime_state_refresh(uint8_t initialized, uint8_t ready,
                                     const struct net_nic_probe *nic,
                                     struct net_hyperv_runtime_state *state);

#endif /* NET_HYPERV_RUNTIME_H */
