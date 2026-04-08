#include "drivers/storage/storvsc_session.h"

static void storvsc_session_zero(void *dst, size_t len) {
  uint8_t *bytes = (uint8_t *)dst;
  if (!bytes) {
    return;
  }
  for (size_t i = 0; i < len; ++i) {
    bytes[i] = 0u;
  }
}

void storvsc_session_init(struct storvsc_session_state *state,
                          uint16_t preferred_major_minor) {
  if (!state) {
    return;
  }
  storvsc_session_zero(state, sizeof(*state));
  state->phase = STORVSC_SESSION_WAIT_BEGIN;
  state->requested_major_minor = preferred_major_minor;
}

static uint32_t storvsc_session_expected_operation(uint8_t phase) {
  switch (phase) {
  case STORVSC_SESSION_WAIT_BEGIN:
    return STORVSP_OPERATION_BEGIN_INITIALIZATION;
  case STORVSC_SESSION_WAIT_VERSION:
    return STORVSP_OPERATION_QUERY_PROTOCOL_VERSION;
  case STORVSC_SESSION_WAIT_PROPERTIES:
    return STORVSP_OPERATION_QUERY_PROPERTIES;
  case STORVSC_SESSION_WAIT_END:
    return STORVSP_OPERATION_END_INITIALIZATION;
  case STORVSC_SESSION_WAIT_ENUMERATE:
    return STORVSP_OPERATION_ENUMERATE_BUS;
  default:
    return 0u;
  }
}

static int storvsc_session_response_matches(uint8_t phase, uint32_t operation) {
  uint32_t expected_operation = storvsc_session_expected_operation(phase);

  if (expected_operation == 0u) {
    return 0;
  }
  if (operation == STORVSP_OPERATION_COMPLETE_IO) {
    return 1;
  }
  return operation == expected_operation;
}

size_t storvsc_session_build_next(struct storvsc_session_state *state,
                                  uint8_t *out, size_t cap) {
  if (!state || !out || cap == 0u) {
    return 0u;
  }
  if (state->request_sent || state->phase == STORVSC_SESSION_READY ||
      state->phase == STORVSC_SESSION_FAILED) {
    return 0u;
  }

  switch (state->phase) {
  case STORVSC_SESSION_WAIT_BEGIN:
    state->last_request_operation = STORVSP_OPERATION_BEGIN_INITIALIZATION;
    state->request_sent = 1u;
    return storvsp_build_begin_init(out, cap);
  case STORVSC_SESSION_WAIT_VERSION:
    state->last_request_operation = STORVSP_OPERATION_QUERY_PROTOCOL_VERSION;
    state->request_sent = 1u;
    return storvsp_build_query_protocol(out, cap, state->requested_major_minor);
  case STORVSC_SESSION_WAIT_PROPERTIES:
    state->last_request_operation = STORVSP_OPERATION_QUERY_PROPERTIES;
    state->request_sent = 1u;
    return storvsp_build_query_properties(out, cap);
  case STORVSC_SESSION_WAIT_END:
    state->last_request_operation = STORVSP_OPERATION_END_INITIALIZATION;
    state->request_sent = 1u;
    return storvsp_build_end_init(out, cap);
  case STORVSC_SESSION_WAIT_ENUMERATE:
    state->last_request_operation = STORVSP_OPERATION_ENUMERATE_BUS;
    state->request_sent = 1u;
    return storvsp_build_enumerate_bus(out, cap);
  default:
    return 0u;
  }
}

int storvsc_session_handle_response(struct storvsc_session_state *state,
                                    const uint8_t *buf, size_t len) {
  struct storvsp_packet_header_info header;
  int is_matching_response = 0;

  if (!state || !buf) {
    return -1;
  }
  if (storvsp_parse_header(buf, len, &header) != 0) {
    state->phase = STORVSC_SESSION_FAILED;
    return -2;
  }
  is_matching_response =
      storvsc_session_response_matches(state->phase, header.operation);
  if (!is_matching_response) {
    return 0;
  }
  state->request_sent = 0u;
  state->last_status = header.status;
  if (header.status != STORVSP_STATUS_SUCCESS) {
    state->phase = STORVSC_SESSION_FAILED;
    return -3;
  }

  switch (state->phase) {
  case STORVSC_SESSION_WAIT_BEGIN:
    state->phase = STORVSC_SESSION_WAIT_VERSION;
    return 1;
  case STORVSC_SESSION_WAIT_VERSION: {
    struct storvsp_protocol_response response;
    if (storvsp_parse_protocol_response(buf, len, &response) != 0) {
      state->phase = STORVSC_SESSION_FAILED;
      return -5;
    }
    state->negotiated_major_minor = response.negotiated_major_minor;
    state->phase = STORVSC_SESSION_WAIT_PROPERTIES;
    return 1;
  }
  case STORVSC_SESSION_WAIT_PROPERTIES:
    if (storvsp_parse_properties_response(buf, len, &state->properties,
                                          &state->last_status) != 0) {
      state->phase = STORVSC_SESSION_FAILED;
      return -6;
    }
    if (state->last_status != STORVSP_STATUS_SUCCESS) {
      state->phase = STORVSC_SESSION_FAILED;
      return -7;
    }
    state->phase = STORVSC_SESSION_WAIT_END;
    return 1;
  case STORVSC_SESSION_WAIT_END:
    state->phase = STORVSC_SESSION_READY;
    return 1;
  case STORVSC_SESSION_WAIT_ENUMERATE:
    state->phase = STORVSC_SESSION_READY;
    return 1;
  default:
    return 0;
  }
}

int storvsc_session_is_ready(const struct storvsc_session_state *state) {
  return state && state->phase == STORVSC_SESSION_READY;
}
