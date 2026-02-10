#ifndef DRIVERS_NET_E1000_H
#define DRIVERS_NET_E1000_H

#include <stddef.h>
#include <stdint.h>

int e1000_init(uint64_t bar0, uint8_t mac_out[6]);
int e1000_ready(void);
int e1000_send_frame(const uint8_t *frame, uint16_t len);
int e1000_poll_frame(uint8_t *out_frame, uint16_t out_cap, uint16_t *out_len);

#endif /* DRIVERS_NET_E1000_H */
