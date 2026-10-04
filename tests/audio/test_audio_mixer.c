#include "audio/audio_mixer.h"

#include <stdio.h>

#define CHECK(expr) do { \
    if (!(expr)) { \
        printf("[audio-mixer] FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

int run_audio_mixer_tests(void) {
    struct audio_mixer mixer;
    uint32_t mono_id = 99;
    uint32_t stereo_id = 99;
    int16_t mono[] = { 1000, -2000, 32767 };
    int16_t stereo[] = { 500, -500, 2000, 1000, 32767, 32767 };
    int16_t output[8] = { 0 };
    int failures = 0;
    size_t i;

    audio_mixer_init(&mixer);
    CHECK(mixer.global_volume == AUDIO_MIXER_VOLUME_MAX);
    CHECK(audio_mixer_open(&mixer, 10, 1, &mono_id) == AUDIO_MIXER_OK);
    CHECK(audio_mixer_open(&mixer, 20, 2, &stereo_id) == AUDIO_MIXER_OK);
    CHECK(audio_mixer_submit(&mixer, mono_id, mono, 3) == AUDIO_MIXER_OK);
    CHECK(audio_mixer_submit(&mixer, stereo_id, stereo, 3) == AUDIO_MIXER_OK);
    CHECK(audio_mixer_submit(&mixer, mono_id, mono, 3) == AUDIO_MIXER_BUSY);
    CHECK(audio_mixer_set_stream_volume(&mixer, stereo_id, 500) == AUDIO_MIXER_OK);
    CHECK(audio_mixer_set_global_volume(&mixer, 500) == AUDIO_MIXER_OK);
    CHECK(audio_mixer_mix_stereo_s16(&mixer, output, 4) == 4);
    CHECK(output[0] == 625 && output[1] == 375);
    CHECK(output[2] == -500 && output[3] == -750);
    CHECK(output[4] == 24575 && output[5] == 24575);
    CHECK(output[6] == 0 && output[7] == 0);
    CHECK(mixer.underrun_frames == 1);
    CHECK(audio_mixer_stream_pending_frames(&mixer, mono_id) == 0);

    CHECK(audio_mixer_submit(&mixer, mono_id, mono, 3) == AUDIO_MIXER_OK);
    CHECK(audio_mixer_set_app_volume(&mixer, 10, 0) == AUDIO_MIXER_OK);
    CHECK(audio_mixer_mix_stereo_s16(&mixer, output, 1) == 1);
    CHECK(output[0] == 0 && output[1] == 0);
    CHECK(audio_mixer_set_app_volume(&mixer, 999, 100) == AUDIO_MIXER_NOT_FOUND);
    CHECK(audio_mixer_set_global_volume(&mixer, 1001) == AUDIO_MIXER_INVALID);
    CHECK(audio_mixer_close(&mixer, mono_id) == AUDIO_MIXER_OK);
    CHECK(audio_mixer_close(&mixer, mono_id) == AUDIO_MIXER_NOT_FOUND);

    audio_mixer_init(&mixer);
    for (i = 0; i < AUDIO_MIXER_MAX_STREAMS; ++i) {
        uint32_t id;
        CHECK(audio_mixer_open(&mixer, (uint32_t)i + 1u, 2, &id) == AUDIO_MIXER_OK);
        CHECK(id == i);
    }
    CHECK(audio_mixer_open(&mixer, 100, 2, &mono_id) == AUDIO_MIXER_NO_SLOT);
    CHECK(audio_mixer_open(&mixer, 1, 3, &mono_id) == AUDIO_MIXER_INVALID);

    audio_mixer_init(&mixer);
    CHECK(audio_mixer_open(&mixer, 1, 1, &mono_id) == AUDIO_MIXER_OK);
    CHECK(audio_mixer_open(&mixer, 2, 1, &stereo_id) == AUDIO_MIXER_OK);
    CHECK(audio_mixer_submit(&mixer, mono_id, mono + 2, 1) == AUDIO_MIXER_OK);
    CHECK(audio_mixer_submit(&mixer, stereo_id, mono + 2, 1) == AUDIO_MIXER_OK);
    CHECK(audio_mixer_mix_stereo_s16(&mixer, output, 1) == 1);
    CHECK(output[0] == 32767 && output[1] == 32767);

    if (failures == 0) printf("[audio-mixer] ok\n");
    return failures;
}
