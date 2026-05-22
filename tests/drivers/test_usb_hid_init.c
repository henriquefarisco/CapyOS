/*
 * tests/test_usb_hid_init.c
 *
 * Slice 3A regression suite for src/drivers/usb/usb_hid.c.
 *
 * Goal: lock the contract that usb_hid_init only claims a device when
 * (a) the state machine reached ADDRESSED or CONFIGURED and (b) the class
 * code was populated. Earlier code required CONFIGURED unconditionally,
 * which meant ATTACHED-only devices (the only state usb_enumerate_devices
 * ever produced) were silently skipped.
 *
 * The tests use tests/stub_usb_core.c to expose a controllable device
 * table without touching xHCI MMIO.
 */

#include "drivers/usb/usb_core.h"
#include "drivers/usb/usb_hid.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Provided by tests/stubs/stub_usb_core.c. */
extern void stub_usb_core_reset(void);
extern int stub_usb_core_set_devices(const struct usb_device_info *src, int count);
extern int stub_usb_core_poll_call_count(void);
/* §15.3 LED stub counters. */
extern int stub_usb_hid_send_led_report_calls(void);
extern uint8_t stub_usb_hid_send_led_report_last_bitmap(void);
extern uint8_t stub_usb_hid_send_led_report_last_slot(void);
extern uint8_t stub_usb_hid_send_led_report_last_interface(void);

static int g_failures = 0;

static void fail(const char *msg) {
    printf("[usb-hid-init] FAIL: %s\n", msg);
    g_failures++;
}

static void zero_dev(struct usb_device_info *dev) {
    memset(dev, 0, sizeof(*dev));
}

static struct usb_device_info make_kbd_device(enum usb_device_state state,
                                              uint8_t class_code) {
    struct usb_device_info dev;
    zero_dev(&dev);
    dev.slot_id = 1;
    dev.port = 0;
    dev.state = state;
    dev.class_code = class_code;
    dev.subclass = USB_SUBCLASS_BOOT;
    dev.protocol = USB_PROTOCOL_KBD;
    dev.endpoint_count = 1;
    dev.endpoints[0].address = 0x81;
    dev.endpoints[0].type = 0x03; /* Interrupt */
    dev.endpoints[0].max_packet_size = 8;
    dev.endpoints[0].interval = 10;
    return dev;
}

static struct usb_device_info make_mouse_device(enum usb_device_state state,
                                                uint8_t class_code) {
    struct usb_device_info dev;
    zero_dev(&dev);
    dev.slot_id = 2;
    dev.port = 1;
    dev.state = state;
    dev.class_code = class_code;
    dev.subclass = USB_SUBCLASS_BOOT;
    dev.protocol = USB_PROTOCOL_MOUSE;
    dev.endpoint_count = 1;
    dev.endpoints[0].address = 0x82;
    dev.endpoints[0].type = 0x03;
    dev.endpoints[0].max_packet_size = 4;
    dev.endpoints[0].interval = 10;
    return dev;
}

/* Test 1: device stuck at ATTACHED without class info must NOT be claimed,
 * because the descriptors that populate class_code never ran. This is the
 * exact bug scenario the slice fixes for production. */
static void test_attached_without_class_is_ignored(void) {
    stub_usb_core_reset();
    struct usb_device_info devs[1];
    devs[0] = make_kbd_device(USB_DEV_ATTACHED, 0); /* class_code=0 */
    if (stub_usb_core_set_devices(devs, 1) != 0) {
        fail("stub_usb_core_set_devices rejected payload");
        return;
    }

    int rc = usb_hid_init();
    if (rc == 0) fail("init should fail when only attached/no-class device exists");
    if (usb_hid_keyboard_available()) fail("keyboard must not be available");
    if (usb_hid_mouse_available()) fail("mouse must not be available");
}

/* Test 2: a device in ADDRESSED state with class info populated must be
 * registered as a keyboard. This locks the fact that ADDRESSED + class
 * is now a sufficient gate. */
static void test_addressed_with_class_keyboard_is_found(void) {
    stub_usb_core_reset();
    struct usb_device_info devs[1];
    devs[0] = make_kbd_device(USB_DEV_ADDRESSED, USB_CLASS_HID);
    stub_usb_core_set_devices(devs, 1);

    int rc = usb_hid_init();
    if (rc != 0) fail("init should succeed for addressed HID keyboard");
    if (!usb_hid_keyboard_available()) fail("keyboard must be available after init");
    if (usb_hid_mouse_available()) fail("mouse must not be available (only kbd registered)");
}

