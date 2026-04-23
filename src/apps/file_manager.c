#include "apps/file_manager.h"
#include "gui/compositor.h"
#include "gui/font.h"
#include "gui/widget.h"
#include "fs/vfs.h"
#include "auth/session.h"
#include "util/kstring.h"
#include "memory/kmem.h"
#include <stddef.h>

static struct file_manager_app g_fm;
static int g_fm_open = 0;

static int fm_iter_cb(const char *name, uint16_t mode, void *ctx);

static void fm_set_status(struct file_manager_app *app, const char *text, uint32_t color) {
  if (!app) return;
  kstrcpy(app->status_text, sizeof(app->status_text), text ? text : "");
  app->status_color = color;
}

static void fm_set_vfs_status(struct file_manager_app *app, const char *prefix, int rc) {
  char buf[96];
  if (!app) return;
  buf[0] = '\0';
  if (prefix && prefix[0]) {
    kstrcpy(buf, sizeof(buf), prefix);
    kbuf_append(buf, sizeof(buf), ": ");
  }
  kbuf_append(buf, sizeof(buf), vfs_error_string(rc));
  fm_set_status(app, buf, 0x00F38BA8);
}

static void fm_set_ok_status(struct file_manager_app *app, const char *text) {
  fm_set_status(app, text, 0x00A6E3A1);
}

static void file_manager_load_path(struct file_manager_app *app, const char *path,
                                   int preserve_status) {
  int rc = 0;
  char saved_status[96];
  uint32_t saved_color = 0;
  if (!app || !path) return;
  saved_status[0] = '\0';
  if (preserve_status && app->status_text[0]) {
    kstrcpy(saved_status, sizeof(saved_status), app->status_text);
    saved_color = app->status_color;
  }
  kstrcpy(app->current_path, FM_PATH_MAX, path);
  app->entry_count = 0;
  app->selected = -1;
  app->scroll_offset = 0;
  rc = vfs_listdir(path, fm_iter_cb, app);
  if (rc != 0) {
    app->entry_count = 0;
    fm_set_vfs_status(app, "Open failed", rc);
  } else if (saved_status[0]) {
    fm_set_status(app, saved_status, saved_color);
  } else {
    fm_set_ok_status(app, path);
  }
  if (app->window) compositor_invalidate(app->window->id);
}

static void fm_join_path(const char *dir, const char *name, char *out, size_t out_len) {
  size_t len = 0;
  if (!out || out_len == 0) return;
  kstrcpy(out, out_len, dir ? dir : "/");
  len = kstrlen(out);
  if (len > 1 && len + 1 < out_len) {
    out[len++] = '/';
    out[len] = '\0';
  }
  kbuf_append(out, out_len, name ? name : "");
}

static int fm_path_exists(const char *path) {
  struct dentry *d = NULL;
  if (vfs_lookup(path, &d) != 0) return 0;
  if (d && d->refcount) d->refcount--;
  return 1;
}

static int fm_unique_path(struct file_manager_app *app, const char *base,
                          const char *suffix, char *out, size_t out_len) {
  char name[64];
  uint32_t attempt = 0;
  if (!app || !base || !out || out_len == 0) return -1;
  while (attempt < 1024) {
    name[0] = '\0';
    kstrcpy(name, sizeof(name), base);
    if (attempt > 0) {
      kbuf_append(name, sizeof(name), "_");
      kbuf_append_u32(name, sizeof(name), attempt);
    }
    if (suffix) kbuf_append(name, sizeof(name), suffix);
    fm_join_path(app->current_path, name, out, out_len);
    if (!fm_path_exists(out)) return 0;
    attempt++;
  }
  return -1;
}

static const char *fm_initial_path(void) {
  struct session_context *sess = session_active();
  const struct user_record *user = sess ? session_user(sess) : NULL;
  if (user && user->home[0]) return user->home;
  return "/";
}

static void file_manager_cleanup(void) {
  g_fm.window = NULL;
  g_fm_open = 0;
}

static void file_manager_on_close(struct gui_window *win) {
  (void)win;
  file_manager_cleanup();
}

static int fm_iter_cb(const char *name, uint16_t mode, void *ctx) {
  struct file_manager_app *app = (struct file_manager_app *)ctx;
  if (app->entry_count >= FM_MAX_ENTRIES) return -1;
  struct fm_entry *e = &app->entries[app->entry_count++];
  kstrcpy(e->name, 64, name);
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
  if (delta > 0 && app->scroll_offset > 0) {
    int step = (int)delta;
    app->scroll_offset = (app->scroll_offset > step) ? (app->scroll_offset - step) : 0;
  } else if (delta < 0) {
    app->scroll_offset += (int)(-delta);
  }
  if (app->scroll_offset >= app->entry_count && app->entry_count > 0)
    app->scroll_offset = app->entry_count - 1;
  if (app->scroll_offset < 0)
    app->scroll_offset = 0;
}

void file_manager_navigate(struct file_manager_app *app, const char *path) {
  file_manager_load_path(app, path, 0);
}

static void file_manager_navigate_up(struct file_manager_app *app) {
  if (!app) return;
  /* Find last '/' in current_path and truncate */
  size_t len = kstrlen(app->current_path);
  if (len <= 1) return; /* already at root */
  /* Skip trailing '/' */
  if (len > 1 && app->current_path[len - 1] == '/') len--;
  /* Find previous '/' */
  while (len > 0 && app->current_path[len - 1] != '/') len--;
  if (len == 0) len = 1; /* keep root '/' */
  char parent[FM_PATH_MAX];
  for (size_t i = 0; i < len; i++) parent[i] = app->current_path[i];
  parent[len] = '\0';
  file_manager_navigate(app, parent);
}

