#include "audio/audio_mixer.h"

static int valid_stream(const struct audio_mixer *mixer, uint32_t stream_id) {
    return mixer && stream_id < AUDIO_MIXER_MAX_STREAMS &&
           mixer->streams[stream_id].active;
}

static int16_t clamp_s16(int64_t value) {
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return (int16_t)value;
}

void audio_mixer_init(struct audio_mixer *mixer) {
    size_t i;
    if (!mixer) return;
    for (i = 0; i < AUDIO_MIXER_MAX_STREAMS; ++i) {
        mixer->streams[i].app_id = 0;
        mixer->streams[i].channels = 0;
        mixer->streams[i].volume = 0;
        mixer->streams[i].samples = 0;
        mixer->streams[i].frame_count = 0;
        mixer->streams[i].frame_cursor = 0;
        mixer->streams[i].active = 0;
    }
    mixer->global_volume = AUDIO_MIXER_VOLUME_MAX;
    mixer->mixed_frames = 0;
    mixer->underrun_frames = 0;
}

int audio_mixer_open(struct audio_mixer *mixer,
                     uint32_t app_id,
                     uint16_t channels,
                     uint32_t *stream_id) {
    size_t i;
    if (!mixer || !stream_id || app_id == 0 || (channels != 1 && channels != 2))
        return AUDIO_MIXER_INVALID;
    for (i = 0; i < AUDIO_MIXER_MAX_STREAMS; ++i) {
        struct audio_mixer_stream *stream = &mixer->streams[i];
        if (stream->active) continue;
        stream->app_id = app_id;
        stream->channels = channels;
        stream->volume = AUDIO_MIXER_VOLUME_MAX;
        stream->samples = 0;
        stream->frame_count = 0;
        stream->frame_cursor = 0;
        stream->active = 1;
        *stream_id = (uint32_t)i;
        return AUDIO_MIXER_OK;
    }
    return AUDIO_MIXER_NO_SLOT;
}

int audio_mixer_close(struct audio_mixer *mixer, uint32_t stream_id) {
    struct audio_mixer_stream *stream;
    if (!valid_stream(mixer, stream_id)) return AUDIO_MIXER_NOT_FOUND;
    stream = &mixer->streams[stream_id];
    stream->active = 0;
    stream->samples = 0;
    stream->frame_count = 0;
    stream->frame_cursor = 0;
    return AUDIO_MIXER_OK;
}

int audio_mixer_submit(struct audio_mixer *mixer,
                       uint32_t stream_id,
                       const int16_t *samples,
                       size_t frame_count) {
    struct audio_mixer_stream *stream;
    if (!valid_stream(mixer, stream_id)) return AUDIO_MIXER_NOT_FOUND;
    if (!samples || frame_count == 0) return AUDIO_MIXER_INVALID;
    stream = &mixer->streams[stream_id];
    if (stream->frame_cursor < stream->frame_count) return AUDIO_MIXER_BUSY;
    stream->samples = samples;
    stream->frame_count = frame_count;
    stream->frame_cursor = 0;
    return AUDIO_MIXER_OK;
}

int audio_mixer_set_global_volume(struct audio_mixer *mixer, uint16_t volume) {
    if (!mixer || volume > AUDIO_MIXER_VOLUME_MAX) return AUDIO_MIXER_INVALID;
    mixer->global_volume = volume;
    return AUDIO_MIXER_OK;
}

int audio_mixer_set_stream_volume(struct audio_mixer *mixer,
                                  uint32_t stream_id,
                                  uint16_t volume) {
    if (!valid_stream(mixer, stream_id)) return AUDIO_MIXER_NOT_FOUND;
    if (volume > AUDIO_MIXER_VOLUME_MAX) return AUDIO_MIXER_INVALID;
    mixer->streams[stream_id].volume = volume;
    return AUDIO_MIXER_OK;
}

int audio_mixer_set_app_volume(struct audio_mixer *mixer,
                               uint32_t app_id,
                               uint16_t volume) {
    size_t i;
    int found = 0;
    if (!mixer || app_id == 0 || volume > AUDIO_MIXER_VOLUME_MAX)
        return AUDIO_MIXER_INVALID;
    for (i = 0; i < AUDIO_MIXER_MAX_STREAMS; ++i) {
        if (mixer->streams[i].active && mixer->streams[i].app_id == app_id) {
            mixer->streams[i].volume = volume;
            found = 1;
        }
    }
    return found ? AUDIO_MIXER_OK : AUDIO_MIXER_NOT_FOUND;
}

size_t audio_mixer_stream_pending_frames(const struct audio_mixer *mixer,
                                         uint32_t stream_id) {
    const struct audio_mixer_stream *stream;
    if (!valid_stream(mixer, stream_id)) return 0;
    stream = &mixer->streams[stream_id];
    return stream->frame_count - stream->frame_cursor;
}

size_t audio_mixer_mix_stereo_s16(struct audio_mixer *mixer,
                                  int16_t *output,
                                  size_t frame_count) {
    size_t frame;
    if (!mixer || (!output && frame_count != 0)) return 0;
    for (frame = 0; frame < frame_count; ++frame) {
        int64_t left = 0;
        int64_t right = 0;
        size_t i;
        int contributed = 0;
        for (i = 0; i < AUDIO_MIXER_MAX_STREAMS; ++i) {
            struct audio_mixer_stream *stream = &mixer->streams[i];
            int64_t sample_left;
            int64_t sample_right;
            if (!stream->active || stream->frame_cursor >= stream->frame_count)
                continue;
            if (stream->channels == 1) {
                sample_left = stream->samples[stream->frame_cursor];
                sample_right = sample_left;
            } else {
                size_t sample = stream->frame_cursor * 2u;
                sample_left = stream->samples[sample];
                sample_right = stream->samples[sample + 1u];
            }
            left += (sample_left * stream->volume) / AUDIO_MIXER_VOLUME_MAX;
            right += (sample_right * stream->volume) / AUDIO_MIXER_VOLUME_MAX;
            stream->frame_cursor++;
            contributed = 1;
        }
        left = (left * mixer->global_volume) / AUDIO_MIXER_VOLUME_MAX;
        right = (right * mixer->global_volume) / AUDIO_MIXER_VOLUME_MAX;
        output[frame * 2u] = clamp_s16(left);
        output[frame * 2u + 1u] = clamp_s16(right);
        mixer->mixed_frames++;
        if (!contributed) mixer->underrun_frames++;
    }
    return frame_count;
}
