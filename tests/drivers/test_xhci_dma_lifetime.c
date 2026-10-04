#include "drivers/usb/xhci.h"
#include "drivers/usb/usb_core.h"
#include "internal/test_xhci_helpers.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

extern void *kmalloc_aligned(uint64_t, uint64_t);

static struct xhci_controller controller;
static struct xhci_trb commands[XHCI_CMD_RING_TRBS];
static struct xhci_trb events[XHCI_EVT_RING_TRBS];
static uint32_t doorbells[16];
static uint64_t dcbaa[9];

static void fixture(void) {
    memset(&controller, 0, sizeof(controller));
    memset(commands, 0, sizeof(commands));
    memset(events, 0, sizeof(events));
    memset(doorbells, 0, sizeof(doorbells));
    memset(dcbaa, 0, sizeof(dcbaa));
    controller.initialized = 1;
    controller.max_slots = 8;
    controller.cmd_ring = commands;
    controller.cmd_ring_cycle = 1;
    controller.evt_ring = events;
    controller.evt_ring_cycle = 1;
    controller.db_base = (volatile uint8_t *)doorbells;
    controller.dcbaa = dcbaa;
    controller.ep0_rings[1] = kmalloc_aligned(sizeof(commands), 64);
    assert(controller.ep0_rings[1]);
    memset(controller.ep0_rings[1], 0, sizeof(commands));
    controller.ep0_ring_cycle[1] = 1;
    controller.context_size = 32;
    controller.device_contexts[1] = kmalloc_aligned(64u * 32u, 64);
    assert(controller.device_contexts[1]);
    memset(controller.device_contexts[1], 0, 64u * 32u);
    ((uint32_t *)controller.device_contexts[1])[0] = 1u << 20;
    ((uint32_t *)controller.device_contexts[1])[9] = 8u << 16;
}

static void dispose(void) {
    seed_command_completion(events, controller.evt_ring_idx, 1, &commands[controller.cmd_ring_idx]);
    assert(xhci_release_slot(&controller, 1) == 0);
    assert(!controller.ep0_buffers[1] && !controller.ep0_rings[1]);
    assert(!controller.ep0_failed[1] && !controller.ep0_busy[1]);
}

