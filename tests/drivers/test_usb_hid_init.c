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

/* Provided by tests/stub_usb_core.c. */
extern void stub_usb_core_reset(void);
extern int stub_usb_core_set_devices(const struct usb_device_info *src, int count);
extern int stub_usb_core_poll_call_count(void);

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
    if (g_failures == 0) printf("[tests] usb_hid_init OK\n");
    return g_failures;
}
