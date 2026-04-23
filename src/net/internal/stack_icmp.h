#ifndef NET_STACK_ICMP_H
#define NET_STACK_ICMP_H

#include "net/stack.h"

#include <stddef.h>
#include <stdint.h>

struct net_icmp_hdr {
  uint8_t type;
  uint8_t code;
  uint16_t checksum;
  uint16_t ident;
  uint16_t sequence;
} __attribute__((packed));

struct net_icmp_state {
  uint8_t waiting;
  uint16_t wait_ident;
  uint16_t wait_seq;
  uint8_t reply_ready;
  uint32_t reply_ip;
};

typedef int (*net_icmp_send_ipv4_fn)(uint8_t protocol, uint32_t dst_ip,
                                     const uint8_t *payload,
                                     size_t payload_len);

void net_icmp_reset(struct net_icmp_state *state);
void net_icmp_begin_wait(struct net_icmp_state *state, uint16_t ident,
                         uint16_t seq);
void net_icmp_end_wait(struct net_icmp_state *state);
int net_icmp_build_echo_request(uint16_t ident, uint16_t seq, uint8_t *out,
                                size_t cap, size_t *out_len);
int net_icmp_handle(struct net_icmp_state *state, struct net_stack_stats *stats,
                    uint32_t src_ip, const uint8_t *payload, size_t len,
                    int to_local, net_icmp_send_ipv4_fn send_ipv4);

#endif /* NET_STACK_ICMP_H */
