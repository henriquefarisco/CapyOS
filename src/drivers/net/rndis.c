#include "drivers/net/rndis.h"

struct rndis_msg_hdr {
  uint32_t type;
  uint32_t len;
} __attribute__((packed));

struct rndis_initialize_request_msg {
  uint32_t type;
  uint32_t len;
  uint32_t request_id;
  uint32_t major_version;
  uint32_t minor_version;
  uint32_t max_transfer_size;
} __attribute__((packed));

struct rndis_halt_request_msg {
  uint32_t type;
  uint32_t len;
  uint32_t request_id;
} __attribute__((packed));

struct rndis_query_request_msg {
  uint32_t type;
  uint32_t len;
  uint32_t request_id;
  uint32_t oid;
  uint32_t info_len;
  uint32_t info_offset;
  uint32_t reserved;
} __attribute__((packed));

struct rndis_set_request_msg {
  uint32_t type;
  uint32_t len;
  uint32_t request_id;
  uint32_t oid;
  uint32_t info_len;
  uint32_t info_offset;
  uint32_t reserved;
} __attribute__((packed));

struct rndis_initialize_complete_msg {
  uint32_t type;
  uint32_t len;
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
} __attribute__((packed));

struct rndis_query_complete_msg {
  uint32_t type;
  uint32_t len;
  uint32_t request_id;
  uint32_t status;
  uint32_t info_len;
  uint32_t info_offset;
} __attribute__((packed));

struct rndis_set_complete_msg {
  uint32_t type;
  uint32_t len;
  uint32_t request_id;
  uint32_t status;
} __attribute__((packed));

static void rndis_memcpy(void *dst, const void *src, size_t len) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  if (!d || !s) {
    return;
  }
  for (size_t i = 0; i < len; ++i) {
    d[i] = s[i];
  }
}

static size_t rndis_build_info_request(uint8_t *out, size_t cap, uint32_t type,
                                       uint32_t request_id, uint32_t oid,
                                       const void *payload,
                                       size_t payload_len) {
  struct rndis_query_request_msg *msg = (struct rndis_query_request_msg *)out;
  size_t total_len = sizeof(*msg) + payload_len;

  if (!out || cap < total_len) {
    return 0;
  }

  msg->type = type;
  msg->len = (uint32_t)total_len;
  msg->request_id = request_id;
  msg->oid = oid;
  msg->info_len = (uint32_t)payload_len;
  msg->info_offset = payload_len ? 20u : 0u;
  msg->reserved = 0u;
  if (payload_len) {
    rndis_memcpy(out + sizeof(*msg), payload, payload_len);
  }
  return total_len;
}

size_t rndis_build_initialize_request(uint8_t *out, size_t cap,
                                      uint32_t request_id,
                                      uint32_t major_version,
                                      uint32_t minor_version,
                                      uint32_t max_transfer_size) {
  struct rndis_initialize_request_msg *msg =
      (struct rndis_initialize_request_msg *)out;

  if (!out || cap < sizeof(*msg)) {
    return 0;
  }

  msg->type = RNDIS_MSG_INITIALIZE;
  msg->len = (uint32_t)sizeof(*msg);
  msg->request_id = request_id;
  msg->major_version = major_version;
  msg->minor_version = minor_version;
  msg->max_transfer_size = max_transfer_size;
  return sizeof(*msg);
}

size_t rndis_build_halt_request(uint8_t *out, size_t cap, uint32_t request_id) {
  struct rndis_halt_request_msg *msg = (struct rndis_halt_request_msg *)out;

  if (!out || cap < sizeof(*msg)) {
    return 0;
  }

  msg->type = RNDIS_MSG_HALT;
  msg->len = (uint32_t)sizeof(*msg);
  msg->request_id = request_id;
  return sizeof(*msg);
}

size_t rndis_build_query_request(uint8_t *out, size_t cap, uint32_t request_id,
                                 uint32_t oid, const void *payload,
                                 size_t payload_len) {
  return rndis_build_info_request(out, cap, RNDIS_MSG_QUERY, request_id, oid,
                                  payload, payload_len);
}

