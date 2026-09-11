#include "audio/audio_engine.h"

#include "audio/audio_mixer.h"
#include "drivers/audio/hda.h"
#include "drivers/audio/hda_core.h"
#include "fs/vfs.h"
#include "kernel/log/klog.h"
#include "memory/kmem.h"
#include "util/kstring.h"

#ifdef CAPYOS_HAVE_CAPYCODECS_AUDIO
#include "capy_audio.h"
#endif

#define AUDIO_SERVICE_APP_MEDIA_PLAYER 0x4d504c59u
#define AUDIO_SERVICE_MAX_FILE_BYTES (8u * 1024u * 1024u)
#define AUDIO_SERVICE_MAX_PCM_BYTES (64u * 1024u)
#define AUDIO_SERVICE_MAX_MIX_FRAMES (AUDIO_SERVICE_MAX_PCM_BYTES / 4u)
#define AUDIO_SERVICE_TONE_FRAMES 12000u
#define AUDIO_SERVICE_MAX_APP_VOLUMES 16u

struct audio_service_app_volume {
    uint32_t app_id;
    uint16_t volume;
    uint8_t used;
};

static struct audio_mixer g_mixer;
/* Serialized by audio_runtime.c. Only poll_irq is safe in IRQ context.
 * Keep unscaled PCM so repeated volume changes do not compound attenuation. */
static struct audio_service_status g_status = {
    .global_volume = AUDIO_MIXER_VOLUME_MAX
};
static int16_t g_mix_buffer[AUDIO_SERVICE_MAX_PCM_BYTES / sizeof(int16_t)];
static int16_t g_source_buffer[AUDIO_SERVICE_MAX_PCM_BYTES / sizeof(int16_t)];
static int g_streaming;
static int g_pending_result; /* 1 = EOF; negative = error; task drains it. */
static uint8_t *g_pcm_samples;
static uint16_t g_pcm_channels;
static uint64_t g_pcm_cursor;
static uint64_t g_played_bytes;
static uint64_t g_refilled_fragments;
static uint32_t g_last_position;
static uint32_t g_last_wallclock;
static uint32_t g_last_progress_clock;
static struct audio_service_app_volume
    g_app_volumes[AUDIO_SERVICE_MAX_APP_VOLUMES];

static uint16_t audio_service_app_volume(uint32_t app_id) {
    size_t i;
    for (i = 0; i < AUDIO_SERVICE_MAX_APP_VOLUMES; ++i) {
        if (g_app_volumes[i].used && g_app_volumes[i].app_id == app_id)
            return g_app_volumes[i].volume;
    }
    return AUDIO_MIXER_VOLUME_MAX;
}

static void audio_service_error(int error, const char *message) {
    audio_engine_stop();
    g_status.last_error = error;
    g_status.playing = 0;
    g_status.active_app_id = 0;
    if (message) klog(KLOG_WARN, message);
}

#ifdef CAPYOS_HAVE_CAPYCODECS_AUDIO
static void *audio_codec_alloc(size_t size, void *user_data) {
    (void)user_data;
    return size <= AUDIO_SERVICE_MAX_FILE_BYTES ? kalloc(size) : 0;
}

static void audio_codec_free(void *ptr, void *user_data) {
    (void)user_data;
    kfree(ptr);
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
    if (audio_service_mix(app_id, frames, g_mix_buffer) != 0 ||
        hda_play_stereo_s16(g_mix_buffer, frames) != 0) {
        audio_service_error(-3, "[audio] PCM playback start failed");
        return -1;
    }
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
    if (hda_init() != 0) {
        audio_service_error(-1, "[audio] HDA unavailable; audio disabled safely");
        return -1;
    }
    g_status.available = 1;
    g_status.last_error = 0;
    klog(KLOG_INFO, "[audio] system mixer ready at 48 kHz stereo S16LE");
    return 0;
}

#ifdef CAPYOS_HAVE_CAPYCODECS_AUDIO
static int audio_service_fill_fragment(int16_t *output) {
    const int16_t *samples = (const int16_t *)(const void *)g_pcm_samples;
    size_t frames = HDA_BDL_FRAGMENT_BYTES / 4u;
    size_t i;
    kmemzero(g_source_buffer, HDA_BDL_FRAGMENT_BYTES);
    for (i = 0; i < frames && g_pcm_cursor < g_status.source_frames;
         ++i, ++g_pcm_cursor) {
        g_source_buffer[i * 2u] = samples[g_pcm_cursor * g_pcm_channels];
        g_source_buffer[i * 2u + 1u] =
            samples[g_pcm_cursor * g_pcm_channels + (g_pcm_channels == 2u)];
    }
    return audio_service_mix(g_status.active_app_id, frames, output);
}

