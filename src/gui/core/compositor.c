#include "gui/compositor.h"
#include "gui/font.h"
#include "gui/widget.h"
#include "memory/kmem.h"
#include <stddef.h>

#define CURSOR_WIDTH 8
#define CURSOR_HEIGHT 12

static uint32_t *comp_fb = NULL;
static uint32_t *comp_backbuffer = NULL;
static uint32_t comp_width = 0;
static uint32_t comp_height = 0;
static uint32_t comp_pitch = 0;
static uint32_t comp_backbuffer_stride = 0;
static uint32_t comp_wallpaper = 0x002244;
static struct gui_window windows[COMPOSITOR_MAX_WINDOWS];
static uint32_t next_window_id = 1;
static struct compositor_stats comp_stats;
static struct gui_theme_palette g_theme = {
  0x000A1713, 0x00111B18, 0x00213A31, 0x0000A651, 0x00314F44, 0x00E9F8E7,
  0x0092B7A6, 0x0000C364, 0x00213A31, 0x000A1713, 0x00111B18, 0x00E9F8E7,
  0x00213A31, 0x00111B18, 0x00E9F8E7, 1
};
static void (*desktop_paint_cb)(struct gui_surface *) = NULL;
static int comp_scene_dirty = 1;
static int comp_full_presented = 0;
static int comp_cursor_valid = 0;
static int32_t comp_cursor_x = 0;
static int32_t comp_cursor_y = 0;

static void comp_memset32(uint32_t *dst, uint32_t val, size_t count) {
  for (size_t i = 0; i < count; i++) dst[i] = val;
}

static void comp_memcpy(void *dst, const void *src, size_t len) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  for (size_t i = 0; i < len; i++) d[i] = s[i];
}

static void comp_strcpy(char *dst, const char *src, size_t max) {
  size_t i = 0;
  while (i + 1 < max && src && src[i]) {
    dst[i] = src[i];
    i++;
  }
  if (max != 0) dst[i] = '\0';
}

static int comp_streq(const char *a, const char *b) {
  size_t i = 0;
  if (!a || !b) return 0;
  while (a[i] && b[i]) {
    if (a[i] != b[i]) return 0;
    i++;
  }
  return a[i] == b[i];
}

static uint32_t window_title_height(void) {
  return 24;
}

static int window_is_overlay(const struct gui_window *win) {
  return win && !win->decorated && !win->movable && !win->resizable;
}

static uint32_t *alloc_surface(uint32_t w, uint32_t h) {
  size_t pixels = (size_t)w * (size_t)h;
  if (w == 0 || h == 0 || pixels == 0) return NULL;
  return (uint32_t *)kmalloc(pixels * sizeof(uint32_t));
}

static void free_surface(uint32_t *pixels) {
  if (pixels) kfree(pixels);
}

static void reset_window_slot(struct gui_window *win) {
  if (!win) return;
  win->id = 0;
  win->title[0] = '\0';
  win->frame.x = 0;
  win->frame.y = 0;
  win->frame.width = 0;
  win->frame.height = 0;
  win->surface.pixels = NULL;
  win->surface.width = 0;
  win->surface.height = 0;
  win->surface.pitch = 0;
  win->z_order = 0;
  win->visible = 0;
  win->focused = 0;
  win->decorated = 1;
  win->resizable = 1;
  win->movable = 1;
  win->bg_color = 0;
  win->border_color = 0;
  win->user_data = NULL;
  win->on_paint = NULL;
  win->on_close = NULL;
  win->on_resize = NULL;
  win->on_key = NULL;
  win->on_mouse = NULL;
  win->on_scroll = NULL;
}

static void release_window(struct gui_window *win, int notify_close) {
  void (*on_close)(struct gui_window *win) = NULL;
  if (!win || !win->id) return;
  if (notify_close && win->on_close) {
    on_close = win->on_close;
    win->on_close = NULL;
    on_close(win);
  }
  if (win->surface.pixels) {
    free_surface(win->surface.pixels);
  }
  reset_window_slot(win);
}

static void request_scene_redraw(void) {
  comp_scene_dirty = 1;
  comp_full_presented = 0;
  comp_stats.dirty_rects++;
}

