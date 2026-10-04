#include "audio/audio_engine.h"

#include "audio/audio_mixer.h"
#include "audio/audio_output.h"
#include "audio/audio_limits.h"
#include "drivers/audio/hda_core.h"
#include "fs/vfs.h"
#include "kernel/log/klog.h"
#include "kernel/scheduler.h"
#include "memory/kmem.h"
#include "util/kstring.h"

#ifdef CAPYOS_HAVE_CAPYCODECS_AUDIO
#include "capy_audio.h"
#endif

#define AUDIO_SERVICE_APP_MEDIA_PLAYER 0x4d504c59u
#define AUDIO_SERVICE_MAX_PCM_BYTES AUDIO_OUTPUT_RING_BYTES
#define AUDIO_SERVICE_FRAME_BYTES 4u /* stereo S16LE */
#define AUDIO_SERVICE_MAX_MIX_FRAMES (AUDIO_SERVICE_MAX_PCM_BYTES / AUDIO_SERVICE_FRAME_BYTES)
#define AUDIO_SERVICE_FRAGMENT_BYTES AUDIO_OUTPUT_FRAGMENT_BYTES
/* 250 ms and 1 s in the shared 24 MHz wallclock domain. */
#define AUDIO_SERVICE_POLL_GAP_TICKS (AUDIO_OUTPUT_WALLCLOCK_HZ / 4u)
#define AUDIO_SERVICE_STALL_TICKS AUDIO_OUTPUT_WALLCLOCK_HZ
#define AUDIO_SERVICE_TONE_FRAMES 12000u
#define AUDIO_SERVICE_MAX_APP_VOLUMES 16u
#define AUDIO_SERVICE_NO_SOURCE AUDIO_SERVICE_MAX_SOURCES

struct audio_service_app_volume {
    uint32_t app_id;
    uint16_t volume;
    uint8_t used;
};

/* One decoded WAV owned by the service. The IRQ phase mixes from `pcm` at
 * `cursor` and only marks `completed`; releasing memory is task-context work
 * (audio_engine_poll / stop paths). */
struct audio_service_source {
    uint8_t used;
    uint8_t completed;
    uint16_t channels;
    uint32_t app_id;
    uint8_t *pcm;
    uint64_t frames;
    uint64_t cursor;      /* frames already mixed into ring fragments */
    uint64_t start_bytes; /* ring stream offset at which frame 0 was queued */
    uint64_t playback_id;
};

static struct audio_mixer g_mixer;
/* Selected once by audio_engine_init(); NULL until then and when no
 * controller is usable. All hardware access goes through this table. */
static const struct audio_output *g_output;
/* Serialized by audio_runtime.c. Only poll_irq is safe in IRQ context.
 * Keep unscaled PCM so repeated volume changes do not compound attenuation.
 * The legacy fields (active_app_id, source_frames, played_frames, completed,
 * playback_id, playing) describe the PRIMARY source: the most recent play. */
static struct audio_service_status g_status = {
    .global_volume = AUDIO_MIXER_VOLUME_MAX
};
static int16_t g_mix_buffer[AUDIO_SERVICE_MAX_PCM_BYTES / sizeof(int16_t)];
static int16_t g_source_buffer[AUDIO_SERVICE_MAX_PCM_BYTES / sizeof(int16_t)];
static struct audio_service_source g_sources[AUDIO_SERVICE_MAX_SOURCES];
static size_t g_primary = AUDIO_SERVICE_NO_SOURCE;
static int g_dma_running;  /* Tone or stream started: poll_irq services hardware. */
static int g_streaming;    /* Ring fed from decoded sources (never the tone). */
static int g_pending_result; /* 1 = EOF; negative = error; task drains it. */
static uint64_t g_played_bytes;
static uint64_t g_queued_bytes; /* Bytes written into the ring since DMA start. */
static uint64_t g_refilled_fragments;
static uint32_t g_drain_frames;
static uint32_t g_last_position;
static uint32_t g_last_wallclock;
static uint32_t g_last_progress_clock;
static struct audio_service_app_volume
    g_app_volumes[AUDIO_SERVICE_MAX_APP_VOLUMES];

/* Preparation does not own the engine gate. Keep only heap operations atomic
 * with respect to BSP task dispatch; interrupts may still service the ring. */
