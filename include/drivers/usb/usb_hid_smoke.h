#ifndef DRIVERS_USB_USB_HID_SMOKE_H
#define DRIVERS_USB_USB_HID_SMOKE_H
/* Etapa 3 — Slice 3D external validation gate.
 *
 * Deterministic readiness gate for the `smoke-x64-vmware-usb-hid-keyboard`
 * external smoke target. The smoke harness watches the COM1 serial console
 * for the canonical marker; the kernel emits it once, only after the full
 * USB HID keyboard path has been exercised end-to-end:
 *
 *   1. >=1 USB HID boot-protocol keyboard reaches USB_DEV_CONFIGURED
 *      (SET_CONFIGURATION + HID SET_PROTOCOL(BOOT) + Configure Endpoint OK).
 *   2. >=1 ASCII character has been buffered from a real interrupt
 *      transfer report via usb_hid_handle_keyboard_report -> kbd buffer.
 *
 * Both halves must be observed by the runtime before the marker fires.
 * The state is latched so that subsequent transitions never re-emit, and
 * the gate is pure / host-testable.
 *
 * This module owns ONLY the gate logic. Emission to COM1 is wired in
 * `src/drivers/usb/usb_hid.c` next to the keyboard report handler.
 */
#include <stdint.h>

#define USB_HID_KEYBOARD_SMOKE_GATE_VERSION 1
#define USB_HID_KEYBOARD_SMOKE_MARKER "[smoke] usb-hid-keyboard ready"

struct usb_hid_keyboard_smoke_state {
  uint32_t configured_count;
  uint32_t chars_received;
  uint8_t emitted;
};

void usb_hid_keyboard_smoke_state_reset(struct usb_hid_keyboard_smoke_state *state);

/* Pure gate predicate. Returns 1 iff both halves of the readiness
 * contract are satisfied. Safe to call on an uninitialized snapshot. */
int usb_hid_keyboard_smoke_gate_observed(uint32_t configured_count,
                                         uint32_t chars_received);

/* Composed transition checker. Updates the running counters into the
 * provided state and returns 1 exactly once: the first call in which the
 * gate transitions from blocked to observed AND the latch is still
 * cleared. Latches `emitted = 1` after returning 1, so subsequent calls
 * return 0 regardless of further updates. Returns 0 on NULL state. */
int usb_hid_keyboard_smoke_observe(struct usb_hid_keyboard_smoke_state *state,
                                   uint32_t configured_count,
                                   uint32_t chars_received);

/* Emits USB_HID_KEYBOARD_SMOKE_MARKER followed by '\n' on the COM1
 * serial console. Kernel build implements this in
 * `src/drivers/usb/usb_hid_smoke_io.c` (calls com1_puts); host test
 * builds link the stub at `tests/stubs/stub_usb_hid_smoke_io.c`.
 *
 * Callers must guard each invocation with
 * `usb_hid_keyboard_smoke_observe` so that the marker is emitted
 * exactly once per boot. The function performs no internal latching. */
void usb_hid_keyboard_smoke_emit_marker(void);

#endif /* DRIVERS_USB_USB_HID_SMOKE_H */
