#include "drivers/usb/usb_core.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

static void fail(const char *msg) {
    printf("[usb-descriptor-parse] FAIL: %s\n", msg);
    g_failures++;
}

static void zero_dev(struct usb_device_info *dev) {
    memset(dev, 0, sizeof(*dev));
}

static void test_parse_device_descriptor_minimal(void) {
    static const uint8_t desc[18] = {
        18, USB_DESC_TYPE_DEVICE, 0x00, 0x02,
        0, 0, 0, 64,
        0x34, 0x12, 0x78, 0x56,
        0x00, 0x01, 1, 2, 3, 1
    };
    struct usb_device_descriptor out;
    memset(&out, 0, sizeof(out));
    if (usb_parse_device_descriptor(desc, sizeof(desc), &out) != 0) {
        fail("valid device descriptor must parse");
        return;
    }
    if (out.bcdUSB != 0x0200u) fail("device bcdUSB must be little-endian");
    if (out.bMaxPacketSize0 != 64u) fail("device max packet size must parse");
    if (out.idVendor != 0x1234u) fail("device vendor id must parse little-endian");
    if (out.idProduct != 0x5678u) fail("device product id must parse little-endian");
    if (out.bNumConfigurations != 1u) fail("device configuration count must parse");
}

static void test_build_get_descriptor_request(void) {
    struct usb_setup_packet setup;
    memset(&setup, 0, sizeof(setup));
    if (usb_build_get_descriptor_request(USB_DESC_TYPE_DEVICE, 0, 0, 18, &setup) != 0) {
        fail("GET_DESCRIPTOR setup builder must accept valid input");
        return;
    }
    if (setup.bmRequestType != 0x80u) fail("GET_DESCRIPTOR request type must be device-to-host standard device");
    if (setup.bRequest != USB_REQ_GET_DESCRIPTOR) fail("GET_DESCRIPTOR request id must be encoded");
    if (setup.wValue != ((uint16_t)USB_DESC_TYPE_DEVICE << 8)) fail("GET_DESCRIPTOR wValue must encode type/index");
    if (setup.wIndex != 0u) fail("GET_DESCRIPTOR wIndex must be caller-supplied");
    if (setup.wLength != 18u) fail("GET_DESCRIPTOR wLength must be caller-supplied");
    if (usb_build_get_descriptor_request(USB_DESC_TYPE_DEVICE, 0, 0, 0, &setup) == 0) {
        fail("GET_DESCRIPTOR builder must reject zero length");
    }
    if (usb_build_get_descriptor_request(USB_DESC_TYPE_DEVICE, 0, 0, 18, NULL) == 0) {
        fail("GET_DESCRIPTOR builder must reject null output");
    }
}

static void test_parse_config_descriptor_keyboard(void) {
    static const uint8_t desc[] = {
        9, USB_DESC_TYPE_CONFIGURATION, 34, 0, 1, 1, 0, 0xA0, 50,
        9, USB_DESC_TYPE_INTERFACE, 0, 0, 1, USB_CLASS_HID, USB_SUBCLASS_BOOT, USB_PROTOCOL_KBD, 0,
        9, 0x21, 0x11, 0x01, 0, 1, 0x22, 63, 0,
        7, USB_DESC_TYPE_ENDPOINT, 0x81, 0x03, 8, 0, 10
    };
    struct usb_device_info dev;
    zero_dev(&dev);
    if (usb_parse_configuration_descriptor(desc, sizeof(desc), &dev) != 0) {
        fail("keyboard configuration descriptor must parse");
        return;
    }
    if (dev.class_code != USB_CLASS_HID) fail("keyboard class must be HID");
    if (dev.subclass != USB_SUBCLASS_BOOT) fail("keyboard subclass must be boot");
    if (dev.protocol != USB_PROTOCOL_KBD) fail("keyboard protocol must be keyboard");
    if (dev.configuration_value != 1u) fail("keyboard configuration value must parse");
    if (dev.interface_number != 0u) fail("keyboard interface number must parse");
    if (!dev.is_keyboard) fail("keyboard flag must be set");
    if (dev.is_mouse) fail("mouse flag must not be set for keyboard");
    if (dev.endpoint_count != 1u) fail("keyboard endpoint count must be one");
    if (dev.endpoints[0].address != 0x81u) fail("keyboard endpoint address must parse");
    if (dev.endpoints[0].type != 3u) fail("keyboard endpoint type must be interrupt");
    if (dev.endpoints[0].max_packet_size != 8u) fail("keyboard endpoint mps must parse");
    if (dev.endpoints[0].interval != 10u) fail("keyboard endpoint interval must parse");
}