static void *audio_request_alloc(size_t bytes) {
    scheduler_preempt_disable();
    void *result = kalloc(bytes);
    scheduler_preempt_enable();
    return result;
}
static void audio_request_free(void *ptr) {
    scheduler_preempt_disable();
    kfree(ptr);
    scheduler_preempt_enable();
}

static uint16_t audio_service_app_volume(uint32_t app_id) {
    size_t i;
    for (i = 0; i < AUDIO_SERVICE_MAX_APP_VOLUMES; ++i) {
        if (g_app_volumes[i].used && g_app_volumes[i].app_id == app_id)
            return g_app_volumes[i].volume;
    }
    return AUDIO_MIXER_VOLUME_MAX;
}

static uint64_t audio_service_next_playback_id(void) {
    if (++g_status.playback_id == 0) ++g_status.playback_id;
    return g_status.playback_id;
}

static size_t audio_service_source_find(uint32_t app_id) {
    size_t i;
    for (i = 0; i < AUDIO_SERVICE_MAX_SOURCES; ++i) {
        if (g_sources[i].used && g_sources[i].app_id == app_id) return i;
    }
    return AUDIO_SERVICE_NO_SOURCE;
}

/* Task context only: frees decoded PCM. */
static void audio_service_source_release(size_t index) {
    struct audio_service_source *source = &g_sources[index];
    if (source->pcm) kfree(source->pcm);
    kmemzero(source, sizeof(*source));
}

static void audio_service_sources_release_all(void) {
    size_t i;
    for (i = 0; i < AUDIO_SERVICE_MAX_SOURCES; ++i) {
        if (g_sources[i].used) audio_service_source_release(i);
    }
}

static int audio_service_sources_in_use(void) {
    size_t i;
    for (i = 0; i < AUDIO_SERVICE_MAX_SOURCES; ++i) {
        if (g_sources[i].used) return 1;
    }
    return 0;
}

/* Frames of `source` the DMA has actually played: ring progress past the
 * offset where its first frame was queued, capped at its length. */
static uint64_t audio_service_source_played(const struct audio_service_source *source) {
    uint64_t played;
    if (g_played_bytes <= source->start_bytes) return 0;
    played = (g_played_bytes - source->start_bytes) / AUDIO_SERVICE_FRAME_BYTES;
    return played > source->frames ? source->frames : played;
}

/* Most recently started source that is still playing, or none. */
static size_t audio_service_source_latest(void) {
    size_t i;
    size_t best = AUDIO_SERVICE_NO_SOURCE;
    for (i = 0; i < AUDIO_SERVICE_MAX_SOURCES; ++i) {
        if (!g_sources[i].used || g_sources[i].completed) continue;
        if (best == AUDIO_SERVICE_NO_SOURCE ||
            g_sources[i].playback_id > g_sources[best].playback_id)
            best = i;
    }
    return best;
}

static void audio_service_primary_sync(void) {
    const struct audio_service_source *source;
    if (g_primary == AUDIO_SERVICE_NO_SOURCE) return;
    source = &g_sources[g_primary];
    g_status.active_app_id = source->app_id;
    g_status.source_frames = source->frames;
    g_status.played_frames = audio_service_source_played(source);
    g_status.playback_id = source->playback_id;
    g_status.playing = 1;
    g_status.completed = 0;
}

/* The primary reached EOF: legacy fields report completion exactly as the
 * single-source service did (stopped, unowned, completed, fully played). */
static void audio_service_primary_complete(void) {
    g_status.played_frames = g_status.source_frames;
    g_status.completed = 1;
    g_status.playing = 0;
    g_status.active_app_id = 0;
    g_primary = AUDIO_SERVICE_NO_SOURCE;
}

/* Hardware-level failure: everything stops. */
static void audio_service_error(int error, const char *message) {
    audio_engine_stop();
    g_status.last_error = error;
    g_status.playing = 0;
    g_status.active_app_id = 0;
    if (message) klog(KLOG_WARN, message);
}

#ifdef CAPYOS_HAVE_CAPYCODECS_AUDIO
/* Per-request failure: other applications keep playing. */
static void audio_service_reject(int error, const char *message) {
    g_status.last_error = error;
    if (message) klog(KLOG_WARN, message);
}

static size_t audio_service_source_alloc(void) {
    size_t i;
    for (i = 0; i < AUDIO_SERVICE_MAX_SOURCES; ++i) {
        if (!g_sources[i].used) return i;
    }
    return AUDIO_SERVICE_NO_SOURCE;
}

