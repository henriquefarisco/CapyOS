/* xhci_port_slot.c — Port + slot management (enable, address, release).
 *
 * Split from xhci.c (2026-05-21) to keep each TU ≤ 900 lines.
 * Owns:
 *   - xhci_port_get_status / xhci_port_reset / xhci_port_ack_csc.
 *   - xhci_enable_slot / xhci_address_device / xhci_release_slot.
 */
#include "drivers/usb/internal/xhci_internal.h"

#include <stddef.h>
#include <stdint.h>

extern void *kmalloc_aligned(uint64_t size, uint64_t alignment);
extern void kfree_aligned(void *ptr);

/* Get port status */
int xhci_port_get_status(struct xhci_controller *xhci, int port) {
  if (!xhci->initialized || port < 0 || port >= xhci->max_ports)
    return -1;

  /* Port registers start at op_base + 0x400, each port is 16 bytes */
  volatile uint32_t *portsc =
      (volatile uint32_t *)(xhci->op_base + 0x400 + port * 16);
  return (int)mmio_read32(portsc);
}

/* Reset a port */
int xhci_port_reset(struct xhci_controller *xhci, int port) {
  if (!xhci->initialized || port < 0 || port >= xhci->max_ports)
    return -1;

  volatile uint32_t *portsc =
      (volatile uint32_t *)(xhci->op_base + 0x400 + port * 16);

  /* Set Port Reset bit */
  uint32_t val = mmio_read32(portsc);
  val |= XHCI_PORTSC_PR;
  val &=
      ~(XHCI_PORTSC_CSC | XHCI_PORTSC_PRC); /* Clear change bits by writing 0 */
  mmio_write32(portsc, val);

  /* Wait for reset to complete (PRC set) */
  for (int i = 0; i < 500000; i++) {
    val = mmio_read32(portsc);
    if (val & XHCI_PORTSC_PRC) {
      uint32_t cur;
      int j;
      /* Clear PRC by writing 1 */
      mmio_write32(portsc, val | XHCI_PORTSC_PRC);
      /* Etapa 3 — Slice 3D §15.5 fix: xHCI 1.2 §4.3 step 6 says
       * the xHC sets PED atomically with PRC, but some emulators
       * (and edge-case real hardware) take a small additional
       * window. `xhci_address_device` requires PED=1; without this
       * wait it would return -2 erroneously when the controller
       * is slightly slow. Bounded by a short retry budget so a
       * truly non-compliant controller still surfaces as -3. */
      for (j = 0; j < 10000; j++) {
        cur = mmio_read32(portsc);
        if (cur & XHCI_PORTSC_PED) return 0;
        cpu_relax();
      }
      return -3; /* PED never asserted after reset */
    }
    cpu_relax();
  }

  return -2; /* Reset timeout */
}

/* Etapa 3 — Slice 3D §15.1 fix support: clear CSC change bit
 * preserving the other RW1C change bits. Without this helper,
 * `usb_hotplug_check` would re-fire on every poll tick because
 * writing 0 to a RW1C bit has no effect. */
int xhci_port_ack_csc(struct xhci_controller *xhci, int port) {
  volatile uint32_t *portsc;
  uint32_t val;
  uint32_t writeback;
  if (!xhci || !xhci->initialized || port < 0 || port >= xhci->max_ports) {
    return -1;
  }
  portsc = (volatile uint32_t *)(xhci->op_base + 0x400 + port * 16);
  val = mmio_read32(portsc);
  /* Preserve RW / RWS bits but mask off all RW1C change bits so the
   * write only acks the one we want (CSC). */
  writeback = (val & ~XHCI_PORTSC_CHANGE_BITS) | XHCI_PORTSC_CSC;
  mmio_write32(portsc, writeback);
  return 0;
}

/* --- Slot and device operations (minimal bring-up) --- */

