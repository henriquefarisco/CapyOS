#include "stack_ipv4.h"

#include "stack_utils.h"

#include <stddef.h>
#include <stdint.h>

#define NET_ETHERTYPE_IPV4 0x0800u
#define NET_FRAME_MAX 1600u
#define NET_IPV4_VERSION_IHL 0x45u
#define NET_IPV4_DONT_FRAGMENT 0x4000u

struct net_eth_hdr {
  uint8_t dst[6];
  uint8_t src[6];
  uint16_t ethertype;
} __attribute__((packed));

struct net_ipv4_hdr {
  uint8_t version_ihl;
  uint8_t dscp_ecn;
  uint16_t total_len;
  uint16_t ident;
  uint16_t flags_frag_off;
  uint8_t ttl;
  uint8_t protocol;
  uint16_t checksum;
  uint32_t src_ip;
  uint32_t dst_ip;
} __attribute__((packed));

struct net_udp_hdr {
  uint16_t src_port;
  uint16_t dst_port;
  uint16_t len;
  uint16_t checksum;
} __attribute__((packed));

struct net_tcp_hdr {
  uint16_t src_port;
  uint16_t dst_port;
  uint32_t seq;
  uint32_t ack;
  uint16_t flags;
  uint16_t window;
  uint16_t checksum;
  uint16_t urgent;
} __attribute__((packed));

static int runtime_ready(const struct net_stack_ipv4_runtime *runtime) {
  return runtime && runtime->ipv4 && runtime->stats && runtime->ident_seq &&
         runtime->age_ticks && runtime->arp_entries && runtime->dhcp &&
         runtime->dns && runtime->icmp;
}

static int ip_is_local(const struct net_stack_ipv4_runtime *runtime,
                       uint32_t ip) {
  return runtime_ready(runtime) && ip == runtime->ipv4->addr;
}

static int ip_is_broadcast(uint32_t ip) {
  return ip == NET_IPV4_ADDR(255, 255, 255, 255);
}

static void build_ipv4_header(struct net_stack_ipv4_runtime *runtime,
                              struct net_ipv4_hdr *ip, uint16_t payload_len,
                              uint8_t protocol, uint32_t src_ip,
                              uint32_t dst_ip) {
  ip->version_ihl = NET_IPV4_VERSION_IHL;
  ip->dscp_ecn = 0;
  ip->total_len = net_stack_htons16(
      (uint16_t)(sizeof(struct net_ipv4_hdr) + payload_len));
  ip->ident = net_stack_htons16((*runtime->ident_seq)++);
  ip->flags_frag_off = net_stack_htons16(NET_IPV4_DONT_FRAGMENT);
  ip->ttl = runtime->ipv4->ttl ? runtime->ipv4->ttl : 64;
  ip->protocol = protocol;
  ip->checksum = 0;
  ip->src_ip = net_stack_htonl32(src_ip);
  ip->dst_ip = net_stack_htonl32(dst_ip);
  ip->checksum = net_stack_htons16(
      net_stack_checksum16((const uint8_t *)ip, sizeof(struct net_ipv4_hdr)));
}

static int handle_udp(struct net_stack_ipv4_runtime *runtime,
                      const uint8_t *payload, size_t len) {
  const struct net_udp_hdr *udp;
  const uint8_t *udp_payload;
  size_t udp_len;
  uint16_t src_port;
  uint16_t dst_port;

  if (!runtime_ready(runtime) || len < sizeof(struct net_udp_hdr)) {
    if (runtime && runtime->stats) {
      runtime->stats->frames_drop++;
    }
    return -1;
  }
  udp = (const struct net_udp_hdr *)payload;
  udp_payload = payload + sizeof(struct net_udp_hdr);
  udp_len = len - sizeof(struct net_udp_hdr);
  src_port = net_stack_ntohs16(udp->src_port);
  dst_port = net_stack_ntohs16(udp->dst_port);
  runtime->stats->udp_rx++;
  if (src_port == NET_DHCP_SERVER_PORT && dst_port == NET_DHCP_CLIENT_PORT) {
    return net_dhcp_handle(runtime->dhcp, runtime->stats, runtime->ipv4,
                           udp_payload, udp_len);
  }
  if (src_port == NET_DNS_SERVER_PORT && dst_port == NET_DNS_CLIENT_PORT) {
    return net_dns_handle(runtime->dns, udp_payload, udp_len);
  }
  return 0;
}

static int handle_tcp(struct net_stack_ipv4_runtime *runtime,
                      const uint8_t *payload, size_t len) {
  (void)payload;
  if (!runtime_ready(runtime) || len < sizeof(struct net_tcp_hdr)) {
    if (runtime && runtime->stats) {
      runtime->stats->frames_drop++;
    }
    return -1;
  }
  runtime->stats->tcp_rx++;
  return 0;
}

