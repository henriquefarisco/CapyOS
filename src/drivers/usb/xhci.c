/* CAPYOS USB XHCI Controller Driver Implementation
 * Phase 1: Controller detection and initialization
 *
 * Split (2026-05-21) across 5 TUs to keep each ≤ 900 lines:
 *   - xhci.c             (controller find/reset/init/start/stop + shared helpers)
 *   - xhci_context.c     (TRB/input-context builders + endpoint/port helpers)
 *   - xhci_event.c       (unified event-ring pump + dispatcher)
 *   - xhci_port_slot.c   (port management + slot enable/address/release)
 *   - xhci_transfer.c    (control transfer + interrupt endpoint + poll)
 *
 * Shared accessors and the command-ring helpers live in
 * include/drivers/usb/internal/xhci_internal.h so each TU is self-
 * contained.
 */
#include "drivers/usb/internal/xhci_internal.h"
#include "drivers/pcie.h"
#include "drivers/usb/usb_core.h"
#include "kernel/log/klog.h"

#include <stddef.h>
#include <stdint.h>


/* Forward declarations for memory functions */
extern void *kmalloc_aligned(uint64_t size, uint64_t alignment);
extern void kfree_aligned(void *ptr);

/* Freestanding mem helpers — declared in xhci_internal.h so every
 * split TU can call them. Defined here to avoid duplicating object
 * code across the 5 TUs. */
void xhci_memzero(void *ptr, uint64_t size) {
  uint8_t *p = (uint8_t *)ptr;
  if (!p) return;
  for (uint64_t i = 0; i < size; i++) p[i] = 0;
}

void xhci_memcpy(void *dst, const void *src, uint64_t size) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  if (!d || !s) return;
  for (uint64_t i = 0; i < size; i++) d[i] = s[i];
}

int xhci_ring_command(struct xhci_controller *xhci, struct xhci_trb *trb) {
  uint32_t idx;
  volatile uint32_t *db;
  if (!xhci || !trb || !xhci->cmd_ring || !xhci->db_base) return -1;
  idx = xhci->cmd_ring_idx;
  if (idx >= XHCI_CMD_RING_TRBS - 1u) {
    idx = 0;
    xhci->cmd_ring_idx = 0;
    xhci->cmd_ring_cycle ^= 1;
  }
  trb->control = (trb->control & ~1u) | (uint32_t)(xhci->cmd_ring_cycle & 1);
  xhci->cmd_ring[idx] = *trb;
  idx++;
  if (idx >= XHCI_CMD_RING_TRBS - 1u) {
    xhci->cmd_ring[XHCI_CMD_RING_TRBS - 1].control =
        (TRB_TYPE_LINK << 10) | (1 << 1) | (uint32_t)(xhci->cmd_ring_cycle & 1);
    idx = 0;
    xhci->cmd_ring_cycle ^= 1;
  }
  xhci->cmd_ring_idx = idx;
  db = (volatile uint32_t *)(xhci->db_base);
  mmio_write32(db, 0);
  return 0;
}

int xhci_wait_command_completion(struct xhci_controller *xhci,
                                 uint8_t *slot_id) {
  if (!xhci || !xhci->evt_ring) return -1;
  for (int i = 0; i < 500000; i++) {
    if (xhci->cmd_pending.valid) {
      uint8_t completed_slot = xhci->cmd_pending.slot;
      uint32_t cc = xhci->cmd_pending.cc;
      xhci->cmd_pending.valid = 0u;
      if (slot_id) *slot_id = completed_slot;
      return (cc == XHCI_TRB_CC_SUCCESS) ? 0 : -2;
    }
    xhci_event_pump(xhci);
    if (xhci->cmd_pending.valid) {
      uint8_t completed_slot = xhci->cmd_pending.slot;
      uint32_t cc = xhci->cmd_pending.cc;
      xhci->cmd_pending.valid = 0u;
      if (slot_id) *slot_id = completed_slot;
      return (cc == XHCI_TRB_CC_SUCCESS) ? 0 : -2;
    }
    cpu_relax();
  }
  return -3;
}

/* Global XHCI controller for easy access */
static struct xhci_controller g_xhci = {0};
static int g_xhci_found = 0;

