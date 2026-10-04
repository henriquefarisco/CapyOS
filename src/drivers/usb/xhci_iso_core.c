#include "drivers/usb/xhci_iso.h"

#define ISO_TYPE 5u
#define LINK_TYPE 6u
static void publish(struct xhci_trb *trb, uint32_t control) {
  __atomic_thread_fence(__ATOMIC_RELEASE);
  *(volatile uint32_t *)((uint8_t *)trb + 12) = control;
}

int xhci_iso_queue_init(struct xhci_iso_queue *q,
                       struct xhci_trb *ring, uint64_t ring_dma) {
  if (!q) return -1;
  *q = (struct xhci_iso_queue){0};
  if (!ring || !ring_dma || ((uintptr_t)ring & 63u) || (ring_dma & 63u) ||
      (ring_dma & 65535u) + sizeof(*ring) * XHCI_ISO_RING_TRBS > 65536u)
    return -1;
  for (unsigned i = 0; i < XHCI_ISO_RING_TRBS; ++i)
    ring[i] = (struct xhci_trb){0};
  ring[XHCI_ISO_DATA_TRBS].param = ring_dma;
  publish(&ring[XHCI_ISO_DATA_TRBS], (LINK_TYPE << 10) | 2u | 1u);
  q->ring = ring; q->ring_dma = ring_dma; q->cycle = 1;
  return 0;
}

int xhci_iso_enqueue(struct xhci_iso_queue *q, uint64_t packet_dma,
                    uint16_t bytes) {
  if (!q || !q->ring || q->failed || !packet_dma || !bytes || bytes > 1023u ||
      q->producer >= XHCI_ISO_DATA_TRBS || q->consumer >= XHCI_ISO_DATA_TRBS ||
      q->pending >= XHCI_ISO_DATA_TRBS || (packet_dma & 65535u) + bytes > 65536u)
    return -1;
  unsigned index = q->producer;
  struct xhci_trb *trb = &q->ring[index];
  trb->param = packet_dma;
  trb->status = bytes; /* TD Size=0, one packet and one TRB per interval. */
  if (index + 1u == XHCI_ISO_DATA_TRBS)
    publish(&q->ring[XHCI_ISO_DATA_TRBS], (LINK_TYPE << 10) | 2u | q->cycle);
  /* SIA, IOC, Isoch. Frame ID/TBC/TLBPC are zero for one FS packet. */
  publish(trb, (1u << 31) | (1u << 5) | (ISO_TYPE << 10) | q->cycle);
  ++q->pending;
  if (++q->producer == XHCI_ISO_DATA_TRBS) { q->producer = 0; q->cycle ^= 1u; }
  return 0;
}

int xhci_iso_complete(struct xhci_iso_queue *q, uint64_t trb_dma,
                     unsigned completion_code, uint32_t residual) {
  if (!q || !q->ring || q->failed) return -1;
  if (!q->pending || q->pending > XHCI_ISO_DATA_TRBS ||
      q->consumer >= XHCI_ISO_DATA_TRBS ||
      trb_dma != q->ring_dma + (uint64_t)q->consumer * sizeof(struct xhci_trb) ||
      completion_code != XHCI_TRB_CC_SUCCESS || residual) {
    q->failed = 1;
    return -1;
  }
  q->completed_bytes += q->ring[q->consumer].status & 0x1ffffu;
  --q->pending;
  if (++q->consumer == XHCI_ISO_DATA_TRBS) q->consumer = 0;
  return 0;
}

int xhci_build_iso_out_context(void *input, unsigned context_size,
    const void *device_context, uint8_t endpoint, uint16_t max_packet,
    uint64_t ring_dma) {
  if (!input || !device_context || ((uintptr_t)input & 3u) ||
      ((uintptr_t)device_context & 3u) || (context_size != 32 && context_size != 64) ||
      !endpoint || endpoint > 15 || !max_packet || max_packet > 1023 ||
      !ring_dma || (ring_dma & 63u)) return -1;
  uintptr_t destination = (uintptr_t)input, source = (uintptr_t)device_context;
  if ((destination >= source && destination - source < 16u) ||
      (source > destination && source - destination < context_size * 33u))
    return -1;
  const uint32_t *old_slot = device_context;
  if (((old_slot[0] >> 20) & 15u) != 1u) return -1; /* full-speed only */
  uint32_t *words = input;
  for (unsigned i = 0; i < context_size * 33u / 4u; ++i) words[i] = 0;
  unsigned dci = endpoint * 2u;
  uint32_t *slot = words + context_size / 4u;
  for (unsigned i = 0; i < 4; ++i) slot[i] = old_slot[i];
  unsigned entries = old_slot[0] >> 27;
  if (entries < dci) entries = dci;
  slot[0] = (slot[0] & 0x07ffffffu) | entries << 27;
  slot[3] = 0; /* output-only device address/state are RsvdZ on input */
  words[1] = 1u | (1u << dci);
  uint32_t *ep = words + (dci + 1u) * context_size / 4u;
  ep[0] = 3u << 16; /* 2^3 microframes = 1 ms */
  ep[1] = (1u << 3) | (uint32_t)max_packet << 16; /* Isoch OUT, CErr=0 */
  ep[2] = (uint32_t)ring_dma | 1u;
  ep[3] = (uint32_t)(ring_dma >> 32);
  ep[4] = max_packet | (uint32_t)max_packet << 16; /* average length + Max ESIT */
  return 0;
}
