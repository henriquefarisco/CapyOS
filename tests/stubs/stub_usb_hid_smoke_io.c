/* Host-test stub for the USB HID keyboard smoke marker emitter.
 *
 * The kernel build links `src/drivers/usb/usb_hid_smoke_io.c`, which
 * writes the marker to COM1. Host tests cannot do hardware I/O, so we
 * provide a no-op implementation here. The gate logic is exercised by
 * `tests/drivers/test_usb_hid_smoke_gate.c` directly against
 * `src/drivers/usb/usb_hid_smoke.c`. */
#include "drivers/usb/usb_hid_smoke.h"

void usb_hid_keyboard_smoke_emit_marker(void) {
  /* intentionally empty in host tests */
}
