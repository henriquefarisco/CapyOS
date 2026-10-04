/* Real codec + service + mixer; only hardware and host I/O are replaced. */
#include "audio/audio_service.h"
#include "drivers/audio/ac97.h"
#include "drivers/audio/hda.h"
#include "fs/vfs.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int16_t first_sample;
static int fail_play;
static unsigned stops;
static unsigned stop_requests;
static int in_irq;
extern void (*audio_mock_timer_service)(void);
static unsigned allocations;
static unsigned polls;
static uint8_t hardware_errors;
static uint32_t hardware_position, hardware_clock;
static uint32_t initial_clock;
static unsigned plays;
static enum hda_runtime_state hardware_state = HDA_STATE_READY;
static int16_t dma_samples[32768];
static const uint8_t *file_data;
static size_t file_size, file_read_limit;
static uint16_t file_mode = VFS_MODE_FILE;
static unsigned opens, closes;
static int allocation_budget = -1;
static struct file fake_file;

void *kalloc(size_t size) {
    assert(!in_irq);
    if (allocation_budget == 0) return NULL;
    if (allocation_budget > 0) --allocation_budget;
    void *p = malloc(size);
    if (p) ++allocations;
    return p;
}
void kfree(void *p) { assert(!in_irq); if (p) { --allocations; free(p); } }
void klog(int level, const char *msg) { assert(!in_irq); (void)level; (void)msg; }
void kmemzero(void *ptr, size_t size) { memset(ptr, 0, size); }
int hda_init(void) { assert(!in_irq); return 0; }
/* HDA always initializes here, so the service never selects the AC'97
 * backend; its entry points exist so the selection table links and any
 * unexpected fallback call fails loudly (see
 * test_audio_service_backend_probe.c for the fallback path). */
int ac97_init(void) { assert(!in_irq); return -1; }
int ac97_play_stereo_s16(const int16_t *samples, size_t frames) {
    (void)samples; (void)frames; assert(0); return -1;
}
void ac97_stop(void) { assert(0); }
void ac97_request_stop(void) { assert(0); }
int ac97_refill_fragment(uint32_t fragment, const int16_t *samples) {
    (void)fragment; (void)samples; assert(0); return -1;
}
int ac97_get_status(struct ac97_runtime_status *status) {
    (void)status; assert(0); return -1;
}
void hda_stop(void) { assert(!in_irq); ++stops; hardware_state = HDA_STATE_READY; }
void hda_request_stop(void) { ++stop_requests; }
int hda_get_status(struct hda_runtime_status *status) {
    ++polls;
    *status = (struct hda_runtime_status){.state = hardware_state,
        .stream_status = hardware_errors, .buffer_bytes = 65536,
        .position_bytes = hardware_position, .wallclock_ticks = hardware_clock};
    return 0;
}
int hda_play_stereo_s16(const int16_t *samples, size_t frames) {
    assert(frames > 0 && frames <= 16384);
    first_sample = samples[0];
    memcpy(dma_samples, samples, frames * 4u);
    ++plays;
    hardware_position = 0; hardware_clock = initial_clock;
    hardware_state = fail_play ? HDA_STATE_FAILED : HDA_STATE_PLAYING;
    return fail_play ? -1 : 0;
}
int hda_refill_fragment(uint32_t fragment, const int16_t *samples) {
    assert(fragment < 16);
    memcpy(dma_samples + fragment * 2048, samples, 4096);
    return 0;
}

static void write32(uint8_t *p, uint32_t n) {
    p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8);
    p[2] = (uint8_t)(n >> 16); p[3] = (uint8_t)(n >> 24);
}

