#ifndef NET_HYPERV_RUNTIME_POLICY_H
#define NET_HYPERV_RUNTIME_POLICY_H

#include "drivers/net/net_probe.h"
#include "net/hyperv_runtime.h"

#include <stdint.h>

enum net_hyperv_runtime_action {
  NET_HYPERV_RUNTIME_ACTION_INVALID_UNINITIALIZED = 0,
  NET_HYPERV_RUNTIME_ACTION_SNAPSHOT_ONLY,
  NET_HYPERV_RUNTIME_ACTION_INVALID_KIND,
  NET_HYPERV_RUNTIME_ACTION_INVALID_UNCONFIGURED,
  NET_HYPERV_RUNTIME_ACTION_REFRESH_OFFER_CACHE,
  NET_HYPERV_RUNTIME_ACTION_ENABLE_CONTROLLER,
  NET_HYPERV_RUNTIME_ACTION_STEP_CONTROLLER,
};

struct net_hyperv_runtime_policy {
  uint8_t action;
};

void net_hyperv_runtime_policy_plan(
    uint8_t initialized, uint8_t ready, const struct net_nic_probe *nic,
    const struct net_hyperv_runtime_snapshot *snapshot,
    struct net_hyperv_runtime_policy *out);

#endif /* NET_HYPERV_RUNTIME_POLICY_H */
