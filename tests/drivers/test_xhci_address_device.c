#include "drivers/usb/xhci.h"
#include "drivers/usb/usb_core.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

static void fail(const char *msg) {
    printf("[xhci-address-device] FAIL: %s\n", msg);
    g_failures++;
}

static uint32_t *ctx_dwords(void *base, uint32_t context_size, uint32_t index) {
    return (uint32_t *)((uint8_t *)base + (uint64_t)context_size * index);
}

static void test_port_speed_extracts_portsc_speed_field(void) {
    uint32_t portsc = XHCI_PORTSC_CCS | XHCI_PORTSC_PED | (4u << XHCI_PORTSC_SPEED_SHIFT);
    if (xhci_port_speed_from_status(portsc) != 4u) {
        fail("port speed extraction must use PORTSC speed bits");
    }
}

static void test_ep0_mps_policy_matches_usb_speed(void) {
    if (xhci_ep0_max_packet_size_for_speed(0) != 8u) fail("unknown speed must use conservative EP0 MPS");
    if (xhci_ep0_max_packet_size_for_speed(1) != 8u) fail("full speed must use conservative EP0 MPS before descriptors");
    if (xhci_ep0_max_packet_size_for_speed(3) != 64u) fail("high speed EP0 MPS must be 64");
    if (xhci_ep0_max_packet_size_for_speed(4) != 512u) fail("super speed EP0 MPS must be 512");
    if (xhci_ep0_max_packet_size_for_speed(5) != 512u) fail("super speed plus EP0 MPS must be 512");
}

static void test_build_address_device_input_context_32_byte(void) {
    uint32_t input_ctx[(32u * 33u) / sizeof(uint32_t)];
    struct xhci_trb ep0_ring[16] __attribute__((aligned(16)));
    uint32_t *icc;
    uint32_t *slot_ctx;
    uint32_t *ep0_ctx;
    memset(input_ctx, 0, sizeof(input_ctx));
    memset(ep0_ring, 0, sizeof(ep0_ring));

    if (xhci_build_address_device_input_context(input_ctx, 32u, 2, 3u, ep0_ring) != 0) {
        fail("32-byte context builder must accept valid input");
        return;
    }

    icc = ctx_dwords(input_ctx, 32u, 0);
    slot_ctx = ctx_dwords(input_ctx, 32u, 1);
    ep0_ctx = ctx_dwords(input_ctx, 32u, 2);

    if (icc[1] != 0x3u) fail("input control context must add slot and ep0 contexts");
    if (((slot_ctx[0] >> 20) & 0xFu) != 3u) fail("slot context must encode port speed");
    if (((slot_ctx[0] >> 27) & 0x1Fu) != 1u) fail("slot context must set context entries to ep0");
    if (((slot_ctx[1] >> 16) & 0xFFu) != 3u) fail("slot context must encode one-based root hub port");
    if (((ep0_ctx[1] >> 1) & 0x3u) != 3u) fail("ep0 context must set error count");
    if (((ep0_ctx[1] >> 3) & 0x7u) != 4u) fail("ep0 context must be control endpoint type");
    if (((ep0_ctx[1] >> 16) & 0xFFFFu) != 64u) fail("high speed ep0 max packet size must be encoded");
    if ((ep0_ctx[2] & 1u) != 1u) fail("ep0 dequeue pointer must set dequeue cycle state");
    if (ep0_ctx[4] != 8u) fail("ep0 context must set average TRB length");
}

static void test_build_address_device_input_context_64_byte(void) {
    uint32_t input_ctx[(64u * 33u) / sizeof(uint32_t)];
    struct xhci_trb ep0_ring[16] __attribute__((aligned(16)));
    uint32_t *icc;
    uint32_t *slot_ctx;
    uint32_t *ep0_ctx;
    memset(input_ctx, 0, sizeof(input_ctx));
    memset(ep0_ring, 0, sizeof(ep0_ring));

    if (xhci_build_address_device_input_context(input_ctx, 64u, 0, 4u, ep0_ring) != 0) {
        fail("64-byte context builder must accept valid input");
        return;
    }

    icc = ctx_dwords(input_ctx, 64u, 0);
    slot_ctx = ctx_dwords(input_ctx, 64u, 1);
    ep0_ctx = ctx_dwords(input_ctx, 64u, 2);

    if (icc[1] != 0x3u) fail("64-byte input control context must add contexts");
    if (((slot_ctx[0] >> 20) & 0xFu) != 4u) fail("64-byte slot context must encode speed");
    if (((slot_ctx[1] >> 16) & 0xFFu) != 1u) fail("64-byte slot context must encode one-based port");
    if (((ep0_ctx[1] >> 16) & 0xFFFFu) != 512u) fail("super speed ep0 max packet size must be encoded");
}

