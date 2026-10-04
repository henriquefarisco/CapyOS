/* xhci_transfer.c — Control transfer + interrupt endpoint surface.
 *
 * Split from xhci.c (2026-05-21) to keep each TU ≤ 900 lines.
 * Owns:
 *   - xhci_control_transfer (EP0 SETUP/DATA/STATUS pump).
 *   - xhci_configure_interrupt_endpoint (Configure Endpoint command).
 *   - xhci_poll_interrupt (cooperative interrupt-IN drain).
 *   - xhci_find_keyboard / xhci_keyboard_poll legacy entry points.
 */
#include "drivers/usb/internal/xhci_internal.h"
#include "drivers/usb/usb_core.h"

#include <stddef.h>
#include <stdint.h>

extern void *kmalloc_aligned(uint64_t size, uint64_t alignment);
extern void kfree_aligned(void *ptr);

static int xhci_wait_transfer_completion(struct xhci_controller *xhci,
                                         uint8_t slot_id, uint8_t ep_id) {
  /* Only EP0 control completions are awaited synchronously. Interrupt
   * endpoints use xhci_poll_interrupt which pumps non-blockingly. */
  if (!xhci || !xhci->evt_ring || ep_id != 1u || slot_id == 0u ||
      slot_id > xhci->max_slots) {
    return -1;
  }
  for (int i = 0; i < 500000; i++) {
    if (__atomic_load_n(&xhci->ep0_pending[slot_id].valid, __ATOMIC_ACQUIRE)) {
      uint32_t cc = xhci->ep0_pending[slot_id].cc;
      __atomic_store_n(&xhci->ep0_pending[slot_id].valid, 0u, __ATOMIC_RELEASE);
      return (cc == XHCI_TRB_CC_SUCCESS) ? 0 : -2;
    }
    xhci_event_pump(xhci);
    if (__atomic_load_n(&xhci->ep0_pending[slot_id].valid, __ATOMIC_ACQUIRE)) {
      uint32_t cc = xhci->ep0_pending[slot_id].cc;
      __atomic_store_n(&xhci->ep0_pending[slot_id].valid, 0u, __ATOMIC_RELEASE);
      return (cc == XHCI_TRB_CC_SUCCESS) ? 0 : -2;
    }
    cpu_relax();
  }
  return -3;
}

static void xhci_queue_ep0_trb(struct xhci_controller *xhci, uint8_t slot_id,
                               const struct xhci_trb *trb) {
  uint32_t idx = xhci->ep0_ring_idx[slot_id];
  struct xhci_trb queued = *trb;
  if (idx >= XHCI_CMD_RING_TRBS - 1u) {
    idx = 0;
    xhci->ep0_ring_cycle[slot_id] ^= 1;
  }
  queued.control = (queued.control & ~1u) |
                   (uint32_t)(xhci->ep0_ring_cycle[slot_id] & 1);
  xhci->ep0_rings[slot_id][idx] = queued;
  idx++;
  if (idx >= XHCI_CMD_RING_TRBS - 1u) {
    xhci->ep0_rings[slot_id][XHCI_CMD_RING_TRBS - 1].control =
        (TRB_TYPE_LINK << 10) | (1 << 1) |
        (uint32_t)(xhci->ep0_ring_cycle[slot_id] & 1);
    idx = 0;
    xhci->ep0_ring_cycle[slot_id] ^= 1;
  }
  xhci->ep0_ring_idx[slot_id] = idx;
}

static uint64_t xhci_setup_packet_param(const struct usb_setup_packet *setup) {
  const uint8_t *p = (const uint8_t *)setup;
  uint64_t v = 0;
  for (uint32_t i = 0; i < 8u; i++) v |= (uint64_t)p[i] << (i * 8u);
  return v;
}

