#ifndef NET_STACK_SELFTEST_H
#define NET_STACK_SELFTEST_H

#include "net/stack.h"
#include "stack_arp.h"
#include "stack_icmp.h"

#include <stddef.h>
#include <stdint.h>

typedef int (*net_selftest_receive_frame_fn)(const uint8_t *frame, size_t len);

int net_stack_protocol_selftest_run(const struct net_ipv4_config *ipv4,
                                    struct net_stack_stats *stats,
                                    net_selftest_receive_frame_fn receive_frame);

#endif /* NET_STACK_SELFTEST_H */
