/* CAPYOS USB XHCI Controller Driver Implementation
 * Phase 1: Controller detection and initialization
 */
#include "drivers/usb/xhci.h"
#include "drivers/pcie.h"
#include <stddef.h>
#include <stdint.h>


/* Forward declarations for memory functions */
extern void *kmalloc_aligned(uint64_t size, uint64_t alignment);
extern void kfree(void *ptr);

/* MMIO read/write helpers */
static inline uint32_t mmio_read32(volatile void *addr) {
  return *(volatile uint32_t *)addr;
}

static inline void mmio_write32(volatile void *addr, uint32_t val) {
  *(volatile uint32_t *)addr = val;
}

static inline uint64_t mmio_read64(volatile void *addr) {
  return *(volatile uint64_t *)addr;
}

static inline void mmio_write64(volatile void *addr, uint64_t val) {
  *(volatile uint64_t *)addr = val;
}

static inline void cpu_relax(void) { __asm__ volatile("pause" ::: "memory"); }

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
  (void)hccparams1;

  /* Set up base pointers */
  xhci->op_base = xhci->mmio_base + caplength;
  xhci->db_base = xhci->mmio_base + (dboff & ~0x3);
  xhci->rt_base = xhci->mmio_base + (rtsoff & ~0x1F);

  /* Extract parameters */
  xhci->max_slots = (uint8_t)(hcsparams1 & 0xFF);
  xhci->max_intrs = (uint16_t)((hcsparams1 >> 8) & 0x7FF);
  xhci->max_ports = (uint8_t)((hcsparams1 >> 24) & 0xFF);

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

  /* Allocate Command Ring (256 TRBs, 64-byte aligned) */
  xhci->cmd_ring =
      (struct xhci_trb *)kmalloc_aligned(256 * sizeof(struct xhci_trb), 64);
  if (!xhci->cmd_ring) {
    kfree(xhci->dcbaa);
    return -4;
  }
  for (int i = 0; i < 256; i++) {
    xhci->cmd_ring[i].param = 0;
    xhci->cmd_ring[i].status = 0;
    xhci->cmd_ring[i].control = 0;
  }
  xhci->cmd_ring_idx = 0;
  xhci->cmd_ring_cycle = 1;

  /* Set up link TRB at end of command ring */
  xhci->cmd_ring[255].param = (uint64_t)(uintptr_t)xhci->cmd_ring;
  xhci->cmd_ring[255].status = 0;
  xhci->cmd_ring[255].control =
      (TRB_TYPE_LINK << 10) | (1 << 1) | 1; /* Toggle cycle */

  /* Allocate Event Ring Segment Table and Event Ring */
  /* For simplicity, single segment with 256 TRBs */
  xhci->evt_ring =
      (struct xhci_trb *)kmalloc_aligned(256 * sizeof(struct xhci_trb), 64);
  if (!xhci->evt_ring) {
    kfree(xhci->cmd_ring);
    kfree(xhci->dcbaa);
    return -5;
  }
  for (int i = 0; i < 256; i++) {
    xhci->evt_ring[i].param = 0;
    xhci->evt_ring[i].status = 0;
    xhci->evt_ring[i].control = 0;
  }
  xhci->evt_ring_idx = 0;

  /* Program DCBAAP */
  mmio_write64(xhci->op_base + XHCI_DCBAAP, (uint64_t)(uintptr_t)xhci->dcbaa);

  /* Program Command Ring Control Register */
  uint64_t crcr = (uint64_t)(uintptr_t)xhci->cmd_ring;
  crcr |= 1; /* Ring Cycle State = 1 */
  mmio_write64(xhci->op_base + XHCI_CRCR, crcr);

  /* Set max device slots enabled */
  uint32_t config = xhci->max_slots;
  mmio_write32(xhci->op_base + XHCI_CONFIG, config);

  /* TODO: Set up Event Ring Segment Table and interrupter */
  /* For polling mode, we'll check event ring directly */

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
  if (!xhci || !xhci->initialized || !xhci->cmd_ring || !slot_id) return -1;
  /* Build Enable Slot TRB */
  struct xhci_trb trb;
  trb.param = 0;
  trb.status = 0;
  trb.control = (TRB_TYPE_ENABLE_SLOT << 10) | (xhci->cmd_ring_cycle & 1);
  uint32_t idx = xhci->cmd_ring_idx;
  xhci->cmd_ring[idx] = trb;
  xhci->cmd_ring_idx = (idx + 1) % 64;
  /* Ring doorbell 0 (command) */
  volatile uint32_t *db = (volatile uint32_t *)(xhci->db_base);
  mmio_write32(db, 0);
  /* Poll event ring for completion */
  for (int i = 0; i < 500000; i++) {
    struct xhci_trb *evt = &xhci->evt_ring[xhci->evt_ring_idx];
    uint32_t ctrl = mmio_read32((volatile uint32_t *)&evt->control);
    if ((ctrl >> 10 & 0x3F) == TRB_TYPE_CMD_COMPLETE) {
      uint32_t cc = (mmio_read32((volatile uint32_t *)&evt->status) >> 24) & 0xFF;
      *slot_id = (uint8_t)((ctrl >> 24) & 0xFF);
      xhci->evt_ring_idx = (xhci->evt_ring_idx + 1) % 64;
      return (cc == 1) ? 0 : -2; /* 1 = success */
    }
    cpu_relax();
  }
  return -3; /* timeout */
}

int xhci_address_device(struct xhci_controller *xhci, uint8_t slot_id, int port) {
  if (!xhci || !xhci->initialized || slot_id == 0) return -1;
  (void)port;
  /* Full address_device requires Input Context + Address Device TRB.
   * Minimal stub: mark slot as addressed for port detection. */
  return 0;
}

int xhci_find_keyboard(struct xhci_controller *xhci, struct usb_device *kbd) {
  if (!xhci || !xhci->initialized || !kbd) return -1;
  /* Scan ports for connected HID device with boot protocol keyboard.
   * Full implementation requires GET_DESCRIPTOR control transfers.
   * Stub: report no keyboard found until descriptor parsing is added. */
  return -1;
}

int xhci_keyboard_poll(struct xhci_controller *xhci, struct usb_device *kbd,
                       uint8_t *key) {
  if (!xhci || !key) return -1;
  (void)kbd;
  /* Full implementation requires interrupt endpoint transfer ring.
   * Stub: no key available. */
  *key = 0;
  return -1;
}

