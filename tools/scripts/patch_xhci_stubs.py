#!/usr/bin/env python3
"""Append missing XHCI function implementations to xhci.c."""

path = "/Volumes/CapyOS/src/drivers/usb/xhci.c"
with open(path, "rb") as f:
    data = f.read()

if b"xhci_enable_slot" in data:
    print("Already patched")
    exit(0)

# Detect line ending
nl = b"\r\n" if b"\r\n" in data else b"\n"

new_code = nl.join([
b"",
b"/* --- Slot and device operations (minimal bring-up) --- */",
b"",
b"int xhci_enable_slot(struct xhci_controller *xhci, uint8_t *slot_id) {",
b"  if (!xhci || !xhci->initialized || !xhci->cmd_ring || !slot_id) return -1;",
b"  /* Build Enable Slot TRB */",
b"  struct xhci_trb trb;",
b"  trb.param = 0;",
b"  trb.status = 0;",
b"  trb.control = (TRB_TYPE_ENABLE_SLOT << 10) | (xhci->cmd_ring_cycle & 1);",
b"  uint32_t idx = xhci->cmd_ring_idx;",
b"  xhci->cmd_ring[idx] = trb;",
b"  xhci->cmd_ring_idx = (idx + 1) % 64;",
b"  /* Ring doorbell 0 (command) */",
b"  volatile uint32_t *db = (volatile uint32_t *)(xhci->db_base);",
b"  mmio_write32(db, 0);",
b"  /* Poll event ring for completion */",
b"  for (int i = 0; i < 500000; i++) {",
b"    struct xhci_trb *evt = &xhci->evt_ring[xhci->evt_ring_idx];",
b"    uint32_t ctrl = mmio_read32((volatile uint32_t *)&evt->control);",
b"    if ((ctrl >> 10 & 0x3F) == TRB_TYPE_CMD_COMPLETE) {",
b"      uint32_t cc = (mmio_read32((volatile uint32_t *)&evt->status) >> 24) & 0xFF;",
b"      *slot_id = (uint8_t)((ctrl >> 24) & 0xFF);",
b"      xhci->evt_ring_idx = (xhci->evt_ring_idx + 1) % 64;",
b"      return (cc == 1) ? 0 : -2; /* 1 = success */",
b"    }",
b"    cpu_relax();",
b"  }",
b"  return -3; /* timeout */",
b"}",
b"",
b"int xhci_address_device(struct xhci_controller *xhci, uint8_t slot_id, int port) {",
b"  if (!xhci || !xhci->initialized || slot_id == 0) return -1;",
b"  (void)port;",
b"  /* Full address_device requires Input Context + Address Device TRB.",
b"   * Minimal stub: mark slot as addressed for port detection. */",
b"  return 0;",
b"}",
b"",
b"int xhci_find_keyboard(struct xhci_controller *xhci, struct usb_device *kbd) {",
b"  if (!xhci || !xhci->initialized || !kbd) return -1;",
b"  /* Scan ports for connected HID device with boot protocol keyboard.",
b"   * Full implementation requires GET_DESCRIPTOR control transfers.",
b"   * Stub: report no keyboard found until descriptor parsing is added. */",
b"  return -1;",
b"}",
b"",
b"int xhci_keyboard_poll(struct xhci_controller *xhci, struct usb_device *kbd,",
b"                       uint8_t *key) {",
b"  if (!xhci || !key) return -1;",
b"  (void)kbd;",
b"  /* Full implementation requires interrupt endpoint transfer ring.",
b"   * Stub: no key available. */",
b"  *key = 0;",
b"  return -1;",
b"}",
b"",
]) + nl

data = data.rstrip() + nl + new_code
with open(path, "wb") as f:
    f.write(data)
print("xhci.c patched OK")
