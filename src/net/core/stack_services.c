#include "../internal/stack_services.h"

#include "net/dns.h"

#include <stddef.h>
#include <stdint.h>

#define NET_DHCP_MAGIC 0x63825363u
#define NET_DHCP_OPTION_SUBNET_MASK 1u
#define NET_DHCP_OPTION_ROUTER 3u
#define NET_DHCP_OPTION_DNS 6u
#define NET_DHCP_OPTION_REQUESTED_IP 50u
#define NET_DHCP_OPTION_MESSAGE_TYPE 53u
#define NET_DHCP_OPTION_SERVER_ID 54u
#define NET_DHCP_OPTION_PARAM_REQ_LIST 55u
#define NET_DHCP_OPTION_END 255u

struct net_udp_hdr {
  uint16_t src_port;
  uint16_t dst_port;
  uint16_t len;
  uint16_t checksum;
} __attribute__((packed));

struct net_bootp_hdr {
  uint8_t op;
  uint8_t htype;
  uint8_t hlen;
  uint8_t hops;
  uint32_t xid;
  uint16_t secs;
  uint16_t flags;
  uint32_t ciaddr;
  uint32_t yiaddr;
  uint32_t siaddr;
  uint32_t giaddr;
  uint8_t chaddr[16];
  uint8_t sname[64];
  uint8_t file[128];
  uint32_t magic;
} __attribute__((packed));

static void services_mem_zero(void *ptr, size_t len) {
  uint8_t *p = (uint8_t *)ptr;
  if (!p) {
    return;
  }
  for (size_t i = 0; i < len; ++i) {
    p[i] = 0;
  }
}

static void services_mem_copy(void *dst, const void *src, size_t len) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  if (!d || !s) {
    return;
  }
  for (size_t i = 0; i < len; ++i) {
    d[i] = s[i];
  }
}

static int services_mem_equal(const void *a, const void *b, size_t len) {
  const uint8_t *left = (const uint8_t *)a;
  const uint8_t *right = (const uint8_t *)b;
  if (!left || !right) {
    return 0;
  }
  for (size_t i = 0; i < len; ++i) {
    if (left[i] != right[i]) {
      return 0;
    }
  }
  return 1;
}

static uint16_t services_htons16(uint16_t v) {
  return (uint16_t)((v << 8) | (v >> 8));
}

static uint32_t services_htonl32(uint32_t v) {
  return ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8) |
         ((v & 0x00FF0000u) >> 8) | ((v & 0xFF000000u) >> 24);
}

static uint32_t services_ntohl32(uint32_t v) { return services_htonl32(v); }

static uint16_t read_be16(const uint8_t *ptr) {
  return (uint16_t)(((uint16_t)ptr[0] << 8) | ptr[1]);
}

void net_dhcp_reset(struct net_dhcp_state *state) {
  services_mem_zero(state, sizeof(*state));
}

void net_dns_reset(struct net_dns_state *state) {
  services_mem_zero(state, sizeof(*state));
}

static void dhcp_parse_options(const uint8_t *options, size_t len,
                               uint8_t *msg_type, uint32_t *server_id,
                               uint32_t *subnet_mask, uint32_t *router,
                               uint32_t *dns) {
  size_t idx = 0;
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
        uint32_t raw = 0;
        services_mem_copy(&raw, &options[idx], 4u);
        *server_id = services_ntohl32(raw);
      }
      break;
    case NET_DHCP_OPTION_SUBNET_MASK:
      if (subnet_mask && opt_len >= 4u) {
        uint32_t raw = 0;
        services_mem_copy(&raw, &options[idx], 4u);
        *subnet_mask = services_ntohl32(raw);
      }
      break;
    case NET_DHCP_OPTION_ROUTER:
      if (router && opt_len >= 4u) {
        uint32_t raw = 0;
        services_mem_copy(&raw, &options[idx], 4u);
        *router = services_ntohl32(raw);
      }
      break;
    case NET_DHCP_OPTION_DNS:
      if (dns && opt_len >= 4u) {
        uint32_t raw = 0;
        services_mem_copy(&raw, &options[idx], 4u);
        *dns = services_ntohl32(raw);
      }
      break;
    default:
      break;
    }
    idx += opt_len;
  }
}

