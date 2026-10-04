#include "drivers/audio/usb_audio_output.h"
#include "drivers/usb/usb_core.h"
#include "drivers/usb/xhci_iso.h"
#include "drivers/usb/internal/xhci_internal.h"
#include "arch/x86_64/timebase.h"
#include "kernel/scheduler.h"
#include "drivers/serial/serial_com1.h"

extern void *kmalloc_aligned(uint64_t size, uint64_t alignment);
extern void kfree_aligned(void *ptr);

/* One UAC1 full-speed output, 48 kHz stereo S16. Packet copies are separate
 * from the mixer ring: a submitted TRB owns its payload until completion.
 * Task operations guard BSP dispatch. IRQ/status takes only the event gate.
 * DMA allocations are permanent after publication, including every failure. */
#define PACKET_STRIDE 1024u
#define QUEUE_AHEAD 32u /* 32 ms, beyond the 10 ms service tick. */
static struct {
    struct xhci_controller *controller;
    struct xhci_iso_queue queue;
    struct usb_device_info device;
    void *input;
    uint8_t *payload;
    uint8_t pcm[AUDIO_OUTPUT_RING_BYTES];
    uint64_t queued, tsc_start, tsc_hz;
    uint32_t pcm_bytes;
    uint8_t dci, attempted, submitted, configured, stopping;
    struct audio_output_status status;
} output;