static void test_build_address_device_input_context_rejects_bad_inputs(void) {
    uint32_t input_ctx[(32u * 33u) / sizeof(uint32_t)];
    struct xhci_trb ep0_ring[16] __attribute__((aligned(16)));
    if (xhci_build_address_device_input_context(NULL, 32u, 0, 3u, ep0_ring) == 0) {
        fail("builder must reject null input context");
    }
    if (xhci_build_address_device_input_context(input_ctx, 48u, 0, 3u, ep0_ring) == 0) {
        fail("builder must reject unsupported context size");
    }
    if (xhci_build_address_device_input_context(input_ctx, 32u, -1, 3u, ep0_ring) == 0) {
        fail("builder must reject negative port");
    }
    if (xhci_build_address_device_input_context(input_ctx, 32u, 0, 3u, NULL) == 0) {
        fail("builder must reject null ep0 ring");
    }
}

static void test_endpoint_dci_mapping(void) {
    if (xhci_endpoint_dci(0x00) != 0u) fail("EP0 must not map as interrupt DCI");
    if (xhci_endpoint_dci(0x01) != 2u) fail("EP1 OUT must map to DCI 2");
    if (xhci_endpoint_dci(0x81) != 3u) fail("EP1 IN must map to DCI 3");
    if (xhci_endpoint_dci(0x8F) != 31u) fail("EP15 IN must map to DCI 31");
}

static void test_build_configure_endpoint_input_context_interrupt_in(void) {
    uint32_t input_ctx[(32u * 33u) / sizeof(uint32_t)];
    struct xhci_trb intr_ring[16] __attribute__((aligned(16)));
    uint32_t *icc;
    uint32_t *slot_ctx;
    uint32_t *ep_ctx;
    memset(input_ctx, 0, sizeof(input_ctx));
    memset(intr_ring, 0, sizeof(intr_ring));
    if (xhci_build_configure_endpoint_input_context(input_ctx, 32u, 0x81u, 8u, 10u,
                                                    intr_ring) != 0) {
        fail("configure endpoint context builder must accept interrupt IN endpoint");
        return;
    }
    icc = ctx_dwords(input_ctx, 32u, 0);
    slot_ctx = ctx_dwords(input_ctx, 32u, 1);
    ep_ctx = ctx_dwords(input_ctx, 32u, xhci_endpoint_dci(0x81u) + 1u);
    if (icc[1] != ((1u << 0) | (1u << 3))) fail("configure input control must add slot and DCI");
    if (((slot_ctx[0] >> 27) & 0x1Fu) != 3u) fail("configure slot context entries must reach endpoint DCI");
    if (((ep_ctx[0] >> 16) & 0xFFu) != 10u) fail("endpoint interval must be encoded");
    if (((ep_ctx[1] >> 1) & 0x3u) != 3u) fail("interrupt endpoint error count must be set");
    if (((ep_ctx[1] >> 3) & 0x7u) != 7u) fail("endpoint type must be interrupt IN");
    if (((ep_ctx[1] >> 16) & 0xFFFFu) != 8u) fail("endpoint max packet size must be encoded");
    if ((ep_ctx[2] & 1u) != 1u) fail("interrupt dequeue pointer must set DCS");
    if (ep_ctx[4] != 8u) fail("average TRB length must match report length");
}

static void test_build_normal_trb_layout(void) {
    struct xhci_trb trb;
    uint8_t report[8];
    memset(&trb, 0, sizeof(trb));
    memset(report, 0, sizeof(report));
    if (xhci_build_normal_trb(&trb, report, sizeof(report)) != 0) {
        fail("normal TRB builder must accept report buffer");
        return;
    }
    if (trb.param != (uint64_t)(uintptr_t)report) fail("normal TRB must point at report buffer");
    if (trb.status != sizeof(report)) fail("normal TRB must encode report length");
    if (((trb.control >> 10) & 0x3Fu) != TRB_TYPE_NORMAL) fail("normal TRB type must be normal");
    if (((trb.control >> 5) & 1u) != 1u) fail("normal TRB must request IOC");
}