void compositor_apply_theme(const char *theme, uint32_t screen_w, uint32_t screen_h) {
  uint8_t scale = (screen_w >= 1280 || screen_h >= 900) ? 2 : 1;

  if (theme && comp_streq(theme, "ocean")) {
    g_theme.wallpaper = 0x00041024;
    g_theme.window_bg = 0x000C213A;
    g_theme.window_border = 0x0021476A;
    g_theme.title_active = 0x0035B7FF;
    g_theme.title_inactive = 0x0021476A;
    g_theme.text = 0x00DDF6FF;
    g_theme.text_muted = 0x0089AFCF;
    g_theme.accent = 0x005FD5FF;
    g_theme.accent_alt = 0x0021476A;
    g_theme.accent_text = 0x00041024;
    g_theme.taskbar_bg = 0x0008192D;
    g_theme.taskbar_fg = 0x00DDF6FF;
    g_theme.taskbar_highlight = 0x0021476A;
    g_theme.terminal_bg = 0x000A1B3A;
    g_theme.terminal_fg = 0x00DDF6FF;
    g_theme.ui_scale = scale;
    comp_wallpaper = g_theme.wallpaper;
    request_scene_redraw();
    return;
  }

  if (theme && comp_streq(theme, "forest")) {
    g_theme.wallpaper = 0x000A1710;
    g_theme.window_bg = 0x0015231A;
    g_theme.window_border = 0x00284A31;
    g_theme.title_active = 0x002FAE5B;
    g_theme.title_inactive = 0x00284A31;
    g_theme.text = 0x00E9F8E7;
    g_theme.text_muted = 0x0092B7A6;
    g_theme.accent = 0x0048D778;
    g_theme.accent_alt = 0x00284A31;
    g_theme.accent_text = 0x000A1710;
    g_theme.taskbar_bg = 0x000D1A12;
    g_theme.taskbar_fg = 0x00E9F8E7;
    g_theme.taskbar_highlight = 0x00284A31;
    g_theme.terminal_bg = 0x000F2415;
    g_theme.terminal_fg = 0x00E9F8E7;
    g_theme.ui_scale = scale;
    comp_wallpaper = g_theme.wallpaper;
    request_scene_redraw();
    return;
  }

  if (theme && comp_streq(theme, "high-contrast")) {
    g_theme.wallpaper = 0x00000000;
    g_theme.window_bg = 0x00000000;
    g_theme.window_border = 0x00FFFFFF;
    g_theme.title_active = 0x00FFFF00;
    g_theme.title_inactive = 0x00808080;
    g_theme.text = 0x00FFFFFF;
    g_theme.text_muted = 0x00C0C0C0;
    g_theme.accent = 0x00FFFF00;
    g_theme.accent_alt = 0x00404040;
    g_theme.accent_text = 0x00000000;
    g_theme.taskbar_bg = 0x00000000;
    g_theme.taskbar_fg = 0x00FFFFFF;
    g_theme.taskbar_highlight = 0x00404040;
    g_theme.terminal_bg = 0x00000000;
    g_theme.terminal_fg = 0x00FFFFFF;
    g_theme.ui_scale = scale;
    comp_wallpaper = g_theme.wallpaper;
    request_scene_redraw();
    return;
  }

  g_theme.wallpaper = 0x000A1713;
  g_theme.window_bg = 0x00111B18;
  g_theme.window_border = 0x00213A31;
  g_theme.title_active = 0x0000A651;
  g_theme.title_inactive = 0x00314F44;
  g_theme.text = 0x00E9F8E7;
  g_theme.text_muted = 0x0092B7A6;
  g_theme.accent = 0x0000C364;
  g_theme.accent_alt = 0x00213A31;
  g_theme.accent_text = 0x000A1713;
  g_theme.taskbar_bg = 0x000C1715;
  g_theme.taskbar_fg = 0x00E9F8E7;
  g_theme.taskbar_highlight = 0x00213A31;
  g_theme.terminal_bg = 0x00102030;
  g_theme.terminal_fg = 0x00F0F0F0;
  g_theme.ui_scale = scale;
  comp_wallpaper = g_theme.wallpaper;
  request_scene_redraw();
}

const struct gui_theme_palette *compositor_theme(void) {
  return &g_theme;
}

uint8_t compositor_ui_scale(void) {
  return g_theme.ui_scale;
}

