#include "stack_selftest.h"

#include <stddef.h>
#include <stdint.h>

#define NET_ETHERTYPE_ARP 0x0806u
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

static void selftest_mem_zero(void *ptr, size_t len) {
  uint8_t *p = (uint8_t *)ptr;
  for (size_t i = 0; i < len; ++i) {
    p[i] = 0;
  }
}

static void selftest_mem_copy(void *dst, const void *src, size_t len) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  for (size_t i = 0; i < len; ++i) {
    d[i] = s[i];
  }
}

static uint16_t selftest_htons16(uint16_t v) {
  return (uint16_t)((v << 8) | (v >> 8));
}

static uint32_t selftest_htonl32(uint32_t v) {
  return ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8) |
         ((v & 0x00FF0000u) >> 8) | ((v & 0xFF000000u) >> 24);
}

static uint16_t selftest_checksum16(const uint8_t *data, size_t len) {
  uint32_t sum = 0;
  while (len >= 2) {
    uint16_t word = (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
    sum += word;
    data += 2;
    len -= 2;
  }
  if (len) {
    sum += (uint16_t)((uint16_t)data[0] << 8);
  }
  while (sum >> 16) {
    sum = (sum & 0xFFFFu) + (sum >> 16);
  }
  return (uint16_t)(~sum);
}

static void build_ipv4_header(struct net_ipv4_hdr *ip, uint16_t payload_len,
                              uint8_t protocol, uint32_t src_ip,
                              uint32_t dst_ip, uint8_t ttl) {
  ip->version_ihl = NET_IPV4_VERSION_IHL;
  ip->dscp_ecn = 0;
  ip->total_len =
      selftest_htons16((uint16_t)(sizeof(struct net_ipv4_hdr) + payload_len));
  ip->ident = selftest_htons16(0x2222u);
  ip->flags_frag_off = selftest_htons16(NET_IPV4_DONT_FRAGMENT);
  ip->ttl = ttl ? ttl : 64u;
  ip->protocol = protocol;
  ip->checksum = 0;
  ip->src_ip = selftest_htonl32(src_ip);
  ip->dst_ip = selftest_htonl32(dst_ip);
  ip->checksum =
      selftest_htons16(selftest_checksum16((const uint8_t *)ip, sizeof(*ip)));
}

static size_t build_l2_l3(uint8_t *frame, const uint8_t src_mac[6],
                          const uint8_t dst_mac[6], uint32_t src_ip,
                          uint32_t dst_ip, uint8_t proto, uint16_t l4_len,
                          uint8_t ttl) {
  struct net_eth_hdr *eth = (struct net_eth_hdr *)frame;
  struct net_ipv4_hdr *ip = NULL;
  selftest_mem_copy(eth->dst, dst_mac, 6);
  selftest_mem_copy(eth->src, src_mac, 6);
  eth->ethertype = selftest_htons16(NET_ETHERTYPE_IPV4);

  ip = (struct net_ipv4_hdr *)(frame + sizeof(struct net_eth_hdr));
  build_ipv4_header(ip, l4_len, proto, src_ip, dst_ip, ttl);
  return sizeof(struct net_eth_hdr) + sizeof(struct net_ipv4_hdr);
}

int net_stack_protocol_selftest_run(const struct net_ipv4_config *ipv4,
                                    struct net_stack_stats *stats,
                                    net_selftest_receive_frame_fn receive_frame) {
  struct net_stack_stats before;
  const uint8_t peer_mac[6] = {0x52u, 0x54u, 0x00u, 0x12u, 0x34u, 0x99u};
  const uint32_t peer_ip = NET_IPV4_ADDR(10, 0, 2, 200);

  if (!ipv4 || !stats || !receive_frame) {
    return -1;
  }
  before = *stats;

  {
    uint8_t frame[sizeof(struct net_eth_hdr) + sizeof(struct net_arp_pkt)];
    struct net_eth_hdr *eth = (struct net_eth_hdr *)frame;
    struct net_arp_pkt *arp = (struct net_arp_pkt *)(frame + sizeof(*eth));
    for (int i = 0; i < 6; ++i) {
      eth->dst[i] = 0xFFu;
    }
    selftest_mem_copy(eth->src, peer_mac, 6);
    eth->ethertype = selftest_htons16(NET_ETHERTYPE_ARP);
    arp->htype = selftest_htons16(NET_ARP_HTYPE_ETHERNET);
    arp->ptype = selftest_htons16(NET_ARP_PTYPE_IPV4);
    arp->hlen = 6;
    arp->plen = 4;
    arp->opcode = selftest_htons16(NET_ARP_OP_REQUEST);
    selftest_mem_copy(arp->sender_mac, peer_mac, 6);
    arp->sender_ip = selftest_htonl32(peer_ip);
    selftest_mem_zero(arp->target_mac, 6);
    arp->target_ip = selftest_htonl32(ipv4->addr);
    (void)receive_frame(frame, sizeof(frame));
  }

  {
    uint8_t frame[NET_FRAME_MAX];
    size_t off = build_l2_l3(frame, peer_mac, ipv4->mac, peer_ip, ipv4->addr,
                             NET_L4_PROTO_ICMP,
                             (uint16_t)(sizeof(struct net_icmp_hdr) + 4u),
                             ipv4->ttl);
    struct net_icmp_hdr *icmp = (struct net_icmp_hdr *)(frame + off);
    uint8_t *data = (uint8_t *)(icmp + 1);
    icmp->type = 8;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->ident = selftest_htons16(0xCAFEu);
    icmp->sequence = selftest_htons16(1u);
    data[0] = 0x43u;
    data[1] = 0x41u;
    data[2] = 0x50u;
    data[3] = 0x59u;
    icmp->checksum = selftest_htons16(
        selftest_checksum16((const uint8_t *)icmp, sizeof(*icmp) + 4u));
    (void)receive_frame(frame, off + sizeof(*icmp) + 4u);
  }

  {
    uint8_t frame[NET_FRAME_MAX];
    size_t off = build_l2_l3(frame, peer_mac, ipv4->mac, peer_ip, ipv4->addr,
                             NET_L4_PROTO_UDP, sizeof(struct net_udp_hdr),
                             ipv4->ttl);
    struct net_udp_hdr *udp = (struct net_udp_hdr *)(frame + off);
    udp->src_port = selftest_htons16(5353u);
    udp->dst_port = selftest_htons16(9000u);
    udp->len = selftest_htons16(sizeof(struct net_udp_hdr));
    udp->checksum = 0;
    (void)receive_frame(frame, off + sizeof(*udp));
  }

  {
    uint8_t frame[NET_FRAME_MAX];
    size_t off = build_l2_l3(frame, peer_mac, ipv4->mac, peer_ip, ipv4->addr,
                             NET_L4_PROTO_TCP, sizeof(struct net_tcp_hdr),
                             ipv4->ttl);
    struct net_tcp_hdr *tcp = (struct net_tcp_hdr *)(frame + off);
    tcp->src_port = selftest_htons16(40000u);
    tcp->dst_port = selftest_htons16(8080u);
    tcp->seq = selftest_htonl32(1u);
    tcp->ack = 0;
    tcp->flags = selftest_htons16((uint16_t)((5u << 12) | 0x0002u));
    tcp->window = selftest_htons16(64240u);
    tcp->checksum = 0;
    tcp->urgent = 0;
    (void)receive_frame(frame, off + sizeof(*tcp));
  }

  if (stats->arp_seen <= before.arp_seen || stats->icmp_rx <= before.icmp_rx ||
      stats->udp_rx <= before.udp_rx || stats->tcp_rx <= before.tcp_rx) {
    return -1;
  }
  return 0;
}
