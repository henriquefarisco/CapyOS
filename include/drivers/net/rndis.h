#ifndef DRIVERS_NET_RNDIS_H
#define DRIVERS_NET_RNDIS_H

#include <stddef.h>
#include <stdint.h>

#define RNDIS_MSG_INITIALIZE 0x00000002u
#define RNDIS_MSG_HALT 0x00000003u
#define RNDIS_MSG_QUERY 0x00000004u
#define RNDIS_MSG_SET 0x00000005u

#define RNDIS_MSG_INITIALIZE_COMPLETE 0x80000002u
#define RNDIS_MSG_QUERY_COMPLETE 0x80000004u
#define RNDIS_MSG_SET_COMPLETE 0x80000005u

#define RNDIS_STATUS_SUCCESS 0x00000000u

struct rndis_initialize_complete {
  uint32_t request_id;
  uint32_t status;
  uint32_t major_version;
  uint32_t minor_version;
  uint32_t device_flags;
  uint32_t medium;
  uint32_t max_packets_per_transfer;
  uint32_t max_transfer_size;
  uint32_t packet_alignment_factor;
  uint32_t af_list_offset;
  uint32_t af_list_size;
};

struct rndis_set_complete {
  uint32_t request_id;
  uint32_t status;
};

size_t rndis_build_initialize_request(uint8_t *out, size_t cap,
                                      uint32_t request_id,
                                      uint32_t major_version,
                                      uint32_t minor_version,
                                      uint32_t max_transfer_size);
size_t rndis_build_halt_request(uint8_t *out, size_t cap, uint32_t request_id);
size_t rndis_build_query_request(uint8_t *out, size_t cap, uint32_t request_id,
                                 uint32_t oid, const void *payload,
                                 size_t payload_len);
size_t rndis_build_set_request(uint8_t *out, size_t cap, uint32_t request_id,
                               uint32_t oid, const void *payload,
                               size_t payload_len);

int rndis_parse_initialize_complete(const uint8_t *buf, size_t len,
                                    struct rndis_initialize_complete *out);
int rndis_parse_query_complete(const uint8_t *buf, size_t len,
                               uint32_t expected_request_id,
                               const uint8_t **out_payload,
                               size_t *out_payload_len);
int rndis_parse_query_complete_u32(const uint8_t *buf, size_t len,
                                   uint32_t expected_request_id,
                                   uint32_t *out_value);
int rndis_parse_set_complete(const uint8_t *buf, size_t len,
                             uint32_t expected_request_id,
                             struct rndis_set_complete *out);

#endif /* DRIVERS_NET_RNDIS_H */