size_t rndis_build_set_request(uint8_t *out, size_t cap, uint32_t request_id,
                               uint32_t oid, const void *payload,
                               size_t payload_len) {
  return rndis_build_info_request(out, cap, RNDIS_MSG_SET, request_id, oid,
                                  payload, payload_len);
}

int rndis_parse_initialize_complete(const uint8_t *buf, size_t len,
                                    struct rndis_initialize_complete *out) {
  const struct rndis_initialize_complete_msg *msg =
      (const struct rndis_initialize_complete_msg *)buf;

  if (!buf || !out || len < sizeof(*msg)) {
    return -1;
  }
  if (msg->type != RNDIS_MSG_INITIALIZE_COMPLETE ||
      msg->len < sizeof(*msg) || (size_t)msg->len > len) {
    return -2;
  }

  out->request_id = msg->request_id;
  out->status = msg->status;
  out->major_version = msg->major_version;
  out->minor_version = msg->minor_version;
  out->device_flags = msg->device_flags;
  out->medium = msg->medium;
  out->max_packets_per_transfer = msg->max_packets_per_transfer;
  out->max_transfer_size = msg->max_transfer_size;
  out->packet_alignment_factor = msg->packet_alignment_factor;
  out->af_list_offset = msg->af_list_offset;
  out->af_list_size = msg->af_list_size;
  return 0;
}

int rndis_parse_query_complete(const uint8_t *buf, size_t len,
                               uint32_t expected_request_id,
                               const uint8_t **out_payload,
                               size_t *out_payload_len) {
  const struct rndis_query_complete_msg *msg =
      (const struct rndis_query_complete_msg *)buf;
  size_t payload_offset = 0;

  if (!buf || !out_payload || !out_payload_len || len < sizeof(*msg)) {
    return -1;
  }
  if (msg->type != RNDIS_MSG_QUERY_COMPLETE || msg->len < sizeof(*msg) ||
      (size_t)msg->len > len || msg->request_id != expected_request_id ||
      msg->status != RNDIS_STATUS_SUCCESS) {
    return -2;
  }

  payload_offset = sizeof(uint32_t) * 2u + (size_t)msg->info_offset;
  if (msg->info_len == 0u) {
    *out_payload = NULL;
    *out_payload_len = 0u;
    return 0;
  }
  if (payload_offset > len || (size_t)msg->info_len > (len - payload_offset)) {
    return -3;
  }

  *out_payload = buf + payload_offset;
  *out_payload_len = (size_t)msg->info_len;
  return 0;
}

int rndis_parse_query_complete_u32(const uint8_t *buf, size_t len,
                                   uint32_t expected_request_id,
                                   uint32_t *out_value) {
  const uint8_t *payload = NULL;
  size_t payload_len = 0u;

  if (!out_value) {
    return -1;
  }
  if (rndis_parse_query_complete(buf, len, expected_request_id, &payload,
                                 &payload_len) != 0) {
    return -2;
  }
  if (!payload || payload_len < sizeof(uint32_t)) {
    return -3;
  }

  *out_value = ((uint32_t)payload[0]) | ((uint32_t)payload[1] << 8) |
               ((uint32_t)payload[2] << 16) | ((uint32_t)payload[3] << 24);
  return 0;
}

int rndis_parse_set_complete(const uint8_t *buf, size_t len,
                             uint32_t expected_request_id,
                             struct rndis_set_complete *out) {
  const struct rndis_set_complete_msg *msg =
      (const struct rndis_set_complete_msg *)buf;

  if (!buf || !out || len < sizeof(*msg)) {
    return -1;
  }
  if (msg->type != RNDIS_MSG_SET_COMPLETE || msg->len < sizeof(*msg) ||
      (size_t)msg->len > len || msg->request_id != expected_request_id) {
    return -2;
  }

  out->request_id = msg->request_id;
  out->status = msg->status;
  return 0;
}