static uint64_t ticks(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
static uint32_t wallclock(void) {
    if (!output.tsc_hz) return 0;
    uint64_t elapsed = ticks() - output.tsc_start;
    return (uint32_t)((elapsed / output.tsc_hz) * AUDIO_OUTPUT_WALLCLOCK_HZ +
        (elapsed % output.tsc_hz) * AUDIO_OUTPUT_WALLCLOCK_HZ / output.tsc_hz);
}
static int fail(void) {
    output.stopping = 1;
    output.status.state = AUDIO_OUTPUT_FAILED;
    output.status.stream_error = 1;
    output.status.last_error = -1;
    return -1;
}

/* A stale command completion cannot authorize reuse of DMA storage. */
static int command(unsigned type, uint64_t parameter, unsigned endpoint) {
    struct xhci_controller *xhci = output.controller;
    if (!xhci || !xhci->cmd_ring) return -1;
    unsigned index = xhci->cmd_ring_idx;
    if (index >= XHCI_CMD_RING_TRBS - 1u) index = 0;
    uint64_t expected = (uintptr_t)&xhci->cmd_ring[index];
    __atomic_store_n(&xhci->cmd_pending.valid, 0u, __ATOMIC_RELEASE);
    struct xhci_trb trb = {.param = parameter,
        .control = (type << 10) | ((uint32_t)output.device.slot_id << 24) |
                   (endpoint << 16)};
    __atomic_thread_fence(__ATOMIC_RELEASE);
    if (xhci_ring_command(xhci, &trb) ||
        xhci_wait_command_completion(xhci, NULL) ||
        xhci->cmd_pending.command_pointer != expected) return -1;
    return 0;
}

static int control(uint8_t type, uint8_t request, uint16_t value,
                   uint16_t index, void *data, uint16_t length) {
    struct usb_setup_packet setup = {type, request, value, index, length};
    return xhci_control_transfer(output.controller, output.device.slot_id,
                                 &setup, data, length, 0);
}

static int initialize(void) {
    scheduler_preempt_disable();
    if (output.attempted) {
        int rc = output.status.state == AUDIO_OUTPUT_READY ? 0 : -1;
        scheduler_preempt_enable();
        return rc;
    }
    output.attempted = 1;
    usb_core_init();
    output.controller = usb_core_controller();
    if (!output.controller || !x64_timebase_hz()) goto failed;
    (void)usb_enumerate_devices();
    for (int i = 0; i < usb_get_device_count(); ++i) {
        struct usb_device_info device;
        if (usb_get_device(i, &device) == 0 && device.audio_output.alternate &&
            (device.state == USB_DEV_ADDRESSED || device.state == USB_DEV_CONFIGURED)) {
            output.device = device;
            break;
        }
    }
    if (!output.device.slot_id) goto failed;
    const struct usb_audio_format *format = &output.device.audio_output;
    /* Padding beyond the fixed packet and delayed-lock devices need additional
     * rate/settling policy; reject instead of changing the 48 kHz clock. */
    if (format->lock_delay ||
        (format->pad_packets && format->max_packet != USB_AUDIO_PACKET_BYTES)) goto failed;
    output.input = kmalloc_aligned(4096u, 4096u);
    output.payload = kmalloc_aligned(XHCI_ISO_DATA_TRBS * PACKET_STRIDE, 65536u);
    struct xhci_trb *ring = kmalloc_aligned(4096u, 4096u);
    if (!output.input || !output.payload || !ring) {
        if (ring) kfree_aligned(ring);
        if (output.input) kfree_aligned(output.input);
        if (output.payload) kfree_aligned(output.payload);
        output.input = NULL; output.payload = NULL;
        goto failed;
    }
    if (xhci_iso_queue_init(&output.queue, ring, (uintptr_t)ring)) goto failed;
    output.dci = format->endpoint * 2u;
    if (output.device.state != USB_DEV_CONFIGURED &&
        control(0, USB_REQ_SET_CONFIGURATION, format->configuration, 0, NULL, 0)) goto failed;
    if (control(1, 11, format->alternate, format->interface_number, NULL, 0)) goto failed;
    uint8_t rate[3] = {0x80, 0xbb, 0};
    if (format->frequency_control &&
        control(0x22, 1, 0x0100, format->endpoint, rate, sizeof(rate))) goto failed;
    if (xhci_build_iso_out_context(output.input, output.controller->context_size,
        output.controller->device_contexts[output.device.slot_id],
        format->endpoint, format->max_packet, (uintptr_t)ring)) goto failed;
    if (!xhci_event_try_lock(output.controller)) goto failed;
    if (output.controller->iso_queue) {
        xhci_event_unlock(output.controller);
        goto failed;
    }
    output.controller->iso_queue = &output.queue;
    output.controller->iso_slot = output.device.slot_id;
    output.controller->iso_dci = output.dci;
    xhci_event_unlock(output.controller);
    if (command(TRB_TYPE_CONFIG_EP, (uintptr_t)output.input, 0)) goto failed;
    output.configured = 1;
    output.tsc_hz = x64_timebase_hz();
    output.tsc_start = ticks();
    output.status.state = AUDIO_OUTPUT_READY;
    output.status.buffer_bytes = AUDIO_OUTPUT_RING_BYTES;
    com1_puts("[usb-audio] isoch endpoint configured\n");
    scheduler_preempt_enable();
    return 0;
failed:
    fail();
    scheduler_preempt_enable();
    return -1;
}

/* Event gate held. At most QUEUE_AHEAD packets, bounded copies, no allocation. */
static int queue_packets(void) {
    while (output.queue.pending < QUEUE_AHEAD) {
        uint8_t *packet = output.payload + output.queue.producer * PACKET_STRIDE;
        for (unsigned i = 0; i < USB_AUDIO_PACKET_BYTES; ++i)
            packet[i] = output.pcm[(output.queued + i) % output.pcm_bytes];
        if (xhci_iso_enqueue(&output.queue, (uintptr_t)packet, USB_AUDIO_PACKET_BYTES))
            return fail();
        output.queued += USB_AUDIO_PACKET_BYTES;
    }
    __atomic_thread_fence(__ATOMIC_RELEASE);
    mmio_write32(output.controller->db_base + output.device.slot_id * 4u, output.dci);
    output.submitted = 1;
    return 0;
}

static void request_stop(void) { output.stopping = 1; }
static void stop(void) {
    scheduler_preempt_disable();
    output.stopping = 1;
    if (!output.configured || output.status.state == AUDIO_OUTPUT_FAILED) goto done;
    if (!output.submitted) { output.status.state = AUDIO_OUTPUT_READY; goto done; }
    /* Stop Endpoint returns ownership before any TRB/payload can be reused.
     * Stopped transfer events during the command are not successful packets. */
    if (!xhci_event_try_lock(output.controller)) { fail(); goto done; }
    output.queue.failed = 1;
    xhci_event_unlock(output.controller);
    if (command(15u, 0, output.dci)) { fail(); goto done; }
    if (!xhci_event_try_lock(output.controller)) { fail(); goto done; }
    xhci_event_pump_locked(output.controller);
    struct xhci_trb *ring = output.queue.ring;
    int rc = xhci_iso_queue_init(&output.queue, ring, (uintptr_t)ring);
    xhci_event_unlock(output.controller);
    if (rc || command(16u, (uintptr_t)ring | 1u, output.dci)) { fail(); goto done; }
    output.submitted = 0;
    output.status.state = AUDIO_OUTPUT_READY;
done:
    scheduler_preempt_enable();
}

static int play(const int16_t *samples, size_t frames) {
    if (!samples || !frames || frames > AUDIO_OUTPUT_RING_BYTES / 4u) return -1;
    scheduler_preempt_disable();
    stop();
    if (output.status.state != AUDIO_OUTPUT_READY) goto failed;
    output.pcm_bytes = (uint32_t)frames * 4u;
    xhci_memcpy(output.pcm, samples, output.pcm_bytes);
    output.queued = 0; output.stopping = 0;
    output.status.position_bytes = 0;
    output.status.buffer_bytes = output.pcm_bytes;
    if (!xhci_event_try_lock(output.controller)) goto failed;
    int rc = output.controller->iso_queue == &output.queue ? queue_packets() : -1;
    xhci_event_unlock(output.controller);
    if (rc) goto failed;
    output.status.state = AUDIO_OUTPUT_PLAYING;
    scheduler_preempt_enable();
    return 0;
failed:
    fail();
    scheduler_preempt_enable();
    return -1;
}

static int refill(uint32_t fragment, const int16_t *samples) {
    if (!samples || fragment >= AUDIO_OUTPUT_RING_BYTES / AUDIO_OUTPUT_FRAGMENT_BYTES ||
        output.status.state != AUDIO_OUTPUT_PLAYING ||
        output.pcm_bytes != AUDIO_OUTPUT_RING_BYTES || output.stopping) return -1;
    xhci_memcpy(output.pcm + fragment * AUDIO_OUTPUT_FRAGMENT_BYTES,
                 samples, AUDIO_OUTPUT_FRAGMENT_BYTES);
    return 0;
}
static int status(struct audio_output_status *result) {
    if (!result) return -1;
    if (output.status.state == AUDIO_OUTPUT_PLAYING &&
        xhci_event_try_lock(output.controller)) {
        xhci_event_pump_locked(output.controller);
        int port = xhci_port_get_status(output.controller, output.device.port);
        if (output.controller->iso_queue != &output.queue || output.queue.failed ||
            port < 0 || !(port & XHCI_PORTSC_CCS)) fail();
        else {
            output.status.position_bytes = output.queue.completed_bytes % output.pcm_bytes;
            if (!output.stopping) (void)queue_packets();
        }
        xhci_event_unlock(output.controller);
    }
    output.status.wallclock_ticks = wallclock();
    *result = output.status;
    return 0;
}

const struct audio_output usb_audio_output = {
    .kind = AUDIO_OUTPUT_USB, .name = "usb-uac1", .init = initialize,
    .play_stereo_s16 = play, .refill_fragment = refill,
    .request_stop = request_stop, .stop = stop, .get_status = status,
};
