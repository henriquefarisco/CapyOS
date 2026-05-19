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

int usb_enumerate_devices(void) {
  if (!g_usb_initialized) return 0;

  int found = 0;
  int previous_count = g_device_count;
  struct usb_device_info previous[USB_MAX_DEVICES];
  for (int i = 0; i < USB_MAX_DEVICES; i++) previous[i] = g_devices[i];
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
  for (int i = 0; i < g_device_count; i++) {
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

void usb_hotplug_check(void) {
  if (!g_usb_initialized) return;
  for (uint8_t port = 0; port < g_xhci.max_ports; port++) {
    int status = xhci_port_get_status(&g_xhci, port);
    if (status & 0x020000) { /* Port Status Change */
      klog_dec(KLOG_INFO, "[usb] Hotplug event on port ", port);
      usb_enumerate_devices();
    }
  }
}
