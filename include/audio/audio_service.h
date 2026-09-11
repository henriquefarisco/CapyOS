#ifndef CAPYOS_AUDIO_SERVICE_H
#define CAPYOS_AUDIO_SERVICE_H

#include <stddef.h>
#include <stdint.h>

struct audio_service_status {
    int initialized;
    int available;
    int playing;
    int last_error;
    uint16_t global_volume;
    uint32_t active_app_id;
    uint32_t sample_rate;
    uint16_t channels;
    uint64_t source_frames;
    uint64_t played_frames;
    int completed; /* Natural EOF, distinct from stop/error. */
    uint64_t playback_id; /* New successful start; preserved across EOF/stop. */
};

int audio_service_init(void);
int audio_service_play_wav_memory(uint32_t app_id,
                                  const uint8_t *data,
                                  size_t size);
int audio_service_play_wav_file(uint32_t app_id, const char *path);
int audio_service_play_test_tone(uint32_t app_id);
void audio_service_stop(void);
/* Start an idempotent task-context DMA pump after scheduler adoption. This
 * worker retains no desktop/session/PCM pointer outside the serialized engine.
 * Play/stop/gain remain single foreground-owner commands, never IRQ calls. */
int audio_service_start_worker(void);
#ifdef CAPYOS_MEDIA_PLAYER_SMOKE
int audio_service_smoke_preemption(void);
int audio_service_smoke_guarded_playback(void);
#endif
/* Serialized pump: concurrent/reentrant calls skip; no hardware I/O when idle.
 * Also usable by pre-login tests before the worker has been started. */
void audio_service_poll(void);
int audio_service_set_global_volume(uint16_t volume);
int audio_service_set_app_volume(uint32_t app_id, uint16_t volume);
int audio_service_get_status(struct audio_service_status *status);

#endif
