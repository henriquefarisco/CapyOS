/* usb_hid.c: USB HID class driver for keyboard and mouse.
 * Scans USB devices enumerated by usb_core for HID boot protocol keyboards
 * and mice, then polls interrupt endpoints for input reports. */
#include "drivers/usb/usb_hid.h"
#include "drivers/usb/usb_core.h"
#include "core/klog.h"

#include <stddef.h>
#include <stdint.h>

/* HID keyboard scancode to ASCII lookup (US layout, boot protocol).
 * Index = USB HID usage ID, value = ASCII character. */
static const char hid_scancode_to_ascii[128] = {
  0,0,0,0,
  'a','b','c','d','e','f','g','h','i','j','k','l','m',
  'n','o','p','q','r','s','t','u','v','w','x','y','z',
  '1','2','3','4','5','6','7','8','9','0',
  '\n',   /* 0x28 Enter */
  0x1B,   /* 0x29 Escape */
  '\b',   /* 0x2A Backspace */
  '\t',   /* 0x2B Tab */
  ' ',    /* 0x2C Space */
  '-','=','[',']','\\',0,';','\'','`',',','.','/',
  0,0,0,0,0,0,0,0,0,0,0,0, /* F1-F12 */
  0,0,0,0,0,0, /* PrintScr, ScrollLock, Pause, Insert, Home, PgUp */
  0,   /* 0x4C Delete */
  0,0,0, /* End, PgDn, Right */
  0,0,0,0, /* Left, Down, Up, NumLock */
};

static const char hid_scancode_shift[128] = {
  0,0,0,0,
  'A','B','C','D','E','F','G','H','I','J','K','L','M',
  'N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
  '!','@','#','$','%','^','&','*','(',')',
  '\n',0x1B,'\b','\t',' ',
  '_','+','{','}','|',0,':','"','~','<','>','?',
};

struct usb_hid_state {
  int initialized;
  int kbd_slot;
  int kbd_endpoint;
  int mouse_slot;
  int mouse_endpoint;
  char kbd_buffer[USB_HID_KBD_BUFFER_SIZE];
  uint8_t kbd_head;
  uint8_t kbd_tail;
  uint8_t prev_keys[6];
};

static struct usb_hid_state g_hid;

static void hid_zero(void *dst, size_t len) {
  uint8_t *p = (uint8_t *)dst;
  for (size_t i = 0; i < len; i++) p[i] = 0;
}

int usb_hid_init(void) {
  int dev_count;
  struct usb_device_info dev;

  hid_zero(&g_hid, sizeof(g_hid));
  g_hid.kbd_slot = -1;
  g_hid.mouse_slot = -1;

  dev_count = usb_get_device_count();
  if (dev_count <= 0) {
    klog(KLOG_INFO, "[usb-hid] No USB devices enumerated.");
    return -1;
  }

  for (int i = 0; i < dev_count; i++) {
    if (usb_get_device(i, &dev) != 0) continue;
    if (dev.state != USB_DEV_CONFIGURED) continue;

    if (usb_device_is_hid_keyboard(&dev) && g_hid.kbd_slot < 0) {
      g_hid.kbd_slot = i;
      for (uint8_t e = 0; e < dev.endpoint_count; e++) {
        if ((dev.endpoints[e].type & 0x03) == 3) { /* Interrupt */
          g_hid.kbd_endpoint = dev.endpoints[e].address;
          break;
        }
      }
      klog(KLOG_INFO, "[usb-hid] HID keyboard found.");
    }

    if (usb_device_is_hid_mouse(&dev) && g_hid.mouse_slot < 0) {
      g_hid.mouse_slot = i;
      for (uint8_t e = 0; e < dev.endpoint_count; e++) {
        if ((dev.endpoints[e].type & 0x03) == 3) {
          g_hid.mouse_endpoint = dev.endpoints[e].address;
          break;
        }
      }
      klog(KLOG_INFO, "[usb-hid] HID mouse found.");
    }
  }

  g_hid.initialized = 1;
  return (g_hid.kbd_slot >= 0 || g_hid.mouse_slot >= 0) ? 0 : -1;
}

static void kbd_buffer_push(char c) {
  uint8_t next = (g_hid.kbd_head + 1) % USB_HID_KBD_BUFFER_SIZE;
  if (next == g_hid.kbd_tail) return; /* full */
  g_hid.kbd_buffer[g_hid.kbd_head] = c;
  g_hid.kbd_head = next;
}

static int kbd_buffer_pop(char *c) {
  if (g_hid.kbd_head == g_hid.kbd_tail) return 0;
  *c = g_hid.kbd_buffer[g_hid.kbd_tail];
  g_hid.kbd_tail = (g_hid.kbd_tail + 1) % USB_HID_KBD_BUFFER_SIZE;
  return 1;
}

static void process_kbd_report(const struct usb_hid_keyboard_report *report) {
  int shift = (report->modifiers & 0x22) ? 1 : 0; /* L/R Shift */

  for (int i = 0; i < 6; i++) {
    uint8_t key = report->keys[i];
    if (key == 0) continue;

    /* Check if key was already pressed in previous report */
    int already = 0;
    for (int j = 0; j < 6; j++) {
      if (g_hid.prev_keys[j] == key) { already = 1; break; }
    }
    if (already) continue;

    /* New key press - translate to ASCII */
    if (key < 128) {
      char ch = shift ? hid_scancode_shift[key] : hid_scancode_to_ascii[key];
      if (ch) kbd_buffer_push(ch);
    }
  }

  for (int i = 0; i < 6; i++) {
    g_hid.prev_keys[i] = report->keys[i];
  }
}

int usb_hid_keyboard_available(void) {
  return g_hid.initialized && g_hid.kbd_slot >= 0;
}

int usb_hid_keyboard_poll(char *out_char) {
  if (!g_hid.initialized || g_hid.kbd_slot < 0 || !out_char) return 0;

  /* Poll USB core for new data (stub: real impl needs xHCI interrupt TRB) */
  usb_poll_all();

  return kbd_buffer_pop(out_char);
}

int usb_hid_mouse_available(void) {
  return g_hid.initialized && g_hid.mouse_slot >= 0;
}

int usb_hid_mouse_poll(int8_t *dx, int8_t *dy, int8_t *dz, uint8_t *buttons) {
  if (!g_hid.initialized || g_hid.mouse_slot < 0) return 0;
  if (!dx || !dy || !dz || !buttons) return 0;

  usb_poll_all();

  /* Stub: real implementation needs to read interrupt transfer from xHCI */
  *dx = 0;
  *dy = 0;
  *dz = 0;
  *buttons = 0;
  return 0;
}
