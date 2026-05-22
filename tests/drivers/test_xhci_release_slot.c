/* test_xhci_release_slot.c — Release slot + port ack CSC tests.
 *
 * Split (2026-05-21) from tests/drivers/test_xhci_address_device.c to
 * keep each TU ≤ 900 lines. Owns:
 *   - test_release_slot_* (invalid inputs, addressed, configured,
 *     pending latches, disable failure tolerance, idempotent clean).
 *   - test_port_ack_csc_* (invalid inputs, RW1C semantics).
 */
#include "drivers/usb/xhci.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "internal/test_xhci_helpers.h"

/* Provided by tests/stubs/stub_kmem.c. Required by the §14.3
 * teardown tests which exercise the real free path against pointers
 * allocated through kmalloc_aligned. */
extern void *kmalloc_aligned(uint64_t size, uint64_t alignment);

static int g_failures = 0;

static void fail(const char *msg) {
    printf("[xhci-release-slot] FAIL: %s\n", msg);
    g_failures++;
}

/* Etapa 3 — Slice 3D hardening tests for `xhci_release_slot` (§14.3).
 *
 * The release routine must free every per-slot allocation owned by the
 * controller, zero the DCBAA entry, drop any pending event latches and
 * tolerate the Disable Slot command failing (typical after device
 * disconnect). Pointers are allocated via kmalloc_aligned so the real
 * kfree_aligned path runs without faults. */

static void prepare_release_controller(struct xhci_controller *xhci,
                                       struct xhci_trb *cmd_ring,
                                       struct xhci_trb *evt_ring,
                                       uint64_t *dcbaa,
                                       uint32_t *doorbells) {
    memset(xhci, 0, sizeof(*xhci));
    memset(cmd_ring, 0, sizeof(struct xhci_trb) * XHCI_CMD_RING_TRBS);
    memset(evt_ring, 0, sizeof(struct xhci_trb) * XHCI_EVT_RING_TRBS);
    memset(doorbells, 0xFF, sizeof(uint32_t) * 16u);
    for (uint32_t i = 0; i <= 32u; i++) dcbaa[i] = 0u;
    xhci->initialized = 1;
    xhci->max_slots = 32u;
    xhci->cmd_ring = cmd_ring;
    xhci->cmd_ring_cycle = 1;
    xhci->evt_ring = evt_ring;
    xhci->evt_ring_cycle = 1;
    xhci->db_base = (volatile uint8_t *)doorbells;
    xhci->dcbaa = dcbaa;
}

static void test_release_slot_rejects_invalid_inputs(void) {
    struct xhci_controller xhci;
    memset(&xhci, 0, sizeof(xhci));
    xhci.initialized = 1;
    xhci.max_slots = 8u;
    if (xhci_release_slot(NULL, 1u) != -1) fail("release must reject NULL xhci");
    if (xhci_release_slot(&xhci, 0u) != -1) fail("release must reject slot 0");
    if (xhci_release_slot(&xhci, 9u) != -1) fail("release must reject slot > max_slots");
    xhci.initialized = 0;
    if (xhci_release_slot(&xhci, 1u) != -1) fail("release must reject uninitialized controller");
}

static void test_release_slot_frees_addressed_state(void) {
    struct xhci_controller xhci;
    struct xhci_trb cmd_ring[XHCI_CMD_RING_TRBS] __attribute__((aligned(16)));
    struct xhci_trb evt_ring[XHCI_EVT_RING_TRBS] __attribute__((aligned(16)));
    uint64_t dcbaa[33];
    uint32_t doorbells[16];
    void *ep0_ring;
    void *device_ctx;
    prepare_release_controller(&xhci, cmd_ring, evt_ring, dcbaa, doorbells);
    /* Seed Disable Slot command completion at evt[0]. */
    seed_command_completion(evt_ring, 0, 5u);
    /* Allocate real ring + context so kfree_aligned will succeed. */
    ep0_ring = kmalloc_aligned(XHCI_CMD_RING_TRBS * sizeof(struct xhci_trb), 64);
    device_ctx = kmalloc_aligned(32u * 32u, 64);
    if (!ep0_ring || !device_ctx) {
        fail("kmalloc_aligned must succeed in host tests");
        return;
    }
    xhci.ep0_rings[5] = (struct xhci_trb *)ep0_ring;
    xhci.ep0_ring_idx[5] = 7u;
    xhci.ep0_ring_cycle[5] = 1;
    xhci.device_contexts[5] = device_ctx;
    xhci.dcbaa[5] = (uint64_t)(uintptr_t)device_ctx;
    if (xhci_release_slot(&xhci, 5u) != 0) {
        fail("release must succeed when Disable Slot completes with CC=SUCCESS");
    }
    if (xhci.ep0_rings[5] != NULL) fail("release must NULL EP0 ring pointer");
    if (xhci.device_contexts[5] != NULL) fail("release must NULL device context pointer");
    if (xhci.dcbaa[5] != 0u) fail("release must zero DCBAA entry");
    if (xhci.ep0_ring_idx[5] != 0u) fail("release must zero EP0 ring index");
    if (xhci.ep0_ring_cycle[5] != 0) fail("release must zero EP0 ring cycle");
}