int net_dhcp_handle(struct net_dhcp_state *state, struct net_stack_stats *stats,
                    const struct net_ipv4_config *ipv4,
                    const uint8_t *payload, size_t len) {
  const struct net_bootp_hdr *bootp = NULL;
  uint8_t msg_type = 0;
  uint32_t server_id = 0;
  uint32_t subnet_mask = 0;
  uint32_t router = 0;
  uint32_t dns = 0;

  if (!state || !stats || !ipv4 || !payload) {
    return -1;
  }
  if (len < sizeof(struct net_bootp_hdr)) {
    stats->frames_drop++;
    return -1;
  }

  bootp = (const struct net_bootp_hdr *)payload;
  if (bootp->op != 2u || bootp->htype != 1u || bootp->hlen != 6u ||
      services_ntohl32(bootp->xid) != state->xid ||
      services_ntohl32(bootp->magic) != NET_DHCP_MAGIC ||
      !services_mem_equal(bootp->chaddr, ipv4->mac, 6)) {
    return 0;
  }

  dhcp_parse_options(payload + sizeof(struct net_bootp_hdr),
                     len - sizeof(struct net_bootp_hdr), &msg_type, &server_id,
                     &subnet_mask, &router, &dns);

  if (msg_type == NET_DHCP_MSG_OFFER && state->waiting_offer) {
    state->offer_ready = 1;
    state->offered_ip = services_ntohl32(bootp->yiaddr);
    state->server_id = server_id;
    state->subnet_mask = subnet_mask;
    state->router = router;
    state->dns = dns;
  } else if (msg_type == NET_DHCP_MSG_ACK && state->waiting_ack) {
    state->ack_ready = 1;
    state->ack_ip = services_ntohl32(bootp->yiaddr);
    if (server_id) {
      state->server_id = server_id;
    }
    if (subnet_mask) {
      state->subnet_mask = subnet_mask;
    }
    if (router) {
      state->router = router;
    }
    if (dns) {
      state->dns = dns;
    }
  } else if (msg_type == NET_DHCP_MSG_NAK && state->waiting_ack) {
    state->nak_received = 1;
  }

  return 0;
}

int net_dhcp_send_message(struct net_dhcp_state *state,
                          const struct net_ipv4_config *ipv4, uint8_t msg_type,
                          uint32_t requested_ip, uint32_t server_id,
                          net_service_send_ipv4_fn send_ipv4) {
  uint8_t udp_payload[sizeof(struct net_udp_hdr) + sizeof(struct net_bootp_hdr) +
                      32];
  struct net_udp_hdr *udp = (struct net_udp_hdr *)udp_payload;
  struct net_bootp_hdr *bootp =
      (struct net_bootp_hdr *)(udp_payload + sizeof(struct net_udp_hdr));
  uint8_t *options = udp_payload + sizeof(struct net_udp_hdr) +
                     sizeof(struct net_bootp_hdr);
  size_t opt_len = 0;

  if (!state || !ipv4 || !send_ipv4) {
    return -1;
  }

  services_mem_zero(udp_payload, sizeof(udp_payload));
  bootp->op = 1u;
  bootp->htype = 1u;
  bootp->hlen = 6u;
  bootp->xid = services_htonl32(state->xid);
  bootp->flags = services_htons16(0x8000u);
  services_mem_copy(bootp->chaddr, ipv4->mac, 6);
  bootp->magic = services_htonl32(NET_DHCP_MAGIC);

  options[opt_len++] = NET_DHCP_OPTION_MESSAGE_TYPE;
  options[opt_len++] = 1u;
  options[opt_len++] = msg_type;
  if (msg_type == NET_DHCP_MSG_REQUEST) {
    uint32_t requested_ip_be = services_htonl32(requested_ip);
    uint32_t server_id_be = services_htonl32(server_id);
    options[opt_len++] = NET_DHCP_OPTION_REQUESTED_IP;
    options[opt_len++] = 4u;
    services_mem_copy(&options[opt_len], &requested_ip_be, 4u);
    opt_len += 4u;
    options[opt_len++] = NET_DHCP_OPTION_SERVER_ID;
    options[opt_len++] = 4u;
    services_mem_copy(&options[opt_len], &server_id_be, 4u);
    opt_len += 4u;
  }
  options[opt_len++] = NET_DHCP_OPTION_PARAM_REQ_LIST;
  options[opt_len++] = 3u;
  options[opt_len++] = NET_DHCP_OPTION_SUBNET_MASK;
  options[opt_len++] = NET_DHCP_OPTION_ROUTER;
  options[opt_len++] = NET_DHCP_OPTION_DNS;
  options[opt_len++] = NET_DHCP_OPTION_END;

  udp->src_port = services_htons16(NET_DHCP_CLIENT_PORT);
  udp->dst_port = services_htons16(NET_DHCP_SERVER_PORT);
  udp->len =
      services_htons16((uint16_t)(sizeof(struct net_udp_hdr) +
                                  sizeof(struct net_bootp_hdr) + opt_len));
  udp->checksum = 0;

  return send_ipv4(NET_L4_PROTO_UDP, NET_IPV4_ADDR(255, 255, 255, 255),
                   udp_payload,
                   sizeof(struct net_udp_hdr) + sizeof(struct net_bootp_hdr) +
                       opt_len);
}

