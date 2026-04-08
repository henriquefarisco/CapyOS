#ifndef DRIVERS_NET_NETVSC_RUNTIME_H
#define DRIVERS_NET_NETVSC_RUNTIME_H

#include "drivers/net/net_probe.h"
#include "drivers/net/netvsc_backend.h"

#include <stdint.h>

enum netvsc_runtime_phase {
  NETVSC_RUNTIME_UNCONFIGURED = 0,
  NETVSC_RUNTIME_DISABLED,
  NETVSC_RUNTIME_PROBE,
  NETVSC_RUNTIME_CHANNEL,
  NETVSC_RUNTIME_CONTROL,
  NETVSC_RUNTIME_READY,
  NETVSC_RUNTIME_FAILED,
};

struct netvsc_runtime_state {
  uint8_t configured;
  uint8_t enabled;
  uint8_t phase;
  uint8_t reserved;
  int32_t last_error;
  struct netvsc_backend_ops ops;
  struct netvsc_backend_state backend;
};

struct netvsc_controller_status {
  uint8_t configured;
  uint8_t enabled;
  uint8_t ready;
  uint8_t vmbus_stage;
  uint8_t stage;
  uint8_t phase;
  uint8_t offer_ready;
  uint8_t channel_ready;
  uint8_t reserved;
  int32_t last_error;
  struct vmbus_offer_info offer;
};

void netvsc_runtime_init(struct netvsc_runtime_state *state);
int netvsc_runtime_configure(struct netvsc_runtime_state *state,
                             const struct net_nic_probe *nic,
                             const struct netvsc_backend_ops *ops);
void netvsc_runtime_set_enabled(struct netvsc_runtime_state *state, int enabled);
void netvsc_runtime_degrade_passive(struct netvsc_runtime_state *state);
int netvsc_runtime_step(struct netvsc_runtime_state *state);
int netvsc_runtime_controller_status(const struct netvsc_runtime_state *state,
                                     struct netvsc_controller_status *out);

#endif /* DRIVERS_NET_NETVSC_RUNTIME_H */
