#include "gui/taskbar.h"
#include "gui/font.h"
#include "gui/compositor.h"
#include <stddef.h>

static void tb_strcpy(char *d, const char *s, size_t max) {
  size_t i = 0;
  while (i < max - 1 && s[i]) { d[i] = s[i]; i++; }
  d[i] = '\0';
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

void taskbar_init(struct taskbar *tb, uint32_t screen_w, uint32_t screen_h) {
  if (!tb) return;
  tb->position = TASKBAR_BOTTOM;
  tb->bg_color = 0x1E1E2E;
  tb->fg_color = 0xCDD6F4;
  tb->highlight_color = 0x45475A;
  tb->item_count = 0;
  tb->menu_open = 0;
  tb->show_clock = 1;
  tb_strcpy(tb->clock_text, "00:00:00", 16);

  int32_t y = (int32_t)(screen_h - TASKBAR_HEIGHT);
  tb->window = compositor_create_window("Taskbar", 0, y, screen_w, TASKBAR_HEIGHT);
  if (tb->window) {
    tb->window->decorated = 0;
    tb->window->movable = 0;
    tb->window->resizable = 0;
    tb->window->z_order = 999;
    compositor_show_window(tb->window->id);
  }
}

void taskbar_add_window(struct taskbar *tb, uint32_t window_id, const char *name) {
  if (!tb || tb->item_count >= TASKBAR_MAX_ITEMS) return;
  struct taskbar_item *item = &tb->items[tb->item_count++];
  item->window_id = window_id;
  tb_strcpy(item->name, name ? name : "Window", TASKBAR_ITEM_NAME_MAX);
  item->active = 1;
  item->focused = 0;
}

void taskbar_remove_window(struct taskbar *tb, uint32_t window_id) {
  if (!tb) return;
  for (uint32_t i = 0; i < tb->item_count; i++) {
    if (tb->items[i].window_id == window_id) {
      for (uint32_t j = i; j < tb->item_count - 1; j++)
        tb->items[j] = tb->items[j + 1];
      tb->item_count--;
      return;
    }
  }
}

void taskbar_set_focused(struct taskbar *tb, uint32_t window_id) {
  if (!tb) return;
  for (uint32_t i = 0; i < tb->item_count; i++) {
    tb->items[i].focused = (tb->items[i].window_id == window_id) ? 1 : 0;
  }
}

void taskbar_update_clock(struct taskbar *tb, const char *time_str) {
  if (tb && time_str) tb_strcpy(tb->clock_text, time_str, 16);
}

void taskbar_paint(struct taskbar *tb) {
  if (!tb || !tb->window) return;
  struct gui_surface *s = &tb->window->surface;

  tb_fill_rect(s, 0, 0, s->width, s->height, tb->bg_color);

  /* Separator line at top */
  tb_fill_rect(s, 0, 0, s->width, 1, 0x313244);

  const struct font *f = font_default();
  if (!f) return;

  /* Menu button */
  int32_t x = 4;
  tb_fill_rect(s, x, 4, 60, TASKBAR_HEIGHT - 8, 0x89B4FA);
  font_draw_string(s, f, x + 8, 8, "Menu", 0x1E1E2E);
  x += 68;

  /* Window list */
  for (uint32_t i = 0; i < tb->item_count; i++) {
    struct taskbar_item *item = &tb->items[i];
    uint32_t bg = item->focused ? tb->highlight_color : tb->bg_color;
    uint32_t item_w = 120;
    tb_fill_rect(s, x, 4, item_w, TASKBAR_HEIGHT - 8, bg);
    font_draw_string(s, f, x + 4, 8, item->name, tb->fg_color);
    x += (int32_t)item_w + 4;
  }

  /* Clock on right side */
  if (tb->show_clock) {
    uint32_t cw = font_string_width(f, tb->clock_text);
    int32_t cx = (int32_t)(s->width - cw - 12);
    font_draw_string(s, f, cx, 8, tb->clock_text, tb->fg_color);
  }
}

void taskbar_handle_click(struct taskbar *tb, int32_t x, int32_t y) {
  if (!tb) return;
  (void)y;

  if (x >= 4 && x < 64) {
    taskbar_toggle_menu(tb);
    return;
  }

  int32_t item_x = 68;
  for (uint32_t i = 0; i < tb->item_count; i++) {
    if (x >= item_x && x < item_x + 120) {
      compositor_focus_window(tb->items[i].window_id);
      compositor_show_window(tb->items[i].window_id);
      taskbar_set_focused(tb, tb->items[i].window_id);
      return;
    }
    item_x += 124;
  }
}

void taskbar_toggle_menu(struct taskbar *tb) {
  if (tb) tb->menu_open = !tb->menu_open;
}
