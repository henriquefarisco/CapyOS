#ifndef DRIVERS_NET_NETVSC_H
#define DRIVERS_NET_NETVSC_H

#include "drivers/hyperv/hyperv.h"
#include "drivers/net/netvsp.h"

#include <stddef.h>
#include <stdint.h>

#define NETVSC_RNDIS_OID_GEN_MAXIMUM_FRAME_SIZE 0x00010106u
#define NETVSC_RNDIS_OID_GEN_CURRENT_PACKET_FILTER 0x0001010Eu
#define NETVSC_RNDIS_OID_802_3_CURRENT_ADDRESS 0x01010102u

#define NETVSC_PACKET_FILTER_DIRECTED 0x00000001u
#define NETVSC_PACKET_FILTER_MULTICAST 0x00000002u
#define NETVSC_PACKET_FILTER_BROADCAST 0x00000008u
#define NETVSC_PACKET_FILTER_DEFAULT                                             \
  (NETVSC_PACKET_FILTER_DIRECTED | NETVSC_PACKET_FILTER_MULTICAST |            \
   NETVSC_PACKET_FILTER_BROADCAST)

struct netvsc_runtime_status {
  uint8_t offer_ready;
  uint8_t channel_ready;
  struct vmbus_offer_info offer;
};

enum netvsc_control_phase {
  NETVSC_CONTROL_IDLE = 0,
  NETVSC_CONTROL_WAIT_INITIALIZE,
  NETVSC_CONTROL_WAIT_QUERY_MTU,
  NETVSC_CONTROL_WAIT_QUERY_MAC,
  NETVSC_CONTROL_WAIT_SET_FILTER,
  NETVSC_CONTROL_READY,
  NETVSC_CONTROL_FAILED,
};

struct netvsc_control_state {
  uint8_t phase;
  uint8_t initialized;
  uint8_t mac_valid;
  uint8_t filter_set;
  uint8_t reserved;
  uint32_t next_request_id;
  uint32_t last_request_id;
  uint32_t mtu;
  uint32_t packet_filter;
  uint32_t last_status;
  uint8_t mac[6];
};

struct netvsc_control_transport {
  uint32_t request_id;
  uint32_t rndis_message_type;
  uint32_t netvsp_message_type;
  uint32_t payload_len;
};

int netvsc_query_offer(struct vmbus_offer_info *out);
int netvsc_runtime_status(struct netvsc_runtime_status *out);
int netvsc_refresh_runtime(struct netvsc_runtime_status *out);
void netvsc_control_init(struct netvsc_control_state *state);
size_t netvsc_control_build_next_request(struct netvsc_control_state *state,
                                         uint8_t *out, size_t cap);
size_t netvsc_control_build_next_transport(
    struct netvsc_control_state *state,
    struct netvsc_control_transport *transport, uint8_t *out, size_t cap);
int netvsc_control_handle_response(struct netvsc_control_state *state,
                                   const uint8_t *buf, size_t len);
int netvsc_control_handle_transport_response(
    struct netvsc_control_state *state, const uint8_t *buf, size_t len);
int netvsc_control_is_ready(const struct netvsc_control_state *state);

#endif /* DRIVERS_NET_NETVSC_H */
