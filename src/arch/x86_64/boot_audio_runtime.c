#include "boot/boot_audio.h"
#include "boot/boot_ui.h"
#include "arch/x86_64/timebase.h"
#include "drivers/serial/serial_com1.h"

extern const uint8_t capyos_boot_wav_start[], capyos_boot_wav_end[];
static void progress(uint32_t percent) { boot_ui_splash_advance(percent, 100); }
static void relax(void) { __asm__ volatile("pause"); }
static void diagnostic_number(int value) {
    char digits[10];
    unsigned count = 0;
    uint32_t magnitude = value < 0 ? 0u - (uint32_t)value : (uint32_t)value;
    if (value < 0) com1_putc('-');
    do { digits[count++] = (char)('0' + magnitude % 10u); magnitude /= 10u; } while (magnitude);
    while (count) com1_putc(digits[--count]);
}
static int play(uint32_t app, const uint8_t *data, size_t size) {
    int rc = audio_service_play_wav_memory(app, data, size);
    if (rc) {
        com1_puts("[boot-audio] prepare error="); diagnostic_number(rc); com1_putc('\n');
    }
    return rc;
}
static int status(struct audio_service_status *value) {
    int rc = audio_service_get_status(value);
    if (rc || value->last_error) {
        com1_puts("[boot-audio] stream error=");
        diagnostic_number(rc ? rc : value->last_error); com1_putc('\n');
    }
    return rc;
}

void kernel_boot_play_startup_sound(void) {
    const struct boot_audio_io io = {
        play, audio_service_poll, status,
        audio_service_stop, x64_timebase_ticks_100hz, progress, relax
    };
    boot_ui_splash_set_status("Starting CapyOS...");
    com1_puts("[boot-audio] starting\n");
    int rc = boot_audio_play(capyos_boot_wav_start,
        (size_t)((uintptr_t)capyos_boot_wav_end - (uintptr_t)capyos_boot_wav_start), &io);
    com1_puts(rc == 0 ? "[boot-audio] completed\n" : "[boot-audio] unavailable; continuing boot\n");
}
