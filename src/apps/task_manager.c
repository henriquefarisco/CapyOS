#include "apps/task_manager.h"
#include "gui/compositor.h"
#include "gui/font.h"
#include "kernel/process_iter.h"
#include "kernel/task_iter.h"
#include "services/service_manager.h"
#include "util/kstring.h"
#include "memory/kmem.h"
#include <stddef.h>

#define TASK_MANAGER_TAB_COUNT 3
#define TASK_MANAGER_TAB_HEIGHT 22

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

static int task_manager_count_tasks(void) {
  struct task_iter it;
  struct task_stats s;
  int n = 0;
  for (int ok = task_iter_first(&it, &s); ok; ok = task_iter_next(&it, &s)) {
    n++;
  }
  return n;
}

static int task_manager_count_processes(void) {
  struct process_iter it;
  struct process_stats s;
  int n = 0;
  for (int ok = process_iter_first(&it, &s); ok;
       ok = process_iter_next(&it, &s)) {
    n++;
  }
  return n;
}

static int task_manager_visible_count_for(enum task_manager_view view) {
  switch (view) {
  case TASK_MANAGER_VIEW_TASKS:
    return task_manager_count_tasks();
  case TASK_MANAGER_VIEW_PROCESSES:
    return task_manager_count_processes();
  case TASK_MANAGER_VIEW_SERVICES:
  default:
    return (int)service_manager_count();
  }
}

static int task_manager_visible_count(const struct task_manager_app *app) {
  if (!app) return 0;
  return task_manager_visible_count_for(app->view);
}

static int task_manager_service_for_row(int row,
                                        struct system_service_status *service_out) {
  if (row < 0 || !service_out) return -1;
  return service_manager_get_at((size_t)row, service_out);
}

/* task_manager_task_for_row / task_manager_process_for_row will return
 * in M4 phase 8 once kill-by-row is wired to fault-isolated processes. */

