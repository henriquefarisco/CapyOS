#include "net/dns.h"

#include <stddef.h>
#include <stdint.h>

#define NET_DNS_FLAG_QR 0x8000u
#define NET_DNS_RCODE_MASK 0x000Fu
#define NET_DNS_TYPE_A 0x0001u
#define NET_DNS_CLASS_IN 0x0001u

struct net_dns_header {
  uint16_t id;
  uint16_t flags;
  uint16_t qdcount;
  uint16_t ancount;
  uint16_t nscount;
  uint16_t arcount;
} __attribute__((packed));

static uint16_t read_be16(const uint8_t *ptr) {
  return (uint16_t)(((uint16_t)ptr[0] << 8) | ptr[1]);
}

static uint32_t read_be32(const uint8_t *ptr) {
  return ((uint32_t)ptr[0] << 24) | ((uint32_t)ptr[1] << 16) |
         ((uint32_t)ptr[2] << 8) | (uint32_t)ptr[3];
}

static int skip_name(const uint8_t *msg, size_t len, size_t *offset) {
  size_t pos = 0;
  uint32_t depth = 0;

  if (!msg || !offset || *offset >= len) {
    return -1;
  }

  pos = *offset;
  while (pos < len) {
    uint8_t count = msg[pos];
    if (count == 0u) {
      *offset = pos + 1u;
      return 0;
    }
    if ((count & 0xC0u) == 0xC0u) {
      if ((pos + 1u) >= len || depth++ > 8u) {
        return -1;
      }
      *offset = pos + 2u;
      return 0;
    }
    if (count > 63u || (pos + 1u + count) > len) {
      return -1;
    }
    pos += (size_t)count + 1u;
  }
  return -1;
}

int net_dns_encode_name(const char *host, uint8_t *out, size_t out_cap,
                        size_t *out_len) {
  size_t label_len = 0;
  size_t total = 0;

  if (!host || !out || out_cap == 0u || host[0] == '\0') {
    return -1;
  }

  for (size_t i = 0;; ++i) {
    char c = host[i];
    if (c == '.' || c == '\0') {
      size_t label_start = (i >= label_len) ? (i - label_len) : 0u;
      if (label_len == 0u || label_len > 63u || (total + 1u + label_len) >= out_cap) {
        return -1;
      }
      out[total++] = (uint8_t)label_len;
      for (size_t j = 0; j < label_len; ++j) {
        char ch = host[label_start + j];
        if (ch == '.' || ch == '\0') {
          return -1;
        }
        out[total++] = (uint8_t)ch;
      }
      label_len = 0u;
      if (c == '\0') {
        if (total >= out_cap) {
          return -1;
        }
        out[total++] = 0u;
        if (out_len) {
          *out_len = total;
        }
        return 0;
      }
      continue;
    }
    label_len++;
  }
}

int net_dns_parse_first_a(const uint8_t *msg, size_t len, uint16_t expected_id,
                          uint32_t *out_ip) {
  size_t offset = 0;
  uint16_t qdcount = 0;
  uint16_t ancount = 0;

  if (!msg || len < sizeof(struct net_dns_header) || !out_ip) {
    return -1;
  }

  const struct net_dns_header *hdr = (const struct net_dns_header *)msg;
  if (read_be16((const uint8_t *)&hdr->id) != expected_id) {
    return -1;
  }
  if ((read_be16((const uint8_t *)&hdr->flags) & NET_DNS_FLAG_QR) == 0u) {
    return -1;
  }
  if ((read_be16((const uint8_t *)&hdr->flags) & NET_DNS_RCODE_MASK) != 0u) {
    return -1;
  }

  qdcount = read_be16((const uint8_t *)&hdr->qdcount);
  ancount = read_be16((const uint8_t *)&hdr->ancount);
  offset = sizeof(struct net_dns_header);

  for (uint16_t i = 0; i < qdcount; ++i) {
    if (skip_name(msg, len, &offset) != 0 || (offset + 4u) > len) {
      return -1;
    }
    offset += 4u;
  }

  for (uint16_t i = 0; i < ancount; ++i) {
    uint16_t type = 0;
    uint16_t class_code = 0;
    uint16_t rdlen = 0;

    if (skip_name(msg, len, &offset) != 0 || (offset + 10u) > len) {
      return -1;
    }
    type = read_be16(&msg[offset]);
    class_code = read_be16(&msg[offset + 2u]);
    rdlen = read_be16(&msg[offset + 8u]);
    offset += 10u;
    if ((offset + rdlen) > len) {
      return -1;
    }
    if (type == NET_DNS_TYPE_A && class_code == NET_DNS_CLASS_IN &&
        rdlen == 4u) {
      *out_ip = read_be32(&msg[offset]);
      return 0;
    }
    offset += rdlen;
  }

  return -1;
}
