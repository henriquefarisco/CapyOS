#ifndef DRIVERS_NET_VMXNET3_H
#define DRIVERS_NET_VMXNET3_H

#include <stdint.h>

int vmxnet3_init(uint64_t bar0, uint64_t bar1, uint8_t mac[6]);
int vmxnet3_ready(void);
int vmxnet3_send_frame(const uint8_t *frame, uint16_t len);
int vmxnet3_poll_frame(uint8_t *out, uint16_t cap, uint16_t *len);

#endif /* DRIVERS_NET_VMXNET3_H */
