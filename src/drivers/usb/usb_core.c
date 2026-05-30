#include "drivers/usb/usb_core.h"
#include "drivers/usb/usb_hid.h"
#include "drivers/usb/xhci.h"
#include "kernel/log/klog.h"
#include <stddef.h>

static struct usb_device_info g_devices[USB_MAX_DEVICES];
static int g_device_count = 0;
static struct xhci_controller g_xhci;
static int g_usb_initialized = 0;

static void usb_memset(void *d, int v, size_t n) {
  uint8_t *p = (uint8_t *)d;
  for (size_t i = 0; i < n; i++) p[i] = (uint8_t)v;
}

static uint16_t usb_read_le16(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static int usb_find_existing_addressed_device(const struct usb_device_info *devices,
                                              int count, uint8_t port,
                                              struct usb_device_info *out) {
  if (!devices || !out || count < 0) return -1;
  for (int i = 0; i < count && i < USB_MAX_DEVICES; i++) {
    if (devices[i].port == port && devices[i].slot_id != 0 &&
        (devices[i].state == USB_DEV_ADDRESSED ||
         devices[i].state == USB_DEV_CONFIGURED)) {
      *out = devices[i];
      return 0;
    }
  }
  return -1;
}

static int usb_get_descriptor(uint8_t slot_id, uint8_t type, uint8_t index,
                              void *buf, uint16_t len) {
  struct usb_setup_packet setup;
  if (!buf || len == 0) return -1;
  if (usb_build_get_descriptor_request(type, index, 0, len, &setup) != 0) return -1;
  return xhci_control_transfer(&g_xhci, slot_id, &setup, buf, len, 1);
}

static int usb_set_configuration(uint8_t slot_id, uint8_t configuration_value) {
  struct usb_setup_packet setup;
  if (slot_id == 0 || configuration_value == 0) return -1;
  setup.bmRequestType = 0x00u;
  setup.bRequest = USB_REQ_SET_CONFIGURATION;
  setup.wValue = configuration_value;
  setup.wIndex = 0;
  setup.wLength = 0;
  return xhci_control_transfer(&g_xhci, slot_id, &setup, NULL, 0, 0);
}

static int usb_hid_set_boot_protocol(uint8_t slot_id, uint8_t interface_number) {
  struct usb_setup_packet setup;
  if (slot_id == 0) return -1;
  setup.bmRequestType = 0x21u;
  setup.bRequest = USB_HID_REQ_SET_PROTOCOL;
  setup.wValue = 0;
  setup.wIndex = interface_number;
  setup.wLength = 0;
  return xhci_control_transfer(&g_xhci, slot_id, &setup, NULL, 0, 0);
}

static int usb_read_and_parse_descriptors(struct usb_device_info *dev) {
  uint8_t device_desc[18];
  uint8_t config_head[9];
  uint8_t config_desc[256];
  uint16_t total;
  struct usb_device_descriptor parsed_descriptor;
  struct usb_device_info parsed_dev;
  if (!dev || dev->slot_id == 0) return -1;
  usb_memset(device_desc, 0, sizeof(device_desc));
  usb_memset(config_head, 0, sizeof(config_head));
  usb_memset(config_desc, 0, sizeof(config_desc));
  usb_memset(&parsed_descriptor, 0, sizeof(parsed_descriptor));
  parsed_dev = *dev;
  if (usb_get_descriptor(dev->slot_id, USB_DESC_TYPE_DEVICE, 0, device_desc,
                         sizeof(device_desc)) != 0) {
    return -1;
  }
  if (usb_parse_device_descriptor(device_desc, sizeof(device_desc),
                                  &parsed_descriptor) != 0) {
    return -1;
  }
  if (usb_get_descriptor(dev->slot_id, USB_DESC_TYPE_CONFIGURATION, 0,
                         config_head, sizeof(config_head)) != 0) {
    return -1;
  }
  if (config_head[0] < sizeof(config_head) ||
      config_head[1] != USB_DESC_TYPE_CONFIGURATION) {
    return -1;
  }
  total = usb_read_le16(&config_head[2]);
  if (total < sizeof(config_head) || total > sizeof(config_desc)) {
    return -1;
  }
  if (usb_get_descriptor(dev->slot_id, USB_DESC_TYPE_CONFIGURATION, 0,
                         config_desc, total) != 0) {
    return -1;
  }
  if (usb_parse_configuration_descriptor(config_desc, total, &parsed_dev) != 0) {
    return -1;
  }
  parsed_dev.descriptor = parsed_descriptor;
  if (parsed_dev.class_code == 0) {
    parsed_dev.class_code = parsed_descriptor.bDeviceClass;
    parsed_dev.subclass = parsed_descriptor.bDeviceSubClass;
    parsed_dev.protocol = parsed_descriptor.bDeviceProtocol;
  }
  *dev = parsed_dev;
  return 0;
}

static const struct usb_endpoint_info *usb_first_interrupt_in_endpoint(
    const struct usb_device_info *dev) {
  if (!dev) return NULL;
  for (uint8_t i = 0; i < dev->endpoint_count; i++) {
    const struct usb_endpoint_info *ep = &dev->endpoints[i];
    if ((ep->type & 0x03u) == 3u && (ep->address & 0x80u)) return ep;
  }
  return NULL;
}

static uint16_t usb_hid_report_len(const struct usb_device_info *dev,
                                   const struct usb_endpoint_info *ep) {
  if (!ep) return 0;
  if (usb_device_is_hid_keyboard(dev)) {
    return ep->max_packet_size >= sizeof(struct usb_hid_keyboard_report)
               ? sizeof(struct usb_hid_keyboard_report)
               : 0;
  }
  if (usb_device_is_hid_mouse(dev)) {
    if (ep->max_packet_size < 3u) return 0;
    return ep->max_packet_size >= sizeof(struct usb_hid_mouse_report)
               ? sizeof(struct usb_hid_mouse_report)
               : 3u;
  }
  return 0;
}

static int usb_configure_hid_interrupt_endpoint(struct usb_device_info *dev) {
  const struct usb_endpoint_info *ep;
  uint16_t report_len;
  if (!dev || dev->slot_id == 0) return -1;
  if (!usb_device_is_hid_keyboard(dev) && !usb_device_is_hid_mouse(dev)) return -1;
  ep = usb_first_interrupt_in_endpoint(dev);
  report_len = usb_hid_report_len(dev, ep);
  if (!ep || report_len == 0) return -1;
  if (usb_set_configuration(dev->slot_id, dev->configuration_value) != 0) {
    return -1;
  }
  if (usb_hid_set_boot_protocol(dev->slot_id, dev->interface_number) != 0) {
    return -1;
  }
  if (xhci_configure_interrupt_endpoint(&g_xhci, dev->slot_id, ep, report_len) != 0) {
    return -1;
  }
  dev->state = USB_DEV_CONFIGURED;
  return 0;
}

void usb_core_init(void) {
  usb_memset(g_devices, 0, sizeof(g_devices));
  usb_memset(&g_xhci, 0, sizeof(g_xhci));
  g_device_count = 0;

  if (xhci_find(&g_xhci) != 0) {
    klog(KLOG_INFO, "[usb] No XHCI controller found.");
    return;
  }

  if (xhci_init(&g_xhci) != 0) {
    klog(KLOG_WARN, "[usb] XHCI init failed.");
    return;
  }

  if (xhci_start(&g_xhci) != 0) {
    klog(KLOG_WARN, "[usb] XHCI start failed.");
    return;
  }

  g_usb_initialized = 1;
  klog(KLOG_INFO, "[usb] XHCI controller initialized.");
  klog_dec(KLOG_INFO, "[usb] Max ports: ", g_xhci.max_ports);
}

/* Etapa 3 — Slice 3D hardening (§14.3 follow-up + post-audit fix).
 *
 * Releases controller-side resources for slots whose owning port has
 * either lost the device (CCS=0) or been status-changed (CSC=1,
 * indicating a port-cycle that replaced the device). Must run BEFORE
 * the new enumeration loop, otherwise a slot whose ID is recycled by
 * `xhci_enable_slot` would collide with stale `device_contexts[]` /
 * `ep0_rings[]` populated from the prior device, and the new
 * `xhci_address_device` would fail with -1 (slot busy).
 *
 * Reuse semantics: when CCS=1 AND CSC=0, the same device is still
 * physically present and the enumeration loop reuses the existing
 * slot via `usb_find_existing_addressed_device`; we MUST NOT release
 * it here. */
static void usb_release_stale_slots(const struct usb_device_info *previous,
                                    int previous_count) {
  int i;
  for (i = 0; i < previous_count && i < USB_MAX_DEVICES; i++) {
    uint8_t prev_slot = previous[i].slot_id;
    uint8_t prev_port = previous[i].port;
    int status;
    if (prev_slot == 0u) continue;
    status = xhci_port_get_status(&g_xhci, prev_port);
    if (status < 0) continue;
    /* Device still attached AND no status change → keep slot for the
     * reuse path in usb_enumerate_devices. */
    if ((status & XHCI_PORTSC_CCS) && !(status & XHCI_PORTSC_CSC)) {
      continue;
    }
    (void)xhci_release_slot(&g_xhci, prev_slot);
    klog_dec(KLOG_INFO, "[usb] Released slot ", prev_slot);
  }
}

int usb_enumerate_devices(void) {
  if (!g_usb_initialized) return 0;

  int found = 0;
  int previous_count = g_device_count;
  struct usb_device_info previous[USB_MAX_DEVICES];
  for (int i = 0; i < USB_MAX_DEVICES; i++) previous[i] = g_devices[i];
  /* Release controller resources for ports that lost their device or
   * port-cycled, BEFORE we issue Enable Slot. This prevents slot ID
   * recycling by xHCI from colliding with stale per-slot state from
   * the previous device on that slot. */
  usb_release_stale_slots(previous, previous_count);
  for (uint8_t port = 0; port < g_xhci.max_ports && found < USB_MAX_DEVICES; port++) {
    int status = xhci_port_get_status(&g_xhci, port);
    uint8_t slot_id = 0;
    struct usb_device_info *dev = NULL;
    if (!(status & 0x01)) continue; /* no device connected */
    dev = &g_devices[found];
    if (!(status & XHCI_PORTSC_CSC) &&
        usb_find_existing_addressed_device(previous, previous_count, port, dev) == 0) {
      found++;
      continue;
    }

    if (xhci_port_reset(&g_xhci, port) != 0) continue;

    usb_memset(dev, 0, sizeof(*dev));
    dev->port = port;
    dev->state = USB_DEV_ATTACHED;

    if (xhci_enable_slot(&g_xhci, &slot_id) != 0 || slot_id == 0) {
      dev->state = USB_DEV_ERROR;
      found++;
      klog_dec(KLOG_WARN, "[usb] XHCI enable slot failed on port ", port);
      continue;
    }
    dev->slot_id = slot_id;
    if (xhci_address_device(&g_xhci, slot_id, port) != 0) {
      dev->state = USB_DEV_ERROR;
      found++;
      klog_dec(KLOG_WARN, "[usb] XHCI address device failed on port ", port);
      continue;
    }
    dev->state = USB_DEV_ADDRESSED;
    if (usb_read_and_parse_descriptors(dev) != 0) {
      klog_dec(KLOG_WARN, "[usb] XHCI descriptor read failed on port ", port);
    } else if ((usb_device_is_hid_keyboard(dev) || usb_device_is_hid_mouse(dev)) &&
               usb_configure_hid_interrupt_endpoint(dev) != 0) {
      klog_dec(KLOG_WARN, "[usb] XHCI HID interrupt endpoint config failed on port ", port);
    }
    found++;
    klog_dec(KLOG_INFO, "[usb] XHCI addressed slot ", slot_id);
  }

  g_device_count = found;
  return found;
}

int usb_get_device_count(void) { return g_device_count; }

int usb_get_device(int index, struct usb_device_info *out) {
  if (index < 0 || index >= g_device_count || !out) return -1;
  *out = g_devices[index];
  return 0;
}

int usb_device_is_hid_keyboard(const struct usb_device_info *dev) {
  if (!dev) return 0;
  return (dev->class_code == USB_CLASS_HID &&
          dev->subclass == USB_SUBCLASS_BOOT &&
          dev->protocol == USB_PROTOCOL_KBD) ? 1 : dev->is_keyboard;
}

int usb_device_is_hid_mouse(const struct usb_device_info *dev) {
  if (!dev) return 0;
  return (dev->class_code == USB_CLASS_HID &&
          dev->subclass == USB_SUBCLASS_BOOT &&
          dev->protocol == USB_PROTOCOL_MOUSE) ? 1 : dev->is_mouse;
}

void usb_poll_all(void) {
  if (!g_usb_initialized) return;
  /* P1-F fix (2026-05-25): defense-in-depth bound. `g_device_count`
   * is normally capped at `USB_MAX_DEVICES` by `usb_enumerate_devices`,
   * but a stray write elsewhere could push it above the array size
   * and the per-frame poll path would then read past `g_devices[]`
   * indefinitely. The redundant `i < USB_MAX_DEVICES` clamp keeps
   * the loop honest at zero cost per iteration. */
  for (int i = 0; i < g_device_count && i < USB_MAX_DEVICES; i++) {
    struct usb_device_info *dev = &g_devices[i];
    const struct usb_endpoint_info *ep;
    uint8_t report[8];
    int len;
    if (dev->state != USB_DEV_CONFIGURED) continue;
    ep = usb_first_interrupt_in_endpoint(dev);
    if (!ep) continue;
    len = xhci_poll_interrupt(&g_xhci, dev->slot_id, ep->address, report,
                              sizeof(report));
    if (len <= 0) continue;
    if (usb_device_is_hid_keyboard(dev) &&
        len >= (int)sizeof(struct usb_hid_keyboard_report)) {
      usb_hid_handle_keyboard_report((const struct usb_hid_keyboard_report *)report);
    } else if (usb_device_is_hid_mouse(dev) && len >= 3) {
      struct usb_hid_mouse_report mouse_report;
      mouse_report.buttons = report[0];
      mouse_report.dx = (int8_t)report[1];
      mouse_report.dy = (int8_t)report[2];
      mouse_report.dz = len >= (int)sizeof(struct usb_hid_mouse_report)
                            ? (int8_t)report[3]
                            : 0;
      usb_hid_handle_mouse_report(&mouse_report);
    }
  }
}

/* Etapa 3 — Slice 3D §15.3: HID SET_REPORT (Output) to drive keyboard
 * LEDs (Caps Lock / Num Lock / Scroll Lock). Uses the standard HID
 * class request on EP0 with a 1-byte payload following the HID Usage
 * Tables §10 keyboard layout. */
int usb_hid_send_led_report(uint8_t slot_id, uint8_t interface_number,
                            uint8_t led_bitmap) {
  struct usb_setup_packet setup;
  uint8_t buf;
  if (slot_id == 0u) return -1;
  setup.bmRequestType = 0x21u; /* Host-to-Device | Class | Interface */
  setup.bRequest = 0x09u;       /* SET_REPORT */
  setup.wValue = 0x0200u;       /* ReportType=Output(2), ReportID=0 */
  setup.wIndex = (uint16_t)interface_number;
  setup.wLength = 1u;
  buf = led_bitmap;
  return xhci_control_transfer(&g_xhci, slot_id, &setup, &buf, 1u, 0);
}

/* Etapa 3 — Slice 3D §15.1 fix: detect Port Status Change on each
 * root-hub port and trigger re-enumeration. CSC is RW1C; we clear it
 * via `xhci_port_ack_csc` BEFORE calling `usb_enumerate_devices` so
 * subsequent polls don't re-fire on the same event. */
void usb_hotplug_check(void) {
  int any_change = 0;
  if (!g_usb_initialized) return;
  for (uint8_t port = 0; port < g_xhci.max_ports; port++) {
    int status = xhci_port_get_status(&g_xhci, port);
    if (status < 0) continue;
    if ((uint32_t)status & XHCI_PORTSC_CSC) {
      klog_dec(KLOG_INFO, "[usb] Hotplug event on port ", port);
      (void)xhci_port_ack_csc(&g_xhci, port);
      any_change = 1;
    }
  }
  if (any_change) {
    /* Single re-enumeration handles all changed ports in one pass; the
     * inner loop in usb_enumerate_devices iterates all ports and
     * `usb_release_stale_slots` releases departed devices. */
    (void)usb_enumerate_devices();
  }
}