static void *audio_codec_alloc(size_t size, void *user_data) {
    (void)user_data;
    return size <= AUDIO_SERVICE_MAX_DECODED_BYTES ? audio_request_alloc(size) : 0;
}

static void audio_codec_free(void *ptr, void *user_data) {
    (void)user_data;
    audio_request_free(ptr);
}
#endif

static int audio_service_mix(uint32_t app_id, size_t frames, int16_t *output) {
    uint32_t stream_id;
    audio_mixer_init(&g_mixer);
    (void)audio_mixer_set_global_volume(&g_mixer, g_status.global_volume);
    if (audio_mixer_open(&g_mixer, app_id, 2u, &stream_id) != AUDIO_MIXER_OK ||
        audio_mixer_set_stream_volume(&g_mixer, stream_id,
                                      audio_service_app_volume(app_id)) !=
            AUDIO_MIXER_OK ||
        audio_mixer_submit(&g_mixer, stream_id, g_source_buffer, frames) !=
            AUDIO_MIXER_OK ||
        audio_mixer_mix_stereo_s16(&g_mixer, output, frames) != frames)
        return -1;
    return 0;
}

static int audio_service_render(uint32_t app_id, size_t frames) {
    if (!g_output || audio_service_mix(app_id, frames, g_mix_buffer) != 0 ||
        g_output->play_stereo_s16(g_mix_buffer, frames) != 0) {
        audio_service_error(-3, "[audio] PCM playback start failed");
        return -1;
    }
    g_dma_running = 1;
    g_status.playing = 1;
    g_status.last_error = 0;
    g_status.active_app_id = app_id;
    g_status.sample_rate = 48000u;
    g_status.channels = 2u;
    g_status.source_frames = frames;
    g_status.played_frames = 0;
    g_status.completed = 0;
    return 0;
}

static int audio_service_refresh_volume(void) {
    /* Streaming gain is applied to newly queued fragments, without restarting
     * DMA or replaying already consumed source samples. */
    if (g_streaming) return 0;
    return g_status.playing ? audio_service_render(g_status.active_app_id,
                                   (size_t)g_status.source_frames) : 0;
}

int audio_engine_init(void) {
    if (g_status.initialized) return g_status.available ? 0 : -1;
    audio_mixer_init(&g_mixer);
    g_status.initialized = 1;
    /* Intel HDA is the VMware-validated path; AC'97 is the lab fallback. A
     * missing or failed controller disables audio without touching the rest
     * of the system. */
    g_output = audio_output_select();
    if (!g_output) {
        audio_service_error(-1, "[audio] no usable output; audio disabled safely");
        return -1;
    }
    g_status.available = 1;
    g_status.last_error = 0;
    klog(KLOG_INFO, g_output->kind == AUDIO_OUTPUT_HDA
                        ? "[audio] system mixer ready at 48 kHz stereo S16LE via hda"
                        : g_output->kind == AUDIO_OUTPUT_USB
                        ? "[audio] system mixer ready at 48 kHz stereo S16LE via usb-uac1"
                        : "[audio] system mixer ready at 48 kHz stereo S16LE via ac97");
    return 0;
}

int audio_engine_get_output_status(struct audio_output_status *status) {
    if (!status || !g_output) return -1;
    return g_output->get_status(status);
}

#ifdef CAPYOS_HAVE_CAPYCODECS_AUDIO
/* The ring keeps running only while the backend reports a healthy stream;
 * a join onto a faulted ring would be silently discarded by the next poll. */
static int audio_service_ring_healthy(void) {
    struct audio_output_status hardware;
    return g_output && g_output->get_status(&hardware) == 0 &&
           hardware.state == AUDIO_OUTPUT_PLAYING && !hardware.stream_error;
}

/* One 4096-byte stereo fragment mixed from every active source. IRQ-safe:
 * the mixer reads the decoded PCM in place, no allocation or logging. */
