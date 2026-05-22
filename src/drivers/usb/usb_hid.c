/* usb_hid.c: USB HID class driver for keyboard and mouse.
 * Scans USB devices enumerated by usb_core for HID boot protocol keyboards
 * and mice, then polls interrupt endpoints for input reports. */
#include "drivers/usb/usb_hid.h"
#include "drivers/usb/usb_core.h"
#include "drivers/usb/usb_hid_smoke.h"
#include "kernel/log/klog.h"

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
  struct usb_hid_mouse_report mouse_report;
  int mouse_report_ready;
  /* Etapa 3 — Slice 3D external validation gate counters. */
  uint32_t kbd_configured_count;
  uint32_t kbd_chars_received;
  struct usb_hid_keyboard_smoke_state smoke;
  /* Etapa 3 — Slice 3D §15.3: keyboard LED state and the device
   * coordinates needed to dispatch SET_REPORT.
   * `led_state`: bit 0 NumLock, bit 1 CapsLock, bit 2 ScrollLock per
   * HID Usage Tables §10.
   * `kbd_slot_id` / `kbd_interface`: captured at usb_hid_init from
   * the keyboard's `struct usb_device_info`. Independent of
   * `kbd_slot` which is the g_devices index for the cooperative
   * input path. */
  uint8_t led_state;
  uint8_t kbd_slot_id;
  uint8_t kbd_interface;
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
    /* Accept either ADDRESSED or CONFIGURED. ADDRESSED is allowed because
     * the slice that populates class info from the device descriptor may
     * leave the device in ADDRESSED state until Configure Endpoint runs.
     * Reject anything without a class code so we never treat a bare
     * port-reset device as a HID. */
    if (dev.state != USB_DEV_ADDRESSED && dev.state != USB_DEV_CONFIGURED) continue;
    if (dev.class_code == 0) continue;

    if (usb_device_is_hid_keyboard(&dev) && g_hid.kbd_slot < 0) {
      g_hid.kbd_slot = i;
      g_hid.kbd_slot_id = dev.slot_id;
      g_hid.kbd_interface = dev.interface_number;
      for (uint8_t e = 0; e < dev.endpoint_count; e++) {
        if ((dev.endpoints[e].type & 0x03) == 3) { /* Interrupt */
          g_hid.kbd_endpoint = dev.endpoints[e].address;
          break;
        }
      }
      klog(KLOG_INFO, "[usb-hid] HID keyboard found.");
    }
    /* Etapa 3 — Slice 3D smoke gate: count HID keyboards that reached
     * USB_DEV_CONFIGURED. Only those can actually deliver interrupt
     * reports, so this is the strict denominator for the readiness gate. */
    if (usb_device_is_hid_keyboard(&dev) && dev.state == USB_DEV_CONFIGURED &&
        g_hid.kbd_configured_count < 0xFFFFFFFFu) {
      g_hid.kbd_configured_count++;
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
  /* Etapa 3 — Slice 3D smoke gate: a buffered char proves the end-to-end
   * XHCI -> Configure Endpoint -> interrupt transfer -> HID handler path
   * actually delivered a keystroke from the device. Combined with the
   * configured-keyboard count from usb_hid_init, this satisfies the
   * external readiness contract documented in usb_hid_smoke.h. The
   * smoke state latches `emitted` after the first transition, so the
   * marker is written to COM1 at most once per boot. */
  if (g_hid.kbd_chars_received < 0xFFFFFFFFu) {
    g_hid.kbd_chars_received++;
  }
  if (usb_hid_keyboard_smoke_observe(&g_hid.smoke, g_hid.kbd_configured_count,
                                     g_hid.kbd_chars_received)) {
    usb_hid_keyboard_smoke_emit_marker();
  }
}

static int kbd_buffer_pop(char *c) {
  if (g_hid.kbd_head == g_hid.kbd_tail) return 0;
  *c = g_hid.kbd_buffer[g_hid.kbd_tail];
  g_hid.kbd_tail = (g_hid.kbd_tail + 1) % USB_HID_KBD_BUFFER_SIZE;
  return 1;
}

/* Etapa 3 — Slice 3D §15.3: HID Usage IDs for lock keys per HID Usage
 * Tables §10 keyboard page. */
#define USB_HID_USAGE_CAPS_LOCK 0x39u
#define USB_HID_USAGE_SCROLL_LOCK 0x47u
#define USB_HID_USAGE_NUM_LOCK 0x53u