/* Test 3: same as test 2 but for mouse. */
static void test_addressed_with_class_mouse_is_found(void) {
    stub_usb_core_reset();
    struct usb_device_info devs[1];
    devs[0] = make_mouse_device(USB_DEV_ADDRESSED, USB_CLASS_HID);
    stub_usb_core_set_devices(devs, 1);

    int rc = usb_hid_init();
    if (rc != 0) fail("init should succeed for addressed HID mouse");
    if (!usb_hid_mouse_available()) fail("mouse must be available after init");
    if (usb_hid_keyboard_available()) fail("keyboard must not be available (only mouse registered)");
}

/* Test 4: keyboard poll must be safe and return 0 when the host stub has no
 * real transfer pipeline to fill the ring buffer. The function
 * must not write garbage into *out_char and must not crash. */
static void test_keyboard_poll_safe_without_transfer(void) {
    stub_usb_core_reset();
    struct usb_device_info devs[1];
    devs[0] = make_kbd_device(USB_DEV_ADDRESSED, USB_CLASS_HID);
    stub_usb_core_set_devices(devs, 1);
    if (usb_hid_init() != 0) {
        fail("init prerequisite failed for poll test");
        return;
    }

    char out = 0x7F;
    int produced = usb_hid_keyboard_poll(&out);
    if (produced != 0) fail("poll must report zero characters without ring data");
    /* poll triggers usb_poll_all under the hood; stub records the call. */
    if (stub_usb_core_poll_call_count() < 1) fail("poll must consult usb_poll_all");
}

/* Test 5: CONFIGURED + class still works (we kept backward compatibility
 * for the future when slice 3C lands and devices reach CONFIGURED). */
static void test_configured_keyboard_still_works(void) {
    stub_usb_core_reset();
    struct usb_device_info devs[1];
    devs[0] = make_kbd_device(USB_DEV_CONFIGURED, USB_CLASS_HID);
    stub_usb_core_set_devices(devs, 1);

    int rc = usb_hid_init();
    if (rc != 0) fail("init must keep accepting CONFIGURED + class devices");
    if (!usb_hid_keyboard_available()) fail("CONFIGURED keyboard must be registered");
}

/* Test 6: mixed table must pick the first keyboard and the first mouse,
 * skipping junk entries with state=ATTACHED. */
static void test_mixed_table_picks_correctly(void) {
    stub_usb_core_reset();
    struct usb_device_info devs[3];
    devs[0] = make_kbd_device(USB_DEV_ATTACHED, 0);                 /* junk */
    devs[1] = make_kbd_device(USB_DEV_ADDRESSED, USB_CLASS_HID);    /* keep */
    devs[2] = make_mouse_device(USB_DEV_ADDRESSED, USB_CLASS_HID);  /* keep */
    stub_usb_core_set_devices(devs, 3);

    int rc = usb_hid_init();
    if (rc != 0) fail("mixed table init must succeed when at least one HID is valid");
    if (!usb_hid_keyboard_available()) fail("mixed table must surface a keyboard");
    if (!usb_hid_mouse_available()) fail("mixed table must surface a mouse");
}

static void test_keyboard_report_handler_buffers_ascii(void) {
    stub_usb_core_reset();
    struct usb_device_info devs[1];
    struct usb_hid_keyboard_report report;
    devs[0] = make_kbd_device(USB_DEV_CONFIGURED, USB_CLASS_HID);
    stub_usb_core_set_devices(devs, 1);
    if (usb_hid_init() != 0) {
        fail("keyboard report test init failed");
        return;
    }
    memset(&report, 0, sizeof(report));
    report.keys[0] = 4u;
    usb_hid_handle_keyboard_report(&report);
    char out = 0;
    if (usb_hid_keyboard_poll(&out) != 1) fail("keyboard report must produce one char");
    if (out != 'a') fail("keyboard usage 4 must translate to 'a'");
}

static void test_mouse_report_handler_surfaces_delta(void) {
    stub_usb_core_reset();
    struct usb_device_info devs[1];
    struct usb_hid_mouse_report report;
    int8_t dx = 0;
    int8_t dy = 0;
    int8_t dz = 0;
    uint8_t buttons = 0;
    devs[0] = make_mouse_device(USB_DEV_CONFIGURED, USB_CLASS_HID);
    stub_usb_core_set_devices(devs, 1);
    if (usb_hid_init() != 0) {
        fail("mouse report test init failed");
        return;
    }
    report.buttons = 1u;
    report.dx = 3;
    report.dy = -2;
    report.dz = 1;
    usb_hid_handle_mouse_report(&report);
    if (usb_hid_mouse_poll(&dx, &dy, &dz, &buttons) != 1) {
        fail("mouse report must produce one packet");
    }
    if (buttons != 1u || dx != 3 || dy != -2 || dz != 1) {
        fail("mouse poll must surface latest report");
    }
}

/* Etapa 3 — Slice 3D §15.4 hardening: Ctrl+alpha must translate to
 * the corresponding control code so shell line editors receive
 * Ctrl+C / Ctrl+D / Ctrl+L correctly. Non-alpha keys under Ctrl
 * must pass through unchanged (no spurious mutation). */