static int audio_service_fill_fragment(int16_t *output) {
    size_t frames = AUDIO_SERVICE_FRAGMENT_BYTES / AUDIO_SERVICE_FRAME_BYTES;
    size_t consumed[AUDIO_SERVICE_MAX_SOURCES];
    size_t i;
    audio_mixer_init(&g_mixer);
    (void)audio_mixer_set_global_volume(&g_mixer, g_status.global_volume);
    for (i = 0; i < AUDIO_SERVICE_MAX_SOURCES; ++i) {
        struct audio_service_source *source = &g_sources[i];
        uint32_t stream_id;
        const int16_t *samples;
        consumed[i] = 0;
        if (!source->used || source->completed || source->cursor >= source->frames)
            continue;
        consumed[i] = source->frames - source->cursor < frames
                          ? (size_t)(source->frames - source->cursor) : frames;
        samples = (const int16_t *)(const void *)
            (source->pcm + source->cursor * source->channels * 2u);
        if (audio_mixer_open(&g_mixer, source->app_id, source->channels,
                             &stream_id) != AUDIO_MIXER_OK ||
            audio_mixer_set_stream_volume(&g_mixer, stream_id,
                                          audio_service_app_volume(source->app_id)) !=
                AUDIO_MIXER_OK ||
            audio_mixer_submit(&g_mixer, stream_id, samples, consumed[i]) !=
                AUDIO_MIXER_OK)
            return -1;
    }
    if (audio_mixer_mix_stereo_s16(&g_mixer, output, frames) != frames) return -1;
    for (i = 0; i < AUDIO_SERVICE_MAX_SOURCES; ++i)
        g_sources[i].cursor += consumed[i];
    return 0;
}

int audio_prepare_memory(const uint8_t *data, size_t size,
                         struct audio_prepared_source *out) {
    struct capy_audio_allocator allocator;
    struct capy_audio_limits limits;
    struct capy_audio_pcm pcm;
    int rc;
    if (!out) return -1;
    *out = (struct audio_prepared_source){.error = -1};
    if (!data || size == 0 || size > AUDIO_SERVICE_MAX_FILE_BYTES)
        return -1;
    allocator.alloc = audio_codec_alloc;
    allocator.free = audio_codec_free;
    allocator.user_data = 0;
    capy_audio_default_limits(&limits);
    limits.max_input_bytes = AUDIO_SERVICE_MAX_FILE_BYTES;
    limits.max_output_bytes = AUDIO_SERVICE_MAX_DECODED_BYTES;
    limits.max_frames = AUDIO_SERVICE_MAX_DECODED_BYTES / 2u;
    limits.max_sample_rate = 48000u;
    limits.max_channels = 2u;
    /* Ogg counts packets/pages against this budget as well as bounded bytes. */
    limits.max_chunks = AUDIO_SERVICE_MAX_CHUNKS;
    rc = capy_audio_decode_memory_limited(data, size, &allocator, &limits, &pcm);
    if (rc != CAPY_AUDIO_OK) {
        out->error = rc;
        return -1;
    }
    if (pcm.metadata.sample_format != CAPY_AUDIO_SAMPLE_S16_LE ||
        pcm.metadata.sample_rate != 48000u ||
        pcm.metadata.frame_count == 0 ||
        (pcm.metadata.channels != 1u && pcm.metadata.channels != 2u)) {
        capy_audio_pcm_free(&pcm);
        out->error = -2;
        return -1;
    }
    out->samples = pcm.samples;
    out->frames = pcm.metadata.frame_count;
    out->channels = pcm.metadata.channels;
    out->error = 0;
    return 0;
}

