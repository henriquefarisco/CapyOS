/* Multi-source mixing through the real service, runtime, mixer, codec and
 * backend table; only hardware and host I/O are replaced (same stub shape as
 * test_audio_service.c). Proves: a second in-memory source joins a running
 * ring without a DMA restart, fragments carry the mixed sum, per-app gain
 * applies to new fragments only, the primary's EOF reads like the legacy
 * single-source EOF while the other application keeps playing, per-app stop,
 * the source cap, isolated rejections, and the exclusive file/tone paths. */
#include "audio/audio_output.h"
#include "audio/audio_service.h"
#include "drivers/audio/ac97.h"
#include "drivers/audio/hda.h"
#include "fs/vfs.h"
#include "kernel/scheduler.h"
#include "audio_ogg_fixture.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned allocations, plays, stops, steps;
static int in_irq;
static int vmware_latency;
static enum hda_runtime_state hardware_state = HDA_STATE_READY;
static uint32_t hardware_position, hardware_clock;
static int16_t dma_samples[32768];
static const uint8_t *file_data;
static size_t file_size;
static struct file fake_file;
extern void (*audio_mock_timer_service)(void);
static unsigned prepare_steps, read_steps;
static void dma_step(void);

static void during_prepare(unsigned *budget) {
    unsigned count = *budget;
    *budget = 0;
    if (!count) return;
    /* Allocation can guard task dispatch, but must not exclude IRQ service. */
    struct audio_service_status status;
    assert(audio_service_get_status(&status) == 0 && status.playing);
    /* A recursive/concurrent play cannot overwrite the request-local decode. */
    assert(audio_service_play_wav_memory(99, audio_ogg_fixture,
                                         sizeof(audio_ogg_fixture)) == -1);
    for (unsigned i = 0; i < count; ++i) dma_step();
}

void *kalloc(size_t size) {
    assert(!in_irq);
    during_prepare(&prepare_steps);
    void *p = malloc(size); if (p) ++allocations; return p;
}
void kfree(void *p) { assert(!in_irq); if (p) { --allocations; free(p); } }
void klog(int level, const char *msg) { assert(!in_irq); (void)level; (void)msg; }
void kmemzero(void *ptr, size_t size) { memset(ptr, 0, size); }
int hda_init(void) { assert(!in_irq); return 0; }
void hda_stop(void) { assert(!in_irq); ++stops; hardware_state = HDA_STATE_READY; }
void hda_request_stop(void) {}
int hda_get_status(struct hda_runtime_status *status) {
    *status = (struct hda_runtime_status){.state = hardware_state, .buffer_bytes = 65536,
        .vendor_id = vmware_latency ? 0x15ad : 0, .device_id = vmware_latency ? 0x1977 : 0,
        .position_bytes = hardware_position, .wallclock_ticks = hardware_clock};
    return 0;
}
int hda_play_stereo_s16(const int16_t *samples, size_t frames) {
    assert(frames > 0 && frames <= 16384);
    memcpy(dma_samples, samples, frames * 4u);
    ++plays; steps = 0;
    hardware_position = 0; hardware_clock = 1000;
    hardware_state = HDA_STATE_PLAYING;
    return 0;
}
int hda_refill_fragment(uint32_t fragment, const int16_t *samples) {
    assert(fragment < 16);
    memcpy(dma_samples + fragment * 2048, samples, 4096);
    return 0;
}
int ac97_init(void) { return -1; }
int ac97_play_stereo_s16(const int16_t *s, size_t f) { (void)s; (void)f; assert(0); return -1; }
void ac97_stop(void) { assert(0); }
void ac97_request_stop(void) { assert(0); }
int ac97_refill_fragment(uint32_t f, const int16_t *s) { (void)f; (void)s; assert(0); return -1; }
int ac97_get_status(struct ac97_runtime_status *s) { (void)s; assert(0); return -1; }
int vfs_stat_path(const char *path, struct vfs_stat *stat) {
    (void)path;
    if (!file_data) return VFS_ERR_NOT_FOUND;
    *stat = (struct vfs_stat){.size = (uint32_t)file_size, .mode = VFS_MODE_FILE};
    return VFS_OK;
}
struct file *vfs_open(const char *path, uint32_t flags) {
    (void)path; assert(flags == VFS_OPEN_READ);
    fake_file.position = 0;
    return &fake_file;
}
int vfs_close(struct file *file) { assert(file == &fake_file); return 0; }
long vfs_read(struct file *file, void *buf, size_t size) {
    assert(file == &fake_file);
    during_prepare(&read_steps);
    if (file->position >= file_size) return 0;
    if (size > file_size - file->position) size = file_size - file->position;
    memcpy(buf, file_data + file->position, size);
    file->position += (uint32_t)size;
    return (long)size;
}

