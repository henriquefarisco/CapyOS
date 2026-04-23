#ifndef NET_STACK_IPV4_H
#define NET_STACK_IPV4_H

#include "net/stack.h"
#include "stack_arp.h"
#include "stack_icmp.h"
#include "stack_services.h"

#include <stddef.h>
#include <stdint.h>

#define NET_IPV4_HEADER_SIZE 20u

struct net_stack_ipv4_runtime {
  uint8_t initialized;
  uint8_t ready;
  uint16_t *ident_seq;
  uint32_t *age_ticks;
  struct net_ipv4_config *ipv4;
  struct net_stack_stats *stats;
  struct net_arp_entry *arp_entries;
  uint32_t arp_capacity;
  struct net_dhcp_state *dhcp;
  struct net_dns_state *dns;
  struct net_icmp_state *icmp;
};

typedef int (*net_stack_ipv4_send_frame_fn)(const uint8_t *frame, uint16_t len);
typedef int (*net_stack_ipv4_send_ipv4_fn)(uint8_t protocol, uint32_t dst_ip,
                                           const uint8_t *payload,
                                           size_t payload_len);

int net_stack_ipv4_send_frame(struct net_stack_ipv4_runtime *runtime,
                              uint8_t protocol, uint32_t src_ip,
                              uint32_t dst_ip, const uint8_t dst_mac[6],
                              const uint8_t *payload, size_t payload_len,
                              net_stack_ipv4_send_frame_fn send_frame);
int net_stack_ipv4_handle(struct net_stack_ipv4_runtime *runtime,
                          const uint8_t *payload, size_t len,
                          const uint8_t src_mac[6],
                          net_stack_ipv4_send_ipv4_fn send_ipv4);

#endif /* NET_STACK_IPV4_H */