static void seed_command_completion(struct xhci_trb *evt_ring, uint32_t evt_idx,
                                    uint8_t slot_id) {
    evt_ring[evt_idx].param = 0;
    evt_ring[evt_idx].status = (uint32_t)XHCI_TRB_CC_SUCCESS << 24;
    evt_ring[evt_idx].control = (TRB_TYPE_CMD_COMPLETE << 10) |
                                ((uint32_t)slot_id << 24) | 1u;
}

static void seed_port_status_event(struct xhci_trb *evt_ring, uint32_t evt_idx) {
    evt_ring[evt_idx].param = 0;
    evt_ring[evt_idx].status = 0;
    evt_ring[evt_idx].control = (TRB_TYPE_PORT_STATUS << 10) | 1u;
}

static void seed_transfer_event(struct xhci_trb *evt_ring, uint32_t evt_idx,
                                uint8_t slot_id, uint8_t ep_id) {
    evt_ring[evt_idx].param = 0;
    evt_ring[evt_idx].status = (uint32_t)XHCI_TRB_CC_SUCCESS << 24;
    evt_ring[evt_idx].control = (TRB_TYPE_TRANSFER << 10) |
                                ((uint32_t)ep_id << 16) |
                                ((uint32_t)slot_id << 24) | 1u;
}

static void test_enable_slot_advances_past_index_63(void) {
    struct xhci_controller xhci;
    struct xhci_trb cmd_ring[XHCI_CMD_RING_TRBS] __attribute__((aligned(16)));
    struct xhci_trb evt_ring[XHCI_EVT_RING_TRBS] __attribute__((aligned(16)));
    uint32_t doorbell = 0xFFFFFFFFu;
    uint8_t slot_id = 0;
    memset(&xhci, 0, sizeof(xhci));
    memset(cmd_ring, 0, sizeof(cmd_ring));
    memset(evt_ring, 0, sizeof(evt_ring));
    seed_command_completion(evt_ring, 0, 7u);
    xhci.initialized = 1;
    xhci.cmd_ring = cmd_ring;
    xhci.evt_ring = evt_ring;
    xhci.db_base = (volatile uint8_t *)&doorbell;
    xhci.max_slots = 16u;
    xhci.cmd_ring_idx = 63u;
    xhci.cmd_ring_cycle = 1;
    xhci.evt_ring_cycle = 1;
    if (xhci_enable_slot(&xhci, &slot_id) != 0) {
        fail("enable slot must consume seeded command completion");
        return;
    }
    if (slot_id != 7u) fail("enable slot must return completed slot id");
    if (xhci.cmd_ring_idx != 64u) fail("command ring index must advance past 63");
    if (xhci.evt_ring_idx != 1u) fail("event ring index must advance by one");
    if (doorbell != 0u) fail("enable slot must ring command doorbell");
}

static void test_enable_slot_wraps_on_link_trb_boundary(void) {
    struct xhci_controller xhci;
    struct xhci_trb cmd_ring[XHCI_CMD_RING_TRBS] __attribute__((aligned(16)));
    struct xhci_trb evt_ring[XHCI_EVT_RING_TRBS] __attribute__((aligned(16)));
    uint32_t doorbell = 0xFFFFFFFFu;
    uint8_t slot_id = 0;
    memset(&xhci, 0, sizeof(xhci));
    memset(cmd_ring, 0, sizeof(cmd_ring));
    memset(evt_ring, 0, sizeof(evt_ring));
    seed_command_completion(evt_ring, 0, 9u);
    xhci.initialized = 1;
    xhci.cmd_ring = cmd_ring;
    xhci.evt_ring = evt_ring;
    xhci.db_base = (volatile uint8_t *)&doorbell;
    xhci.max_slots = 16u;
    xhci.cmd_ring_idx = XHCI_CMD_RING_TRBS - 2u;
    xhci.cmd_ring_cycle = 1;
    xhci.evt_ring_cycle = 1;
    if (xhci_enable_slot(&xhci, &slot_id) != 0) {
        fail("enable slot at ring tail must complete");
        return;
    }
    if (slot_id != 9u) fail("tail enable slot must return completed slot id");
    if (xhci.cmd_ring_idx != 0u) fail("command ring must wrap before link TRB");
    if (xhci.cmd_ring_cycle != 0) fail("command ring cycle must toggle on wrap");
}