int xhci_enable_slot(struct xhci_controller *xhci, uint8_t *slot_id) {
  uint8_t completed_slot = 0;
  int rc;
  if (!xhci || !xhci->initialized || !xhci->cmd_ring || !slot_id) return -1;
  *slot_id = 0;
  struct xhci_trb trb;
  trb.param = 0;
  trb.status = 0;
  trb.control = (TRB_TYPE_ENABLE_SLOT << 10);
  if (xhci_ring_command(xhci, &trb) != 0) return -1;
  rc = xhci_wait_command_completion(xhci, &completed_slot);
  if (rc != 0 || completed_slot == 0 || completed_slot > xhci->max_slots) {
    *slot_id = 0;
    return (rc != 0) ? rc : -2;
  }
  *slot_id = completed_slot;
  return 0;
}

int xhci_address_device(struct xhci_controller *xhci, uint8_t slot_id, int port) {
  uint32_t portsc;
  uint8_t speed;
  uint32_t ctx_size;
  uint64_t input_size;
  uint64_t device_size;
  void *input_ctx;
  void *device_ctx;
  struct xhci_trb *ep0_ring;
  struct xhci_trb trb;
  int rc;
  if (!xhci || !xhci->initialized || slot_id == 0 || slot_id > xhci->max_slots ||
      port < 0 || port >= xhci->max_ports || !xhci->dcbaa) {
    return -1;
  }
  if (xhci->device_contexts[slot_id] || xhci->ep0_rings[slot_id]) return -1;
  portsc = (uint32_t)xhci_port_get_status(xhci, port);
  if (!(portsc & XHCI_PORTSC_CCS) || !(portsc & XHCI_PORTSC_PED)) return -2;
  speed = xhci_port_speed_from_status(portsc);
  ctx_size = xhci->context_size ? xhci->context_size : 32u;
  input_size = (uint64_t)ctx_size * 33u;
  device_size = (uint64_t)ctx_size * 32u;
  input_ctx = kmalloc_aligned(input_size, 64);
  device_ctx = kmalloc_aligned(device_size, 64);
  ep0_ring = (struct xhci_trb *)kmalloc_aligned(
      XHCI_CMD_RING_TRBS * sizeof(struct xhci_trb), 64);
  if (!input_ctx || !device_ctx || !ep0_ring) {
    if (input_ctx) kfree_aligned(input_ctx);
    if (device_ctx) kfree_aligned(device_ctx);
    if (ep0_ring) kfree_aligned(ep0_ring);
    return -3;
  }
  xhci_memzero(input_ctx, input_size);
  xhci_memzero(device_ctx, device_size);
  xhci_memzero(ep0_ring, XHCI_CMD_RING_TRBS * sizeof(struct xhci_trb));
  ep0_ring[XHCI_CMD_RING_TRBS - 1].param = (uint64_t)(uintptr_t)ep0_ring;
  ep0_ring[XHCI_CMD_RING_TRBS - 1].status = 0;
  ep0_ring[XHCI_CMD_RING_TRBS - 1].control =
      (TRB_TYPE_LINK << 10) | (1 << 1) | 1;
  if (xhci_build_address_device_input_context(input_ctx, ctx_size, port, speed,
                                              ep0_ring) != 0) {
    kfree_aligned(input_ctx);
    kfree_aligned(device_ctx);
    kfree_aligned(ep0_ring);
    return -4;
  }
  /* Etapa 3 — Slice 3D §15.2 fix: store all per-slot pointers BEFORE
   * issuing Address Device so the failure cleanup path can delegate
   * to `xhci_release_slot`, which emits Disable Slot to the
   * controller AND frees the allocations. The previous flow only
   * freed our own pointers but left the controller-side slot in the
   * Enabled state (slot leak). */
  xhci->dcbaa[slot_id] = (uint64_t)(uintptr_t)device_ctx;
  xhci->device_contexts[slot_id] = device_ctx;
  xhci->ep0_rings[slot_id] = ep0_ring;
  xhci->ep0_ring_idx[slot_id] = 0;
  xhci->ep0_ring_cycle[slot_id] = 1;
  trb.param = (uint64_t)(uintptr_t)input_ctx;
  trb.status = 0;
  trb.control = (TRB_TYPE_ADDRESS_DEV << 10) | ((uint32_t)slot_id << 24);
  rc = xhci_ring_command(xhci, &trb);
  if (rc == 0) rc = xhci_wait_command_completion(xhci, NULL);
  kfree_aligned(input_ctx);
  if (rc != 0) {
    /* Address Device failed. The slot is still Enabled on the
     * controller; emit Disable Slot and free per-slot allocations
     * via the centralised teardown. Disable Slot may itself fail
     * (controller could be in an unexpected state), but
     * xhci_release_slot tolerates that and always drops our
     * pointers, so the next Enable Slot can reuse this slot ID
     * without colliding. */
    (void)xhci_release_slot(xhci, slot_id);
    return rc;
  }
  return 0;
}