void task_manager_refresh(struct task_manager_app *app) {
  int visible = 0;
  if (!app) return;
  visible = task_manager_visible_count(app);
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

void task_manager_set_view(struct task_manager_app *app,
                           enum task_manager_view view) {
  if (!app) return;
  if (view != TASK_MANAGER_VIEW_SERVICES &&
      view != TASK_MANAGER_VIEW_TASKS &&
      view != TASK_MANAGER_VIEW_PROCESSES) {
    return;
  }
  if (app->view == view) return;
  app->view = view;
  app->selected = -1;
  app->scroll_offset = 0;
  task_manager_refresh(app);
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

static int task_manager_row_for_y(const struct task_manager_app *app,
                                  int32_t y) {
  if (!app) return -1;
  if (y < (int32_t)(TASK_MANAGER_TAB_HEIGHT + 24)) return -1;
  return (y - (TASK_MANAGER_TAB_HEIGHT + 24)) / 18 + app->scroll_offset;
}

static int task_manager_row_is_valid(const struct task_manager_app *app,
                                     int row) {
  return app && row >= 0 && row < task_manager_visible_count(app);
}

static enum task_manager_view task_manager_tab_for_x(int32_t x,
                                                     uint32_t window_w) {
  /* Three equal-width tabs spanning the top strip. */
  uint32_t third = window_w / 3u;
  if (third == 0u) return TASK_MANAGER_VIEW_SERVICES;
  if ((uint32_t)x < third)        return TASK_MANAGER_VIEW_SERVICES;
  if ((uint32_t)x < 2u * third)   return TASK_MANAGER_VIEW_TASKS;
  return TASK_MANAGER_VIEW_PROCESSES;
}

static void task_manager_window_mouse(struct gui_window *win, int32_t x, int32_t y,
                                      uint8_t buttons) {
  if (!win || !win->user_data || !(buttons & 1)) return;
  struct task_manager_app *app = (struct task_manager_app *)win->user_data;
  int32_t footer_y = (int32_t)(win->frame.height - 22);

  /* Tab strip at the top: switch view. */
  if (y < (int32_t)TASK_MANAGER_TAB_HEIGHT && x >= 0) {
    enum task_manager_view target =
        task_manager_tab_for_x(x, win->frame.width);
    task_manager_set_view(app, target);
    compositor_invalidate(win->id);
    return;
  }

  /* Footer buttons: Refresh and Restart. */
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

  /* Row selection. */
  int row = task_manager_row_for_y(app, y);
  if (task_manager_row_is_valid(app, row)) {
    app->selected = row;
    compositor_invalidate(win->id);
  }
}

static void task_manager_window_scroll(struct gui_window *win, int32_t delta) {
  int visible = 0;
  if (!win || !win->user_data) return;
  struct task_manager_app *app = (struct task_manager_app *)win->user_data;
  visible = task_manager_visible_count(app);
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
  g_tm.view = TASK_MANAGER_VIEW_SERVICES;
  compositor_show_window(g_tm.window->id);
  compositor_focus_window(g_tm.window->id);
  g_tm_open = 1;
  task_manager_refresh(&g_tm);
}

/* --- Painter helpers ------------------------------------------------- */

static void task_manager_fill_rect(struct gui_surface *s, int32_t x0,
                                   int32_t y0, uint32_t w, uint32_t h,
                                   uint32_t color) {
  for (uint32_t by = 0; by < h; by++) {
    int32_t py = y0 + (int32_t)by;
    if (py < 0 || (uint32_t)py >= s->height) continue;
    uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + (uint32_t)py * s->pitch);
    for (uint32_t bx = 0; bx < w; bx++) {
      int32_t px = x0 + (int32_t)bx;
      if (px >= 0 && (uint32_t)px < s->width) line[px] = color;
    }
  }
}

static void task_manager_paint_tabs(struct task_manager_app *app,
                                    struct gui_surface *s,
                                    const struct font *f,
                                    const struct gui_theme_palette *theme) {
  static const char *labels[TASK_MANAGER_TAB_COUNT] = {
    "Services", "Tasks", "Processes"
  };
  uint32_t third = s->width / 3u;
  if (third == 0u) return;
  task_manager_fill_rect(s, 0, 0, s->width, TASK_MANAGER_TAB_HEIGHT,
                         theme->window_bg);
  for (int i = 0; i < TASK_MANAGER_TAB_COUNT; i++) {
    int32_t tx = (int32_t)(third * (uint32_t)i);
    uint32_t tw = (i == TASK_MANAGER_TAB_COUNT - 1)
                      ? (s->width - third * (uint32_t)i)
                      : third;
    int active = ((int)app->view == i);
    uint32_t bg = active ? theme->accent_alt : theme->window_bg;
    task_manager_fill_rect(s, tx, 0, tw, TASK_MANAGER_TAB_HEIGHT, bg);
    uint32_t text_color = active ? theme->text : theme->text_muted;
    font_draw_string(s, f, tx + 8, 4, labels[i], text_color);
  }
  /* Bottom border under the tab strip. */
  task_manager_fill_rect(s, 0, TASK_MANAGER_TAB_HEIGHT - 1, s->width, 1,
                         theme->window_border);
}

static void task_manager_paint_header(struct gui_surface *s,
                                      const struct font *f,
                                      const struct gui_theme_palette *theme,
                                      const char *header_text) {
  int32_t header_y = TASK_MANAGER_TAB_HEIGHT + 4;
  int32_t border_y = TASK_MANAGER_TAB_HEIGHT + 20;
  font_draw_string(s, f, 8, header_y, header_text, theme->accent);
  task_manager_fill_rect(s, 0, border_y, s->width, 1, theme->window_border);
}

static int32_t task_manager_row_y(int row_index) {
  return (int32_t)(TASK_MANAGER_TAB_HEIGHT + 24) + row_index * 18;
}

static void task_manager_highlight_row(struct gui_surface *s,
                                       const struct gui_theme_palette *theme,
                                       int32_t ypos) {
  task_manager_fill_rect(s, 0, ypos, s->width, 18, theme->accent_alt);
}

static void task_manager_paint_services_rows(struct task_manager_app *app,
                                             struct gui_surface *s,
                                             const struct font *f,
                                             const struct gui_theme_palette *theme) {
  int32_t footer_y = (int32_t)(s->height - 22);
  int row = 0;
  for (size_t i = 0; i < service_manager_count(); i++) {
    struct system_service_status service;
    if (service_manager_get_at(i, &service) != 0) continue;
    if (row < app->scroll_offset) { row++; continue; }
    int32_t ypos = task_manager_row_y(row - app->scroll_offset);
    if (ypos + 18 > footer_y) break;
    if (row == app->selected) task_manager_highlight_row(s, theme, ypos);

    char id_buf[8];
    itoa_simple((int)service.id, id_buf, 8);
    font_draw_string(s, f, 8, ypos, id_buf, theme->text);
    font_draw_string(s, f, 32, ypos, service_manager_state_label(service.state),
                     service.state == SYSTEM_SERVICE_STATE_READY
                         ? theme->accent : theme->text_muted);
    font_draw_string(s, f, 124, ypos,
                     service_manager_startup_label(service.startup),
                     theme->text);
    font_draw_string(s, f, 172, ypos, service.name, theme->text);
    row++;
  }
}

static void task_manager_paint_tasks_rows(struct task_manager_app *app,
                                          struct gui_surface *s,
                                          const struct font *f,
                                          const struct gui_theme_palette *theme) {
  int32_t footer_y = (int32_t)(s->height - 22);
  struct task_iter it;
  struct task_stats t;
  int row = 0;
  for (int ok = task_iter_first(&it, &t); ok; ok = task_iter_next(&it, &t)) {
    if (row < app->scroll_offset) { row++; continue; }
    int32_t ypos = task_manager_row_y(row - app->scroll_offset);
    if (ypos + 18 > footer_y) break;
    if (row == app->selected) task_manager_highlight_row(s, theme, ypos);

    char pid_buf[8];
    itoa_simple((int)t.pid, pid_buf, 8);
    font_draw_string(s, f, 8, ypos, pid_buf, theme->text);
    font_draw_string(s, f, 48, ypos, task_state_label(t.state),
                     t.state == TASK_STATE_RUNNING || t.state == TASK_STATE_READY
                         ? theme->accent : theme->text_muted);
    font_draw_string(s, f, 124, ypos, task_priority_label(t.priority),
                     theme->text);
    font_draw_string(s, f, 172, ypos, t.name[0] ? t.name : "(unnamed)",
                     theme->text);
    row++;
  }
  if (row == 0) {
    int32_t ypos = task_manager_row_y(0);
    font_draw_string(s, f, 8, ypos, "(no active tasks yet)",
                     theme->text_muted);
  }
}

static void task_manager_paint_processes_rows(struct task_manager_app *app,
                                              struct gui_surface *s,
                                              const struct font *f,
                                              const struct gui_theme_palette *theme) {
  int32_t footer_y = (int32_t)(s->height - 22);
  struct process_iter it;
  struct process_stats p;
  int row = 0;
  for (int ok = process_iter_first(&it, &p); ok;
       ok = process_iter_next(&it, &p)) {
    if (row < app->scroll_offset) { row++; continue; }
    int32_t ypos = task_manager_row_y(row - app->scroll_offset);
    if (ypos + 18 > footer_y) break;
    if (row == app->selected) task_manager_highlight_row(s, theme, ypos);

    char pid_buf[8];
    itoa_simple((int)p.pid, pid_buf, 8);
    font_draw_string(s, f, 8, ypos, pid_buf, theme->text);
    font_draw_string(s, f, 48, ypos, process_state_label(p.state),
                     p.state == PROC_STATE_RUNNING ? theme->accent
                                                   : theme->text_muted);
    char uid_buf[8];
    itoa_simple((int)p.uid, uid_buf, 8);
    font_draw_string(s, f, 124, ypos, uid_buf, theme->text);
    font_draw_string(s, f, 172, ypos, p.name[0] ? p.name : "(unnamed)",
                     theme->text);
    row++;
  }
  if (row == 0) {
    int32_t ypos = task_manager_row_y(0);
    font_draw_string(s, f, 8, ypos, "(no active processes yet)",
                     theme->text_muted);
  }
}

static const char *task_manager_view_label(enum task_manager_view view) {
  switch (view) {
  case TASK_MANAGER_VIEW_SERVICES:  return "Services: ";
  case TASK_MANAGER_VIEW_TASKS:     return "Tasks: ";
  case TASK_MANAGER_VIEW_PROCESSES: return "Processes: ";
  default:                          return "Items: ";
  }
}

static void task_manager_paint_footer(struct task_manager_app *app,
                                      struct gui_surface *s,
                                      const struct font *f,
                                      const struct gui_theme_palette *theme) {
  int32_t footer_y = (int32_t)(s->height - 22);
  char count_buf[40];
  const char *label = task_manager_view_label(app->view);
  int p = 0;
  for (int i = 0; label[i] && p < (int)sizeof(count_buf) - 12; i++) {
    count_buf[p++] = label[i];
  }
  int total = task_manager_visible_count(app);
  if (total < 0) total = 0;
  char tmp[12];
  itoa_simple(total, tmp, sizeof(tmp));
  for (int i = 0; tmp[i] && p < (int)sizeof(count_buf) - 1; i++) {
    count_buf[p++] = tmp[i];
  }
  count_buf[p] = '\0';
  font_draw_string(s, f, 8, footer_y + 2, count_buf, theme->text_muted);

  /* Refresh button (always available). */
  task_manager_fill_rect(s, 80, footer_y, 76, 20, theme->accent_alt);
  font_draw_string(s, f, 88, footer_y + 2, "Refresh", theme->text);

  /* Restart button: only meaningful for services. */
  uint32_t btn_w = 64;
  int32_t btn_x = (int32_t)(s->width - btn_w - 8);
  int restart_enabled = (app->view == TASK_MANAGER_VIEW_SERVICES &&
                         app->selected >= 0);
  uint32_t btn_bg = restart_enabled ? 0x00CC3333u : theme->accent_alt;
  task_manager_fill_rect(s, btn_x, footer_y, btn_w, 20, btn_bg);
  font_draw_string(s, f, btn_x + 4, footer_y + 2, "Restart",
                   restart_enabled ? 0x00FFFFFFu : theme->text_muted);
}

void task_manager_paint(struct task_manager_app *app) {
  if (!app || !app->window) return;
  struct gui_surface *s = &app->window->surface;
  const struct font *f = font_default();
  const struct gui_theme_palette *theme = compositor_theme();
  if (!f) return;

  /* Background. */
  task_manager_fill_rect(s, 0, 0, s->width, s->height, theme->window_bg);

  /* Tab strip. */
  task_manager_paint_tabs(app, s, f, theme);

  /* Column header per view. */
  switch (app->view) {
  case TASK_MANAGER_VIEW_TASKS:
    task_manager_paint_header(s, f, theme, "PID    STATE    PRI       NAME");
    task_manager_paint_tasks_rows(app, s, f, theme);
    break;
  case TASK_MANAGER_VIEW_PROCESSES:
    task_manager_paint_header(s, f, theme, "PID    STATE    UID       NAME");
    task_manager_paint_processes_rows(app, s, f, theme);
    break;
  case TASK_MANAGER_VIEW_SERVICES:
  default:
    task_manager_paint_header(s, f, theme, "ID  STATE      START NAME");
    task_manager_paint_services_rows(app, s, f, theme);
    break;
  }

  task_manager_paint_footer(app, s, f, theme);
}

void task_manager_restart_selected(struct task_manager_app *app) {
  if (!app || app->selected < 0) return;
  /* Restart only makes sense for services. Killing tasks/processes from
   * the GUI is wired in M4 phase 8 once fault isolation is in place. */
  if (app->view != TASK_MANAGER_VIEW_SERVICES) return;
  struct system_service_status service;
  if (task_manager_service_for_row(app->selected, &service) != 0) return;
  (void)service_manager_restart(service.id);
  task_manager_refresh(app);
}
