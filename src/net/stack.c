#include "net/stack.h"

#include <stddef.h>
#include <stdint.h>

#define NET_ETHERTYPE_IPV4 0x0800u
#define NET_ETHERTYPE_ARP 0x0806u
#define NET_ARP_HTYPE_ETHERNET 0x0001u
#define NET_ARP_PTYPE_IPV4 0x0800u
#define NET_ARP_OP_REQUEST 0x0001u
#define NET_ARP_OP_REPLY 0x0002u
#define NET_ARP_CAPACITY 16u
#define NET_FRAME_MAX 1600u
#define NET_IPV4_VERSION_IHL 0x45u
#define NET_IPV4_DONT_FRAGMENT 0x4000u

struct net_eth_hdr {
  uint8_t dst[6];
  uint8_t src[6];
  uint16_t ethertype;
} __attribute__((packed));

struct net_arp_pkt {
  uint16_t htype;
  uint16_t ptype;
  uint8_t hlen;
  uint8_t plen;
  uint16_t opcode;
  uint8_t sender_mac[6];
  uint32_t sender_ip;
  uint8_t target_mac[6];
  uint32_t target_ip;
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

struct net_icmp_hdr {
  uint8_t type;
  uint8_t code;
  uint16_t checksum;
  uint16_t ident;
  uint16_t sequence;
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

struct net_arp_entry {
  uint32_t ip;
  uint8_t mac[6];
  uint8_t valid;
  uint32_t age;
};

struct net_state {
  uint8_t initialized;
  uint8_t ready;
  uint16_t ident_seq;
  uint32_t age_ticks;
  struct net_nic_probe nic;
  struct net_ipv4_config ipv4;
  struct net_stack_stats stats;
  struct net_arp_entry arp[NET_ARP_CAPACITY];
};

static struct net_state g_net;

static void mem_zero(void *ptr, size_t len) {
  uint8_t *p = (uint8_t *)ptr;
  for (size_t i = 0; i < len; ++i) {
    p[i] = 0;
  }
}

static void mem_copy(void *dst, const void *src, size_t len) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  for (size_t i = 0; i < len; ++i) {
    d[i] = s[i];
  }
}

static uint16_t htons16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }

static uint32_t htonl32(uint32_t v) {
  return ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8) |
         ((v & 0x00FF0000u) >> 8) | ((v & 0xFF000000u) >> 24);
}

static uint16_t ntohs16(uint16_t v) { return htons16(v); }
static uint32_t ntohl32(uint32_t v) { return htonl32(v); }

