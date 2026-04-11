#ifndef DRIVERS_NET_VIRTIO_NET_H
#define DRIVERS_NET_VIRTIO_NET_H

#include <stdint.h>

int virtio_net_init(uint64_t bar0, int is_io, uint8_t mac[6]);
int virtio_net_ready(void);
int virtio_net_send_frame(const uint8_t *frame, uint16_t len);
int virtio_net_poll_frame(uint8_t *out, uint16_t cap, uint16_t *len);

#endif /* DRIVERS_NET_VIRTIO_NET_H */
