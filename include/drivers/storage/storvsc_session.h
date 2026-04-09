#ifndef DRIVERS_STORAGE_STORVSC_SESSION_H
#define DRIVERS_STORAGE_STORVSC_SESSION_H

#include "drivers/storage/storvsp.h"

#include <stddef.h>
#include <stdint.h>

enum storvsc_session_phase {
  STORVSC_SESSION_WAIT_BEGIN = 0,
  STORVSC_SESSION_WAIT_VERSION,
  STORVSC_SESSION_WAIT_PROPERTIES,
  STORVSC_SESSION_WAIT_END,
  STORVSC_SESSION_WAIT_ENUMERATE,
  STORVSC_SESSION_READY,
  STORVSC_SESSION_FAILED,
};

struct storvsc_session_state {
  uint8_t phase;
  uint8_t request_sent;
  uint16_t reserved;
  uint16_t requested_major_minor;
  uint16_t negotiated_major_minor;
  uint32_t last_request_operation;
  uint32_t last_status;
  struct storvsp_channel_properties properties;
};

void storvsc_session_init(struct storvsc_session_state *state,
                          uint16_t preferred_major_minor);
size_t storvsc_session_build_next(struct storvsc_session_state *state,
                                  uint8_t *out, size_t cap);
int storvsc_session_handle_response(struct storvsc_session_state *state,
                                    const uint8_t *buf, size_t len);
int storvsc_session_is_ready(const struct storvsc_session_state *state);

#endif /* DRIVERS_STORAGE_STORVSC_SESSION_H */