int audio_engine_start_prepared(uint32_t app_id,
    struct audio_prepared_source *prepared, int exclusive) {
    struct audio_service_source *source;
    struct audio_output_status hardware;
    size_t slot, i;
    if (!app_id || !prepared) return -1;
    if (prepared->error || !prepared->samples || !prepared->frames ||
        (prepared->channels != 1 && prepared->channels != 2)) {
        audio_service_reject(prepared->error ? prepared->error : -2,
                             "[audio] source preparation rejected");
        return -1;
    }
    if (audio_engine_init() != 0) return -1;
    /* Decode/I/O did not hold the engine gate: EOF or a fault may have been
     * serviced in the meantime. Revalidate before changing source ownership.
     * A rejected replacement leaves the application's old source intact. */
    audio_engine_poll();
    if (!exclusive && g_streaming && !g_pending_result && audio_service_ring_healthy())
        (void)audio_engine_stop_app(app_id);
    else
        audio_engine_stop();
    slot = audio_service_source_alloc();
    if (slot == AUDIO_SERVICE_NO_SOURCE) {
        audio_service_reject(-7, "[audio] too many concurrent sources");
        return -1;
    }
    /* Ownership transfers to the service until EOF/stop/error. DMA only sees
     * mixed copies in its own ring, never this decoded allocation. */
    source = &g_sources[slot];
    source->used = 1;
    source->completed = 0;
    source->channels = prepared->channels;
    source->app_id = app_id;
    source->pcm = prepared->samples;
    source->frames = prepared->frames;
    prepared->samples = 0;
    source->cursor = 0;
    source->playback_id = audio_service_next_playback_id();
    g_primary = slot;
    g_status.sample_rate = 48000u;
    g_status.channels = 2u;
    g_status.last_error = 0;
    if (g_streaming) {
        /* Join a running ring: this source starts at the next fragment queued,
         * at most one ring (about 341 ms) after the call. */
        source->start_bytes = g_queued_bytes;
        audio_service_primary_sync();
        return 0;
    }
    /* Fresh ring: stream offsets restart at zero before the legacy fields are
     * derived from them, so played_frames reads 0 until the first poll. */
    if (g_output->get_status(&hardware) != 0 ||
        hardware.startup_padding_bytes > AUDIO_SERVICE_MAX_PCM_BYTES ||
        hardware.drain_padding_bytes > AUDIO_SERVICE_MAX_PCM_BYTES ||
        hardware.startup_padding_bytes % AUDIO_SERVICE_FRAGMENT_BYTES ||
        hardware.drain_padding_bytes % AUDIO_SERVICE_FRAGMENT_BYTES) {
        audio_service_error(-3, "[audio] invalid output latency contract");
        return -1;
    }
    source->start_bytes = hardware.startup_padding_bytes;
    g_drain_frames = hardware.drain_padding_bytes / AUDIO_SERVICE_FRAME_BYTES;
    g_played_bytes = 0;
    g_queued_bytes = 0;
    g_refilled_fragments = 0;
    audio_service_primary_sync();
    for (i = 0; i < AUDIO_SERVICE_MAX_PCM_BYTES / AUDIO_SERVICE_FRAGMENT_BYTES; ++i) {
        if (i < hardware.startup_padding_bytes / AUDIO_SERVICE_FRAGMENT_BYTES) {
            kmemzero(g_mix_buffer + i * AUDIO_SERVICE_FRAGMENT_BYTES / sizeof(int16_t),
                      AUDIO_SERVICE_FRAGMENT_BYTES);
            continue;
        }
        if (audio_service_fill_fragment(g_mix_buffer +
                i * AUDIO_SERVICE_FRAGMENT_BYTES / sizeof(int16_t)) != 0) {
            audio_service_error(-3, "[audio] stream prefill failed");
            return -1;
        }
    }
    if (g_output->play_stereo_s16(g_mix_buffer, AUDIO_SERVICE_MAX_MIX_FRAMES) != 0 ||
        g_output->get_status(&hardware) != 0 ||
        hardware.state != AUDIO_OUTPUT_PLAYING) {
        audio_service_error(-3, "[audio] stream start failed");
        return -1;
    }
    g_streaming = 1;
    g_dma_running = 1;
    g_queued_bytes = AUDIO_SERVICE_MAX_PCM_BYTES;
    g_last_position = 0;
    g_last_wallclock = hardware.wallclock_ticks;
    g_last_progress_clock = hardware.wallclock_ticks;
#ifdef CAPYOS_MEDIA_PLAYER_SMOKE
    klog_dec(KLOG_INFO, "[audio] stream started wallclock=", g_last_wallclock);
#endif
    return 0;
}
#else
int audio_prepare_memory(const uint8_t *data, size_t size,
                         struct audio_prepared_source *out) {
    (void)data;
    (void)size;
    if (out) *out = (struct audio_prepared_source){.error = -4};
    return -1;
}
int audio_engine_start_prepared(uint32_t app_id,
    struct audio_prepared_source *prepared, int exclusive) {
    (void)app_id; (void)prepared; (void)exclusive;
    audio_service_error(-4, "[audio] CapyCodecs audio ABI unavailable");
    return -1;
}
#endif

void audio_prepared_release(struct audio_prepared_source *source) {
    if (!source) return;
    if (source->samples) audio_request_free(source->samples);
    *source = (struct audio_prepared_source){0};
}

