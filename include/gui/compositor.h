#ifndef GUI_COMPOSITOR_H
#define GUI_COMPOSITOR_H

#include <stdint.h>
#include <stddef.h>

#define COMPOSITOR_MAX_WINDOWS 32
#define COMPOSITOR_MAX_LAYERS  8

struct gui_rect {
  int32_t x, y;
  uint32_t width, height;
};

struct gui_surface {
  uint32_t *pixels;
  uint32_t width;
  uint32_t height;
  uint32_t pitch;
};

struct gui_theme_palette {
  uint32_t wallpaper;
  uint32_t window_bg;
  uint32_t window_border;
  uint32_t title_active;
  uint32_t title_inactive;
  uint32_t text;
  uint32_t text_muted;
  uint32_t accent;
  uint32_t accent_alt;
  uint32_t accent_text;
  uint32_t taskbar_bg;
  uint32_t taskbar_fg;
  uint32_t taskbar_highlight;
  uint32_t terminal_bg;
  uint32_t terminal_fg;
  uint8_t ui_scale;
};

struct gui_window {
  uint32_t id;
  char title[64];
  struct gui_rect frame;
  struct gui_surface surface;
  uint32_t z_order;
  int visible;
  int focused;
  int decorated;
  int resizable;
  int movable;
  uint32_t bg_color;
  uint32_t border_color;
  void *user_data;
  void (*on_paint)(struct gui_window *win);
  void (*on_close)(struct gui_window *win);
  void (*on_resize)(struct gui_window *win, uint32_t w, uint32_t h);
  void (*on_key)(struct gui_window *win, uint32_t keycode, uint8_t mods);
  void (*on_mouse)(struct gui_window *win, int32_t x, int32_t y, uint8_t btns);
  void (*on_scroll)(struct gui_window *win, int32_t delta);
};

struct compositor_stats {
  uint32_t window_count;
  uint32_t visible_count;
  uint64_t frames_rendered;
  uint64_t dirty_rects;
};

void compositor_init(uint32_t *framebuffer, uint32_t width, uint32_t height,
                     uint32_t pitch);
struct gui_window *compositor_create_window(const char *title, int32_t x,
                                            int32_t y, uint32_t w, uint32_t h);
void compositor_destroy_window(uint32_t window_id);
void compositor_show_window(uint32_t window_id);
void compositor_hide_window(uint32_t window_id);
void compositor_focus_window(uint32_t window_id);
void compositor_move_window(uint32_t window_id, int32_t x, int32_t y);
void compositor_resize_window(uint32_t window_id, uint32_t w, uint32_t h);
void compositor_set_title(uint32_t window_id, const char *title);
void compositor_invalidate(uint32_t window_id);
void compositor_invalidate_rect(uint32_t window_id, struct gui_rect *rect);
void compositor_render(void);
void compositor_render_cursor(int32_t x, int32_t y);
struct gui_window *compositor_window_at(int32_t x, int32_t y);
struct gui_window *compositor_focused_window(void);
void compositor_stats_get(struct compositor_stats *out);
void compositor_set_wallpaper(uint32_t color);
void compositor_apply_theme(const char *theme, uint32_t screen_w, uint32_t screen_h);
const struct gui_theme_palette *compositor_theme(void);
uint8_t compositor_ui_scale(void);
void compositor_set_desktop_callback(void (*callback)(struct gui_surface *));
int compositor_hit_close_button(struct gui_window *win, int32_t x, int32_t y);

#endif /* GUI_COMPOSITOR_H */