void compositor_init(uint32_t *framebuffer, uint32_t width, uint32_t height,
                     uint32_t pitch) {
  compositor_shutdown();

  comp_fb = framebuffer;
  comp_width = width;
  comp_height = height;
  comp_pitch = pitch;
  comp_backbuffer_stride = pitch / 4;
  if (comp_backbuffer_stride < comp_width) comp_backbuffer_stride = comp_width;
  if (comp_backbuffer_stride != 0 && comp_height != 0) {
    comp_backbuffer = alloc_surface(comp_backbuffer_stride, comp_height);
  }

  compositor_apply_theme(NULL, width, height);

  next_window_id = 1;
  comp_stats.window_count = 0;
  comp_stats.visible_count = 0;
  comp_stats.frames_rendered = 0;
  comp_stats.dirty_rects = 0;
  comp_scene_dirty = 1;
  comp_full_presented = 0;
  comp_cursor_valid = 0;
  comp_cursor_x = 0;
  comp_cursor_y = 0;
}

void compositor_shutdown(void) {
  for (int i = 0; i < COMPOSITOR_MAX_WINDOWS; i++) {
    release_window(&windows[i], 1);
  }

  if (comp_backbuffer) {
    free_surface(comp_backbuffer);
    comp_backbuffer = NULL;
  }

  comp_fb = NULL;
  comp_width = 0;
  comp_height = 0;
  comp_pitch = 0;
  comp_backbuffer_stride = 0;
  next_window_id = 1;
  comp_stats.window_count = 0;
  comp_stats.visible_count = 0;
  comp_stats.frames_rendered = 0;
  comp_stats.dirty_rects = 0;
  desktop_paint_cb = NULL;
  comp_scene_dirty = 1;
  comp_full_presented = 0;
  comp_cursor_valid = 0;
  comp_cursor_x = 0;
  comp_cursor_y = 0;
}

static struct gui_window *find_window(uint32_t id) {
  for (int i = 0; i < COMPOSITOR_MAX_WINDOWS; i++) {
    if (windows[i].id == id) return &windows[i];
  }
  return NULL;
}

struct gui_window *compositor_create_window(const char *title, int32_t x,
                                            int32_t y, uint32_t w, uint32_t h) {
  for (int i = 0; i < COMPOSITOR_MAX_WINDOWS; i++) {
    if (windows[i].id == 0) {
      struct gui_window *win = &windows[i];
      uint32_t *pixels = NULL;
      win->id = next_window_id++;
      comp_strcpy(win->title, title ? title : "Window", 64);
      win->frame.x = x;
      win->frame.y = y;
      win->frame.width = w;
      win->frame.height = h;
      pixels = alloc_surface(w, h);
      if (!pixels) {
        win->id = 0;
        return NULL;
      }
      win->surface.width = w;
      win->surface.height = h;
      win->surface.pitch = w * 4;
      win->surface.pixels = pixels;
      comp_memset32(win->surface.pixels, g_theme.window_bg, (size_t)w * h);
      win->z_order = comp_stats.window_count;
      win->visible = 0;
      win->focused = 0;
      win->decorated = 1;
      win->resizable = 1;
      win->movable = 1;
      win->bg_color = g_theme.window_bg;
      win->border_color = g_theme.window_border;
      win->user_data = NULL;
      win->on_paint = NULL;
      win->on_close = NULL;
      win->on_resize = NULL;
      win->on_key = NULL;
      win->on_mouse = NULL;
      win->on_scroll = NULL;
      comp_stats.window_count++;
      return win;
    }
  }
  return NULL;
}

void compositor_destroy_window(uint32_t window_id) {
  struct gui_window *win = find_window(window_id);
  if (!win) return;
  /* Notify the app so it can clean up widgets and state. This is a
   * safety net — the desktop click handler already calls on_close
   * before reaching here, but direct callers might skip it. */
  if (win->on_close) {
    win->on_close(win);
    win->on_close = NULL; /* prevent double-call */
  }
  if (win->surface.pixels) {
    free_surface(win->surface.pixels);
    win->surface.pixels = NULL;
  }
  if (win->visible && comp_stats.visible_count > 0) comp_stats.visible_count--;
  win->id = 0;
  win->visible = 0;
  win->focused = 0;
  win->user_data = NULL;
  if (comp_stats.window_count > 0) comp_stats.window_count--;
  request_scene_redraw();
}