static void test_enable_slot_toggles_event_cycle_on_wrap(void) {
    struct xhci_controller xhci;
    struct xhci_trb cmd_ring[XHCI_CMD_RING_TRBS] __attribute__((aligned(16)));
    struct xhci_trb evt_ring[XHCI_EVT_RING_TRBS] __attribute__((aligned(16)));
    uint32_t doorbell = 0xFFFFFFFFu;
    uint8_t slot_id = 0;
    memset(&xhci, 0, sizeof(xhci));
    memset(cmd_ring, 0, sizeof(cmd_ring));
    memset(evt_ring, 0, sizeof(evt_ring));
    seed_command_completion(evt_ring, XHCI_EVT_RING_TRBS - 1u, 11u);
    xhci.initialized = 1;
    xhci.cmd_ring = cmd_ring;
    xhci.evt_ring = evt_ring;
    xhci.db_base = (volatile uint8_t *)&doorbell;
    xhci.max_slots = 16u;
    xhci.cmd_ring_idx = 0u;
    xhci.cmd_ring_cycle = 1;
    xhci.evt_ring_idx = XHCI_EVT_RING_TRBS - 1u;
    xhci.evt_ring_cycle = 1;
    if (xhci_enable_slot(&xhci, &slot_id) != 0) {
        fail("enable slot must consume event at ring tail");
        return;
    }
    if (slot_id != 11u) fail("event tail completion must return slot id");
    if (xhci.evt_ring_idx != 0u) fail("event ring index must wrap to zero");
    if (xhci.evt_ring_cycle != 0) fail("event ring cycle must toggle on wrap");
}

static void test_enable_slot_skips_non_command_events(void) {
    struct xhci_controller xhci;
    struct xhci_trb cmd_ring[XHCI_CMD_RING_TRBS] __attribute__((aligned(16)));
    struct xhci_trb evt_ring[XHCI_EVT_RING_TRBS] __attribute__((aligned(16)));
    uint32_t doorbell = 0xFFFFFFFFu;
    uint8_t slot_id = 0;
    memset(&xhci, 0, sizeof(xhci));
    memset(cmd_ring, 0, sizeof(cmd_ring));
    memset(evt_ring, 0, sizeof(evt_ring));
    seed_port_status_event(evt_ring, 0);
    seed_command_completion(evt_ring, 1, 12u);
    xhci.initialized = 1;
    xhci.cmd_ring = cmd_ring;
    xhci.evt_ring = evt_ring;
    xhci.db_base = (volatile uint8_t *)&doorbell;
    xhci.max_slots = 16u;
    xhci.cmd_ring_cycle = 1;
    xhci.evt_ring_cycle = 1;
    if (xhci_enable_slot(&xhci, &slot_id) != 0) {
        fail("enable slot must skip non-command events before completion");
        return;
    }
    if (slot_id != 12u) fail("completion after non-command event must return slot id");
    if (xhci.evt_ring_idx != 2u) fail("event ring must consume skipped and completion events");
}

