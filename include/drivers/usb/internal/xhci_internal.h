/* xhci_internal.h — Shared helpers between the xhci driver TUs.
 *
 * Split (2026-05-21) when src/drivers/usb/xhci.c was broken into 5
 * focused translation units (xhci.c, xhci_context.c, xhci_event.c,
 * xhci_port_slot.c, xhci_transfer.c). The shared MMIO accessors and
 * the command-ring helpers live here so each TU stays self-contained.
 *
 * Do NOT include this from files outside src/drivers/usb/.
 */
#ifndef DRIVERS_USB_INTERNAL_XHCI_INTERNAL_H
#define DRIVERS_USB_INTERNAL_XHCI_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "drivers/usb/xhci.h"

/* MMIO accessors — kept inline so each TU can call them without an
 * extra function-call hop. */
static inline uint32_t mmio_read32(volatile void *addr) {
  return *(volatile uint32_t *)addr;
}

static inline void mmio_write32(volatile void *addr, uint32_t val) {
  *(volatile uint32_t *)addr = val;
}

static inline void mmio_write64(volatile void *addr, uint64_t val) {
  *(volatile uint64_t *)addr = val;
}

static inline void cpu_relax(void) {
#if defined(__x86_64__) || defined(__i386__)
  __asm__ volatile("pause" ::: "memory");
#else
  __asm__ volatile("" ::: "memory");
#endif
}

/* Freestanding mem helpers, no libc. Defined in xhci.c. */
void xhci_memzero(void *ptr, uint64_t size);
void xhci_memcpy(void *dst, const void *src, uint64_t size);

/* Command-ring helpers. Defined in xhci.c.
 *
 *   xhci_ring_command(): copy a TRB into the next free slot in the
 *   controller command ring, advance the cycle bit, and ring the
 *   doorbell at offset 0. Used by xhci_enable_slot,
 *   xhci_address_device, xhci_release_slot and
 *   xhci_configure_interrupt_endpoint.
 *
 *   xhci_wait_command_completion(): pump the event ring (cooperative,
 *   bounded retries) until the controller posts a Command Completion
 *   event into xhci->cmd_pending. Returns 0 on SUCCESS, -2 on a
 *   non-SUCCESS completion code, -3 on timeout. */
int xhci_ring_command(struct xhci_controller *xhci, struct xhci_trb *trb);
int xhci_wait_command_completion(struct xhci_controller *xhci,
                                 uint8_t *slot_id);

#endif /* DRIVERS_USB_INTERNAL_XHCI_INTERNAL_H */