void compositor_show_window(uint32_t window_id) {
  struct gui_window *win = find_window(window_id);
  if (!win || win->visible) return;
  win->visible = 1;
  comp_stats.visible_count++;
  request_scene_redraw();
}

void compositor_hide_window(uint32_t window_id) {
  struct gui_window *win = find_window(window_id);
  if (!win || !win->visible) return;
  win->visible = 0;
  if (comp_stats.visible_count > 0) comp_stats.visible_count--;
  request_scene_redraw();
}

void compositor_focus_window(uint32_t window_id) {
  struct gui_window *target = find_window(window_id);
  uint32_t top_z = 0;
  if (!target) return;

  for (int i = 0; i < COMPOSITOR_MAX_WINDOWS; i++) {
    if (!windows[i].id || window_is_overlay(&windows[i])) continue;
    if (windows[i].z_order > top_z) top_z = windows[i].z_order;
  }

  if (!window_is_overlay(target) && target->z_order < top_z) {
    for (int i = 0; i < COMPOSITOR_MAX_WINDOWS; i++) {
      if (!windows[i].id || &windows[i] == target || window_is_overlay(&windows[i]))
        continue;
      if (windows[i].z_order > target->z_order) windows[i].z_order--;
    }
    target->z_order = top_z;
  }

  for (int i = 0; i < COMPOSITOR_MAX_WINDOWS; i++) {
    windows[i].focused = (windows[i].id == window_id) ? 1 : 0;
  }

  request_scene_redraw();
}

void compositor_move_window(uint32_t window_id, int32_t x, int32_t y) {
  struct gui_window *win = find_window(window_id);
  if (!win) return;
  if (win->frame.x == x && win->frame.y == y) return;
  win->frame.x = x;
  win->frame.y = y;
  request_scene_redraw();
}

void compositor_resize_window(uint32_t window_id, uint32_t w, uint32_t h) {
  struct gui_window *win = find_window(window_id);
  uint32_t *pixels = NULL;
  if (!win || !win->resizable) return;
  if (win->frame.width == w && win->frame.height == h) return;
  pixels = alloc_surface(w, h);
  if (!pixels) return;
  comp_memset32(pixels, win->bg_color, (size_t)w * h);
  free_surface(win->surface.pixels);
  win->frame.width = w;
  win->frame.height = h;
  win->surface.width = w;
  win->surface.height = h;
  win->surface.pitch = w * 4;
  win->surface.pixels = pixels;
  if (win->on_resize) win->on_resize(win, w, h);
  request_scene_redraw();
}

void compositor_set_title(uint32_t window_id, const char *title) {
  struct gui_window *win = find_window(window_id);
  if (!win || !title) return;
  comp_strcpy(win->title, title, 64);
  request_scene_redraw();
}

void compositor_invalidate(uint32_t window_id) {
  if (!find_window(window_id)) return;
  request_scene_redraw();
}

void compositor_invalidate_rect(uint32_t window_id, struct gui_rect *rect) {
  (void)rect;
  compositor_invalidate(window_id);
}

static void render_window_decoration(struct gui_window *win, uint32_t *buf,
                                     uint32_t buf_stride) {
  if (!win->decorated) return;
  int32_t x = win->frame.x;
  int32_t y = win->frame.y;
  uint32_t w = win->frame.width;

  uint32_t title_color = win->focused ? g_theme.title_active : g_theme.title_inactive;
  uint32_t title_h = window_title_height();

  for (uint32_t row = 0; row < title_h; row++) {
    int32_t py = y - (int32_t)title_h + (int32_t)row;
    if (py < 0 || (uint32_t)py >= comp_height) continue;
    for (uint32_t col = 0; col < w; col++) {
      int32_t px = x + (int32_t)col;
      if (px < 0 || (uint32_t)px >= comp_width) continue;
      buf[py * buf_stride + px] = title_color;
    }
  }

  {
    const struct font *f = font_default();
    if (f) {
      struct gui_surface title_surf = { buf, comp_width, comp_height, buf_stride * 4 };
      font_draw_string(&title_surf, f, x + 4, y - (int32_t)title_h + 4,
                       win->title, g_theme.text);

      /* Close button [X] on the right side of the title bar */
      int32_t btn_size = (int32_t)title_h - 6;
      int32_t btn_x = x + (int32_t)w - btn_size - 4;
      int32_t btn_y = y - (int32_t)title_h + 3;
      /* Button background */
      for (int32_t row = 0; row < btn_size; row++) {
        int32_t py = btn_y + row;
        if (py < 0 || (uint32_t)py >= comp_height) continue;
        for (int32_t col = 0; col < btn_size; col++) {
          int32_t px = btn_x + col;
          if (px < 0 || (uint32_t)px >= comp_width) continue;
          buf[py * buf_stride + px] = g_theme.accent_alt;
        }
      }
      font_draw_string(&title_surf, f, btn_x + (btn_size / 2) - 3,
                       btn_y + (btn_size / 2) - (int32_t)(f->glyph_height / 2),
                       "X", g_theme.text);
    }
  }
}

