#ifndef NET_STACK_ARP_H
#define NET_STACK_ARP_H

#include "net/stack.h"

#include <stddef.h>
#include <stdint.h>

#define NET_ETHERTYPE_ARP 0x0806u
#define NET_ARP_HTYPE_ETHERNET 0x0001u
#define NET_ARP_PTYPE_IPV4 0x0800u
#define NET_ARP_OP_REQUEST 0x0001u
#define NET_ARP_OP_REPLY 0x0002u
#define NET_ARP_CAPACITY 16u

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

struct net_arp_entry {
  uint32_t ip;
  uint8_t mac[6];
  uint8_t valid;
  uint32_t age;
};

typedef int (*net_arp_send_frame_fn)(const uint8_t *frame, uint16_t len);
typedef int (*net_arp_poll_fn)(void);
typedef void (*net_arp_delay_fn)(void);

int net_arp_find(const struct net_arp_entry *entries, uint32_t capacity,
                 uint32_t ip);
void net_arp_update(struct net_arp_entry *entries, uint32_t capacity,
                    uint32_t *age_ticks, struct net_stack_stats *stats,
                    uint32_t ip, const uint8_t mac[6]);
uint32_t net_arp_count(const struct net_arp_entry *entries, uint32_t capacity);
uint32_t net_arp_route_next_hop(uint32_t local_ip, uint32_t mask,
                                uint32_t gateway, uint32_t dst_ip);
int net_arp_send_request(const struct net_ipv4_config *ipv4, uint32_t target_ip,
                         struct net_stack_stats *stats,
                         net_arp_send_frame_fn send_frame);
int net_arp_send_reply(const struct net_ipv4_config *ipv4,
                       const uint8_t target_mac[6], uint32_t target_ip,
                       struct net_stack_stats *stats,
                       net_arp_send_frame_fn send_frame);
int net_arp_resolve(struct net_arp_entry *entries, uint32_t capacity,
                    uint32_t *age_ticks, struct net_stack_stats *stats,
                    const struct net_ipv4_config *ipv4, uint32_t target_ip,
                    net_arp_send_frame_fn send_frame, net_arp_poll_fn poll_fn,
                    net_arp_delay_fn delay_fn);
int net_arp_handle(struct net_arp_entry *entries, uint32_t capacity,
                   uint32_t *age_ticks, struct net_stack_stats *stats,
                   const struct net_ipv4_config *ipv4, const uint8_t *payload,
                   size_t len, int ready, net_arp_send_frame_fn send_frame);

#endif /* NET_STACK_ARP_H */
