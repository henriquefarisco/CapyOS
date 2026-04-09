#include "drivers/storage/storvsp.h"

struct storvsp_packet {
  uint32_t operation;
  uint32_t flags;
  uint32_t status;
  union {
    struct storvsp_protocol_version version;
    struct storvsp_channel_properties properties;
    uint8_t raw[0x34];
  } payload;
} __attribute__((packed));

static void storvsp_zero(void *dst, size_t len) {
  uint8_t *bytes = (uint8_t *)dst;
  if (!bytes) {
    return;
  }
  for (size_t i = 0; i < len; ++i) {
    bytes[i] = 0u;
  }
}

static void storvsp_copy(void *dst, const void *src, size_t len) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  if (!d || !s) {
    return;
  }
  for (size_t i = 0; i < len; ++i) {
    d[i] = s[i];
  }
}

static size_t storvsp_build_packet(uint8_t *out, size_t cap, uint32_t operation,
                                   const void *payload, size_t payload_len) {
  struct storvsp_packet *packet = (struct storvsp_packet *)out;

  if (!out || cap < sizeof(*packet) || payload_len > sizeof(packet->payload)) {
    return 0u;
  }

  storvsp_zero(packet, sizeof(*packet));
  packet->operation = operation;
  packet->flags = STORVSP_FLAG_REQUEST_COMPLETION;
  packet->status = STORVSP_STATUS_SUCCESS;
  if (payload && payload_len != 0u) {
    storvsp_copy(packet->payload.raw, payload, payload_len);
  }
  return sizeof(*packet);
}

static int storvsp_is_control_completion(uint32_t operation,
                                         uint32_t request_operation) {
  if (operation == STORVSP_OPERATION_COMPLETE_IO) {
    return 1;
  }
  return operation == request_operation;
}

size_t storvsp_build_begin_init(uint8_t *out, size_t cap) {
  return storvsp_build_packet(out, cap, STORVSP_OPERATION_BEGIN_INITIALIZATION,
                              NULL, 0u);
}

size_t storvsp_build_query_protocol(uint8_t *out, size_t cap,
                                    uint16_t major_minor) {
  struct storvsp_protocol_version version;
  version.major_minor = major_minor;
  version.revision = 0u;
  return storvsp_build_packet(out, cap, STORVSP_OPERATION_QUERY_PROTOCOL_VERSION,
                              &version, sizeof(version));
}

size_t storvsp_build_query_properties(uint8_t *out, size_t cap) {
  return storvsp_build_packet(out, cap, STORVSP_OPERATION_QUERY_PROPERTIES,
                              NULL, 0u);
}

size_t storvsp_build_end_init(uint8_t *out, size_t cap) {
  return storvsp_build_packet(out, cap, STORVSP_OPERATION_END_INITIALIZATION,
                              NULL, 0u);
}

size_t storvsp_build_enumerate_bus(uint8_t *out, size_t cap) {
  return storvsp_build_packet(out, cap, STORVSP_OPERATION_ENUMERATE_BUS, NULL,
                              0u);
}

int storvsp_parse_header(const uint8_t *buf, size_t len,
                         struct storvsp_packet_header_info *out) {
  const struct storvsp_packet *packet = (const struct storvsp_packet *)buf;

  if (!buf || !out || len < sizeof(*packet)) {
    return -1;
  }

  out->operation = packet->operation;
  out->flags = packet->flags;
  out->status = packet->status;
  return 0;
}

int storvsp_parse_protocol_response(const uint8_t *buf, size_t len,
                                    struct storvsp_protocol_response *out) {
  const struct storvsp_packet *packet = (const struct storvsp_packet *)buf;

  if (!buf || !out || len < sizeof(*packet)) {
    return -1;
  }
  if (!storvsp_is_control_completion(packet->operation,
                                     STORVSP_OPERATION_QUERY_PROTOCOL_VERSION)) {
    return -2;
  }

  out->status = packet->status;
  out->negotiated_major_minor = packet->payload.version.major_minor;
  return 0;
}

int storvsp_parse_properties_response(
    const uint8_t *buf, size_t len, struct storvsp_channel_properties *out,
    uint32_t *out_status) {
  const struct storvsp_packet *packet = (const struct storvsp_packet *)buf;

  if (!buf || !out || !out_status || len < sizeof(*packet)) {
    return -1;
  }
  if (!storvsp_is_control_completion(packet->operation,
                                     STORVSP_OPERATION_QUERY_PROPERTIES)) {
    return -2;
  }

  *out_status = packet->status;
  *out = packet->payload.properties;
  return 0;
}
