#include "drivers/net/netvsp.h"

struct netvsp_message_header {
  uint32_t message_type;
  uint32_t message_len;
  uint32_t channel_type;
  uint32_t payload_len;
  uint32_t reserved;
} __attribute__((packed));

struct netvsp_init_message {
  uint32_t message_type;
  uint32_t message_len;
  uint32_t protocol_major;
  uint32_t protocol_minor;
} __attribute__((packed));

struct netvsp_init_complete_message {
  uint32_t message_type;
  uint32_t message_len;
  uint32_t status;
  uint32_t protocol_major;
  uint32_t protocol_minor;
} __attribute__((packed));

static void netvsp_memcpy(void *dst, const void *src, size_t len) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;

  if (!d || !s) {
    return;
  }
  for (size_t i = 0; i < len; ++i) {
    d[i] = s[i];
  }
}

size_t netvsp_build_init_message(uint8_t *out, size_t cap, uint32_t major,
                                 uint32_t minor) {
  struct netvsp_init_message *msg = (struct netvsp_init_message *)out;

  if (!out || cap < sizeof(*msg)) {
    return 0u;
  }

  msg->message_type = NETVSP_MSG_INIT;
  msg->message_len = (uint32_t)sizeof(*msg);
  msg->protocol_major = major;
  msg->protocol_minor = minor;
  return sizeof(*msg);
}

int netvsp_parse_init_complete(const uint8_t *buf, size_t len,
                               struct netvsp_init_complete *out) {
  const struct netvsp_init_complete_message *msg =
      (const struct netvsp_init_complete_message *)buf;

  if (!buf || !out || len < sizeof(*msg)) {
    return -1;
  }
  if (msg->message_type != NETVSP_MSG_INIT_COMPLETE ||
      msg->message_len < sizeof(*msg) || (size_t)msg->message_len > len) {
    return -2;
  }

  out->status = msg->status;
  out->protocol_major = msg->protocol_major;
  out->protocol_minor = msg->protocol_minor;
  return 0;
}

size_t netvsp_build_rndis_control_message(uint8_t *out, size_t cap,
                                          const uint8_t *payload,
                                          size_t payload_len) {
  struct netvsp_message_header *msg = (struct netvsp_message_header *)out;
  size_t total_len = sizeof(*msg) + payload_len;

  if (!out || !payload || cap < total_len) {
    return 0u;
  }

  msg->message_type = NETVSP_MSG_SEND_RNDIS_CONTROL;
  msg->message_len = (uint32_t)total_len;
  msg->channel_type = NETVSP_CHANNEL_TYPE_CONTROL;
  msg->payload_len = (uint32_t)payload_len;
  msg->reserved = 0u;
  netvsp_memcpy(out + sizeof(*msg), payload, payload_len);
  return total_len;
}

int netvsp_parse_rndis_control_message(const uint8_t *buf, size_t len,
                                       struct netvsp_transport_info *info,
                                       const uint8_t **out_payload,
                                       size_t *out_payload_len) {
  const struct netvsp_message_header *msg =
      (const struct netvsp_message_header *)buf;
  size_t payload_offset = sizeof(*msg);

  if (!buf || !info || !out_payload || !out_payload_len ||
      len < sizeof(*msg)) {
    return -1;
  }
  if (msg->message_type != NETVSP_MSG_SEND_RNDIS_CONTROL ||
      msg->message_len < sizeof(*msg) || (size_t)msg->message_len > len) {
    return -2;
  }
  if (msg->channel_type != NETVSP_CHANNEL_TYPE_CONTROL) {
    return -3;
  }
  if ((size_t)msg->payload_len > ((size_t)msg->message_len - payload_offset)) {
    return -4;
  }

  info->message_type = msg->message_type;
  info->channel_type = msg->channel_type;
  info->payload_len = msg->payload_len;

  if (msg->payload_len == 0u) {
    *out_payload = NULL;
    *out_payload_len = 0u;
    return 0;
  }

  *out_payload = buf + payload_offset;
  *out_payload_len = msg->payload_len;
  return 0;
}
