#ifndef CAPYOS_DRIVERS_AUDIO_HDA_H
#define CAPYOS_DRIVERS_AUDIO_HDA_H

#include <stddef.h>
#include <stdint.h>

enum hda_runtime_state {
    HDA_STATE_UNINITIALIZED = 0,
    HDA_STATE_UNAVAILABLE,
    HDA_STATE_READY,
    HDA_STATE_PLAYING,
    HDA_STATE_FAILED
};

struct hda_runtime_status {
    enum hda_runtime_state state;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t codec_address;
    uint8_t output_node;
    uint8_t pin_node;
    uint32_t buffer_bytes;
    uint32_t position_bytes;
    uint32_t wallclock_ticks; /* HDA WALLCLK, 24 MHz, wraps modulo 2^32. */
    uint8_t stream_status;
    int last_error;
};

/* Initializes the first PCI High Definition Audio controller. */
int hda_init(void);

/* Starts a bounded, looping 48 kHz stereo S16LE DMA buffer. */
int hda_play_stereo_s16(const int16_t *samples, size_t frame_count);
void hda_stop(void);
/* IRQ-safe stop request only: no waits/reclamation. Task-context hda_stop()
 * must subsequently confirm ownership before any DMA reuse. */
void hda_request_stop(void);
/* Refill a retired 4096-byte fragment of the 64 KiB stream ring. Single owner;
 * caller must prove retirement and poll frequently enough to avoid underrun. */
int hda_refill_fragment(uint32_t fragment, const int16_t *stereo_samples);
/* Samples status and acknowledges observed buffer completion in polling mode. */
int hda_get_status(struct hda_runtime_status *status);
const char *hda_state_name(enum hda_runtime_state state);

#endif
