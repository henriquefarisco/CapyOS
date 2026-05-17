/*
 * src/gui/desktop/taskbar.c
 *
 * Main taskbar (the bottom bar): window list, clock, tray, paint
 * and click router. After the 2026-05-15 refactor this file holds:
 *
 *   - string and drawing utilities used pervasively across the
 *     taskbar group (`tb_*`)
 *   - window-list management (`taskbar_init`, `taskbar_add_window`,
 *     `taskbar_remove_window`, `taskbar_set_focused`)
 *   - clock + tray update entry points
 *   - bar paint (`taskbar_paint`) and click router
 *     (`taskbar_handle_click`)
 *
 * The start menu (data model + popups) lives in
 * `taskbar_menu.c`. Menu event handlers (toggle, click, hover,
 * scroll, keyboard) live in `taskbar_menu_input.c`. The split was
 * driven by the 900-line layout limit documented in
 * `docs/architecture/source-layout.md`.
 */
#include "gui/taskbar.h"
#include "gui/font.h"
#include "gui/compositor.h"

#include "gui/desktop/internal/taskbar_internal.h"

#include <stddef.h>
#include <stdint.h>

/* ── tb_* string + drawing utilities ─────────────────────────────────── */

void tb_strcpy(char *d, const char *s, size_t max) {
  size_t i = 0;
  if (!d || max == 0) return;
  if (!s) s = "";
  while (i < max - 1 && s[i]) { d[i] = s[i]; i++; }
  d[i] = '\0';
}

uint32_t tb_strlen(const char *s) {
  uint32_t n = 0;
  if (!s) return 0;
  while (s[n]) n++;
  return n;
}

int tb_streq(const char *a, const char *b) {
  uint32_t i = 0;
  if (!a || !b) return 0;
  while (a[i] && b[i]) { if (a[i] != b[i]) return 0; i++; }
  return a[i] == b[i];
}

void tb_append(char *dst, size_t dst_len, const char *src) {
  size_t p = 0, i = 0;
  if (!dst || dst_len == 0 || !src) return;
  while (p + 1 < dst_len && dst[p]) p++;
  while (p + 1 < dst_len && src[i]) dst[p++] = src[i++];
  dst[p] = '\0';
}

static char tb_lower(char c) {
  if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
  return c;
}

int tb_contains_ci(const char *text, const char *needle) {
  uint32_t i = 0;
  if (!needle || !needle[0]) return 1;
  if (!text) return 0;
  while (text[i]) {
    uint32_t j = 0;
    while (needle[j] && text[i + j] &&
           tb_lower(text[i + j]) == tb_lower(needle[j])) j++;
    if (!needle[j]) return 1;
    i++;
  }
  return 0;
}

static int tb_has_prefix(const char *text, const char *prefix) {
  uint32_t i = 0;
  if (!text || !prefix) return 0;
  while (prefix[i]) {
    if (text[i] != prefix[i]) return 0;
    i++;
  }
  return 1;
}

static int tb_tray_net_state(struct taskbar *tb) {
  if (!tb) return 0;
  if (tb_has_prefix(tb->tray_text, "net-on")) return 1;
  if (tb_has_prefix(tb->tray_text, "net-wait")) return 2;
  if (tb_has_prefix(tb->tray_text, "net-off")) return 3;
  return 0;
}

void tb_fill_rect(struct gui_surface *s, int32_t x, int32_t y,
                  uint32_t w, uint32_t h, uint32_t color) {
  for (uint32_t r = 0; r < h; r++) {
    int32_t py = y + (int32_t)r;
    if (py < 0 || (uint32_t)py >= s->height) continue;
    uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + py * s->pitch);
    for (uint32_t c = 0; c < w; c++) {
      int32_t px = x + (int32_t)c;
      if (px >= 0 && (uint32_t)px < s->width) line[px] = color;
    }
  }
}

