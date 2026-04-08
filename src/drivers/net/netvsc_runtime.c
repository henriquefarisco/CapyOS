#include "drivers/net/netvsc_runtime.h"

static void netvsc_runtime_memzero(void *ptr, uint32_t len) {
  uint8_t *p = (uint8_t *)ptr;
  if (!p) {
    return;
  }
  for (uint32_t i = 0; i < len; ++i) {
    p[i] = 0;
  }
}

static void netvsc_runtime_sync_phase(struct netvsc_runtime_state *state) {
  if (!state) {
    return;
  }
  if (!state->configured) {
    state->phase = NETVSC_RUNTIME_UNCONFIGURED;
    return;
  }
  if (!state->enabled && !netvsc_backend_is_ready(&state->backend)) {
    state->phase = NETVSC_RUNTIME_DISABLED;
    return;
  }

  switch (state->backend.phase) {
  case NETVSC_BACKEND_PROBE:
    state->phase = NETVSC_RUNTIME_PROBE;
    break;
  case NETVSC_BACKEND_CHANNEL:
    state->phase = NETVSC_RUNTIME_CHANNEL;
    break;
  case NETVSC_BACKEND_CONTROL:
    state->phase = NETVSC_RUNTIME_CONTROL;
    break;
  case NETVSC_BACKEND_READY:
    state->phase = NETVSC_RUNTIME_READY;
    break;
  case NETVSC_BACKEND_FAILED:
  default:
    state->phase = NETVSC_RUNTIME_FAILED;
    break;
  }
}

void netvsc_runtime_init(struct netvsc_runtime_state *state) {
  if (!state) {
    return;
  }
  netvsc_runtime_memzero(state, sizeof(*state));
  state->phase = NETVSC_RUNTIME_UNCONFIGURED;
}

int netvsc_runtime_configure(struct netvsc_runtime_state *state,
                             const struct net_nic_probe *nic,
                             const struct netvsc_backend_ops *ops) {
  if (!state || !nic || !ops || nic->kind != NET_NIC_KIND_HYPERV_NETVSC) {
    return -1;
  }

  netvsc_backend_init(&state->backend);
  state->configured = 1u;
  state->enabled = 0u;
  state->last_error = 0;
  state->ops = *ops;
  netvsc_runtime_sync_phase(state);
  return 0;
}

void netvsc_runtime_set_enabled(struct netvsc_runtime_state *state, int enabled) {
  if (!state || !state->configured) {
    return;
  }
  state->enabled = enabled ? 1u : 0u;
  netvsc_runtime_sync_phase(state);
}

void netvsc_runtime_degrade_passive(struct netvsc_runtime_state *state) {
  struct netvsc_backend_ops ops;
  int32_t last_error = 0;

  if (!state || !state->configured) {
    return;
  }
  ops = state->ops;
  last_error = state->last_error ? state->last_error : state->backend.last_error;
  netvsc_backend_init(&state->backend);
  state->configured = 1u;
  state->enabled = 0u;
  state->ops = ops;
  state->last_error = last_error;
  netvsc_runtime_sync_phase(state);
}

int netvsc_runtime_step(struct netvsc_runtime_state *state) {
  int rc = 0;

  if (!state || !state->configured || !state->ops.query_offer ||
      !state->ops.open_channel || !state->ops.send_control ||
      !state->ops.recv_control) {
    return -1;
  }
  if (!state->enabled) {
    netvsc_runtime_sync_phase(state);
    return 0;
  }

  rc = netvsc_backend_step(&state->backend, &state->ops);
  if (rc < 0) {
    state->last_error = rc;
  } else if (state->backend.last_error != 0) {
    state->last_error = state->backend.last_error;
  }
  netvsc_runtime_sync_phase(state);
  return rc;
}

int netvsc_runtime_controller_status(const struct netvsc_runtime_state *state,
                                     struct netvsc_controller_status *out) {
  if (!state || !out) {
    return -1;
  }

  netvsc_runtime_memzero(out, sizeof(*out));
  out->configured = state->configured;
  out->enabled = state->enabled;
  out->ready = netvsc_backend_is_ready(&state->backend) ? 1u : 0u;
  out->phase = state->phase;
  out->last_error = state->last_error;
  out->offer_ready = state->backend.offer_ready;
  out->channel_ready = state->backend.channel_ready;
  out->offer = state->backend.offer;
  return 0;
}
