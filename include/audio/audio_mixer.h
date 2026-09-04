#ifndef CAPYOS_AUDIO_MIXER_H
#define CAPYOS_AUDIO_MIXER_H

#include <stddef.h>
#include <stdint.h>

#define AUDIO_MIXER_MAX_STREAMS 16u
#define AUDIO_MIXER_VOLUME_MAX 1000u

enum audio_mixer_result {
    AUDIO_MIXER_OK = 0,
    AUDIO_MIXER_INVALID = -1,
    AUDIO_MIXER_NO_SLOT = -2,
    AUDIO_MIXER_BUSY = -3,
    AUDIO_MIXER_NOT_FOUND = -4
};

struct audio_mixer_stream {
    uint32_t app_id;
    uint16_t channels;
    uint16_t volume;
    const int16_t *samples;
    size_t frame_count;
    size_t frame_cursor;
    uint8_t active;
};

struct audio_mixer {
    struct audio_mixer_stream streams[AUDIO_MIXER_MAX_STREAMS];
    uint16_t global_volume;
    uint64_t mixed_frames;
    uint64_t underrun_frames;
};

/*
 * The mixer is intentionally allocation-free and single-owner. The runtime
 * adapter must serialize calls made by application and device contexts.
 * Submitted sample storage remains owned by the caller and must stay valid
 * until audio_mixer_stream_pending_frames() reaches zero.
 */
void audio_mixer_init(struct audio_mixer *mixer);
int audio_mixer_open(struct audio_mixer *mixer,
                     uint32_t app_id,
                     uint16_t channels,
                     uint32_t *stream_id);
int audio_mixer_close(struct audio_mixer *mixer, uint32_t stream_id);
int audio_mixer_submit(struct audio_mixer *mixer,
                       uint32_t stream_id,
                       const int16_t *samples,
                       size_t frame_count);
int audio_mixer_set_global_volume(struct audio_mixer *mixer, uint16_t volume);
int audio_mixer_set_stream_volume(struct audio_mixer *mixer,
                                  uint32_t stream_id,
                                  uint16_t volume);
int audio_mixer_set_app_volume(struct audio_mixer *mixer,
                               uint32_t app_id,
                               uint16_t volume);
size_t audio_mixer_stream_pending_frames(const struct audio_mixer *mixer,
                                         uint32_t stream_id);
size_t audio_mixer_mix_stereo_s16(struct audio_mixer *mixer,
                                  int16_t *output,
                                  size_t frame_count);

#endif
