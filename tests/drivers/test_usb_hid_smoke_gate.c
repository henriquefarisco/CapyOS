/*
 * tests/drivers/test_usb_hid_smoke_gate.c
 *
 * Etapa 3 — Slice 3D external validation gate.
 *
 * Locks the contract of `usb_hid_keyboard_smoke_observe`:
 *   - both halves (configured >= 1 AND chars >= 1) are required;
 *   - the latch fires exactly once on the first observed transition;
 *   - subsequent updates never re-trigger emission, even if the
 *     counters keep growing;
 *   - NULL state is handled defensively without UB.
 *
 * The gate logic is pure; this suite never touches COM1 or hardware.
 */

#include "drivers/usb/usb_hid_smoke.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

static void fail(const char *msg) {
  printf("[usb-hid-smoke-gate] FAIL: %s\n", msg);
  g_failures++;
}

static void test_gate_blocked_when_either_half_missing(void) {
  if (usb_hid_keyboard_smoke_gate_observed(0u, 0u) != 0) {
    fail("0 configured / 0 chars must block the gate");
  }
  if (usb_hid_keyboard_smoke_gate_observed(1u, 0u) != 0) {
    fail("configured keyboard without chars must block the gate");
  }
  if (usb_hid_keyboard_smoke_gate_observed(0u, 1u) != 0) {
    fail("chars without a configured keyboard must block the gate");
  }
}

static void test_gate_observes_when_both_halves_present(void) {
  if (usb_hid_keyboard_smoke_gate_observed(1u, 1u) != 1) {
    fail("1 configured + 1 char must observe the gate");
  }
  if (usb_hid_keyboard_smoke_gate_observed(4u, 10u) != 1) {
    fail("larger counts must also observe the gate");
  }
  if (usb_hid_keyboard_smoke_gate_observed(0xFFFFFFFFu, 0xFFFFFFFFu) != 1) {
    fail("saturated counts must observe the gate without overflow");
  }
}

static void test_observe_latches_after_first_transition(void) {
  struct usb_hid_keyboard_smoke_state state;
  usb_hid_keyboard_smoke_state_reset(&state);
  if (state.configured_count != 0u || state.chars_received != 0u ||
      state.emitted != 0u) {
    fail("reset must zero all fields");
    return;
  }
  /* Pre-transition observations must not latch. */
  if (usb_hid_keyboard_smoke_observe(&state, 0u, 0u) != 0) {
    fail("blocked observe must not emit");
  }
  if (usb_hid_keyboard_smoke_observe(&state, 1u, 0u) != 0) {
    fail("partial observe (no chars) must not emit");
  }
  if (state.emitted != 0u) {
    fail("latch must remain cleared until both halves observed");
  }
  /* First transition emits exactly once. */
  if (usb_hid_keyboard_smoke_observe(&state, 1u, 1u) != 1) {
    fail("first full observe must emit");
  }
  if (state.emitted != 1u) {
    fail("emitted flag must latch after first emission");
  }
  /* Subsequent observations are silent regardless of counter growth. */
  if (usb_hid_keyboard_smoke_observe(&state, 1u, 2u) != 0) {
    fail("re-observe must not re-emit");
  }
  if (usb_hid_keyboard_smoke_observe(&state, 5u, 99u) != 0) {
    fail("higher counts must not re-emit");
  }
}

static void test_observe_updates_counters_even_when_blocked(void) {
  struct usb_hid_keyboard_smoke_state state;
  usb_hid_keyboard_smoke_state_reset(&state);
  (void)usb_hid_keyboard_smoke_observe(&state, 0u, 7u);
  if (state.chars_received != 7u || state.configured_count != 0u) {
    fail("blocked observe must still record counters");
  }
  (void)usb_hid_keyboard_smoke_observe(&state, 2u, 0u);
  if (state.configured_count != 2u) {
    fail("blocked observe must update configured count");
  }
  if (state.chars_received != 0u) {
    fail("blocked observe must replace chars snapshot");
  }
  if (state.emitted != 0u) {
    fail("blocked observe must not latch");
  }
}

static void test_observe_handles_null_state(void) {
  if (usb_hid_keyboard_smoke_observe(NULL, 1u, 1u) != 0) {
    fail("NULL state must return 0 without UB");
  }
  /* Defensive: reset must not crash on NULL. */
  usb_hid_keyboard_smoke_state_reset(NULL);
}

static void test_marker_string_is_stable(void) {
  /* Lock the literal — the smoke harness depends on this exact text. */
  if (strcmp(USB_HID_KEYBOARD_SMOKE_MARKER,
             "[smoke] usb-hid-keyboard ready") != 0) {
    fail("USB_HID_KEYBOARD_SMOKE_MARKER must match the harness contract");
  }
  if (USB_HID_KEYBOARD_SMOKE_GATE_VERSION != 1) {
    fail("USB_HID_KEYBOARD_SMOKE_GATE_VERSION must be 1");
  }
}

int run_usb_hid_smoke_gate_tests(void) {
  g_failures = 0;
  test_gate_blocked_when_either_half_missing();
  test_gate_observes_when_both_halves_present();
  test_observe_latches_after_first_transition();
  test_observe_updates_counters_even_when_blocked();
  test_observe_handles_null_state();
  test_marker_string_is_stable();
  if (g_failures == 0) printf("[tests] usb_hid_smoke_gate OK\n");
  return g_failures;
}
