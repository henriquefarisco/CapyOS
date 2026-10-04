#include "audio/builtin_music.h"
extern const uint8_t capyos_acoustic_start[], capyos_acoustic_end[];
extern const uint8_t capyos_opera_start[], capyos_opera_end[];
extern const uint8_t capyos_sound_start[], capyos_sound_end[];
int builtin_music_install(void) {
#define TRACK(title, symbol) {"/Music/Capy " title ".ogg", "/system/music-install/" title ".tmp", \
    capyos_##symbol##_start, (size_t)((uintptr_t)capyos_##symbol##_end - (uintptr_t)capyos_##symbol##_start)}
    const struct builtin_music_track tracks[] = {
        TRACK("Acoustic", acoustic), TRACK("Opera", opera), TRACK("Sound", sound)
    };
#undef TRACK
    return builtin_music_seed(tracks, 3);
}