/* Find XHCI controller on PCI bus */
int xhci_find(struct xhci_controller *xhci) {
  struct pci_device pci_dev;

  /* XHCI is class 0x0C (Serial Bus), subclass 0x03 (USB), prog IF 0x30 */
  if (pci_find_device(PCI_CLASS_SERIAL_BUS, PCI_SUBCLASS_USB, &pci_dev) != 0) {
    return -1; /* No USB controller found */
  }

  /* Check prog_if for XHCI (0x30) */
  if (pci_dev.prog_if != PCI_PROGIF_XHCI) {
    return -2; /* Not XHCI (might be EHCI/OHCI/UHCI) */
  }

  xhci->bus = pci_dev.bus;
  xhci->dev = pci_dev.device;
  xhci->func = pci_dev.function;

  /* Get MMIO base from BAR0 (always memory-mapped for XHCI) */
  uint64_t bar0 = pci_dev.bar[0];
  if (bar0 == 0 || (bar0 & 0x1)) {
    return -3; /* Invalid BAR or I/O space (unexpected) */
  }

  xhci->mmio_base = (volatile uint8_t *)(uintptr_t)(bar0 & ~0xFULL);

  /* Enable bus mastering and memory space access */
  uint16_t cmd = pci_config_read16(xhci->bus, xhci->dev, xhci->func, 0x04);
  cmd |= (1 << 1) | (1 << 2); /* Memory Space Enable + Bus Master Enable */
  pci_config_write16(xhci->bus, xhci->dev, xhci->func, 0x04, cmd);

  g_xhci_found = 1;
  return 0;
}

/* Reset XHCI controller */
int xhci_reset(struct xhci_controller *xhci) {
  if (!xhci->mmio_base)
    return -1;

  /* Read CAPLENGTH to find operational registers */
  uint8_t caplength = *(volatile uint8_t *)(xhci->mmio_base + XHCI_CAPLENGTH);
  xhci->op_base = xhci->mmio_base + caplength;

  /* Stop controller first */
  uint32_t cmd = mmio_read32(xhci->op_base + XHCI_USBCMD);
  cmd &= ~XHCI_CMD_RS;
  mmio_write32(xhci->op_base + XHCI_USBCMD, cmd);

  /* Wait for HCHalted. HCH-first precedence (code review fix
   * 2026-05-25): if both HCH and HSE are set, the controller is
   * already halted and the subsequent HCRST will clear HSE per
   * xHCI 1.2 §5.4.1. Bailing on HSE here would skip that recovery
   * path. Only return HSE error if the controller failed to halt
   * (HCH clear) while HSE is set, which is anomalous per
   * §5.4.2 (HSE is supposed to force termination of TRBs/DMA). */
  for (int i = 0; i < 100000; i++) {
    uint32_t sts = mmio_read32(xhci->op_base + XHCI_USBSTS);
    if (sts & XHCI_STS_HCH)
      break;
    if (sts & XHCI_STS_HSE) {
      klog_hex(KLOG_ERROR,
               "[xhci] USBSTS.HSE without HCH during stop, sts=",
               (uint64_t)sts);
      return -3;
    }
    cpu_relax();
  }

  /* Issue reset */
  cmd = mmio_read32(xhci->op_base + XHCI_USBCMD);
  cmd |= XHCI_CMD_HCRST;
  mmio_write32(xhci->op_base + XHCI_USBCMD, cmd);

  /* Wait for reset to complete (HCRST clears and CNR clears).
   * HSE early-exit applies here too; a reset that pushes the
   * controller into HSE is unrecoverable from software. */
  for (int i = 0; i < 1000000; i++) {
    uint32_t c = mmio_read32(xhci->op_base + XHCI_USBCMD);
    uint32_t s = mmio_read32(xhci->op_base + XHCI_USBSTS);
    if (s & XHCI_STS_HSE) {
      klog_hex(KLOG_ERROR, "[xhci] USBSTS.HSE during reset, sts=",
               (uint64_t)s);
      return -4;
    }
    if (!(c & XHCI_CMD_HCRST) && !(s & XHCI_STS_CNR))
      return 0;
    cpu_relax();
  }

  return -2; /* Reset timeout */
}