int audio_engine_play_wav_memory(uint32_t app_id,
                                  const uint8_t *data,
                                  size_t size) {
    struct capy_audio_allocator allocator;
    struct capy_audio_limits limits;
    struct capy_audio_pcm pcm;
    size_t i;
    int rc;
    struct hda_runtime_status hardware;
    if (!app_id || !data || size == 0 || size > AUDIO_SERVICE_MAX_FILE_BYTES)
        return -1;
    if (audio_engine_init() != 0) return -1;
    audio_engine_stop();
    allocator.alloc = audio_codec_alloc;
    allocator.free = audio_codec_free;
    allocator.user_data = 0;
    capy_audio_default_limits(&limits);
    limits.max_input_bytes = AUDIO_SERVICE_MAX_FILE_BYTES;
    limits.max_output_bytes = AUDIO_SERVICE_MAX_FILE_BYTES;
    limits.max_frames = AUDIO_SERVICE_MAX_FILE_BYTES / 2u;
    limits.max_sample_rate = 48000u;
    limits.max_channels = 2u;
    limits.max_chunks = 128u;
    rc = capy_audio_decode_memory_limited(data, size, &allocator, &limits, &pcm);
    if (rc != CAPY_AUDIO_OK) {
        audio_service_error(rc, "[audio] WAV decode rejected by codec contract");
        return -1;
    }
    if (pcm.metadata.sample_format != CAPY_AUDIO_SAMPLE_S16_LE ||
        pcm.metadata.sample_rate != 48000u ||
        (pcm.metadata.channels != 1u && pcm.metadata.channels != 2u)) {
        capy_audio_pcm_free(&pcm);
        audio_service_error(-2, "[audio] WAV must be 48 kHz mono/stereo S16LE");
        return -1;
    }
    /* Ownership transfers to the service until EOF/stop/error. DMA only sees
     * copies in its own ring, never this decoded allocation. */
    g_pcm_samples = pcm.samples;
    g_pcm_channels = pcm.metadata.channels;
    g_pcm_cursor = 0;
    g_status.source_frames = pcm.metadata.frame_count;
    g_status.played_frames = 0;
    g_status.active_app_id = app_id;
    g_status.sample_rate = 48000u;
    g_status.channels = 2u;
    g_status.last_error = 0;
    g_status.completed = 0;
    for (i = 0; i < AUDIO_SERVICE_MAX_PCM_BYTES / HDA_BDL_FRAGMENT_BYTES; ++i) {
        if (audio_service_fill_fragment(g_mix_buffer +
                i * HDA_BDL_FRAGMENT_BYTES / sizeof(int16_t)) != 0) {
            audio_service_error(-3, "[audio] stream prefill failed");
            return -1;
        }
    }
    if (hda_play_stereo_s16(g_mix_buffer, AUDIO_SERVICE_MAX_MIX_FRAMES) != 0 ||
        hda_get_status(&hardware) != 0 || hardware.state != HDA_STATE_PLAYING) {
        audio_service_error(-3, "[audio] stream start failed");
        return -1;
    }
    g_streaming = 1;
    g_status.playing = 1;
    if (++g_status.playback_id == 0) ++g_status.playback_id;
    g_played_bytes = 0;
    g_refilled_fragments = 0;
    g_last_position = 0;
    g_last_wallclock = hardware.wallclock_ticks;
    g_last_progress_clock = hardware.wallclock_ticks;
#ifdef CAPYOS_MEDIA_PLAYER_SMOKE
    klog_dec(KLOG_INFO, "[audio] stream started wallclock=", g_last_wallclock);
#endif
    return 0;
}
#else
int audio_engine_play_wav_memory(uint32_t app_id,
                                  const uint8_t *data,
                                  size_t size) {
    (void)app_id;
    (void)data;
    (void)size;
    audio_service_error(-4, "[audio] CapyCodecs audio ABI unavailable");
    return -1;
}
#endif