static void test_control_transfer_queues_get_descriptor_trbs(void) {
    struct xhci_controller xhci;
    struct xhci_trb ep0_ring[XHCI_CMD_RING_TRBS] __attribute__((aligned(16)));
    struct xhci_trb evt_ring[XHCI_EVT_RING_TRBS] __attribute__((aligned(16)));
    uint32_t doorbells[16];
    uint8_t data[18];
    struct usb_setup_packet setup;
    uint64_t setup_param;
    memset(&xhci, 0, sizeof(xhci));
    memset(ep0_ring, 0, sizeof(ep0_ring));
    memset(evt_ring, 0, sizeof(evt_ring));
    memset(doorbells, 0xFF, sizeof(doorbells));
    memset(data, 0, sizeof(data));
    if (usb_build_get_descriptor_request(USB_DESC_TYPE_DEVICE, 0, 0, sizeof(data), &setup) != 0) {
        fail("setup request prerequisite failed");
        return;
    }
    seed_transfer_event(evt_ring, 0, 3u, 1u);
    xhci.initialized = 1;
    xhci.max_slots = 8u;
    xhci.ep0_rings[3] = ep0_ring;
    xhci.ep0_ring_cycle[3] = 1;
    xhci.evt_ring = evt_ring;
    xhci.evt_ring_cycle = 1;
    xhci.db_base = (volatile uint8_t *)doorbells;
    if (xhci_control_transfer(&xhci, 3u, &setup, data, sizeof(data), 1) != 0) {
        fail("control transfer must complete from seeded transfer event");
        return;
    }
    setup_param = ((uint64_t)setup.bmRequestType) |
                  ((uint64_t)setup.bRequest << 8) |
                  ((uint64_t)setup.wValue << 16) |
                  ((uint64_t)setup.wIndex << 32) |
                  ((uint64_t)setup.wLength << 48);
    if (ep0_ring[0].param != setup_param) fail("setup TRB must inline setup packet");
    if (((ep0_ring[0].control >> 10) & 0x3Fu) != TRB_TYPE_SETUP) fail("first EP0 TRB must be setup");
    if (((ep0_ring[0].control >> 16) & 0x3u) != 3u) fail("setup TRB must declare IN data stage");
    if ((ep0_ring[0].control & 1u) != 1u) fail("setup TRB must carry EP0 cycle bit");
    if (ep0_ring[1].param != (uint64_t)(uintptr_t)data) fail("data TRB must point at data buffer");
    if (ep0_ring[1].status != sizeof(data)) fail("data TRB must encode length");
    if (((ep0_ring[1].control >> 10) & 0x3Fu) != TRB_TYPE_DATA) fail("second EP0 TRB must be data");
    if (((ep0_ring[1].control >> 16) & 1u) != 1u) fail("data TRB must be IN");
    if (((ep0_ring[2].control >> 10) & 0x3Fu) != TRB_TYPE_STATUS) fail("third EP0 TRB must be status");
    if (((ep0_ring[2].control >> 16) & 1u) != 0u) fail("status TRB must be OUT for IN transfer");
    if (xhci.ep0_ring_idx[3] != 3u) fail("EP0 ring index must advance by three TRBs");
    if (doorbells[3] != 1u) fail("control transfer must ring slot doorbell for EP0");
}

static void test_control_transfer_wraps_ep0_ring(void) {
    struct xhci_controller xhci;
    struct xhci_trb ep0_ring[XHCI_CMD_RING_TRBS] __attribute__((aligned(16)));
    struct xhci_trb evt_ring[XHCI_EVT_RING_TRBS] __attribute__((aligned(16)));
    uint32_t doorbells[16];
    uint8_t data[8];
    struct usb_setup_packet setup;
    memset(&xhci, 0, sizeof(xhci));
    memset(ep0_ring, 0, sizeof(ep0_ring));
    memset(evt_ring, 0, sizeof(evt_ring));
    memset(doorbells, 0xFF, sizeof(doorbells));
    memset(data, 0, sizeof(data));
    usb_build_get_descriptor_request(USB_DESC_TYPE_DEVICE, 0, 0, sizeof(data), &setup);
    seed_transfer_event(evt_ring, 0, 4u, 1u);
    xhci.initialized = 1;
    xhci.max_slots = 8u;
    xhci.ep0_rings[4] = ep0_ring;
    xhci.ep0_ring_idx[4] = XHCI_CMD_RING_TRBS - 2u;
    xhci.ep0_ring_cycle[4] = 1;
    xhci.evt_ring = evt_ring;
    xhci.evt_ring_cycle = 1;
    xhci.db_base = (volatile uint8_t *)doorbells;
    if (xhci_control_transfer(&xhci, 4u, &setup, data, sizeof(data), 1) != 0) {
        fail("control transfer at EP0 tail must complete");
        return;
    }
    if (((ep0_ring[XHCI_CMD_RING_TRBS - 2u].control >> 10) & 0x3Fu) != TRB_TYPE_SETUP) {
        fail("tail EP0 TRB must contain setup stage");
    }
    if (((ep0_ring[0].control >> 10) & 0x3Fu) != TRB_TYPE_DATA) {
        fail("wrapped EP0 TRB 0 must contain data stage");
    }
    if (((ep0_ring[1].control >> 10) & 0x3Fu) != TRB_TYPE_STATUS) {
        fail("wrapped EP0 TRB 1 must contain status stage");
    }
    if (xhci.ep0_ring_idx[4] != 2u) fail("EP0 ring index must wrap and advance");
    if (xhci.ep0_ring_cycle[4] != 0) fail("EP0 ring cycle must toggle on wrap");
}