int xhci_control_transfer(struct xhci_controller *xhci, uint8_t slot_id,
                          const struct usb_setup_packet *setup, void *buf,
                          uint16_t len, int dir_in) {
  struct xhci_trb trb;
  volatile uint32_t *db;
  if (!xhci || !xhci->initialized || slot_id == 0 || slot_id > xhci->max_slots ||
      !setup || !xhci->ep0_rings[slot_id] || !xhci->db_base ||
      (!buf && len != 0) || len > 4096u || setup->wLength != len ||
      (len && ((setup->bmRequestType >> 7) != !!dir_in))) {
    return -1;
  }
  uint32_t expected = 0;
  if (!__atomic_compare_exchange_n(&xhci->ep0_busy[slot_id], &expected, 1u,
                                   0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
    return -4;
  if (xhci->ep0_failed[slot_id] || !xhci->ep0_rings[slot_id]) {
    __atomic_store_n(&xhci->ep0_busy[slot_id], 0u, __ATOMIC_RELEASE);
    return -4;
  }
  /* Never hand caller/stack memory to DMA. A timed-out endpoint retains this
   * page and cannot reuse it until successful slot disable ends ownership. */
  if (len && !xhci->ep0_buffers[slot_id]) {
    xhci->ep0_buffers[slot_id] = kmalloc_aligned(4096u, 4096u);
    if (!xhci->ep0_buffers[slot_id]) {
      __atomic_store_n(&xhci->ep0_busy[slot_id], 0u, __ATOMIC_RELEASE);
      return -5;
    }
  }
  if (len) {
    if (dir_in) xhci_memzero(xhci->ep0_buffers[slot_id], len);
    else xhci_memcpy(xhci->ep0_buffers[slot_id], buf, len);
  }
  trb.param = xhci_setup_packet_param(setup);
  trb.status = 8u;
  trb.control = (TRB_TYPE_SETUP << 10) | (1u << 6);
  if (len != 0) trb.control |= (uint32_t)((dir_in ? 3u : 2u) << 16);
  xhci_queue_ep0_trb(xhci, slot_id, &trb);
  if (len != 0) {
    trb.param = (uint64_t)(uintptr_t)xhci->ep0_buffers[slot_id];
    trb.status = len;
    trb.control = (TRB_TYPE_DATA << 10);
    if (dir_in) trb.control |= 1u << 16;
    xhci_queue_ep0_trb(xhci, slot_id, &trb);
  }
  trb.param = 0;
  trb.status = 0;
  trb.control = (TRB_TYPE_STATUS << 10) | (1u << 5);
  if (!dir_in) trb.control |= 1u << 16;
  xhci_queue_ep0_trb(xhci, slot_id, &trb);
  db = (volatile uint32_t *)(xhci->db_base + ((uint32_t)slot_id * 4u));
  __atomic_thread_fence(__ATOMIC_RELEASE);
  mmio_write32(db, 1u);
  int rc = xhci_wait_transfer_completion(xhci, slot_id, 1u);
  if (rc != 0) xhci->ep0_failed[slot_id] = 1u;
  else if (dir_in && len) {
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    xhci_memcpy(buf, xhci->ep0_buffers[slot_id], len);
  }
  __atomic_store_n(&xhci->ep0_busy[slot_id], 0u, __ATOMIC_RELEASE);
  return rc;
}

int xhci_update_ep0_packet_size(struct xhci_controller *xhci, uint8_t slot_id,
                                uint8_t descriptor_size) {
  if (!xhci || !xhci->initialized || !slot_id || slot_id > xhci->max_slots ||
      !xhci->device_contexts[slot_id] || !xhci->ep0_rings[slot_id] ||
      !xhci->cmd_ring || !xhci->db_base ||
      (xhci->context_size != 32u && xhci->context_size != 64u)) return -1;
  uint32_t expected = 0;
  if (!__atomic_compare_exchange_n(&xhci->ep0_busy[slot_id], &expected, 1u,
                                   0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
    return -4;
  int rc = -1;
  if (xhci->ep0_failed[slot_id] || !xhci->device_contexts[slot_id] ||
      !xhci->ep0_rings[slot_id]) { rc = -4; goto done; }
  __atomic_thread_fence(__ATOMIC_ACQUIRE);
  const uint32_t *device = xhci->device_contexts[slot_id];
  unsigned speed = (device[0] >> 20) & 15u;
  uint16_t packet = descriptor_size;
  if (speed == 1) {
    if (packet != 8 && packet != 16 && packet != 32 && packet != 64) goto done;
  } else if (speed == 2) {
    if (packet != 8) goto done;
  } else if (speed == 3) {
    if (packet != 64) goto done;
  } else if (speed == 4 || speed == 5) {
    if (packet != 9) goto done;
    packet = 512;
  } else goto done;
  if ((device[xhci->context_size / 4u + 1u] >> 16) == packet) {
    rc = 0;
    goto done;
  }
  if (!xhci->ep0_buffers[slot_id])
    xhci->ep0_buffers[slot_id] = kmalloc_aligned(4096u, 4096u);
  if (!xhci->ep0_buffers[slot_id]) { rc = -5; goto done; }
  /* Reuse the persistent page only while EP0 is idle. If the command times
   * out, quarantine also protects this Input Context from late DMA reads. */
  uint32_t *input = (uint32_t *)xhci->ep0_buffers[slot_id];
  xhci_memzero(input, 4096u);
  input[1] = 2u; /* Add EP0 only; Evaluate Context copies its MPS field. */
  input[xhci->context_size / 2u + 1u] = (uint32_t)packet << 16;
  struct xhci_trb command = {
    .param = (uint64_t)(uintptr_t)input,
    .control = (TRB_TYPE_EVALUATE_CONTEXT << 10) | (uint32_t)slot_id << 24
  };
  __atomic_thread_fence(__ATOMIC_RELEASE);
  rc = xhci_ring_command(xhci, &command);
  if (rc == 0) rc = xhci_wait_command_completion(xhci, NULL);
  if (rc != 0) xhci->ep0_failed[slot_id] = 1;
done:
  __atomic_store_n(&xhci->ep0_busy[slot_id], 0u, __ATOMIC_RELEASE);
  return rc;
}

static int xhci_queue_interrupt_trb(struct xhci_controller *xhci,
                                    uint8_t slot_id, void *buf,
                                    uint16_t len) {
  uint32_t idx;
  struct xhci_trb trb;
  if (!xhci || slot_id == 0 || !xhci->intr_rings[slot_id] || !buf || len == 0) {
    return -1;
  }
  idx = xhci->intr_ring_idx[slot_id];
  if (idx >= XHCI_CMD_RING_TRBS - 1u) {
    idx = 0;
    xhci->intr_ring_cycle[slot_id] ^= 1;
  }
  if (xhci_build_normal_trb(&trb, buf, len) != 0) return -1;
  trb.control = (trb.control & ~1u) |
                (uint32_t)(xhci->intr_ring_cycle[slot_id] & 1);
  xhci->intr_rings[slot_id][idx] = trb;
  idx++;
  if (idx >= XHCI_CMD_RING_TRBS - 1u) {
    xhci->intr_rings[slot_id][XHCI_CMD_RING_TRBS - 1].control =
        (TRB_TYPE_LINK << 10) | (1 << 1) |
        (uint32_t)(xhci->intr_ring_cycle[slot_id] & 1);
    idx = 0;
    xhci->intr_ring_cycle[slot_id] ^= 1;
  }
  xhci->intr_ring_idx[slot_id] = idx;
  return 0;
}

int xhci_configure_interrupt_endpoint(struct xhci_controller *xhci,
                                      uint8_t slot_id,
                                      const struct usb_endpoint_info *ep,
                                      uint16_t report_len) {
  uint32_t ctx_size;
  uint64_t input_size;
  void *input_ctx;
  struct xhci_trb *ring;
  uint8_t *buffer;
  struct xhci_trb trb;
  uint8_t dci;
  int rc;
  if (!xhci || !xhci->initialized || slot_id == 0 || slot_id > xhci->max_slots ||
      !ep || ep->type != 3u || !(ep->address & 0x80u) ||
      ep->max_packet_size == 0 || report_len == 0 || !xhci->cmd_ring ||
      !xhci->db_base || !xhci->device_contexts[slot_id] || xhci->ep0_failed[slot_id]) {
    return -1;
  }
  dci = xhci_endpoint_dci(ep->address);
  if (dci == 0) return -1;
  if (xhci->intr_rings[slot_id] || xhci->intr_buffers[slot_id]) return -1;
  ctx_size = xhci->context_size ? xhci->context_size : 32u;
  if (ctx_size != 32u && ctx_size != 64u) return -1;
  input_size = (uint64_t)ctx_size * 33u;
  if (!xhci->ep0_buffers[slot_id])
    xhci->ep0_buffers[slot_id] = kmalloc_aligned(4096u, 4096u);
  input_ctx = xhci->ep0_buffers[slot_id];
  ring = (struct xhci_trb *)kmalloc_aligned(
      XHCI_CMD_RING_TRBS * sizeof(struct xhci_trb), 64);
  buffer = (uint8_t *)kmalloc_aligned(report_len, 16);
  if (!input_ctx || !ring || !buffer) {
    if (ring) kfree_aligned(ring);
    if (buffer) kfree_aligned(buffer);
    return -2;
  }
  xhci_memzero(input_ctx, input_size);
  xhci_memzero(ring, XHCI_CMD_RING_TRBS * sizeof(struct xhci_trb));
  xhci_memzero(buffer, report_len);
  ring[XHCI_CMD_RING_TRBS - 1].param = (uint64_t)(uintptr_t)ring;
  ring[XHCI_CMD_RING_TRBS - 1].status = 0;
  ring[XHCI_CMD_RING_TRBS - 1].control = (TRB_TYPE_LINK << 10) | (1 << 1) | 1;
  if (xhci_build_configure_endpoint_input_context(
          input_ctx, ctx_size, ep->address, ep->max_packet_size, ep->interval,
          ring) != 0) {
    kfree_aligned(ring);
    kfree_aligned(buffer);
    return -3;
  }
  xhci->intr_rings[slot_id] = ring;
  xhci->intr_buffers[slot_id] = buffer;
  xhci->intr_buffer_len[slot_id] = report_len;
  xhci->intr_ep_addr[slot_id] = ep->address;
  xhci->intr_ep_dci[slot_id] = dci;
  xhci->intr_ring_idx[slot_id] = 0;
  xhci->intr_ring_cycle[slot_id] = 1;
  if (xhci_queue_interrupt_trb(xhci, slot_id, buffer, report_len) != 0) {
    xhci->intr_rings[slot_id] = NULL;
    xhci->intr_buffers[slot_id] = NULL;
    xhci->intr_buffer_len[slot_id] = 0;
    xhci->intr_ep_addr[slot_id] = 0;
    xhci->intr_ep_dci[slot_id] = 0;
    xhci->intr_ring_idx[slot_id] = 0;
    xhci->intr_ring_cycle[slot_id] = 0;
    kfree_aligned(ring);
    kfree_aligned(buffer);
    return -4;
  }
  trb.param = (uint64_t)(uintptr_t)input_ctx;
  trb.status = 0;
  trb.control = (TRB_TYPE_CONFIG_EP << 10) | ((uint32_t)slot_id << 24);
  rc = xhci_ring_command(xhci, &trb);
  if (rc == 0) rc = xhci_wait_command_completion(xhci, NULL);
  if (rc != 0) {
    /* A late Configure Endpoint can still read the Input Context and ring.
     * Quarantine the slot until a matching Disable Slot returns ownership. */
    xhci->ep0_failed[slot_id] = 1;
    return rc;
  }
  if (xhci->db_base) {
    volatile uint32_t *db =
        (volatile uint32_t *)(xhci->db_base + ((uint32_t)slot_id * 4u));
    mmio_write32(db, dci);
  }
  return 0;
}

int xhci_poll_interrupt(struct xhci_controller *xhci, uint8_t slot_id,
                        uint8_t ep_addr, void *out, uint16_t out_len) {
  uint8_t dci;
  uint32_t cc;
  uint32_t residual;
  uint16_t produced;
  if (!xhci || !xhci->initialized || slot_id == 0 || slot_id > xhci->max_slots ||
      !out || out_len == 0 || !xhci->evt_ring || !xhci->intr_rings[slot_id] ||
      !xhci->intr_buffers[slot_id]) {
    return -1;
  }
  dci = xhci_endpoint_dci(ep_addr);
  if (dci == 0 || dci != xhci->intr_ep_dci[slot_id]) return -1;
  /* Drain whatever is currently available; the dispatcher routes our
   * own event into intr_pending[slot_id] and other endpoints' events
   * into their own slots, so cooperative polling stays cooperative. */
  xhci_event_pump(xhci);
  if (!__atomic_load_n(&xhci->intr_pending[slot_id].valid, __ATOMIC_ACQUIRE)) return 0;
  cc = xhci->intr_pending[slot_id].cc;
  residual = xhci->intr_pending[slot_id].residual;
  __atomic_store_n(&xhci->intr_pending[slot_id].valid, 0u, __ATOMIC_RELEASE);
  if (cc != XHCI_TRB_CC_SUCCESS) return -2;
  produced = xhci->intr_buffer_len[slot_id];
  if (residual >= produced) {
    produced = 0;
  } else {
    produced = (uint16_t)(produced - residual);
  }
  if (out_len > produced) {
    out_len = produced;
  }
  xhci_memcpy(out, xhci->intr_buffers[slot_id], out_len);
  if (xhci_queue_interrupt_trb(xhci, slot_id, xhci->intr_buffers[slot_id],
                               xhci->intr_buffer_len[slot_id]) != 0) {
    return -3;
  }
  if (xhci->db_base) {
    volatile uint32_t *db =
        (volatile uint32_t *)(xhci->db_base + ((uint32_t)slot_id * 4u));
    mmio_write32(db, dci);
  }
  return out_len;
}

int xhci_find_keyboard(struct xhci_controller *xhci, struct usb_device *kbd) {
  if (!xhci || !xhci->initialized || !kbd) return -1;
  /* Legacy compatibility entry point. The active USB core/HID path owns
   * descriptor parsing and boot-protocol device discovery. */
  return -1;
}

int xhci_keyboard_poll(struct xhci_controller *xhci, struct usb_device *kbd,
                       uint8_t *key) {
  if (!xhci || !key) return -1;
  (void)kbd;
  /* Legacy compatibility entry point. Interrupt report polling is owned by
   * usb_poll_all + usb_hid_handle_keyboard_report. */
  *key = 0;
  return -1;
}