/* LED bitmap bit positions per HID §10. */
#define USB_HID_LED_NUM_LOCK 0x01u
#define USB_HID_LED_CAPS_LOCK 0x02u
#define USB_HID_LED_SCROLL_LOCK 0x04u

static void usb_hid_dispatch_led_report(void) {
  if (g_hid.kbd_slot_id == 0u) return;
  (void)usb_hid_send_led_report(g_hid.kbd_slot_id, g_hid.kbd_interface,
                                g_hid.led_state);
}

void usb_hid_handle_keyboard_report(const struct usb_hid_keyboard_report *report) {
  if (!report) return;
  int shift = (report->modifiers & 0x22) ? 1 : 0; /* L/R Shift */
  /* Etapa 3 — Slice 3D §15.4 fix: detect Ctrl modifier (bit 0 = Left
   * Ctrl, bit 4 = Right Ctrl per USB HID Usage Tables §10) to
   * translate alpha keys into control codes (Ctrl+A = 0x01, ...,
   * Ctrl+Z = 0x1A). This is what shell line editors expect for
   * Ctrl+C/D/L/etc. Non-alpha keys under Ctrl are passed through
   * unchanged. */
  int ctrl = (report->modifiers & 0x11) ? 1 : 0;
  /* Etapa 3 — Slice 3D §15.3: Caps Lock affects letter case
   * persistently. Read from latched LED state; toggled below on each
   * new press of the Caps Lock key. */
  int caps = (g_hid.led_state & USB_HID_LED_CAPS_LOCK) ? 1 : 0;
  int led_changed = 0;

  for (int i = 0; i < 6; i++) {
    uint8_t key = report->keys[i];
    if (key == 0) continue;

    /* Check if key was already pressed in previous report */
    int already = 0;
    for (int j = 0; j < 6; j++) {
      if (g_hid.prev_keys[j] == key) { already = 1; break; }
    }
    if (already) continue;

    /* Etapa 3 — Slice 3D §15.3: toggle latched LED state on lock key
     * press. The translation table maps these usages to 0, so they
     * never produce ASCII; we only update LEDs. */
    if (key == USB_HID_USAGE_CAPS_LOCK) {
      g_hid.led_state ^= USB_HID_LED_CAPS_LOCK;
      led_changed = 1;
      caps = (g_hid.led_state & USB_HID_LED_CAPS_LOCK) ? 1 : 0;
      continue;
    }
    if (key == USB_HID_USAGE_NUM_LOCK) {
      g_hid.led_state ^= USB_HID_LED_NUM_LOCK;
      led_changed = 1;
      continue;
    }
    if (key == USB_HID_USAGE_SCROLL_LOCK) {
      g_hid.led_state ^= USB_HID_LED_SCROLL_LOCK;
      led_changed = 1;
      continue;
    }

    /* New key press - translate to ASCII */
    if (key < 128) {
      char ch = shift ? hid_scancode_shift[key] : hid_scancode_to_ascii[key];
      /* Caps Lock swaps case for letters only (not symbols). */
      if (caps) {
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
        else if (ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32);
      }
      if (ctrl && ch >= 'a' && ch <= 'z') {
        ch = (char)(ch - 'a' + 1);
      } else if (ctrl && ch >= 'A' && ch <= 'Z') {
        ch = (char)(ch - 'A' + 1);
      }
      if (ch) kbd_buffer_push(ch);
    }
  }

  for (int i = 0; i < 6; i++) {
    g_hid.prev_keys[i] = report->keys[i];
  }
  if (led_changed) usb_hid_dispatch_led_report();
}

void usb_hid_handle_mouse_report(const struct usb_hid_mouse_report *report) {
  if (!report) return;
  g_hid.mouse_report = *report;
  g_hid.mouse_report_ready = 1;
}

int usb_hid_keyboard_available(void) {
  return g_hid.initialized && g_hid.kbd_slot >= 0;
}

int usb_hid_keyboard_poll(char *out_char) {
  if (!g_hid.initialized || g_hid.kbd_slot < 0 || !out_char) return 0;

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
  if (!g_hid.mouse_report_ready) {
    *dx = 0;
    *dy = 0;
    *dz = 0;
    *buttons = 0;
    return 0;
  }
  *dx = g_hid.mouse_report.dx;
  *dy = g_hid.mouse_report.dy;
  *dz = g_hid.mouse_report.dz;
  *buttons = g_hid.mouse_report.buttons;
  g_hid.mouse_report_ready = 0;
  return 1;
}
