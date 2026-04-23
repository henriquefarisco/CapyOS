#include "net/hyperv_runtime_policy.h"

static void hyperv_runtime_policy_zero(struct net_hyperv_runtime_policy *out) {
  if (!out) {
    return;
  }
  out->action = NET_HYPERV_RUNTIME_ACTION_INVALID_UNINITIALIZED;
}

void net_hyperv_runtime_policy_plan(
    uint8_t initialized, uint8_t ready, const struct net_nic_probe *nic,
    const struct net_hyperv_runtime_snapshot *snapshot,
    struct net_hyperv_runtime_policy *out) {
  hyperv_runtime_policy_zero(out);
  if (!out) {
    return;
  }
  if (!initialized || !nic || !nic->found) {
    out->action = NET_HYPERV_RUNTIME_ACTION_INVALID_UNINITIALIZED;
    return;
  }
  if (ready) {
    out->action = NET_HYPERV_RUNTIME_ACTION_SNAPSHOT_ONLY;
    return;
  }
  if (nic->kind != NET_NIC_KIND_HYPERV_NETVSC) {
    out->action = NET_HYPERV_RUNTIME_ACTION_INVALID_KIND;
    return;
  }
  if (!snapshot || !snapshot->runtime_configured) {
    out->action = NET_HYPERV_RUNTIME_ACTION_INVALID_UNCONFIGURED;
    return;
  }
  if (!snapshot->runtime_enabled) {
    out->action = snapshot->offer_ready
                      ? NET_HYPERV_RUNTIME_ACTION_ENABLE_CONTROLLER
                      : NET_HYPERV_RUNTIME_ACTION_REFRESH_OFFER_CACHE;
    return;
  }
  out->action = NET_HYPERV_RUNTIME_ACTION_STEP_CONTROLLER;
}
