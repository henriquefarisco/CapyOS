#ifndef DRIVERS_NET_RTL8139_H
#define DRIVERS_NET_RTL8139_H

#include <stdint.h>

int rtl8139_init(uint64_t bar0, int is_io, uint8_t mac[6]);
int rtl8139_ready(void);
int rtl8139_send_frame(const uint8_t *frame, uint16_t len);
int rtl8139_poll_frame(uint8_t *out, uint16_t cap, uint16_t *len);

#endif /* DRIVERS_NET_RTL8139_H */
