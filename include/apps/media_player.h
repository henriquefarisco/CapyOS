#ifndef APPS_MEDIA_PLAYER_H
#define APPS_MEDIA_PLAYER_H

#include <stddef.h>

#define MEDIA_PLAYER_QUEUE_MAX 8u
#define MEDIA_PLAYER_PATH_MAX 256u

void media_player_open(void);
int media_player_open_path(const char *path);
int media_player_enqueue(const char *path);
size_t media_player_queue_count(void);
/* Same cooperative desktop owner as audio_service_poll, after that pump. */
void media_player_poll(void);
int media_player_smoke_roundtrip(void);
#ifdef CAPYOS_MEDIA_PLAYER_SMOKE
int media_player_smoke_start(void);
void media_player_smoke_note_frame(void);
int media_player_smoke_result(void);
#endif

#endif
