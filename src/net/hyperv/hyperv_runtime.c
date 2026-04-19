#include "arch/x86_64/framebuffer_console.h"
#include "net/hyperv_runtime.h"
#include "net/hyperv_runtime_policy.h"

#include "kernel/log/klog.h"
#include "core/version.h"
#include "drivers/hyperv/hyperv.h"
#include "drivers/net/netvsc_vmbus.h"


static void hyperv_runtime_zero(void *ptr, uint32_t len) {
  uint8_t *p = (uint8_t *)ptr;
  if (!p) {
    return;
  }
  for (uint32_t i = 0; i < len; ++i) {
    p[i] = 0;
  }
}

static void apply_snapshot(struct net_hyperv_runtime_state *state,
                           const struct net_hyperv_runtime_snapshot *snapshot) {
  if (!state || !snapshot) {
    return;
  }

  state->offer_ready = snapshot->offer_ready;
  state->vmbus_stage = snapshot->vmbus_stage;
  state->stage = snapshot->stage;
  state->bus_prepared = snapshot->bus_prepared;
  state->channel_ready = snapshot->channel_ready;
  state->bus_connected = snapshot->bus_connected;
  state->runtime_configured = snapshot->runtime_configured;
  state->runtime_enabled = snapshot->runtime_enabled;
  state->runtime_phase = snapshot->runtime_phase;
  state->last_error = snapshot->last_error;
  if (snapshot->offer_ready) {
    state->offer = snapshot->offer;
  } else {
    hyperv_runtime_zero(&state->offer, sizeof(state->offer));
  }
}

static void log_runtime_checkpoint(const struct net_hyperv_runtime_state *state) {
  static uint8_t last_stage = 0xFFu;
  static uint8_t last_action = 0xFFu;
  static int32_t last_result = 0x7FFFFFFF;
  static uint32_t last_relid = 0xFFFFFFFFu;
  static uint32_t last_conn = 0xFFFFFFFFu;

  if (!state) {
    return;
  }
  if (state->stage == last_stage && state->last_action == last_action &&
      state->last_result == last_result &&
      state->offer.child_relid == last_relid &&
      state->offer.connection_id == last_conn) {
    return;
  }

  fbcon_print("[netvsc] build=");
  fbcon_print(CAPYOS_VERSION_FULL);
  fbcon_print(" feature=");
  fbcon_print(CAPYOS_FEATURE_HYPERV_RUNTIME);
  fbcon_print(" stage=");
  fbcon_print(hyperv_vmbus_stage_label(state->stage));
  fbcon_print(" action=0x");
  fbcon_print_hex((uint64_t)state->last_action);
  fbcon_print(" result=0x");
  fbcon_print_hex((uint64_t)(uint32_t)state->last_result);
  fbcon_print(" relid=0x");
  fbcon_print_hex((uint64_t)state->offer.child_relid);
  fbcon_print(" conn=0x");
  fbcon_print_hex((uint64_t)state->offer.connection_id);
  fbcon_print("\n");

  last_stage = state->stage;
  last_action = state->last_action;
  last_result = state->last_result;
  last_relid = state->offer.child_relid;
  last_conn = state->offer.connection_id;

  /* Mirror to persistent klog */
  klog_hex(KLOG_INFO, "[hvrt] stage=", (uint64_t)state->stage);
  klog_hex(KLOG_INFO, "[hvrt] action=", (uint64_t)state->last_action);
  klog_hex(KLOG_INFO, "[hvrt] result=", (uint64_t)(uint32_t)state->last_result);
}

void net_hyperv_runtime_state_init(struct net_hyperv_runtime_state *state) {
  if (!state) {
    return;
  }
  hyperv_runtime_zero(state, sizeof(*state));
  netvsc_runtime_init(&state->controller);
  state->last_action = NET_HYPERV_RUNTIME_ACTION_INVALID_UNINITIALIZED;
}

int net_hyperv_runtime_state_configure(const struct net_nic_probe *nic,
                                       struct net_hyperv_runtime_state *state) {
  struct net_hyperv_runtime_snapshot snapshot;
  struct netvsc_backend_ops ops;

  if (!nic || !state || nic->kind != NET_NIC_KIND_HYPERV_NETVSC) {
    return -1;
  }
  netvsc_vmbus_ops_init(&ops);
  if (netvsc_runtime_configure(&state->controller, nic, &ops) != 0) {
    return -1;
  }
  netvsc_runtime_set_enabled(&state->controller, 0);
  state->last_action = NET_HYPERV_RUNTIME_ACTION_SNAPSHOT_ONLY;
  state->last_result = 0;
  net_hyperv_runtime_snapshot(nic, state, &snapshot);
  apply_snapshot(state, &snapshot);
  log_runtime_checkpoint(state);
  return 0;
}

void net_hyperv_runtime_state_apply_nic(
    const struct net_hyperv_runtime_state *state, struct net_nic_probe *nic) {
  if (!state || !nic || nic->kind != NET_NIC_KIND_HYPERV_NETVSC) {
    return;
  }
  if (state->offer_ready) {
    nic->vmbus_relid = state->offer.child_relid;
    nic->vmbus_connection_id = state->offer.connection_id;
    nic->vmbus_dedicated_interrupt = state->offer.is_dedicated_interrupt;
  } else {
    nic->vmbus_relid = 0u;
    nic->vmbus_connection_id = 0u;
    nic->vmbus_dedicated_interrupt = 0u;
  }
}

