#include "boot/boot_audio.h"
#include <assert.h>
#include <stdio.h>

static unsigned mode, polls, stops, percent, started;
static int play(uint32_t app, const uint8_t *data, size_t size) {
    assert(app == 0x424f4f54u && data && size == 1);
    ++started;
    return mode == 1 ? -1 : 0;
}
static void poll(void) { ++polls; }
static void stop(void) { ++stops; }
static uint64_t ticks(void) { return mode == 3 ? polls * 500u : polls; }
static void progress(uint32_t p) { assert(p <= 100); percent = p; }
static void relax(void) {}
static int status(struct audio_service_status *s) {
    if (mode == 2) return -1;
    s->source_frames = 100;
    s->played_frames = polls < 10 ? polls * 10 : 100;
    s->playing = mode == 3 || polls < 10;
    s->completed = mode != 4 && polls >= 10;
    s->last_error = mode == 5 ? -1 : 0;
    return 0;
}
int main(void) {
    struct boot_audio_io io = {play, poll, status, stop, ticks, progress, relax};
    uint8_t data = 0;
    assert(boot_audio_play(0, 1, &io) == -1 && !started);
    for (mode = 0; mode <= 5; ++mode) {
        polls = stops = started = percent = 0;
        int rc = boot_audio_play(&data, 1, &io);
        assert(rc == (mode == 0 ? 0 : -1));
        assert(started == 1 && stops == (mode == 1 ? 0u : 1u));
        if (mode == 0) assert(polls == 10 && percent == 100);
        if (mode == 3) assert(polls == 3);
    }
    puts("[boot-audio] EOF, unavailable, timeout, aborted and driver-error passed");
}
