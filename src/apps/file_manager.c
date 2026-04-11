#include "apps/file_manager.h"
#include "gui/compositor.h"
#include "gui/font.h"
#include "gui/widget.h"
#include "fs/vfs.h"
#include "memory/kmem.h"
#include <stddef.h>

static struct file_manager_app g_fm;

static void fm_memset(void *d, int v, size_t n) {
  uint8_t *p = (uint8_t *)d; for (size_t i = 0; i < n; i++) p[i] = (uint8_t)v;
}
static void fm_strcpy(char *d, const char *s, size_t max) {
  size_t i = 0; while (i < max - 1 && s[i]) { d[i] = s[i]; i++; } d[i] = '\0';
}
static size_t fm_strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }

static int fm_iter_cb(const char *name, uint16_t mode, void *ctx) {
  struct file_manager_app *app = (struct file_manager_app *)ctx;
  if (app->entry_count >= FM_MAX_ENTRIES) return -1;
  struct fm_entry *e = &app->entries[app->entry_count++];
  fm_strcpy(e->name, name, 64);
  e->mode = mode;
  e->size = 0;
  return 0;
}

static void file_manager_window_paint(struct gui_window *win) {
  if (!win || !win->user_data) return;
  file_manager_paint((struct file_manager_app *)win->user_data);
}

static void file_manager_window_mouse(struct gui_window *win, int32_t x, int32_t y,
                                      uint8_t buttons) {
  if (!win || !win->user_data || !(buttons & 1)) return;
  file_manager_handle_click((struct file_manager_app *)win->user_data, x, y);
}

static void file_manager_window_scroll(struct gui_window *win, int32_t delta) {
  if (!win || !win->user_data) return;
  struct file_manager_app *app = (struct file_manager_app *)win->user_data;
  if (delta > 0 && app->scroll_offset > 0)
    app->scroll_offset -= (delta > app->scroll_offset) ? app->scroll_offset : delta;
  else if (delta < 0 && app->scroll_offset + 1 < app->entry_count)
    app->scroll_offset += (-delta);
  if (app->scroll_offset >= app->entry_count && app->entry_count > 0)
    app->scroll_offset = app->entry_count - 1;
}

void file_manager_navigate(struct file_manager_app *app, const char *path) {
  if (!app || !path) return;
  fm_strcpy(app->current_path, path, FM_PATH_MAX);
  app->entry_count = 0;
  app->selected = -1;
  app->scroll_offset = 0;
  vfs_listdir(path, fm_iter_cb, app);
}

void file_manager_open(void) {
  const struct gui_theme_palette *theme = compositor_theme();
  uint8_t scale = compositor_ui_scale();
  fm_memset(&g_fm, 0, sizeof(g_fm));
  g_fm.window = compositor_create_window("File Manager", 80, 60,
                                         500 + 120 * (scale - 1),
                                         400 + 120 * (scale - 1));
  if (!g_fm.window) return;
  g_fm.window->bg_color = theme->window_bg;
  g_fm.window->border_color = theme->window_border;
  g_fm.window->user_data = &g_fm;
  g_fm.window->on_paint = file_manager_window_paint;
  g_fm.window->on_mouse = file_manager_window_mouse;
  g_fm.window->on_scroll = file_manager_window_scroll;
  compositor_show_window(g_fm.window->id);
  compositor_focus_window(g_fm.window->id);
  file_manager_navigate(&g_fm, "/");
}

void file_manager_paint(struct file_manager_app *app) {
  if (!app || !app->window) return;
  struct gui_surface *s = &app->window->surface;
  const struct font *f = font_default();
  const struct gui_theme_palette *theme = compositor_theme();
  if (!f) return;

  /* Clear background */
  for (uint32_t y = 0; y < s->height; y++) {
    uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + y * s->pitch);
    for (uint32_t x = 0; x < s->width; x++) line[x] = theme->window_bg;
  }

  /* Path bar */
  font_draw_string(s, f, 8, 4, app->current_path, theme->accent);

  /* Separator */
  for (uint32_t x = 0; x < s->width; x++) {
    uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + 20 * s->pitch);
    line[x] = theme->window_border;
  }

  /* File list */
  int32_t y = 24;
  for (int i = app->scroll_offset; i < app->entry_count && y < (int32_t)s->height - 20; i++) {
    struct fm_entry *e = &app->entries[i];
    uint32_t color = (e->mode & 0x2) ? theme->accent : theme->text;
    uint32_t bg = (i == app->selected) ? theme->accent_alt : theme->window_bg;

    /* Selection highlight */
    for (uint32_t x = 0; x < s->width; x++) {
      uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + (uint32_t)y * s->pitch);
      line[x] = bg;
    }

    /* Icon: [D] for dir, [F] for file */
    const char *icon = (e->mode & 0x2) ? "[D] " : "[F] ";
    font_draw_string(s, f, 8, y, icon, theme->text_muted);
    font_draw_string(s, f, 40, y, e->name, color);
    y += (int32_t)f->glyph_height + 2;
  }
}

void file_manager_handle_click(struct file_manager_app *app, int32_t x, int32_t y) {
  if (!app) return;
  (void)x;
  if (y < 24) return;
  int idx = app->scroll_offset + (y - 24) / 18;
  if (idx >= 0 && idx < app->entry_count) {
    app->selected = idx;
    struct fm_entry *e = &app->entries[idx];
    if (e->mode & 0x2) {
      /* Navigate into directory */
      char new_path[FM_PATH_MAX];
      size_t plen = fm_strlen(app->current_path);
      fm_strcpy(new_path, app->current_path, FM_PATH_MAX);
      if (plen > 1) { new_path[plen] = '/'; new_path[plen+1] = '\0'; plen++; }
      fm_strcpy(new_path + plen, e->name, FM_PATH_MAX - plen);
      file_manager_navigate(app, new_path);
    }
  }
}
