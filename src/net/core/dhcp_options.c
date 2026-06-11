#include "net/dhcp_options.h"

/*
 * Self-contained DHCP option-block parser. No allocation, no I/O, no kernel
 * deps — pure logic so the host test suite can drive it directly. Extracted
 * verbatim (behaviour-preserving) from stack_services.c; the previous code
 * read each 4-byte field via mem_copy + ntohl, which is exactly a
 * big-endian load, so `dhcp_read_be32` below is equivalent on any host
 * endianness.
 */

#define NET_DHCP_OPTION_SUBNET_MASK 1u
#define NET_DHCP_OPTION_ROUTER 3u
#define NET_DHCP_OPTION_DNS 6u
#define NET_DHCP_OPTION_MESSAGE_TYPE 53u
#define NET_DHCP_OPTION_SERVER_ID 54u
#define NET_DHCP_OPTION_END 255u

static uint32_t dhcp_read_be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

void dhcp_parse_options(const uint8_t *options, size_t len,
                        uint8_t *msg_type, uint32_t *server_id,
                        uint32_t *subnet_mask, uint32_t *router,
                        uint32_t *dns) {
  size_t idx = 0;
  if (!options) {
    return;
  }
  while (idx < len) {
    uint8_t code = options[idx++];
    if (code == 0u) {
      continue;
    }
    if (code == NET_DHCP_OPTION_END) {
      break;
    }
    if (idx >= len) {
      break;
    }
    uint8_t opt_len = options[idx++];
    if ((size_t)opt_len > (len - idx)) {
      break;
    }
    switch (code) {
    case NET_DHCP_OPTION_MESSAGE_TYPE:
      if (msg_type && opt_len >= 1u) {
        *msg_type = options[idx];
      }
      break;
    case NET_DHCP_OPTION_SERVER_ID:
      if (server_id && opt_len >= 4u) {
        *server_id = dhcp_read_be32(&options[idx]);
      }
      break;
    case NET_DHCP_OPTION_SUBNET_MASK:
      if (subnet_mask && opt_len >= 4u) {
        *subnet_mask = dhcp_read_be32(&options[idx]);
      }
      break;
    case NET_DHCP_OPTION_ROUTER:
      if (router && opt_len >= 4u) {
        *router = dhcp_read_be32(&options[idx]);
      }
      break;
    case NET_DHCP_OPTION_DNS:
      if (dns && opt_len >= 4u) {
        *dns = dhcp_read_be32(&options[idx]);
      }
      break;
    default:
      break;
    }
    idx += opt_len;
  }
}