int audio_engine_play_wav_file(uint32_t app_id, const char *path) {
    struct vfs_stat stat;
    struct file *file;
    uint8_t *data;
    size_t total = 0;
    unsigned reads = 0;
    int rc;
    if (!app_id || !path || vfs_stat_path(path, &stat) != VFS_OK ||
        stat.mode != VFS_MODE_FILE || stat.size == 0 ||
        stat.size > AUDIO_SERVICE_MAX_FILE_BYTES)
        return -1;
    file = vfs_open(path, VFS_OPEN_READ);
    if (!file) return -1;
    /* Loading is synchronous; stop the old DMA rather than leave an unserviced
     * stream replaying while storage reads block the cooperative owner. */
    audio_engine_stop();
    data = (uint8_t *)kalloc(stat.size);
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
        kfree(data);
        return -1;
    }
    rc = audio_engine_play_wav_memory(app_id, data, stat.size);
    kfree(data);
    return rc;
}

int audio_engine_play_test_tone(uint32_t app_id) {
    uint32_t i;
    const uint32_t period = 200u; /* 240 Hz square wave at 48 kHz. */
    if (!app_id) app_id = AUDIO_SERVICE_APP_MEDIA_PLAYER;
    if (audio_engine_init() != 0) return -1;
    audio_engine_stop();
    for (i = 0; i < AUDIO_SERVICE_TONE_FRAMES; ++i) {
        int16_t sample = (i % period) < period / 2u ? 6000 : -6000;
        g_source_buffer[i * 2u] = sample;
        g_source_buffer[i * 2u + 1u] = sample;
    }
    if (audio_service_render(app_id, AUDIO_SERVICE_TONE_FRAMES) != 0) return -1;
    if (++g_status.playback_id == 0) ++g_status.playback_id;
    return 0;
}

void audio_engine_stop(void) {
    g_pending_result = 0;
    hda_stop();
    if (g_pcm_samples) kfree(g_pcm_samples);
    g_pcm_samples = 0;
    g_streaming = 0;
    g_status.playing = 0;
    g_status.active_app_id = 0;
    g_status.completed = 0;
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
    hda_request_stop();
    g_pending_result = result;
}

void audio_engine_poll_irq(void) {
    struct hda_runtime_status status;
    if (!g_status.playing || g_pending_result) return;
    if (hda_get_status(&status) != 0 || status.state != HDA_STATE_PLAYING ||
        (status.stream_status & 0x18u))
        { audio_request_finish(-4); return; }
#ifdef CAPYOS_HAVE_CAPYCODECS_AUDIO
    if (g_streaming) {
        uint32_t advance;
        uint64_t retired;
        /* A full 64 KiB ring lasts 341 ms. Reject gaps >=250 ms before
         * interpreting modulo positions or touching retired DMA fragments. */
        if (status.buffer_bytes != AUDIO_SERVICE_MAX_PCM_BYTES ||
            (uint32_t)(status.wallclock_ticks - g_last_wallclock) >= 6000000u ||
            hda_ring_advance(g_last_position, status.position_bytes,
                             status.buffer_bytes, &advance) != 0) {
            audio_request_finish(-5);
            return;
        }
        g_last_position = status.position_bytes;
        g_last_wallclock = status.wallclock_ticks;
        if (advance) g_last_progress_clock = status.wallclock_ticks;
        if ((uint32_t)(status.wallclock_ticks - g_last_progress_clock) >= 24000000u) {
            audio_request_finish(-5);
            return;
        }
        g_played_bytes += advance;
        g_status.played_frames = g_played_bytes / 4u;
        if (g_status.played_frames >= g_status.source_frames) {
            audio_request_finish(1);
            return;
        }
        retired = g_played_bytes / HDA_BDL_FRAGMENT_BYTES;
        if (retired - g_refilled_fragments >
            AUDIO_SERVICE_MAX_PCM_BYTES / HDA_BDL_FRAGMENT_BYTES) {
            audio_request_finish(-5);
            return;
        }
        while (g_refilled_fragments < retired) {
            uint32_t fragment = (uint32_t)(g_refilled_fragments %
                (AUDIO_SERVICE_MAX_PCM_BYTES / HDA_BDL_FRAGMENT_BYTES));
            if (audio_service_fill_fragment(g_mix_buffer) != 0 ||
                hda_refill_fragment(fragment, g_mix_buffer) != 0) {
                audio_request_finish(-5);
                return;
            }
            ++g_refilled_fragments;
        }
    }
#endif
}

void audio_engine_poll(void) {
    struct hda_runtime_status hardware;
    audio_engine_poll_irq();
    int result = g_pending_result;
    if (!result) return;
    audio_engine_stop();
    if (hda_get_status(&hardware) != 0 || hardware.state != HDA_STATE_READY)
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