static void compose_scene(uint32_t *buf, uint32_t buf_stride) {
  uint32_t max_z = 0;

  if (!buf || buf_stride == 0) return;

  for (uint32_t y = 0; y < comp_height; y++) {
    comp_memset32(buf + y * buf_stride, comp_wallpaper, comp_width);
  }

  if (desktop_paint_cb) {
    struct gui_surface desktop = { buf, comp_width, comp_height, buf_stride * 4 };
    desktop_paint_cb(&desktop);
  }

  for (int i = 0; i < COMPOSITOR_MAX_WINDOWS; i++) {
    if (windows[i].id && windows[i].visible && windows[i].z_order > max_z) {
      max_z = windows[i].z_order;
    }
  }

  for (uint32_t z = 0; z <= max_z; z++) {
    for (int i = 0; i < COMPOSITOR_MAX_WINDOWS; i++) {
      struct gui_window *win = &windows[i];
      if (!win->id || !win->visible || win->z_order != z) continue;
      if (!win->surface.pixels) continue;

      if (win->on_paint) win->on_paint(win);
      render_window_decoration(win, buf, buf_stride);

      {
        int32_t wx = win->frame.x;
        int32_t wy = win->frame.y;
        uint32_t sw = win->surface.width;
        for (uint32_t row = 0; row < win->frame.height; row++) {
          int32_t py = wy + (int32_t)row;
          int32_t px_start = 0;
          int32_t px_end = 0;
          uint32_t col_start = 0;
          uint32_t copy_len = 0;
          if (py < 0 || (uint32_t)py >= comp_height) continue;
          px_start = wx < 0 ? 0 : wx;
          px_end = wx + (int32_t)win->frame.width;
          if (px_end > (int32_t)comp_width) px_end = (int32_t)comp_width;
          if (px_start >= px_end) continue;
          col_start = (uint32_t)(px_start - wx);
          copy_len = (uint32_t)(px_end - px_start);
          comp_memcpy(&buf[py * buf_stride + px_start],
                      &win->surface.pixels[row * sw + col_start],
                      copy_len * 4);
        }
      }
    }
  }
}

static void present_full_frame_from_backbuffer(void) {
  uint32_t front_stride = comp_pitch / 4;
  if (!comp_fb || !comp_backbuffer || front_stride == 0) return;
  for (uint32_t y = 0; y < comp_height; y++) {
    comp_memcpy(&comp_fb[y * front_stride],
                &comp_backbuffer[y * comp_backbuffer_stride],
                comp_width * sizeof(uint32_t));
  }
}

static void copy_backbuffer_rect_to_front(int32_t x, int32_t y,
                                          uint32_t w, uint32_t h) {
  int32_t x0 = x;
  int32_t y0 = y;
  int32_t x1 = x + (int32_t)w;
  int32_t y1 = y + (int32_t)h;
  uint32_t front_stride = comp_pitch / 4;

  if (!comp_fb || !comp_backbuffer || front_stride == 0 || comp_backbuffer_stride == 0) {
    return;
  }

  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > (int32_t)comp_width) x1 = (int32_t)comp_width;
  if (y1 > (int32_t)comp_height) y1 = (int32_t)comp_height;
  if (x0 >= x1 || y0 >= y1) return;

  for (int32_t py = y0; py < y1; py++) {
    comp_memcpy(&comp_fb[(uint32_t)py * front_stride + (uint32_t)x0],
                &comp_backbuffer[(uint32_t)py * comp_backbuffer_stride + (uint32_t)x0],
                (size_t)(x1 - x0) * sizeof(uint32_t));
  }
}

