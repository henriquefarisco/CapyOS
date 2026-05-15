#include "gui/taskbar.h"
#include "gui/font.h"
#include "gui/compositor.h"
#include "lang/app_language.h"
#include "drivers/input/keyboard_layout.h"
#include <stddef.h>

#define TASKBAR_MENU_ROW_RECENT_TOGGLE 900
#define TASKBAR_MENU_ROW_RECENT_BASE 1000

static void tb_strcpy(char *d, const char *s, size_t max) {
  size_t i = 0;
  if (!d || max == 0) return;
  if (!s) s = "";
  while (i < max - 1 && s[i]) { d[i] = s[i]; i++; }
  d[i] = '\0';
}

static uint32_t tb_strlen(const char *s) {
  uint32_t n = 0;
  if (!s) return 0;
  while (s[n]) n++;
  return n;
}

static int tb_streq(const char *a, const char *b) {
  uint32_t i = 0;
  if (!a || !b) return 0;
  while (a[i] && b[i]) { if (a[i] != b[i]) return 0; i++; }
  return a[i] == b[i];
}

static void tb_append(char *dst, size_t dst_len, const char *src) {
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

static int tb_contains_ci(const char *text, const char *needle) {
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

static void tb_fill_rect(struct gui_surface *s, int32_t x, int32_t y,
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

static void tb_fit_text(const struct font *f, const char *src,
                        uint32_t max_width, char *out, size_t out_len) {
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

static void tb_draw_fit(struct gui_surface *s, const struct font *f,
                        int32_t x, int32_t y, uint32_t max_width,
                        const char *text, uint32_t color) {
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
  tb_fill_rect(s, x + 6, 10, 10, 10, theme->accent_text);
  tb_fill_rect(s, x + 8, 12, 6, 6, menu_btn_bg);
  tb_draw_fit(s, f, x + 22, 8, (menu_w > 28u) ? menu_w - 28u : 0u,
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

static uint32_t taskbar_menu_entry_group(struct taskbar *tb, uint32_t index) {
  uint32_t group = 0;
  if (!tb || index >= tb->menu_entry_count) return 0;
  for (uint32_t i = 0; i < index && i < tb->menu_entry_count; i++) {
    if (tb->menu_entries[i].is_separator) group++;
  }
  return group;
}

static const char *taskbar_menu_group_label(struct taskbar *tb, uint32_t index) {
  uint32_t group = taskbar_menu_entry_group(tb, index);
  if (!tb || index >= tb->menu_entry_count) return "Apps";
  if (tb->menu_entries[index].pinned) return "Pinned";
  if (group == 0) return "Apps";
  if (group == 1) return "System";
  return "Session";
}

static int taskbar_menu_entry_is_session(struct taskbar *tb, uint32_t index) {
  if (!tb || index >= tb->menu_entry_count) return 0;
  if (tb->menu_entries[index].is_separator) return 0;
  return taskbar_menu_entry_group(tb, index) >= 2u;
}

static uint32_t taskbar_session_count(struct taskbar *tb) {
  uint32_t count = 0;
  if (!tb) return 0;
  for (uint32_t i = 0; i < tb->menu_entry_count; i++) {
    if (taskbar_menu_entry_is_session(tb, i)) count++;
  }
  return count;
}

static int taskbar_session_entry_by_slot(struct taskbar *tb, uint32_t slot) {
  uint32_t seen = 0;
  if (!tb) return -1;
  for (uint32_t i = 0; i < tb->menu_entry_count; i++) {
    if (!taskbar_menu_entry_is_session(tb, i)) continue;
    if (seen == slot) return (int)i;
    seen++;
  }
  return -1;
}

static int taskbar_menu_entry_matches(struct taskbar *tb, uint32_t index) {
  const char *group = NULL;
  if (!tb || index >= tb->menu_entry_count) return 0;
  if (tb->menu_entries[index].is_separator) return 0;
  if (taskbar_menu_entry_is_session(tb, index)) return 0;
  if (!tb->menu_filter[0]) return 1;
  group = taskbar_menu_group_label(tb, index);
  return tb_contains_ci(tb->menu_entries[index].label, tb->menu_filter) ||
         tb_contains_ci(group, tb->menu_filter);
}

static int taskbar_recent_row(uint32_t index) {
  return TASKBAR_MENU_ROW_RECENT_BASE + (int)index;
}

static int taskbar_row_is_recent(int row) {
  return row >= TASKBAR_MENU_ROW_RECENT_BASE;
}

static uint32_t taskbar_row_recent_index(int row) {
  return (uint32_t)(row - TASKBAR_MENU_ROW_RECENT_BASE);
}

static int taskbar_recent_entry_matches(struct taskbar *tb, uint32_t index) {
  if (!tb || index >= tb->recent_count) return 0;
  if (!tb->menu_filter[0]) return 1;
  return tb_contains_ci(tb->recent_entries[index].label, tb->menu_filter) ||
         tb_contains_ci("Recent", tb->menu_filter);
}

static int taskbar_recent_matches(struct taskbar *tb, uint32_t index) {
  if (!tb || !tb->recent_expanded) return 0;
  return taskbar_recent_entry_matches(tb, index);
}

static uint32_t taskbar_recent_available_count(struct taskbar *tb) {
  uint32_t count = 0;
  if (!tb) return 0;
  for (uint32_t i = 0; i < tb->recent_count; i++) {
    if (taskbar_recent_entry_matches(tb, i)) count++;
  }
  return count;
}

static void taskbar_note_recent(struct taskbar *tb,
                                struct taskbar_menu_entry *entry) {
  struct taskbar_recent_entry next;
  uint32_t found = TASKBAR_MENU_RECENT_MAX;
  uint32_t move_from = 0;
  if (!tb || !entry || !entry->recentable || !entry->label[0]) return;
  tb_strcpy(next.label, entry->label, sizeof(next.label));
  next.action = entry->action;
  next.user_data = entry->user_data;
  for (uint32_t i = 0; i < tb->recent_count; i++) {
    if (tb_streq(tb->recent_entries[i].label, next.label)) {
      found = i;
      break;
    }
  }
  if (found < TASKBAR_MENU_RECENT_MAX) {
    move_from = found;
  } else if (tb->recent_count < TASKBAR_MENU_RECENT_MAX) {
    move_from = tb->recent_count++;
  } else {
    move_from = TASKBAR_MENU_RECENT_MAX - 1u;
  }
  for (uint32_t i = move_from; i > 0; i--) {
    tb->recent_entries[i] = tb->recent_entries[i - 1u];
  }
  tb->recent_entries[0] = next;
}

static uint32_t taskbar_match_count(struct taskbar *tb) {
  uint32_t count = 0;
  if (!tb) return 0;
  if (taskbar_recent_available_count(tb) > 0u) count++;
  for (uint32_t i = 0; i < tb->recent_count; i++) {
    if (taskbar_recent_matches(tb, i)) count++;
  }
  for (uint32_t i = 0; i < tb->menu_entry_count; i++) {
    if (taskbar_menu_entry_matches(tb, i)) count++;
  }
  return count;
}

static int taskbar_match_at_ordinal(struct taskbar *tb, uint32_t wanted) {
  uint32_t pos = 0;
  if (!tb) return -1;
  if (taskbar_recent_available_count(tb) > 0u) {
    if (pos == wanted) return TASKBAR_MENU_ROW_RECENT_TOGGLE;
    pos++;
  }
  for (uint32_t i = 0; i < tb->recent_count; i++) {
    if (!taskbar_recent_matches(tb, i)) continue;
    if (pos == wanted) return taskbar_recent_row(i);
    pos++;
  }
  for (uint32_t i = 0; i < tb->menu_entry_count; i++) {
    if (!taskbar_menu_entry_matches(tb, i)) continue;
    if (pos == wanted) return (int)i;
    pos++;
  }
  return -1;
}

static int taskbar_ordinal_of(struct taskbar *tb, int row) {
  uint32_t pos = 0;
  if (!tb) return -1;
  if (taskbar_recent_available_count(tb) > 0u) {
    if (row == TASKBAR_MENU_ROW_RECENT_TOGGLE) return (int)pos;
    pos++;
  }
  for (uint32_t i = 0; i < tb->recent_count; i++) {
    if (!taskbar_recent_matches(tb, i)) continue;
    if (row == taskbar_recent_row(i)) return (int)pos;
    pos++;
  }
  for (uint32_t i = 0; i < tb->menu_entry_count; i++) {
    if (!taskbar_menu_entry_matches(tb, i)) continue;
    if (row == (int)i) return (int)pos;
    pos++;
  }
  return -1;
}

static int taskbar_first_match(struct taskbar *tb) {
  if (taskbar_match_count(tb) == 0u) return -1;
  return taskbar_match_at_ordinal(tb, 0u);
}

static int taskbar_last_match(struct taskbar *tb) {
  uint32_t count = taskbar_match_count(tb);
  if (count == 0u) return -1;
  return taskbar_match_at_ordinal(tb, count - 1u);
}

static int taskbar_next_match(struct taskbar *tb, int current) {
  uint32_t count = taskbar_match_count(tb);
  int ord = 0;
  if (!tb || count == 0u) return -1;
  ord = taskbar_ordinal_of(tb, current);
  if (ord < 0) return taskbar_first_match(tb);
  return taskbar_match_at_ordinal(tb, ((uint32_t)ord + 1u) % count);
}

static int taskbar_prev_match(struct taskbar *tb, int current) {
  uint32_t count = taskbar_match_count(tb);
  int ord = 0;
  if (!tb || count == 0u) return -1;
  ord = taskbar_ordinal_of(tb, current);
  if (ord < 0) return taskbar_last_match(tb);
  return taskbar_match_at_ordinal(tb, ord == 0 ? count - 1u : (uint32_t)ord - 1u);
}

static uint32_t menu_total_height(struct taskbar *tb) {
  uint32_t h = TASKBAR_MENU_HEADER_HEIGHT + 4u;
  uint32_t recent_available = taskbar_recent_available_count(tb);
  const char *last = "";
  int visible = 0;
  if (!tb) return h + TASKBAR_MENU_EMPTY_HEIGHT;
  if (recent_available > 0u) {
    h += TASKBAR_MENU_ENTRY_HEIGHT;
    visible = 1;
  }
  for (uint32_t i = 0; i < tb->menu_entry_count; i++) {
    const char *group = NULL;
    if (!taskbar_menu_entry_matches(tb, i)) continue;
    group = taskbar_menu_group_label(tb, i);
    if (!tb_streq(last, group)) {
      h += TASKBAR_MENU_CATEGORY_HEIGHT;
      last = group;
    }
    h += TASKBAR_MENU_ENTRY_HEIGHT;
    visible = 1;
  }
  if (!visible) h += TASKBAR_MENU_EMPTY_HEIGHT;
  if (taskbar_session_count(tb) > 0u) h += TASKBAR_MENU_FOOTER_HEIGHT;
  return h;
}

static uint32_t taskbar_menu_footer_height(struct taskbar *tb) {
  return taskbar_session_count(tb) > 0u ? TASKBAR_MENU_FOOTER_HEIGHT : 0u;
}

static uint32_t taskbar_menu_max_height(struct taskbar *tb) {
  uint32_t screen_w = 0;
  uint32_t screen_h = 0;
  uint32_t footer_h = taskbar_menu_footer_height(tb);
  uint32_t min_h = TASKBAR_MENU_HEADER_HEIGHT + TASKBAR_MENU_ENTRY_HEIGHT + footer_h;
  uint32_t max_h = 0;
  (void)screen_w;
  compositor_screen_size(&screen_w, &screen_h);
  max_h = screen_h > 0u ? screen_h / 2u : min_h;
  if (screen_h > TASKBAR_HEIGHT && max_h > screen_h - TASKBAR_HEIGHT)
    max_h = screen_h - TASKBAR_HEIGHT;
  if (max_h < min_h) max_h = min_h;
  return max_h;
}

static uint32_t taskbar_menu_visible_height(struct taskbar *tb) {
  uint32_t total = menu_total_height(tb);
  uint32_t max_h = taskbar_menu_max_height(tb);
  return total > max_h ? max_h : total;
}

static uint32_t taskbar_recent_popup_height(struct taskbar *tb) {
  uint32_t count = taskbar_recent_available_count(tb);
  return count ? 4u + count * TASKBAR_MENU_ENTRY_HEIGHT : 0u;
}

static uint32_t taskbar_menu_current_height(struct taskbar *tb) {
  if (tb && tb->menu_popup && tb->menu_popup->frame.height)
    return tb->menu_popup->frame.height;
  return taskbar_menu_visible_height(tb);
}

static uint32_t taskbar_menu_scroll_max(struct taskbar *tb) {
  uint32_t total = menu_total_height(tb);
  uint32_t visible = taskbar_menu_visible_height(tb);
  uint32_t footer_h = taskbar_menu_footer_height(tb);
  uint32_t natural = 0;
  uint32_t viewport = 0;
  if (total <= visible) return 0u;
  if (total <= TASKBAR_MENU_HEADER_HEIGHT + footer_h) return 0u;
  natural = total - TASKBAR_MENU_HEADER_HEIGHT - footer_h;
  if (visible <= TASKBAR_MENU_HEADER_HEIGHT + footer_h) return natural;
  viewport = visible - TASKBAR_MENU_HEADER_HEIGHT - footer_h;
  return natural > viewport ? natural - viewport : 0u;
}

static void taskbar_clamp_menu_scroll(struct taskbar *tb) {
  uint32_t max_scroll = 0;
  if (!tb) return;
  max_scroll = taskbar_menu_scroll_max(tb);
  if (tb->menu_scroll_offset < 0) tb->menu_scroll_offset = 0;
  if ((uint32_t)tb->menu_scroll_offset > max_scroll)
    tb->menu_scroll_offset = (int)max_scroll;
}

static void taskbar_menu_popup_position(struct taskbar *tb, uint32_t popup_w,
                                        uint32_t popup_h, int32_t *out_x,
                                        int32_t *out_y) {
  uint32_t screen_w = 0;
  uint32_t screen_h = 0;
  int32_t x = 0;
  int32_t y = 0;
  compositor_screen_size(&screen_w, &screen_h);
  if (tb && tb->window) {
    x = tb->window->frame.x;
    y = tb->window->frame.y - (int32_t)popup_h;
  }
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (screen_w > 0u) {
    int32_t max_x = (screen_w > popup_w) ? (int32_t)(screen_w - popup_w) : 0;
    if (x > max_x) x = max_x;
  }
  if (screen_h > 0u) {
    int32_t max_y = (screen_h > popup_h) ? (int32_t)(screen_h - popup_h) : 0;
    if (y > max_y) y = max_y;
  }
  if (out_x) *out_x = x;
  if (out_y) *out_y = y;
}

static int taskbar_menu_entry_at(struct taskbar *tb, int32_t local_x,
                                     int32_t local_y) {
  int32_t ey = 0;
  uint32_t recent_available = 0;
  const char *last = "";
  uint32_t session_count = 0;
  uint32_t footer_h = 0;
  uint32_t visible_h = 0;
  int32_t list_top = (int32_t)TASKBAR_MENU_HEADER_HEIGHT;
  int32_t list_bottom = 0;
  if (!tb || local_y < list_top) return -1;
  recent_available = taskbar_recent_available_count(tb);
  session_count = taskbar_session_count(tb);
  footer_h = taskbar_menu_footer_height(tb);
  visible_h = taskbar_menu_current_height(tb);
  list_bottom = (int32_t)(visible_h > footer_h ? visible_h - footer_h : visible_h);
  if (footer_h > 0u && local_y >= list_bottom) {
    uint32_t margin = 10u;
    uint32_t gap = 6u;
    uint32_t usable = TASKBAR_MENU_WIDTH > margin * 2u
                          ? TASKBAR_MENU_WIDTH - margin * 2u : 0u;
    uint32_t button_w = 0u;
    if (session_count > 1u && usable > gap * (session_count - 1u))
      usable -= gap * (session_count - 1u);
    button_w = session_count ? usable / session_count : 0u;
    if (local_y < list_bottom + 8 || local_y >= list_bottom + 32)
      return -1;
    for (uint32_t slot = 0; slot < session_count; slot++) {
      int32_t bx = (int32_t)(margin + slot * (button_w + gap));
      if (local_x >= bx && local_x < bx + (int32_t)button_w)
        return taskbar_session_entry_by_slot(tb, slot);
    }
    return -1;
  }
  if (local_y >= list_bottom) return -1;
  ey = (int32_t)TASKBAR_MENU_HEADER_HEIGHT - tb->menu_scroll_offset;
  if (recent_available > 0u) {
    if (ey >= list_top && ey + (int32_t)TASKBAR_MENU_ENTRY_HEIGHT <= list_bottom &&
        local_y >= ey && local_y < ey + (int32_t)TASKBAR_MENU_ENTRY_HEIGHT)
      return TASKBAR_MENU_ROW_RECENT_TOGGLE;
    ey += TASKBAR_MENU_ENTRY_HEIGHT;
  }
  for (uint32_t i = 0; i < tb->menu_entry_count; i++) {
    const char *group = NULL;
    if (!taskbar_menu_entry_matches(tb, i)) continue;
    group = taskbar_menu_group_label(tb, i);
    if (!tb_streq(last, group)) {
      ey += TASKBAR_MENU_CATEGORY_HEIGHT;
      last = group;
    }
    if (ey >= list_top && ey + (int32_t)TASKBAR_MENU_ENTRY_HEIGHT <= list_bottom &&
        local_y >= ey && local_y < ey + (int32_t)TASKBAR_MENU_ENTRY_HEIGHT) {
      return (int)i;
    }
    ey += TASKBAR_MENU_ENTRY_HEIGHT;
  }
  return -1;
}

static void taskbar_refresh_menu_popup(struct taskbar *tb) {
  uint32_t h = 0;
  int32_t x = 0, y = 0;
  if (!tb || !tb->menu_open || !tb->menu_popup) return;
  taskbar_clamp_menu_scroll(tb);
  h = taskbar_menu_visible_height(tb);
  if (tb->menu_popup->frame.width != TASKBAR_MENU_WIDTH ||
      tb->menu_popup->frame.height != h) {
    int old_resizable = tb->menu_popup->resizable;
    tb->menu_popup->resizable = 1;
    compositor_resize_window(tb->menu_popup->id, TASKBAR_MENU_WIDTH, h);
    tb->menu_popup->resizable = old_resizable;
  }
  taskbar_menu_popup_position(tb, TASKBAR_MENU_WIDTH, h, &x, &y);
  compositor_move_window(tb->menu_popup->id, x, y);
  compositor_invalidate(tb->menu_popup->id);
}

/* Etapa UX W7-ish (2026-05-03): mistura "para cima" cada canal RGB
 * por uma fracao 0..255 (255 = branco puro). Mantem hue, aumenta
 * brilho. Util para o efeito "fade" de hover sobre uma cor de bg
 * arbitraria sem precisar de palette extra. */
static uint32_t tb_lighten(uint32_t color, uint8_t amount) {
  uint32_t r = (color >> 16) & 0xFFu;
  uint32_t g = (color >> 8) & 0xFFu;
  uint32_t b = color & 0xFFu;
  uint32_t a = color & 0xFF000000u;
  r = r + ((255u - r) * amount) / 255u;
  g = g + ((255u - g) * amount) / 255u;
  b = b + ((255u - b) * amount) / 255u;
  if (r > 255u) r = 255u;
  if (g > 255u) g = 255u;
  if (b > 255u) b = 255u;
  return a | (r << 16) | (g << 8) | b;
}

static void taskbar_draw_menu_row(struct taskbar *tb, struct gui_surface *s,
                                  const struct font *f, int row,
                                  int32_t ey, const char *label) {
  const struct gui_theme_palette *theme = compositor_theme();
  int active = 0;
  if (!tb || !s || !f || !label) return;
  active = (row == tb->hover_entry) ||
           (tb->hover_entry < 0 && row == tb->selected_entry);
  if (active) {
    uint32_t hover_bg = tb_lighten(theme->window_bg, 40u);
    if (s->width > 4u) {
      tb_fill_rect(s, 4, ey, s->width - 4u,
                   TASKBAR_MENU_ENTRY_HEIGHT, hover_bg);
    }
    tb_fill_rect(s, 4, ey, 4, TASKBAR_MENU_ENTRY_HEIGHT, theme->accent);
  }
  tb_fill_rect(s, 16, ey + 8, 10, 10,
               active ? theme->accent : theme->window_border);
  tb_draw_fit(s, f, 34, ey + 6, (s->width > 44u) ? s->width - 44u : 0u,
              label, active ? theme->accent : theme->text);
}

static int taskbar_recent_popup_entry_at(struct taskbar *tb, int32_t local_y) {
  int32_t ey = 2;
  if (!tb) return -1;
  for (uint32_t i = 0; i < tb->recent_count; i++) {
    if (!taskbar_recent_entry_matches(tb, i)) continue;
    if (local_y >= ey && local_y < ey + (int32_t)TASKBAR_MENU_ENTRY_HEIGHT)
      return taskbar_recent_row(i);
    ey += TASKBAR_MENU_ENTRY_HEIGHT;
  }
  return -1;
}

static void recent_popup_paint(struct gui_window *win) {
  if (!win || !win->user_data) return;
  struct taskbar *tb = (struct taskbar *)win->user_data;
  const struct gui_theme_palette *theme = compositor_theme();
  const struct font *f = font_default();
  struct gui_surface *s = &win->surface;
  int32_t ey = 2;
  if (!f) return;
  tb_fill_rect(s, 0, 0, s->width, s->height, theme->window_bg);
  for (uint32_t i = 0; i < tb->recent_count; i++) {
    if (!taskbar_recent_entry_matches(tb, i)) continue;
    taskbar_draw_menu_row(tb, s, f, taskbar_recent_row(i), ey,
                          tb->recent_entries[i].label);
    ey += TASKBAR_MENU_ENTRY_HEIGHT;
  }
}

static void taskbar_recent_popup_position(struct taskbar *tb, uint32_t popup_w,
                                          uint32_t popup_h, int32_t *out_x,
                                          int32_t *out_y) {
  uint32_t screen_w = 0;
  uint32_t screen_h = 0;
  int32_t x = 0;
  int32_t y = 0;
  compositor_screen_size(&screen_w, &screen_h);
  if (tb && tb->menu_popup) {
    x = tb->menu_popup->frame.x + (int32_t)tb->menu_popup->frame.width;
    y = tb->menu_popup->frame.y +
        (int32_t)TASKBAR_MENU_HEADER_HEIGHT - tb->menu_scroll_offset;
  }
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (screen_w > 0u) {
    int32_t max_x = (screen_w > popup_w) ? (int32_t)(screen_w - popup_w) : 0;
    if (x > max_x) x = max_x;
  }
  if (screen_h > 0u) {
    int32_t max_y = (screen_h > popup_h) ? (int32_t)(screen_h - popup_h) : 0;
    if (y > max_y) y = max_y;
  }
  if (out_x) *out_x = x;
  if (out_y) *out_y = y;
}

static void taskbar_hide_recent_popup(struct taskbar *tb) {
  if (tb && tb->recent_popup) compositor_hide_window(tb->recent_popup->id);
}

static void taskbar_refresh_recent_popup(struct taskbar *tb) {
  uint32_t h = 0;
  int32_t x = 0;
  int32_t y = 0;
  int want = 0;
  if (!tb || !tb->menu_open || !tb->menu_popup) return;
  want = (tb->hover_entry == TASKBAR_MENU_ROW_RECENT_TOGGLE ||
          taskbar_row_is_recent(tb->hover_entry)) &&
         taskbar_recent_available_count(tb) > 0u;
  if (!want) {
    taskbar_hide_recent_popup(tb);
    return;
  }
  h = taskbar_recent_popup_height(tb);
  if (h == 0u) {
    taskbar_hide_recent_popup(tb);
    return;
  }
  taskbar_recent_popup_position(tb, TASKBAR_MENU_WIDTH, h, &x, &y);
  if (!tb->recent_popup) {
    tb->recent_popup = compositor_create_window("Recent", x, y,
                                                TASKBAR_MENU_WIDTH, h);
    if (tb->recent_popup) {
      tb->recent_popup->decorated = 0;
      tb->recent_popup->movable = 0;
      tb->recent_popup->resizable = 0;
      tb->recent_popup->corner_radius = 6;
      tb->recent_popup->border_color = compositor_theme()->window_border;
      tb->recent_popup->z_order = COMPOSITOR_MAX_WINDOWS + 6;
      tb->recent_popup->bg_color = compositor_theme()->window_bg;
      tb->recent_popup->user_data = tb;
      tb->recent_popup->on_paint = recent_popup_paint;
    }
  }
  if (!tb->recent_popup) return;
  if (tb->recent_popup->frame.width != TASKBAR_MENU_WIDTH ||
      tb->recent_popup->frame.height != h) {
    int old_resizable = tb->recent_popup->resizable;
    tb->recent_popup->resizable = 1;
    compositor_resize_window(tb->recent_popup->id, TASKBAR_MENU_WIDTH, h);
    tb->recent_popup->resizable = old_resizable;
  }
  compositor_move_window(tb->recent_popup->id, x, y);
  compositor_show_window(tb->recent_popup->id);
  compositor_invalidate(tb->recent_popup->id);
}

static uint32_t taskbar_session_button_color(uint32_t slot) {
  if (slot == 0u) return 0x002F80EDu;
  if (slot == 1u) return 0x00F2994Au;
  return 0x00EB5757u;
}

static void taskbar_draw_session_icon(struct gui_surface *s, int32_t bx,
                                      int32_t by, uint32_t button_w,
                                      uint32_t slot, uint32_t fg) {
  int32_t cx = bx + (int32_t)(button_w / 2u);
  int32_t y = by + 8;
  if (!s || button_w < 18u) return;
  if (slot == 0u) {
    tb_fill_rect(s, cx - 8, y + 4, 7u, 14u, fg);
    tb_fill_rect(s, cx - 6, y + 6, 3u, 10u, 0x00111B18u);
    tb_fill_rect(s, cx - 1, y + 10, 10u, 2u, fg);
    tb_fill_rect(s, cx + 6, y + 7, 2u, 2u, fg);
    tb_fill_rect(s, cx + 8, y + 9, 2u, 2u, fg);
    tb_fill_rect(s, cx + 6, y + 12, 2u, 2u, fg);
  } else if (slot == 1u) {
    tb_fill_rect(s, cx - 8, y + 5, 13u, 2u, fg);
    tb_fill_rect(s, cx + 3, y + 3, 2u, 5u, fg);
    tb_fill_rect(s, cx + 5, y + 5, 3u, 2u, fg);
    tb_fill_rect(s, cx - 5, y + 14, 13u, 2u, fg);
    tb_fill_rect(s, cx - 5, y + 12, 2u, 5u, fg);
    tb_fill_rect(s, cx - 8, y + 14, 3u, 2u, fg);
    tb_fill_rect(s, cx + 6, y + 7, 2u, 7u, fg);
    tb_fill_rect(s, cx - 8, y + 7, 2u, 7u, fg);
  } else {
    tb_fill_rect(s, cx - 1, y + 3, 3u, 9u, fg);
    tb_fill_rect(s, cx - 8, y + 9, 3u, 8u, fg);
    tb_fill_rect(s, cx + 6, y + 9, 3u, 8u, fg);
    tb_fill_rect(s, cx - 6, y + 16, 13u, 3u, fg);
    tb_fill_rect(s, cx - 5, y + 7, 3u, 3u, fg);
    tb_fill_rect(s, cx + 3, y + 7, 3u, 3u, fg);
  }
}

static void taskbar_draw_session_footer(struct taskbar *tb,
                                        struct gui_surface *s,
                                        const struct font *f) {
  const struct gui_theme_palette *theme = compositor_theme();
  uint32_t count = taskbar_session_count(tb);
  uint32_t gap = 6u;
  uint32_t margin = 10u;
  uint32_t usable = 0u;
  uint32_t button_w = 0u;
  int32_t y = 0;
  (void)f;
  if (!tb || !s || count == 0u || s->height < TASKBAR_MENU_FOOTER_HEIGHT)
    return;
  y = (int32_t)(s->height - TASKBAR_MENU_FOOTER_HEIGHT);
  tb_fill_rect(s, 8, y, s->width > 16u ? s->width - 16u : 0u, 1,
               theme->window_border);
  if (s->width <= margin * 2u) return;
  usable = s->width - margin * 2u;
  if (count > 1u && usable > gap * (count - 1u))
    usable -= gap * (count - 1u);
  button_w = count ? usable / count : 0u;
  for (uint32_t slot = 0; slot < count; slot++) {
    int index = taskbar_session_entry_by_slot(tb, slot);
    int32_t bx = (int32_t)(margin + slot * (button_w + gap));
    uint32_t bg = taskbar_session_button_color(slot);
    if (index < 0) continue;
    if (index == tb->hover_entry ||
        (tb->hover_entry < 0 && index == tb->selected_entry)) {
      bg = tb_lighten(bg, 44u);
    }
    tb_fill_rect(s, bx, y + 8, button_w, 24u, bg);
    tb_fill_rect(s, bx, y + 8, button_w, 1u, theme->window_border);
    tb_fill_rect(s, bx, y + 31, button_w, 1u, theme->window_border);
    tb_fill_rect(s, bx, y + 8, 1u, 24u, theme->window_border);
    if (button_w > 0u)
      tb_fill_rect(s, bx + (int32_t)button_w - 1, y + 8, 1u, 24u,
                   theme->window_border);
    taskbar_draw_session_icon(s, bx, y, button_w, slot, 0x00FFFFFFu);
  }
}

static void menu_popup_paint(struct gui_window *win) {
  if (!win || !win->user_data) return;
  struct taskbar *tb = (struct taskbar *)win->user_data;
  const struct gui_theme_palette *theme = compositor_theme();
  const struct font *f = font_default();
  struct gui_surface *s = &win->surface;
  char search_line[64];
  char recent_label[48];
  int32_t ey = TASKBAR_MENU_HEADER_HEIGHT;
  uint32_t recent_available = taskbar_recent_available_count(tb);
  uint32_t footer_h = taskbar_menu_footer_height(tb);
  uint32_t scroll_max = 0;
  const char *last = "";
  int32_t list_top = (int32_t)TASKBAR_MENU_HEADER_HEIGHT;
  int32_t list_bottom = 0;
  int visible = 0;
  if (!f) return;
  taskbar_clamp_menu_scroll(tb);
  scroll_max = taskbar_menu_scroll_max(tb);
  list_bottom = (int32_t)(s->height > footer_h ? s->height - footer_h : s->height);
  ey -= tb->menu_scroll_offset;
  tb_fill_rect(s, 0, 0, s->width, s->height, theme->window_bg);
  tb_fill_rect(s, 0, 0, 4, s->height, theme->accent_alt);
  tb_draw_fit(s, f, 16, 8, (s->width > 32u) ? s->width - 32u : 0u,
              "Capy Launcher", theme->accent);
  if (s->width > 24u) {
    tb_fill_rect(s, 12, 28, s->width - 24u, 22, theme->taskbar_bg);
    tb_fill_rect(s, 12, 28, s->width - 24u, 1, theme->window_border);
  }
  search_line[0] = '\0';
  tb_strcpy(search_line, tb->menu_filter[0] ? "Search: " : "Type to search",
            sizeof(search_line));
  if (tb->menu_filter[0]) tb_append(search_line, sizeof(search_line), tb->menu_filter);
  tb_draw_fit(s, f, 18, 34, (s->width > 36u) ? s->width - 36u : 0u,
              search_line, tb->menu_filter[0] ? theme->text : theme->text_muted);
  if (list_bottom > list_top) {
    tb_fill_rect(s, 8, list_top, s->width > 16u ? s->width - 16u : 0u, 1,
                 theme->window_border);
  }
  if (recent_available > 0u) {
    tb_strcpy(recent_label, APP_T("Recentes ", "Recent apps ", "Recientes "),
              sizeof(recent_label));
    tb_append(recent_label, sizeof(recent_label), ">");
    if (ey >= list_top && ey + (int32_t)TASKBAR_MENU_ENTRY_HEIGHT <= list_bottom) {
      taskbar_draw_menu_row(tb, s, f, TASKBAR_MENU_ROW_RECENT_TOGGLE, ey,
                            recent_label);
    }
    ey += TASKBAR_MENU_ENTRY_HEIGHT;
    visible = 1;
  }
  for (uint32_t i = 0; i < tb->menu_entry_count; i++) {
    const char *group = NULL;
    if (!taskbar_menu_entry_matches(tb, i)) continue;
    group = taskbar_menu_group_label(tb, i);
    if (!tb_streq(last, group)) {
      if (ey >= list_top && ey + (int32_t)TASKBAR_MENU_CATEGORY_HEIGHT <= list_bottom) {
        tb_draw_fit(s, f, 16, ey + 2, (s->width > 32u) ? s->width - 32u : 0u,
                    group, theme->text_muted);
      }
      ey += TASKBAR_MENU_CATEGORY_HEIGHT;
      last = group;
    }
    if (ey >= list_top && ey + (int32_t)TASKBAR_MENU_ENTRY_HEIGHT <= list_bottom) {
      taskbar_draw_menu_row(tb, s, f, (int)i, ey, tb->menu_entries[i].label);
    }
    ey += TASKBAR_MENU_ENTRY_HEIGHT;
    visible = 1;
  }
  if (!visible) {
    int32_t empty_y = list_top + 4;
    if (empty_y + (int32_t)TASKBAR_MENU_EMPTY_HEIGHT <= list_bottom) {
      tb_draw_fit(s, f, 16, empty_y + 6, (s->width > 32u) ? s->width - 32u : 0u,
                  "No apps found", theme->text_muted);
    }
  }
  if (scroll_max > 0u && list_bottom > list_top + 10 && s->width > 12u) {
    uint32_t track_h = (uint32_t)(list_bottom - list_top - 6);
    uint32_t thumb_h = 0;
    int32_t track_x = (int32_t)s->width - 8;
    int32_t track_y = list_top + 3;
    int32_t thumb_y = track_y;
    tb_fill_rect(s, track_x, track_y, 3u, track_h, theme->window_border);
    thumb_h = track_h > 18u ? track_h / 3u : track_h;
    if (thumb_h < 12u && track_h >= 12u) thumb_h = 12u;
    if (thumb_h > track_h) thumb_h = track_h;
    if (track_h > thumb_h) {
      thumb_y += (int32_t)(((uint32_t)tb->menu_scroll_offset * (track_h - thumb_h)) / scroll_max);
    }
    tb_fill_rect(s, track_x - 1, thumb_y, 5u, thumb_h, theme->accent);
  }
  taskbar_draw_session_footer(tb, s, f);
}

void taskbar_toggle_menu(struct taskbar *tb) {
  if (!tb) return;
  tb->menu_open = !tb->menu_open;

  if (tb->menu_open) {
    tb->menu_filter[0] = '\0';
    tb->recent_expanded = 0;
    tb->menu_scroll_offset = 0;
    tb->selected_entry = taskbar_first_match(tb);
    tb->hover_entry = -1;
    /* Create popup on first open; reuse it afterwards */
    if (!tb->menu_popup && tb->menu_entry_count > 0) {
      uint32_t popup_h = taskbar_menu_visible_height(tb);
      int32_t popup_x = 0;
      int32_t popup_y = 0;
      taskbar_menu_popup_position(tb, TASKBAR_MENU_WIDTH, popup_h,
                                  &popup_x, &popup_y);
      tb->menu_popup = compositor_create_window(
          "Menu", popup_x, popup_y, TASKBAR_MENU_WIDTH, popup_h);
      if (tb->menu_popup) {
        tb->menu_popup->decorated = 0;
        tb->menu_popup->movable = 0;
        tb->menu_popup->resizable = 0;
        /* Etapa UX W7-ish (2026-05-03): cantos arredondados (raio
         * 6 px) + border externa do tema. O compositor desenha
         * automaticamente quando corner_radius != 0. */
        tb->menu_popup->corner_radius = 6;
        tb->menu_popup->border_color =
            compositor_theme()->window_border;
        tb->menu_popup->z_order = COMPOSITOR_MAX_WINDOWS + 5;
        tb->menu_popup->bg_color = compositor_theme()->window_bg;
        tb->menu_popup->user_data = tb;
        tb->menu_popup->on_paint = menu_popup_paint;
      }
    }
    if (tb->menu_popup) {
      taskbar_refresh_menu_popup(tb);
      compositor_show_window(tb->menu_popup->id);
      compositor_invalidate(tb->menu_popup->id);
    }
  } else {
    tb->hover_entry = -1;
    tb->selected_entry = -1;
    tb->menu_filter[0] = '\0';
    taskbar_hide_recent_popup(tb);
    if (tb->menu_popup) {
      compositor_hide_window(tb->menu_popup->id);
    }
  }

  if (tb->window) compositor_invalidate(tb->window->id);
}

static void taskbar_add_menu_entry_flags(struct taskbar *tb, const char *label,
                                         void (*action)(void *),
                                         void *user_data, int pinned,
                                         int recentable) {
  if (!tb || tb->menu_entry_count >= TASKBAR_MENU_MAX_ENTRIES) return;
  struct taskbar_menu_entry *e = &tb->menu_entries[tb->menu_entry_count++];
  tb_strcpy(e->label, label ? label : "", TASKBAR_ITEM_NAME_MAX);
  e->action = action;
  e->user_data = user_data;
  e->is_separator = 0;
  e->pinned = pinned ? 1 : 0;
  e->recentable = recentable ? 1 : 0;
}

void taskbar_add_menu_entry(struct taskbar *tb, const char *label,
                            void (*action)(void *), void *user_data) {
  taskbar_add_menu_entry_flags(tb, label, action, user_data, 0, 0);
}

void taskbar_add_menu_entry_pinned(struct taskbar *tb, const char *label,
                                   void (*action)(void *),
                                   void *user_data) {
  taskbar_add_menu_entry_flags(tb, label, action, user_data, 1, 1);
}

void taskbar_add_menu_separator(struct taskbar *tb) {
  if (!tb || tb->menu_entry_count >= TASKBAR_MENU_MAX_ENTRIES) return;
  struct taskbar_menu_entry *e = &tb->menu_entries[tb->menu_entry_count++];
  e->label[0] = '\0';
  e->action = NULL;
  e->user_data = NULL;
  e->is_separator = 1;
  e->pinned = 0;
  e->recentable = 0;
}

static int taskbar_activate_menu_entry(struct taskbar *tb, int index) {
  void (*action)(void *) = NULL;
  void *user_data = NULL;
  if (!tb || index < 0) return 0;
  if (index == TASKBAR_MENU_ROW_RECENT_TOGGLE) {
    tb->hover_entry = TASKBAR_MENU_ROW_RECENT_TOGGLE;
    tb->selected_entry = TASKBAR_MENU_ROW_RECENT_TOGGLE;
    taskbar_refresh_recent_popup(tb);
    return 1;
  }
  if (taskbar_row_is_recent(index)) {
    uint32_t recent = taskbar_row_recent_index(index);
    struct taskbar_recent_entry current;
    if (recent >= tb->recent_count) return 0;
    current = tb->recent_entries[recent];
    action = tb->recent_entries[recent].action;
    user_data = tb->recent_entries[recent].user_data;
    for (uint32_t i = recent; i > 0; i--) {
      tb->recent_entries[i] = tb->recent_entries[i - 1u];
    }
    tb->recent_entries[0] = current;
    taskbar_toggle_menu(tb);
    if (action) action(user_data);
    return 1;
  }
  if ((uint32_t)index >= tb->menu_entry_count) return 0;
  if (tb->menu_entries[index].is_separator) return 0;
  action = tb->menu_entries[index].action;
  user_data = tb->menu_entries[index].user_data;
  taskbar_note_recent(tb, &tb->menu_entries[index]);
  taskbar_toggle_menu(tb);
  if (action) action(user_data);
  return 1;
}

int taskbar_handle_menu_click(struct taskbar *tb, int32_t screen_x,
                              int32_t screen_y) {
  int index = -1;
  if (!tb || !tb->menu_open || !tb->menu_popup) return 0;
  int32_t px = tb->menu_popup->frame.x;
  int32_t py = tb->menu_popup->frame.y;
  uint32_t pw = tb->menu_popup->frame.width;
  uint32_t ph = tb->menu_popup->frame.height;
  if (tb->recent_popup && tb->recent_popup->visible) {
    int32_t rx = tb->recent_popup->frame.x;
    int32_t ry = tb->recent_popup->frame.y;
    uint32_t rw = tb->recent_popup->frame.width;
    uint32_t rh = tb->recent_popup->frame.height;
    if (screen_x >= rx && screen_x < rx + (int32_t)rw &&
        screen_y >= ry && screen_y < ry + (int32_t)rh) {
      index = taskbar_recent_popup_entry_at(tb, screen_y - ry);
      if (index >= 0) return taskbar_activate_menu_entry(tb, index);
      return 1;
    }
  }
  if (screen_x < px || screen_x >= px + (int32_t)pw ||
      screen_y < py || screen_y >= py + (int32_t)ph) {
    taskbar_toggle_menu(tb);
    return 0;
  }
  index = taskbar_menu_entry_at(tb, screen_x - px, screen_y - py);
  if (index >= 0) return taskbar_activate_menu_entry(tb, index);
  return 1;
}

void taskbar_handle_menu_hover(struct taskbar *tb, int32_t screen_x,
                                int32_t screen_y) {
  int new_hover = -1;
  if (!tb || !tb->menu_open || !tb->menu_popup) return;
  int32_t px = tb->menu_popup->frame.x;
  int32_t py = tb->menu_popup->frame.y;
  uint32_t pw = tb->menu_popup->frame.width;
  uint32_t ph = tb->menu_popup->frame.height;
  if (tb->recent_popup && tb->recent_popup->visible) {
    int32_t rx = tb->recent_popup->frame.x;
    int32_t ry = tb->recent_popup->frame.y;
    uint32_t rw = tb->recent_popup->frame.width;
    uint32_t rh = tb->recent_popup->frame.height;
    if (screen_x >= rx && screen_x < rx + (int32_t)rw &&
        screen_y >= ry && screen_y < ry + (int32_t)rh) {
      new_hover = taskbar_recent_popup_entry_at(tb, screen_y - ry);
    }
  }
  if (new_hover < 0 && screen_x >= px && screen_x < px + (int32_t)pw &&
      screen_y >= py && screen_y < py + (int32_t)ph) {
    new_hover = taskbar_menu_entry_at(tb, screen_x - px, screen_y - py);
  }
  if (new_hover != tb->hover_entry) {
    tb->hover_entry = new_hover;
    if (new_hover >= 0) tb->selected_entry = new_hover;
    if (tb->menu_popup) compositor_invalidate(tb->menu_popup->id);
    if (tb->recent_popup && tb->recent_popup->visible)
      compositor_invalidate(tb->recent_popup->id);
    taskbar_refresh_recent_popup(tb);
  } else if (new_hover == TASKBAR_MENU_ROW_RECENT_TOGGLE ||
             taskbar_row_is_recent(new_hover)) {
    taskbar_refresh_recent_popup(tb);
  }
}


int taskbar_handle_menu_scroll(struct taskbar *tb, int32_t screen_x,
                               int32_t screen_y, int32_t delta) {
  int old_offset = 0;
  int step = (int)TASKBAR_MENU_ENTRY_HEIGHT;
  int32_t px = 0;
  int32_t py = 0;
  uint32_t pw = 0;
  uint32_t ph = 0;
  if (!tb || !tb->menu_open || !tb->menu_popup || delta == 0) return 0;
  px = tb->menu_popup->frame.x;
  py = tb->menu_popup->frame.y;
  pw = tb->menu_popup->frame.width;
  ph = tb->menu_popup->frame.height;
  if (screen_x < px || screen_x >= px + (int32_t)pw ||
      screen_y < py || screen_y >= py + (int32_t)ph)
    return 0;
  old_offset = tb->menu_scroll_offset;
  if (delta > 0) tb->menu_scroll_offset -= step;
  else tb->menu_scroll_offset += step;
  taskbar_clamp_menu_scroll(tb);
  if (tb->menu_scroll_offset != old_offset) {
    tb->hover_entry = -1;
    taskbar_hide_recent_popup(tb);
    compositor_invalidate(tb->menu_popup->id);
  }
  return 1;
}

int taskbar_handle_menu_key(struct taskbar *tb, uint32_t keycode, char ch) {
  uint32_t len = 0;
  if (!tb || !tb->menu_open) return 0;
  if (keycode == KEY_UP) {
    tb->selected_entry = taskbar_prev_match(tb, tb->selected_entry);
    tb->hover_entry = -1;
    taskbar_hide_recent_popup(tb);
    taskbar_refresh_menu_popup(tb);
    return 1;
  }
  if (keycode == KEY_DOWN) {
    tb->selected_entry = taskbar_next_match(tb, tb->selected_entry);
    tb->hover_entry = -1;
    taskbar_hide_recent_popup(tb);
    taskbar_refresh_menu_popup(tb);
    return 1;
  }
  if (ch == '\n' || ch == '\r' || keycode == '\n' || keycode == '\r') {
    if (tb->selected_entry >= 0) {
      return taskbar_activate_menu_entry(tb, tb->selected_entry);
    }
    return 1;
  }
  if (ch == '\b' || keycode == '\b') {
    len = tb_strlen(tb->menu_filter);
    if (len > 0) tb->menu_filter[len - 1u] = '\0';
    tb->selected_entry = taskbar_first_match(tb);
    tb->hover_entry = -1;
    tb->menu_scroll_offset = 0;
    taskbar_hide_recent_popup(tb);
    taskbar_refresh_menu_popup(tb);
    return 1;
  }
  if (keycode == KEY_DELETE) {
    tb->menu_filter[0] = '\0';
    tb->selected_entry = taskbar_first_match(tb);
    tb->hover_entry = -1;
    tb->menu_scroll_offset = 0;
    taskbar_hide_recent_popup(tb);
    taskbar_refresh_menu_popup(tb);
    return 1;
  }
  if (ch >= 32 && ch < 127) {
    len = tb_strlen(tb->menu_filter);
    if (len + 1u < TASKBAR_MENU_SEARCH_MAX) {
      tb->menu_filter[len] = ch;
      tb->menu_filter[len + 1u] = '\0';
    }
    tb->selected_entry = taskbar_first_match(tb);
    tb->hover_entry = -1;
    tb->menu_scroll_offset = 0;
    taskbar_hide_recent_popup(tb);
    taskbar_refresh_menu_popup(tb);
    return 1;
  }
  return 1;
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
