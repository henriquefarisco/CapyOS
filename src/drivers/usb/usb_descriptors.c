#include "drivers/usb/usb_core.h"

static uint16_t usb_le16(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void usb_zero_endpoints(struct usb_device_info *dev) {
  dev->endpoint_count = 0;
  for (uint8_t i = 0; i < USB_MAX_ENDPOINTS; i++) {
    dev->endpoints[i].address = 0;
    dev->endpoints[i].type = 0;
    dev->endpoints[i].max_packet_size = 0;
    dev->endpoints[i].interval = 0;
  }
}

int usb_build_get_descriptor_request(uint8_t desc_type, uint8_t desc_index,
                                     uint16_t lang_or_index, uint16_t length,
                                     struct usb_setup_packet *out) {
  if (!out || length == 0) {
    return -1;
  }
  out->bmRequestType = 0x80u;
  out->bRequest = USB_REQ_GET_DESCRIPTOR;
  out->wValue = ((uint16_t)desc_type << 8) | desc_index;
  out->wIndex = lang_or_index;
  out->wLength = length;
  return 0;
}

int usb_parse_device_descriptor(const uint8_t *buf, size_t len,
                                struct usb_device_descriptor *out) {
  if (!buf || !out || len < 18u || buf[0] != 18u ||
      buf[1] != USB_DESC_TYPE_DEVICE) {
    return -1;
  }
  out->bLength = buf[0];
  out->bDescriptorType = buf[1];
  out->bcdUSB = usb_le16(&buf[2]);
  out->bDeviceClass = buf[4];
  out->bDeviceSubClass = buf[5];
  out->bDeviceProtocol = buf[6];
  out->bMaxPacketSize0 = buf[7];
  out->idVendor = usb_le16(&buf[8]);
  out->idProduct = usb_le16(&buf[10]);
  out->bcdDevice = usb_le16(&buf[12]);
  out->iManufacturer = buf[14];
  out->iProduct = buf[15];
  out->iSerialNumber = buf[16];
  out->bNumConfigurations = buf[17];
  return 0;
}

int usb_parse_configuration_descriptor(const uint8_t *buf, size_t len,
                                       struct usb_device_info *dev) {
  size_t total;
  size_t off;
  int collect_endpoints;
  struct usb_device_info parsed;
  if (!buf || !dev || len < 9u || buf[0] < 9u ||
      buf[1] != USB_DESC_TYPE_CONFIGURATION) {
    return -1;
  }
  total = usb_le16(&buf[2]);
  if (total < 9u || total > len) {
    return -1;
  }
  parsed = *dev;
  usb_zero_endpoints(&parsed);
  parsed.class_code = 0;
  parsed.subclass = 0;
  parsed.protocol = 0;
  parsed.configuration_value = buf[5];
  parsed.interface_number = 0;
  parsed.is_keyboard = 0;
  parsed.is_mouse = 0;
  off = 0;
  collect_endpoints = 0;
  while (off < total) {
    uint8_t dlen;
    uint8_t dtype;
    if (total - off < 2u) return -1;
    dlen = buf[off];
    dtype = buf[off + 1u];
    if (dlen < 2u || off + dlen > total) return -1;
    if (dtype == USB_DESC_TYPE_INTERFACE && dlen >= 9u) {
      uint8_t class_code = buf[off + 5u];
      uint8_t subclass = buf[off + 6u];
      uint8_t protocol = buf[off + 7u];
      if (class_code == USB_CLASS_HID && parsed.class_code != USB_CLASS_HID) {
        usb_zero_endpoints(&parsed);
      }
      if (parsed.class_code == 0 || class_code == USB_CLASS_HID) {
        parsed.interface_number = buf[off + 2u];
        parsed.class_code = class_code;
        parsed.subclass = subclass;
        parsed.protocol = protocol;
        collect_endpoints = 1;
      } else {
        collect_endpoints = 0;
      }
      if (class_code == USB_CLASS_HID && subclass == USB_SUBCLASS_BOOT &&
          protocol == USB_PROTOCOL_KBD) {
        parsed.is_keyboard = 1;
      }
      if (class_code == USB_CLASS_HID && subclass == USB_SUBCLASS_BOOT &&
          protocol == USB_PROTOCOL_MOUSE) {
        parsed.is_mouse = 1;
      }
    } else if (dtype == USB_DESC_TYPE_ENDPOINT && dlen >= 7u && collect_endpoints) {
      if (parsed.endpoint_count < USB_MAX_ENDPOINTS) {
        struct usb_endpoint_info *ep = &parsed.endpoints[parsed.endpoint_count];
        ep->address = buf[off + 2u];
        ep->type = buf[off + 3u] & 0x3u;
        ep->max_packet_size = usb_le16(&buf[off + 4u]);
        ep->interval = buf[off + 6u];
        parsed.endpoint_count++;
      }
    }
    off += dlen;
  }
  *dev = parsed;
  return 0;
}
