/*
 * src/gui/desktop/taskbar_menu.c
 *
 * Start-menu data model, popup rendering and recent/session helpers
 * for the taskbar. Carved out of `src/gui/desktop/taskbar.c` at the
 * 2026-05-15 refactor so each translation unit stays under the
 * 900-line layout limit.
 *
 * Owns:
 *   - Menu entry / recent / session match helpers used to walk the
 *     dynamic data model from both the popup paint and the event
 *     handlers.
 *   - Popup positioning, height computation and scroll clamping.
 *   - Drawing primitives for menu rows, recent popup and session
 *     footer.
 *   - The two `gui_window::on_paint` callbacks registered for the
 *     menu and recent popups (`menu_popup_paint`, `recent_popup_paint`).
 *   - Public `taskbar_add_menu_entry*` and `taskbar_add_menu_separator`
 *     mutators.
 *
 * Event handlers (toggle, click, hover, scroll, key, activate) live
 * in `taskbar_menu_input.c`.
 */
#include "gui/taskbar.h"
#include "gui/compositor.h"
#include "gui/font.h"
#include "lang/app_language.h"

#include "gui/desktop/internal/taskbar_internal.h"

#include <stddef.h>
#include <stdint.h>

static uint32_t taskbar_menu_entry_group(struct taskbar *tb, uint32_t index) {
  uint32_t group = 0;
  if (!tb || index >= tb->menu_entry_count) return 0;
  for (uint32_t i = 0; i < index && i < tb->menu_entry_count; i++) {
    if (tb->menu_entries[i].is_separator) group++;
  }
  return group;
}

const char *taskbar_menu_group_label(struct taskbar *tb, uint32_t index) {
  uint32_t group = taskbar_menu_entry_group(tb, index);
  if (!tb || index >= tb->menu_entry_count) return "Apps";
  if (tb->menu_entries[index].pinned) return "Pinned";
  if (group == 0) return "Apps";
  if (group == 1) return "System";
  return "Session";
}

int taskbar_menu_entry_is_session(struct taskbar *tb, uint32_t index) {
  if (!tb || index >= tb->menu_entry_count) return 0;
  if (tb->menu_entries[index].is_separator) return 0;
  return taskbar_menu_entry_group(tb, index) >= 2u;
}

uint32_t taskbar_session_count(struct taskbar *tb) {
  uint32_t count = 0;
  if (!tb) return 0;
  for (uint32_t i = 0; i < tb->menu_entry_count; i++) {
    if (taskbar_menu_entry_is_session(tb, i)) count++;
  }
  return count;
}

int taskbar_session_entry_by_slot(struct taskbar *tb, uint32_t slot) {
  uint32_t seen = 0;
  if (!tb) return -1;
  for (uint32_t i = 0; i < tb->menu_entry_count; i++) {
    if (!taskbar_menu_entry_is_session(tb, i)) continue;
    if (seen == slot) return (int)i;
    seen++;
  }
  return -1;
}

int taskbar_menu_entry_matches(struct taskbar *tb, uint32_t index) {
  const char *group = NULL;
  if (!tb || index >= tb->menu_entry_count) return 0;
  if (tb->menu_entries[index].is_separator) return 0;
  if (taskbar_menu_entry_is_session(tb, index)) return 0;
  if (!tb->menu_filter[0]) return 1;
  group = taskbar_menu_group_label(tb, index);
  return tb_contains_ci(tb->menu_entries[index].label, tb->menu_filter) ||
         tb_contains_ci(group, tb->menu_filter);
}

int taskbar_recent_row(uint32_t index) {
  return TASKBAR_MENU_ROW_RECENT_BASE + (int)index;
}

int taskbar_row_is_recent(int row) {
  return row >= TASKBAR_MENU_ROW_RECENT_BASE;
}

uint32_t taskbar_row_recent_index(int row) {
  return (uint32_t)(row - TASKBAR_MENU_ROW_RECENT_BASE);
}

int taskbar_recent_entry_matches(struct taskbar *tb, uint32_t index) {
  if (!tb || index >= tb->recent_count) return 0;
  if (!tb->menu_filter[0]) return 1;
  return tb_contains_ci(tb->recent_entries[index].label, tb->menu_filter) ||
         tb_contains_ci("Recent", tb->menu_filter);
}

int taskbar_recent_matches(struct taskbar *tb, uint32_t index) {
  if (!tb || !tb->recent_expanded) return 0;
  return taskbar_recent_entry_matches(tb, index);
}

uint32_t taskbar_recent_available_count(struct taskbar *tb) {
  uint32_t count = 0;
  if (!tb) return 0;
  for (uint32_t i = 0; i < tb->recent_count; i++) {
    if (taskbar_recent_entry_matches(tb, i)) count++;
  }
  return count;
}

void taskbar_note_recent(struct taskbar *tb,
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

int taskbar_first_match(struct taskbar *tb) {
  if (taskbar_match_count(tb) == 0u) return -1;
  return taskbar_match_at_ordinal(tb, 0u);
}

int taskbar_last_match(struct taskbar *tb) {
  uint32_t count = taskbar_match_count(tb);
  if (count == 0u) return -1;
  return taskbar_match_at_ordinal(tb, count - 1u);
}

int taskbar_next_match(struct taskbar *tb, int current) {
  uint32_t count = taskbar_match_count(tb);
  int ord = 0;
  if (!tb || count == 0u) return -1;
  ord = taskbar_ordinal_of(tb, current);
  if (ord < 0) return taskbar_first_match(tb);
  return taskbar_match_at_ordinal(tb, ((uint32_t)ord + 1u) % count);
}

int taskbar_prev_match(struct taskbar *tb, int current) {
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

uint32_t taskbar_menu_footer_height(struct taskbar *tb) {
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

uint32_t taskbar_menu_visible_height(struct taskbar *tb) {
  uint32_t total = menu_total_height(tb);
  uint32_t max_h = taskbar_menu_max_height(tb);
  return total > max_h ? max_h : total;
}

uint32_t taskbar_recent_popup_height(struct taskbar *tb) {
  uint32_t count = taskbar_recent_available_count(tb);
  return count ? 4u + count * TASKBAR_MENU_ENTRY_HEIGHT : 0u;
}

uint32_t taskbar_menu_current_height(struct taskbar *tb) {
  if (tb && tb->menu_popup && tb->menu_popup->frame.height)
    return tb->menu_popup->frame.height;
  return taskbar_menu_visible_height(tb);
}

uint32_t taskbar_menu_scroll_max(struct taskbar *tb) {
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

void taskbar_clamp_menu_scroll(struct taskbar *tb) {
  uint32_t max_scroll = 0;
  if (!tb) return;
  max_scroll = taskbar_menu_scroll_max(tb);
  if (tb->menu_scroll_offset < 0) tb->menu_scroll_offset = 0;
  if ((uint32_t)tb->menu_scroll_offset > max_scroll)
    tb->menu_scroll_offset = (int)max_scroll;
}

void taskbar_menu_popup_position(struct taskbar *tb, uint32_t popup_w,
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

int taskbar_menu_entry_at(struct taskbar *tb, int32_t local_x,
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

void taskbar_refresh_menu_popup(struct taskbar *tb) {
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

int taskbar_recent_popup_entry_at(struct taskbar *tb, int32_t local_y) {
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

void taskbar_hide_recent_popup(struct taskbar *tb) {
  if (tb && tb->recent_popup) compositor_hide_window(tb->recent_popup->id);
}

void taskbar_refresh_recent_popup(struct taskbar *tb) {
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

void menu_popup_paint(struct gui_window *win) {
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
