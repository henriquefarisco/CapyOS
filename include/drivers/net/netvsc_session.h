#ifndef DRIVERS_NET_NETVSC_SESSION_H
#define DRIVERS_NET_NETVSC_SESSION_H

#include "drivers/net/netvsc.h"

#include <stddef.h>
#include <stdint.h>

enum netvsc_session_phase {
  NETVSC_SESSION_WAIT_INIT = 0,
  NETVSC_SESSION_CONTROL,
  NETVSC_SESSION_READY,
  NETVSC_SESSION_FAILED,
};

struct netvsc_session_state {
  uint8_t phase;
  uint8_t init_sent;
  uint8_t init_complete;
  uint8_t reserved;
  uint32_t requested_major;
  uint32_t requested_minor;
  uint32_t negotiated_major;
  uint32_t negotiated_minor;
  uint32_t last_status;
  struct netvsc_control_state control;
};

void netvsc_session_init(struct netvsc_session_state *state, uint32_t major,
                         uint32_t minor);
size_t netvsc_session_build_next(struct netvsc_session_state *state,
                                 struct netvsc_control_transport *transport,
                                 uint8_t *out, size_t cap);
int netvsc_session_handle_response(struct netvsc_session_state *state,
                                   const uint8_t *buf, size_t len);
int netvsc_session_is_ready(const struct netvsc_session_state *state);

#endif /* DRIVERS_NET_NETVSC_SESSION_H */
