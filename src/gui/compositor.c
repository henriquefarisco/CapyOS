#include "gui/compositor.h"
#include "gui/font.h"
#include "memory/kmem.h"
#include <stddef.h>

static uint32_t *comp_fb = NULL;
static uint32_t comp_width = 0;
static uint32_t comp_height = 0;
static uint32_t comp_pitch = 0;
static uint32_t comp_wallpaper = 0x002244;
static struct gui_window windows[COMPOSITOR_MAX_WINDOWS];
static uint32_t next_window_id = 1;
static struct compositor_stats comp_stats;
static uint32_t *back_buffer = NULL;
static void (*desktop_paint_cb)(struct gui_surface *) = NULL;

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
  while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
  dst[i] = '\0';
}

void compositor_init(uint32_t *framebuffer, uint32_t width, uint32_t height,
                     uint32_t pitch) {
  comp_fb = framebuffer;
  comp_width = width;
  comp_height = height;
  comp_pitch = pitch;

  for (int i = 0; i < COMPOSITOR_MAX_WINDOWS; i++) {
    windows[i].id = 0;
    windows[i].visible = 0;
  }

  next_window_id = 1;
  comp_stats.window_count = 0;
  comp_stats.visible_count = 0;
  comp_stats.frames_rendered = 0;
  comp_stats.dirty_rects = 0;

  back_buffer = (uint32_t *)kmalloc(width * height * 4);
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
      win->id = next_window_id++;
      comp_strcpy(win->title, title ? title : "Window", 64);
      win->frame.x = x;
      win->frame.y = y;
      win->frame.width = w;
      win->frame.height = h;
      win->surface.width = w;
      win->surface.height = h;
      win->surface.pitch = w * 4;
      win->surface.pixels = (uint32_t *)kmalloc(w * h * 4);
      if (win->surface.pixels) {
        comp_memset32(win->surface.pixels, 0xFFFFFF, w * h);
      }
      win->z_order = comp_stats.window_count;
      win->visible = 0;
      win->focused = 0;
      win->decorated = 1;
      win->resizable = 1;
      win->movable = 1;
      win->bg_color = 0xF0F0F0;
      win->border_color = 0x404040;
      win->user_data = NULL;
      win->on_paint = NULL;
      win->on_close = NULL;
      win->on_resize = NULL;
      win->on_key = NULL;
      win->on_mouse = NULL;
      comp_stats.window_count++;
      return win;
    }
  }
  return NULL;
}

void compositor_destroy_window(uint32_t window_id) {
  struct gui_window *win = find_window(window_id);
  if (!win) return;
  if (win->surface.pixels) {
    kfree(win->surface.pixels);
    win->surface.pixels = NULL;
  }
  if (win->visible && comp_stats.visible_count > 0) comp_stats.visible_count--;
  win->id = 0;
  if (comp_stats.window_count > 0) comp_stats.window_count--;
}

void compositor_show_window(uint32_t window_id) {
  struct gui_window *win = find_window(window_id);
  if (!win || win->visible) return;
  win->visible = 1;
  comp_stats.visible_count++;
}

void compositor_hide_window(uint32_t window_id) {
  struct gui_window *win = find_window(window_id);
  if (!win || !win->visible) return;
  win->visible = 0;
  if (comp_stats.visible_count > 0) comp_stats.visible_count--;
}

void compositor_focus_window(uint32_t window_id) {
  for (int i = 0; i < COMPOSITOR_MAX_WINDOWS; i++) {
    windows[i].focused = (windows[i].id == window_id) ? 1 : 0;
  }
}

void compositor_move_window(uint32_t window_id, int32_t x, int32_t y) {
  struct gui_window *win = find_window(window_id);
  if (win) { win->frame.x = x; win->frame.y = y; }
}

void compositor_resize_window(uint32_t window_id, uint32_t w, uint32_t h) {
  struct gui_window *win = find_window(window_id);
  if (!win || !win->resizable) return;
  if (win->surface.pixels) kfree(win->surface.pixels);
  win->frame.width = w;
  win->frame.height = h;
  win->surface.width = w;
  win->surface.height = h;
  win->surface.pitch = w * 4;
  win->surface.pixels = (uint32_t *)kmalloc(w * h * 4);
  if (win->surface.pixels) comp_memset32(win->surface.pixels, win->bg_color, w * h);
  if (win->on_resize) win->on_resize(win, w, h);
}

void compositor_set_title(uint32_t window_id, const char *title) {
  struct gui_window *win = find_window(window_id);
  if (win && title) comp_strcpy(win->title, title, 64);
}

