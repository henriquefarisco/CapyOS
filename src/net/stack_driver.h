#ifndef NET_STACK_DRIVER_H
#define NET_STACK_DRIVER_H

#include "drivers/net/net_probe.h"

#include <stdint.h>

int net_stack_driver_send_frame(const struct net_nic_probe *nic,
                                const uint8_t *frame, uint16_t len);
int net_stack_driver_poll_frame(const struct net_nic_probe *nic, uint8_t *out,
                                uint16_t cap, uint16_t *len);
int net_stack_driver_init_runtime(const struct net_nic_probe *nic,
                                  uint8_t mac[6]);

#endif /* NET_STACK_DRIVER_H */