int audio_prepare_file(const char *path, struct audio_prepared_source *out) {
    struct vfs_stat stat;
    struct file *file;
    uint8_t *data;
    size_t total = 0;
    unsigned reads = 0;
    int rc;
    if (!out) return -1;
    *out = (struct audio_prepared_source){.error = -1};
    if (!path || vfs_stat_path(path, &stat) != VFS_OK ||
        stat.mode != VFS_MODE_FILE || stat.size == 0 ||
        stat.size > AUDIO_SERVICE_MAX_FILE_BYTES)
        return -1;
    file = vfs_open(path, VFS_OPEN_READ);
    if (!file) return -1;
    data = (uint8_t *)audio_request_alloc(stat.size);
    if (!data) {
        vfs_close(file);
        return -1;
    }
    while (total < stat.size && reads++ < 512u) {
        size_t request = stat.size - total;
        long read;
        if (request > 65536u) request = 65536u;
        read = vfs_read(file, data + total, request);
        if (read <= 0 || (size_t)read > request) break;
        total += (size_t)read;
    }
    vfs_close(file);
    if (total != stat.size) {
        audio_request_free(data);
        return -1;
    }
    rc = audio_prepare_memory(data, stat.size, out);
    audio_request_free(data);
    return rc;
}

int audio_engine_play_test_tone(uint32_t app_id) {
    uint32_t i;
    const uint32_t period = 200u; /* 240 Hz square wave at 48 kHz. */
    if (!app_id) app_id = AUDIO_SERVICE_APP_MEDIA_PLAYER;
    if (audio_engine_init() != 0) return -1;
    /* Diagnostic tone: exclusive, looping, re-rendered on gain changes. */
    audio_engine_stop();
    for (i = 0; i < AUDIO_SERVICE_TONE_FRAMES; ++i) {
        int16_t sample = (i % period) < period / 2u ? 6000 : -6000;
        g_source_buffer[i * 2u] = sample;
        g_source_buffer[i * 2u + 1u] = sample;
    }
    if (audio_service_render(app_id, AUDIO_SERVICE_TONE_FRAMES) != 0) return -1;
    (void)audio_service_next_playback_id();
    return 0;
}

void audio_engine_stop(void) {
    g_pending_result = 0;
    if (g_output) g_output->stop();
    audio_service_sources_release_all();
    g_primary = AUDIO_SERVICE_NO_SOURCE;
    g_streaming = 0;
    g_dma_running = 0;
    g_status.playing = 0;
    g_status.active_app_id = 0;
    g_status.completed = 0;
}

int audio_engine_stop_app(uint32_t app_id) {
    size_t index;
    uint8_t completed;
    if (!app_id) return -1;
    index = audio_service_source_find(app_id);
    if (index == AUDIO_SERVICE_NO_SOURCE) {
        /* The tone is exclusive and owned by the legacy status fields. */
        if (!g_streaming && g_status.playing && g_status.active_app_id == app_id) {
            audio_engine_stop();
            return 0;
        }
        return -1;
    }
    completed = g_sources[index].completed;
    audio_service_source_release(index);
    if (!audio_service_sources_in_use()) {
        /* Last source gone: same full stop as before multi-source mixing. */
        audio_engine_stop();
        return 0;
    }
    /* Other applications keep playing; audio already mixed into the ring
     * (at most one ring ahead) drains on its own. */
    if (g_primary == index) {
        if (completed) {
            /* Already at EOF, only not reaped yet: report it like the pump. */
            audio_service_primary_complete();
            return 0;
        }
        g_primary = audio_service_source_latest();
        if (g_primary == AUDIO_SERVICE_NO_SOURCE) {
            g_status.playing = 0;
            g_status.active_app_id = 0;
            g_status.completed = 0;
        } else {
            audio_service_primary_sync();
        }
    }
    return 0;
}

int audio_engine_set_global_volume(uint16_t volume) {
    if (audio_mixer_set_global_volume(&g_mixer, volume) != AUDIO_MIXER_OK)
        return -1;
    g_status.global_volume = volume;
    return audio_service_refresh_volume();
}

