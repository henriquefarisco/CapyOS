#include "arch/x86_64/kernel_main_internal.h"

#if defined(CAPYOS_AUDIO_PLAYBACK_SMOKE) && defined(CAPYOS_AUDIO_MULTI_SMOKE)
#include "arch/x86_64/timebase.h"
#include "audio/audio_output.h"
#include "audio/audio_service.h"
#include "drivers/serial/serial_com1.h"

#define APP_A 0x534d4b41u
#define APP_B 0x534d4b42u
#define RATE 48000u
static uint8_t wav_a[44u + 8u * RATE * 4u];
static uint8_t wav_b[44u + 3u * RATE * 4u];

static void write32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i < 4u; ++i) p[i] = (uint8_t)(value >> (8u * i));
}

static void fixture(uint8_t *p, uint32_t frames, int16_t amplitude) {
    static const uint8_t header[44] = {
        'R','I','F','F',0,0,0,0,'W','A','V','E',
        'f','m','t',' ',16,0,0,0,1,0,2,0,0x80,0xbb,0,0,
        0,0xee,2,0,4,0,16,0,'d','a','t','a',0,0,0,0
    };
    for (unsigned i = 0; i < sizeof(header); ++i) p[i] = header[i];
    write32(p + 4, frames * 4u + 36u);
    write32(p + 40, frames * 4u);
    for (uint32_t i = 0; i < frames; ++i) {
        uint16_t sample = (uint16_t)(i % 200u < 100u ? amplitude : -amplitude);
        p[44u+i*4u] = p[46u+i*4u] = (uint8_t)sample;
        p[45u+i*4u] = p[47u+i*4u] = (uint8_t)(sample >> 8);
    }
}

static void marker(const char *text) {
    com1_puts(text);
    while (*text) dbgcon_putc((uint8_t)*text++);
}

int kernel_boot_run_audio_multi_smoke(void) {
    struct audio_service_status a, b, primary;
    struct audio_output_status output;
    unsigned phase = 0;
    uint64_t started;
    fixture(wav_a, 8u * RATE, 4000);
    fixture(wav_b, 3u * RATE, 2000);
    marker("[smoke] audio-multi starting\n");
    if (audio_service_set_global_volume(1000) ||
        audio_service_set_app_volume(APP_A, 1000) ||
        audio_service_set_app_volume(APP_B, 1000) ||
        audio_service_play_wav_memory(APP_A, wav_a, sizeof(wav_a))) goto fail;
    started = x64_timebase_ticks_100hz();
    while (x64_timebase_ticks_100hz() - started < 1200u) {
        audio_service_poll();
        if (audio_service_get_app_status(APP_A, &a) ||
            audio_service_get_status(&primary) ||
            audio_service_get_output_status(&output)) continue;
        if (!a.playing || primary.last_error || output.stream_error ||
            output.state != AUDIO_OUTPUT_PLAYING) goto fail;
        if (phase == 0 && a.played_frames >= RATE) {
            if (audio_service_play_wav_memory(APP_B, wav_b, sizeof(wav_b))) goto fail;
            marker("[smoke] audio-multi joined\n");
            phase = 1;
        } else if (phase == 1 && a.played_frames >= 2u * RATE) {
            if (audio_service_set_app_volume(APP_B, 500)) goto fail;
            marker("[smoke] audio-multi app-gain\n");
            phase = 2;
        } else if (phase == 2 && a.played_frames >= 3u * RATE) {
            if (audio_service_set_global_volume(500)) goto fail;
            marker("[smoke] audio-multi global-gain\n");
            phase = 3;
        } else if (phase == 3 && primary.completed && !primary.playing) {
            if (primary.source_frames != 3u * RATE ||
                primary.played_frames != 3u * RATE) goto fail;
            marker("[smoke] audio-multi independent-eof\n");
            phase = 4;
        } else if (phase == 4 && a.played_frames >= RATE * 49u / 10u) {
            if (audio_service_play_wav_memory(APP_B, wav_b, sizeof(wav_b))) goto fail;
            phase = 5;
        } else if (phase == 5 && a.played_frames >= RATE * 58u / 10u) {
            if (audio_service_stop_app(APP_B) ||
                audio_service_get_app_status(APP_B, &b) || b.playing) goto fail;
            marker("[smoke] audio-multi independent-stop\n");
            phase = 6;
        } else if (phase == 6 && a.played_frames >= RATE * 68u / 10u) {
            if (audio_service_stop_app(APP_A) ||
                audio_service_get_output_status(&output) ||
                output.state != AUDIO_OUTPUT_READY) goto fail;
            (void)audio_service_set_global_volume(1000);
            (void)audio_service_set_app_volume(APP_B, 1000);
            marker("[smoke] audio-multi ready\n");
            return 0;
        }
        __asm__ volatile("pause");
    }
fail:
    audio_service_stop();
    marker("[smoke] audio-multi FAIL\n");
    return -1;
}
#endif
