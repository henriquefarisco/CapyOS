/*
 * Host-side stub for src/drivers/usb/usb_core.c.
 *
 * The real usb_core.c reaches xHCI MMIO during init/enumerate, which the
 * host build cannot exercise. Slice 3A only needs to verify how usb_hid.c
 * filters devices returned by the core, so this stub exposes a controllable
 * device table plus the pure class-identification helpers reproduced from
 * the production translation unit.
 *
 * Tests populate the table via stub_usb_core_set_devices and never trigger
 * usb_poll_all/usb_hotplug_check, which remain inert no-ops here.
 */
#include "drivers/usb/usb_core.h"

#include <stddef.h>
#include <stdint.h>

#define STUB_USB_MAX_DEVICES USB_MAX_DEVICES

static struct usb_device_info g_stub_devices[STUB_USB_MAX_DEVICES];
static int g_stub_device_count = 0;
static int g_stub_poll_calls = 0;
static int g_stub_hotplug_calls = 0;

void stub_usb_core_reset(void) {
    g_stub_device_count = 0;
    g_stub_poll_calls = 0;
    g_stub_hotplug_calls = 0;
    for (int i = 0; i < STUB_USB_MAX_DEVICES; i++) {
        uint8_t *raw = (uint8_t *)&g_stub_devices[i];
        for (size_t j = 0; j < sizeof(g_stub_devices[i]); j++) raw[j] = 0;
    }
}

int stub_usb_core_set_devices(const struct usb_device_info *src, int count) {
    if (count < 0 || count > STUB_USB_MAX_DEVICES) return -1;
    if (count > 0 && !src) return -1;
    g_stub_device_count = count;
    for (int i = 0; i < count; i++) g_stub_devices[i] = src[i];
    for (int i = count; i < STUB_USB_MAX_DEVICES; i++) {
        uint8_t *raw = (uint8_t *)&g_stub_devices[i];
        for (size_t j = 0; j < sizeof(g_stub_devices[i]); j++) raw[j] = 0;
    }
    return 0;
}

int stub_usb_core_poll_call_count(void) { return g_stub_poll_calls; }
int stub_usb_core_hotplug_call_count(void) { return g_stub_hotplug_calls; }

/* --- Symbols consumed by src/drivers/usb/usb_hid.c --- */

void usb_core_init(void) {
    /* No real hardware in host tests; init is a no-op. Tests use
     * stub_usb_core_set_devices to populate the table instead. */
}

int usb_enumerate_devices(void) { return g_stub_device_count; }

int usb_get_device_count(void) { return g_stub_device_count; }

int usb_get_device(int index, struct usb_device_info *out) {
    if (index < 0 || index >= g_stub_device_count || !out) return -1;
    *out = g_stub_devices[index];
    return 0;
}

int usb_device_is_hid_keyboard(const struct usb_device_info *dev) {
    if (!dev) return 0;
    return (dev->class_code == USB_CLASS_HID &&
            dev->subclass == USB_SUBCLASS_BOOT &&
            dev->protocol == USB_PROTOCOL_KBD) ? 1 : dev->is_keyboard;
}

int usb_device_is_hid_mouse(const struct usb_device_info *dev) {
    if (!dev) return 0;
    return (dev->class_code == USB_CLASS_HID &&
            dev->subclass == USB_SUBCLASS_BOOT &&
            dev->protocol == USB_PROTOCOL_MOUSE) ? 1 : dev->is_mouse;
}

void usb_poll_all(void) {
    g_stub_poll_calls++;
}

void usb_hotplug_check(void) {
    g_stub_hotplug_calls++;
}