/* Initialize XHCI controller */
int xhci_init(struct xhci_controller *xhci) {
  if (!xhci->mmio_base)
    return -1;

  /* Read capability registers */
  uint8_t caplength = *(volatile uint8_t *)(xhci->mmio_base + XHCI_CAPLENGTH);
  uint16_t version = *(volatile uint16_t *)(xhci->mmio_base + XHCI_HCIVERSION);
  uint32_t hcsparams1 = mmio_read32(xhci->mmio_base + XHCI_HCSPARAMS1);
  uint32_t hcsparams2 = mmio_read32(xhci->mmio_base + XHCI_HCSPARAMS2);
  uint32_t hccparams1 = mmio_read32(xhci->mmio_base + XHCI_HCCPARAMS1);
  uint32_t dboff = mmio_read32(xhci->mmio_base + XHCI_DBOFF);
  uint32_t rtsoff = mmio_read32(xhci->mmio_base + XHCI_RTSOFF);

  (void)version;
  (void)hcsparams2;

  /* Set up base pointers */
  xhci->op_base = xhci->mmio_base + caplength;
  xhci->db_base = xhci->mmio_base + (dboff & ~0x3);
  xhci->rt_base = xhci->mmio_base + (rtsoff & ~0x1F);

  /* Extract parameters */
  xhci->max_slots = (uint8_t)(hcsparams1 & 0xFF);
  xhci->max_intrs = (uint16_t)((hcsparams1 >> 8) & 0x7FF);
  xhci->max_ports = (uint8_t)((hcsparams1 >> 24) & 0xFF);
  xhci->context_size = (hccparams1 & (1u << 2)) ? 64u : 32u;

  /* Reset controller */
  if (xhci_reset(xhci) != 0)
    return -2;

  /* Get page size */
  xhci->page_size = mmio_read32(xhci->op_base + XHCI_PAGESIZE);
  xhci->page_size = (xhci->page_size & 0xFFFF) << 12; /* In bytes */
  if (xhci->page_size == 0)
    xhci->page_size = 4096;

  /* Allocate Device Context Base Address Array (DCBAA)
   * Size: (max_slots + 1) * 8 bytes, 64-byte aligned */
  uint64_t dcbaa_size = ((uint64_t)xhci->max_slots + 1) * 8;
  xhci->dcbaa = (uint64_t *)kmalloc_aligned(dcbaa_size, 64);
  if (!xhci->dcbaa)
    return -3;
  for (uint32_t i = 0; i <= xhci->max_slots; i++)
    xhci->dcbaa[i] = 0;

  /* Allocate Command Ring (XHCI_CMD_RING_TRBS TRBs, 64-byte aligned).
   * The last TRB is reserved for a Link TRB that wraps to index 0 with
   * cycle toggled. Usable command slots: indices 0..(XHCI_CMD_RING_TRBS-2). */
  xhci->cmd_ring =
      (struct xhci_trb *)kmalloc_aligned(XHCI_CMD_RING_TRBS * sizeof(struct xhci_trb), 64);
  if (!xhci->cmd_ring) {
    kfree_aligned(xhci->dcbaa);
    return -4;
  }
  for (int i = 0; i < XHCI_CMD_RING_TRBS; i++) {
    xhci->cmd_ring[i].param = 0;
    xhci->cmd_ring[i].status = 0;
    xhci->cmd_ring[i].control = 0;
  }
  xhci->cmd_ring_idx = 0;
  xhci->cmd_ring_cycle = 1;

  /* Set up link TRB at the last entry of the command ring */
  xhci->cmd_ring[XHCI_CMD_RING_TRBS - 1].param = (uint64_t)(uintptr_t)xhci->cmd_ring;
  xhci->cmd_ring[XHCI_CMD_RING_TRBS - 1].status = 0;
  xhci->cmd_ring[XHCI_CMD_RING_TRBS - 1].control =
      (TRB_TYPE_LINK << 10) | (1 << 1) | 1; /* Toggle cycle */

  /* Allocate Event Ring (single segment, XHCI_EVT_RING_TRBS entries). */
  xhci->evt_ring =
      (struct xhci_trb *)kmalloc_aligned(XHCI_EVT_RING_TRBS * sizeof(struct xhci_trb), 64);
  if (!xhci->evt_ring) {
    kfree_aligned(xhci->cmd_ring);
    kfree_aligned(xhci->dcbaa);
    return -5;
  }
  for (int i = 0; i < XHCI_EVT_RING_TRBS; i++) {
    xhci->evt_ring[i].param = 0;
    xhci->evt_ring[i].status = 0;
    xhci->evt_ring[i].control = 0;
  }
  xhci->evt_ring_idx = 0;
  xhci->evt_ring_cycle = 1;
  xhci_memzero(xhci->device_contexts, sizeof(xhci->device_contexts));
  xhci_memzero(xhci->ep0_rings, sizeof(xhci->ep0_rings));
  xhci_memzero(xhci->ep0_ring_idx, sizeof(xhci->ep0_ring_idx));
  xhci_memzero(xhci->ep0_ring_cycle, sizeof(xhci->ep0_ring_cycle));
  xhci_memzero(xhci->intr_rings, sizeof(xhci->intr_rings));
  xhci_memzero(xhci->intr_buffers, sizeof(xhci->intr_buffers));
  xhci_memzero(xhci->intr_buffer_len, sizeof(xhci->intr_buffer_len));
  xhci_memzero(xhci->intr_ep_addr, sizeof(xhci->intr_ep_addr));
  xhci_memzero(xhci->intr_ep_dci, sizeof(xhci->intr_ep_dci));
  xhci_memzero(xhci->intr_ring_idx, sizeof(xhci->intr_ring_idx));
  xhci_memzero(xhci->intr_ring_cycle, sizeof(xhci->intr_ring_cycle));
  xhci_memzero(&xhci->cmd_pending, sizeof(xhci->cmd_pending));
  xhci_memzero(xhci->ep0_pending, sizeof(xhci->ep0_pending));
  xhci_memzero(xhci->intr_pending, sizeof(xhci->intr_pending));
  xhci->event_stray_count = 0u;

  xhci->erst =
      (struct xhci_erst_entry *)kmalloc_aligned(sizeof(struct xhci_erst_entry), 64);
  if (!xhci->erst) {
    kfree_aligned(xhci->evt_ring);
    kfree_aligned(xhci->cmd_ring);
    kfree_aligned(xhci->dcbaa);
    return -6;
  }
  xhci->erst->ring_segment_base = (uint64_t)(uintptr_t)xhci->evt_ring;
  xhci->erst->ring_segment_size = XHCI_EVT_RING_TRBS;
  xhci->erst->reserved = 0;

  /* Program DCBAAP */
  mmio_write64(xhci->op_base + XHCI_DCBAAP, (uint64_t)(uintptr_t)xhci->dcbaa);

  /* Program Command Ring Control Register */
  uint64_t crcr = (uint64_t)(uintptr_t)xhci->cmd_ring;
  crcr |= 1; /* Ring Cycle State = 1 */
  mmio_write64(xhci->op_base + XHCI_CRCR, crcr);

  /* Set max device slots enabled */
  uint32_t config = xhci->max_slots;
  mmio_write32(xhci->op_base + XHCI_CONFIG, config);

  mmio_write32(xhci->rt_base + XHCI_IR0_ERSTSZ, 1);
  mmio_write64(xhci->rt_base + XHCI_IR0_ERSTBA, (uint64_t)(uintptr_t)xhci->erst);
  mmio_write64(xhci->rt_base + XHCI_IR0_ERDP, (uint64_t)(uintptr_t)xhci->evt_ring);

  xhci->initialized = 1;
  return 0;
}