void tb_fit_text(const struct font *f, const char *src, uint32_t max_width,
                 char *out, size_t out_len) {
  size_t len = 0;
  size_t max_chars = 0;
  if (!out || out_len == 0) return;
  out[0] = '\0';
  if (!f || !src || max_width == 0 || f->glyph_width == 0) return;
  max_chars = max_width / f->glyph_width;
  if (max_chars == 0) return;
  while (src[len]) len++;
  if (len <= max_chars && len < out_len) {
    tb_strcpy(out, src, out_len);
    return;
  }
  if (max_chars <= 3 || out_len <= 4) {
    size_t n = max_chars;
    if (n >= out_len) n = out_len - 1;
    for (size_t i = 0; i < n; i++) out[i] = '.';
    out[n] = '\0';
    return;
  }
  {
    size_t copy = max_chars - 3;
    if (copy > out_len - 4) copy = out_len - 4;
    for (size_t i = 0; i < copy; i++) out[i] = src[i];
    out[copy] = '.';
    out[copy + 1] = '.';
    out[copy + 2] = '.';
    out[copy + 3] = '\0';
  }
}

void tb_draw_fit(struct gui_surface *s, const struct font *f, int32_t x,
                 int32_t y, uint32_t max_width, const char *text,
                 uint32_t color) {
  char fitted[TASKBAR_ITEM_NAME_MAX];
  tb_fit_text(f, text, max_width, fitted, sizeof(fitted));
  if (fitted[0]) font_draw_string(s, f, x, y, fitted, color);
}

static int32_t tb_clock_x(struct taskbar *tb, const struct font *f,
                          uint32_t surface_w) {
  uint32_t cw = 0;
  if (!tb || !tb->show_clock || !f) return (int32_t)surface_w;
  cw = font_string_width(f, tb->clock_text);
  if (surface_w <= cw + 12u) return 0;
  return (int32_t)(surface_w - cw - 12u);
}

static uint32_t tb_tray_width(struct taskbar *tb, const struct font *f) {
  uint32_t tw = 0;
  if (!tb || !tb->tray_text[0]) return 0;
  if (tb_tray_net_state(tb) != 0) return 34u;
  if (!f) return 0;
  tw = font_string_width(f, tb->tray_text) + 12u;
  return (tw < 44u) ? 44u : tw;
}

static int32_t tb_tray_x(struct taskbar *tb, const struct font *f,
                         uint32_t surface_w) {
  uint32_t tw = tb_tray_width(tb, f);
  int32_t cx = tb_clock_x(tb, f, surface_w);
  if (tw == 0) return cx;
  if (cx <= (int32_t)(tw + 8u)) return 0;
  return cx - (int32_t)tw - 8;
}

static int32_t tb_items_right_edge(struct taskbar *tb, const struct font *f,
                                   uint32_t surface_w) {
  int32_t right = (int32_t)surface_w - 4;
  if (tb && tb->show_clock) right = tb_clock_x(tb, f, surface_w) - 8;
  if (tb && tb->tray_text[0]) right = tb_tray_x(tb, f, surface_w) - 8;
  if (right < 0) right = 0;
  return right;
}

static uint32_t tb_mix_color(uint32_t color, uint32_t target,
                             uint8_t amount) {
  uint32_t r = (color >> 16) & 0xFFu;
  uint32_t g = (color >> 8) & 0xFFu;
  uint32_t b = color & 0xFFu;
  uint32_t tr = (target >> 16) & 0xFFu;
  uint32_t tg = (target >> 8) & 0xFFu;
  uint32_t tb = target & 0xFFu;
  r = r + ((tr > r) ? ((tr - r) * amount) / 255u
                    : -((r - tr) * amount) / 255u);
  g = g + ((tg > g) ? ((tg - g) * amount) / 255u
                    : -((g - tg) * amount) / 255u);
  b = b + ((tb > b) ? ((tb - b) * amount) / 255u
                    : -((b - tb) * amount) / 255u);
  return (r << 16) | (g << 8) | b;
}

static void tb_draw_launcher_mark(struct gui_surface *s, int32_t x,
                                  int32_t y, uint32_t bg, uint32_t fg) {
  uint32_t shadow = tb_mix_color(bg, 0x00000000, 72u);
  if (!s) return;
  tb_fill_rect(s, x + 1, y + 2, 15u, 15u, shadow);
  tb_fill_rect(s, x, y, 7u, 7u, fg);
  tb_fill_rect(s, x + 9, y, 7u, 7u, fg);
  tb_fill_rect(s, x, y + 9, 7u, 7u, fg);
  tb_fill_rect(s, x + 9, y + 9, 7u, 7u, fg);
  tb_fill_rect(s, x + 2, y + 2, 3u, 3u, bg);
  tb_fill_rect(s, x + 11, y + 2, 3u, 3u, bg);
  tb_fill_rect(s, x + 2, y + 11, 3u, 3u, bg);
  tb_fill_rect(s, x + 11, y + 11, 3u, 3u, bg);
}

