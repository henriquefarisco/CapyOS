/* Etapa 3 — Slice 3D external validation gate.
 * Pure gate logic for USB_HID_KEYBOARD_SMOKE_MARKER emission.
 * No I/O, no allocation, no globals — host-testable. */
#include "drivers/usb/usb_hid_smoke.h"

#include <stddef.h>

void usb_hid_keyboard_smoke_state_reset(
    struct usb_hid_keyboard_smoke_state *state) {
  if (!state) return;
  state->configured_count = 0u;
  state->chars_received = 0u;
  state->emitted = 0u;
}

int usb_hid_keyboard_smoke_gate_observed(uint32_t configured_count,
                                         uint32_t chars_received) {
  return (configured_count >= 1u && chars_received >= 1u) ? 1 : 0;
}

int usb_hid_keyboard_smoke_observe(
    struct usb_hid_keyboard_smoke_state *state, uint32_t configured_count,
    uint32_t chars_received) {
  if (!state) return 0;
  state->configured_count = configured_count;
  state->chars_received = chars_received;
  if (state->emitted) return 0;
  if (!usb_hid_keyboard_smoke_gate_observed(configured_count, chars_received)) {
    return 0;
  }
  state->emitted = 1u;
  return 1;
}
