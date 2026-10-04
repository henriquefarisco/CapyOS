#include "audio/audio_output.h"

#include "drivers/audio/ac97.h"
#include "drivers/audio/ac97_core.h"
#include "drivers/audio/hda.h"
#include "drivers/audio/hda_core.h"
#ifdef CAPYOS_HAVE_USB_AUDIO_OUTPUT
#include "drivers/audio/usb_audio_output.h"
#endif

/* Both drivers already implement the shared ring; keep the contract honest at
 * compile time so a future driver cannot silently change the geometry. */
_Static_assert(HDA_BDL_FRAGMENT_BYTES == AUDIO_OUTPUT_FRAGMENT_BYTES,
               "HDA fragment size must match the audio output contract");
_Static_assert(HDA_DMA_BYTES == AUDIO_OUTPUT_RING_BYTES,
               "HDA ring size must match the audio output contract");
_Static_assert(AC97_BDL_FRAGMENT_BYTES == AUDIO_OUTPUT_FRAGMENT_BYTES,
               "AC'97 fragment size must match the audio output contract");
_Static_assert(AC97_RING_BYTES == AUDIO_OUTPUT_RING_BYTES,
               "AC'97 ring size must match the audio output contract");
_Static_assert(AC97_WALLCLOCK_HZ == AUDIO_OUTPUT_WALLCLOCK_HZ,
               "AC'97 wallclock domain must match the audio output contract");

/* HDA SDSTS: bit 3 DESE (descriptor error), bit 4 FIFOE. */
#define HDA_SD_STS_FATAL 0x18u

static int hda_output_get_status(struct audio_output_status *status) {
    struct hda_runtime_status hardware;
    if (!status || hda_get_status(&hardware) != 0) return -1;
    switch (hardware.state) {
        case HDA_STATE_READY: status->state = AUDIO_OUTPUT_READY; break;
        case HDA_STATE_PLAYING: status->state = AUDIO_OUTPUT_PLAYING; break;
        case HDA_STATE_FAILED: status->state = AUDIO_OUTPUT_FAILED; break;
        default: status->state = AUDIO_OUTPUT_UNAVAILABLE; break;
    }
    status->buffer_bytes = hardware.buffer_bytes;
    status->position_bytes = hardware.position_bytes;
    status->wallclock_ticks = hardware.wallclock_ticks;
    status->stream_error = (hardware.stream_status & HDA_SD_STS_FATAL) != 0;
    status->last_error = hardware.last_error;
    /* VMware's WAVE backend opens with a short initial starvation interval and
     * prefetches nine 4096-byte buffers. Prime before source PCM and keep zeros
     * flowing through that queued tail before reporting EOF/closing the stream.
     * Other HDA models retain the zero-padding contract. */
    int vmware = hardware.vendor_id == 0x15adu && hardware.device_id == 0x1977u;
    status->startup_padding_bytes = vmware ? 3u * AUDIO_OUTPUT_FRAGMENT_BYTES : 0;
    status->drain_padding_bytes = vmware ? 9u * AUDIO_OUTPUT_FRAGMENT_BYTES : 0;
    return 0;
}

static int ac97_output_get_status(struct audio_output_status *status) {
    struct ac97_runtime_status hardware;
    if (!status || ac97_get_status(&hardware) != 0) return -1;
    switch (hardware.state) {
        case AC97_STATE_READY: status->state = AUDIO_OUTPUT_READY; break;
        case AC97_STATE_PLAYING: status->state = AUDIO_OUTPUT_PLAYING; break;
        case AC97_STATE_FAILED: status->state = AUDIO_OUTPUT_FAILED; break;
        default: status->state = AUDIO_OUTPUT_UNAVAILABLE; break;
    }
    status->buffer_bytes = hardware.buffer_bytes;
    status->position_bytes = hardware.position_bytes;
    status->wallclock_ticks = hardware.wallclock_ticks;
    /* PO_SR bit 3 is BCIS (a completion, acknowledged by the driver), not an
     * error: only a FIFO fault or an engine that halted while we believe it
     * is running are fatal for the stream. */
    status->stream_error =
        (hardware.stream_status & AC97_PO_SR_FIFOE) != 0 ||
        (hardware.state == AC97_STATE_PLAYING &&
         (hardware.stream_status & AC97_PO_SR_DCH) != 0);
    status->last_error = hardware.last_error;
    status->startup_padding_bytes = status->drain_padding_bytes = 0;
    return 0;
}

static const struct audio_output g_hda_output = {
    .kind = AUDIO_OUTPUT_HDA,
    .name = "hda",
    .init = hda_init,
    .play_stereo_s16 = hda_play_stereo_s16,
    .refill_fragment = hda_refill_fragment,
    .request_stop = hda_request_stop,
    .stop = hda_stop,
    .get_status = hda_output_get_status,
};

static const struct audio_output g_ac97_output = {
    .kind = AUDIO_OUTPUT_AC97,
    .name = "ac97",
    .init = ac97_init,
    .play_stereo_s16 = ac97_play_stereo_s16,
    .refill_fragment = ac97_refill_fragment,
    .request_stop = ac97_request_stop,
    .stop = ac97_stop,
    .get_status = ac97_output_get_status,
};

const struct audio_output *audio_output_select(void) {
    if (g_hda_output.init() == 0) return &g_hda_output;
    if (g_ac97_output.init() == 0) return &g_ac97_output;
#ifdef CAPYOS_HAVE_USB_AUDIO_OUTPUT
    if (usb_audio_output.init() == 0) return &usb_audio_output;
#endif
    return 0;
}