static void taskbar_window_paint(struct gui_window *win) {
  if (!win || !win->user_data) return;
  taskbar_paint((struct taskbar *)win->user_data);
}

void taskbar_init(struct taskbar *tb, uint32_t screen_w, uint32_t screen_h) {
  const struct gui_theme_palette *theme = compositor_theme();
  if (!tb) return;
  tb->position = TASKBAR_BOTTOM;
  tb->bg_color = theme->taskbar_bg;
  tb->fg_color = theme->taskbar_fg;
  tb->highlight_color = theme->taskbar_highlight;
  tb->item_count = 0;
  tb->menu_open = 0;
  tb->menu_entry_count = 0;
  tb->recent_count = 0;
  tb->menu_popup = NULL;
  tb->recent_popup = NULL;
  tb->hover_entry = -1;
  tb->selected_entry = -1;
  tb->recent_expanded = 0;
  tb->menu_filter[0] = '\0';
  tb->menu_scroll_offset = 0;
  tb->tray_text[0] = '\0';
  tb->show_clock = 1;
  tb_strcpy(tb->clock_text, "00:00:00", 16);

  int32_t y = (int32_t)(screen_h - TASKBAR_HEIGHT);
  tb->window = compositor_create_window("Taskbar", 0, y, screen_w, TASKBAR_HEIGHT);
  if (tb->window) {
    tb->window->decorated = 0;
    tb->window->movable = 0;
    tb->window->resizable = 0;
    tb->window->corner_radius = 0; /* taskbar fica retangular */
    tb->window->z_order = COMPOSITOR_MAX_WINDOWS + 4;
    tb->window->bg_color = theme->taskbar_bg;
    tb->window->user_data = tb;
    tb->window->on_paint = taskbar_window_paint;
    compositor_show_window(tb->window->id);
  }
}

void taskbar_remove_window(struct taskbar *tb, uint32_t window_id) {
  if (!tb) return;
  for (uint32_t i = 0; i < tb->item_count; i++) {
    if (tb->items[i].window_id == window_id) {
      for (uint32_t j = i; j < tb->item_count - 1; j++)
        tb->items[j] = tb->items[j + 1];
      tb->item_count--;
      if (tb->window) compositor_invalidate(tb->window->id);
      return;
    }
  }
}

static void taskbar_prune_stale_windows(struct taskbar *tb) {
  if (!tb) return;
  for (uint32_t i = 0; i < tb->item_count;) {
    if (!compositor_window_exists(tb->items[i].window_id)) {
      taskbar_remove_window(tb, tb->items[i].window_id);
      continue;
    }
    i++;
  }
}

void taskbar_add_window(struct taskbar *tb, uint32_t window_id, const char *name) {
  if (!tb) return;
  taskbar_prune_stale_windows(tb);
  if (tb->item_count >= TASKBAR_MAX_ITEMS) return;
  for (uint32_t i = 0; i < tb->item_count; i++) {
    if (tb->items[i].window_id == window_id) return;
  }
  {
    struct taskbar_item *item = &tb->items[tb->item_count++];
    item->window_id = window_id;
    tb_strcpy(item->name, name ? name : "Window", TASKBAR_ITEM_NAME_MAX);
    item->active = 1;
    item->focused = 0;
    if (tb->window) compositor_invalidate(tb->window->id);
  }
}

void taskbar_set_focused(struct taskbar *tb, uint32_t window_id) {
  int changed = 0;
  if (!tb) return;
  for (uint32_t i = 0; i < tb->item_count; i++) {
    int focused = (tb->items[i].window_id == window_id) ? 1 : 0;
    if (tb->items[i].focused != focused) {
      tb->items[i].focused = focused;
      changed = 1;
    }
  }
  if (changed && tb->window) compositor_invalidate(tb->window->id);
}

int taskbar_update_clock(struct taskbar *tb, const char *time_str) {
  if (!tb || !time_str) return 0;
  if (tb->clock_text[0] == time_str[0] &&
      tb->clock_text[1] == time_str[1] &&
      tb->clock_text[2] == time_str[2] &&
      tb->clock_text[3] == time_str[3] &&
      tb->clock_text[4] == time_str[4] &&
      tb->clock_text[5] == time_str[5] &&
      tb->clock_text[6] == time_str[6] &&
      tb->clock_text[7] == time_str[7] &&
      tb->clock_text[8] == time_str[8]) {
    return 0;
  }
  tb_strcpy(tb->clock_text, time_str, 16);
  if (tb->window) compositor_invalidate(tb->window->id);
  return 1;
}