static void test_keyboard_report_handler_translates_ctrl_combinations(void) {
    stub_usb_core_reset();
    struct usb_device_info devs[1];
    struct usb_hid_keyboard_report report;
    char out = 0;
    devs[0] = make_kbd_device(USB_DEV_CONFIGURED, USB_CLASS_HID);
    stub_usb_core_set_devices(devs, 1);
    if (usb_hid_init() != 0) {
        fail("ctrl combinations test init failed");
        return;
    }
    /* Left Ctrl + 'a' (usage 4) → 0x01 */
    memset(&report, 0, sizeof(report));
    report.modifiers = 0x01u;
    report.keys[0] = 4u;
    usb_hid_handle_keyboard_report(&report);
    out = 0;
    if (usb_hid_keyboard_poll(&out) != 1 || out != 0x01) {
        fail("Ctrl+A must translate to 0x01");
    }
    /* Release. Required so the next press isn't deduplicated by
     * prev_keys. */
    memset(&report, 0, sizeof(report));
    usb_hid_handle_keyboard_report(&report);
    /* Right Ctrl + 'c' (usage 6) → 0x03 (ETX, Ctrl+C) */
    memset(&report, 0, sizeof(report));
    report.modifiers = 0x10u;
    report.keys[0] = 6u;
    usb_hid_handle_keyboard_report(&report);
    out = 0;
    if (usb_hid_keyboard_poll(&out) != 1 || out != 0x03) {
        fail("Right-Ctrl+C must translate to 0x03 (ETX)");
    }
    memset(&report, 0, sizeof(report));
    usb_hid_handle_keyboard_report(&report);
    /* Ctrl+Shift+A: shift turns 'a' into 'A', then ctrl maps to 0x01. */
    memset(&report, 0, sizeof(report));
    report.modifiers = 0x01u | 0x02u;
    report.keys[0] = 4u;
    usb_hid_handle_keyboard_report(&report);
    out = 0;
    if (usb_hid_keyboard_poll(&out) != 1 || out != 0x01) {
        fail("Ctrl+Shift+A must still translate to 0x01");
    }
}

static void test_keyboard_report_handler_passes_ctrl_with_non_alpha(void) {
    stub_usb_core_reset();
    struct usb_device_info devs[1];
    struct usb_hid_keyboard_report report;
    char out = 0;
    devs[0] = make_kbd_device(USB_DEV_CONFIGURED, USB_CLASS_HID);
    stub_usb_core_set_devices(devs, 1);
    if (usb_hid_init() != 0) {
        fail("ctrl non-alpha test init failed");
        return;
    }
    /* Ctrl + '1' (usage 30) — '1' is not alpha; Ctrl must be ignored
     * in the translation, char must pass through unchanged. */
    memset(&report, 0, sizeof(report));
    report.modifiers = 0x01u;
    report.keys[0] = 30u;
    usb_hid_handle_keyboard_report(&report);
    out = 0;
    if (usb_hid_keyboard_poll(&out) != 1 || out != '1') {
        fail("Ctrl+1 must emit '1' unchanged (non-alpha)");
    }
    memset(&report, 0, sizeof(report));
    usb_hid_handle_keyboard_report(&report);
    /* Ctrl + Space (usage 0x2C) — also non-alpha. */
    memset(&report, 0, sizeof(report));
    report.modifiers = 0x01u;
    report.keys[0] = 0x2Cu;
    usb_hid_handle_keyboard_report(&report);
    out = 0;
    if (usb_hid_keyboard_poll(&out) != 1 || out != ' ') {
        fail("Ctrl+Space must emit ' ' unchanged");
    }
}

/* Etapa 3 — Slice 3D §15.3 hardening: keyboard LED feedback.
 * Caps Lock / Num Lock / Scroll Lock presses must toggle the latched
 * led_state, dispatch a SET_REPORT to the keyboard, and (for Caps
 * Lock) affect subsequent letter case translation. */