void compositor_invalidate(uint32_t window_id) {
  struct gui_window *win = find_window(window_id);
  if (win && win->on_paint) win->on_paint(win);
  comp_stats.dirty_rects++;
}

void compositor_invalidate_rect(uint32_t window_id, struct gui_rect *rect) {
  (void)rect;
  compositor_invalidate(window_id);
}

static void render_window_decoration(struct gui_window *win, uint32_t *buf,
                                      uint32_t buf_w) {
  if (!win->decorated) return;
  int32_t x = win->frame.x;
  int32_t y = win->frame.y;
  uint32_t w = win->frame.width;

  uint32_t title_color = win->focused ? 0x3366AA : 0x888888;
  uint32_t title_h = 24;

  for (uint32_t row = 0; row < title_h; row++) {
    int32_t py = y - (int32_t)title_h + (int32_t)row;
    if (py < 0 || (uint32_t)py >= comp_height) continue;
    for (uint32_t col = 0; col < w; col++) {
      int32_t px = x + (int32_t)col;
      if (px < 0 || (uint32_t)px >= buf_w) continue;
      buf[py * buf_w + px] = title_color;
    }
  }

  const struct font *f = font_default();
  if (f) {
    struct gui_surface title_surf = { buf, buf_w, comp_height, buf_w * 4 };
    font_draw_string(&title_surf, f, x + 4, y - (int32_t)title_h + 4,
                     win->title, 0xFFFFFF);
  }
}

void compositor_render(void) {
  uint32_t *buf = back_buffer ? back_buffer : comp_fb;
  uint32_t buf_w = comp_width;

  comp_memset32(buf, comp_wallpaper, comp_width * comp_height);

  if (desktop_paint_cb) {
    struct gui_surface desktop = { buf, comp_width, comp_height, comp_width * 4 };
    desktop_paint_cb(&desktop);
  }

  for (uint32_t z = 0; z < comp_stats.window_count + 1; z++) {
    for (int i = 0; i < COMPOSITOR_MAX_WINDOWS; i++) {
      struct gui_window *win = &windows[i];
      if (!win->id || !win->visible || win->z_order != z) continue;

      if (win->on_paint) win->on_paint(win);
      render_window_decoration(win, buf, buf_w);

      int32_t wx = win->frame.x;
      int32_t wy = win->frame.y;
      for (uint32_t row = 0; row < win->frame.height; row++) {
        int32_t py = wy + (int32_t)row;
        if (py < 0 || (uint32_t)py >= comp_height) continue;
        for (uint32_t col = 0; col < win->frame.width; col++) {
          int32_t px = wx + (int32_t)col;
          if (px < 0 || (uint32_t)px >= comp_width) continue;
          uint32_t src_pixel = win->surface.pixels[row * win->surface.width + col];
          buf[py * buf_w + px] = src_pixel;
        }
      }
    }
  }

  if (back_buffer && comp_fb) {
    comp_memcpy(comp_fb, back_buffer, comp_width * comp_height * 4);
  }

  comp_stats.frames_rendered++;
}

void compositor_render_cursor(int32_t x, int32_t y) {
  if (!comp_fb) return;
  uint32_t cursor_color = 0xFFFFFF;
  uint32_t outline_color = 0x000000;
  static const uint8_t cursor_mask[12] = {
    0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC,
    0xFE, 0xFC, 0xF8, 0xD0, 0x88, 0x04
  };
  for (int row = 0; row < 12; row++) {
    int32_t py = y + row;
    if (py < 0 || (uint32_t)py >= comp_height) continue;
    uint32_t *line = (uint32_t *)((uint8_t *)comp_fb + py * comp_pitch);
    for (int col = 0; col < 8; col++) {
      if (!(cursor_mask[row] & (0x80 >> col))) continue;
      int32_t px = x + col;
      if (px < 0 || (uint32_t)px >= comp_width) continue;
      line[px] = (col == 0 || row == 0) ? outline_color : cursor_color;
    }
  }
}

struct gui_window *compositor_window_at(int32_t x, int32_t y) {
  struct gui_window *top = NULL;
  uint32_t top_z = 0;
  for (int i = 0; i < COMPOSITOR_MAX_WINDOWS; i++) {
    struct gui_window *w = &windows[i];
    if (!w->id || !w->visible) continue;
    if (x >= w->frame.x && x < w->frame.x + (int32_t)w->frame.width &&
        y >= w->frame.y && y < w->frame.y + (int32_t)w->frame.height) {
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
  comp_wallpaper = color;
}

void compositor_set_desktop_callback(void (*callback)(struct gui_surface *)) {
  desktop_paint_cb = callback;
}