void net_hyperv_runtime_snapshot(
    const struct net_nic_probe *nic,
    const struct net_hyperv_runtime_state *state,
    struct net_hyperv_runtime_snapshot *out) {
  struct netvsc_controller_status runtime_status;
  struct vmbus_offer_info cached_offer;

  if (!out) {
    return;
  }

  hyperv_runtime_zero(out, sizeof(*out));
  if (!nic || nic->kind != NET_NIC_KIND_HYPERV_NETVSC) {
    return;
  }

  out->bus_prepared = (uint8_t)(vmbus_runtime_prepared() ? 1 : 0);
  out->bus_connected = (uint8_t)(vmbus_runtime_connected() ? 1 : 0);
  out->vmbus_stage = vmbus_runtime_stage();
  if (netvsc_vmbus_offer_cached(&cached_offer) == 0) {
    out->offer_ready = 1u;
    out->offer = cached_offer;
    if (out->vmbus_stage < HYPERV_VMBUS_STAGE_OFFERS) {
      out->vmbus_stage = HYPERV_VMBUS_STAGE_OFFERS;
    }
  }
  out->stage = out->vmbus_stage;

  if (!state ||
      netvsc_runtime_controller_status(&state->controller, &runtime_status) !=
          0) {
    return;
  }

  out->offer_ready = (uint8_t)(out->offer_ready || runtime_status.offer_ready);
  out->channel_ready = runtime_status.channel_ready;
  out->runtime_configured = runtime_status.configured;
  out->runtime_enabled = runtime_status.enabled;
  out->runtime_phase = runtime_status.phase;
  out->last_error = runtime_status.last_error;
  if (runtime_status.offer_ready) {
    out->offer_ready = 1u;
    out->offer = runtime_status.offer;
  }
  out->stage = hyperv_runtime_stage_for(
      out->vmbus_stage, runtime_status.configured, out->offer_ready,
      runtime_status.channel_ready, runtime_status.phase,
      runtime_status.last_error);
  if (out->offer_ready && out->vmbus_stage < HYPERV_VMBUS_STAGE_OFFERS) {
    out->vmbus_stage = HYPERV_VMBUS_STAGE_OFFERS;
    out->stage = hyperv_runtime_stage_for(
        out->vmbus_stage, runtime_status.configured, out->offer_ready,
        runtime_status.channel_ready, runtime_status.phase,
        runtime_status.last_error);
  }
}

int net_hyperv_runtime_state_refresh(uint8_t initialized, uint8_t ready,
                                     const struct net_nic_probe *nic,
                                     struct net_hyperv_runtime_state *state) {
  struct net_hyperv_runtime_policy policy;
  struct vmbus_offer_info cached_offer;
  struct net_hyperv_runtime_snapshot snapshot;
  int rc = 0;

  net_hyperv_runtime_snapshot(nic, state, &snapshot);
  apply_snapshot(state, &snapshot);
  net_hyperv_runtime_policy_plan(initialized, ready, nic, &snapshot, &policy);
  if (state) {
    state->last_action = policy.action;
    state->refresh_attempts += 1u;
  }
  switch (policy.action) {
  case NET_HYPERV_RUNTIME_ACTION_INVALID_UNINITIALIZED:
    if (state) {
      state->last_result = -1;
    }
    return -1;
  case NET_HYPERV_RUNTIME_ACTION_SNAPSHOT_ONLY:
      if (state) {
        state->last_result = 0;
        log_runtime_checkpoint(state);
      }
      return 0;
  case NET_HYPERV_RUNTIME_ACTION_INVALID_KIND:
    if (state) {
      state->last_result = -2;
    }
    return -2;
  case NET_HYPERV_RUNTIME_ACTION_INVALID_UNCONFIGURED:
    if (state) {
      state->last_result = -3;
    }
    return -3;
  case NET_HYPERV_RUNTIME_ACTION_REFRESH_OFFER_CACHE:
    if (netvsc_vmbus_offer_cached(&cached_offer) != 0) {
      if (netvsc_vmbus_offer_refresh_connected(&cached_offer) != 0) {
        net_hyperv_runtime_snapshot(nic, state, &snapshot);
        apply_snapshot(state, &snapshot);
        if (state) {
          state->last_result = 0;
        }
        return 0;
      }
      net_hyperv_runtime_snapshot(nic, state, &snapshot);
      apply_snapshot(state, &snapshot);
      if (state) {
        state->last_result = 1;
        state->refresh_changes += 1u;
        log_runtime_checkpoint(state);
      }
      return 1;
    }
    net_hyperv_runtime_snapshot(nic, state, &snapshot);
    apply_snapshot(state, &snapshot);
    if (state) {
      state->last_result = 1;
      state->refresh_changes += 1u;
      log_runtime_checkpoint(state);
    }
    return 1;
  case NET_HYPERV_RUNTIME_ACTION_ENABLE_CONTROLLER:
    netvsc_runtime_set_enabled(&state->controller, 1);
    net_hyperv_runtime_snapshot(nic, state, &snapshot);
    apply_snapshot(state, &snapshot);
    if (state) {
      state->last_result = 1;
      state->refresh_changes += 1u;
    }
    return 1;
  case NET_HYPERV_RUNTIME_ACTION_STEP_CONTROLLER:
    break;
  default:
    if (state) {
      state->last_result = -1;
    }
    return -1;
  }

  rc = netvsc_runtime_step(&state->controller);
  if (rc < 0) {
    netvsc_runtime_degrade_passive(&state->controller);
  }
  net_hyperv_runtime_snapshot(nic, state, &snapshot);
  apply_snapshot(state, &snapshot);
  if (state) {
    state->last_result = rc;
    if (rc > 0) {
      state->refresh_changes += 1u;
    }
    log_runtime_checkpoint(state);
  }
  return rc;
}