static void test_release_slot_frees_configured_state(void) {
    struct xhci_controller xhci;
    struct xhci_trb cmd_ring[XHCI_CMD_RING_TRBS] __attribute__((aligned(16)));
    struct xhci_trb evt_ring[XHCI_EVT_RING_TRBS] __attribute__((aligned(16)));
    uint64_t dcbaa[33];
    uint32_t doorbells[16];
    void *ep0_ring;
    void *device_ctx;
    void *intr_ring;
    void *intr_buffer;
    prepare_release_controller(&xhci, cmd_ring, evt_ring, dcbaa, doorbells);
    seed_command_completion(evt_ring, 0, 4u);
    ep0_ring = kmalloc_aligned(XHCI_CMD_RING_TRBS * sizeof(struct xhci_trb), 64);
    device_ctx = kmalloc_aligned(32u * 32u, 64);
    intr_ring = kmalloc_aligned(XHCI_CMD_RING_TRBS * sizeof(struct xhci_trb), 64);
    intr_buffer = kmalloc_aligned(8u, 16);
    if (!ep0_ring || !device_ctx || !intr_ring || !intr_buffer) {
        fail("kmalloc_aligned must succeed for full configured state");
        return;
    }
    xhci.ep0_rings[4] = (struct xhci_trb *)ep0_ring;
    xhci.device_contexts[4] = device_ctx;
    xhci.intr_rings[4] = (struct xhci_trb *)intr_ring;
    xhci.intr_buffers[4] = (uint8_t *)intr_buffer;
    xhci.intr_buffer_len[4] = 8u;
    xhci.intr_ep_addr[4] = 0x81u;
    xhci.intr_ep_dci[4] = xhci_endpoint_dci(0x81u);
    xhci.intr_ring_idx[4] = 3u;
    xhci.intr_ring_cycle[4] = 1;
    xhci.dcbaa[4] = (uint64_t)(uintptr_t)device_ctx;
    if (xhci_release_slot(&xhci, 4u) != 0) {
        fail("release must succeed for configured slot");
    }
    if (xhci.intr_rings[4] != NULL) fail("release must NULL interrupt ring");
    if (xhci.intr_buffers[4] != NULL) fail("release must NULL interrupt buffer");
    if (xhci.intr_buffer_len[4] != 0u) fail("release must zero interrupt buffer length");
    if (xhci.intr_ep_addr[4] != 0u) fail("release must zero interrupt EP address");
    if (xhci.intr_ep_dci[4] != 0u) fail("release must zero interrupt EP DCI");
    if (xhci.intr_ring_idx[4] != 0u) fail("release must zero interrupt ring index");
    if (xhci.intr_ring_cycle[4] != 0) fail("release must zero interrupt ring cycle");
}

