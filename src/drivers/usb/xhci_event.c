/* xhci_event.c — Unified event-ring pump + dispatcher.
 *
 * Split from xhci.c (2026-05-21) to keep each TU ≤ 900 lines.
 *
 * Etapa 3 — Slice 3D event ring hardening rationale lives here.
 * The dispatcher routes each event to its owner (cmd_pending,
 * ep0_pending[], intr_pending[]) and advances past strays so the
 * producer never observes Event Ring Buffer Overflow.
 */
#include "drivers/usb/internal/xhci_internal.h"

#include <stddef.h>
#include <stdint.h>

static void xhci_advance_event_ring(struct xhci_controller *xhci) {
  xhci->evt_ring_idx = (xhci->evt_ring_idx + 1) % XHCI_EVT_RING_TRBS;
  if (xhci->evt_ring_idx == 0) xhci->evt_ring_cycle ^= 1;
  if (xhci->rt_base) {
    mmio_write64(xhci->rt_base + XHCI_IR0_ERDP,
                 (uint64_t)(uintptr_t)&xhci->evt_ring[xhci->evt_ring_idx] |
                     XHCI_ERDP_EHB);
  }
}

/* Etapa 3 — Slice 3D event ring hardening.
 *
 * Unified event-ring dispatcher. Replaces three separate, partially
 * duplicated cycle-bit loops (xhci_wait_command_completion,
 * xhci_wait_transfer_completion, xhci_poll_interrupt) that previously
 * left non-matching Transfer Events stuck at the consumer head. The
 * dispatcher routes each event to its owner once and advances the ring
 * past strays so the producer never observes Event Ring Buffer
 * Overflow. Owners read their results from `cmd_pending`,
 * `ep0_pending[]` or `intr_pending[]` and clear `valid` when consumed.
 *
 * Type-0 (Reserved per xHCI 1.2 §6.4.4) is treated as a stop marker so
 * that a recently-wrapped consumer position sitting on uninitialised
 * memory does not consume garbage. Real hardware never produces
 * type-0 events. */
static void xhci_dispatch_event(struct xhci_controller *xhci,
                                struct xhci_trb *evt) {
  uint32_t ctrl = mmio_read32((volatile uint32_t *)&evt->control);
  uint32_t st = mmio_read32((volatile uint32_t *)&evt->status);
  uint32_t type = (ctrl >> 10) & 0x3Fu;
  uint8_t slot = (uint8_t)((ctrl >> 24) & 0xFFu);
  uint8_t ep_dci = (uint8_t)((ctrl >> 16) & 0x1Fu);
  uint32_t cc = (st >> 24) & 0xFFu;
  uint32_t residual = st & 0x00FFFFFFu;

  if (type == TRB_TYPE_CMD_COMPLETE) {
    /* Latest command completion wins; a previous unread completion
     * would have been a software bug (commands are serialised). */
    xhci->cmd_pending.valid = 1u;
    xhci->cmd_pending.slot = slot;
    xhci->cmd_pending.cc = cc;
    xhci->cmd_pending.residual = 0u;
    return;
  }
  if (type != TRB_TYPE_TRANSFER) {
    /* Port Status Change, Bandwidth Request, etc. — not consumed by
     * the current bring-up. Advance past them silently. */
    return;
  }
  if (slot == 0u || slot > xhci->max_slots) {
    xhci->event_stray_count++;
    return;
  }
  if (ep_dci == 1u && xhci->ep0_rings[slot]) {
    xhci->ep0_pending[slot].valid = 1u;
    xhci->ep0_pending[slot].slot = slot;
    xhci->ep0_pending[slot].cc = cc;
    xhci->ep0_pending[slot].residual = residual;
    return;
  }
  if (xhci->intr_rings[slot] && ep_dci == xhci->intr_ep_dci[slot]) {
    xhci->intr_pending[slot].valid = 1u;
    xhci->intr_pending[slot].slot = slot;
    xhci->intr_pending[slot].cc = cc;
    xhci->intr_pending[slot].residual = residual;
    return;
  }
  /* Transfer event for a slot/endpoint we do not currently own. The
   * device was likely released between command issue and event
   * delivery, or the controller posted a spurious event. Counting
   * surfaces these without stalling the ring. */
  xhci->event_stray_count++;
}

void xhci_event_pump(struct xhci_controller *xhci) {
  if (!xhci || !xhci->evt_ring) return;
  /* Bounded by ring size: at most one full segment can be valid
   * between consumer and producer at any instant. */
  for (uint32_t safety = 0; safety < XHCI_EVT_RING_TRBS; safety++) {
    struct xhci_trb *evt = &xhci->evt_ring[xhci->evt_ring_idx];
    uint32_t ctrl = mmio_read32((volatile uint32_t *)&evt->control);
    uint32_t type = (ctrl >> 10) & 0x3Fu;
    if ((ctrl & 1u) != (uint32_t)(xhci->evt_ring_cycle & 1)) return;
    /* Type 0 is Reserved per spec; in the test harness this also marks
     * fresh zero memory just past the producer head after wrap. */
    if (type == 0u) return;
    xhci_dispatch_event(xhci, evt);
    xhci_advance_event_ring(xhci);
  }
}