static void test_streaming(const uint8_t *header) {
    const size_t bytes = 3u * 65536u;
    uint8_t *wav = malloc(bytes + 44);
    struct audio_service_status status;
    unsigned before;
    assert(wav);
    memcpy(wav, header, 44);
    wav[22] = 2; wav[32] = 4;
    write32(wav + 28, 192000);
    write32(wav + 4, (uint32_t)bytes + 36);
    write32(wav + 40, (uint32_t)bytes);
    for (size_t i = 0; i < bytes; i += 2) { wav[44+i] = 0x70; wav[45+i] = 0x17; }
    hardware_errors = 0;
    assert(audio_service_set_global_volume(1000) == 0);
    assert(audio_service_set_app_volume(1, 1000) == 0);
    initial_clock = UINT32_MAX - 1000000u; /* Cross the HDA WALLCLK wrap. */
    assert(audio_service_play_wav_memory(1, wav, bytes + 44) == 0);
    assert(allocations == 1);
    before = plays;
    assert(audio_service_set_global_volume(500) == 0);
    for (unsigned i = 0; i < 48; ++i) {
        assert(dma_samples[(i % 16) * 2048] == (i < 16 ? 6000 : 3000));
        hardware_position = ((i + 1) % 16) * 4096;
        if (!hardware_position) hardware_position = 65536;
        hardware_clock += 512000;
        audio_service_poll();
    }
    assert(plays == before); /* No DMA restart on gain/refill. */
    assert(audio_service_get_status(&status) == 0 && status.completed);
    assert(!status.playing && status.played_frames == bytes / 4);
    assert(allocations == 0);
    initial_clock = 0;
    assert(audio_service_play_wav_memory(1, wav, bytes + 44) == 0);
    hardware_clock = 6000000; /* A whole ambiguous/unsafe polling gap. */
    audio_service_poll();
    assert(audio_service_get_status(&status) == 0 && !status.playing);
    assert(status.last_error == -5 && !status.completed && allocations == 0);
    assert(audio_service_play_wav_memory(1, wav, bytes + 44) == 0);
    hardware_position = 65537;
    audio_service_poll();
    assert(audio_service_get_status(&status) == 0 && status.last_error == -5);
    assert(!status.playing && !status.completed && allocations == 0);
    assert(audio_service_play_wav_memory(1, wav, bytes + 44) == 0);
    for (unsigned i = 0; i < 50; ++i) {
        hardware_clock += 512000; /* Frequent polling, but stalled DMA. */
        audio_service_poll();
    }
    assert(audio_service_get_status(&status) == 0 && status.last_error == -5);
    assert(!status.playing && !status.completed && allocations == 0);
    free(wav);
}
int vfs_stat_path(const char *path, struct vfs_stat *stat) {
    (void)path;
    if (!file_data) return VFS_ERR_NOT_FOUND;
    *stat = (struct vfs_stat){.size = (uint32_t)file_size, .mode = file_mode};
    return VFS_OK;
}
struct file *vfs_open(const char *path, uint32_t flags) {
    (void)path;
    assert(flags == VFS_OPEN_READ);
    ++opens; fake_file.position = 0;
    return &fake_file;
}
int vfs_close(struct file *file) { assert(file == &fake_file); ++closes; return 0; }
long vfs_read(struct file *file, void *buf, size_t size) {
    assert(file == &fake_file);
    /* Timer/worker and snapshots remain available during request-local I/O.
     * This fixture is idle, so polling must not touch hardware or free data. */
    struct audio_service_status concurrent;
    unsigned old_polls = polls, old_stops = stops, old_allocations = allocations;
    in_irq = 1; audio_mock_timer_service(); in_irq = 0;
    audio_service_poll();
    assert(audio_service_get_status(&concurrent) == 0);
    assert(audio_service_play_wav_file(99, "/recursive.wav") == -1);
    assert(polls == old_polls && stops == old_stops && allocations == old_allocations);
    if (file->position >= file_read_limit) return 0;
    if (size > 7) size = 7; /* Legal short reads must be accumulated. */
    if (size > file_read_limit - file->position) size = file_read_limit - file->position;
    memcpy(buf, file_data + file->position, size);
    file->position += (uint32_t)size;
    return (long)size;
}

static void test_file_reads(const uint8_t *wav, size_t size) {
    file_data = wav; file_size = file_read_limit = size;
    assert(audio_service_play_wav_file(1, "/fixture.wav") == 0);
    assert(opens == closes && allocations == 1);
    audio_service_stop();
    assert(allocations == 0);
    file_read_limit = size - 1;
    assert(audio_service_play_wav_file(1, "/fixture.wav") == -1);
    assert(opens == closes && allocations == 0);
    file_read_limit = size;
    for (int budget = 0; budget < 2; ++budget) {
        allocation_budget = budget;
        assert(audio_service_play_wav_file(1, "/fixture.wav") == -1);
        assert(opens == closes && allocations == 0);
    }
    allocation_budget = -1;
    unsigned before = opens;
    file_size = 8u * 1024u * 1024u + 1u;
    assert(audio_service_play_wav_file(1, "/fixture.wav") == -1);
    file_size = size; file_mode = VFS_MODE_DIR;
    assert(audio_service_play_wav_file(1, "/fixture.wav") == -1);
    assert(opens == before && allocations == 0);
    file_data = NULL; file_mode = VFS_MODE_FILE;
}

