/* test_xhci_transfers.c — Control transfer + configure interrupt + poll tests.
 *
 * Split (2026-05-21) from tests/drivers/test_xhci_address_device.c to
 * keep each TU ≤ 900 lines. Owns:
 *   - test_control_transfer_* (GET_DESCRIPTOR, EP0 wrap, SET_CONFIGURATION,
 *     stray drain, HID SET_PROTOCOL).
 *   - test_configure_interrupt_endpoint_queues_command_and_primes_ring.
 *   - test_poll_interrupt_* (copies report + rearms, drains stray).
 */
#include "drivers/usb/xhci.h"
#include "drivers/usb/usb_core.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "internal/test_xhci_helpers.h"

static int g_failures = 0;

static void fail(const char *msg) {
    printf("[xhci-transfers] FAIL: %s\n", msg);
    g_failures++;
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

/* Etapa 3 — Slice 3D event ring hardening (§14.2 follow-up).
 *
 * Stray transfer events (for slots/endpoints we do not own) MUST be
 * drained from the event ring by the dispatcher so the producer never
 * observes Event Ring Buffer Overflow. The control transfer for slot 5
 * times out because its own EP0 event never arrives; the stray event
 * for slot 7 is counted in `event_stray_count`. Old behaviour
 * (`evt_ring_idx == 0`, stray pinned at head) is intentionally
 * obsolete — it caused the multi-endpoint stagnation documented in
 * `docs/architecture/etapa-3-driver-foundation-plan.md` §14.2. */
static void test_control_transfer_drains_stray_transfer_event(void) {
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
    /* Transfer event addressed to slot 7 EP0; slot 7 has no ep0_ring
     * registered, so the dispatcher categorises it as stray. */
    seed_transfer_event(evt_ring, 0, 7u, 1u);
    xhci.initialized = 1;
    xhci.max_slots = 8u;
    xhci.ep0_rings[5] = ep0_ring;
    xhci.ep0_ring_cycle[5] = 1;
    xhci.evt_ring = evt_ring;
    xhci.evt_ring_cycle = 1;
    xhci.db_base = (volatile uint8_t *)doorbells;
    if (xhci_control_transfer(&xhci, 5u, &setup, NULL, 0, 0) == 0) {
        fail("control transfer must time out when its own event never arrives");
    }
    if (xhci.evt_ring_idx != 1u) {
        fail("stray transfer event must be drained past, not pinned at head");
    }
    if (xhci.event_stray_count != 1u) {
        fail("stray transfer event must be counted");
    }
    if (xhci.ep0_pending[7].valid != 0u) {
        fail("stray slot must not latch a pending event in an unowned slot");
    }
    if (xhci.ep0_pending[5].valid != 0u) {
        fail("owner's pending slot must remain empty when no event arrives");
    }
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

/* Etapa 3 — Slice 3D event ring hardening (§14.2 follow-up).
 *
 * Counterpart of `test_control_transfer_drains_stray_transfer_event`
 * for interrupt endpoints. A transfer event targeting slot 7 (no
 * owner registered) MUST be drained by the dispatcher and counted as
 * stray; the rightful poller (slot 6) returns 0 without copying any
 * report and observes the empty pending slot. */
static void test_poll_interrupt_drains_stray_transfer_event(void) {
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
    if (xhci.evt_ring_idx != 1u) {
        fail("stray interrupt event must be drained past, not pinned at head");
    }
    if (xhci.event_stray_count != 1u) {
        fail("stray interrupt event must be counted");
    }
    if (xhci.intr_pending[6].valid != 0u) {
        fail("owner's interrupt pending slot must remain empty");
    }
    if (xhci.intr_pending[7].valid != 0u) {
        fail("stray event must not populate an unowned interrupt pending slot");
    }
}

int run_xhci_transfers_tests(void) {
    g_failures = 0;
    test_control_transfer_queues_get_descriptor_trbs();
    test_control_transfer_wraps_ep0_ring();
    test_control_transfer_set_configuration_no_data();
    test_control_transfer_drains_stray_transfer_event();
    test_control_transfer_hid_set_protocol_no_data();
    test_configure_interrupt_endpoint_queues_command_and_primes_ring();
    test_poll_interrupt_copies_report_and_rearms();
    test_poll_interrupt_drains_stray_transfer_event();
    if (g_failures == 0) printf("[tests] xhci_transfers OK\n");
    return g_failures;
}