/* Etapa 3 — Slice 3D hardening (§14.3 follow-up).
 *
 * Releases all per-slot resources owned by the controller after a USB
 * device has been hot-unplugged or after a port reset replaced the
 * device on the same physical port. The xHCI 1.2 spec §4.6.4 mandates
 * a Disable Slot command to release the controller-side slot; we then
 * free the EP0 ring, device context, interrupt ring and report buffer
 * and zero the DCBAA entry plus any pending event latches so that a
 * future Enable Slot/Address Device cycle on the same slot starts from
 * a clean state.
 *
 * Frees happen regardless of the Disable Slot completion outcome: a
 * disconnected device often makes the command fail with CC != SUCCESS,
 * but the slot can still be reused once we drop our pointers. */
int xhci_release_slot(struct xhci_controller *xhci, uint8_t slot_id) {
  struct xhci_trb trb;
  int rc;
  if (!xhci || !xhci->initialized || slot_id == 0u ||
      slot_id > xhci->max_slots) {
    return -1;
  }
  trb.param = 0;
  trb.status = 0;
  trb.control = (TRB_TYPE_DISABLE_SLOT << 10) | ((uint32_t)slot_id << 24);
  rc = xhci_ring_command(xhci, &trb);
  if (rc == 0) rc = xhci_wait_command_completion(xhci, NULL);
  /* Free controller-owned allocations even on cmd failure: the slot is
   * being torn down regardless, and leaking the rings would defeat the
   * whole point of this routine. */
  if (xhci->ep0_rings[slot_id]) {
    kfree_aligned(xhci->ep0_rings[slot_id]);
    xhci->ep0_rings[slot_id] = NULL;
  }
  if (xhci->device_contexts[slot_id]) {
    kfree_aligned(xhci->device_contexts[slot_id]);
    xhci->device_contexts[slot_id] = NULL;
  }
  if (xhci->intr_rings[slot_id]) {
    kfree_aligned(xhci->intr_rings[slot_id]);
    xhci->intr_rings[slot_id] = NULL;
  }
  if (xhci->intr_buffers[slot_id]) {
    kfree_aligned(xhci->intr_buffers[slot_id]);
    xhci->intr_buffers[slot_id] = NULL;
  }
  xhci->ep0_ring_idx[slot_id] = 0u;
  xhci->ep0_ring_cycle[slot_id] = 0;
  xhci->intr_buffer_len[slot_id] = 0u;
  xhci->intr_ep_addr[slot_id] = 0u;
  xhci->intr_ep_dci[slot_id] = 0u;
  xhci->intr_ring_idx[slot_id] = 0u;
  xhci->intr_ring_cycle[slot_id] = 0;
  if (xhci->dcbaa) xhci->dcbaa[slot_id] = 0u;
  /* Drop any stale pending latches so a recycled slot does not
   * consume an event meant for the previous device. */
  xhci->ep0_pending[slot_id].valid = 0u;
  xhci->intr_pending[slot_id].valid = 0u;
  return rc;
}
