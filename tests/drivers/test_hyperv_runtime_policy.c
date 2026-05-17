#include <stdio.h>
#include <string.h>

#include "drivers/net/net_probe.h"
#include "net/hyperv_runtime_policy.h"

static int expect_action(const char *name,
                         enum net_hyperv_runtime_action expected,
                         uint8_t initialized, uint8_t ready,
                         const struct net_nic_probe *nic,
                         const struct net_hyperv_runtime_snapshot *snapshot) {
  struct net_hyperv_runtime_policy policy;
  net_hyperv_runtime_policy_plan(initialized, ready, nic, snapshot, &policy);
  if (policy.action != expected) {
    printf("[hyperv_runtime_policy] %s expected %u got %u\n", name,
           (unsigned)expected, (unsigned)policy.action);
    return 1;
  }
  return 0;
}

int run_hyperv_runtime_policy_tests(void) {
  int fails = 0;
  struct net_nic_probe nic;
  struct net_hyperv_runtime_snapshot snapshot;

  memset(&nic, 0, sizeof(nic));
  memset(&snapshot, 0, sizeof(snapshot));

  fails += expect_action("uninitialized",
                         NET_HYPERV_RUNTIME_ACTION_INVALID_UNINITIALIZED, 0, 0,
                         &nic, &snapshot);

  nic.found = 1u;
  nic.kind = NET_NIC_KIND_E1000;
  fails += expect_action("unsupported-kind",
                         NET_HYPERV_RUNTIME_ACTION_INVALID_KIND, 1, 0, &nic,
                         &snapshot);

  nic.kind = NET_NIC_KIND_HYPERV_NETVSC;
  fails += expect_action("unconfigured",
                         NET_HYPERV_RUNTIME_ACTION_INVALID_UNCONFIGURED, 1, 0,
                         &nic, &snapshot);

  snapshot.runtime_configured = 1u;
  fails += expect_action("refresh-offer-cache",
                         NET_HYPERV_RUNTIME_ACTION_REFRESH_OFFER_CACHE, 1, 0,
                         &nic, &snapshot);

  snapshot.offer_ready = 1u;
  fails += expect_action("enable-controller",
                         NET_HYPERV_RUNTIME_ACTION_ENABLE_CONTROLLER, 1, 0,
                         &nic, &snapshot);

  snapshot.runtime_enabled = 1u;
  fails += expect_action("step-controller",
                         NET_HYPERV_RUNTIME_ACTION_STEP_CONTROLLER, 1, 0, &nic,
                         &snapshot);

  fails += expect_action("ready-noop", NET_HYPERV_RUNTIME_ACTION_SNAPSHOT_ONLY,
                         1, 1, &nic, &snapshot);

  if (fails == 0) {
    printf("[tests] hyperv_runtime_policy OK\n");
  }
  return fails;
}