static void tb_draw_net_tray(struct gui_surface *s, int32_t x, int32_t y,
                             uint32_t w, int state) {
  const struct gui_theme_palette *theme = compositor_theme();
  uint32_t bars = state == 1 ? theme->accent : theme->text_muted;
  uint32_t alert = 0x00CC3333;
  if (!s || w < 24u) return;
  tb_fill_rect(s, x, y, w, TASKBAR_HEIGHT - 8, theme->window_border);
  tb_fill_rect(s, x + 1, y + 1, w - 2u, TASKBAR_HEIGHT - 10, theme->taskbar_bg);
  tb_fill_rect(s, x + 8, y + 15, 3, 5, bars);
  tb_fill_rect(s, x + 13, y + 11, 3, 9, state == 3 ? theme->text_muted : bars);
  tb_fill_rect(s, x + 18, y + 7, 3, 13, state == 1 ? bars : theme->text_muted);
  if (state == 2) {
    tb_fill_rect(s, x + 24, y + 15, 2, 2, theme->text_muted);
    tb_fill_rect(s, x + 28, y + 15, 2, 2, theme->text_muted);
  }
  if (state == 3) {
    for (uint32_t i = 0; i < 7u; i++) {
      tb_fill_rect(s, x + 24 + (int32_t)i, y + 8 + (int32_t)i, 1, 1, alert);
      tb_fill_rect(s, x + 30 - (int32_t)i, y + 8 + (int32_t)i, 1, 1, alert);
    }
  }
}

void taskbar_paint(struct taskbar *tb) {
  const struct gui_theme_palette *theme = compositor_theme();
  const struct font *f = font_default();
  uint8_t scale = compositor_ui_scale();
  uint32_t menu_w = 82 + 20 * (scale - 1);
  uint32_t item_w = 120 + 28 * (scale - 1);
  struct gui_surface *s = NULL;
  int32_t x = 4;
  int32_t item_right = 0;
  if (!tb || !tb->window) return;
  taskbar_prune_stale_windows(tb);
  s = &tb->window->surface;
  tb->bg_color = theme->taskbar_bg;
  tb->fg_color = theme->taskbar_fg;
  tb->highlight_color = theme->taskbar_highlight;

  tb_fill_rect(s, 0, 0, s->width, s->height, tb->bg_color);

  /* Separator line at top */
  tb_fill_rect(s, 0, 0, s->width, 1, theme->accent_alt);
  tb_fill_rect(s, 0, 1, s->width, 1, theme->window_border);

  if (!f) return;

  item_right = tb_items_right_edge(tb, f, s->width);

  /* Menu button */
  uint32_t menu_btn_bg = tb->menu_open ? theme->accent_alt : theme->accent;
  tb_fill_rect(s, x, 4, menu_w, TASKBAR_HEIGHT - 8, menu_btn_bg);
  tb_fill_rect(s, x, 4, menu_w, 1, theme->accent_text);
  tb_fill_rect(s, x, TASKBAR_HEIGHT - 5, menu_w, 1,
               tb_mix_color(menu_btn_bg, 0x00000000, 88u));
  tb_fill_rect(s, x, 4, 1, TASKBAR_HEIGHT - 8,
               tb_mix_color(menu_btn_bg, 0x00000000, 72u));
  tb_draw_launcher_mark(s, x + 8, 8, menu_btn_bg, theme->accent_text);
  tb_draw_fit(s, f, x + 30, 8, (menu_w > 36u) ? menu_w - 36u : 0u,
              "Capy", theme->accent_text);
  x += (int32_t)menu_w + 8;

  /* Window list */
  for (uint32_t i = 0; i < tb->item_count; i++) {
    struct taskbar_item *item = &tb->items[i];
    uint32_t draw_w = item_w;
    uint32_t bg = item->focused ? tb->highlight_color : tb->bg_color;
    uint32_t edge = item->focused ? theme->accent : theme->window_border;
    if (x >= item_right) break;
    if (x + (int32_t)draw_w > item_right)
      draw_w = (uint32_t)(item_right - x);
    if (draw_w < 16u) break;
    tb_fill_rect(s, x, 4, draw_w, TASKBAR_HEIGHT - 8, bg);
    tb_fill_rect(s, x, 4, draw_w, 1, edge);
    tb_fill_rect(s, x, TASKBAR_HEIGHT - 5, draw_w, 1, edge);
    tb_fill_rect(s, x, 4, 1, TASKBAR_HEIGHT - 8, edge);
    tb_draw_fit(s, f, x + 6, 8, (draw_w > 12u) ? draw_w - 12u : 0u,
                item->name, tb->fg_color);
    x += (int32_t)item_w + 4;
  }

  if (tb->tray_text[0]) {
    uint32_t tray_w = tb_tray_width(tb, f);
    int32_t tx = tb_tray_x(tb, f, s->width);
    int state = tb_tray_net_state(tb);
    if (tray_w > 12u && tx < (int32_t)s->width) {
      if (tx < 0) tx = 0;
      if ((uint32_t)tx + tray_w > s->width)
        tray_w = s->width - (uint32_t)tx;
      if (tray_w > 12u) {
        if (state) {
          tb_draw_net_tray(s, tx, 4, tray_w, state);
        } else {
          tb_fill_rect(s, tx, 4, tray_w, TASKBAR_HEIGHT - 8,
                       theme->window_border);
          tb_fill_rect(s, tx + 1, 5, tray_w - 2, TASKBAR_HEIGHT - 10,
                       tb->bg_color);
          tb_draw_fit(s, f, tx + 6, 8, tray_w - 12u, tb->tray_text,
                      tb->fg_color);
        }
      }
    }
  }

  /* Clock on right side */
  if (tb->show_clock) {
    int32_t cx = tb_clock_x(tb, f, s->width);
    uint32_t cw = font_string_width(f, tb->clock_text);
    int32_t pill_x = (cx > 6) ? cx - 6 : 0;
    uint32_t pill_w = cw + 12u;
    if (pill_x < (int32_t)s->width) {
      if ((uint32_t)pill_x + pill_w > s->width)
        pill_w = s->width - (uint32_t)pill_x;
      tb_fill_rect(s, pill_x, 4, pill_w, TASKBAR_HEIGHT - 8,
                   theme->accent_alt);
    }
    tb_draw_fit(s, f, cx, 8, (s->width > (uint32_t)cx)
                ? s->width - (uint32_t)cx : 0u,
                tb->clock_text, tb->fg_color);
  }
}

