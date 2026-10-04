#include "boot/boot_audio.h"

int boot_audio_play(const uint8_t *data, size_t size, const struct boot_audio_io *io) {
    if (!data || !size || !io || !io->play || !io->poll || !io->status ||
        !io->stop || !io->ticks || !io->progress || !io->relax) return -1;
    if (io->play(0x424f4f54u, data, size) != 0) return -1;
    uint64_t start = io->ticks();
    uint32_t last_percent = 101;
    int result = -1;
    /* Independent iteration bound also contains a broken clock source. */
    for (uint32_t iterations = 0; iterations < 100000000u; ++iterations) {
        struct audio_service_status status = {0};
        io->poll();
        if (io->status(&status) != 0 || status.last_error) break;
        if (!status.playing) {
            result = status.completed && status.source_frames &&
                status.played_frames == status.source_frames ? 0 : -1;
            break;
        }
        if (io->ticks() - start >= 1500u) break;
        uint32_t percent = status.source_frames && status.played_frames <= status.source_frames ?
            (uint32_t)(status.played_frames * 100u / status.source_frames) : 0;
        if (percent != last_percent) {
            io->progress(percent);
            last_percent = percent;
        }
        io->relax();
    }
    io->stop();
    if (result == 0) io->progress(100);
    return result;
}
