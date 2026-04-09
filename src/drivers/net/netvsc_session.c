#include "drivers/net/netvsc_session.h"

static void netvsc_session_memzero(void *ptr, size_t len) {
  uint8_t *p = (uint8_t *)ptr;
  if (!p) {
    return;
  }
  for (size_t i = 0; i < len; ++i) {
    p[i] = 0;
  }
}

void netvsc_session_init(struct netvsc_session_state *state, uint32_t major,
                         uint32_t minor) {
  if (!state) {
    return;
  }
  netvsc_session_memzero(state, sizeof(*state));
  state->phase = NETVSC_SESSION_WAIT_INIT;
  state->requested_major = major;
  state->requested_minor = minor;
  netvsc_control_init(&state->control);
}

size_t netvsc_session_build_next(struct netvsc_session_state *state,
                                 struct netvsc_control_transport *transport,
                                 uint8_t *out, size_t cap) {
  size_t len = 0u;

  if (transport) {
    netvsc_session_memzero(transport, sizeof(*transport));
  }
  if (!state || !out || cap == 0u) {
    return 0u;
  }

  if (state->phase == NETVSC_SESSION_WAIT_INIT) {
    if (state->init_sent || state->init_complete) {
      return 0u;
    }
    len = netvsp_build_init_message(out, cap, state->requested_major,
                                    state->requested_minor);
    if (len == 0u) {
      return 0u;
    }
    state->init_sent = 1u;
    if (transport) {
      transport->netvsp_message_type = NETVSP_MSG_INIT;
      transport->payload_len = (uint32_t)len;
    }
    return len;
  }

  if (state->phase == NETVSC_SESSION_CONTROL) {
    len = netvsc_control_build_next_transport(&state->control, transport, out, cap);
    if (len == 0u && netvsc_control_is_ready(&state->control)) {
      state->phase = NETVSC_SESSION_READY;
    }
    return len;
  }

  return 0u;
}

int netvsc_session_handle_response(struct netvsc_session_state *state,
                                   const uint8_t *buf, size_t len) {
  int rc = 0;
  struct netvsp_init_complete init_complete;

  if (!state || !buf) {
    return -1;
  }

  if (state->phase == NETVSC_SESSION_WAIT_INIT) {
    if (netvsp_parse_init_complete(buf, len, &init_complete) != 0) {
      state->phase = NETVSC_SESSION_FAILED;
      return -2;
    }
    state->last_status = init_complete.status;
    if (init_complete.status != NETVSP_STATUS_SUCCESS) {
      state->phase = NETVSC_SESSION_FAILED;
      return -3;
    }
    state->init_complete = 1u;
    state->init_sent = 0u;
    state->negotiated_major = init_complete.protocol_major;
    state->negotiated_minor = init_complete.protocol_minor;
    state->phase = NETVSC_SESSION_CONTROL;
    return 1;
  }

  if (state->phase == NETVSC_SESSION_CONTROL) {
    rc = netvsc_control_handle_transport_response(&state->control, buf, len);
    if (rc < 0) {
      state->phase = NETVSC_SESSION_FAILED;
      return rc;
    }
    if (netvsc_control_is_ready(&state->control)) {
      state->phase = NETVSC_SESSION_READY;
    }
    return rc;
  }

  return 0;
}

int netvsc_session_is_ready(const struct netvsc_session_state *state) {
  return state && state->phase == NETVSC_SESSION_READY;
}
