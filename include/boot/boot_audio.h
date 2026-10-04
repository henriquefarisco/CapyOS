#ifndef CAPYOS_BOOT_AUDIO_H
#define CAPYOS_BOOT_AUDIO_H
#include <stddef.h>
#include <stdint.h>
#include "audio/audio_service.h"

struct boot_audio_io {
    int (*play)(uint32_t app, const uint8_t *data, size_t size);
    void (*poll)(void);
    int (*status)(struct audio_service_status *status);
    void (*stop)(void);
    uint64_t (*ticks)(void); /* monotonic 100 Hz */
    void (*progress)(uint32_t percent);
    void (*relax)(void);
};
/* Returns zero only after natural EOF. Failure never prevents the caller booting. */
int boot_audio_play(const uint8_t *data, size_t size, const struct boot_audio_io *io);
void kernel_boot_play_startup_sound(void);
#endif
