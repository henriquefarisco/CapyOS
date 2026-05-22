/* test_xhci_event_pump.c — Public xhci_event_pump dispatcher tests.
 *
 * Split (2026-05-21) from tests/drivers/test_xhci_address_device.c to
 * keep each TU ≤ 900 lines. Owns the unit tests for the unified
 * Etapa 3 — Slice 3D event-ring dispatcher (xhci_event_pump):
 *   - test_event_pump_routes_owned_ep0_event
 *   - test_event_pump_routes_owned_interrupt_event
 *   - test_event_pump_counts_stray_for_unknown_owner
 *   - test_event_pump_drains_multiple_events_until_stop
 *   - test_event_pump_stops_on_cycle_mismatch
 */
#include "drivers/usb/xhci.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "internal/test_xhci_helpers.h"

static int g_failures = 0;

static void fail(const char *msg) {
    printf("[xhci-event-pump] FAIL: %s\n", msg);
    g_failures++;
}

static void test_event_pump_routes_owned_ep0_event(void) {
    struct xhci_controller xhci;
    struct xhci_trb ep0_ring[16] __attribute__((aligned(16)));
    struct xhci_trb evt_ring[XHCI_EVT_RING_TRBS] __attribute__((aligned(16)));
    memset(&xhci, 0, sizeof(xhci));
    memset(ep0_ring, 0, sizeof(ep0_ring));
    memset(evt_ring, 0, sizeof(evt_ring));
    seed_transfer_event(evt_ring, 0, 5u, 1u);
    xhci.max_slots = 8u;
    xhci.evt_ring = evt_ring;
    xhci.evt_ring_cycle = 1;
    xhci.ep0_rings[5] = ep0_ring;
    xhci_event_pump(&xhci);
    if (!xhci.ep0_pending[5].valid) {
        fail("pump must latch EP0 transfer event into owner slot");
    }
    if (xhci.ep0_pending[5].cc != XHCI_TRB_CC_SUCCESS) {
        fail("pump must propagate completion code");
    }
    if (xhci.evt_ring_idx != 1u) {
        fail("pump must advance ring past consumed event");
    }
    if (xhci.event_stray_count != 0u) {
        fail("owned event must not be counted as stray");
    }
}

static void test_event_pump_routes_owned_interrupt_event(void) {
    struct xhci_controller xhci;
    struct xhci_trb intr_ring[16] __attribute__((aligned(16)));
    struct xhci_trb evt_ring[XHCI_EVT_RING_TRBS] __attribute__((aligned(16)));
    uint8_t buffer[8] = {0};
    memset(&xhci, 0, sizeof(xhci));
    memset(intr_ring, 0, sizeof(intr_ring));
    memset(evt_ring, 0, sizeof(evt_ring));
    seed_transfer_event(evt_ring, 0, 4u, xhci_endpoint_dci(0x81u));
    xhci.max_slots = 8u;
    xhci.evt_ring = evt_ring;
    xhci.evt_ring_cycle = 1;
    xhci.intr_rings[4] = intr_ring;
    xhci.intr_buffers[4] = buffer;
    xhci.intr_buffer_len[4] = sizeof(buffer);
    xhci.intr_ep_dci[4] = xhci_endpoint_dci(0x81u);
    xhci_event_pump(&xhci);
    if (!xhci.intr_pending[4].valid) {
        fail("pump must latch interrupt transfer event into owner slot");
    }
    if (xhci.evt_ring_idx != 1u) {
        fail("pump must advance interrupt ring past consumed event");
    }
    if (xhci.event_stray_count != 0u) {
        fail("owned interrupt event must not be counted as stray");
    }
}

static void test_event_pump_counts_stray_for_unknown_owner(void) {
    struct xhci_controller xhci;
    struct xhci_trb evt_ring[XHCI_EVT_RING_TRBS] __attribute__((aligned(16)));
    memset(&xhci, 0, sizeof(xhci));
    memset(evt_ring, 0, sizeof(evt_ring));
    /* Stray TRANSFER for slot 9 EP0; no ep0_rings registered. */
    seed_transfer_event(evt_ring, 0, 9u, 1u);
    xhci.max_slots = 16u;
    xhci.evt_ring = evt_ring;
    xhci.evt_ring_cycle = 1;
    xhci_event_pump(&xhci);
    if (xhci.event_stray_count != 1u) {
        fail("pump must count stray transfer events");
    }
    if (xhci.evt_ring_idx != 1u) {
        fail("pump must advance past stray events");
    }
    if (xhci.ep0_pending[9].valid != 0u) {
        fail("stray must not populate an unowned pending slot");
    }
}

static void test_event_pump_drains_multiple_events_until_stop(void) {
    struct xhci_controller xhci;
    struct xhci_trb ep0_ring[16] __attribute__((aligned(16)));
    struct xhci_trb evt_ring[XHCI_EVT_RING_TRBS] __attribute__((aligned(16)));
    memset(&xhci, 0, sizeof(xhci));
    memset(ep0_ring, 0, sizeof(ep0_ring));
    memset(evt_ring, 0, sizeof(evt_ring));
    /* Three back-to-back events; pump must drain all three in one call. */
    seed_port_status_event(evt_ring, 0);
    seed_command_completion(evt_ring, 1, 3u);
    seed_transfer_event(evt_ring, 2, 4u, 1u);
    xhci.max_slots = 8u;
    xhci.evt_ring = evt_ring;
    xhci.evt_ring_cycle = 1;
    xhci.ep0_rings[4] = ep0_ring;
    xhci_event_pump(&xhci);
    if (xhci.evt_ring_idx != 3u) {
        fail("pump must drain all valid events in one call");
    }
    if (!xhci.cmd_pending.valid || xhci.cmd_pending.slot != 3u) {
        fail("pump must latch command completion from middle of batch");
    }
    if (!xhci.ep0_pending[4].valid) {
        fail("pump must latch trailing transfer event");
    }
    /* Fourth slot is zero — stop marker. evt_ring_idx must not advance
     * past the zero TRB even though its cycle bit would technically
     * match the new CCS after wrap. */
    if (xhci.event_stray_count != 0u) {
        fail("non-transfer port status event must not be counted as stray");
    }
}

static void test_event_pump_stops_on_cycle_mismatch(void) {
    struct xhci_controller xhci;
    struct xhci_trb evt_ring[XHCI_EVT_RING_TRBS] __attribute__((aligned(16)));
    memset(&xhci, 0, sizeof(xhci));
    memset(evt_ring, 0, sizeof(evt_ring));
    /* CCS=1, but no events with cycle=1 seeded. Pump must be a no-op. */
    xhci.max_slots = 8u;
    xhci.evt_ring = evt_ring;
    xhci.evt_ring_cycle = 1;
    xhci_event_pump(&xhci);
    if (xhci.evt_ring_idx != 0u) {
        fail("pump must not advance when no valid event is present");
    }
}

int run_xhci_event_pump_tests(void) {
    g_failures = 0;
    test_event_pump_routes_owned_ep0_event();
    test_event_pump_routes_owned_interrupt_event();
    test_event_pump_counts_stray_for_unknown_owner();
    test_event_pump_drains_multiple_events_until_stop();
    test_event_pump_stops_on_cycle_mismatch();
    if (g_failures == 0) printf("[tests] xhci_event_pump OK\n");
    return g_failures;
}
