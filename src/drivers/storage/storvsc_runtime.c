#include "drivers/storage/storvsc_runtime.h"

static void storvsc_runtime_zero(void *dst, uint32_t len) {
  uint8_t *bytes = (uint8_t *)dst;
  if (!bytes) {
    return;
  }
  for (uint32_t i = 0; i < len; ++i) {
    bytes[i] = 0u;
  }
}

static void storvsc_runtime_sync_phase(struct storvsc_runtime_state *state) {
  if (!state) {
    return;
  }
  if (!state->configured) {
    state->phase = STORVSC_RUNTIME_UNCONFIGURED;
    return;
  }
  if (!state->enabled && !storvsc_backend_is_ready(&state->backend)) {
    state->phase = STORVSC_RUNTIME_DISABLED;
    return;
  }

  switch (state->backend.phase) {
  case STORVSC_BACKEND_PROBE:
    state->phase = STORVSC_RUNTIME_PROBE;
    break;
  case STORVSC_BACKEND_CHANNEL:
    state->phase = STORVSC_RUNTIME_CHANNEL;
    break;
  case STORVSC_BACKEND_CONTROL:
    state->phase = STORVSC_RUNTIME_CONTROL;
    break;
  case STORVSC_BACKEND_READY:
    state->phase = STORVSC_RUNTIME_READY;
    break;
  case STORVSC_BACKEND_FAILED:
  default:
    state->phase = STORVSC_RUNTIME_FAILED;
    break;
  }
}

void storvsc_runtime_init(struct storvsc_runtime_state *state) {
  if (!state) {
    return;
  }
  storvsc_runtime_zero(state, sizeof(*state));
  state->phase = STORVSC_RUNTIME_UNCONFIGURED;
}

int storvsc_runtime_configure(struct storvsc_runtime_state *state,
                              int hyperv_present,
                              const struct storvsc_backend_ops *ops) {
  if (!state || !ops || !hyperv_present) {
    return -1;
  }

  storvsc_backend_init(&state->backend);
  state->configured = 1u;
  state->enabled = 0u;
  state->last_error = 0;
  state->ops = *ops;
  storvsc_runtime_sync_phase(state);
  return 0;
}

void storvsc_runtime_set_enabled(struct storvsc_runtime_state *state, int enabled) {
  if (!state || !state->configured) {
    return;
  }
  state->enabled = enabled ? 1u : 0u;
  storvsc_runtime_sync_phase(state);
}

void storvsc_runtime_degrade_passive(struct storvsc_runtime_state *state) {
  struct storvsc_backend_ops ops;
  int32_t last_error = 0;

  if (!state || !state->configured) {
    return;
  }
  ops = state->ops;
  last_error =
      state->last_error ? state->last_error : state->backend.last_error;
  storvsc_backend_init(&state->backend);
  state->configured = 1u;
  state->enabled = 0u;
  state->ops = ops;
  state->last_error = last_error;
  storvsc_runtime_sync_phase(state);
}

int storvsc_runtime_step_probe_only(struct storvsc_runtime_state *state) {
  int rc = 0;

  if (!state || !state->configured || !state->ops.query_offer ||
      !state->ops.open_channel || !state->ops.send_control ||
      !state->ops.recv_control) {
    return -1;
  }
  if (!state->enabled) {
    storvsc_runtime_sync_phase(state);
    return 0;
  }
  if (state->backend.phase != STORVSC_BACKEND_PROBE) {
    storvsc_runtime_sync_phase(state);
    return 0;
  }

  rc = storvsc_backend_step(&state->backend, &state->ops);
  if (rc < 0) {
    state->last_error = rc;
  } else if (state->backend.last_error != 0) {
    state->last_error = state->backend.last_error;
  }
  storvsc_runtime_sync_phase(state);
  return rc;
}

int storvsc_runtime_step(struct storvsc_runtime_state *state) {
  int rc = 0;

  if (!state || !state->configured || !state->ops.query_offer ||
      !state->ops.open_channel || !state->ops.send_control ||
      !state->ops.recv_control) {
    return -1;
  }
  if (!state->enabled) {
    storvsc_runtime_sync_phase(state);
    return 0;
  }

  rc = storvsc_backend_step(&state->backend, &state->ops);
  if (rc < 0) {
    state->last_error = rc;
  } else if (state->backend.last_error != 0) {
    state->last_error = state->backend.last_error;
  }
  storvsc_runtime_sync_phase(state);
  return rc;
}

int storvsc_runtime_controller_status(const struct storvsc_runtime_state *state,
                                      struct storvsc_controller_status *out) {
  if (!state || !out) {
    return -1;
  }

  storvsc_runtime_zero(out, sizeof(*out));
  out->configured = state->configured;
  out->enabled = state->enabled;
  out->ready = storvsc_backend_is_ready(&state->backend) ? 1u : 0u;
  out->phase = state->phase;
  out->last_error = state->last_error;
  out->offer_ready = state->backend.offer_ready;
  out->channel_ready = state->backend.channel_ready;
  out->offer = state->backend.offer;
  out->open_id = state->backend.channel.open_id;
  out->gpadl_handle = state->backend.channel.gpadl_handle;
  out->send_ring_size = state->backend.channel.send_ring_size;
  out->recv_ring_size = state->backend.channel.recv_ring_size;
  out->last_gpadl_status = state->backend.channel.last_gpadl_status;
  out->last_open_status = state->backend.channel.last_open_status;
  out->last_open_msgtype = state->backend.channel.last_open_msgtype;
  out->last_open_relid = state->backend.channel.last_open_relid;
  out->last_open_observed_id = state->backend.channel.last_open_observed_id;
  out->last_target_vcpu = state->backend.channel.last_target_vcpu;
  out->last_downstream_offset = state->backend.channel.last_downstream_offset;
  out->last_retry_count = state->backend.channel.last_retry_count;
  out->control_wait_budget = state->backend.control_wait_budget;
  out->control_expected_operation = state->backend.session.last_request_operation;
  out->last_control = state->backend.last_control;
  out->properties = state->backend.session.properties;
  return 0;
}