static void test_irq_completion(const uint8_t *wav, size_t size) {
    struct audio_service_status status;
    hardware_errors = 0;
    assert(audio_service_play_wav_memory(1, wav, size) == 0);
    unsigned before_stops = stops, before_requests = stop_requests;
    hardware_position = 8; hardware_clock += 240000;
    in_irq = 1; audio_mock_timer_service(); in_irq = 0;
    assert(stop_requests == before_requests + 1 && stops == before_stops && allocations == 1);
    unsigned before_polls = polls;
    in_irq = 1; audio_mock_timer_service(); in_irq = 0;
    assert(polls == before_polls); /* A pending stop never refills/reprobes. */
    assert(audio_service_get_status(&status) == 0 && status.playing && !status.completed);
    audio_service_poll();
    assert(allocations == 0 && audio_service_get_status(&status) == 0 && status.completed);

    assert(audio_service_play_wav_memory(1, wav, size) == 0);
    hardware_position = 8; hardware_clock += 240000;
    in_irq = 1; audio_mock_timer_service(); in_irq = 0;
    audio_service_stop(); /* Cancel pending EOF before a replacement start. */
    assert(audio_service_play_test_tone(1) == 0);
    audio_service_poll();
    assert(audio_service_get_status(&status) == 0 && status.playing && !status.completed);
    audio_service_stop();

    assert(audio_service_play_wav_memory(1, wav, size) == 0);
    hardware_errors = 0x10;
    in_irq = 1; audio_mock_timer_service(); in_irq = 0;
    assert(allocations == 1);
    audio_service_poll();
    assert(allocations == 0 && audio_service_get_status(&status) == 0 && status.last_error == -4);
    hardware_errors = 0;
}

int main(void) {
    struct audio_service_status status;
    extern int audio_mock_worker_fail;
    audio_mock_worker_fail = 1;
    assert(audio_service_start_worker() == -1);
    assert(audio_service_init() == -1);
    assert(audio_service_play_test_tone(1) == -1);
    assert(audio_service_get_status(&status) == 0 && !status.available && status.last_error == -6);
    audio_mock_worker_fail = 0;
    assert(audio_service_start_worker() == 0);
    assert(audio_service_start_worker() == 0);
    /* Two mono S16LE frames, 48 kHz; first sample = 6000. */
    const uint8_t wav[] = {
        'R','I','F','F',40,0,0,0,'W','A','V','E',
        'f','m','t',' ',16,0,0,0,1,0,1,0,0x80,0xbb,0,0,
        0,0x77,1,0,2,0,16,0,'d','a','t','a',4,0,0,0,
        0x70,0x17,0x90,0xe8
    };
    assert(audio_service_set_global_volume(500) == 0);
    assert(audio_service_set_app_volume(1, 500) == 0);
    assert(audio_service_play_test_tone(1) == 0);
    audio_service_poll();
    assert(polls == 1);
    assert(audio_service_get_status(&status) == 0 && status.playing);
    uint64_t tone_id = status.playback_id;
    assert(tone_id != 0);
    assert(first_sample == 1500); /* Settings survive first init. */
    assert(audio_service_set_global_volume(1000) == 0);
    assert(first_sample == 3000);
    assert(audio_service_set_app_volume(1, 1000) == 0);
    assert(first_sample == 6000); /* No cumulative attenuation. */
    assert(audio_service_set_app_volume(2, 0) == 0);
    assert(first_sample == 6000); /* Another application's mute is isolated. */
    assert(audio_service_get_status(&status) == 0 && status.playback_id == tone_id);
    assert(audio_service_play_test_tone(2) == 0);
    assert(audio_service_get_status(&status) == 0 && status.playback_id != tone_id);
    assert(first_sample == 0);
    audio_service_stop();
    assert(audio_service_play_wav_memory(1, wav, sizeof(wav)) == 0);
    assert(first_sample == 6000 && allocations == 1);
    assert(audio_service_set_app_volume(1, 250) == 0);
    assert(first_sample == 6000); /* Queued frames are not restarted/re-scaled. */
    assert(audio_service_set_global_volume(1001) == -1);
    assert(audio_service_set_app_volume(0, 100) == -1);
    assert(audio_service_play_wav_memory(1, wav, sizeof(wav) - 1) == -1);
    assert(audio_service_get_status(&status) == 0);
    assert(status.playing && status.active_app_id == 1 && allocations == 1);
    assert(status.last_error != 0); /* Invalid replacement preserves old PCM. */
    fail_play = 1;
    assert(audio_service_play_test_tone(1) == -1);
    assert(audio_service_get_status(&status) == 0 && !status.playing);
    assert(allocations == 0);
    fail_play = 0;
    assert(audio_service_play_test_tone(1) == 0);
    hardware_errors = 0x10; /* Descriptor error stops the service, not the OS. */
    audio_service_poll();
    assert(audio_service_get_status(&status) == 0 && !status.playing);
    assert(status.last_error == -4 && status.active_app_id == 0);
    unsigned final_polls = polls;
    audio_service_poll();
    assert(polls == final_polls); /* Idle polling must not probe/touch hardware. */
    test_streaming(wav);
    test_file_reads(wav, sizeof(wav));
    test_irq_completion(wav, sizeof(wav));
    puts("[audio-service] ok");
    return 0;
}
