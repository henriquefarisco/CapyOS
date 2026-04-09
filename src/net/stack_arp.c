#include "stack_arp.h"

#include <stddef.h>
#include <stdint.h>

struct net_eth_hdr {
  uint8_t dst[6];
  uint8_t src[6];
  uint16_t ethertype;
} __attribute__((packed));

static void arp_mem_copy(void *dst, const void *src, size_t len) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  for (size_t i = 0; i < len; ++i) {
    d[i] = s[i];
  }
}

static uint16_t arp_htons16(uint16_t v) {
  return (uint16_t)((v << 8) | (v >> 8));
}

static uint32_t arp_htonl32(uint32_t v) {
  return ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8) |
         ((v & 0x00FF0000u) >> 8) | ((v & 0xFF000000u) >> 24);
}

static uint16_t arp_ntohs16(uint16_t v) { return arp_htons16(v); }
static uint32_t arp_ntohl32(uint32_t v) { return arp_htonl32(v); }

static int arp_ip_is_local(uint32_t local_ip, uint32_t ip) {
  return ip == local_ip;
}

static int arp_ip_is_broadcast(uint32_t ip) {
  return ip == NET_IPV4_ADDR(255, 255, 255, 255);
}

static int arp_ip_is_unspecified(uint32_t ip) { return ip == 0u; }

static int arp_ip_is_same_subnet(uint32_t a, uint32_t b, uint32_t mask) {
  return (a & mask) == (b & mask);
}

int net_arp_find(const struct net_arp_entry *entries, uint32_t capacity,
                 uint32_t ip) {
  if (!entries) {
    return -1;
  }
  for (uint32_t i = 0; i < capacity; ++i) {
    if (entries[i].valid && entries[i].ip == ip) {
      return (int)i;
    }
  }
  return -1;
}

void net_arp_update(struct net_arp_entry *entries, uint32_t capacity,
                    uint32_t *age_ticks, struct net_stack_stats *stats,
                    uint32_t ip, const uint8_t mac[6]) {
  int idx = -1;
  if (!entries || !capacity || !age_ticks || !stats || !mac) {
    return;
  }

  idx = net_arp_find(entries, capacity, ip);
  if (idx < 0) {
    for (uint32_t i = 0; i < capacity; ++i) {
      if (!entries[i].valid) {
        idx = (int)i;
        break;
      }
    }
  }
  if (idx < 0) {
    idx = (int)(*age_ticks % capacity);
  }

  entries[idx].valid = 1;
  entries[idx].ip = ip;
  arp_mem_copy(entries[idx].mac, mac, 6);
  entries[idx].age = (*age_ticks)++;
  stats->arp_updates++;
}

uint32_t net_arp_count(const struct net_arp_entry *entries, uint32_t capacity) {
  uint32_t count = 0;
  if (!entries) {
    return 0;
  }
  for (uint32_t i = 0; i < capacity; ++i) {
    if (entries[i].valid) {
      count++;
    }
  }
  return count;
}

uint32_t net_arp_route_next_hop(uint32_t local_ip, uint32_t mask,
                                uint32_t gateway, uint32_t dst_ip) {
  if (arp_ip_is_local(local_ip, dst_ip) || arp_ip_is_broadcast(dst_ip)) {
    return dst_ip;
  }
  if (arp_ip_is_same_subnet(dst_ip, local_ip, mask)) {
    return dst_ip;
  }
  if (arp_ip_is_unspecified(gateway)) {
    return dst_ip;
  }
  return gateway;
}

static int net_arp_send_packet(const struct net_ipv4_config *ipv4,
                               uint16_t opcode, const uint8_t dst_mac[6],
                               const uint8_t target_mac[6],
                               uint32_t sender_ip, uint32_t target_ip,
                               struct net_stack_stats *stats,
                               net_arp_send_frame_fn send_frame) {
  uint8_t frame[sizeof(struct net_eth_hdr) + sizeof(struct net_arp_pkt)];
  struct net_eth_hdr *eth = (struct net_eth_hdr *)frame;
  struct net_arp_pkt *arp =
      (struct net_arp_pkt *)(frame + sizeof(struct net_eth_hdr));

  if (!ipv4 || !dst_mac || !target_mac || !stats || !send_frame) {
    return -1;
  }

  arp_mem_copy(eth->dst, dst_mac, 6);
  arp_mem_copy(eth->src, ipv4->mac, 6);
  eth->ethertype = arp_htons16(NET_ETHERTYPE_ARP);

  arp->htype = arp_htons16(NET_ARP_HTYPE_ETHERNET);
  arp->ptype = arp_htons16(NET_ARP_PTYPE_IPV4);
  arp->hlen = 6;
  arp->plen = 4;
  arp->opcode = arp_htons16(opcode);
  arp_mem_copy(arp->sender_mac, ipv4->mac, 6);
  arp->sender_ip = arp_htonl32(sender_ip);
  arp_mem_copy(arp->target_mac, target_mac, 6);
  arp->target_ip = arp_htonl32(target_ip);

  if (send_frame(frame, (uint16_t)sizeof(frame)) == 0) {
    stats->frames_tx++;
    return 0;
  }
  stats->frames_drop++;
  return -1;
}

