/* CAPYOS USB XHCI Controller Driver Implementation
 * Phase 1: Controller detection and initialization
 */
#include "drivers/usb/xhci.h"
#include "drivers/pcie.h"
#include "drivers/usb/usb_core.h"
#include <stddef.h>
#include <stdint.h>


/* Forward declarations for memory functions */
extern void *kmalloc_aligned(uint64_t size, uint64_t alignment);
extern void kfree_aligned(void *ptr);

/* MMIO read/write helpers */
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

static void xhci_memzero(void *ptr, uint64_t size) {
  uint8_t *p = (uint8_t *)ptr;
  if (!p) return;
  for (uint64_t i = 0; i < size; i++) p[i] = 0;
}

static void xhci_memcpy(void *dst, const void *src, uint64_t size) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  if (!d || !s) return;
  for (uint64_t i = 0; i < size; i++) d[i] = s[i];
}

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

static int xhci_ring_command(struct xhci_controller *xhci,
                             struct xhci_trb *trb) {
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

static void xhci_advance_event_ring(struct xhci_controller *xhci) {
  xhci->evt_ring_idx = (xhci->evt_ring_idx + 1) % XHCI_EVT_RING_TRBS;
  if (xhci->evt_ring_idx == 0) xhci->evt_ring_cycle ^= 1;
  if (xhci->rt_base) {
    mmio_write64(xhci->rt_base + XHCI_IR0_ERDP,
                 (uint64_t)(uintptr_t)&xhci->evt_ring[xhci->evt_ring_idx] |
                     XHCI_ERDP_EHB);
  }
}

static int xhci_wait_transfer_completion(struct xhci_controller *xhci,
                                         uint8_t slot_id, uint8_t ep_id) {
  if (!xhci || !xhci->evt_ring) return -1;
  for (int i = 0; i < 500000; i++) {
    struct xhci_trb *evt = &xhci->evt_ring[xhci->evt_ring_idx];
    uint32_t ctrl = mmio_read32((volatile uint32_t *)&evt->control);
    if ((ctrl & 1u) != (uint32_t)(xhci->evt_ring_cycle & 1)) {
      cpu_relax();
      continue;
    }
    if (((ctrl >> 10) & 0x3Fu) == TRB_TYPE_TRANSFER) {
      uint8_t event_ep = (uint8_t)((ctrl >> 16) & 0x1Fu);
      uint8_t event_slot = (uint8_t)((ctrl >> 24) & 0xFFu);
      uint32_t cc = (mmio_read32((volatile uint32_t *)&evt->status) >> 24) & 0xFFu;
      if (event_slot == slot_id && event_ep == ep_id) {
        xhci_advance_event_ring(xhci);
        return (cc == XHCI_TRB_CC_SUCCESS) ? 0 : -2;
      }
      return -4;
    }
    xhci_advance_event_ring(xhci);
  }
  return -3;
}

static int xhci_wait_command_completion(struct xhci_controller *xhci,
                                        uint8_t *slot_id) {
  if (!xhci || !xhci->evt_ring) return -1;
  for (int i = 0; i < 500000; i++) {
    struct xhci_trb *evt = &xhci->evt_ring[xhci->evt_ring_idx];
    uint32_t ctrl = mmio_read32((volatile uint32_t *)&evt->control);
    if ((ctrl & 1u) != (uint32_t)(xhci->evt_ring_cycle & 1)) {
      cpu_relax();
      continue;
    }
    if (((ctrl >> 10) & 0x3F) == TRB_TYPE_CMD_COMPLETE) {
      uint32_t cc = (mmio_read32((volatile uint32_t *)&evt->status) >> 24) & 0xFF;
      if (slot_id) *slot_id = (uint8_t)((ctrl >> 24) & 0xFF);
      xhci_advance_event_ring(xhci);
      return (cc == XHCI_TRB_CC_SUCCESS) ? 0 : -2;
    }
    xhci_advance_event_ring(xhci);
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

  /* Wait for HCHalted */
  for (int i = 0; i < 100000; i++) {
    uint32_t sts = mmio_read32(xhci->op_base + XHCI_USBSTS);
    if (sts & XHCI_STS_HCH)
      break;
    cpu_relax();
  }

  /* Issue reset */
  cmd = mmio_read32(xhci->op_base + XHCI_USBCMD);
  cmd |= XHCI_CMD_HCRST;
  mmio_write32(xhci->op_base + XHCI_USBCMD, cmd);

  /* Wait for reset to complete (HCRST clears and CNR clears) */
  for (int i = 0; i < 1000000; i++) {
    uint32_t c = mmio_read32(xhci->op_base + XHCI_USBCMD);
    uint32_t s = mmio_read32(xhci->op_base + XHCI_USBSTS);
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

  /* Wait for running */
  for (int i = 0; i < 100000; i++) {
    uint32_t sts = mmio_read32(xhci->op_base + XHCI_USBSTS);
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

  /* Wait for halted */
  for (int i = 0; i < 100000; i++) {
    uint32_t sts = mmio_read32(xhci->op_base + XHCI_USBSTS);
    if (sts & XHCI_STS_HCH) {
      xhci->running = 0;
      return 0;
    }
    cpu_relax();
  }

  return -2;
}

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
      /* Clear PRC by writing 1 */
      mmio_write32(portsc, val | XHCI_PORTSC_PRC);
      return 0;
    }
    cpu_relax();
  }

  return -2; /* Reset timeout */
}

/* Global accessor */
struct xhci_controller *xhci_get_controller(void) {
  return g_xhci_found ? &g_xhci : NULL;
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
  xhci->dcbaa[slot_id] = (uint64_t)(uintptr_t)device_ctx;
  trb.param = (uint64_t)(uintptr_t)input_ctx;
  trb.status = 0;
  trb.control = (TRB_TYPE_ADDRESS_DEV << 10) | ((uint32_t)slot_id << 24);
  rc = xhci_ring_command(xhci, &trb);
  if (rc == 0) rc = xhci_wait_command_completion(xhci, NULL);
  kfree_aligned(input_ctx);
  if (rc != 0) {
    xhci->dcbaa[slot_id] = 0;
    xhci->device_contexts[slot_id] = NULL;
    xhci->ep0_rings[slot_id] = NULL;
    xhci->ep0_ring_idx[slot_id] = 0;
    xhci->ep0_ring_cycle[slot_id] = 0;
    kfree_aligned(device_ctx);
    kfree_aligned(ep0_ring);
    return rc;
  }
  xhci->device_contexts[slot_id] = device_ctx;
  xhci->ep0_rings[slot_id] = ep0_ring;
  xhci->ep0_ring_idx[slot_id] = 0;
  xhci->ep0_ring_cycle[slot_id] = 1;
  return 0;
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
      (!buf && len != 0)) {
    return -1;
  }
  trb.param = xhci_setup_packet_param(setup);
  trb.status = 8u;
  trb.control = (TRB_TYPE_SETUP << 10) | (1u << 6);
  if (len != 0) trb.control |= (uint32_t)((dir_in ? 3u : 2u) << 16);
  xhci_queue_ep0_trb(xhci, slot_id, &trb);
  if (len != 0) {
    trb.param = (uint64_t)(uintptr_t)buf;
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
  mmio_write32(db, 1u);
  return xhci_wait_transfer_completion(xhci, slot_id, 1u);
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
      !xhci->db_base || !xhci->device_contexts[slot_id]) {
    return -1;
  }
  dci = xhci_endpoint_dci(ep->address);
  if (dci == 0) return -1;
  if (xhci->intr_rings[slot_id] || xhci->intr_buffers[slot_id]) return -1;
  ctx_size = xhci->context_size ? xhci->context_size : 32u;
  input_size = (uint64_t)ctx_size * 33u;
  input_ctx = kmalloc_aligned(input_size, 64);
  ring = (struct xhci_trb *)kmalloc_aligned(
      XHCI_CMD_RING_TRBS * sizeof(struct xhci_trb), 64);
  buffer = (uint8_t *)kmalloc_aligned(report_len, 16);
  if (!input_ctx || !ring || !buffer) {
    if (input_ctx) kfree_aligned(input_ctx);
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
    kfree_aligned(input_ctx);
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
    kfree_aligned(input_ctx);
    kfree_aligned(ring);
    kfree_aligned(buffer);
    return -4;
  }
  trb.param = (uint64_t)(uintptr_t)input_ctx;
  trb.status = 0;
  trb.control = (TRB_TYPE_CONFIG_EP << 10) | ((uint32_t)slot_id << 24);
  rc = xhci_ring_command(xhci, &trb);
  if (rc == 0) rc = xhci_wait_command_completion(xhci, NULL);
  kfree_aligned(input_ctx);
  if (rc != 0) {
    xhci->intr_rings[slot_id] = NULL;
    xhci->intr_buffers[slot_id] = NULL;
    xhci->intr_buffer_len[slot_id] = 0;
    xhci->intr_ep_addr[slot_id] = 0;
    xhci->intr_ep_dci[slot_id] = 0;
    xhci->intr_ring_idx[slot_id] = 0;
    xhci->intr_ring_cycle[slot_id] = 0;
    kfree_aligned(ring);
    kfree_aligned(buffer);
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
  if (!xhci || !xhci->initialized || slot_id == 0 || slot_id > xhci->max_slots ||
      !out || out_len == 0 || !xhci->evt_ring || !xhci->intr_rings[slot_id] ||
      !xhci->intr_buffers[slot_id]) {
    return -1;
  }
  dci = xhci_endpoint_dci(ep_addr);
  if (dci == 0 || dci != xhci->intr_ep_dci[slot_id]) return -1;
  for (int i = 0; i < XHCI_EVT_RING_TRBS; i++) {
    struct xhci_trb *evt = &xhci->evt_ring[xhci->evt_ring_idx];
    uint32_t ctrl = mmio_read32((volatile uint32_t *)&evt->control);
    if ((ctrl & 1u) != (uint32_t)(xhci->evt_ring_cycle & 1)) return 0;
    if (((ctrl >> 10) & 0x3Fu) == TRB_TYPE_TRANSFER) {
      uint8_t event_ep = (uint8_t)((ctrl >> 16) & 0x1Fu);
      uint8_t event_slot = (uint8_t)((ctrl >> 24) & 0xFFu);
      uint32_t st = mmio_read32((volatile uint32_t *)&evt->status);
      uint32_t cc = (st >> 24) & 0xFFu;
      uint32_t residual = st & 0x00FFFFFFu;
      if (event_slot == slot_id && event_ep == dci) {
        xhci_advance_event_ring(xhci);
        if (cc != XHCI_TRB_CC_SUCCESS) return -2;
        uint16_t produced = xhci->intr_buffer_len[slot_id];
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
      return 0;
    }
    xhci_advance_event_ring(xhci);
  }
  return 0;
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

