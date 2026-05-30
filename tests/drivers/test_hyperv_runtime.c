#include <stdio.h>
#include <string.h>

#include "drivers/hyperv/hyperv.h"
#include "drivers/net/netvsc_runtime.h"
#include "net/hyperv_runtime.h"

static struct vmbus_offer_info g_cached_offer;
static uint8_t g_vmbus_stage = HYPERV_VMBUS_STAGE_OFF;
static int g_vmbus_prepared = 0;
static int g_vmbus_connected = 0;
static int g_cached_offer_valid = 0;

static void test_reset(void) {
  memset(&g_cached_offer, 0, sizeof(g_cached_offer));
  g_vmbus_stage = HYPERV_VMBUS_STAGE_OFF;
  g_vmbus_prepared = 0;
  g_vmbus_connected = 0;
  g_cached_offer_valid = 0;
}

void fbcon_print(const char *s) { (void)s; }

void fbcon_print_hex(uint64_t val) { (void)val; }

int vmbus_runtime_prepared(void) { return g_vmbus_prepared; }

int vmbus_runtime_connected(void) { return g_vmbus_connected; }

uint8_t vmbus_runtime_stage(void) { return g_vmbus_stage; }

void netvsc_vmbus_ops_init(struct netvsc_backend_ops *out) {
  if (!out) {
    return;
  }
  memset(out, 0, sizeof(*out));
}

int netvsc_vmbus_offer_cached(struct vmbus_offer_info *out) {
  if (!g_cached_offer_valid || !out) {
    return -1;
  }
  *out = g_cached_offer;
  return 0;
}

int netvsc_vmbus_offer_refresh_connected(struct vmbus_offer_info *out) {
  return netvsc_vmbus_offer_cached(out);
}

int run_hyperv_runtime_tests(void) {
  int fails = 0;
  struct net_nic_probe nic;
  struct net_hyperv_runtime_state state;
  struct net_hyperv_runtime_snapshot snapshot;

  test_reset();
  memset(&nic, 0, sizeof(nic));
  memset(&state, 0, sizeof(state));
  memset(&snapshot, 0, sizeof(snapshot));

  nic.found = 1u;
  nic.kind = NET_NIC_KIND_HYPERV_NETVSC;
  g_vmbus_prepared = 1;
  g_vmbus_connected = 1;
  g_vmbus_stage = HYPERV_VMBUS_STAGE_CONTACT;
  g_cached_offer_valid = 1;
  g_cached_offer.child_relid = 41u;
  g_cached_offer.connection_id = 73u;
  state.controller.configured = 1u;
  state.controller.enabled = 0u;
  state.controller.phase = NETVSC_RUNTIME_DISABLED;

  net_hyperv_runtime_snapshot(&nic, &state, &snapshot);
  if (!snapshot.offer_ready || snapshot.offer.child_relid != 41u ||
      snapshot.offer.connection_id != 73u ||
      snapshot.vmbus_stage != HYPERV_VMBUS_STAGE_OFFERS ||
      snapshot.stage != HYPERV_VMBUS_STAGE_OFFERS) {
    printf("[hyperv_runtime] cached VMBus offer was not preserved in snapshot\n");
    fails++;
  }

  test_reset();
  memset(&nic, 0, sizeof(nic));
  memset(&state, 0, sizeof(state));
  memset(&snapshot, 0, sizeof(snapshot));

  nic.found = 1u;
  nic.kind = NET_NIC_KIND_HYPERV_NETVSC;
  g_vmbus_stage = HYPERV_VMBUS_STAGE_OFFERS;
  state.controller.configured = 1u;
  state.controller.enabled = 1u;
  state.controller.phase = NETVSC_RUNTIME_CONTROL;
  state.controller.backend.offer_ready = 1u;
  state.controller.backend.channel_ready = 1u;
  state.controller.backend.offer.child_relid = 99u;
  state.controller.backend.offer.connection_id = 144u;

  net_hyperv_runtime_snapshot(&nic, &state, &snapshot);
  if (!snapshot.offer_ready || snapshot.offer.child_relid != 99u ||
      snapshot.offer.connection_id != 144u) {
    printf("[hyperv_runtime] controller offer did not override snapshot state\n");
    fails++;
  }

  if (fails == 0) {
    printf("[tests] hyperv_runtime OK\n");
  }
  return fails;
}
