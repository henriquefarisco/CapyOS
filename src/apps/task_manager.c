#include "apps/task_manager.h"
#include "gui/compositor.h"
#include "gui/font.h"
#include "kernel/task.h"
#include "memory/kmem.h"
#include <stddef.h>

static struct task_manager_app g_tm;

static void task_manager_window_paint(struct gui_window *win) {
  if (!win || !win->user_data) return;
  task_manager_paint((struct task_manager_app *)win->user_data);
}

void task_manager_open(void) {
  const struct gui_theme_palette *theme = compositor_theme();
  uint8_t scale = compositor_ui_scale();
  g_tm.window = compositor_create_window("Task Manager", 150, 100,
                                         450 + 120 * (scale - 1),
                                         350 + 100 * (scale - 1));
  if (!g_tm.window) return;
  g_tm.window->bg_color = theme->window_bg;
  g_tm.window->border_color = theme->window_border;
  g_tm.window->user_data = &g_tm;
  g_tm.window->on_paint = task_manager_window_paint;
  g_tm.selected = -1;
  g_tm.scroll_offset = 0;
  compositor_show_window(g_tm.window->id);
  compositor_focus_window(g_tm.window->id);
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

  font_draw_string(s, f, 8, 4, "PID   STATE     PRI  NAME", theme->accent);

  /* Separator */
  for (uint32_t x = 0; x < s->width; x++) {
    uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + 20 * s->pitch);
    line[x] = theme->window_border;
  }

  /* Use task_list callback to render. For simplicity, print to surface directly. */
  /* This is a visual stub — a real impl would iterate task_table */
  font_draw_string(s, f, 8, 24, "1     running   2    kernel-main", theme->text);
  font_draw_string(s, f, 8, 42, "2     ready     0    idle", theme->text_muted);

  char count_buf[32] = "Tasks: ";
  size_t tc = task_count();
  char tmp[8]; int tp = 0;
  if (tc == 0) tmp[tp++] = '0';
  else { uint32_t v = (uint32_t)tc; char r[8]; int rp = 0;
         while (v > 0) { r[rp++] = '0' + (v%10); v /= 10; }
         for (int i = rp-1; i >= 0; i--) tmp[tp++] = r[i]; }
  tmp[tp] = '\0';
  int p = 7;
  for (int i = 0; tmp[i] && p < 30; i++) count_buf[p++] = tmp[i];
  count_buf[p] = '\0';
  font_draw_string(s, f, 8, (int32_t)(s->height - 20), count_buf, theme->text_muted);
}

void task_manager_kill_selected(struct task_manager_app *app) {
  (void)app;
  /* Would call task_kill(selected_pid) */
}
