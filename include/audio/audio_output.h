#ifndef CAPYOS_AUDIO_OUTPUT_H
#define CAPYOS_AUDIO_OUTPUT_H

#include <stddef.h>
#include <stdint.h>

/* Output backend contract between the audio service and its controller
 * drivers (Intel HDA, AC'97). Every backend exposes the same ring: 64 KiB of
 * 48 kHz stereo S16LE split into sixteen 4096-byte fragments, cyclic, polled
 * completion, single owner serialized by audio_runtime.c. The wallclock is a
 * 24 MHz counter (HDA WALLCLK or a TSC-derived equivalent) wrapping modulo
 * 2^32, so the service keeps one set of stall/gap thresholds. */
#define AUDIO_OUTPUT_FRAGMENT_BYTES 4096u
#define AUDIO_OUTPUT_RING_BYTES     65536u
#define AUDIO_OUTPUT_WALLCLOCK_HZ   24000000u

enum audio_output_kind {
    AUDIO_OUTPUT_NONE = 0,
    AUDIO_OUTPUT_HDA,
    AUDIO_OUTPUT_AC97,
    AUDIO_OUTPUT_USB
};

enum audio_output_state {
    AUDIO_OUTPUT_UNAVAILABLE = 0,
    AUDIO_OUTPUT_READY,
    AUDIO_OUTPUT_PLAYING,
    AUDIO_OUTPUT_FAILED
};

struct audio_output_status {
    enum audio_output_state state;
    uint32_t buffer_bytes;
    /* Includes buffer_bytes itself right before the ring wraps (CBL rule). */
    uint32_t position_bytes;
    uint32_t wallclock_ticks; /* AUDIO_OUTPUT_WALLCLOCK_HZ domain. */
    int stream_error;         /* Nonzero: FIFO/descriptor fault or halted engine. */
    int last_error;
    /* Optional streaming startup/drain padding, fragment-aligned and <= ring.
     * Source frame counts exclude it. Zero on backends without host buffering. */
    uint32_t startup_padding_bytes;
    uint32_t drain_padding_bytes;
};

struct audio_output {
    enum audio_output_kind kind;
    const char *name;
    int (*init)(void);
    int (*play_stereo_s16)(const int16_t *samples, size_t frame_count);
    /* IRQ-safe: PIO/MMIO and copies only, no allocation, logging or waits. */
    int (*refill_fragment)(uint32_t fragment, const int16_t *stereo_samples);
    void (*request_stop)(void); /* IRQ-safe. */
    void (*stop)(void);         /* Task context; may wait for the engine. */
    int (*get_status)(struct audio_output_status *status); /* IRQ-safe. */
};

/* Initializes and returns the first working backend, preferring Intel HDA
 * (the VMware-validated path) and falling back to AC'97. NULL when neither
 * controller is usable; the caller then disables audio safely. */
const struct audio_output *audio_output_select(void);

#endif
