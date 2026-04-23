#include "apps/task_manager.h"
#include "gui/compositor.h"
#include "gui/font.h"
#include "services/service_manager.h"
#include "util/kstring.h"
#include "memory/kmem.h"
#include <stddef.h>

static struct task_manager_app g_tm;
static int g_tm_open = 0;

static void itoa_simple(int v, char *buf, int buflen) {
  int p = 0;
  if (v < 0) { buf[p++] = '-'; v = -v; }
  if (v == 0) { buf[p++] = '0'; buf[p] = '\0'; return; }
  char tmp[12]; int tp = 0;
  while (v > 0 && tp < 11) { tmp[tp++] = '0' + (v % 10); v /= 10; }
  for (int i = tp - 1; i >= 0 && p < buflen - 1; i--) buf[p++] = tmp[i];
  buf[p] = '\0';
}

static int task_manager_visible_count(void) {
  return (int)service_manager_count();
}

static int task_manager_service_for_row(int row,
                                        struct system_service_status *service_out) {
  if (row < 0 || !service_out) return -1;
  return service_manager_get_at((size_t)row, service_out);
}

void task_manager_refresh(struct task_manager_app *app) {
  int visible = 0;
  if (!app) return;
  visible = task_manager_visible_count();
  if (visible <= 0) {
    app->selected = -1;
    app->scroll_offset = 0;
  } else {
    if (app->selected >= visible) app->selected = visible - 1;
    if (app->scroll_offset < 0) app->scroll_offset = 0;
    if (app->scroll_offset >= visible) app->scroll_offset = visible - 1;
  }
  if (app->window) compositor_invalidate(app->window->id);
}

static void task_manager_cleanup(void) {
  g_tm.window = NULL;
  g_tm_open = 0;
}

static void task_manager_on_close(struct gui_window *win) {
  (void)win;
  task_manager_cleanup();
}

static void task_manager_window_paint(struct gui_window *win) {
  if (!win || !win->user_data) return;
  task_manager_paint((struct task_manager_app *)win->user_data);
}

static void task_manager_window_mouse(struct gui_window *win, int32_t x, int32_t y,
                                      uint8_t buttons) {
  if (!win || !win->user_data || !(buttons & 1)) return;
  struct task_manager_app *app = (struct task_manager_app *)win->user_data;
  int32_t footer_y = (int32_t)(win->frame.height - 22);
  if (y >= footer_y && x >= 80 && x < 156) {
    task_manager_refresh(app);
    return;
  }
  if (y >= footer_y && x >= (int32_t)(win->frame.width - 72) &&
      x < (int32_t)(win->frame.width - 8)) {
    task_manager_restart_selected(app);
    compositor_invalidate(win->id);
    return;
  }
  if (y < 24) return;
  int row = (y - 24) / 18 + app->scroll_offset;
  {
    struct system_service_status service;
    if (row >= 0 && task_manager_service_for_row(row, &service) == 0) {
    app->selected = row;
    compositor_invalidate(win->id);
    }
  }
}

static void task_manager_window_scroll(struct gui_window *win, int32_t delta) {
  int visible = 0;
  if (!win || !win->user_data) return;
  struct task_manager_app *app = (struct task_manager_app *)win->user_data;
  visible = task_manager_visible_count();
  if (visible <= 0) {
    task_manager_refresh(app);
    return;
  }
  if (delta > 0 && app->scroll_offset > 0)
    app->scroll_offset--;
  else if (delta < 0 && app->scroll_offset < visible - 1)
    app->scroll_offset++;
  task_manager_refresh(app);
}

void task_manager_open(void) {
  const struct gui_theme_palette *theme = compositor_theme();
  uint8_t scale = compositor_ui_scale();

  /* If already open, just focus the existing window */
  if (g_tm_open && g_tm.window) {
    compositor_show_window(g_tm.window->id);
    compositor_focus_window(g_tm.window->id);
    return;
  }

  /* Clean up stale state */
  task_manager_cleanup();
  kmemzero(&g_tm, sizeof(g_tm));

  g_tm.window = compositor_create_window("Task Manager", 150, 100,
                                         450 + 120 * (scale - 1),
                                         350 + 100 * (scale - 1));
  if (!g_tm.window) return;
  g_tm.window->bg_color = theme->window_bg;
  g_tm.window->border_color = theme->window_border;
  g_tm.window->user_data = &g_tm;
  g_tm.window->on_paint = task_manager_window_paint;
  g_tm.window->on_mouse = task_manager_window_mouse;
  g_tm.window->on_scroll = task_manager_window_scroll;
  g_tm.window->on_close = task_manager_on_close;
  g_tm.selected = -1;
  g_tm.scroll_offset = 0;
  compositor_show_window(g_tm.window->id);
  compositor_focus_window(g_tm.window->id);
  g_tm_open = 1;
  task_manager_refresh(&g_tm);
}

