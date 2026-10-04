#ifndef CAPYOS_DRIVERS_AUDIO_AC97_H
#define CAPYOS_DRIVERS_AUDIO_AC97_H

#include <stddef.h>
#include <stdint.h>

/* Intel ICH-style AC'97 controller (PCI class 0x04 / subclass 0x01) driven
 * through its NAM/NABM I/O BARs. Same shape as drivers/audio/hda.h so the
 * audio service can select either backend: one 64 KiB cyclic ring of sixteen
 * 4096-byte fragments, polled completion, single owner. */

enum ac97_runtime_state {
    AC97_STATE_UNINITIALIZED = 0,
    AC97_STATE_UNAVAILABLE,
    AC97_STATE_READY,
    AC97_STATE_PLAYING,
    AC97_STATE_FAILED
};

struct ac97_runtime_status {
    enum ac97_runtime_state state;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t codec_vendor; /* NAM 0x7C:0x7E vendor ID words. */
    uint32_t buffer_bytes;
    uint32_t position_bytes;
    /* The controller has no wall clock: this is TSC-derived, scaled to the
     * HDA WALLCLK domain (24 MHz) and wraps modulo 2^32. */
    uint32_t wallclock_ticks;
    uint16_t stream_status; /* PO_SR snapshot: DCH/CELV/LVBCI/BCIS/FIFOE. */
    int last_error;
};

/* Initializes the first PCI AC'97-compatible audio controller. */
int ac97_init(void);

/* Starts the looping 48 kHz stereo S16LE ring. The descriptor list always
 * spans the full 64 KiB: a shorter source is repeated to fill the ring, so it
 * loops at its own length and is re-phased once per ring wrap. Gapless
 * playback uses the streaming refill path with a full ring. */
int ac97_play_stereo_s16(const int16_t *samples, size_t frame_count);
void ac97_stop(void);
/* IRQ-safe stop request only: no waits/reclamation. Task-context ac97_stop()
 * must subsequently confirm ownership before any DMA reuse. */
void ac97_request_stop(void);
/* Refill a retired 4096-byte fragment of the 64 KiB stream ring. Single owner;
 * caller must prove retirement and poll frequently enough to avoid underrun.
 * Also keeps the descriptor engine running by trailing LVI behind CIV. */
int ac97_refill_fragment(uint32_t fragment, const int16_t *stereo_samples);
/* Samples status, keeps LVI trailing CIV and acknowledges observed buffer
 * completions in polling mode. */
int ac97_get_status(struct ac97_runtime_status *status);
const char *ac97_state_name(enum ac97_runtime_state state);

#endif