static void test_parse_config_descriptor_mouse(void) {
    static const uint8_t desc[] = {
        9, USB_DESC_TYPE_CONFIGURATION, 25, 0, 1, 1, 0, 0x80, 50,
        9, USB_DESC_TYPE_INTERFACE, 0, 0, 1, USB_CLASS_HID, USB_SUBCLASS_BOOT, USB_PROTOCOL_MOUSE, 0,
        7, USB_DESC_TYPE_ENDPOINT, 0x82, 0x03, 4, 0, 8
    };
    struct usb_device_info dev;
    zero_dev(&dev);
    if (usb_parse_configuration_descriptor(desc, sizeof(desc), &dev) != 0) {
        fail("mouse configuration descriptor must parse");
        return;
    }
    if (dev.class_code != USB_CLASS_HID) fail("mouse class must be HID");
    if (dev.protocol != USB_PROTOCOL_MOUSE) fail("mouse protocol must be mouse");
    if (!dev.is_mouse) fail("mouse flag must be set");
    if (dev.is_keyboard) fail("keyboard flag must not be set for mouse");
    if (dev.endpoint_count != 1u) fail("mouse endpoint count must be one");
    if (dev.endpoints[0].address != 0x82u) fail("mouse endpoint address must parse");
    if (dev.endpoints[0].max_packet_size != 4u) fail("mouse endpoint mps must parse");
}

static void test_parse_truncated_descriptor_rejected(void) {
    static const uint8_t desc[] = {
        9, USB_DESC_TYPE_CONFIGURATION, 25, 0, 1, 1, 0, 0x80, 50,
        9, USB_DESC_TYPE_INTERFACE, 0, 0, 1, USB_CLASS_HID, USB_SUBCLASS_BOOT
    };
    struct usb_device_info dev;
    zero_dev(&dev);
    dev.class_code = USB_CLASS_STORAGE;
    dev.configuration_value = 7u;
    dev.interface_number = 3u;
    dev.endpoint_count = 1;
    dev.endpoints[0].address = 0x02;
    if (usb_parse_configuration_descriptor(desc, sizeof(desc), &dev) == 0) {
        fail("truncated configuration descriptor must be rejected");
    }
    if (dev.class_code != USB_CLASS_STORAGE) fail("rejected config parse must preserve class");
    if (dev.configuration_value != 7u) fail("rejected config parse must preserve configuration value");
    if (dev.interface_number != 3u) fail("rejected config parse must preserve interface number");
    if (dev.endpoint_count != 1u) fail("rejected config parse must preserve endpoints");
    if (dev.endpoints[0].address != 0x02u) fail("rejected config parse must preserve endpoint address");
}