/* Start XHCI controller */
int xhci_start(struct xhci_controller *xhci) {
  if (!xhci->initialized)
    return -1;

  uint32_t cmd = mmio_read32(xhci->op_base + XHCI_USBCMD);
  cmd |= XHCI_CMD_RS; /* Run */
  mmio_write32(xhci->op_base + XHCI_USBCMD, cmd);

  /* Wait for running. HSE early-exit: if the controller enters
   * Host System Error before HCH clears, the start has failed
   * and spinning longer cannot recover. */
  for (int i = 0; i < 100000; i++) {
    uint32_t sts = mmio_read32(xhci->op_base + XHCI_USBSTS);
    if (sts & XHCI_STS_HSE) {
      klog_hex(KLOG_ERROR, "[xhci] USBSTS.HSE during start, sts=",
               (uint64_t)sts);
      return -3;
    }
    if (!(sts & XHCI_STS_HCH)) {
      xhci->running = 1;
      return 0;
    }
    cpu_relax();
  }

  return -2; /* Start timeout */
}

/* Stop XHCI controller */
int xhci_stop(struct xhci_controller *xhci) {
  if (!xhci->initialized)
    return -1;

  uint32_t cmd = mmio_read32(xhci->op_base + XHCI_USBCMD);
  cmd &= ~XHCI_CMD_RS;
  mmio_write32(xhci->op_base + XHCI_USBCMD, cmd);

  /* Wait for halted. HCH-first precedence (code review fix
   * 2026-05-25): the caller's intent is "stop the controller"; if
   * HCH is set, the intent is satisfied regardless of whether HSE
   * is also set (HSE just means the previous halt was caused by an
   * internal error rather than the user's RS=0 write). Only treat
   * HSE as a failure if HCH is clear, which is anomalous (HSE is
   * supposed to force termination per xHCI 1.2 §5.4.2). */
  for (int i = 0; i < 100000; i++) {
    uint32_t sts = mmio_read32(xhci->op_base + XHCI_USBSTS);
    if (sts & XHCI_STS_HCH) {
      xhci->running = 0;
      return 0;
    }
    if (sts & XHCI_STS_HSE) {
      klog_hex(KLOG_ERROR,
               "[xhci] USBSTS.HSE without HCH during stop, sts=",
               (uint64_t)sts);
      return -3;
    }
    cpu_relax();
  }

  return -2;
}

/* Global accessor */
struct xhci_controller *xhci_get_controller(void) {
  return g_xhci_found ? &g_xhci : NULL;
}
