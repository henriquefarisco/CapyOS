/*
 * src/gui/desktop/taskbar_menu_input.c
 *
 * Event handlers for the taskbar start menu: toggle, click, hover,
 * scroll and keyboard. Carved out of `src/gui/desktop/taskbar.c` at
 * the 2026-05-15 refactor so each translation unit stays under the
 * 900-line layout limit.
 *
 * Public symbols owned here:
 *   - `taskbar_toggle_menu`
 *   - `taskbar_handle_menu_click`
 *   - `taskbar_handle_menu_hover`
 *   - `taskbar_handle_menu_scroll`
 *   - `taskbar_handle_menu_key`
 *
 * Data model, popup rendering and helpers live in
 * `taskbar_menu.c`; the main bar lifecycle lives in `taskbar.c`.
 */
#include "gui/taskbar.h"
#include "gui/compositor.h"
#include "drivers/input/keyboard_layout.h"

#include "gui/desktop/internal/taskbar_internal.h"

#include <stddef.h>
#include <stdint.h>

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