static void test_control_transfer_set_configuration_no_data(void) {
    struct xhci_controller xhci;
    struct xhci_trb ep0_ring[XHCI_CMD_RING_TRBS] __attribute__((aligned(16)));
    struct xhci_trb evt_ring[XHCI_EVT_RING_TRBS] __attribute__((aligned(16)));
    uint32_t doorbells[16];
    struct usb_setup_packet setup;
    memset(&xhci, 0, sizeof(xhci));
    memset(ep0_ring, 0, sizeof(ep0_ring));
    memset(evt_ring, 0, sizeof(evt_ring));
    memset(doorbells, 0xFF, sizeof(doorbells));
    setup.bmRequestType = 0x00u;
    setup.bRequest = USB_REQ_SET_CONFIGURATION;
    setup.wValue = 1u;
    setup.wIndex = 0;
    setup.wLength = 0;
    seed_transfer_event(evt_ring, 0, 5u, 1u);
    xhci.initialized = 1;
    xhci.max_slots = 8u;
    xhci.ep0_rings[5] = ep0_ring;
    xhci.ep0_ring_cycle[5] = 1;
    xhci.evt_ring = evt_ring;
    xhci.evt_ring_cycle = 1;
    xhci.db_base = (volatile uint8_t *)doorbells;
    if (xhci_control_transfer(&xhci, 5u, &setup, NULL, 0, 0) != 0) {
        fail("SET_CONFIGURATION no-data control transfer must complete");
        return;
    }
    if (((ep0_ring[0].control >> 10) & 0x3Fu) != TRB_TYPE_SETUP) {
        fail("SET_CONFIGURATION first TRB must be setup");
    }
    if (((ep0_ring[0].control >> 16) & 0x3u) != 0u) {
        fail("SET_CONFIGURATION setup TRB must have no data stage");
    }
    if (((ep0_ring[1].control >> 10) & 0x3Fu) != TRB_TYPE_STATUS) {
        fail("SET_CONFIGURATION second TRB must be status");
    }
    if (((ep0_ring[1].control >> 16) & 1u) != 1u) {
        fail("SET_CONFIGURATION status stage must be IN");
    }
    if (xhci.ep0_ring_idx[5] != 2u) fail("SET_CONFIGURATION must queue two TRBs");
    if (doorbells[5] != 1u) fail("SET_CONFIGURATION must ring EP0 doorbell");
}

static void test_control_transfer_preserves_unmatched_transfer_event(void) {
    struct xhci_controller xhci;
    struct xhci_trb ep0_ring[XHCI_CMD_RING_TRBS] __attribute__((aligned(16)));
    struct xhci_trb evt_ring[XHCI_EVT_RING_TRBS] __attribute__((aligned(16)));
    uint32_t doorbells[16];
    struct usb_setup_packet setup;
    memset(&xhci, 0, sizeof(xhci));
    memset(ep0_ring, 0, sizeof(ep0_ring));
    memset(evt_ring, 0, sizeof(evt_ring));
    memset(doorbells, 0xFF, sizeof(doorbells));
    setup.bmRequestType = 0x00u;
    setup.bRequest = USB_REQ_SET_CONFIGURATION;
    setup.wValue = 1u;
    setup.wIndex = 0;
    setup.wLength = 0;
    seed_transfer_event(evt_ring, 0, 7u, 1u);
    xhci.initialized = 1;
    xhci.max_slots = 8u;
    xhci.ep0_rings[5] = ep0_ring;
    xhci.ep0_ring_cycle[5] = 1;
    xhci.evt_ring = evt_ring;
    xhci.evt_ring_cycle = 1;
    xhci.db_base = (volatile uint8_t *)doorbells;
    if (xhci_control_transfer(&xhci, 5u, &setup, NULL, 0, 0) == 0) {
        fail("control transfer must reject event for another slot");
    }
    if (xhci.evt_ring_idx != 0u) fail("unmatched control transfer event must remain pending");
}

