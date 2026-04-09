#ifndef DRIVERS_NET_NETVSP_H
#define DRIVERS_NET_NETVSP_H

#include <stddef.h>
#include <stdint.h>

#define NETVSP_MSG_INIT 0x00000001u
#define NETVSP_MSG_INIT_COMPLETE 0x80000001u
#define NETVSP_MSG_SEND_RNDIS_CONTROL 0x00010001u
#define NETVSP_CHANNEL_TYPE_CONTROL 0x00000000u
#define NETVSP_STATUS_SUCCESS 0x00000000u

struct netvsp_init_complete {
  uint32_t status;
  uint32_t protocol_major;
  uint32_t protocol_minor;
};

struct netvsp_transport_info {
  uint32_t message_type;
  uint32_t channel_type;
  uint32_t payload_len;
};

size_t netvsp_build_init_message(uint8_t *out, size_t cap, uint32_t major,
                                 uint32_t minor);
int netvsp_parse_init_complete(const uint8_t *buf, size_t len,
                               struct netvsp_init_complete *out);
size_t netvsp_build_rndis_control_message(uint8_t *out, size_t cap,
                                          const uint8_t *payload,
                                          size_t payload_len);
int netvsp_parse_rndis_control_message(const uint8_t *buf, size_t len,
                                       struct netvsp_transport_info *info,
                                       const uint8_t **out_payload,
                                       size_t *out_payload_len);

#endif /* DRIVERS_NET_NETVSP_H */
