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
void usb_poll_all(void);
void usb_hotplug_check(void);

#endif /* DRIVERS_USB_CORE_H */
