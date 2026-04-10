#include "drivers/usb/usb_core.h"
#include "drivers/usb/xhci.h"
#include "core/klog.h"
#include <stddef.h>

static struct usb_device_info g_devices[USB_MAX_DEVICES];
static int g_device_count = 0;
static struct xhci_controller g_xhci;
static int g_usb_initialized = 0;

static void usb_memset(void *d, int v, size_t n) {
  uint8_t *p = (uint8_t *)d;
  for (size_t i = 0; i < n; i++) p[i] = (uint8_t)v;
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
  for (uint8_t port = 0; port < g_xhci.max_ports && found < USB_MAX_DEVICES; port++) {
    int status = xhci_port_get_status(&g_xhci, port);
    if (!(status & 0x01)) continue; /* no device connected */

    if (xhci_port_reset(&g_xhci, port) != 0) continue;

    /* TODO: full enumeration requires xhci_enable_slot + xhci_address_device
     * which are not yet implemented in the XHCI driver. For now, record
     * that a device is attached on this port. */
    struct usb_device_info *dev = &g_devices[found];
    usb_memset(dev, 0, sizeof(*dev));
    dev->slot_id = 0;
    dev->port = port;
    dev->state = USB_DEV_ATTACHED;

    found++;
    klog_dec(KLOG_INFO, "[usb] Device detected on port ", port);
  }

  g_device_count = found;
  if (found > 0) {
    klog(KLOG_INFO, "[usb] Full enumeration pending (enable_slot/address not yet implemented).");
  }
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
  /* TODO: USB HID polling requires endpoint transfer ring setup,
   * which depends on xhci_enable_slot/address_device. Stub for now. */
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
