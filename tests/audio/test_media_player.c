/* Exercise the actual CapyUI player callbacks; fake only audio/window edges. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../../CapyUI/src/apps/media_player.c"

static struct audio_service_status mock_audio;
static struct gui_window mock_window;
static uint32_t pixels[540u * 390u];
static struct font mock_font;
static struct gui_theme_palette mock_theme = {.accent = 0x123456, .accent_alt = 0x654321};
static unsigned plays, stops, invalidations;
static const char *fail_path;
static char played_path[256];

size_t kstrlen(const char *s) { return strlen(s); }
void kstrcpy(char *dst, size_t size, const char *src) { snprintf(dst, size, "%s", src); }
int kstreq(const char *a, const char *b) { return strcmp(a, b) == 0; }
void kmemzero(void *p, size_t size) { memset(p, 0, size); }
const char *app_current_language(void) { return "en"; }
const char *localization_select(const char *language, const char *pt, const char *en, const char *es) {
    (void)language; (void)pt; (void)es; return en;
}
const struct font *font_default(void) { return &mock_font; }
void font_draw_string(struct gui_surface *s, const struct font *f, int32_t x,
                       int32_t y, const char *text, uint32_t color) {
    (void)s; (void)f; (void)x; (void)y; (void)text; (void)color;
}
const struct gui_theme_palette *compositor_theme(void) { return &mock_theme; }
struct gui_window *compositor_create_window(const char *title, int32_t x, int32_t y,
                                            uint32_t w, uint32_t h) {
    (void)title; (void)x; (void)y;
    assert(w == 540 && h == 390);
    mock_window.id = 1;
    mock_window.surface.width = w; mock_window.surface.height = h;
    mock_window.surface.pitch = w * 4; mock_window.surface.pixels = pixels;
    return &mock_window;
}
void compositor_show_window(uint32_t id) { assert(id == 1); }
void compositor_focus_window(uint32_t id) { assert(id == 1); }
void compositor_invalidate(uint32_t id) { assert(id == 1); ++invalidations; }
int inline_prompt_show(const char *title, const char *text, int32_t x, int32_t y,
                        inline_prompt_submit_fn cb, void *ctx) {
    (void)title; (void)text; (void)x; (void)y; (void)cb; (void)ctx; return 0;
}
int audio_service_get_status(struct audio_service_status *out) { *out = mock_audio; return 0; }
int audio_service_play_wav_file(uint32_t app, const char *path) {
    ++plays;
    if (fail_path && !strcmp(path, fail_path)) return -1;
    snprintf(played_path, sizeof(played_path), "%s", path);
    ++mock_audio.playback_id; mock_audio.active_app_id = app;
    mock_audio.playing = 1; mock_audio.completed = 0; mock_audio.last_error = 0;
    mock_audio.played_frames = 0; mock_audio.source_frames = 100;
    return 0;
}
int audio_service_play_test_tone(uint32_t app) { return audio_service_play_wav_file(app, "tone"); }
int audio_service_set_global_volume(uint16_t volume) { mock_audio.global_volume = volume; return 0; }
void audio_service_stop(void) { ++stops; mock_audio.playing = mock_audio.completed = 0; mock_audio.active_app_id = 0; }

static void reset_player(void) {
    memset(&g_media, 0, sizeof(g_media)); memset(&mock_audio, 0, sizeof(mock_audio));
    mock_audio.global_volume = 1000; plays = stops = invalidations = 0; fail_path = NULL;
    assert(media_player_open_path("/a.wav") == 0);
    assert(media_player_enqueue("/b.wav") == 0);
}
static void eof(void) {
    mock_audio.playing = 0; mock_audio.active_app_id = 0;
    mock_audio.completed = 1; mock_audio.played_frames = 100;
}
int main(void) {
    reset_player();
    assert(media_player_enqueue("/c.wav") == 0); /* Selection != playing track. */
    mock_audio.played_frames = 50;
    media_player_poll();
    assert(g_media.progress == 50 && plays == 1);
    unsigned before = invalidations;
    media_player_poll(); assert(invalidations == before);
    mp_paint(&mock_window);
    assert(pixels[298u * 540u + 14u] == mock_theme.accent);
    assert(pixels[298u * 540u + 300u] == mock_theme.accent_alt);
    eof(); media_player_poll();
    assert(plays == 2 && !strcmp(played_path, "/b.wav") && g_media.progress == 0);
    media_player_poll(); assert(plays == 2);
    eof(); media_player_poll(); assert(plays == 3 && !strcmp(played_path, "/c.wav"));
    eof(); media_player_poll(); media_player_poll();
    assert(plays == 3 && !g_media.auto_advance && g_media.progress == 100);
    assert(!strcmp(g_media.status, "Playlist completed"));

    reset_player(); mp_mouse(&mock_window, 190, 270, 1);
    assert(stops == 1 && !g_media.auto_advance);
    eof(); media_player_poll(); assert(plays == 1);

    reset_player(); ++mock_audio.playback_id; mock_audio.active_app_id = 42;
    media_player_poll(); mp_close(&mock_window);
    assert(!g_media.auto_advance && stops == 0 && mock_audio.playing);

    reset_player(); fail_path = "/b.wav";
    eof(); media_player_poll(); media_player_poll();
    assert(plays == 2 && !g_media.auto_advance);

    reset_player(); mock_audio.last_error = -5; mock_audio.playing = 0;
    media_player_poll(); assert(plays == 1 && !g_media.auto_advance);
    assert(media_player_smoke_roundtrip() == 0);
    puts("[media-player] playlist/progress/ownership ok");
    return 0;
}