static void test_control_transfer_hid_set_protocol_no_data(void) {
    struct xhci_controller xhci;
    struct xhci_trb ep0_ring[XHCI_CMD_RING_TRBS] __attribute__((aligned(16)));
    struct xhci_trb evt_ring[XHCI_EVT_RING_TRBS] __attribute__((aligned(16)));
    uint32_t doorbells[16];
    struct usb_setup_packet setup;
    uint64_t setup_param;
    memset(&xhci, 0, sizeof(xhci));
    memset(ep0_ring, 0, sizeof(ep0_ring));
    memset(evt_ring, 0, sizeof(evt_ring));
    memset(doorbells, 0xFF, sizeof(doorbells));
    setup.bmRequestType = 0x21u;
    setup.bRequest = USB_HID_REQ_SET_PROTOCOL;
    setup.wValue = 0;
    setup.wIndex = 2u;
    setup.wLength = 0;
    seed_transfer_event(evt_ring, 0, 6u, 1u);
    xhci.initialized = 1;
    xhci.max_slots = 8u;
    xhci.ep0_rings[6] = ep0_ring;
    xhci.ep0_ring_cycle[6] = 1;
    xhci.evt_ring = evt_ring;
    xhci.evt_ring_cycle = 1;
    xhci.db_base = (volatile uint8_t *)doorbells;
    if (xhci_control_transfer(&xhci, 6u, &setup, NULL, 0, 0) != 0) {
        fail("HID SET_PROTOCOL no-data control transfer must complete");
        return;
    }
    setup_param = ((uint64_t)setup.bmRequestType) |
                  ((uint64_t)setup.bRequest << 8) |
                  ((uint64_t)setup.wValue << 16) |
                  ((uint64_t)setup.wIndex << 32) |
                  ((uint64_t)setup.wLength << 48);
    if (ep0_ring[0].param != setup_param) fail("HID SET_PROTOCOL setup packet must be inlined");
    if (((ep0_ring[1].control >> 10) & 0x3Fu) != TRB_TYPE_STATUS) {
        fail("HID SET_PROTOCOL second TRB must be status");
    }
    if (((ep0_ring[1].control >> 16) & 1u) != 1u) {
        fail("HID SET_PROTOCOL status stage must be IN");
    }
    if (doorbells[6] != 1u) fail("HID SET_PROTOCOL must ring EP0 doorbell");
}

static void test_configure_interrupt_endpoint_queues_command_and_primes_ring(void) {
    struct xhci_controller xhci;
    struct xhci_trb cmd_ring[XHCI_CMD_RING_TRBS] __attribute__((aligned(16)));
    struct xhci_trb evt_ring[XHCI_EVT_RING_TRBS] __attribute__((aligned(16)));
    uint32_t doorbells[16];
    uint32_t dummy_device_context;
    struct usb_endpoint_info ep;
    memset(&xhci, 0, sizeof(xhci));
    memset(cmd_ring, 0, sizeof(cmd_ring));
    memset(evt_ring, 0, sizeof(evt_ring));
    memset(doorbells, 0xFF, sizeof(doorbells));
    seed_command_completion(evt_ring, 0, 0u);
    ep.address = 0x81u;
    ep.type = 3u;
    ep.max_packet_size = 8u;
    ep.interval = 10u;
    xhci.initialized = 1;
    xhci.max_slots = 8u;
    xhci.context_size = 32u;
    xhci.cmd_ring = cmd_ring;
    xhci.cmd_ring_cycle = 1;
    xhci.evt_ring = evt_ring;
    xhci.evt_ring_cycle = 1;
    xhci.db_base = (volatile uint8_t *)doorbells;
    xhci.device_contexts[5] = &dummy_device_context;
    if (xhci_configure_interrupt_endpoint(&xhci, 5u, &ep, 8u) != 0) {
        fail("configure interrupt endpoint must consume seeded command completion");
        return;
    }
    if (((cmd_ring[0].control >> 10) & 0x3Fu) != TRB_TYPE_CONFIG_EP) {
        fail("configure endpoint must queue Configure Endpoint command");
    }
    if (((cmd_ring[0].control >> 24) & 0xFFu) != 5u) fail("configure command must encode slot id");
    if (!xhci.intr_rings[5]) fail("interrupt ring must be retained on success");
    if (!xhci.intr_buffers[5]) fail("interrupt report buffer must be retained on success");
    if (xhci.intr_ep_dci[5] != xhci_endpoint_dci(0x81u)) fail("interrupt endpoint DCI must be stored");
    if (((xhci.intr_rings[5][0].control >> 10) & 0x3Fu) != TRB_TYPE_NORMAL) {
        fail("interrupt ring must be primed with normal TRB");
    }
    if (doorbells[5] != xhci_endpoint_dci(0x81u)) fail("configure must ring endpoint doorbell");
}

