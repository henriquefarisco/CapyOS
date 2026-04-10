#ifndef DRIVERS_NET_NETVSC_BACKEND_H
#define DRIVERS_NET_NETVSC_BACKEND_H

#include "drivers/hyperv/hyperv.h"
#include "drivers/net/netvsc_session.h"

#include <stddef.h>
#include <stdint.h>

#define NETVSC_BACKEND_OPEN_ID 0x43565343u
#define NETVSC_BACKEND_GPADL_HANDLE 0x000E2E20u
#define NETVSC_BACKEND_RING_BYTES (64u * 1024u)
#define NETVSC_BACKEND_VSP_MAJOR 6u
#define NETVSC_BACKEND_VSP_MINOR 1u

enum netvsc_backend_phase {
  NETVSC_BACKEND_PROBE = 0,
  NETVSC_BACKEND_CHANNEL,
  NETVSC_BACKEND_CONTROL,
  NETVSC_BACKEND_READY,
  NETVSC_BACKEND_FAILED,
};

struct netvsc_backend_state {
  uint8_t phase;
  uint8_t offer_ready;
  uint8_t channel_ready;
  uint8_t waiting_response;
  uint8_t open_timeout_budget;
  uint8_t open_wait_logged;
  int32_t last_error;
  struct vmbus_offer_info offer;
  struct vmbus_channel_runtime channel;
  struct netvsc_session_state session;
};

struct netvsc_backend_ops {
  int (*query_offer)(struct vmbus_offer_info *out);
  int (*open_channel)(struct vmbus_channel_runtime *channel);
  int (*send_control)(struct vmbus_channel_runtime *channel, const uint8_t *buf,
                      size_t len);
  int (*recv_control)(struct vmbus_channel_runtime *channel, uint8_t *buf,
                      size_t cap, size_t *out_len);
};

void netvsc_backend_init(struct netvsc_backend_state *state);
int netvsc_backend_step(struct netvsc_backend_state *state,
                        const struct netvsc_backend_ops *ops);
int netvsc_backend_is_ready(const struct netvsc_backend_state *state);

#endif /* DRIVERS_NET_NETVSC_BACKEND_H */
