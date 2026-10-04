/* Etapa 10 backend selection: with HDA unavailable the service must fall back
 * to AC'97 through the audio_output table (mode `ac97`), or disable audio
 * safely and latch that decision when no controller exists (mode `none`).
 * Same stub set as test_audio_service.c; only hda_init/ac97_* behave
 * differently, so the real service, runtime, mixer and selection code run. */
#include "audio/audio_output.h"
#include "audio/audio_service.h"
#include "drivers/audio/ac97.h"
#include "drivers/audio/hda.h"
#include "fs/vfs.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AC97_LINE "[audio] system mixer ready at 48 kHz stereo S16LE via ac97"
#define HDA_LINE "[audio] system mixer ready at 48 kHz stereo S16LE via hda"
#define DISABLED_LINE "[audio] no usable output; audio disabled safely"

static int ac97_present;
static unsigned hda_inits, ac97_inits, ac97_plays, ac97_stops, ac97_polls;
static unsigned ac97_lines, hda_lines, disabled_lines;
static enum ac97_runtime_state ac97_state = AC97_STATE_READY;
static uint16_t ac97_sr;

void *kalloc(size_t size) { return malloc(size); }
void kfree(void *p) { free(p); }
void kmemzero(void *ptr, size_t size) { memset(ptr, 0, size); }
void klog(int level, const char *msg) {
    (void)level;
    assert(msg);
    if (!strcmp(msg, AC97_LINE)) ++ac97_lines;
    if (!strcmp(msg, HDA_LINE)) ++hda_lines;
    if (!strcmp(msg, DISABLED_LINE)) ++disabled_lines;
}

/* No HDA controller on this host: every HDA entry point must stay idle. */
int hda_init(void) { ++hda_inits; return -1; }
void hda_stop(void) { assert(0); }
void hda_request_stop(void) { assert(0); }
int hda_get_status(struct hda_runtime_status *status) { (void)status; assert(0); return -1; }
int hda_play_stereo_s16(const int16_t *samples, size_t frames) {
    (void)samples; (void)frames; assert(0); return -1;
}
int hda_refill_fragment(uint32_t fragment, const int16_t *samples) {
    (void)fragment; (void)samples; assert(0); return -1;
}

int ac97_init(void) { ++ac97_inits; return ac97_present ? 0 : -1; }
int ac97_play_stereo_s16(const int16_t *samples, size_t frames) {
    assert(samples && frames > 0 && frames <= 16384);
    ++ac97_plays;
    ac97_state = AC97_STATE_PLAYING;
    return 0;
}
void ac97_stop(void) { ++ac97_stops; ac97_state = AC97_STATE_READY; }
void ac97_request_stop(void) {}
int ac97_refill_fragment(uint32_t fragment, const int16_t *samples) {
    (void)fragment; (void)samples; return 0;
}
int ac97_get_status(struct ac97_runtime_status *status) {
    ++ac97_polls;
    *status = (struct ac97_runtime_status){.state = ac97_state,
        .buffer_bytes = 65536, .stream_status = ac97_sr,
        .wallclock_ticks = 24000000u * ac97_polls};
    return 0;
}

int vfs_stat_path(const char *path, struct vfs_stat *stat) {
    (void)path; (void)stat;
    return VFS_ERR_NOT_FOUND;
}
struct file *vfs_open(const char *path, uint32_t flags) {
    (void)path; (void)flags;
    return NULL;
}
int vfs_close(struct file *file) { (void)file; return 0; }
long vfs_read(struct file *file, void *buf, size_t size) {
    (void)file; (void)buf; (void)size;
    return 0;
}

static void test_fallback_plays_through_ac97(void) {
    struct audio_service_status status;
    struct audio_output_status output;
    assert(audio_service_init() == 0);
    assert(hda_inits == 1 && ac97_inits == 1);
    assert(ac97_lines == 1 && hda_lines == 0 && disabled_lines == 0);
    assert(audio_service_get_status(&status) == 0);
    assert(status.initialized && status.available && status.last_error == 0);
    assert(audio_service_get_output_status(&output) == 0);
    assert(output.state == AUDIO_OUTPUT_READY && !output.stream_error);

    /* The tone renders through the AC'97 table entry, not HDA. */
    assert(audio_service_play_test_tone(1) == 0);
    assert(ac97_plays == 1);
    assert(audio_service_get_status(&status) == 0 && status.playing);
    assert(audio_service_get_output_status(&output) == 0);
    assert(output.state == AUDIO_OUTPUT_PLAYING && output.buffer_bytes == 65536u);

    /* BCIS (bit 3) is a completion on AC'97, never a stream error. */
    ac97_sr = 0x08;
    audio_service_poll();
    assert(audio_service_get_status(&status) == 0 && status.playing && !status.last_error);
    /* FIFOE is fatal and must stop the service, not the OS. */
    ac97_sr = 0x10;
    audio_service_poll();
    assert(audio_service_get_status(&status) == 0 && !status.playing);
    assert(status.last_error == -4 && ac97_stops >= 1);
    ac97_sr = 0;

    /* A halted engine while we believe it is running is fatal as well. */
    assert(audio_service_play_test_tone(1) == 0);
    ac97_sr = 0x01;
    audio_service_poll();
    assert(audio_service_get_status(&status) == 0 && !status.playing && status.last_error == -4);
    ac97_sr = 0;

    /* Selection is latched: no second probe of either controller. */
    assert(audio_service_init() == 0);
    assert(hda_inits == 1 && ac97_inits == 1);
    audio_service_stop();
}

static void test_no_controller_disables_safely(void) {
    struct audio_service_status status;
    struct audio_output_status output;
    assert(audio_service_init() == -1);
    assert(hda_inits == 1 && ac97_inits == 1);
    assert(ac97_lines == 0 && hda_lines == 0 && disabled_lines == 1);
    assert(audio_service_get_status(&status) == 0);
    assert(status.initialized && !status.available && status.last_error == -1);
    assert(!status.playing && status.active_app_id == 0);
    assert(audio_service_get_output_status(&output) == -1);

    /* The unavailable state is latched: no re-probe, no hardware start. */
    assert(audio_service_init() == -1);
    assert(audio_service_play_test_tone(1) == -1);
    assert(audio_service_set_global_volume(500) == 0);
    assert(hda_inits == 1 && ac97_inits == 1 && ac97_plays == 0);
    assert(disabled_lines == 1);
    assert(audio_service_get_status(&status) == 0 && !status.available);
}

int main(int argc, char **argv) {
    extern int audio_mock_worker_fail;
    if (argc != 2 || (strcmp(argv[1], "ac97") && strcmp(argv[1], "none"))) {
        fprintf(stderr, "usage: %s ac97|none\n", argv[0]);
        return 2;
    }
    ac97_present = !strcmp(argv[1], "ac97");
    audio_mock_worker_fail = 0;
    assert(audio_service_start_worker() == 0);
    if (ac97_present) test_fallback_plays_through_ac97();
    else test_no_controller_disables_safely();
    printf("[audio-backend-select] ok (%s)\n", argv[1]);
    return 0;
}
