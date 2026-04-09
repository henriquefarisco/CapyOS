#include "apps/task_manager.h"
#include "gui/compositor.h"
#include "gui/font.h"
#include "kernel/task.h"
#include "memory/kmem.h"
#include <stddef.h>

static struct task_manager_app g_tm;

void task_manager_open(void) {
  g_tm.window = compositor_create_window("Task Manager", 150, 100, 450, 350);
  if (!g_tm.window) return;
  g_tm.window->bg_color = 0x1E1E2E;
  g_tm.selected = -1;
  g_tm.scroll_offset = 0;
  compositor_show_window(g_tm.window->id);
  compositor_focus_window(g_tm.window->id);
}

void task_manager_paint(struct task_manager_app *app) {
  if (!app || !app->window) return;
  struct gui_surface *s = &app->window->surface;
  const struct font *f = font_default();
  if (!f) return;

  for (uint32_t y = 0; y < s->height; y++) {
    uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + y * s->pitch);
    for (uint32_t x = 0; x < s->width; x++) line[x] = 0x1E1E2E;
  }

  font_draw_string(s, f, 8, 4, "PID   STATE     PRI  NAME", 0x89B4FA);

  /* Separator */
  for (uint32_t x = 0; x < s->width; x++) {
    uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + 20 * s->pitch);
    line[x] = 0x313244;
  }

  /* Use task_list callback to render. For simplicity, print to surface directly. */
  /* This is a visual stub — a real impl would iterate task_table */
  font_draw_string(s, f, 8, 24, "1     running   2    kernel-main", 0xCDD6F4);
  font_draw_string(s, f, 8, 42, "2     ready     0    idle", 0x6C7086);

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
  font_draw_string(s, f, 8, (int32_t)(s->height - 20), count_buf, 0xA6ADC8);
}

void task_manager_kill_selected(struct task_manager_app *app) {
  (void)app;
  /* Would call task_kill(selected_pid) */
}
