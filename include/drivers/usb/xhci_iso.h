#ifndef DRIVERS_USB_XHCI_ISO_H
#define DRIVERS_USB_XHCI_ISO_H
#include "drivers/usb/xhci.h"
#include <stddef.h>

#define XHCI_ISO_RING_TRBS 256u
#define XHCI_ISO_DATA_TRBS (XHCI_ISO_RING_TRBS - 1u)
struct xhci_iso_queue {
  struct xhci_trb *ring;
  uint64_t ring_dma, completed_bytes;
  uint16_t producer, consumer, pending;
  uint8_t cycle, failed;
};

/* Caller serializes producer/completion updates and keeps DMA memory alive
 * until hardware endpoint stop/disable has been acknowledged. No allocation
 * or hardware access; publication uses a release fence before the cycle bit. */
int xhci_iso_queue_init(struct xhci_iso_queue *queue,
                       struct xhci_trb *ring, uint64_t ring_dma);
int xhci_iso_enqueue(struct xhci_iso_queue *queue, uint64_t packet_dma,
                    uint16_t bytes);
int xhci_iso_complete(struct xhci_iso_queue *queue, uint64_t trb_dma,
                     unsigned completion_code, uint32_t residual);
int xhci_build_iso_out_context(void *input, unsigned context_size,
    const void *device_context, uint8_t endpoint, uint16_t max_packet,
    uint64_t ring_dma);
#endif