int net_stack_ipv4_send_frame(struct net_stack_ipv4_runtime *runtime,
                              uint8_t protocol, uint32_t src_ip,
                              uint32_t dst_ip, const uint8_t dst_mac[6],
                              const uint8_t *payload, size_t payload_len,
                              net_stack_ipv4_send_frame_fn send_frame) {
  uint8_t frame[NET_FRAME_MAX];
  size_t frame_len;
  struct net_eth_hdr *eth;
  struct net_ipv4_hdr *ip;

  if (!runtime_ready(runtime) || !runtime->initialized || !runtime->ready ||
      !dst_mac || !payload || payload_len == 0 || !send_frame) {
    if (runtime && runtime->stats) {
      runtime->stats->frames_drop++;
    }
    return -1;
  }

  frame_len = sizeof(struct net_eth_hdr) + sizeof(struct net_ipv4_hdr) +
              payload_len;
  if (frame_len > sizeof(frame)) {
    runtime->stats->frames_drop++;
    return -1;
  }

  eth = (struct net_eth_hdr *)frame;
  net_stack_mem_copy(eth->dst, dst_mac, 6);
  net_stack_mem_copy(eth->src, runtime->ipv4->mac, 6);
  eth->ethertype = net_stack_htons16(NET_ETHERTYPE_IPV4);

  ip = (struct net_ipv4_hdr *)(frame + sizeof(struct net_eth_hdr));
  build_ipv4_header(runtime, ip, (uint16_t)payload_len, protocol, src_ip,
                    dst_ip);
  net_stack_mem_copy((uint8_t *)ip + sizeof(struct net_ipv4_hdr), payload,
                     payload_len);

  if (send_frame(frame, (uint16_t)frame_len) != 0) {
    runtime->stats->frames_drop++;
    return -1;
  }

  runtime->stats->frames_tx++;
  if (protocol == NET_L4_PROTO_ICMP) {
    runtime->stats->icmp_tx++;
  } else if (protocol == NET_L4_PROTO_UDP) {
    runtime->stats->udp_tx++;
  } else if (protocol == NET_L4_PROTO_TCP) {
    runtime->stats->tcp_tx++;
  }
  return 0;
}

int net_stack_ipv4_handle(struct net_stack_ipv4_runtime *runtime,
                          const uint8_t *payload, size_t len,
                          const uint8_t src_mac[6],
                          net_stack_ipv4_send_ipv4_fn send_ipv4) {
  const struct net_ipv4_hdr *ip;
  const uint8_t *l4_payload;
  size_t ihl_bytes;
  size_t l4_len;
  uint16_t total_len;
  uint32_t src_ip;
  uint32_t dst_ip;
  int allow_dhcp_unicast;

  if (!runtime_ready(runtime) || !payload || len < sizeof(struct net_ipv4_hdr)) {
    if (runtime && runtime->stats) {
      runtime->stats->frames_drop++;
    }
    return -1;
  }
  ip = (const struct net_ipv4_hdr *)payload;
  if ((ip->version_ihl >> 4) != 4) {
    runtime->stats->frames_drop++;
    return -1;
  }
  ihl_bytes = (size_t)(ip->version_ihl & 0x0Fu) * 4u;
  if (ihl_bytes < sizeof(struct net_ipv4_hdr) || ihl_bytes > len) {
    runtime->stats->frames_drop++;
    return -1;
  }
  if (net_stack_checksum16((const uint8_t *)ip, ihl_bytes) != 0) {
    runtime->stats->ipv4_bad_checksum++;
    runtime->stats->frames_drop++;
    return -1;
  }
  total_len = net_stack_ntohs16(ip->total_len);
  if (total_len < ihl_bytes || total_len > len) {
    runtime->stats->frames_drop++;
    return -1;
  }

  src_ip = net_stack_ntohl32(ip->src_ip);
  dst_ip = net_stack_ntohl32(ip->dst_ip);
  allow_dhcp_unicast =
      (ip->protocol == NET_L4_PROTO_UDP &&
       (runtime->dhcp->waiting_offer || runtime->dhcp->waiting_ack));
  if (src_mac) {
    net_arp_update(runtime->arp_entries, runtime->arp_capacity,
                   runtime->age_ticks, runtime->stats, src_ip, src_mac);
  }

  runtime->stats->ipv4_seen++;
  if (!ip_is_local(runtime, dst_ip) && !ip_is_broadcast(dst_ip) &&
      !allow_dhcp_unicast) {
    return 0;
  }

  l4_payload = payload + ihl_bytes;
  l4_len = total_len - ihl_bytes;
  switch (ip->protocol) {
  case NET_L4_PROTO_ICMP:
    return net_icmp_handle(runtime->icmp, runtime->stats, src_ip, l4_payload,
                           l4_len, 1, send_ipv4);
  case NET_L4_PROTO_UDP:
    return handle_udp(runtime, l4_payload, l4_len);
  case NET_L4_PROTO_TCP:
    return handle_tcp(runtime, l4_payload, l4_len);
  default:
    runtime->stats->frames_drop++;
    return -1;
  }
}