static void test_release_slot_clears_pending_latches(void) {
    struct xhci_controller xhci;
    struct xhci_trb cmd_ring[XHCI_CMD_RING_TRBS] __attribute__((aligned(16)));
    struct xhci_trb evt_ring[XHCI_EVT_RING_TRBS] __attribute__((aligned(16)));
    uint64_t dcbaa[33];
    uint32_t doorbells[16];
    prepare_release_controller(&xhci, cmd_ring, evt_ring, dcbaa, doorbells);
    seed_command_completion(evt_ring, 0, 6u);
    /* Simulate stale pending latches from before the device left. */
    xhci.ep0_pending[6].valid = 1u;
    xhci.ep0_pending[6].cc = XHCI_TRB_CC_SUCCESS;
    xhci.intr_pending[6].valid = 1u;
    xhci.intr_pending[6].cc = XHCI_TRB_CC_SUCCESS;
    if (xhci_release_slot(&xhci, 6u) != 0) {
        fail("release must succeed when only pending latches need clearing");
    }
    if (xhci.ep0_pending[6].valid != 0u) {
        fail("release must invalidate stale EP0 pending latch");
    }
    if (xhci.intr_pending[6].valid != 0u) {
        fail("release must invalidate stale interrupt pending latch");
    }
}

static void test_release_slot_tolerates_disable_failure(void) {
    struct xhci_controller xhci;
    struct xhci_trb cmd_ring[XHCI_CMD_RING_TRBS] __attribute__((aligned(16)));
    struct xhci_trb evt_ring[XHCI_EVT_RING_TRBS] __attribute__((aligned(16)));
    uint64_t dcbaa[33];
    uint32_t doorbells[16];
    void *ep0_ring;
    void *device_ctx;
    prepare_release_controller(&xhci, cmd_ring, evt_ring, dcbaa, doorbells);
    /* Seed Disable Slot completion with non-success CC. The hot-unplug
     * path commonly produces this when the controller already saw the
     * disconnect before we issued the command. */
    evt_ring[0].param = 0;
    evt_ring[0].status = (uint32_t)5u << 24; /* CC=5 (STALL_ERROR) */
    evt_ring[0].control = (TRB_TYPE_CMD_COMPLETE << 10) |
                          ((uint32_t)2u << 24) | 1u;
    ep0_ring = kmalloc_aligned(XHCI_CMD_RING_TRBS * sizeof(struct xhci_trb), 64);
    device_ctx = kmalloc_aligned(32u * 32u, 64);
    if (!ep0_ring || !device_ctx) {
        fail("kmalloc_aligned must succeed in disable-fail scenario");
        return;
    }
    xhci.ep0_rings[2] = (struct xhci_trb *)ep0_ring;
    xhci.device_contexts[2] = device_ctx;
    xhci.dcbaa[2] = (uint64_t)(uintptr_t)device_ctx;
    /* Release should return non-zero to surface the failure but still
     * free everything and zero the DCBAA so a future Enable Slot does
     * not collide. */
    if (xhci_release_slot(&xhci, 2u) == 0) {
        fail("release must propagate non-success CC");
    }
    if (xhci.ep0_rings[2] != NULL) fail("release must free EP0 ring even on CC failure");
    if (xhci.device_contexts[2] != NULL) fail("release must free device context even on CC failure");
    if (xhci.dcbaa[2] != 0u) fail("release must zero DCBAA even on CC failure");
}

static void test_release_slot_idempotent_on_clean_slot(void) {
    struct xhci_controller xhci;
    struct xhci_trb cmd_ring[XHCI_CMD_RING_TRBS] __attribute__((aligned(16)));
    struct xhci_trb evt_ring[XHCI_EVT_RING_TRBS] __attribute__((aligned(16)));
    uint64_t dcbaa[33];
    uint32_t doorbells[16];
    prepare_release_controller(&xhci, cmd_ring, evt_ring, dcbaa, doorbells);
    seed_command_completion(evt_ring, 0, 1u);
    /* No allocations: simulate a slot that was Enable-Slot'd but never
     * Address-Device'd. Release must not crash on the NULL frees. */
    if (xhci_release_slot(&xhci, 1u) != 0) {
        fail("release must succeed on a clean slot");
    }
    if (xhci.ep0_rings[1] != NULL || xhci.device_contexts[1] != NULL ||
        xhci.intr_rings[1] != NULL || xhci.intr_buffers[1] != NULL) {
        fail("release must leave all pointers NULL on clean slot");
    }
}

/* Etapa 3 — Slice 3D §15.1 hardening: xhci_port_ack_csc must clear
 * the CSC bit (RW1C semantics) while preserving the other PORTSC
 * change bits. Without this, usb_hotplug_check would re-fire on
 * every poll tick because writing 0 to a RW1C bit has no effect. */