static void test_keyboard_report_handler_toggles_caps_lock_led(void) {
    stub_usb_core_reset();
    struct usb_device_info devs[1];
    struct usb_hid_keyboard_report report;
    devs[0] = make_kbd_device(USB_DEV_CONFIGURED, USB_CLASS_HID);
    /* Assign a deterministic interface number for the LED test. */
    devs[0].interface_number = 7u;
    stub_usb_core_set_devices(devs, 1);
    if (usb_hid_init() != 0) {
        fail("caps lock test init failed");
        return;
    }
    /* Press Caps Lock (usage 0x39). */
    memset(&report, 0, sizeof(report));
    report.keys[0] = 0x39u;
    usb_hid_handle_keyboard_report(&report);
    if (stub_usb_hid_send_led_report_calls() != 1) {
        fail("caps lock press must dispatch one SET_REPORT");
    }
    if (stub_usb_hid_send_led_report_last_bitmap() != 0x02u) {
        fail("LED bitmap must have CapsLock bit set after first press");
    }
    if (stub_usb_hid_send_led_report_last_slot() != 1u) {
        fail("SET_REPORT must use captured slot_id");
    }
    if (stub_usb_hid_send_led_report_last_interface() != 7u) {
        fail("SET_REPORT must use captured interface number");
    }
    /* Release Caps Lock. */
    memset(&report, 0, sizeof(report));
    usb_hid_handle_keyboard_report(&report);
    if (stub_usb_hid_send_led_report_calls() != 1) {
        fail("release alone must not dispatch another SET_REPORT");
    }
    /* Press Caps Lock again — toggles off. */
    memset(&report, 0, sizeof(report));
    report.keys[0] = 0x39u;
    usb_hid_handle_keyboard_report(&report);
    if (stub_usb_hid_send_led_report_calls() != 2) {
        fail("second caps lock press must dispatch SET_REPORT");
    }
    if (stub_usb_hid_send_led_report_last_bitmap() != 0x00u) {
        fail("LED bitmap must clear CapsLock after second press");
    }
}

static void test_keyboard_report_handler_caps_lock_affects_letters(void) {
    stub_usb_core_reset();
    struct usb_device_info devs[1];
    struct usb_hid_keyboard_report report;
    char out = 0;
    devs[0] = make_kbd_device(USB_DEV_CONFIGURED, USB_CLASS_HID);
    stub_usb_core_set_devices(devs, 1);
    if (usb_hid_init() != 0) {
        fail("caps lock letters test init failed");
        return;
    }
    /* Type 'a' before Caps Lock — should be lowercase. */
    memset(&report, 0, sizeof(report));
    report.keys[0] = 4u;
    usb_hid_handle_keyboard_report(&report);
    out = 0;
    if (usb_hid_keyboard_poll(&out) != 1 || out != 'a') {
        fail("pre-CapsLock 'a' must be lowercase");
    }
    /* Release. */
    memset(&report, 0, sizeof(report));
    usb_hid_handle_keyboard_report(&report);
    /* Press Caps Lock to enable. */
    memset(&report, 0, sizeof(report));
    report.keys[0] = 0x39u;
    usb_hid_handle_keyboard_report(&report);
    /* Release. */
    memset(&report, 0, sizeof(report));
    usb_hid_handle_keyboard_report(&report);
    /* Type 'a' with caps lock on — must be uppercase 'A'. */
    memset(&report, 0, sizeof(report));
    report.keys[0] = 4u;
    usb_hid_handle_keyboard_report(&report);
    out = 0;
    if (usb_hid_keyboard_poll(&out) != 1 || out != 'A') {
        fail("CapsLock-on 'a' must produce 'A'");
    }
    memset(&report, 0, sizeof(report));
    usb_hid_handle_keyboard_report(&report);
    /* Shift+'a' with caps lock — Shift inverts caps, must be 'a'. */
    memset(&report, 0, sizeof(report));
    report.modifiers = 0x02u; /* Left Shift */
    report.keys[0] = 4u;
    usb_hid_handle_keyboard_report(&report);
    out = 0;
    if (usb_hid_keyboard_poll(&out) != 1 || out != 'a') {
        fail("Shift+'a' under CapsLock must invert to 'a'");
    }
    memset(&report, 0, sizeof(report));
    usb_hid_handle_keyboard_report(&report);
    /* '1' with caps lock should stay '1' (not '!'). Symbols not affected. */
    memset(&report, 0, sizeof(report));
    report.keys[0] = 30u;
    usb_hid_handle_keyboard_report(&report);
    out = 0;
    if (usb_hid_keyboard_poll(&out) != 1 || out != '1') {
        fail("CapsLock must not affect non-letter '1'");
    }
}

int run_usb_hid_init_tests(void) {
    g_failures = 0;
    test_attached_without_class_is_ignored();
    test_addressed_with_class_keyboard_is_found();
    test_addressed_with_class_mouse_is_found();
    test_keyboard_poll_safe_without_transfer();
    test_configured_keyboard_still_works();
    test_mixed_table_picks_correctly();
    test_keyboard_report_handler_buffers_ascii();
    test_mouse_report_handler_surfaces_delta();
    test_keyboard_report_handler_translates_ctrl_combinations();
    test_keyboard_report_handler_passes_ctrl_with_non_alpha();
    test_keyboard_report_handler_toggles_caps_lock_led();
    test_keyboard_report_handler_caps_lock_affects_letters();
    if (g_failures == 0) printf("[tests] usb_hid_init OK\n");
    return g_failures;
}
