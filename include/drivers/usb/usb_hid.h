#ifndef DRIVERS_USB_HID_H
#define DRIVERS_USB_HID_H
/* USB HID class driver for keyboard and mouse.
 * Works with the xHCI host controller via usb_core. */
#include <stdint.h>

#define USB_HID_KBD_BUFFER_SIZE 16

struct usb_hid_keyboard_report {
  uint8_t modifiers;
  uint8_t reserved;
  uint8_t keys[6];
} __attribute__((packed));

struct usb_hid_mouse_report {
  uint8_t buttons;
  int8_t dx;
  int8_t dy;
  int8_t dz;
} __attribute__((packed));

int usb_hid_init(void);
int usb_hid_keyboard_poll(char *out_char);
int usb_hid_keyboard_available(void);
int usb_hid_mouse_poll(int8_t *dx, int8_t *dy, int8_t *dz, uint8_t *buttons);
int usb_hid_mouse_available(void);

#endif /* DRIVERS_USB_HID_H */