static uint16_t checksum16(const uint8_t *data, size_t len) {
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

static int ip_is_local(uint32_t ip) { return ip == g_net.ipv4.addr; }

static int ip_is_broadcast(uint32_t ip) { return ip == NET_IPV4_ADDR(255, 255, 255, 255); }

static int arp_find(uint32_t ip) {
  for (uint32_t i = 0; i < NET_ARP_CAPACITY; ++i) {
    if (g_net.arp[i].valid && g_net.arp[i].ip == ip) {
      return (int)i;
    }
  }
  return -1;
}

static void arp_update(uint32_t ip, const uint8_t mac[6]) {
  int idx = arp_find(ip);
  if (idx < 0) {
    for (uint32_t i = 0; i < NET_ARP_CAPACITY; ++i) {
      if (!g_net.arp[i].valid) {
        idx = (int)i;
        break;
      }
    }
  }
  if (idx < 0) {
    idx = (int)(g_net.age_ticks % NET_ARP_CAPACITY);
  }
  g_net.arp[idx].valid = 1;
  g_net.arp[idx].ip = ip;
  mem_copy(g_net.arp[idx].mac, mac, 6);
  g_net.arp[idx].age = g_net.age_ticks++;
  g_net.stats.arp_updates++;
}

static uint32_t arp_count(void) {
  uint32_t count = 0;
  for (uint32_t i = 0; i < NET_ARP_CAPACITY; ++i) {
    if (g_net.arp[i].valid) {
      count++;
    }
  }
  return count;
}

static void set_default_config(void) {
  g_net.ipv4.addr = NET_IPV4_ADDR(10, 0, 2, 15);
  g_net.ipv4.mask = NET_IPV4_ADDR(255, 255, 255, 0);
  g_net.ipv4.gateway = NET_IPV4_ADDR(10, 0, 2, 2);
  g_net.ipv4.dns = NET_IPV4_ADDR(1, 1, 1, 1);
  g_net.ipv4.mtu = 1500;
  g_net.ipv4.ttl = 64;
  g_net.ipv4.mac[0] = 0x02u;
  g_net.ipv4.mac[1] = 0xCAu;
  g_net.ipv4.mac[2] = 0x50u;
  g_net.ipv4.mac[3] = 0x59u;
  g_net.ipv4.mac[4] = 0x00u;
  g_net.ipv4.mac[5] = 0x64u;
}

static int handle_arp(const uint8_t *payload, size_t len) {
  if (len < sizeof(struct net_arp_pkt)) {
    g_net.stats.frames_drop++;
    return -1;
  }
  const struct net_arp_pkt *arp = (const struct net_arp_pkt *)payload;
  if (ntohs16(arp->htype) != NET_ARP_HTYPE_ETHERNET ||
      ntohs16(arp->ptype) != NET_ARP_PTYPE_IPV4 || arp->hlen != 6 ||
      arp->plen != 4) {
    g_net.stats.frames_drop++;
    return -1;
  }

  uint32_t sender_ip = ntohl32(arp->sender_ip);
  uint32_t target_ip = ntohl32(arp->target_ip);
  uint16_t opcode = ntohs16(arp->opcode);
  g_net.stats.arp_seen++;
  arp_update(sender_ip, arp->sender_mac);

  if (opcode == NET_ARP_OP_REQUEST && ip_is_local(target_ip)) {
    g_net.stats.arp_hits++;
  }
  if (opcode == NET_ARP_OP_REPLY && ip_is_local(target_ip)) {
    g_net.stats.arp_hits++;
  }
  return 0;
}

static int handle_icmp(const uint8_t *payload, size_t len, int to_local) {
  if (len < sizeof(struct net_icmp_hdr)) {
    g_net.stats.frames_drop++;
    return -1;
  }
  const struct net_icmp_hdr *icmp = (const struct net_icmp_hdr *)payload;
  g_net.stats.icmp_rx++;
  if (to_local && icmp->type == 8 && icmp->code == 0) {
    /* Echo request received; reply path is tracked even before NIC TX is wired.
     */
    g_net.stats.icmp_tx++;
  }
  return 0;
}

static int handle_udp(const uint8_t *payload, size_t len) {
  (void)payload;
  if (len < sizeof(struct net_udp_hdr)) {
    g_net.stats.frames_drop++;
    return -1;
  }
  g_net.stats.udp_rx++;
  return 0;
}

static int handle_tcp(const uint8_t *payload, size_t len) {
  (void)payload;
  if (len < sizeof(struct net_tcp_hdr)) {
    g_net.stats.frames_drop++;
    return -1;
  }
  g_net.stats.tcp_rx++;
  return 0;
}

static int handle_ipv4(const uint8_t *payload, size_t len,
                       const uint8_t src_mac[6]) {
  if (len < sizeof(struct net_ipv4_hdr)) {
    g_net.stats.frames_drop++;
    return -1;
  }
  const struct net_ipv4_hdr *ip = (const struct net_ipv4_hdr *)payload;
  if ((ip->version_ihl >> 4) != 4) {
    g_net.stats.frames_drop++;
    return -1;
  }
  size_t ihl_bytes = (size_t)(ip->version_ihl & 0x0Fu) * 4u;
  if (ihl_bytes < sizeof(struct net_ipv4_hdr) || ihl_bytes > len) {
    g_net.stats.frames_drop++;
    return -1;
  }
  if (checksum16((const uint8_t *)ip, ihl_bytes) != 0) {
    g_net.stats.ipv4_bad_checksum++;
    g_net.stats.frames_drop++;
    return -1;
  }
  uint16_t total_len = ntohs16(ip->total_len);
  if (total_len < ihl_bytes || total_len > len) {
    g_net.stats.frames_drop++;
    return -1;
  }

  uint32_t src_ip = ntohl32(ip->src_ip);
  uint32_t dst_ip = ntohl32(ip->dst_ip);
  if (src_mac) {
    arp_update(src_ip, src_mac);
  }

  g_net.stats.ipv4_seen++;
  if (!ip_is_local(dst_ip) && !ip_is_broadcast(dst_ip)) {
    return 0;
  }

  const uint8_t *l4_payload = payload + ihl_bytes;
  size_t l4_len = total_len - ihl_bytes;
  switch (ip->protocol) {
  case NET_L4_PROTO_ICMP:
    return handle_icmp(l4_payload, l4_len, 1);
  case NET_L4_PROTO_UDP:
    return handle_udp(l4_payload, l4_len);
  case NET_L4_PROTO_TCP:
    return handle_tcp(l4_payload, l4_len);
  default:
    g_net.stats.frames_drop++;
    return -1;
  }
}

int net_stack_init(void) {
  mem_zero(&g_net, sizeof(g_net));
  set_default_config();
  g_net.ident_seq = 1;

  if (net_probe_first_supported(&g_net.nic) == 0 && g_net.nic.found) {
    g_net.ready = 1;
    g_net.ipv4.mtu = g_net.nic.mtu;
    mem_copy(g_net.ipv4.mac, g_net.nic.mac, 6);
  } else {
    g_net.ready = 0;
  }

  g_net.initialized = 1;
  return g_net.ready ? 0 : -1;
}

int net_stack_ready(void) { return g_net.initialized && g_net.ready; }

int net_stack_set_ipv4(uint32_t addr, uint32_t mask, uint32_t gateway,
                       uint32_t dns) {
  if (!g_net.initialized) {
    return -1;
  }
  g_net.ipv4.addr = addr;
  g_net.ipv4.mask = mask;
  g_net.ipv4.gateway = gateway;
  g_net.ipv4.dns = dns;
  return 0;
}

int net_stack_status(struct net_stack_status *out) {
  if (!out || !g_net.initialized) {
    return -1;
  }
  out->initialized = g_net.initialized;
  out->ready = g_net.ready;
  out->arp_entries = arp_count();
  out->nic = g_net.nic;
  out->ipv4 = g_net.ipv4;
  out->stats = g_net.stats;
  return 0;
}

int net_stack_receive_frame(const uint8_t *frame, size_t len) {
  if (!g_net.initialized || !frame || len < sizeof(struct net_eth_hdr)) {
    return -1;
  }

  const struct net_eth_hdr *eth = (const struct net_eth_hdr *)frame;
  uint16_t ethertype = ntohs16(eth->ethertype);
  const uint8_t *payload = frame + sizeof(struct net_eth_hdr);
  size_t payload_len = len - sizeof(struct net_eth_hdr);

  g_net.stats.frames_rx++;
  switch (ethertype) {
  case NET_ETHERTYPE_ARP:
    return handle_arp(payload, payload_len);
  case NET_ETHERTYPE_IPV4:
    return handle_ipv4(payload, payload_len, eth->src);
  default:
    g_net.stats.eth_unknown++;
    g_net.stats.frames_drop++;
    return -1;
  }
}

static void build_ipv4_header(struct net_ipv4_hdr *ip, uint16_t payload_len,
                              uint8_t protocol, uint32_t src_ip,
                              uint32_t dst_ip, uint16_t ident) {
  ip->version_ihl = NET_IPV4_VERSION_IHL;
  ip->dscp_ecn = 0;
  ip->total_len =
      htons16((uint16_t)(sizeof(struct net_ipv4_hdr) + payload_len));
  ip->ident = htons16(ident);
  ip->flags_frag_off = htons16(NET_IPV4_DONT_FRAGMENT);
  ip->ttl = g_net.ipv4.ttl ? g_net.ipv4.ttl : 64;
  ip->protocol = protocol;
  ip->checksum = 0;
  ip->src_ip = htonl32(src_ip);
  ip->dst_ip = htonl32(dst_ip);
  ip->checksum = checksum16((const uint8_t *)ip, sizeof(struct net_ipv4_hdr));
}

int net_stack_send_ipv4(uint8_t protocol, uint32_t dst_ip,
                        const uint8_t *payload, size_t payload_len) {
  if (!g_net.initialized || !payload || payload_len == 0) {
    return -1;
  }
  if (!g_net.ready) {
    g_net.stats.frames_drop++;
    return -1;
  }

  size_t ip_payload_max =
      (g_net.ipv4.mtu > sizeof(struct net_ipv4_hdr))
          ? (g_net.ipv4.mtu - sizeof(struct net_ipv4_hdr))
          : 0;
  if (payload_len > ip_payload_max) {
    g_net.stats.frames_drop++;
    return -1;
  }

  g_net.stats.frames_tx++;
  int arp_idx = arp_find(dst_ip);
  if (arp_idx < 0) {
    g_net.stats.arp_misses++;
    g_net.stats.frames_drop++;
    return -1;
  }
  g_net.stats.arp_hits++;

  uint8_t frame[NET_FRAME_MAX];
  size_t frame_len =
      sizeof(struct net_eth_hdr) + sizeof(struct net_ipv4_hdr) + payload_len;
  if (frame_len > sizeof(frame)) {
    g_net.stats.frames_drop++;
    return -1;
  }

  struct net_eth_hdr *eth = (struct net_eth_hdr *)frame;
  mem_copy(eth->dst, g_net.arp[arp_idx].mac, 6);
  mem_copy(eth->src, g_net.ipv4.mac, 6);
  eth->ethertype = htons16(NET_ETHERTYPE_IPV4);

  struct net_ipv4_hdr *ip =
      (struct net_ipv4_hdr *)(frame + sizeof(struct net_eth_hdr));
  build_ipv4_header(ip, (uint16_t)payload_len, protocol, g_net.ipv4.addr,
                    dst_ip, g_net.ident_seq++);

  mem_copy((uint8_t *)ip + sizeof(struct net_ipv4_hdr), payload, payload_len);

  if (protocol == NET_L4_PROTO_ICMP) {
    g_net.stats.icmp_tx++;
  } else if (protocol == NET_L4_PROTO_UDP) {
    g_net.stats.udp_tx++;
  } else if (protocol == NET_L4_PROTO_TCP) {
    g_net.stats.tcp_tx++;
  }

  /* Driver TX hook is still pending; this path validates packet assembly. */
  (void)frame;
  (void)frame_len;
  g_net.stats.frames_drop++;
  return -1;
}

static size_t build_l2_l3(uint8_t *frame, const uint8_t src_mac[6],
                          const uint8_t dst_mac[6], uint32_t src_ip,
                          uint32_t dst_ip, uint8_t proto, uint16_t l4_len) {
  struct net_eth_hdr *eth = (struct net_eth_hdr *)frame;
  mem_copy(eth->dst, dst_mac, 6);
  mem_copy(eth->src, src_mac, 6);
  eth->ethertype = htons16(NET_ETHERTYPE_IPV4);

  struct net_ipv4_hdr *ip =
      (struct net_ipv4_hdr *)(frame + sizeof(struct net_eth_hdr));
  build_ipv4_header(ip, l4_len, proto, src_ip, dst_ip, 0x2222u);
  return sizeof(struct net_eth_hdr) + sizeof(struct net_ipv4_hdr);
}

int net_stack_protocol_selftest(void) {
  if (!g_net.initialized) {
    return -1;
  }

  struct net_stack_stats before = g_net.stats;
  const uint8_t peer_mac[6] = {0x52u, 0x54u, 0x00u, 0x12u, 0x34u, 0x56u};
  const uint32_t peer_ip = NET_IPV4_ADDR(10, 0, 2, 2);

  /* 1) ARP request to local IP. */
  {
    uint8_t frame[sizeof(struct net_eth_hdr) + sizeof(struct net_arp_pkt)];
    struct net_eth_hdr *eth = (struct net_eth_hdr *)frame;
    for (int i = 0; i < 6; ++i) {
      eth->dst[i] = 0xFFu;
    }
    mem_copy(eth->src, peer_mac, 6);
    eth->ethertype = htons16(NET_ETHERTYPE_ARP);

    struct net_arp_pkt *arp = (struct net_arp_pkt *)(frame + sizeof(*eth));
    arp->htype = htons16(NET_ARP_HTYPE_ETHERNET);
    arp->ptype = htons16(NET_ARP_PTYPE_IPV4);
    arp->hlen = 6;
    arp->plen = 4;
    arp->opcode = htons16(NET_ARP_OP_REQUEST);
    mem_copy(arp->sender_mac, peer_mac, 6);
    arp->sender_ip = htonl32(peer_ip);
    mem_zero(arp->target_mac, 6);
    arp->target_ip = htonl32(g_net.ipv4.addr);
    (void)net_stack_receive_frame(frame, sizeof(frame));
  }

  /* 2) ICMP echo request. */
  {
    uint8_t frame[NET_FRAME_MAX];
    size_t off = build_l2_l3(frame, peer_mac, g_net.ipv4.mac, peer_ip,
                             g_net.ipv4.addr, NET_L4_PROTO_ICMP,
                             (uint16_t)(sizeof(struct net_icmp_hdr) + 4u));
    struct net_icmp_hdr *icmp = (struct net_icmp_hdr *)(frame + off);
    icmp->type = 8;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->ident = htons16(0xCAFEu);
    icmp->sequence = htons16(1);
    uint8_t *data = (uint8_t *)(icmp + 1);
    data[0] = 0x43u;
    data[1] = 0x41u;
    data[2] = 0x50u;
    data[3] = 0x59u;
    icmp->checksum = checksum16((const uint8_t *)icmp, sizeof(*icmp) + 4u);
    (void)net_stack_receive_frame(frame, off + sizeof(*icmp) + 4u);
  }

  /* 3) UDP datagram to local port. */
  {
    uint8_t frame[NET_FRAME_MAX];
    size_t off = build_l2_l3(frame, peer_mac, g_net.ipv4.mac, peer_ip,
                             g_net.ipv4.addr, NET_L4_PROTO_UDP,
                             sizeof(struct net_udp_hdr));
    struct net_udp_hdr *udp = (struct net_udp_hdr *)(frame + off);
    udp->src_port = htons16(5353u);
    udp->dst_port = htons16(9000u);
    udp->len = htons16(sizeof(struct net_udp_hdr));
    udp->checksum = 0;
    (void)net_stack_receive_frame(frame, off + sizeof(*udp));
  }

  /* 4) TCP SYN segment to local port. */
  {
    uint8_t frame[NET_FRAME_MAX];
    size_t off = build_l2_l3(frame, peer_mac, g_net.ipv4.mac, peer_ip,
                             g_net.ipv4.addr, NET_L4_PROTO_TCP,
                             sizeof(struct net_tcp_hdr));
    struct net_tcp_hdr *tcp = (struct net_tcp_hdr *)(frame + off);
    tcp->src_port = htons16(40000u);
    tcp->dst_port = htons16(8080u);
    tcp->seq = htonl32(1u);
    tcp->ack = 0;
    tcp->flags = htons16((uint16_t)((5u << 12) | 0x0002u));
    tcp->window = htons16(64240u);
    tcp->checksum = 0;
    tcp->urgent = 0;
    (void)net_stack_receive_frame(frame, off + sizeof(*tcp));
  }

  if (g_net.stats.arp_seen <= before.arp_seen ||
      g_net.stats.icmp_rx <= before.icmp_rx ||
      g_net.stats.udp_rx <= before.udp_rx ||
      g_net.stats.tcp_rx <= before.tcp_rx) {
    return -1;
  }

  return 0;
}

const char *net_driver_name(uint8_t kind) { return net_probe_kind_name(kind); }