void taskbar_handle_click(struct taskbar *tb, int32_t x, int32_t y) {
  const struct font *f = font_default();
  uint8_t scale = compositor_ui_scale();
  int32_t menu_w = 82 + 20 * (scale - 1);
  int32_t item_x = menu_w + 12;
  int32_t item_w = 120 + 28 * (scale - 1);
  int32_t item_right = 0;
  int have_item_right = 0;
  if (!tb) return;
  (void)y;
  if (tb->window) {
    item_right = tb_items_right_edge(tb, f, tb->window->surface.width);
    have_item_right = 1;
  }

  if (x >= 4 && x < 4 + menu_w) {
    taskbar_toggle_menu(tb);
    return;
  }

  /* Click outside the menu button while menu is open -> close it */
  if (tb->menu_open) {
    taskbar_toggle_menu(tb);
  }

  for (uint32_t i = 0; i < tb->item_count; i++) {
    int32_t hit_w = item_w;
    if (have_item_right) {
      if (item_x >= item_right) break;
      if (item_x + hit_w > item_right) hit_w = item_right - item_x;
      if (hit_w < 16) break;
    }
    if (x >= item_x && x < item_x + hit_w) {
      if (!compositor_window_exists(tb->items[i].window_id)) {
        taskbar_remove_window(tb, tb->items[i].window_id);
        return;
      }
      compositor_focus_window(tb->items[i].window_id);
      compositor_show_window(tb->items[i].window_id);
      taskbar_set_focused(tb, tb->items[i].window_id);
      return;
    }
    item_x += item_w + 4;
  }
}

int taskbar_update_tray(struct taskbar *tb, const char *text) {
  char next[TASKBAR_TRAY_TEXT_MAX];
  if (!tb) return 0;
  tb_strcpy(next, text ? text : "", sizeof(next));
  if (tb_streq(tb->tray_text, next)) return 0;
  tb_strcpy(tb->tray_text, next, sizeof(tb->tray_text));
  if (tb->window) compositor_invalidate(tb->window->id);
  return 1;
}
