#ifndef DRIVERS_NET_TULIP_H
#define DRIVERS_NET_TULIP_H

#include <stdint.h>

int tulip_init(uint64_t bar0, uint8_t bar0_is_io);
int tulip_ready(void);
int tulip_send_frame(const uint8_t *frame, uint16_t len);
int tulip_poll_frame(uint8_t *out_frame, uint16_t out_cap, uint16_t *out_len);

#endif /* DRIVERS_NET_TULIP_H */
