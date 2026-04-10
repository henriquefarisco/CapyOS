#ifndef DRIVERS_STORAGE_STORVSC_BACKEND_H
#define DRIVERS_STORAGE_STORVSC_BACKEND_H

#include "drivers/hyperv/hyperv.h"
#include "drivers/storage/storvsc_session.h"

#include <stddef.h>
#include <stdint.h>

#define STORVSC_BACKEND_OPEN_ID 0x53565343u
#define STORVSC_BACKEND_GPADL_HANDLE 0x000E3E30u
#define STORVSC_BACKEND_RING_BYTES (128u * 1024u)
#define STORVSC_CONTROL_TRANS_ID 0xFFFFFFFFFFFFFFFDULL

enum storvsc_backend_phase {
  STORVSC_BACKEND_PROBE = 0,
  STORVSC_BACKEND_CHANNEL,
  STORVSC_BACKEND_CONTROL,
  STORVSC_BACKEND_READY,
  STORVSC_BACKEND_FAILED,
};

struct storvsc_control_diag {
  int32_t read_rc;
  int32_t extract_rc;
  int32_t parse_rc;
  uint32_t packet_len;
  uint32_t payload_len;
  uint32_t packet_type;
  uint32_t packet_flags;
  uint32_t operation;
  uint32_t status;
  uint64_t trans_id;
};

struct storvsc_backend_state {
  uint8_t phase;
  uint8_t offer_ready;
  uint8_t channel_ready;
  uint8_t waiting_response;
  uint8_t open_timeout_budget;
  uint8_t open_wait_logged;
  uint8_t control_wait_budget;
  uint8_t control_wait_logged;
  uint16_t reserved0;
  int32_t last_error;
  struct vmbus_offer_info offer;
  struct vmbus_channel_runtime channel;
  struct storvsc_session_state session;
  struct storvsc_control_diag last_control;
};

struct storvsc_backend_ops {
  int (*query_offer)(struct vmbus_offer_info *out);
  int (*open_channel)(struct vmbus_channel_runtime *channel);
  int (*send_control)(struct vmbus_channel_runtime *channel, const uint8_t *buf,
                      size_t len);
  int (*recv_control)(struct vmbus_channel_runtime *channel, uint8_t *buf,
                      size_t cap, size_t *out_len,
                      struct storvsc_control_diag *diag);
};

void storvsc_backend_init(struct storvsc_backend_state *state);
int storvsc_backend_step(struct storvsc_backend_state *state,
                         const struct storvsc_backend_ops *ops);
int storvsc_backend_is_ready(const struct storvsc_backend_state *state);

#endif /* DRIVERS_STORAGE_STORVSC_BACKEND_H */
