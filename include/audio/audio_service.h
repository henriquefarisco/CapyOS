#ifndef CAPYOS_AUDIO_SERVICE_H
#define CAPYOS_AUDIO_SERVICE_H

#include <stddef.h>
#include <stdint.h>

/* Decoded WAV sources mixed concurrently into the single 48 kHz stereo ring. */
#define AUDIO_SERVICE_MAX_SOURCES 4u

/* Up to AUDIO_SERVICE_MAX_SOURCES decoded WAV sources from different
 * applications are mixed into one 48 kHz stereo ring. The fields below that
 * name a source (playing, active_app_id, source_frames, played_frames,
 * completed, playback_id) describe the PRIMARY source: the most recently
 * started one. At its EOF they read exactly as the single-source service did
 * (playing 0, active_app_id 0, completed 1, fully played) even while other
 * applications keep playing; audio_service_get_app_status() gives each
 * application its own view. */
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
/* In-memory sources join a running ring without restarting it (they start at
 * most one ring, ~341 ms, later); a second start by the same application
 * replaces its previous source. Rejections (-2 format, -7 no free source,
 * codec errors) leave other applications playing. */
int audio_service_play_wav_memory(uint32_t app_id,
                                  const uint8_t *data,
                                  size_t size);
/* File reads and bounded decode run outside the engine gate while existing
 * sources continue. Successful file playback starts exclusively at commit;
 * rejected preparation leaves existing sources intact. Memory playback joins.
 * One preparation may be in flight; another play request is rejected. */
int audio_service_play_wav_file(uint32_t app_id, const char *path);
/* Diagnostic tone: exclusive, loops until stopped, re-rendered on gain changes. */
int audio_service_play_test_tone(uint32_t app_id);
/* Stops every source and the DMA ring. */
void audio_service_stop(void);
/* Stops one application's source (or its tone); other applications keep
 * playing. -1 when that application has nothing playing. */
int audio_service_stop_app(uint32_t app_id);
/* Per-application view: playing/completed/frames/playback_id of that
 * application's source; zeros when it has none. Global fields as get_status. */
int audio_service_get_app_status(uint32_t app_id,
                                 struct audio_service_status *status);
/* Stereo source peaks (0..32768) over at most 256 frames at the played cursor,
 * with current app/global gains. Not a hardware meter; queued gain changes
 * may reach the device later. Idle/not-yet-started sources read zero. */
int audio_service_get_app_levels(uint32_t app_id, uint16_t *left, uint16_t *right);
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
/* Backend-agnostic hardware snapshot (ring position, wallclock, stream
 * error) for diagnostics and lab smokes; -1 when the engine gate is busy or
 * no backend was selected. Struct in audio/audio_output.h. */
struct audio_output_status;
int audio_service_get_output_status(struct audio_output_status *status);

#endif
