#ifndef CAPYOS_BUILTIN_MUSIC_H
#define CAPYOS_BUILTIN_MUSIC_H
#include <stddef.h>
#include <stdint.h>
struct builtin_music_track { const char *path; const char *staging; const uint8_t *data; size_t size; };
/* Creates missing bundled songs only. Existing destination files are preserved. */
int builtin_music_seed(const struct builtin_music_track *tracks, size_t count);
int builtin_music_install(void);
#endif
