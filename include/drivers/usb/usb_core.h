#ifndef DRIVERS_USB_CORE_H
#define DRIVERS_USB_CORE_H

#include <stdint.h>
#include <stddef.h>

#define USB_MAX_DEVICES 16
#define USB_MAX_ENDPOINTS 8

#define USB_CLASS_HID      3
#define USB_SUBCLASS_BOOT  1
#define USB_PROTOCOL_KBD   1
#define USB_PROTOCOL_MOUSE 2
#define USB_CLASS_STORAGE  8

#define USB_DESC_TYPE_DEVICE        1
#define USB_DESC_TYPE_CONFIGURATION 2
#define USB_DESC_TYPE_INTERFACE     4
#define USB_DESC_TYPE_ENDPOINT      5

#define USB_REQ_GET_DESCRIPTOR 6
#define USB_REQ_SET_CONFIGURATION 9
#define USB_HID_REQ_SET_PROTOCOL 11

struct usb_setup_packet {
  uint8_t bmRequestType;
  uint8_t bRequest;
  uint16_t wValue;
  uint16_t wIndex;
  uint16_t wLength;
} __attribute__((packed));

/* USB device state machine.
 *
 * Contract for upper layers (e.g. usb_hid):
 *   - DISCONNECTED: slot is empty; do not consume.
 *   - ATTACHED:     port has device, reset done, slot not yet assigned.
 *                   No class/subclass/protocol info is populated yet.
 *                   Upper layers must not try to claim the device.
 *   - ADDRESSED:    xhci slot enabled, Address Device command completed.
 *                   Device/configuration descriptors MAY be populated.
 *                   Upper layers may probe the device only when
 *                   class_code != 0.
 *   - CONFIGURED:   configuration descriptor parsed, endpoints populated,
 *                   SET_CONFIGURATION, HID boot protocol and Configure
 *                   Endpoint completed.
 *                   Polling is safe.
 *   - ERROR:        the device hit an unrecoverable error; ignore.
 *
 * Slices that own each transition:
 *   ATTACHED -> ADDRESSED:  slice 3B (xhci_address_device + enumerate).
 *   ADDRESSED descriptor enrichment: slice 3C (control + parsing).
 *   ADDRESSED -> CONFIGURED: slice 3D (SET_CONFIGURATION + HID boot protocol + Configure EP + interrupt transfer).
 */
enum usb_device_state {
  USB_DEV_DISCONNECTED = 0,
  USB_DEV_ATTACHED,
  USB_DEV_ADDRESSED,
  USB_DEV_CONFIGURED,
  USB_DEV_ERROR
};

struct usb_device_descriptor {
  uint8_t  bLength;
  uint8_t  bDescriptorType;
  uint16_t bcdUSB;
  uint8_t  bDeviceClass;
  uint8_t  bDeviceSubClass;
  uint8_t  bDeviceProtocol;
  uint8_t  bMaxPacketSize0;
  uint16_t idVendor;
  uint16_t idProduct;
  uint16_t bcdDevice;
  uint8_t  iManufacturer;
  uint8_t  iProduct;
  uint8_t  iSerialNumber;
  uint8_t  bNumConfigurations;
} __attribute__((packed));

struct usb_endpoint_info {
  uint8_t address;
  uint8_t type;
  uint16_t max_packet_size;
  uint8_t interval;
};

struct usb_device_info {
  uint8_t slot_id;
  uint8_t port;
  enum usb_device_state state;
  struct usb_device_descriptor descriptor;
  struct usb_endpoint_info endpoints[USB_MAX_ENDPOINTS];
  uint8_t endpoint_count;
  uint8_t configuration_value;
  uint8_t interface_number;
  uint8_t class_code;
  uint8_t subclass;
  uint8_t protocol;
  int is_keyboard;
  int is_mouse;
};

void usb_core_init(void);
int usb_enumerate_devices(void);
int usb_get_device_count(void);
int usb_get_device(int index, struct usb_device_info *out);
int usb_device_is_hid_keyboard(const struct usb_device_info *dev);
int usb_device_is_hid_mouse(const struct usb_device_info *dev);
int usb_build_get_descriptor_request(uint8_t desc_type, uint8_t desc_index,
                                     uint16_t lang_or_index, uint16_t length,
                                     struct usb_setup_packet *out);
int usb_parse_device_descriptor(const uint8_t *buf, size_t len,
                                struct usb_device_descriptor *out);
int usb_parse_configuration_descriptor(const uint8_t *buf, size_t len,
                                       struct usb_device_info *dev);
void usb_poll_all(void);
void usb_hotplug_check(void);

/* Etapa 3 — Slice 3D §15.3: send HID Output Report (keyboard LEDs)
 * via SET_REPORT class request on EP0. `led_bitmap` follows the
 * HID Usage Tables §10 keyboard layout: bit 0=NumLock, bit 1=CapsLock,
 * bit 2=ScrollLock, bit 3=Compose, bit 4=Kana. Returns 0 on success
 * or negative on transfer failure. */
int usb_hid_send_led_report(uint8_t slot_id, uint8_t interface_number,
                            uint8_t led_bitmap);

#endif /* DRIVERS_USB_CORE_H */