static void test_port_ack_csc_rejects_invalid_inputs(void) {
    struct xhci_controller xhci;
    memset(&xhci, 0, sizeof(xhci));
    xhci.initialized = 1;
    xhci.max_ports = 4u;
    if (xhci_port_ack_csc(NULL, 0) != -1) fail("ack_csc must reject NULL xhci");
    if (xhci_port_ack_csc(&xhci, -1) != -1) fail("ack_csc must reject negative port");
    if (xhci_port_ack_csc(&xhci, 4) != -1) fail("ack_csc must reject port >= max_ports");
    xhci.initialized = 0;
    if (xhci_port_ack_csc(&xhci, 0) != -1) fail("ack_csc must reject uninitialized controller");
}

static void test_port_ack_csc_clears_only_csc_bit(void) {
    struct xhci_controller xhci;
    /* Mock op_base region: at least 0x400 + max_ports * 16 bytes.
     * For port 0: portsc at offset 0x400. Allocate enough headroom. */
    uint8_t mock_op[0x500] __attribute__((aligned(64)));
    volatile uint32_t *portsc_p0;
    volatile uint32_t *portsc_p1;
    uint32_t after_p0;
    uint32_t after_p1;
    memset(&xhci, 0, sizeof(xhci));
    memset(mock_op, 0, sizeof(mock_op));
    xhci.initialized = 1;
    xhci.max_ports = 2u;
    xhci.op_base = mock_op;
    portsc_p0 = (volatile uint32_t *)(mock_op + 0x400);
    portsc_p1 = (volatile uint32_t *)(mock_op + 0x400 + 16);
    /* Seed port 0 with CSC=1 + CCS=1 + PED=1 + PEC=1. After ack:
     * CSC must clear, PEC must remain (other RW1C preserved), CCS
     * and PED must remain (RW bits preserved). */
    *portsc_p0 = XHCI_PORTSC_CCS | XHCI_PORTSC_PED | XHCI_PORTSC_CSC |
                 XHCI_PORTSC_PEC;
    /* Seed port 1 with NO CSC but a PRC bit set. Ack should be no-op
     * for CSC and must NOT clear PRC. */
    *portsc_p1 = XHCI_PORTSC_CCS | XHCI_PORTSC_PRC;
    if (xhci_port_ack_csc(&xhci, 0) != 0) {
        fail("ack_csc must succeed on valid port");
        return;
    }
    after_p0 = *portsc_p0;
    /* CSC must be cleared by the RW1C write (writing 1 to CSC in
     * mock memory turns into "we wrote 1", so test memory now has
     * CSC=1 still — but the real semantics is that the controller
     * would clear it. For host-test, we verify the WRITE pattern:
     * the value we wrote should have CSC=1 (to ack) and other
     * change bits=0. Read back reflects what we wrote. */
    if (!(after_p0 & XHCI_PORTSC_CSC)) {
        fail("written value must set CSC=1 to ack via RW1C");
    }
    if (after_p0 & XHCI_PORTSC_PEC) {
        fail("written value must NOT include PEC (would clear it via RW1C)");
    }
    if (!(after_p0 & XHCI_PORTSC_CCS) || !(after_p0 & XHCI_PORTSC_PED)) {
        fail("RW bits CCS/PED must be preserved in the write");
    }
    /* Port 1 untouched. */
    after_p1 = *portsc_p1;
    if (!(after_p1 & XHCI_PORTSC_PRC)) {
        fail("ack on port 0 must not touch port 1");
    }
    if (!(after_p1 & XHCI_PORTSC_CCS)) {
        fail("ack on port 0 must not corrupt port 1 RW bits");
    }
}

int run_xhci_release_slot_tests(void) {
    g_failures = 0;
    test_release_slot_rejects_invalid_inputs();
    test_release_slot_frees_addressed_state();
    test_release_slot_frees_configured_state();
    test_release_slot_clears_pending_latches();
    test_release_slot_tolerates_disable_failure();
    test_release_slot_idempotent_on_clean_slot();
    test_port_ack_csc_rejects_invalid_inputs();
    test_port_ack_csc_clears_only_csc_bit();
    if (g_failures == 0) printf("[tests] xhci_release_slot OK\n");
    return g_failures;
}