int net_arp_send_request(const struct net_ipv4_config *ipv4, uint32_t target_ip,
                         struct net_stack_stats *stats,
                         net_arp_send_frame_fn send_frame) {
  static const uint8_t bcast[6] = {0xFFu, 0xFFu, 0xFFu,
                                   0xFFu, 0xFFu, 0xFFu};
  static const uint8_t zero[6] = {0, 0, 0, 0, 0, 0};
  return net_arp_send_packet(ipv4, NET_ARP_OP_REQUEST, bcast, zero,
                             ipv4 ? ipv4->addr : 0u, target_ip, stats,
                             send_frame);
}

int net_arp_send_reply(const struct net_ipv4_config *ipv4,
                       const uint8_t target_mac[6], uint32_t target_ip,
                       struct net_stack_stats *stats,
                       net_arp_send_frame_fn send_frame) {
  return net_arp_send_packet(ipv4, NET_ARP_OP_REPLY, target_mac, target_mac,
                             ipv4 ? ipv4->addr : 0u, target_ip, stats,
                             send_frame);
}

static int net_arp_wait(const struct net_arp_entry *entries, uint32_t capacity,
                        uint32_t target_ip, uint32_t timeout_ms,
                        net_arp_poll_fn poll_fn, net_arp_delay_fn delay_fn) {
  if (!entries || !poll_fn || !delay_fn) {
    return -1;
  }
  for (uint32_t elapsed = 0; elapsed < timeout_ms; ++elapsed) {
    if (net_arp_find(entries, capacity, target_ip) >= 0) {
      return 0;
    }
    (void)poll_fn();
    delay_fn();
  }
  return -1;
}

int net_arp_resolve(struct net_arp_entry *entries, uint32_t capacity,
                    uint32_t *age_ticks, struct net_stack_stats *stats,
                    const struct net_ipv4_config *ipv4, uint32_t target_ip,
                    net_arp_send_frame_fn send_frame, net_arp_poll_fn poll_fn,
                    net_arp_delay_fn delay_fn) {
  if (!entries || !age_ticks || !stats || !ipv4 || !send_frame || !poll_fn ||
      !delay_fn) {
    return -1;
  }
  if (net_arp_find(entries, capacity, target_ip) >= 0) {
    return 0;
  }

  for (uint32_t tries = 0; tries < 3; ++tries) {
    stats->arp_misses++;
    if (net_arp_send_request(ipv4, target_ip, stats, send_frame) != 0) {
      continue;
    }
    if (net_arp_wait(entries, capacity, target_ip, 200u, poll_fn, delay_fn) ==
        0) {
      return 0;
    }
  }
  return -1;
}

int net_arp_handle(struct net_arp_entry *entries, uint32_t capacity,
                   uint32_t *age_ticks, struct net_stack_stats *stats,
                   const struct net_ipv4_config *ipv4, const uint8_t *payload,
                   size_t len, int ready, net_arp_send_frame_fn send_frame) {
  const struct net_arp_pkt *arp = NULL;
  uint32_t sender_ip = 0;
  uint32_t target_ip = 0;
  uint16_t opcode = 0;

  if (!entries || !capacity || !age_ticks || !stats || !ipv4 || !payload) {
    return -1;
  }
  if (len < sizeof(struct net_arp_pkt)) {
    stats->frames_drop++;
    return -1;
  }

  arp = (const struct net_arp_pkt *)payload;
  if (arp_ntohs16(arp->htype) != NET_ARP_HTYPE_ETHERNET ||
      arp_ntohs16(arp->ptype) != NET_ARP_PTYPE_IPV4 || arp->hlen != 6 ||
      arp->plen != 4) {
    stats->frames_drop++;
    return -1;
  }

  sender_ip = arp_ntohl32(arp->sender_ip);
  target_ip = arp_ntohl32(arp->target_ip);
  opcode = arp_ntohs16(arp->opcode);
  stats->arp_seen++;
  net_arp_update(entries, capacity, age_ticks, stats, sender_ip,
                 arp->sender_mac);

  if (opcode == NET_ARP_OP_REQUEST && arp_ip_is_local(ipv4->addr, target_ip)) {
    stats->arp_hits++;
    if (ready) {
      (void)net_arp_send_reply(ipv4, arp->sender_mac, sender_ip, stats,
                               send_frame);
    }
  } else if (opcode == NET_ARP_OP_REPLY &&
             arp_ip_is_local(ipv4->addr, target_ip)) {
    stats->arp_hits++;
  }
  return 0;
}
