/*
 * src/gui/core/compositor.c
 *
 * Window lifecycle owner + storage definitions for the shared
 * compositor state. Theme palette is in `compositor_theme.c`;
 * scene composition + cursor rendering is in `compositor_render.c`.
 * The split happened on 2026-05-02 (Etapa 2 audit) when the
 * original monolithic file crossed the 900-line layout-audit cap.
 *
 * This TU owns:
 *   - All shared state (`comp_fb`, `comp_backbuffer`, palette,
 *     `comp_windows[]`, etc.) - defined here, declared `extern`
 *     in `internal/compositor_internal.h`.
 *   - Window CRUD: create / destroy / show / hide / focus / move
 *     / resize / set_title / invalidate.
 *   - Hit-testing helpers: `compositor_window_at`,
 *     `compositor_focused_window`, `compositor_hit_close_button`.
 *   - Stats accessor and wallpaper setter.
 *   - Lifecycle: init / shutdown.
 */

#include "internal/compositor_internal.h"
#include "gui/event.h"
#include "gui/compositor_smoke.h"
#include "memory/kmem.h"
#include <stddef.h>

/* === Shared state storage (definitions for extern in internal hdr) ===== */
uint32_t *comp_fb = NULL;
uint32_t *comp_backbuffer = NULL;
uint32_t  comp_width = 0;
uint32_t  comp_height = 0;
uint32_t  comp_pitch = 0;
uint32_t  comp_backbuffer_stride = 0;
uint32_t  comp_wallpaper = 0x002244;
struct gui_window comp_windows[COMPOSITOR_MAX_WINDOWS];
struct compositor_stats comp_stats;
struct gui_theme_palette g_theme = {
  0x000A1713, 0x00111B18, 0x00213A31, 0x0000A651, 0x00314F44, 0x00E9F8E7,
  0x0092B7A6, 0x0000C364, 0x00213A31, 0x000A1713, 0x00111B18, 0x00E9F8E7,
  0x00213A31, 0x00111B18, 0x00E9F8E7, 1
};
void (*comp_desktop_paint_cb)(struct gui_surface *) = NULL;
int      comp_scene_dirty = 1;
int      comp_full_presented = 0;
int      comp_cursor_valid = 0;
int32_t  comp_cursor_x = 0;
int32_t  comp_cursor_y = 0;
/* Etapa F4 cursors (2026-05-03): kind atual do cursor. */
uint8_t  comp_cursor_kind_active = (uint8_t)COMP_CURSOR_ARROW;
uint8_t  comp_cursor_kind_rendered = (uint8_t)COMP_CURSOR_ARROW;

/* Private to this TU: monotonically incremented allocator for
 * window IDs. Resets on `compositor_init`/`shutdown`. */
static uint32_t next_window_id = 1;

/* === Shared helpers (declared extern in internal hdr) ================== */
void comp_memset32(uint32_t *dst, uint32_t val, size_t count) {
  for (size_t i = 0; i < count; i++) dst[i] = val;
}

void comp_memcpy(void *dst, const void *src, size_t len) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  for (size_t i = 0; i < len; i++) d[i] = s[i];
}

int comp_streq(const char *a, const char *b) {
  size_t i = 0;
  if (!a || !b) return 0;
  while (a[i] && b[i]) {
    if (a[i] != b[i]) return 0;
    i++;
  }
  return a[i] == b[i];
}

uint32_t comp_window_title_height(void) {
  return 24;
}

/* Returns 1 if (col,row) - both relative to the window's top-left,
 * including the title bar height - lies inside the rounded
 * rectangle of total size (w, total_h). */
int comp_window_pixel_inside(uint32_t col, uint32_t row,
                             uint32_t w, uint32_t total_h, uint32_t r) {
  int in_left  = (col < r);
  int in_right = (col + r >= w);
  int in_top    = (row < r);
  int in_bot    = (row + r >= total_h);
  if (!(in_left || in_right) || !(in_top || in_bot)) return 1;
  int32_t dx, dy;
  if (in_top) {
    dy = (int32_t)((r - 1u) - row);
  } else {
    dy = (int32_t)(row - (total_h - r));
  }
  if (in_left) {
    dx = (int32_t)((r - 1u) - col);
  } else {
    dx = (int32_t)(col - (w - r));
  }
  /* Strict-less, so the perimeter pixel just outside the disc is
   * masked. Comparing against `(r-1)*(r-1)` would over-trim. */
  return (dx * dx + dy * dy) < (int32_t)(r * r);
}

int compositor_needs_render(void) {
  return comp_scene_dirty ? 1 : 0;
}