int net_dns_send_query(struct net_dns_state *state,
                       const struct net_ipv4_config *ipv4,
                       const char *hostname,
                       net_service_send_ipv4_fn send_ipv4) {
  uint8_t udp_payload[sizeof(struct net_udp_hdr) + 300u];
  struct net_udp_hdr *udp = (struct net_udp_hdr *)udp_payload;
  uint8_t *dns_msg = udp_payload + sizeof(struct net_udp_hdr);
  size_t dns_len = 0;
  size_t name_len = 0;

  if (!state || !ipv4 || !hostname || !send_ipv4 || ipv4->dns == 0u) {
    return -1;
  }

  services_mem_zero(udp_payload, sizeof(udp_payload));
  dns_msg[0] = (uint8_t)((state->query_id >> 8) & 0xFFu);
  dns_msg[1] = (uint8_t)(state->query_id & 0xFFu);
  dns_msg[2] = 0x01u;
  dns_msg[3] = 0x00u;
  dns_msg[4] = 0x00u;
  dns_msg[5] = 0x01u;

  if (net_dns_encode_name(hostname, dns_msg + 12u,
                          sizeof(udp_payload) -
                              sizeof(struct net_udp_hdr) - 16u,
                          &name_len) != 0) {
    return -1;
  }
  dns_len = 12u + name_len;
  dns_msg[dns_len++] = 0x00u;
  dns_msg[dns_len++] = 0x01u;
  dns_msg[dns_len++] = 0x00u;
  dns_msg[dns_len++] = 0x01u;

  udp->src_port = services_htons16(NET_DNS_CLIENT_PORT);
  udp->dst_port = services_htons16(NET_DNS_SERVER_PORT);
  udp->len = services_htons16((uint16_t)(sizeof(struct net_udp_hdr) + dns_len));
  udp->checksum = 0;
  return send_ipv4(NET_L4_PROTO_UDP, ipv4->dns, udp_payload,
                   sizeof(struct net_udp_hdr) + dns_len);
}

int net_dns_handle(struct net_dns_state *state, const uint8_t *payload,
                   size_t len) {
  if (!state || !payload || !state->waiting_reply || len < 12u) {
    return 0;
  }
  if (read_be16(payload) != state->query_id) {
    return 0;
  }
  if (net_dns_parse_first_a(payload, len, state->query_id,
                            &state->answer_ip) == 0) {
    state->response_ready = 1;
  } else {
    state->response_failed = 1;
  }
  return 0;
}