void file_manager_open(void) {
  const struct gui_theme_palette *theme = compositor_theme();
  uint8_t scale = compositor_ui_scale();

  /* If already open, just focus the existing window */
  if (g_fm_open && g_fm.window) {
    compositor_show_window(g_fm.window->id);
    compositor_focus_window(g_fm.window->id);
    return;
  }

  /* Clean up stale state */
  file_manager_cleanup();
  kmemzero(&g_fm, sizeof(g_fm));

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
  g_fm.window->on_close = file_manager_on_close;
  compositor_show_window(g_fm.window->id);
  compositor_focus_window(g_fm.window->id);
  g_fm_open = 1;
  file_manager_navigate(&g_fm, fm_initial_path());
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

  /* Toolbar: Back, New Folder, Delete */
  font_draw_string(s, f, 8, 4, "<-", theme->accent_alt);
  font_draw_string(s, f, 28, 4, app->current_path, theme->accent);
  {
    int32_t bx = (int32_t)(s->width - 232);
    uint32_t btn_bg = theme->accent_alt;
    for (uint32_t by = 0; by < 16; by++) {
      uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + (2 + by) * s->pitch);
      for (uint32_t rx = 0; rx < 68; rx++) if (bx + (int32_t)rx >= 0) line[bx + rx] = btn_bg;
      for (uint32_t rx = 76; rx < 144; rx++) if (bx + (int32_t)rx >= 0) line[bx + rx] = btn_bg;
      for (uint32_t rx = 152; rx < 220; rx++) if (bx + (int32_t)rx >= 0) line[bx + rx] = (app->selected >= 0) ? 0x00CC3333 : btn_bg;
    }
    font_draw_string(s, f, bx + 4, 4, "New File", theme->text);
    font_draw_string(s, f, bx + 80, 4, "New Dir", theme->text);
    font_draw_string(s, f, bx + 156, 4, "Delete", (app->selected >= 0) ? 0x00FFFFFF : theme->text_muted);
  }

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
  if (s->height > 18) {
    int32_t status_y = (int32_t)s->height - (int32_t)f->glyph_height - 2;
    font_draw_string(s, f, 8, status_y, app->status_text,
                     app->status_text[0] ? app->status_color : theme->text_muted);
  }
}

void file_manager_handle_click(struct file_manager_app *app, int32_t x, int32_t y) {
  int rc = 0;
  if (!app) return;
  /* Back button in path bar area */
  if (y < 20 && x < 24) {
    file_manager_navigate_up(app);
    return;
  }
  /* Toolbar buttons (top-right) */
  if (y < 20 && app->window) {
    int32_t bx = (int32_t)(app->window->frame.width - 232);
    if (x >= bx && x < bx + 68) {
      char path[FM_PATH_MAX];
      if (fm_unique_path(app, "new_file", ".txt", path, sizeof(path)) != 0) {
        fm_set_status(app, "Create file: name exhausted", 0x00F38BA8);
        if (app->window) compositor_invalidate(app->window->id);
        return;
      }
      rc = vfs_create(path, VFS_MODE_FILE, NULL);
      if (rc != 0) {
        fm_set_vfs_status(app, "Create file failed", rc);
      } else {
        fm_set_ok_status(app, "File created");
      }
      file_manager_load_path(app, app->current_path, 1);
      return;
    }
    if (x >= bx + 76 && x < bx + 144) {
      char path[FM_PATH_MAX];
      if (fm_unique_path(app, "new_folder", NULL, path, sizeof(path)) != 0) {
        fm_set_status(app, "Create dir: name exhausted", 0x00F38BA8);
        if (app->window) compositor_invalidate(app->window->id);
        return;
      }
      rc = vfs_create(path, VFS_MODE_DIR, NULL);
      if (rc != 0) {
        fm_set_vfs_status(app, "Create dir failed", rc);
      } else {
        fm_set_ok_status(app, "Directory created");
      }
      file_manager_load_path(app, app->current_path, 1);
      return;
    }
    if (x >= bx + 152 && x < bx + 220 && app->selected >= 0 &&
        app->selected < app->entry_count) {
      struct fm_entry *e = &app->entries[app->selected];
      char path[FM_PATH_MAX];
      fm_join_path(app->current_path, e->name, path, sizeof(path));
      if (e->mode & VFS_MODE_DIR) rc = vfs_rmdir(path);
      else rc = vfs_unlink(path);
      if (rc != 0) {
        fm_set_vfs_status(app, "Delete failed", rc);
      } else {
        fm_set_ok_status(app, "Entry removed");
      }
      file_manager_load_path(app, app->current_path, 1);
      return;
    }
  }
  if (y < 24) return;
  int idx = app->scroll_offset + (y - 24) / 18;
  if (idx >= 0 && idx < app->entry_count) {
    app->selected = idx;
    struct fm_entry *e = &app->entries[idx];
    if (e->mode & 0x2) {
      /* Navigate into directory */
      char new_path[FM_PATH_MAX];
      size_t plen = kstrlen(app->current_path);
      kstrcpy(new_path, FM_PATH_MAX, app->current_path);
      if (plen > 1) { new_path[plen] = '/'; new_path[plen+1] = '\0'; plen++; }
      kstrcpy(new_path + plen, FM_PATH_MAX - plen, e->name);
      file_manager_navigate(app, new_path);
    }
  }
}
