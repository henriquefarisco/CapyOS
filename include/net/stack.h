#ifndef NET_STACK_H
#define NET_STACK_H

#include <stddef.h>
#include <stdint.h>

#include "drivers/net/net_probe.h"

#define NET_IPV4_ADDR(a, b, c, d)                                               \
  (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) |       \
   (uint32_t)(d))

enum net_l4_proto {
  NET_L4_PROTO_ICMP = 1,
  NET_L4_PROTO_TCP = 6,
  NET_L4_PROTO_UDP = 17,
};

struct net_ipv4_config {
  uint32_t addr;
  uint32_t mask;
  uint32_t gateway;
  uint32_t dns;
  uint16_t mtu;
  uint8_t ttl;
  uint8_t mac[6];
};

struct net_stack_stats {
  uint64_t frames_rx;
  uint64_t frames_tx;
  uint64_t frames_drop;
  uint64_t eth_unknown;
  uint64_t arp_seen;
  uint64_t arp_updates;
  uint64_t arp_hits;
  uint64_t arp_misses;
  uint64_t ipv4_seen;
  uint64_t ipv4_bad_checksum;
  uint64_t icmp_rx;
  uint64_t icmp_tx;
  uint64_t udp_rx;
  uint64_t udp_tx;
  uint64_t tcp_rx;
  uint64_t tcp_tx;
};

struct net_stack_status {
  uint8_t initialized;
  uint8_t ready;
  uint32_t arp_entries;
  struct net_nic_probe nic;
  struct net_ipv4_config ipv4;
  struct net_stack_stats stats;
};

int net_stack_init(void);
int net_stack_ready(void);
int net_stack_set_ipv4(uint32_t addr, uint32_t mask, uint32_t gateway,
                       uint32_t dns);
int net_stack_status(struct net_stack_status *out);
int net_stack_receive_frame(const uint8_t *frame, size_t len);
int net_stack_poll(void);
int net_stack_send_ipv4(uint8_t protocol, uint32_t dst_ip,
                        const uint8_t *payload, size_t payload_len);
int net_stack_ping(uint32_t dst_ip, uint32_t timeout_ms, uint32_t *rtt_ms,
                   uint32_t *reply_ip);
int net_stack_protocol_selftest(void);
const char *net_driver_name(uint8_t kind);
void net_ipv4_format(uint32_t ip, char out[16]);

#endif /* NET_STACK_H */
