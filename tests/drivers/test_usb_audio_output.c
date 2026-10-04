#include "drivers/audio/usb_audio_output.h"
#include "drivers/usb/usb_core.h"
#include "drivers/usb/xhci_iso.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
extern void *aligned_alloc(size_t alignment, size_t size); /* hosted harness */

static struct xhci_controller controller;
static struct xhci_trb commands[256];
static uint32_t device_context[512], doorbells[32];
static int16_t samples[32768];
static unsigned guard, allocations, frees, controls, completions;
static unsigned failure_command, stale_command, queued_command, command_count;
static uint64_t expected_byte;
static int disconnected, iso_error;

void scheduler_preempt_disable(void) { ++guard; }
void scheduler_preempt_enable(void) { assert(guard); --guard; }
uint64_t x64_timebase_hz(void) { return 1000000000u; }
void com1_puts(const char *s) { (void)s; }
void xhci_memzero(void *p, uint64_t n) { memset(p, 0, n); }
void xhci_memcpy(void *d, const void *s, uint64_t n) { memcpy(d, s, n); }
void *kmalloc_aligned(uint64_t n, uint64_t alignment) {
    assert(guard);
    void *result = aligned_alloc(alignment, (n + alignment - 1) & ~(alignment - 1));
    assert(result); ++allocations; return result;
}
void kfree_aligned(void *p) { assert(guard); ++frees; free(p); }
void usb_core_init(void) { assert(guard); }
struct xhci_controller *usb_core_controller(void) { assert(guard); return &controller; }
int usb_enumerate_devices(void) { assert(guard); return 1; }
int usb_get_device_count(void) { return 1; }
int usb_get_device(int index, struct usb_device_info *out) {
    assert(index == 0 && guard);
    *out = (struct usb_device_info){.slot_id = 1, .port = 0, .state = USB_DEV_ADDRESSED,
        .audio_output = {.configuration = 1, .alternate = 1, .interface_number = 1,
            .endpoint = 1, .interval = 1, .max_packet = 192, .frequency_control = 1}};
    return 0;
}
int xhci_control_transfer(struct xhci_controller *c, uint8_t slot,
    const struct usb_setup_packet *setup, void *data, uint16_t length, int input) {
    assert(c == &controller && slot == 1 && guard && !input);
    if (controls == 0) assert(setup->bRequest == USB_REQ_SET_CONFIGURATION && setup->wValue == 1);
    if (controls == 1) assert(setup->bmRequestType == 1 && setup->bRequest == 11 && setup->wIndex == 1);
    if (controls == 2) assert(setup->bmRequestType == 0x22 && setup->wValue == 0x100 &&
        length == 3 && !memcmp(data, "\x80\xbb\0", 3));
    ++controls;
    return 0;
}
int xhci_ring_command(struct xhci_controller *c, struct xhci_trb *trb) {
    assert(c == &controller && guard);
    queued_command = (trb->control >> 10) & 63u;
    ++command_count;
    if (queued_command == 16) assert((trb->param & 63u) == 1);
    c->cmd_pending.command_pointer = (uintptr_t)&commands[c->cmd_ring_idx++];
    if (queued_command == stale_command) c->cmd_pending.command_pointer += 16;
    return 0;
}
int xhci_wait_command_completion(struct xhci_controller *c, uint8_t *slot) {
    assert(c == &controller && !slot && guard);
    return queued_command == failure_command ? -3 : 0;
}
int xhci_event_try_lock(struct xhci_controller *c) {
    if (c->event_lock) return 0;
    c->event_lock = 1; return 1;
}
void xhci_event_unlock(struct xhci_controller *c) { assert(c->event_lock); c->event_lock = 0; }
int xhci_port_get_status(struct xhci_controller *c, int port) {
    assert(c == &controller && port == 0); return disconnected ? 0 : XHCI_PORTSC_CCS;
}
void xhci_event_pump_locked(struct xhci_controller *c) {
    assert(c->event_lock);
    struct xhci_iso_queue *q = c->iso_queue;
    if (!q || q->failed) return;
    if (iso_error) { q->failed = 1; return; }
    for (unsigned i = 0; i < completions && q->pending; ++i) {
        struct xhci_trb *trb = &q->ring[q->consumer];
        const uint8_t *packet = (const uint8_t *)(uintptr_t)trb->param;
        for (unsigned j = 0; j < 192; ++j)
            assert(packet[j] == ((uint8_t *)samples)[(expected_byte + j) % sizeof(samples)]);
        expected_byte += 192;
        assert(xhci_iso_complete(q, q->ring_dma + q->consumer * 16u, 1, 0) == 0);
    }
}

int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "normal";
    controller.initialized = 1; controller.context_size = 32;
    controller.cmd_ring = commands; controller.db_base = (void *)doorbells;
    controller.device_contexts[1] = device_context; device_context[0] = 1u << 20;
    for (unsigned i = 0; i < 32768; ++i) samples[i] = (int16_t)(i * 17u);
    if (!strcmp(mode, "configure-timeout")) failure_command = TRB_TYPE_CONFIG_EP;
    int rc = usb_audio_output.init();
    assert(allocations == 3 && controls == 3 && !guard);
    if (failure_command) {
        assert(rc == -1 && frees == 0);
        assert(usb_audio_output.init() == -1 && allocations == 3);
        puts("[usb-output] configure timeout retains DMA: PASS"); return 0;
    }
    assert(rc == 0);
    assert(usb_audio_output.play_stereo_s16(samples, 16384) == 0);
    assert(controller.iso_queue->pending == 32);
    completions = 2;
    struct audio_output_status status;
    for (unsigned i = 0; i < 600; ++i) {
        assert(usb_audio_output.get_status(&status) == 0);
        assert(status.state == AUDIO_OUTPUT_PLAYING && !status.stream_error);
        assert(status.position_bytes == expected_byte % sizeof(samples));
        assert(controller.iso_queue->pending == 32);
    }
    assert(usb_audio_output.refill_fragment(16, samples) == -1);
    if (!strcmp(mode, "disconnect")) disconnected = 1;
    if (!strcmp(mode, "transfer-error")) iso_error = 1;
    if (disconnected || iso_error) {
        assert(usb_audio_output.get_status(&status) == 0 && status.stream_error);
        assert(status.state == AUDIO_OUTPUT_FAILED && frees == 0);
        puts("[usb-output] failure stops submission, retains DMA: PASS"); return 0;
    }
    if (!strcmp(mode, "stop-timeout")) failure_command = 15;
    if (!strcmp(mode, "stale-stop")) stale_command = 15;
    usb_audio_output.request_stop();
    assert(usb_audio_output.get_status(&status) == 0 && controller.iso_queue->pending == 30);
    uint16_t producer = controller.iso_queue->producer;
    usb_audio_output.stop();
    assert(!guard && frees == 0);
    assert(usb_audio_output.get_status(&status) == 0);
    if (failure_command || stale_command) {
        assert(status.state == AUDIO_OUTPUT_FAILED && controller.iso_queue->producer == producer);
        assert(usb_audio_output.play_stereo_s16(samples, 16384) == -1);
    } else {
        assert(status.state == AUDIO_OUTPUT_READY && command_count == 3);
        assert(controller.iso_queue->pending == 0 && controller.iso_queue->producer == 0);
        expected_byte = 0;
        assert(usb_audio_output.play_stereo_s16(samples, 16384) == 0);
        assert(usb_audio_output.get_status(&status) == 0 && status.position_bytes == 384);
        usb_audio_output.stop();
    }
    printf("[usb-output] %s: packet copies, >4 wraps, stop ownership: PASS\n", mode);
    return 0;
}
