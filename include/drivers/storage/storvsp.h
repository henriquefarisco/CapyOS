#ifndef DRIVERS_STORAGE_STORVSP_H
#define DRIVERS_STORAGE_STORVSP_H

#include <stddef.h>
#include <stdint.h>

#define STORVSP_PACKET_SIZE 64u
#define STORVSP_WIRE_PACKET_SIZE 128u

#define STORVSP_OPERATION_COMPLETE_IO 1u
#define STORVSP_OPERATION_REMOVE_DEVICE 2u
#define STORVSP_OPERATION_EXECUTE_SRB 3u
#define STORVSP_OPERATION_RESET_LUN 4u
#define STORVSP_OPERATION_RESET_ADAPTER 5u
#define STORVSP_OPERATION_RESET_BUS 6u
#define STORVSP_OPERATION_BEGIN_INITIALIZATION 7u
#define STORVSP_OPERATION_END_INITIALIZATION 8u
#define STORVSP_OPERATION_QUERY_PROTOCOL_VERSION 9u
#define STORVSP_OPERATION_QUERY_PROPERTIES 10u
#define STORVSP_OPERATION_ENUMERATE_BUS 11u

#define STORVSP_STATUS_SUCCESS 0u
#define STORVSP_FLAG_REQUEST_COMPLETION 0x00000001u

#define STORVSP_CHANNEL_SUPPORTS_MULTI_CHANNEL 0x00000001u

#define STORVSP_PROTO_VERSION(major, minor)                                    \
  (((uint16_t)((major) & 0xFFu) << 8) | (uint16_t)((minor) & 0xFFu))
#define STORVSP_PROTO_WIN8 STORVSP_PROTO_VERSION(5u, 1u)
#define STORVSP_PROTO_WIN8_1 STORVSP_PROTO_VERSION(6u, 0u)
#define STORVSP_PROTO_WIN10 STORVSP_PROTO_VERSION(6u, 2u)

struct storvsp_protocol_version {
  uint16_t major_minor;
  uint16_t revision;
} __attribute__((packed));

struct storvsp_channel_properties {
  uint32_t reserved;
  uint16_t max_channel_count;
  uint16_t reserved1;
  uint32_t flags;
  uint32_t max_transfer_bytes;
  uint64_t reserved2;
} __attribute__((packed));

struct storvsp_packet_header_info {
  uint32_t operation;
  uint32_t flags;
  uint32_t status;
};

struct storvsp_protocol_response {
  uint32_t status;
  uint16_t negotiated_major_minor;
};

size_t storvsp_build_begin_init(uint8_t *out, size_t cap);
size_t storvsp_build_query_protocol(uint8_t *out, size_t cap,
                                    uint16_t major_minor);
size_t storvsp_build_query_properties(uint8_t *out, size_t cap);
size_t storvsp_build_end_init(uint8_t *out, size_t cap);
size_t storvsp_build_enumerate_bus(uint8_t *out, size_t cap);
int storvsp_parse_header(const uint8_t *buf, size_t len,
                         struct storvsp_packet_header_info *out);
int storvsp_parse_protocol_response(const uint8_t *buf, size_t len,
                                    struct storvsp_protocol_response *out);
int storvsp_parse_properties_response(
    const uint8_t *buf, size_t len, struct storvsp_channel_properties *out,
    uint32_t *out_status);

#endif /* DRIVERS_STORAGE_STORVSP_H */