void task_manager_paint(struct task_manager_app *app) {
  if (!app || !app->window) return;
  struct gui_surface *s = &app->window->surface;
  const struct font *f = font_default();
  const struct gui_theme_palette *theme = compositor_theme();
  if (!f) return;

  for (uint32_t y = 0; y < s->height; y++) {
    uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + y * s->pitch);
    for (uint32_t x = 0; x < s->width; x++) line[x] = theme->window_bg;
  }

  font_draw_string(s, f, 8, 4, "ID  STATE      START NAME", theme->accent);

  for (uint32_t x = 0; x < s->width; x++) {
    uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + 20 * s->pitch);
    line[x] = theme->window_border;
  }

  int32_t ypos = 24;
  int row = 0;
  for (size_t i = 0; i < service_manager_count(); i++) {
    struct system_service_status service;
    if (service_manager_get_at(i, &service) != 0) continue;
    if (row < app->scroll_offset) { row++; continue; }
    if (ypos + 18 > (int32_t)s->height - 24) break;

    if (row == app->selected) {
      for (uint32_t x = 0; x < s->width; x++) {
        uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + (uint32_t)ypos * s->pitch);
        line[x] = theme->accent_alt;
      }
    }

    char id_buf[8];
    itoa_simple((int)service.id, id_buf, 8);
    font_draw_string(s, f, 8, ypos, id_buf, theme->text);
    font_draw_string(s, f, 32, ypos, service_manager_state_label(service.state),
                     service.state == SYSTEM_SERVICE_STATE_READY ? theme->accent : theme->text_muted);
    font_draw_string(s, f, 124, ypos, service_manager_startup_label(service.startup), theme->text);
    font_draw_string(s, f, 172, ypos, service.name, theme->text);

    ypos += 18;
    row++;
  }

  int32_t footer_y = (int32_t)(s->height - 22);
  char count_buf[32] = "Services: ";
  size_t tc = service_manager_count();
  char tmp[8]; int tp = 0;
  if (tc == 0) tmp[tp++] = '0';
  else { uint32_t v = (uint32_t)tc; char r[8]; int rp = 0;
         while (v > 0) { r[rp++] = '0' + (v%10); v /= 10; }
         for (int i = rp-1; i >= 0; i--) tmp[tp++] = r[i]; }
  tmp[tp] = '\0';
  int p = 10;
  for (int i = 0; tmp[i] && p < 30; i++) count_buf[p++] = tmp[i];
  count_buf[p] = '\0';
  font_draw_string(s, f, 8, footer_y + 2, count_buf, theme->text_muted);

  {
    uint32_t btn_w = 76, btn_h = 20;
    int32_t btn_x = 80;
    uint32_t btn_bg = theme->accent_alt;
    for (uint32_t by = 0; by < btn_h; by++) {
      int32_t py = footer_y + (int32_t)by;
      if (py < 0 || (uint32_t)py >= s->height) continue;
      uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + (uint32_t)py * s->pitch);
      for (uint32_t bx = 0; bx < btn_w; bx++) {
        int32_t px = btn_x + (int32_t)bx;
        if (px >= 0 && (uint32_t)px < s->width) line[px] = btn_bg;
      }
    }
    font_draw_string(s, f, btn_x + 8, footer_y + 2, "Refresh", theme->text);
  }

  {
    uint32_t btn_w = 64, btn_h = 20;
    int32_t btn_x = (int32_t)(s->width - btn_w - 8);
    uint32_t btn_bg = (app->selected >= 0) ? 0x00CC3333 : theme->accent_alt;
    for (uint32_t by = 0; by < btn_h; by++) {
      int32_t py = footer_y + (int32_t)by;
      if (py < 0 || (uint32_t)py >= s->height) continue;
      uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + (uint32_t)py * s->pitch);
      for (uint32_t bx = 0; bx < btn_w; bx++) {
        int32_t px = btn_x + (int32_t)bx;
        if (px >= 0 && (uint32_t)px < s->width) line[px] = btn_bg;
      }
    }
    font_draw_string(s, f, btn_x + 4, footer_y + 2, "Restart", 0x00FFFFFF);
  }
}

void task_manager_restart_selected(struct task_manager_app *app) {
  struct system_service_status service;
  if (!app || app->selected < 0) return;
  if (task_manager_service_for_row(app->selected, &service) != 0) return;
  (void)service_manager_restart(service.id);
  task_manager_refresh(app);
}