static void write32(uint8_t *p, uint32_t n) {
    p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8); p[2] = (uint8_t)(n >> 16); p[3] = (uint8_t)(n >> 24);
}

/* 48 kHz S16LE WAV holding `frames` frames of one constant sample. */
static uint8_t *make_wav(uint16_t channels, uint32_t frames, int16_t sample, size_t *size) {
    size_t data = (size_t)frames * channels * 2u;
    uint8_t *wav = calloc(1, 44u + data);
    assert(wav);
    memcpy(wav, "RIFF", 4); memcpy(wav + 8, "WAVEfmt ", 8); memcpy(wav + 36, "data", 4);
    write32(wav + 4, (uint32_t)(36u + data));
    write32(wav + 16, 16); wav[20] = 1; wav[22] = (uint8_t)channels;
    write32(wav + 24, 48000); write32(wav + 28, 48000u * channels * 2u);
    wav[32] = (uint8_t)(channels * 2u); wav[34] = 16;
    write32(wav + 40, (uint32_t)data);
    for (size_t i = 0; i < data; i += 2) {
        wav[44 + i] = (uint8_t)sample; wav[45 + i] = (uint8_t)(sample >> 8);
    }
    *size = 44u + data;
    return wav;
}

/* One retired fragment: DMA advanced 4096 bytes, 21 ms of wallclock. */
static void dma_step(void) {
    ++steps;
    hardware_position = ((steps - 1u) % 16u + 1u) * 4096u;
    hardware_clock += 512000u;
    in_irq = 1; audio_mock_timer_service(); in_irq = 0;
    audio_service_poll();
}

/* First left/right sample of the fragment refilled by step `n` (1-based). */
static int16_t refilled(unsigned n, unsigned channel) {
    return dma_samples[((n - 1u) % 16u) * 2048u + channel];
}