int audio_engine_set_app_volume(uint32_t app_id, uint16_t volume) {
    size_t i;
    size_t free_slot = AUDIO_SERVICE_MAX_APP_VOLUMES;
    if (!app_id || volume > AUDIO_MIXER_VOLUME_MAX) return -1;
    for (i = 0; i < AUDIO_SERVICE_MAX_APP_VOLUMES; ++i) {
        if (g_app_volumes[i].used && g_app_volumes[i].app_id == app_id) {
            g_app_volumes[i].volume = volume;
            (void)audio_mixer_set_app_volume(&g_mixer, app_id, volume);
            return g_status.active_app_id == app_id ?
                audio_service_refresh_volume() : 0;
        }
        if (!g_app_volumes[i].used && free_slot == AUDIO_SERVICE_MAX_APP_VOLUMES)
            free_slot = i;
    }
    if (free_slot == AUDIO_SERVICE_MAX_APP_VOLUMES) return -1;
    g_app_volumes[free_slot].used = 1;
    g_app_volumes[free_slot].app_id = app_id;
    g_app_volumes[free_slot].volume = volume;
    (void)audio_mixer_set_app_volume(&g_mixer, app_id, volume);
    return g_status.active_app_id == app_id ? audio_service_refresh_volume() : 0;
}

static void audio_request_finish(int result) {
    if (g_output) g_output->request_stop();
    g_pending_result = result;
}

void audio_engine_poll_irq(void) {
    struct audio_output_status status;
    if (!g_dma_running || g_pending_result) return;
    if (!g_output || g_output->get_status(&status) != 0 ||
        status.state != AUDIO_OUTPUT_PLAYING || status.stream_error)
        { audio_request_finish(-4); return; }
#ifdef CAPYOS_HAVE_CAPYCODECS_AUDIO
    if (g_streaming) {
        uint32_t advance;
        uint64_t retired;
        size_t i;
        int all_completed = 1;
        /* A full 64 KiB ring lasts 341 ms. Reject gaps >=250 ms before
         * interpreting modulo positions or touching retired DMA fragments.
         * hda_ring_advance() is backend-neutral ring arithmetic: both backends
         * report positions under the audio_output.h CBL rule. */
        if (status.buffer_bytes != AUDIO_SERVICE_MAX_PCM_BYTES ||
            (uint32_t)(status.wallclock_ticks - g_last_wallclock) >=
                AUDIO_SERVICE_POLL_GAP_TICKS ||
            hda_ring_advance(g_last_position, status.position_bytes,
                             status.buffer_bytes, &advance) != 0) {
            audio_request_finish(-5);
            return;
        }
        g_last_position = status.position_bytes;
        g_last_wallclock = status.wallclock_ticks;
        if (advance) g_last_progress_clock = status.wallclock_ticks;
        if ((uint32_t)(status.wallclock_ticks - g_last_progress_clock) >=
            AUDIO_SERVICE_STALL_TICKS) {
            audio_request_finish(-5);
            return;
        }
        g_played_bytes += advance;
        /* Per-source progress; completion is only marked here and reaped by
         * the task pump, which owns memory. */
        for (i = 0; i < AUDIO_SERVICE_MAX_SOURCES; ++i) {
            struct audio_service_source *source = &g_sources[i];
            if (!source->used) continue;
            if (!source->completed && g_played_bytes >= source->start_bytes +
                (source->frames + g_drain_frames) * AUDIO_SERVICE_FRAME_BYTES)
                source->completed = 1;
            if (!source->completed) all_completed = 0;
        }
        if (g_primary != AUDIO_SERVICE_NO_SOURCE)
            g_status.played_frames =
                audio_service_source_played(&g_sources[g_primary]);
        if (all_completed) {
            audio_request_finish(1);
            return;
        }
        retired = g_played_bytes / AUDIO_SERVICE_FRAGMENT_BYTES;
        if (retired - g_refilled_fragments >
            AUDIO_SERVICE_MAX_PCM_BYTES / AUDIO_SERVICE_FRAGMENT_BYTES) {
            audio_request_finish(-5);
            return;
        }
        while (g_refilled_fragments < retired) {
            uint32_t fragment = (uint32_t)(g_refilled_fragments %
                (AUDIO_SERVICE_MAX_PCM_BYTES / AUDIO_SERVICE_FRAGMENT_BYTES));
            if (audio_service_fill_fragment(g_mix_buffer) != 0 ||
                g_output->refill_fragment(fragment, g_mix_buffer) != 0) {
                audio_request_finish(-5);
                return;
            }
            ++g_refilled_fragments;
            g_queued_bytes += AUDIO_SERVICE_FRAGMENT_BYTES;
        }
    }
#endif
}