static void test_parse_too_many_endpoints_clipped(void) {
    uint8_t desc[9 + 9 + (USB_MAX_ENDPOINTS + 2u) * 7u];
    struct usb_device_info dev;
    size_t off = 0;
    memset(desc, 0, sizeof(desc));
    desc[off++] = 9; desc[off++] = USB_DESC_TYPE_CONFIGURATION;
    desc[off++] = (uint8_t)sizeof(desc); desc[off++] = (uint8_t)(sizeof(desc) >> 8);
    desc[off++] = 1; desc[off++] = 1; desc[off++] = 0; desc[off++] = 0x80; desc[off++] = 50;
    desc[off++] = 9; desc[off++] = USB_DESC_TYPE_INTERFACE;
    desc[off++] = 0; desc[off++] = 0; desc[off++] = USB_MAX_ENDPOINTS + 2u;
    desc[off++] = USB_CLASS_HID; desc[off++] = USB_SUBCLASS_BOOT; desc[off++] = USB_PROTOCOL_KBD; desc[off++] = 0;
    for (uint8_t i = 0; i < USB_MAX_ENDPOINTS + 2u; i++) {
        desc[off++] = 7;
        desc[off++] = USB_DESC_TYPE_ENDPOINT;
        desc[off++] = (uint8_t)(0x81u + i);
        desc[off++] = 0x03;
        desc[off++] = 8;
        desc[off++] = 0;
        desc[off++] = 10;
    }
    zero_dev(&dev);
    if (usb_parse_configuration_descriptor(desc, sizeof(desc), &dev) != 0) {
        fail("many-endpoint descriptor must parse");
        return;
    }
    if (dev.endpoint_count != USB_MAX_ENDPOINTS) {
        fail("endpoint count must be clipped to USB_MAX_ENDPOINTS");
    }
    if (dev.endpoints[USB_MAX_ENDPOINTS - 1u].address != (uint8_t)(0x81u + USB_MAX_ENDPOINTS - 1u)) {
        fail("last retained endpoint must be the max clipped endpoint");
    }
}

static void test_parse_composite_prefers_hid_interface_endpoints(void) {
    static const uint8_t desc[] = {
        9, USB_DESC_TYPE_CONFIGURATION, 41, 0, 2, 1, 0, 0x80, 50,
        9, USB_DESC_TYPE_INTERFACE, 0, 0, 1, USB_CLASS_STORAGE, 6, 80, 0,
        7, USB_DESC_TYPE_ENDPOINT, 0x02, 0x02, 64, 0, 0,
        9, USB_DESC_TYPE_INTERFACE, 1, 0, 1, USB_CLASS_HID, USB_SUBCLASS_BOOT, USB_PROTOCOL_KBD, 0,
        7, USB_DESC_TYPE_ENDPOINT, 0x83, 0x03, 8, 0, 10
    };
    struct usb_device_info dev;
    zero_dev(&dev);
    if (usb_parse_configuration_descriptor(desc, sizeof(desc), &dev) != 0) {
        fail("composite descriptor must parse");
        return;
    }
    if (dev.class_code != USB_CLASS_HID) fail("composite parser must prefer HID interface");
    if (dev.interface_number != 1u) fail("composite parser must record HID interface number");
    if (!dev.is_keyboard) fail("composite HID keyboard flag must be set");
    if (dev.endpoint_count != 1u) fail("composite parser must keep HID endpoints only");
    if (dev.endpoints[0].address != 0x83u) fail("composite parser must discard previous interface endpoint");
}

static void test_invalid_device_descriptor_rejected(void) {
    static const uint8_t bad_type[18] = {
        18, USB_DESC_TYPE_CONFIGURATION, 0, 0, 0, 0, 0, 8,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    struct usb_device_descriptor out;
    memset(&out, 0, sizeof(out));
    if (usb_parse_device_descriptor(bad_type, sizeof(bad_type), &out) == 0) {
        fail("wrong descriptor type must be rejected");
    }
    if (usb_parse_device_descriptor(bad_type, 8u, &out) == 0) {
        fail("short device descriptor must be rejected");
    }
}

int run_usb_descriptor_parse_tests(void) {
    g_failures = 0;
    test_parse_device_descriptor_minimal();
    test_build_get_descriptor_request();
    test_parse_config_descriptor_keyboard();
    test_parse_config_descriptor_mouse();
    test_parse_truncated_descriptor_rejected();
    test_parse_too_many_endpoints_clipped();
    test_parse_composite_prefers_hid_interface_endpoints();
    test_invalid_device_descriptor_rejected();
    if (g_failures == 0) printf("[tests] usb_descriptor_parse OK\n");
    return g_failures;
}
