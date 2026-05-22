/* test_xhci_helpers.h — Shared event-ring seed helpers for the xhci tests.
 *
 * Split (2026-05-21) when tests/drivers/test_xhci_address_device.c was
 * broken into 4 TUs (test_xhci_address_device.c, test_xhci_transfers.c,
 * test_xhci_event_pump.c, test_xhci_release_slot.c). Only the
 * harness-side seed helpers live here; per-TU pass/fail counters and
 * `fail()` stay private to each TU.
 *
 * Do NOT include this from production code or from files outside
 * tests/drivers/.
 */
#ifndef TESTS_DRIVERS_INTERNAL_TEST_XHCI_HELPERS_H
#define TESTS_DRIVERS_INTERNAL_TEST_XHCI_HELPERS_H

#include <stdint.h>

#include "drivers/usb/xhci.h"

static inline void seed_command_completion(struct xhci_trb *evt_ring,
                                           uint32_t evt_idx,
                                           uint8_t slot_id) {
    evt_ring[evt_idx].param = 0;
    evt_ring[evt_idx].status = (uint32_t)XHCI_TRB_CC_SUCCESS << 24;
    evt_ring[evt_idx].control = (TRB_TYPE_CMD_COMPLETE << 10) |
                                ((uint32_t)slot_id << 24) | 1u;
}

static inline void seed_port_status_event(struct xhci_trb *evt_ring,
                                          uint32_t evt_idx) {
    evt_ring[evt_idx].param = 0;
    evt_ring[evt_idx].status = 0;
    evt_ring[evt_idx].control = (TRB_TYPE_PORT_STATUS << 10) | 1u;
}

static inline void seed_transfer_event(struct xhci_trb *evt_ring,
                                       uint32_t evt_idx, uint8_t slot_id,
                                       uint8_t ep_id) {
    evt_ring[evt_idx].param = 0;
    evt_ring[evt_idx].status = (uint32_t)XHCI_TRB_CC_SUCCESS << 24;
    evt_ring[evt_idx].control = (TRB_TYPE_TRANSFER << 10) |
                                ((uint32_t)ep_id << 16) |
                                ((uint32_t)slot_id << 24) | 1u;
}

#endif /* TESTS_DRIVERS_INTERNAL_TEST_XHCI_HELPERS_H */
