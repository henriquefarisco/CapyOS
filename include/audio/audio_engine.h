#ifndef CAPYOS_AUDIO_ENGINE_H
#define CAPYOS_AUDIO_ENGINE_H
#include "audio/audio_service.h"
/* Private engine entry points. Only audio_runtime.c may enter from outside
 * the engine; callers must hold its access gate. */
int audio_engine_init(void);
/* Preparation owns only request-local memory, outside the engine gate. */
struct audio_prepared_source {
    uint8_t *samples;
    uint64_t frames;
    uint16_t channels;
    int error;
};
int audio_prepare_memory(const uint8_t *data, size_t size,
                         struct audio_prepared_source *out);
int audio_prepare_file(const char *path, struct audio_prepared_source *out);
void audio_prepared_release(struct audio_prepared_source *source);
/* Under the engine gate. Consumes samples on acceptance, including a hardware
 * start failure; leaves ownership with the request on rejection. */
int audio_engine_start_prepared(uint32_t app_id,
    struct audio_prepared_source *source, int exclusive);
int audio_engine_play_test_tone(uint32_t app_id);
void audio_engine_stop(void);
void audio_engine_poll(void);
/* Bounded IRQ phase, under the same access gate: no allocation, release,
 * logging, waits, VFS, decode or probing. Completion is drained by poll(). */
void audio_engine_poll_irq(void);
int audio_engine_set_global_volume(uint16_t volume);
int audio_engine_set_app_volume(uint32_t app_id, uint16_t volume);
int audio_engine_get_status(struct audio_service_status *status);
/* Per-application control over the mixed sources (see audio_service.h). */
int audio_engine_stop_app(uint32_t app_id);
int audio_engine_get_app_status(uint32_t app_id,
                                struct audio_service_status *status);
int audio_engine_get_app_levels(uint32_t app_id, uint16_t *left, uint16_t *right);
/* Snapshot of the selected output backend (see audio/audio_output.h); -1
 * before a successful init. IRQ-safe like the backend's own get_status. */
struct audio_output_status;
int audio_engine_get_output_status(struct audio_output_status *status);
#endif