static void test_poll_interrupt_copies_report_and_rearms(void) {
    struct xhci_controller xhci;
    struct xhci_trb intr_ring[XHCI_CMD_RING_TRBS] __attribute__((aligned(16)));
    struct xhci_trb evt_ring[XHCI_EVT_RING_TRBS] __attribute__((aligned(16)));
    uint32_t doorbells[16];
    uint8_t report[8] = {0x02u, 0, 4u, 0, 0, 0, 0, 0};
    uint8_t out[8];
    memset(&xhci, 0, sizeof(xhci));
    memset(intr_ring, 0, sizeof(intr_ring));
    memset(evt_ring, 0, sizeof(evt_ring));
    memset(doorbells, 0xFF, sizeof(doorbells));
    memset(out, 0, sizeof(out));
    seed_transfer_event(evt_ring, 0, 6u, xhci_endpoint_dci(0x81u));
    xhci.initialized = 1;
    xhci.max_slots = 8u;
    xhci.evt_ring = evt_ring;
    xhci.evt_ring_cycle = 1;
    xhci.db_base = (volatile uint8_t *)doorbells;
    xhci.intr_rings[6] = intr_ring;
    xhci.intr_buffers[6] = report;
    xhci.intr_buffer_len[6] = sizeof(report);
    xhci.intr_ep_addr[6] = 0x81u;
    xhci.intr_ep_dci[6] = xhci_endpoint_dci(0x81u);
    xhci.intr_ring_cycle[6] = 1;
    if (xhci_poll_interrupt(&xhci, 6u, 0x81u, out, sizeof(out)) != (int)sizeof(out)) {
        fail("poll interrupt must return copied report length");
        return;
    }
    if (out[0] != 0x02u || out[2] != 4u) fail("poll interrupt must copy report bytes");
    if (((intr_ring[0].control >> 10) & 0x3Fu) != TRB_TYPE_NORMAL) {
        fail("poll interrupt must rearm a normal TRB");
    }
    if (doorbells[6] != xhci_endpoint_dci(0x81u)) fail("poll interrupt must ring endpoint doorbell");
    if (xhci.evt_ring_idx != 1u) fail("poll interrupt must consume transfer event");
}

static void test_poll_interrupt_preserves_unmatched_transfer_event(void) {
    struct xhci_controller xhci;
    struct xhci_trb intr_ring[XHCI_CMD_RING_TRBS] __attribute__((aligned(16)));
    struct xhci_trb evt_ring[XHCI_EVT_RING_TRBS] __attribute__((aligned(16)));
    uint8_t report[8] = {0};
    uint8_t out[8];
    memset(&xhci, 0, sizeof(xhci));
    memset(intr_ring, 0, sizeof(intr_ring));
    memset(evt_ring, 0, sizeof(evt_ring));
    memset(out, 0, sizeof(out));
    seed_transfer_event(evt_ring, 0, 7u, xhci_endpoint_dci(0x81u));
    xhci.initialized = 1;
    xhci.max_slots = 8u;
    xhci.evt_ring = evt_ring;
    xhci.evt_ring_cycle = 1;
    xhci.intr_rings[6] = intr_ring;
    xhci.intr_buffers[6] = report;
    xhci.intr_buffer_len[6] = sizeof(report);
    xhci.intr_ep_addr[6] = 0x81u;
    xhci.intr_ep_dci[6] = xhci_endpoint_dci(0x81u);
    if (xhci_poll_interrupt(&xhci, 6u, 0x81u, out, sizeof(out)) != 0) {
        fail("poll interrupt must not report data for another slot");
    }
    if (xhci.evt_ring_idx != 0u) fail("unmatched transfer event must remain pending");
}

int run_xhci_address_device_tests(void) {
    g_failures = 0;
    test_port_speed_extracts_portsc_speed_field();
    test_ep0_mps_policy_matches_usb_speed();
    test_build_address_device_input_context_32_byte();
    test_build_address_device_input_context_64_byte();
    test_build_address_device_input_context_rejects_bad_inputs();
    test_endpoint_dci_mapping();
    test_build_configure_endpoint_input_context_interrupt_in();
    test_build_normal_trb_layout();
    test_enable_slot_advances_past_index_63();
    test_enable_slot_wraps_on_link_trb_boundary();
    test_enable_slot_toggles_event_cycle_on_wrap();
    test_enable_slot_skips_non_command_events();
    test_control_transfer_queues_get_descriptor_trbs();
    test_control_transfer_wraps_ep0_ring();
    test_control_transfer_set_configuration_no_data();
    test_control_transfer_preserves_unmatched_transfer_event();
    test_control_transfer_hid_set_protocol_no_data();
    test_configure_interrupt_endpoint_queues_command_and_primes_ring();
    test_poll_interrupt_copies_report_and_rearms();
    test_poll_interrupt_preserves_unmatched_transfer_event();
    if (g_failures == 0) printf("[tests] xhci_address_device OK\n");
    return g_failures;
}