int main(void) {
    struct audio_service_status status, app;
    size_t a_size, b_size, bad_size;
    uint8_t *a = make_wav(2, 49152u, 4000, &a_size);  /* 3 rings, stereo. */
    uint8_t *b = make_wav(1, 8192u, 2000, &b_size);   /* 8 fragments, mono. */
    uint8_t *bad = make_wav(2, 1024u, 100, &bad_size);
    extern int audio_mock_worker_fail;
    uint64_t first_id;
    audio_mock_worker_fail = 0;
    assert(audio_service_start_worker() == 0);
    assert(audio_service_set_global_volume(1000) == 0);

    /* A alone: the historical start path. */
    assert(audio_service_play_wav_memory(1, a, a_size) == 0);
    assert(plays == 1 && stops == 1 && allocations == 1);
    assert(audio_service_get_status(&status) == 0 && status.playing);
    assert(status.active_app_id == 1 && status.source_frames == 49152u);
    first_id = status.playback_id;
    uint16_t left, right;
    assert(audio_service_get_app_levels(1, &left, &right) == 0 && left == 4000 && right == 4000);
    assert(audio_service_get_app_levels(42, &left, &right) == 0 && !left && !right);
    assert(audio_service_get_app_levels(0, &left, &right) == -1);
    assert(audio_service_get_app_levels(1, NULL, &right) == -1);
    for (unsigned i = 0; i < 4; ++i) dma_step();
    assert(refilled(4, 0) == 4000 && refilled(4, 1) == 4000);

    /* B joins: no DMA restart, B becomes the primary, A keeps its own view. */
    assert(audio_service_play_wav_memory(2, b, b_size) == 0);
    assert(plays == 1 && stops == 1 && allocations == 2);
    assert(audio_service_get_status(&status) == 0 && status.playing);
    assert(status.active_app_id == 2 && status.source_frames == 8192u);
    assert(status.playback_id > first_id && status.played_frames == 0);
    assert(audio_service_get_app_status(1, &app) == 0 && app.playing);
    assert(app.active_app_id == 1 && app.source_frames == 49152u);
    assert(app.played_frames == 4u * 1024u && app.playback_id == first_id);
    assert(audio_service_get_app_levels(2, &left, &right) == 0 && !left && !right);
    dma_step();
    assert(refilled(5, 0) == 6000 && refilled(5, 1) == 6000); /* 4000 + 2000 */

    /* Per-app gain reaches new fragments only; nothing restarts. */
    assert(audio_service_set_app_volume(2, 500) == 0);
    dma_step();
    assert(plays == 1 && refilled(6, 0) == 5000 && refilled(5, 0) == 6000);
    assert(audio_service_set_app_volume(2, 1000) == 0);

    /* B's frames were queued from stream offset (16 + 4) * 4096; it has played
     * out once the DMA is 8 fragments past that, i.e. after 28 steps. */
    while (steps < 27) dma_step();
    assert(audio_service_get_app_levels(2, &left, &right) == 0 && left == 2000 && right == 2000);
    assert(audio_service_set_app_volume(2, 500) == 0);
    assert(audio_service_set_global_volume(500) == 0);
    assert(audio_service_get_app_levels(2, &left, &right) == 0 && left == 500 && right == 500);
    assert(audio_service_set_app_volume(2, 1000) == 0);
    assert(audio_service_set_global_volume(1000) == 0);
    assert(audio_service_get_status(&status) == 0 && status.playing);
    assert(audio_service_get_app_status(2, &app) == 0 && app.played_frames == 7u * 1024u);
    dma_step();
    assert(audio_service_get_status(&status) == 0);
    assert(!status.playing && status.completed && status.active_app_id == 0);
    assert(status.played_frames == 8192u && status.source_frames == 8192u);
    assert(stops == 1 && allocations == 1); /* B reaped in task context; A lives. */
    assert(audio_service_get_app_status(2, &app) == 0 && !app.playing && !app.completed);
    assert(audio_service_get_app_status(1, &app) == 0 && app.playing);
    assert(app.played_frames == 28u * 1024u && app.playback_id == first_id);
    dma_step();
    assert(refilled(29, 0) == 4000); /* A alone again in the ring. */

    /* Stopping the last source is the full stop; a second stop finds nothing. */
    assert(audio_service_stop_app(1) == 0);
    assert(stops == 2 && allocations == 0);
    assert(audio_service_get_status(&status) == 0 && !status.playing);
    assert(audio_service_stop_app(1) == -1);
    assert(audio_service_get_app_levels(1, &left, &right) == 0 && !left && !right);

    /* A rejected request never disturbs the other application. */
    assert(audio_service_play_wav_memory(1, a, a_size) == 0);
    dma_step();
    assert(audio_service_play_wav_memory(2, bad, bad_size - 1u) == -1);
    assert(audio_service_get_status(&status) == 0 && status.last_error != 0);
    assert(audio_service_get_app_status(1, &app) == 0 && app.playing && allocations == 1);
    assert(plays == 2 && stops == 3);

    /* Source cap: four applications, the fifth is refused, others untouched. */
    assert(audio_service_play_wav_memory(2, b, b_size) == 0);
    assert(audio_service_play_wav_memory(3, b, b_size) == 0);
    assert(audio_service_play_wav_memory(4, b, b_size) == 0);
    assert(allocations == 4 && plays == 2);
    assert(audio_service_play_wav_memory(5, b, b_size) == -1);
    assert(audio_service_get_status(&status) == 0 && status.last_error == -7);
    assert(status.active_app_id == 4 && status.playing && allocations == 4);
    dma_step();
    assert(refilled(2, 0) == 4000 + 3 * 2000);

    /* Same application again: its source is replaced in place. */
    assert(audio_service_get_app_status(2, &app) == 0);
    uint64_t second_id = app.playback_id;
    assert(audio_service_play_wav_memory(2, b, b_size) == 0);
    assert(allocations == 4 && plays == 2);
    assert(audio_service_get_app_status(2, &app) == 0 && app.playback_id > second_id);

    /* Stopping a secondary keeps the ring and the primary; stopping the
     * primary promotes the most recent remaining source. */
    assert(audio_service_stop_app(3) == 0);
    assert(allocations == 3 && stops == 3);
    assert(audio_service_get_status(&status) == 0 && status.active_app_id == 2);
    assert(audio_service_stop_app(2) == 0);
    assert(audio_service_get_status(&status) == 0 && status.active_app_id == 4 && status.playing);

    /* File playback prepares without blocking service, then starts alone. */
    file_data = b; file_size = b_size;
    assert(audio_service_play_wav_file(7, "/b.wav") == 0);
    assert(stops == 4 && plays == 3 && allocations == 1);
    assert(audio_service_get_app_status(1, &app) == 0 && !app.playing);
    assert(audio_service_get_status(&status) == 0 && status.active_app_id == 7);
    file_data = NULL;

    /* The diagnostic tone is exclusive both ways. */
    assert(audio_service_play_test_tone(9) == 0);
    assert(allocations == 0 && plays == 4);
    assert(audio_service_get_app_status(9, &app) == 0 && app.playing && app.active_app_id == 9);
    assert(audio_service_play_wav_memory(1, a, a_size) == 0);
    assert(plays == 5 && allocations == 1);
    assert(audio_service_get_app_status(9, &app) == 0 && !app.playing);
    assert(audio_service_play_test_tone(9) == 0 && allocations == 0);
    assert(audio_service_stop_app(9) == 0);
    assert(audio_service_get_status(&status) == 0 && !status.playing);

    audio_service_stop();
    assert(allocations == 0);

    /* Real Vorbis allocation is interrupted for > one complete DMA ring.
     * Timer and worker must refill/reap independently of preparation. */
    assert(audio_service_play_wav_memory(1, a, a_size) == 0);
    unsigned before_plays = plays, before_stops = stops;
    prepare_steps = 24;
    assert(audio_service_play_wav_memory(2, audio_ogg_fixture,
                                         sizeof(audio_ogg_fixture)) == 0);
    assert(plays == before_plays && stops == before_stops && allocations == 2);
    assert(audio_service_get_app_status(1, &app) == 0 && app.playing);
    assert(app.played_frames == 24u * 1024u && !app.last_error);
    for (unsigned i = 9; i <= 24; ++i) assert(refilled(i, 0) == 4000);
    assert(audio_service_get_app_status(2, &app) == 0 && app.source_frames == 144000);
    audio_service_stop();
    assert(allocations == 0);

    /* EOF may be marked during preparation and reaped when it commits. */
    assert(audio_service_play_wav_memory(1, b, b_size) == 0);
    prepare_steps = 12;
    assert(audio_service_play_wav_memory(2, audio_ogg_fixture,
                                         sizeof(audio_ogg_fixture)) == 0);
    assert(allocations == 1);
    assert(audio_service_get_app_status(1, &app) == 0 && !app.playing);
    assert(audio_service_get_app_status(2, &app) == 0 && app.playing && !app.last_error);
    audio_service_stop();

    /* Slow storage also permits old playback; exclusivity happens at commit. */
    assert(audio_service_play_wav_memory(1, a, a_size) == 0);
    file_data = b; file_size = b_size; read_steps = 24;
    before_stops = stops;
    assert(audio_service_play_wav_file(2, "/b.wav") == 0);
    assert(stops == before_stops + 1 && allocations == 1);
    assert(audio_service_get_app_status(1, &app) == 0 && !app.playing);
    file_data = NULL;
    audio_service_stop();
    assert(allocations == 0);
    /* Signed minimum, distinct channels, and the bounded 256-frame window. */
    memset(a + 44, 0, a_size - 44);
    a[44] = 0; a[45] = 128; /* -32768 left */
    a[46] = 255; a[47] = 127; /* +32767 right */
    assert(audio_service_play_wav_memory(1, a, a_size) == 0);
    assert(audio_service_get_app_levels(1, &left, &right) == 0 && left == 32768 && right == 32767);
    audio_service_stop();
    memset(a + 44, 0, 4);
    a[44 + 256 * 4] = 255; a[45 + 256 * 4] = 127;
    assert(audio_service_play_wav_memory(1, a, a_size) == 0);
    assert(audio_service_get_app_levels(1, &left, &right) == 0 && !left && !right);
    audio_service_stop();
    vmware_latency = 1;
    assert(audio_service_play_wav_memory(2, b, b_size) == 0);
    for (unsigned i = 0; i < 3u * 2048u; ++i) assert(dma_samples[i] == 0);
    assert(dma_samples[3u * 2048u] == 2000);
    assert(audio_service_get_app_levels(2, &left, &right) == 0 && !left && !right);
    for (unsigned i = 0; i < 3; ++i) dma_step();
    assert(audio_service_get_app_status(2, &app) == 0 && app.played_frames == 0);
    assert(audio_service_get_app_levels(2, &left, &right) == 0 && left == 2000 && right == 2000);
    while (steps < 11) dma_step();
    assert(audio_service_get_app_status(2, &app) == 0 && app.played_frames == 8192 && app.playing);
    assert(audio_service_get_app_levels(2, &left, &right) == 0 && !left && !right);
    while (steps < 19) dma_step();
    assert(audio_service_get_status(&status) == 0 && status.playing && !status.completed);
    dma_step();
    assert(audio_service_get_status(&status) == 0 && !status.playing && status.completed);
    assert(status.source_frames == 8192 && status.played_frames == 8192 && !allocations);
    vmware_latency = 0;
    free(a); free(b); free(bad);
    puts("[audio-service-multi] ok");
    return 0;
}