int main(void) {
    struct usb_setup_packet input = {0x80, 6, 0x100, 0, 8};
    struct usb_setup_packet output = {0x21, 9, 0x200, 0, 8};
    uint8_t caller[8];
    fixture();
    memset(caller, 0x7a, sizeof(caller));
    assert(xhci_control_transfer(&controller, 1, &input, caller, 8, 1) == -3);
    uint8_t *retained = controller.ep0_buffers[1];
    assert(retained && !((uintptr_t)retained & 4095u));
    assert(controller.ep0_rings[1][1].param == (uintptr_t)retained);
    memset(retained, 0xee, 8); /* late DMA after synchronous timeout */
    for (unsigned i = 0; i < 8; ++i) assert(caller[i] == 0x7a);
    uint32_t queued = controller.ep0_ring_idx[1];
    assert(controller.ep0_failed[1]);
    assert(xhci_control_transfer(&controller, 1, &input, caller, 8, 1) == -4);
    assert(controller.ep0_ring_idx[1] == queued && retained[0] == 0xee);
    seed_command_completion(events, 0, 1, &commands[controller.cmd_ring_idx]);
    events[0].status = 5u << 24;
    assert(xhci_release_slot(&controller, 1) == -2);
    assert(controller.ep0_buffers[1] == retained && controller.ep0_failed[1]);
    assert(!controller.ep0_busy[1]);
    dispose();

    fixture();
    seed_transfer_event(events, 0, 1, 1);
    memset(caller, 0x35, sizeof(caller));
    assert(xhci_control_transfer(&controller, 1, &output, caller, 8, 0) == 0);
    retained = controller.ep0_buffers[1];
    assert(memcmp(retained, caller, 8) == 0);
    caller[0] = 0;
    assert(retained[0] == 0x35);
    seed_transfer_event(events, 1, 1, 1);
    assert(xhci_control_transfer(&controller, 1, &input, caller, 8, 1) == 0);
    assert(controller.ep0_buffers[1] == retained);
    for (unsigned i = 0; i < 8; ++i) assert(!caller[i]);
    controller.ep0_busy[1] = 1;
    assert(xhci_control_transfer(&controller, 1, &input, caller, 8, 1) == -4);
    assert(xhci_release_slot(&controller, 1) == -4);
    controller.ep0_busy[1] = 0;
    assert(xhci_control_transfer(&controller, 1, &input, caller, 7, 1) == -1);
    assert(xhci_control_transfer(&controller, 1, &input, caller, 8, 0) == -1);
    input.wLength = 4097;
    assert(xhci_control_transfer(&controller, 1, &input, caller, 4097, 1) == -1);
    dispose();
    for (unsigned stride = 32; stride <= 64; stride += 32) {
        fixture();
        controller.context_size = stride;
        uint32_t *device = controller.device_contexts[1];
        device[stride / 4u + 1u] = 8u << 16;
        assert(xhci_update_ep0_packet_size(&controller, 1, 7) == -1);
        assert(!controller.ep0_buffers[1]);
        seed_command_completion(events, 0, 1, &commands[controller.cmd_ring_idx]);
        assert(xhci_update_ep0_packet_size(&controller, 1, 64) == 0);
        uint32_t *context = (uint32_t *)controller.ep0_buffers[1];
        assert(context && context[1] == 2u && context[stride / 2u + 1u] == 64u << 16);
        assert(commands[0].param == (uintptr_t)context);
        assert(((commands[0].control >> 10) & 63u) == TRB_TYPE_EVALUATE_CONTEXT);
        device[stride / 4u + 1u] = 64u << 16; /* emulate controller update */
        assert(xhci_update_ep0_packet_size(&controller, 1, 64) == 0);
        assert(controller.cmd_ring_idx == 1); /* already correct: no command */
        device[0] = 2u << 20;
        assert(xhci_update_ep0_packet_size(&controller, 1, 64) == -1);
        device[0] = 3u << 20;
        assert(xhci_update_ep0_packet_size(&controller, 1, 8) == -1);
        device[0] = 4u << 20;
        assert(xhci_update_ep0_packet_size(&controller, 1, 64) == -1);
        seed_command_completion(events, 1, 1, &commands[controller.cmd_ring_idx]);
        assert(xhci_update_ep0_packet_size(&controller, 1, 9) == 0);
        assert(context[stride / 2u + 1u] == 512u << 16);
        dispose();
    }
    fixture();
    assert(xhci_update_ep0_packet_size(&controller, 1, 64) == -3);
    assert(controller.ep0_failed[1] && controller.ep0_buffers[1]);
    retained = controller.ep0_buffers[1];
    assert(xhci_update_ep0_packet_size(&controller, 1, 64) == -4);
    assert(retained == controller.ep0_buffers[1]);
    /* A late Evaluate Context success is not a Disable Slot acknowledgement. */
    seed_command_completion(events, 0, 1, &commands[0]);
    assert(xhci_release_slot(&controller, 1) == -3);
    assert(controller.ep0_buffers[1] == retained && controller.ep0_rings[1]);
    assert(controller.event_stray_count == 1);
    dispose();
    fixture();
    struct usb_endpoint_info endpoint = {0x81, 3, 8, 1};
    assert(xhci_configure_interrupt_endpoint(&controller, 1, &endpoint, 8) == -3);
    assert(controller.ep0_failed[1] && controller.ep0_buffers[1]);
    assert(controller.intr_rings[1] && controller.intr_buffers[1]);
    retained = controller.ep0_buffers[1];
    seed_command_completion(events, 0, 1, &commands[0]);
    assert(xhci_release_slot(&controller, 1) == -3);
    assert(controller.ep0_buffers[1] == retained && controller.intr_rings[1]);
    dispose();
    assert(!controller.intr_rings[1] && !controller.intr_buffers[1]);

    fixture();
    dispose(); /* Start Address Device with an enabled but empty slot. */
    uint32_t registers[272] = {0};
    registers[0x400 / 4] = XHCI_PORTSC_CCS | XHCI_PORTSC_PED | (1u << 10);
    controller.max_ports = 1;
    controller.op_base = (volatile uint8_t *)registers;
    unsigned address_index = controller.cmd_ring_idx;
    assert(xhci_address_device(&controller, 1, 0) == -3);
    retained = controller.ep0_buffers[1];
    assert(retained && controller.ep0_rings[1] && controller.device_contexts[1]);
    assert(controller.ep0_failed[1]);
    seed_command_completion(events, controller.evt_ring_idx, 1, &commands[address_index]);
    assert(xhci_release_slot(&controller, 1) == -3);
    assert(controller.ep0_buffers[1] == retained && controller.device_contexts[1]);
    dispose();
    puts("[xhci-dma] EP0, address/configure timeout, stale ACK, quarantine, MPS: PASS");
    return 0;
}