int compositor_cursor_needs_render(int32_t x, int32_t y) {
  if (!comp_cursor_valid) return 1;
  if (comp_cursor_kind_rendered != comp_cursor_kind_active) return 1;
  return (comp_cursor_x != x || comp_cursor_y != y) ? 1 : 0;
}

void comp_request_scene_redraw(void) {
  comp_scene_dirty = 1;
  comp_full_presented = 0;
  comp_dirty_mark_full_redraw();
  comp_stats.dirty_rects++;
}

struct gui_window *comp_find_window(uint32_t id) {
  for (int i = 0; i < COMPOSITOR_MAX_WINDOWS; i++) {
    if (comp_windows[i].id == id) return &comp_windows[i];
  }
  return NULL;
}

/* === Private helpers (this TU only) ==================================== */
static void comp_strcpy(char *dst, const char *src, size_t max) {
  size_t i = 0;
  while (i + 1 < max && src && src[i]) {
    dst[i] = src[i];
    i++;
  }
  if (max != 0) dst[i] = '\0';
}

static int window_is_overlay(const struct gui_window *win) {
  return win && !win->decorated && !win->movable && !win->resizable;
}

static uint32_t *alloc_surface(uint32_t w, uint32_t h) {
  size_t pixels = (size_t)w * (size_t)h;
  if (w == 0 || h == 0 || pixels == 0) return NULL;
  /* Fail closed on absurd dimensions: keeps w*h*4 well inside size_t so
   * the allocation can never wrap to an undersized buffer that callers
   * would then treat as w*h pixels (out-of-bounds surface). */
  if (w > COMPOSITOR_MAX_SURFACE_DIM || h > COMPOSITOR_MAX_SURFACE_DIM)
    return NULL;
  return (uint32_t *)kmalloc(pixels * sizeof(uint32_t));
}

static void free_surface(uint32_t *pixels) {
  if (pixels) kfree(pixels);
}

static int clip_rect_to_screen(const struct gui_rect *rect,
                               struct gui_rect *out) {
  int64_t x0;
  int64_t y0;
  int64_t x1;
  int64_t y1;
  if (!rect || !out || rect->width == 0u || rect->height == 0u) return 0;
  x0 = rect->x;
  y0 = rect->y;
  x1 = (int64_t)rect->x + (int64_t)rect->width;
  y1 = (int64_t)rect->y + (int64_t)rect->height;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > (int32_t)comp_width) x1 = (int32_t)comp_width;
  if (y1 > (int32_t)comp_height) y1 = (int32_t)comp_height;
  if (x0 >= x1 || y0 >= y1) return 0;
  out->x = (int32_t)x0;
  out->y = (int32_t)y0;
  out->width = (uint32_t)(x1 - x0);
  out->height = (uint32_t)(y1 - y0);
  return 1;
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
  /* Etapa F4 minimize/maximize+cursors (2026-05-03): reset slot. */
  win->minimized = 0;
  win->maximized = 0;
  win->saved_frame.x = 0;
  win->saved_frame.y = 0;
  win->saved_frame.width = 0;
  win->saved_frame.height = 0;
  win->loading = 0;
  win->capture_mouse = 0;
  win->corner_radius = 0;
  win->bg_color = 0;
  win->border_color = 0;
  win->user_data = NULL;
  win->on_paint = NULL;
  win->on_close = NULL;
  win->on_resize = NULL;
  win->on_focus = NULL;
  win->on_blur = NULL;
  win->on_key = NULL;
  win->on_key_up = NULL;
  win->on_mouse = NULL;
  win->on_scroll = NULL;
  win->on_timer = NULL;
  win->on_hover = NULL;
  win->on_context_menu = NULL;
  win->on_cursor_hint = NULL;
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
  comp_stats.full_frames_presented = 0;
  comp_stats.partial_frames_presented = 0;
  comp_stats.dirty_rects_presented = 0;
  comp_stats.cursor_erases_partial = 0;
  comp_scene_dirty = 1;
  comp_dirty_mark_full_redraw();
  comp_full_presented = 0;
  comp_cursor_valid = 0;
  comp_cursor_x = 0;
  comp_cursor_y = 0;
  compositor_damage_smoke_global_reset();
}