void audio_engine_poll(void) {
    struct audio_output_status hardware;
    int result;
    audio_engine_poll_irq();
    if (g_streaming && !g_pending_result) {
        /* Reap sources that finished while others keep playing. */
        size_t i;
        for (i = 0; i < AUDIO_SERVICE_MAX_SOURCES; ++i) {
            if (!g_sources[i].used || !g_sources[i].completed) continue;
            if (i == g_primary) audio_service_primary_complete();
            audio_service_source_release(i);
        }
    }
    result = g_pending_result;
    if (!result) return;
    audio_engine_stop();
    if (!g_output || g_output->get_status(&hardware) != 0 ||
        hardware.state != AUDIO_OUTPUT_READY)
        result = -4;
    if (result == 1) {
        g_status.played_frames = g_status.source_frames;
        g_status.completed = 1;
    } else {
        g_status.last_error = result;
        klog(KLOG_WARN, "[audio] deferred DMA/stream failure; playback stopped");
    }
}

int audio_engine_get_status(struct audio_service_status *status) {
    if (!status) return -1;
    *status = g_status;
    return 0;
}

int audio_engine_get_app_levels(uint32_t app_id, uint16_t *left, uint16_t *right) {
    if (!app_id || !left || !right) return -1;
    *left = *right = 0;
    if (!g_dma_running || g_pending_result) return 0;
    const uint8_t *pcm;
    uint64_t first, frames;
    uint16_t channels;
    size_t index = audio_service_source_find(app_id);
    if (index != AUDIO_SERVICE_NO_SOURCE) {
        const struct audio_service_source *source = &g_sources[index];
        if (source->completed || !source->pcm || g_played_bytes < source->start_bytes)
            return 0;
        first = audio_service_source_played(source);
        frames = source->frames;
        channels = source->channels;
        pcm = source->pcm;
    } else if (!g_streaming && g_status.playing && g_status.active_app_id == app_id) {
        first = 0;
        frames = AUDIO_SERVICE_TONE_FRAMES;
        channels = 2;
        pcm = (const uint8_t *)g_source_buffer;
    } else return 0;
    if (first >= frames || (channels != 1 && channels != 2)) return 0;
    uint64_t count = frames - first;
    if (count > 256u) count = 256u;
    uint32_t peaks[2] = {0, 0};
    for (uint64_t i = 0; i < count; ++i) {
        for (unsigned ch = 0; ch < 2; ++ch) {
            size_t offset = (size_t)((first + i) * channels + (channels == 1 ? 0 : ch)) * 2u;
            uint32_t raw = pcm[offset] | ((uint32_t)pcm[offset + 1u] << 8);
            uint32_t magnitude = raw >= 32768u ? 65536u - raw : raw;
            if (magnitude > peaks[ch]) peaks[ch] = magnitude;
        }
    }
    uint64_t gain = (uint64_t)g_status.global_volume * audio_service_app_volume(app_id);
    *left = (uint16_t)(peaks[0] * gain / ((uint64_t)AUDIO_MIXER_VOLUME_MAX * AUDIO_MIXER_VOLUME_MAX));
    *right = (uint16_t)(peaks[1] * gain / ((uint64_t)AUDIO_MIXER_VOLUME_MAX * AUDIO_MIXER_VOLUME_MAX));
    return 0;
}

int audio_engine_get_app_status(uint32_t app_id,
                                struct audio_service_status *status) {
    size_t index;
    if (!status || !app_id) return -1;
    *status = g_status;
    index = audio_service_source_find(app_id);
    if (index != AUDIO_SERVICE_NO_SOURCE) {
        const struct audio_service_source *source = &g_sources[index];
        status->active_app_id = app_id;
        status->source_frames = source->frames;
        status->played_frames = audio_service_source_played(source);
        status->playback_id = source->playback_id;
        status->playing = !source->completed;
        status->completed = source->completed;
        return 0;
    }
    /* The exclusive tone is already described by the legacy fields. */
    if (!g_streaming && g_status.playing && g_status.active_app_id == app_id)
        return 0;
    status->active_app_id = app_id;
    status->playing = 0;
    status->completed = 0;
    status->source_frames = 0;
    status->played_frames = 0;
    status->playback_id = 0;
    return 0;
}
