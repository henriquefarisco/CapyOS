#ifndef CAPYOS_AUDIO_ENGINE_H
#define CAPYOS_AUDIO_ENGINE_H
#include "audio/audio_service.h"
/* Private engine entry points. Only audio_runtime.c may enter from outside
 * the engine; callers must hold its access gate. */
int audio_engine_init(void);
int audio_engine_play_wav_memory(uint32_t app_id, const uint8_t *data, size_t size);
int audio_engine_play_wav_file(uint32_t app_id, const char *path);
int audio_engine_play_test_tone(uint32_t app_id);
void audio_engine_stop(void);
void audio_engine_poll(void);
/* Bounded IRQ phase, under the same access gate: no allocation, release,
 * logging, waits, VFS, decode or probing. Completion is drained by poll(). */
void audio_engine_poll_irq(void);
int audio_engine_set_global_volume(uint16_t volume);
int audio_engine_set_app_volume(uint32_t app_id, uint16_t volume);
int audio_engine_get_status(struct audio_service_status *status);
#endif