static void draw_cursor_on_front(int32_t x, int32_t y) {
  static const uint8_t cursor_mask[CURSOR_HEIGHT] = {
    0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC,
    0xFE, 0xFC, 0xF8, 0xD0, 0x88, 0x04
  };

  if (!comp_fb) return;

  for (int row = 0; row < CURSOR_HEIGHT; row++) {
    int32_t py = y + row;
    if (py < 0 || (uint32_t)py >= comp_height) continue;
    {
      uint32_t *line = (uint32_t *)((uint8_t *)comp_fb + py * comp_pitch);
      for (int col = 0; col < CURSOR_WIDTH; col++) {
        int32_t px = x + col;
        if (!(cursor_mask[row] & (0x80 >> col))) continue;
        if (px < 0 || (uint32_t)px >= comp_width) continue;
        line[px] = (col == 0 || row == 0) ? 0x000000 : g_theme.text;
      }
    }
  }
}

void compositor_render(void) {
  uint32_t front_stride = comp_pitch / 4;
  if (!comp_fb || front_stride == 0) return;

  comp_full_presented = 0;
  if (!comp_scene_dirty) return;

  if (comp_backbuffer) {
    compose_scene(comp_backbuffer, comp_backbuffer_stride);
    present_full_frame_from_backbuffer();
  } else {
    compose_scene(comp_fb, front_stride);
  }

  comp_scene_dirty = 0;
  comp_full_presented = 1;
  comp_stats.frames_rendered++;
}

void compositor_render_cursor(int32_t x, int32_t y) {
  if (!comp_fb) return;

  if (!comp_full_presented && comp_cursor_valid &&
      comp_cursor_x == x && comp_cursor_y == y) {
    return;
  }

  if (comp_backbuffer && !comp_full_presented && comp_cursor_valid) {
    copy_backbuffer_rect_to_front(comp_cursor_x, comp_cursor_y,
                                  CURSOR_WIDTH, CURSOR_HEIGHT);
  }

  draw_cursor_on_front(x, y);
  comp_cursor_x = x;
  comp_cursor_y = y;
  comp_cursor_valid = 1;
  comp_full_presented = 0;
}

struct gui_window *compositor_window_at(int32_t x, int32_t y) {
  struct gui_window *top = NULL;
  uint32_t top_z = 0;
  for (int i = 0; i < COMPOSITOR_MAX_WINDOWS; i++) {
    struct gui_window *w = &windows[i];
    int32_t win_top = w->frame.y - (w->decorated ? (int32_t)window_title_height() : 0);
    int32_t bottom = w->frame.y + (int32_t)w->frame.height;
    if (!w->id || !w->visible) continue;
    if (x >= w->frame.x && x < w->frame.x + (int32_t)w->frame.width &&
        y >= win_top && y < bottom) {
      if (!top || w->z_order >= top_z) {
        top = w;
        top_z = w->z_order;
      }
    }
  }
  return top;
}

struct gui_window *compositor_focused_window(void) {
  for (int i = 0; i < COMPOSITOR_MAX_WINDOWS; i++) {
    if (windows[i].id && windows[i].focused) return &windows[i];
  }
  return NULL;
}

void compositor_stats_get(struct compositor_stats *out) {
  if (out) *out = comp_stats;
}

void compositor_set_wallpaper(uint32_t color) {
  if (comp_wallpaper == color) return;
  comp_wallpaper = color;
  request_scene_redraw();
}

void compositor_set_desktop_callback(void (*callback)(struct gui_surface *)) {
  desktop_paint_cb = callback;
  request_scene_redraw();
}

int compositor_hit_close_button(struct gui_window *win, int32_t x, int32_t y) {
  if (!win || !win->decorated) return 0;
  uint32_t title_h = window_title_height();
  int32_t btn_size = (int32_t)title_h - 6;
  int32_t btn_x = win->frame.x + (int32_t)win->frame.width - btn_size - 4;
  int32_t btn_y = win->frame.y - (int32_t)title_h + 3;
  return (x >= btn_x && x < btn_x + btn_size &&
          y >= btn_y && y < btn_y + btn_size);
}
