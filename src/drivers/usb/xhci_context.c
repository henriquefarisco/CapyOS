/* xhci_context.c — TRB / input-context builders + endpoint/port helpers.
 *
 * Split from xhci.c (2026-05-21) to keep each TU ≤ 900 lines.
 * Owns the pure context-layout logic (no MMIO, no allocations) so the
 * builders are trivially unit-testable.
 */
#include "drivers/usb/internal/xhci_internal.h"

#include <stddef.h>
#include <stdint.h>

uint8_t xhci_endpoint_dci(uint8_t ep_addr) {
  uint8_t ep_num = ep_addr & 0x0Fu;
  if (ep_num == 0 || ep_num > 15u) return 0;
  return (uint8_t)((ep_num * 2u) + ((ep_addr & 0x80u) ? 1u : 0u));
}

uint8_t xhci_port_speed_from_status(uint32_t portsc) {
  return (uint8_t)((portsc & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT);
}

uint16_t xhci_ep0_max_packet_size_for_speed(uint8_t port_speed) {
  switch (port_speed) {
    case 3: return 64;
    case 4: return 512;
    case 5: return 512;
    default: return 8;
  }
}

static uint32_t *xhci_context_dwords(void *base, uint32_t context_size,
                                    uint32_t index) {
  return (uint32_t *)((uint8_t *)base + (uint64_t)context_size * index);
}

int xhci_build_address_device_input_context(void *input_ctx,
                                            uint32_t context_size, int port,
                                            uint8_t port_speed,
                                            struct xhci_trb *ep0_ring) {
  uint8_t speed;
  uint16_t mps;
  uint32_t *icc;
  uint32_t *slot_ctx;
  uint32_t *ep0_ctx;
  if (!input_ctx || !ep0_ring || port < 0 || port > 255 ||
      (context_size != 32u && context_size != 64u)) {
    return -1;
  }
  speed = port_speed & 0x0Fu;
  mps = xhci_ep0_max_packet_size_for_speed(speed);
  xhci_memzero(input_ctx, (uint64_t)context_size * 33u);
  icc = xhci_context_dwords(input_ctx, context_size, 0);
  slot_ctx = xhci_context_dwords(input_ctx, context_size, 1);
  ep0_ctx = xhci_context_dwords(input_ctx, context_size, 2);
  icc[1] = (1u << 0) | (1u << 1);
  slot_ctx[0] = ((uint32_t)speed << 20) | (1u << 27);
  slot_ctx[1] = (uint32_t)(port + 1) << 16;
  ep0_ctx[1] = (3u << 1) | (4u << 3) | ((uint32_t)mps << 16);
  ep0_ctx[2] = ((uint32_t)(uintptr_t)ep0_ring & ~0xFu) | 1u;
  ep0_ctx[3] = (uint32_t)((uint64_t)(uintptr_t)ep0_ring >> 32);
  ep0_ctx[4] = 8u;
  return 0;
}

int xhci_build_configure_endpoint_input_context(
    void *input_ctx, uint32_t context_size, uint8_t ep_addr,
    uint16_t max_packet_size, uint8_t interval, struct xhci_trb *transfer_ring) {
  uint8_t dci;
  uint32_t *icc;
  uint32_t *slot_ctx;
  uint32_t *ep_ctx;
  uint32_t max_esit_payload;
  if (!input_ctx || !transfer_ring || max_packet_size == 0 ||
      (context_size != 32u && context_size != 64u)) {
    return -1;
  }
  dci = xhci_endpoint_dci(ep_addr);
  if (dci == 0 || dci >= 32u) return -1;
  xhci_memzero(input_ctx, (uint64_t)context_size * 33u);
  icc = xhci_context_dwords(input_ctx, context_size, 0);
  slot_ctx = xhci_context_dwords(input_ctx, context_size, 1);
  ep_ctx = xhci_context_dwords(input_ctx, context_size, dci + 1u);
  icc[1] = (1u << 0) | (1u << dci);
  slot_ctx[0] = (uint32_t)dci << 27;
  max_esit_payload = max_packet_size;
  ep_ctx[0] = (uint32_t)interval << 16;
  ep_ctx[1] = (3u << 1) | (7u << 3) | ((uint32_t)max_packet_size << 16);
  ep_ctx[2] = ((uint32_t)(uintptr_t)transfer_ring & ~0xFu) | 1u;
  ep_ctx[3] = (uint32_t)((uint64_t)(uintptr_t)transfer_ring >> 32);
  ep_ctx[4] = max_packet_size;
  ep_ctx[5] = max_esit_payload;
  return 0;
}

int xhci_build_normal_trb(struct xhci_trb *trb, void *buf, uint16_t len) {
  if (!trb || !buf || len == 0) return -1;
  trb->param = (uint64_t)(uintptr_t)buf;
  trb->status = len;
  trb->control = (TRB_TYPE_NORMAL << 10) | (1u << 5);
  return 0;
}
