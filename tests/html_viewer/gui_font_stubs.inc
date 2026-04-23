static struct gui_window g_test_window;
static struct gui_theme_palette g_test_theme = {
    0, 0, 0, 0, 0, 0xFFFFFF, 0xAAAAAA, 0x33AAFF, 0x6699FF, 0xFFFFFF,
    0, 0, 0, 0, 0, 1};
static struct font g_test_font = {NULL, 8, 16, 32, 255, 16};

void compositor_init(uint32_t *framebuffer, uint32_t width, uint32_t height,
                     uint32_t pitch) {
  (void)framebuffer;
  (void)width;
  (void)height;
  (void)pitch;
}
void compositor_shutdown(void) {}
struct gui_window *compositor_create_window(const char *title, int32_t x,
                                            int32_t y, uint32_t w, uint32_t h) {
  (void)title;
  (void)x;
  (void)y;
  (void)w;
  (void)h;
  memset(&g_test_window, 0, sizeof(g_test_window));
  return &g_test_window;
}
void compositor_destroy_window(uint32_t window_id) { (void)window_id; }
void compositor_show_window(uint32_t window_id) { (void)window_id; }
void compositor_hide_window(uint32_t window_id) { (void)window_id; }
void compositor_focus_window(uint32_t window_id) { (void)window_id; }
void compositor_move_window(uint32_t window_id, int32_t x, int32_t y) {
  (void)window_id;
  (void)x;
  (void)y;
}
void compositor_resize_window(uint32_t window_id, uint32_t w, uint32_t h) {
  (void)window_id;
  (void)w;
  (void)h;
}
void compositor_set_title(uint32_t window_id, const char *title) {
  (void)window_id;
  (void)title;
}
void compositor_invalidate(uint32_t window_id) { (void)window_id; }
void compositor_invalidate_rect(uint32_t window_id, struct gui_rect *rect) {
  (void)window_id;
  (void)rect;
}
void compositor_render(void) {}
void compositor_render_cursor(int32_t x, int32_t y) {
  (void)x;
  (void)y;
}
struct gui_window *compositor_window_at(int32_t x, int32_t y) {
  (void)x;
  (void)y;
  return NULL;
}
struct gui_window *compositor_focused_window(void) { return NULL; }
void compositor_stats_get(struct compositor_stats *out) { (void)out; }
void compositor_set_wallpaper(uint32_t color) { (void)color; }
void compositor_apply_theme(const char *theme, uint32_t screen_w, uint32_t screen_h) {
  (void)theme;
  (void)screen_w;
  (void)screen_h;
}
const struct gui_theme_palette *compositor_theme(void) { return &g_test_theme; }
uint8_t compositor_ui_scale(void) { return 1; }
void compositor_set_desktop_callback(void (*callback)(struct gui_surface *)) {
  (void)callback;
}
int compositor_hit_close_button(struct gui_window *win, int32_t x, int32_t y) {
  (void)win;
  (void)x;
  (void)y;
  return 0;
}

void font_init(void) {}
const struct font *font_default(void) { return &g_test_font; }
void font_draw_char(struct gui_surface *surface, const struct font *f,
                    int32_t x, int32_t y, char c, uint32_t color) {
  (void)surface;
  (void)f;
  (void)x;
  (void)y;
  (void)c;
  (void)color;
}
void font_draw_string(struct gui_surface *surface, const struct font *f,
                      int32_t x, int32_t y, const char *text, uint32_t color) {
  (void)surface;
  (void)f;
  (void)x;
  (void)y;
  (void)text;
  (void)color;
}
uint32_t font_string_width(const struct font *f, const char *text) {
  return (uint32_t)((text ? strlen(text) : 0u) * (f ? f->glyph_width : 8u));
}
uint32_t font_string_height(const struct font *f, const char *text) {
  uint32_t lines = 1;
  if (text) {
    for (const char *p = text; *p; ++p) {
      if (*p == '\n') lines++;
    }
  }
  return lines * (f ? f->glyph_height : 16u);
}
void font_metrics_get(const struct font *f, struct font_metrics *out) {
  if (!out) return;
  out->ascent = f ? f->glyph_height - 2u : 14u;
  out->descent = 2u;
  out->line_height = f ? f->glyph_height : 16u;
  out->avg_width = f ? f->glyph_width : 8u;
}