void compositor_shutdown(void) {
  for (int i = 0; i < COMPOSITOR_MAX_WINDOWS; i++) {
    release_window(&comp_windows[i], 1);
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
  comp_stats.full_frames_presented = 0;
  comp_stats.partial_frames_presented = 0;
  comp_stats.dirty_rects_presented = 0;
  comp_stats.cursor_erases_partial = 0;
  comp_desktop_paint_cb = NULL;
  comp_scene_dirty = 1;
  comp_dirty_mark_full_redraw();
  comp_full_presented = 0;
  comp_cursor_valid = 0;
  comp_cursor_x = 0;
  comp_cursor_y = 0;
  compositor_damage_smoke_global_reset();
}

struct gui_window *compositor_create_window(const char *title, int32_t x,
                                            int32_t y, uint32_t w, uint32_t h) {
  for (int i = 0; i < COMPOSITOR_MAX_WINDOWS; i++) {
    if (comp_windows[i].id == 0) {
      struct gui_window *win = &comp_windows[i];
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
      /* Etapa F4 minimize/maximize+cursors (2026-05-03): novos
       * campos de estado de janela. */
      win->minimized = 0;
      win->maximized = 0;
      win->saved_frame = win->frame;
      win->loading = 0;
      /* Etapa UX W7-ish (2026-05-03): default rounded corners para
       * janelas decoradas (8 px). Overlays sao criados decorated=1
       * pelo compositor mas o caller pode setar decorated=0 + ajustar
       * corner_radius depois (p/ menu popup, context menu, etc.). */
      win->corner_radius = COMP_WINDOW_CORNER_RADIUS;
      win->bg_color = g_theme.window_bg;
      win->border_color = g_theme.window_border;
      win->user_data = NULL;
      win->on_paint = NULL;
      win->on_close = NULL;
      win->on_resize = NULL;
      win->on_focus = NULL;
      win->on_blur = NULL;
      win->on_key = NULL;
      win->on_key_up = NULL;
      win->on_mouse = NULL;
      win->on_scroll = NULL;
      win->on_timer = NULL;
      win->on_hover = NULL;
      win->on_context_menu = NULL;
      win->on_cursor_hint = NULL;
      /* Etapa 7 / Slice 7.5 (alpha.305): every window created through THIS
       * function (the only path in-kernel apps use) is NOT ring-3-owned by
       * default. gfx_backend_win_create (kernel/syscall_gfx_init.c) sets this
       * nonzero right after this call returns, for windows it creates on
       * behalf of a ring-3 process. Explicit reset matters because
       * comp_windows[] slots are reused (this loop finds the first free
       * `id == 0` slot), so a stale nonzero value from a previous ring-3
       * owner must not leak onto the next (possibly in-kernel-app) window
       * created in the same slot. */
      win->gfx_owner_pid = 0u;
      comp_stats.window_count++;
      return win;
    }
  }
  return NULL;
}

void compositor_destroy_window(uint32_t window_id) {
  struct gui_window *win = comp_find_window(window_id);
  if (!win) return;
  gui_event_discard_window(window_id);
  if (win->focused) gui_event_push_window_blur(window_id, 0);
  gui_event_push_window_close(window_id, 0);
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
  comp_request_scene_redraw();
}

void compositor_show_window(uint32_t window_id) {
  struct gui_window *win = comp_find_window(window_id);
  if (!win) return;
  /* Etapa F4 minimize/maximize (2026-05-03): show implicitamente
   * cancela o estado minimized, pois o usuario esta restaurando
   * a janela (e.g. clique no item do taskbar). */
  win->minimized = 0;
  if (win->visible) {
    /* Ja visivel; apenas garantia do flag minimized=0 acima. */
    return;
  }
  win->visible = 1;
  comp_stats.visible_count++;
  comp_request_scene_redraw();
}

void compositor_hide_window(uint32_t window_id) {
  struct gui_window *win = comp_find_window(window_id);
  if (!win || !win->visible) return;
  win->visible = 0;
  if (comp_stats.visible_count > 0) comp_stats.visible_count--;
  if (win->focused) {
    win->focused = 0;
    gui_event_push_window_blur(window_id, 0);
  }
  comp_request_scene_redraw();
}

void compositor_focus_window(uint32_t window_id) {
  struct gui_window *target = comp_find_window(window_id);
  uint32_t previous_focused = 0;
  uint32_t top_z = 0;
  if (!target) return;
  if (!target->visible || target->minimized) return;

  for (int i = 0; i < COMPOSITOR_MAX_WINDOWS; i++) {
    if (!comp_windows[i].id || window_is_overlay(&comp_windows[i])) continue;
    if (comp_windows[i].z_order > top_z) top_z = comp_windows[i].z_order;
  }

  if (!window_is_overlay(target) && target->z_order < top_z) {
    for (int i = 0; i < COMPOSITOR_MAX_WINDOWS; i++) {
      if (!comp_windows[i].id || &comp_windows[i] == target || window_is_overlay(&comp_windows[i]))
        continue;
      if (comp_windows[i].z_order > target->z_order) comp_windows[i].z_order--;
    }
    target->z_order = top_z;
  }

  for (int i = 0; i < COMPOSITOR_MAX_WINDOWS; i++) {
    if (comp_windows[i].id && comp_windows[i].focused) {
      previous_focused = comp_windows[i].id;
      break;
    }
  }

  for (int i = 0; i < COMPOSITOR_MAX_WINDOWS; i++) {
    comp_windows[i].focused = (comp_windows[i].id == window_id) ? 1 : 0;
  }

  if (previous_focused != window_id) {
    if (previous_focused) gui_event_push_window_blur(previous_focused, 0);
    gui_event_push_window_focus(window_id, 0);
  }
  comp_request_scene_redraw();
}

void compositor_move_window(uint32_t window_id, int32_t x, int32_t y) {
  struct gui_window *win = comp_find_window(window_id);
  if (!win) return;
  if (win->frame.x == x && win->frame.y == y) return;
  win->frame.x = x;
  win->frame.y = y;
  comp_request_scene_redraw();
}

void compositor_resize_window(uint32_t window_id, uint32_t w, uint32_t h) {
  struct gui_window *win = comp_find_window(window_id);
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
  gui_event_push_window_resize(window_id, (int32_t)w, (int32_t)h, 0);
  if (win->on_resize) win->on_resize(win, w, h);
  comp_request_scene_redraw();
}

void compositor_set_title(uint32_t window_id, const char *title) {
  struct gui_window *win = comp_find_window(window_id);
  if (!win || !title) return;
  comp_strcpy(win->title, title, 64);
  comp_request_scene_redraw();
}

void compositor_invalidate(uint32_t window_id) {
  struct gui_window *win = comp_find_window(window_id);
  struct gui_rect local;
  struct gui_rect dirty;
  if (!win) return;
  gui_event_push_paint(window_id, 0);
  local.x = 0;
  local.y = 0;
  local.width = win->surface.width;
  local.height = win->surface.height;
  if (!comp_window_rect_to_screen(win, &local, &dirty)) {
    comp_request_scene_redraw();
    return;
  }
  comp_dirty_append_rect(&dirty);
}

void compositor_invalidate_rect(uint32_t window_id, struct gui_rect *rect) {
  struct gui_window *win = comp_find_window(window_id);
  struct gui_rect dirty;
  if (!win) return;
  if (!rect) {
    compositor_invalidate(window_id);
    return;
  }
  if (rect->width == 0u || rect->height == 0u) return;
  if (!comp_window_rect_to_screen(win, rect, &dirty)) {
    return;
  }
  gui_event_push_paint(window_id, 0);
  comp_dirty_append_rect(&dirty);
}

void compositor_invalidate_desktop_rect(const struct gui_rect *rect) {
  struct gui_rect dirty;
  if (!rect) {
    compositor_invalidate_all();
    return;
  }
  if (!clip_rect_to_screen(rect, &dirty)) return;
  comp_dirty_append_rect(&dirty);
}

/* Etapa UX W7-ish (2026-05-03): redraw global. Util para o
 * desktop_icons (que pinta no surface do wallpaper, nao em uma
 * janela com id). */
void compositor_invalidate_all(void) {
  comp_request_scene_redraw();
}

/* === Hit-test + queries =============================================== */

struct gui_window *compositor_window_at(int32_t x, int32_t y) {
  struct gui_window *top = NULL;
  uint32_t top_z = 0;
  for (int i = 0; i < COMPOSITOR_MAX_WINDOWS; i++) {
    struct gui_window *w = &comp_windows[i];
    int32_t win_top = w->frame.y - (w->decorated ? (int32_t)comp_window_title_height() : 0);
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
    if (comp_windows[i].id && comp_windows[i].focused) return &comp_windows[i];
  }
  return NULL;
}

struct gui_window *compositor_get_window(uint32_t window_id) {
  return comp_find_window(window_id);
}

int compositor_window_exists(uint32_t window_id) {
  return comp_find_window(window_id) ? 1 : 0;
}

void compositor_stats_get(struct compositor_stats *out) {
  if (out) *out = comp_stats;
}

void compositor_screen_size(uint32_t *out_w, uint32_t *out_h) {
  if (out_w) *out_w = comp_width;
  if (out_h) *out_h = comp_height;
}

void compositor_set_wallpaper(uint32_t color) {
  if (comp_wallpaper == color) return;
  comp_wallpaper = color;
  comp_request_scene_redraw();
}

void compositor_set_desktop_callback(void (*callback)(struct gui_surface *)) {
  comp_desktop_paint_cb = callback;
  comp_request_scene_redraw();
}

int compositor_hit_close_button(struct gui_window *win, int32_t x, int32_t y) {
  if (!win || !win->decorated) return 0;
  uint32_t title_h = comp_window_title_height();
  int32_t btn_size = (int32_t)title_h - 6;
  int32_t btn_x = win->frame.x + (int32_t)win->frame.width - btn_size - 4;
  int32_t btn_y = win->frame.y - (int32_t)title_h + 3;
  return (x >= btn_x && x < btn_x + btn_size &&
          y >= btn_y && y < btn_y + btn_size);
}

/* Etapa F4 minimize/maximize (2026-05-03): hit-test do botao
 * Maximize/Restore (segundo a partir da direita). 4 px de gap entre
 * botoes. */
int compositor_hit_maximize_button(struct gui_window *win,
                                    int32_t x, int32_t y) {
  if (!win || !win->decorated) return 0;
  uint32_t title_h = comp_window_title_height();
  int32_t btn_size = (int32_t)title_h - 6;
  int32_t btn_x = win->frame.x + (int32_t)win->frame.width
                   - 2 * btn_size - 8;
  int32_t btn_y = win->frame.y - (int32_t)title_h + 3;
  return (x >= btn_x && x < btn_x + btn_size &&
          y >= btn_y && y < btn_y + btn_size);
}

/* Hit-test do botao Minimize (terceiro a partir da direita). */
int compositor_hit_minimize_button(struct gui_window *win,
                                    int32_t x, int32_t y) {
  if (!win || !win->decorated) return 0;
  uint32_t title_h = comp_window_title_height();
  int32_t btn_size = (int32_t)title_h - 6;
  int32_t btn_x = win->frame.x + (int32_t)win->frame.width
                   - 3 * btn_size - 12;
  int32_t btn_y = win->frame.y - (int32_t)title_h + 3;
  return (x >= btn_x && x < btn_x + btn_size &&
          y >= btn_y && y < btn_y + btn_size);
}

void compositor_minimize_window(uint32_t window_id) {
  struct gui_window *win = comp_find_window(window_id);
  if (!win) return;
  win->minimized = 1;
  if (win->visible) {
    win->visible = 0;
    if (comp_stats.visible_count > 0) comp_stats.visible_count--;
  }
  if (win->focused) {
    win->focused = 0;
    gui_event_push_window_blur(window_id, 0);
  }
  comp_request_scene_redraw();
}

/* Alterna entre maximize e restore. Quando maximizando, salva o
 * frame atual em `saved_frame`; quando restaurando, copia de volta.
 * O surface da janela e re-alocado pelo `compositor_resize_window`
 * (chamado para a nova largura/altura) -- isto significa que o
 * conteudo pintado anteriormente eh descartado e a janela precisa
 * receber on_paint novamente, o que ja e tratado pelo flag
 * comp_scene_dirty. */
void compositor_toggle_maximize_window(uint32_t window_id,
                                        uint32_t screen_w,
                                        uint32_t screen_h_avail) {
  struct gui_window *win = comp_find_window(window_id);
  if (!win) return;
  uint32_t title_h = comp_window_title_height();
  if (!win->maximized) {
    /* Save + maximize. Garante margem do title bar no topo. */
    win->saved_frame = win->frame;
    win->maximized = 1;
    win->frame.x = 0;
    win->frame.y = (int32_t)title_h;
    if (screen_h_avail <= title_h) screen_h_avail = title_h + 100u;
    compositor_resize_window(window_id, screen_w, screen_h_avail - title_h);
    win = comp_find_window(window_id); /* re-fetch (slot might have moved) */
    if (!win) return;
    win->frame.x = 0;
    win->frame.y = (int32_t)title_h;
  } else {
    win->maximized = 0;
    /* Restore. compositor_resize_window aloca novo surface da
     * largura/altura salvas; depois ajustamos a posicao. */
    compositor_resize_window(window_id, win->saved_frame.width,
                             win->saved_frame.height);
    win = comp_find_window(window_id);
    if (!win) return;
    win->frame.x = win->saved_frame.x;
    win->frame.y = win->saved_frame.y;
  }
  comp_request_scene_redraw();
}

void compositor_set_cursor(enum comp_cursor_kind kind) {
  if ((uint8_t)kind >= (uint8_t)COMP_CURSOR_KIND_COUNT) {
    kind = COMP_CURSOR_ARROW;
  }
  if ((uint8_t)kind == comp_cursor_kind_active) return;
  comp_cursor_kind_active = (uint8_t)kind;
}

enum comp_cursor_kind compositor_cursor_kind(void) {
  return (enum comp_cursor_kind)comp_cursor_kind_active;
}
