#include "apps/file_manager.h"
#include "apps/text_editor.h"
#include "gui/compositor.h"
#include "gui/font.h"
#include "gui/widget.h"
#include "gui/context_menu.h"
#include "gui/desktop_icons.h"
#include "gui/inline_prompt.h"
#include "gui/desktop.h"
#include "fs/vfs.h"
#include "auth/session.h"
#include "lang/app_language.h"
#include "util/kstring.h"
#include "memory/kmem.h"
#include <stddef.h>

static struct file_manager_app g_fm;
static int g_fm_open = 0;
static char g_fm_delete_path[FM_PATH_MAX];
static uint16_t g_fm_delete_mode;

static int fm_iter_cb(const char *name, uint16_t mode, void *ctx);
static void fm_join_path(const char *dir, const char *name, char *out,
                         size_t out_len);
static void file_manager_load_path(struct file_manager_app *app,
                                   const char *path, int preserve_status);

#define FM_TOOLBAR_H 34

/* Etapa UX W7-ish (2026-05-03): forward decl. Definicao perto do
 * fim do arquivo (junto com fm_ctx_pick). */
static void file_manager_window_context_menu(struct gui_window *win,
                                             int32_t lx, int32_t ly);

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

static int32_t fm_row_height(const struct font *f) {
  return f ? (int32_t)f->glyph_height + 4 : 20;
}

static void fm_fit_text(const struct font *f, const char *src,
                        uint32_t max_width, char *out, size_t out_len) {
  size_t len = 0;
  size_t max_chars = 0;
  if (!out || out_len == 0) return;
  out[0] = '\0';
  if (!f || !src || max_width == 0 || f->glyph_width == 0) return;
  max_chars = max_width / f->glyph_width;
  if (max_chars == 0) return;
  while (src[len]) len++;
  if (len <= max_chars) {
    kstrcpy(out, out_len, src);
    return;
  }
  if (max_chars <= 3) {
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

static void fm_draw_fit(struct gui_surface *s, const struct font *f,
                        int32_t x, int32_t y, uint32_t max_width,
                        const char *text, uint32_t color) {
  char fitted[96];
  fm_fit_text(f, text, max_width, fitted, sizeof(fitted));
  if (fitted[0]) font_draw_string(s, f, x, y, fitted, color);
}

static void fm_fill_rect(struct gui_surface *s, int32_t x, int32_t y,
                         uint32_t w, uint32_t h, uint32_t color) {
  if (!s) return;
  for (uint32_t r = 0; r < h; r++) {
    int32_t py = y + (int32_t)r;
    if (py < 0 || (uint32_t)py >= s->height) continue;
    uint32_t *line = (uint32_t *)((uint8_t *)s->pixels +
                                  (uint32_t)py * s->pitch);
    for (uint32_t c = 0; c < w; c++) {
      int32_t px = x + (int32_t)c;
      if (px >= 0 && (uint32_t)px < s->width) line[px] = color;
    }
  }
}

static void fm_toolbar_layout(uint32_t width, int32_t *out_x,
                              int32_t *out_w, int32_t *out_gap) {
  int32_t button_w = 60;
  int32_t gap = 5;
  int32_t total = 0;
  if (width < 560u) button_w = 54;
  if (width < 420u) button_w = 48;
  if (width < 340u) button_w = 42;
  total = button_w * 6 + gap * 5;
  if (out_x) *out_x = (width > (uint32_t)total + 72u)
      ? (int32_t)width - total - 8 : 8;
  if (out_w) *out_w = button_w;
  if (out_gap) *out_gap = gap;
}

static void fm_paint_button(struct gui_surface *s, const struct font *f,
                            int32_t x, int32_t y, int32_t w,
                            const char *label, uint32_t bg, uint32_t fg,
                            uint32_t border) {
  if (!s || !f || w <= 0) return;
  fm_fill_rect(s, x, y, (uint32_t)w, 24, bg);
  fm_fill_rect(s, x, y, (uint32_t)w, 1, border);
  fm_fill_rect(s, x, y + 23, (uint32_t)w, 1, border);
  fm_fill_rect(s, x, y, 1, 24, border);
  fm_fill_rect(s, x + w - 1, y, 1, 24, border);
  if (w > 4) fm_fill_rect(s, x + 2, y + 2, (uint32_t)w - 4u, 1, 0x00FFFFFF);
  fm_draw_fit(s, f, x + 5, y + 5, (w > 10) ? (uint32_t)(w - 10) : 0u,
              label, fg);
}


static int fm_row_at(struct file_manager_app *app, int32_t x, int32_t y) {
  int32_t row_h = 0;
  int32_t status_y = 0;
  int idx = -1;
  (void)x;
  if (!app || !app->window) return -1;
  if (y < FM_TOOLBAR_H + 4) return -1;
  row_h = fm_row_height(font_default());
  if (row_h <= 0) return -1;
  status_y = (app->window->surface.height > 18u)
      ? (int32_t)app->window->surface.height - 18 : (int32_t)app->window->surface.height;
  if (y >= status_y) return -1;
  idx = app->scroll_offset + (y - (FM_TOOLBAR_H + 4)) / row_h;
  return (idx >= 0 && idx < app->entry_count) ? idx : -1;
}

static void fm_open_entry(struct file_manager_app *app, int idx) {
  if (!app || idx < 0 || idx >= app->entry_count) return;
  struct fm_entry *e = &app->entries[idx];
  if (e->mode & VFS_MODE_DIR) {
    char new_path[FM_PATH_MAX];
    size_t plen = kstrlen(app->current_path);
    kstrcpy(new_path, FM_PATH_MAX, app->current_path);
    if (plen > 1 && plen + 1 < FM_PATH_MAX) {
      new_path[plen] = '/';
      new_path[plen + 1] = '\0';
      plen++;
    }
    kstrcpy(new_path + plen, FM_PATH_MAX - plen, e->name);
    file_manager_navigate(app, new_path);
  } else {
    const char *name = e->name;
    size_t nlen = kstrlen(name);
    int is_text = 0;
    if (nlen >= 4 && name[nlen - 4] == '.' &&
        (name[nlen - 3] == 't' || name[nlen - 3] == 'T') &&
        (name[nlen - 2] == 'x' || name[nlen - 2] == 'X') &&
        (name[nlen - 1] == 't' || name[nlen - 1] == 'T')) {
      is_text = 1;
    }
    if (nlen >= 3 && name[nlen - 3] == '.' &&
        (name[nlen - 2] == 'm' || name[nlen - 2] == 'M') &&
        (name[nlen - 1] == 'd' || name[nlen - 1] == 'D')) {
      is_text = 1;
    }
    if (is_text) {
      char path[FM_PATH_MAX];
      fm_join_path(app->current_path, name, path, sizeof(path));
      text_editor_open(path);
    }
  }
}

static int fm_move_entry_to_dir(struct file_manager_app *app, int src_idx,
                                int dst_idx) {
  char src[FM_PATH_MAX];
  char dst_dir[FM_PATH_MAX];
  char dst[FM_PATH_MAX];
  int rc = 0;
  if (!app || src_idx < 0 || src_idx >= app->entry_count ||
      dst_idx < 0 || dst_idx >= app->entry_count || src_idx == dst_idx)
    return 0;
  if (!(app->entries[dst_idx].mode & VFS_MODE_DIR)) return 0;
  fm_join_path(app->current_path, app->entries[src_idx].name, src, sizeof(src));
  fm_join_path(app->current_path, app->entries[dst_idx].name, dst_dir, sizeof(dst_dir));
  fm_join_path(dst_dir, app->entries[src_idx].name, dst, sizeof(dst));
  rc = vfs_rename(src, dst);
  if (rc != 0) {
    fm_set_vfs_status(app,
                      APP_T("Falha ao mover", "Move failed",
                            "Error al mover"), rc);
  } else {
    fm_set_ok_status(app,
                     APP_T("Item movido", "Entry moved",
                           "Elemento movido"));
  }
  file_manager_load_path(app, app->current_path, 1);
  desktop_icons_refresh();
  return 1;
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
  app->drag_active = 0;
  app->drag_source = -1;
  app->drag_over = -1;
  app->drag_open_on_release = 0;
  app->drag_moved = 0;
  app->drag_start_x = 0;
  app->drag_start_y = 0;
  app->external_drag_over = -1;
  rc = vfs_listdir(path, fm_iter_cb, app);
  if (rc != 0) {
    app->entry_count = 0;
    fm_set_vfs_status(app,
                       APP_T("Falha ao abrir", "Open failed",
                             "Error al abrir"), rc);
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

static const char *fm_basename(const char *path) {
  const char *base = path;
  if (!path) return "";
  for (size_t i = 0; path[i]; i++) {
    if (path[i] == '/' && path[i + 1]) base = &path[i + 1];
  }
  return base ? base : "";
}

static int fm_path_equal(const char *a, const char *b) {
  return a && b && kstreq(a, b);
}

static int fm_path_inside_or_same(const char *dir, const char *path) {
  size_t plen = 0;
  if (!dir || !path) return 0;
  if (kstreq(dir, path)) return 1;
  plen = kstrlen(path);
  if (plen == 0) return 0;
  if (kstrlen(dir) <= plen) return 0;
  for (size_t i = 0; i < plen; i++) {
    if (dir[i] != path[i]) return 0;
  }
  return dir[plen] == '/';
}

static int fm_move_path_to_dir(struct file_manager_app *app,
                               const char *src_path, const char *dst_dir) {
  char dst_path[FM_PATH_MAX];
  const char *name = fm_basename(src_path);
  int rc = 0;
  if (!app || !src_path || !dst_dir || !name[0]) return 0;
  if (fm_path_inside_or_same(dst_dir, src_path)) {
    fm_set_vfs_status(app,
                      APP_T("Falha ao mover", "Move failed",
                            "Error al mover"),
                      -VFS_ERR_INVALID_PATH);
    if (app->window) compositor_invalidate(app->window->id);
    return 1;
  }
  fm_join_path(dst_dir, name, dst_path, sizeof(dst_path));
  if (fm_path_equal(src_path, dst_path)) {
    fm_set_ok_status(app, APP_T("Item ja esta aqui", "Entry already here",
                                "Elemento ya esta aqui"));
    if (app->window) compositor_invalidate(app->window->id);
    return 1;
  }
  rc = vfs_rename(src_path, dst_path);
  if (rc != 0) {
    fm_set_vfs_status(app,
                      APP_T("Falha ao mover", "Move failed",
                            "Error al mover"), rc);
  } else {
    fm_set_ok_status(app,
                     APP_T("Item movido", "Entry moved",
                           "Elemento movido"));
  }
  file_manager_load_path(app, app->current_path, 1);
  desktop_icons_refresh();
  return 1;
}

static int fm_delete_path_now(struct file_manager_app *app, const char *path,
                              uint16_t mode) {
  int rc = 0;
  if (!app || !path || !path[0]) return 0;
  if (mode & VFS_MODE_DIR) rc = vfs_rmdir_recursive(path);
  else rc = vfs_unlink(path);
  if (rc != 0) {
    fm_set_vfs_status(app,
                      APP_T("Falha ao apagar", "Delete failed",
                            "Error al borrar"), rc);
  } else {
    fm_set_ok_status(app,
                     APP_T("Item removido", "Entry removed",
                           "Elemento eliminado"));
  }
  file_manager_load_path(app, app->current_path, 1);
  desktop_icons_refresh();
  return 1;
}

static void fm_delete_confirm_submit(const char *text, void *ctx) {
  struct file_manager_app *app = (struct file_manager_app *)ctx;
  if (!app) return;
  if (!text || !kstreq(text, "DELETE")) {
    fm_set_status(app, APP_T("Exclusao cancelada", "Delete cancelled",
                             "Borrado cancelado"), 0x00F9E2AF);
    if (app->window) compositor_invalidate(app->window->id);
    return;
  }
  (void)fm_delete_path_now(app, g_fm_delete_path, g_fm_delete_mode);
  g_fm_delete_path[0] = '\0';
  g_fm_delete_mode = 0;
}

static void fm_request_delete_selected(struct file_manager_app *app) {
  struct fm_entry *e = NULL;
  char path[FM_PATH_MAX];
  int32_t px = 0;
  int32_t py = 0;
  if (!app || app->selected < 0 || app->selected >= app->entry_count) return;
  e = &app->entries[app->selected];
  fm_join_path(app->current_path, e->name, path, sizeof(path));
  if (!(e->mode & VFS_MODE_DIR)) {
    (void)fm_delete_path_now(app, path, e->mode);
    return;
  }
  kstrcpy(g_fm_delete_path, sizeof(g_fm_delete_path), path);
  g_fm_delete_mode = e->mode;
  if (app->window) {
    px = app->window->frame.x + 16;
    py = app->window->frame.y + 56;
  }
  (void)inline_prompt_show(APP_T("Digite DELETE", "Type DELETE",
                                 "Escriba DELETE"),
                           "", px, py, fm_delete_confirm_submit, app);
}

static int fm_drop_target_dir_at(struct file_manager_app *app, int32_t screen_x,
                                 int32_t screen_y, char *out,
                                 size_t out_len, int *out_row) {
  int32_t lx = 0;
  int32_t ly = 0;
  int row = -1;
  if (out_row) *out_row = -1;
  if (!app || !app->window || !out || out_len == 0) return 0;
  if (screen_x < app->window->frame.x || screen_y < app->window->frame.y ||
      screen_x >= app->window->frame.x + (int32_t)app->window->frame.width ||
      screen_y >= app->window->frame.y + (int32_t)app->window->frame.height)
    return 0;
  lx = screen_x - app->window->frame.x;
  ly = screen_y - app->window->frame.y;
  row = fm_row_at(app, lx, ly);
  if (row >= 0 && row < app->entry_count &&
      (app->entries[row].mode & VFS_MODE_DIR)) {
    fm_join_path(app->current_path, app->entries[row].name, out, out_len);
    if (out_row) *out_row = row;
    return 1;
  }
  kstrcpy(out, out_len, app->current_path);
  return 1;
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

/* 2026-05-02: repaint after a user resize drag (see
 * src/apps/calculator.c for the rationale). */
static void file_manager_window_resize(struct gui_window *win,
                                       uint32_t w, uint32_t h) {
  (void)w;
  (void)h;
  if (!win || !win->user_data) return;
  file_manager_paint((struct file_manager_app *)win->user_data);
}

static void file_manager_window_mouse(struct gui_window *win, int32_t x, int32_t y,
                                      uint8_t buttons) {
  struct file_manager_app *app = NULL;
  if (!win || !win->user_data) return;
  app = (struct file_manager_app *)win->user_data;
  if (!(buttons & 1)) {
    if (app->drag_active) {
      (void)file_manager_handle_mouse_up(win, win->frame.x + x,
                                         win->frame.y + y);
    }
    return;
  }
  if (app->drag_active) {
    (void)file_manager_handle_drag_move(win, win->frame.x + x,
                                        win->frame.y + y, buttons);
    return;
  }
  file_manager_handle_click(app, x, y);
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
  if (!app || !path) return;
  if (app->current_path[0] && !kstreq(app->current_path, path)) {
    kstrcpy(app->previous_path, FM_PATH_MAX, app->current_path);
  }
  file_manager_load_path(app, path, 0);
}

static void file_manager_navigate_back(struct file_manager_app *app) {
  char target[FM_PATH_MAX];
  char current[FM_PATH_MAX];
  if (!app || !app->previous_path[0]) return;
  kstrcpy(target, sizeof(target), app->previous_path);
  kstrcpy(current, sizeof(current), app->current_path);
  file_manager_load_path(app, target, 0);
  kstrcpy(app->previous_path, FM_PATH_MAX, current);
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
  g_fm.window->capture_mouse = 1;
  g_fm.window->on_scroll = file_manager_window_scroll;
  g_fm.window->on_close = file_manager_on_close;
  g_fm.window->on_resize = file_manager_window_resize;
  /* Etapa UX W7-ish (2026-05-03): right-click context menu. */
  g_fm.window->on_context_menu = file_manager_window_context_menu;
  compositor_show_window(g_fm.window->id);
  compositor_focus_window(g_fm.window->id);
  g_fm_open = 1;
  file_manager_navigate(&g_fm, fm_initial_path());
}

/* Etapa UX W7-ish (2026-05-03): abre o file_manager e navega para
 * o `path` informado. Se ja estiver aberto, apenas re-aponta o path.
 * Util para o desktop icons abrir uma pasta diretamente. */
void file_manager_open_at(const char *path) {
  if (!path || !path[0]) {
    file_manager_open();
    return;
  }
  file_manager_open();
  if (g_fm_open) {
    file_manager_navigate(&g_fm, path);
  }
}

void file_manager_paint(struct file_manager_app *app) {
  if (!app || !app->window) return;
  struct gui_surface *s = &app->window->surface;
  const struct font *f = font_default();
  const struct gui_theme_palette *theme = compositor_theme();
  int32_t toolbar_x = 0, button_w = 0, button_gap = 0;
  int32_t row_h = 0;
  int32_t status_y = 0;
  if (!f) return;
  fm_toolbar_layout(s->width, &toolbar_x, &button_w, &button_gap);
  row_h = fm_row_height(f);

  /* Clear background */
  for (uint32_t y = 0; y < s->height; y++) {
    uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + y * s->pitch);
    for (uint32_t x = 0; x < s->width; x++) line[x] = theme->window_bg;
  }

  /* Toolbar */
  fm_fill_rect(s, 0, 0, s->width, FM_TOOLBAR_H - 1u, theme->taskbar_bg);
  font_draw_string(s, f, 8, 9, "<-", theme->accent_alt);
  fm_draw_fit(s, f, 28, 9,
              (toolbar_x > 36) ? (uint32_t)(toolbar_x - 36) : 0u,
              app->current_path, theme->accent);
  {
    uint32_t btn_bg = theme->accent_alt;
    fm_paint_button(s, f, toolbar_x, 4, button_w,
                    APP_T("Voltar", "Back", "Volver"),
                    app->previous_path[0] ? btn_bg : theme->window_border,
                    app->previous_path[0] ? theme->text : theme->text_muted,
                    theme->window_border);
    fm_paint_button(s, f, toolbar_x + button_w + button_gap, 4, button_w,
                    APP_T("Subir", "Up", "Subir"),
                    btn_bg, theme->text, theme->window_border);
    fm_paint_button(s, f, toolbar_x + (button_w + button_gap) * 2, 4,
                    button_w, APP_T("Arquivo", "File", "Archivo"),
                    btn_bg, theme->text, theme->window_border);
    fm_paint_button(s, f, toolbar_x + (button_w + button_gap) * 3, 4,
                    button_w, APP_T("Pasta", "Folder", "Carpeta"),
                    btn_bg, theme->text, theme->window_border);
    fm_paint_button(s, f, toolbar_x + (button_w + button_gap) * 4, 4,
                    button_w, APP_T("Atual", "Refresh", "Actual"),
                    btn_bg, theme->text, theme->window_border);
    fm_paint_button(s, f, toolbar_x + (button_w + button_gap) * 5, 4,
                    button_w, APP_T("Apagar", "Delete", "Borrar"),
                    (app->selected >= 0) ? 0x00CC3333 : theme->window_border,
                    (app->selected >= 0) ? 0x00FFFFFF : theme->text_muted,
                    theme->window_border);
  }

  /* Separator */
  if (s->height > FM_TOOLBAR_H) {
    for (uint32_t x = 0; x < s->width; x++) {
      uint32_t *line = (uint32_t *)((uint8_t *)s->pixels +
                                    (FM_TOOLBAR_H - 1) * s->pitch);
      line[x] = theme->window_border;
    }
  }

  /* File list */
  int32_t y = FM_TOOLBAR_H + 4;
  status_y = (s->height > f->glyph_height + 2u)
      ? (int32_t)s->height - (int32_t)f->glyph_height - 2
      : (int32_t)s->height;
  for (int i = app->scroll_offset;
       i < app->entry_count && y + row_h <= status_y; i++) {
    struct fm_entry *e = &app->entries[i];
    uint32_t color = (e->mode & 0x2) ? theme->accent : theme->text;
    uint32_t bg = (i == app->selected) ? theme->accent_alt :
                  ((i == app->drag_over || i == app->external_drag_over)
                       ? theme->taskbar_highlight : theme->window_bg);

    /* Selection highlight */
    fm_fill_rect(s, 0, y, s->width, (uint32_t)row_h, bg);

    /* Icon: [D] for dir, [F] for file */
    const char *icon = (e->mode & 0x2) ? "[D] " : "[F] ";
    font_draw_string(s, f, 8, y + 2, icon, theme->text_muted);
    fm_draw_fit(s, f, 40, y + 2,
                (s->width > 48u) ? s->width - 48u : 0u, e->name, color);
    y += row_h;
  }
  if (s->height > 18) {
    fm_draw_fit(s, f, 8, status_y,
                (s->width > 16u) ? s->width - 16u : 0u,
                app->status_text,
                app->status_text[0] ? app->status_color : theme->text_muted);
  }
}

void file_manager_handle_click(struct file_manager_app *app, int32_t x, int32_t y) {
  int rc = 0;
  int32_t toolbar_x = 0, button_w = 0, button_gap = 0;
  if (!app) return;
  if (app->window) {
    fm_toolbar_layout(app->window->surface.width, &toolbar_x,
                      &button_w, &button_gap);
  }
  if (y < FM_TOOLBAR_H && app->window) {
    if (x >= toolbar_x && x < toolbar_x + button_w) {
      file_manager_navigate_back(app);
      return;
    }
    if (x >= toolbar_x + button_w + button_gap &&
        x < toolbar_x + button_w * 2 + button_gap) {
      file_manager_navigate_up(app);
      return;
    }
    if (x >= toolbar_x + (button_w + button_gap) * 2 &&
        x < toolbar_x + button_w * 3 + button_gap * 2) {
      char path[FM_PATH_MAX];
      if (fm_unique_path(app, "new_file", ".txt", path, sizeof(path)) != 0) {
        fm_set_status(app,
                      APP_T("Criar arquivo: nomes esgotados",
                            "Create file: name exhausted",
                            "Crear archivo: nombres agotados"),
                      0x00F38BA8);
        if (app->window) compositor_invalidate(app->window->id);
        return;
      }
      rc = vfs_create(path, VFS_MODE_FILE, NULL);
      if (rc != 0) {
        fm_set_vfs_status(app,
                          APP_T("Falha ao criar arquivo",
                                "Create file failed",
                                "Error al crear archivo"), rc);
      } else {
        fm_set_ok_status(app,
                         APP_T("Arquivo criado", "File created",
                               "Archivo creado"));
      }
      file_manager_load_path(app, app->current_path, 1);
      return;
    }
    if (x >= toolbar_x + (button_w + button_gap) * 3 &&
        x < toolbar_x + button_w * 4 + button_gap * 3) {
      char path[FM_PATH_MAX];
      if (fm_unique_path(app, "new_folder", NULL, path, sizeof(path)) != 0) {
        fm_set_status(app,
                      APP_T("Criar pasta: nomes esgotados",
                            "Create dir: name exhausted",
                            "Crear carpeta: nombres agotados"),
                      0x00F38BA8);
        if (app->window) compositor_invalidate(app->window->id);
        return;
      }
      rc = vfs_create(path, VFS_MODE_DIR, NULL);
      if (rc != 0) {
        fm_set_vfs_status(app,
                          APP_T("Falha ao criar pasta",
                                "Create dir failed",
                                "Error al crear carpeta"), rc);
      } else {
        fm_set_ok_status(app,
                         APP_T("Pasta criada", "Directory created",
                               "Carpeta creada"));
      }
      file_manager_load_path(app, app->current_path, 1);
      return;
    }
    if (x >= toolbar_x + (button_w + button_gap) * 4 &&
        x < toolbar_x + button_w * 5 + button_gap * 4) {
      file_manager_load_path(app, app->current_path, 1);
      return;
    }
    if (x >= toolbar_x + (button_w + button_gap) * 5 &&
        x < toolbar_x + button_w * 6 + button_gap * 5 &&
        app->selected >= 0 &&
        app->selected < app->entry_count) {
      fm_request_delete_selected(app);
      return;
    }
  }
  if (y < FM_TOOLBAR_H + 4) return;
  int idx = fm_row_at(app, x, y);
  if (idx >= 0) {
    app->drag_active = 1;
    app->drag_source = idx;
    app->drag_over = -1;
    app->drag_open_on_release = (app->selected == idx) ? 1 : 0;
    app->drag_moved = 0;
    app->drag_start_x = app->window ? app->window->frame.x + x : x;
    app->drag_start_y = app->window ? app->window->frame.y + y : y;
    app->selected = idx;
    if (app->window) compositor_invalidate(app->window->id);
  }
}

int file_manager_handle_drag_move(struct gui_window *win, int32_t screen_x,
                                  int32_t screen_y, uint8_t buttons) {
  struct file_manager_app *app = &g_fm;
  int32_t lx = 0;
  int32_t ly = 0;
  int next_over = -1;
  int inside = 0;
  (void)win;
  if (!g_fm_open || !app->window || !app->drag_active) return 0;
  if (!(buttons & 1)) return file_manager_handle_mouse_up(app->window, screen_x, screen_y);
  if (screen_x < app->drag_start_x - 2 || screen_x > app->drag_start_x + 2 ||
      screen_y < app->drag_start_y - 2 || screen_y > app->drag_start_y + 2) {
    app->drag_moved = 1;
  }
  lx = screen_x - app->window->frame.x;
  ly = screen_y - app->window->frame.y;
  inside = screen_x >= app->window->frame.x && screen_y >= app->window->frame.y &&
           screen_x < app->window->frame.x + (int32_t)app->window->frame.width &&
           screen_y < app->window->frame.y + (int32_t)app->window->frame.height;
  if (inside) {
    int row = fm_row_at(app, lx, ly);
    if (row >= 0 && row != app->drag_source &&
        (app->entries[row].mode & VFS_MODE_DIR)) {
      next_over = row;
    }
  }
  if (!inside && app->drag_source >= 0 && app->drag_source < app->entry_count) {
    (void)desktop_icons_preview_external_drop(screen_x, screen_y);
  } else {
    desktop_icons_clear_external_drop();
  }
  if (next_over != app->drag_over) {
    app->drag_over = next_over;
    compositor_invalidate(app->window->id);
  }
  return 1;
}

int file_manager_handle_mouse_up(struct gui_window *win, int32_t screen_x,
                                 int32_t screen_y) {
  struct file_manager_app *app = &g_fm;
  int src = 0;
  int dst = 0;
  int open_on_release = 0;
  int moved = 0;
  char src_path[FM_PATH_MAX];
  (void)win;
  src_path[0] = '\0';
  if (!g_fm_open || !app->window || !app->drag_active) return 0;
  src = app->drag_source;
  dst = app->drag_over;
  open_on_release = app->drag_open_on_release;
  moved = app->drag_moved;
  if (src >= 0 && src < app->entry_count) {
    fm_join_path(app->current_path, app->entries[src].name, src_path,
                 sizeof(src_path));
  }
  app->drag_active = 0;
  app->drag_source = -1;
  app->drag_over = -1;
  app->drag_open_on_release = 0;
  app->drag_moved = 0;
  app->drag_start_x = 0;
  app->drag_start_y = 0;
  desktop_icons_clear_external_drop();
  if (dst >= 0 && fm_move_entry_to_dir(app, src, dst)) return 1;
  if (moved && src_path[0] &&
      desktop_icons_drop_path_at(screen_x, screen_y, src_path)) {
    file_manager_load_path(app, app->current_path, 1);
    return 1;
  }
  if (open_on_release && !moved) fm_open_entry(app, src);
  if (app->window) compositor_invalidate(app->window->id);
  return 1;
}

int file_manager_preview_drop_path_at(int32_t screen_x, int32_t screen_y,
                                      const char *src_path) {
  struct file_manager_app *app = &g_fm;
  char dst_dir[FM_PATH_MAX];
  int row = -1;
  int next_over = -1;
  struct gui_window *top = NULL;
  if (!g_fm_open || !app->window || !src_path || !src_path[0]) return 0;
  top = compositor_window_at(screen_x, screen_y);
  if (top != app->window ||
      !fm_drop_target_dir_at(app, screen_x, screen_y, dst_dir,
                             sizeof(dst_dir), &row)) {
    file_manager_clear_external_drop();
    return 0;
  }
  if (row >= 0 && !fm_path_inside_or_same(dst_dir, src_path)) {
    next_over = row;
  }
  if (next_over != app->external_drag_over) {
    app->external_drag_over = next_over;
    compositor_invalidate(app->window->id);
  }
  return 1;
}

int file_manager_drop_path_at(int32_t screen_x, int32_t screen_y,
                              const char *src_path) {
  struct file_manager_app *app = &g_fm;
  char dst_dir[FM_PATH_MAX];
  int row = -1;
  struct gui_window *top = NULL;
  if (!g_fm_open || !app->window || !src_path || !src_path[0]) return 0;
  top = compositor_window_at(screen_x, screen_y);
  if (top != app->window ||
      !fm_drop_target_dir_at(app, screen_x, screen_y, dst_dir,
                             sizeof(dst_dir), &row)) {
    file_manager_clear_external_drop();
    return 0;
  }
  (void)row;
  file_manager_clear_external_drop();
  return fm_move_path_to_dir(app, src_path, dst_dir);
}

void file_manager_clear_external_drop(void) {
  if (g_fm_open && g_fm.window && g_fm.external_drag_over != -1) {
    g_fm.external_drag_over = -1;
    compositor_invalidate(g_fm.window->id);
  }
}

/* Etapa UX W7-ish (2026-05-03): action_ids reservados para
 * context_menu. Numeracao livre dentro do file_manager (o caller
 * passa um destes em items[].action_id e recebe de volta no
 * on_pick). */
#define FM_CTX_OPEN     1u
#define FM_CTX_DELETE   2u
#define FM_CTX_REFRESH  3u
#define FM_CTX_NEW_FILE 4u
#define FM_CTX_NEW_DIR  5u
#define FM_CTX_NAV_UP   6u
#define FM_CTX_RENAME   7u
#define FM_CTX_BACK     8u
#define FM_CTX_TERM     9u

/* Etapa UX W7-ish (2026-05-03): callback do inline_prompt apos o
 * usuario digitar o novo nome no rename. Renomeia src->dst no VFS
 * e refaz a listagem. ctx == app. */
static void fm_rename_submit(const char *new_name, void *ctx) {
  struct file_manager_app *app = (struct file_manager_app *)ctx;
  if (!app || !new_name || new_name[0] == '\0') return;
  if (app->selected < 0 || app->selected >= app->entry_count) return;
  struct fm_entry *e = &app->entries[app->selected];
  char src[FM_PATH_MAX];
  char dst[FM_PATH_MAX];
  fm_join_path(app->current_path, e->name, src, sizeof(src));
  fm_join_path(app->current_path, new_name, dst, sizeof(dst));
  int rc = vfs_rename(src, dst);
  if (rc != 0)
    fm_set_vfs_status(app, APP_T("Falha ao renomear", "Rename failed",
                                  "Error al renombrar"), rc);
  else
    fm_set_ok_status(app, APP_T("Renomeado", "Renamed", "Renombrado"));
  file_manager_load_path(app, app->current_path, 1);
}

static void fm_ctx_pick(uint16_t action_id, void *ctx) {
  struct file_manager_app *app = (struct file_manager_app *)ctx;
  if (!app) return;
  switch (action_id) {
    case FM_CTX_OPEN: {
      if (app->selected < 0 || app->selected >= app->entry_count) return;
      struct fm_entry *e = &app->entries[app->selected];
      char path[FM_PATH_MAX];
      fm_join_path(app->current_path, e->name, path, sizeof(path));
      if (e->mode & VFS_MODE_DIR) {
        file_manager_navigate(app, path);
      } else {
        text_editor_open(path);
      }
      break;
    }
    case FM_CTX_DELETE: {
      if (app->selected < 0 || app->selected >= app->entry_count) return;
      fm_request_delete_selected(app);
      break;
    }
    case FM_CTX_REFRESH:
      file_manager_load_path(app, app->current_path, 1);
      break;
    case FM_CTX_NEW_FILE: {
      char path[FM_PATH_MAX];
      int rc = 0;
      if (fm_unique_path(app, "new_file", ".txt", path, sizeof(path)) != 0) {
        fm_set_status(app,
                      APP_T("Criar arquivo: nomes esgotados",
                            "Create file: name exhausted",
                            "Crear archivo: nombres agotados"),
                      0x00F38BA8);
        if (app->window) compositor_invalidate(app->window->id);
        return;
      }
      rc = vfs_create(path, VFS_MODE_FILE, NULL);
      if (rc != 0)
        fm_set_vfs_status(app,
                          APP_T("Falha ao criar arquivo",
                                "Create file failed",
                                "Error al crear archivo"), rc);
      else
        fm_set_ok_status(app,
                         APP_T("Arquivo criado", "File created",
                               "Archivo creado"));
      file_manager_load_path(app, app->current_path, 1);
      break;
    }
    case FM_CTX_NEW_DIR: {
      char path[FM_PATH_MAX];
      int rc = 0;
      if (fm_unique_path(app, "new_folder", NULL, path, sizeof(path)) != 0) {
        fm_set_status(app,
                      APP_T("Criar pasta: nomes esgotados",
                            "Create dir: name exhausted",
                            "Crear carpeta: nombres agotados"),
                      0x00F38BA8);
        if (app->window) compositor_invalidate(app->window->id);
        return;
      }
      rc = vfs_create(path, VFS_MODE_DIR, NULL);
      if (rc != 0)
        fm_set_vfs_status(app,
                          APP_T("Falha ao criar pasta",
                                "Create dir failed",
                                "Error al crear carpeta"), rc);
      else
        fm_set_ok_status(app,
                         APP_T("Pasta criada", "Directory created",
                               "Carpeta creada"));
      file_manager_load_path(app, app->current_path, 1);
      break;
    }
    case FM_CTX_NAV_UP:
      file_manager_navigate_up(app);
      break;
    case FM_CTX_BACK:
      file_manager_navigate_back(app);
      break;
    case FM_CTX_TERM:
      desktop_open_terminal_here(app->current_path);
      break;
    case FM_CTX_RENAME: {
      if (app->selected < 0 || app->selected >= app->entry_count) return;
      struct fm_entry *e = &app->entries[app->selected];
      /* Posiciona o prompt sob a linha selecionada. */
      int32_t px = app->window ? app->window->frame.x : 0;
      int32_t py = app->window ? app->window->frame.y : 0;
      int32_t row_h = fm_row_height(font_default());
      int32_t row_y = FM_TOOLBAR_H + 4 +
                      (app->selected - app->scroll_offset) * row_h + row_h;
      inline_prompt_show("Rename:", e->name,
                         px + 8, py + row_y,
                         fm_rename_submit, app);
      break;
    }
    default: break;
  }
}

/* Etapa UX W7-ish (2026-05-03): right-click handler. Mostra menu
 * de contexto na posicao do cursor. Items dependem do alvo:
 *   - Sobre uma linha de arquivo/dir: Open / Delete / Refresh / New File / New Folder
 *   - Sobre area vazia (toolbar/bg): Refresh / New File / New Folder / Up
 */
static void file_manager_window_context_menu(struct gui_window *win,
                                             int32_t lx, int32_t ly) {
  if (!win || !win->user_data) return;
  struct file_manager_app *app = (struct file_manager_app *)win->user_data;

  /* Atualiza selected se o click foi sobre uma linha. */
  int target_row = -1;
  if (ly >= FM_TOOLBAR_H + 4) {
    int row_h = fm_row_height(font_default());
    int idx = app->scroll_offset + (ly - (FM_TOOLBAR_H + 4)) / row_h;
    if (idx >= 0 && idx < app->entry_count) {
      app->selected = idx;
      target_row = idx;
      if (app->window) compositor_invalidate(app->window->id);
    }
  }

  struct context_menu_item items[CONTEXT_MENU_MAX_ITEMS];
  uint32_t n = 0;
  for (uint32_t i = 0; i < CONTEXT_MENU_MAX_ITEMS; ++i) {
    items[i].label[0] = '\0';
    items[i].action_id = 0;
    items[i].enabled = 1;
    items[i].reserved = 0;
  }

  if (target_row >= 0) {
    kstrcpy(items[n].label, CONTEXT_MENU_LABEL_MAX,
            APP_T("Abrir", "Open", "Abrir"));
    items[n++].action_id = FM_CTX_OPEN;
    kstrcpy(items[n].label, CONTEXT_MENU_LABEL_MAX,
            APP_T("Renomear", "Rename", "Renombrar"));
    items[n++].action_id = FM_CTX_RENAME;
    kstrcpy(items[n].label, CONTEXT_MENU_LABEL_MAX,
            APP_T("Apagar", "Delete", "Borrar"));
    items[n++].action_id = FM_CTX_DELETE;
    items[n++].label[0] = '\0';
  }
  kstrcpy(items[n].label, CONTEXT_MENU_LABEL_MAX,
          APP_T("Abrir terminal aqui", "Open terminal here",
                "Abrir terminal aqui"));
  items[n++].action_id = FM_CTX_TERM;
  kstrcpy(items[n].label, CONTEXT_MENU_LABEL_MAX,
          APP_T("Novo arquivo", "New File", "Nuevo archivo"));
  items[n++].action_id = FM_CTX_NEW_FILE;
  kstrcpy(items[n].label, CONTEXT_MENU_LABEL_MAX,
          APP_T("Nova pasta", "New Folder", "Nueva carpeta"));
  items[n++].action_id = FM_CTX_NEW_DIR;
  if (target_row < 0) {
    items[n++].label[0] = '\0';
    kstrcpy(items[n].label, CONTEXT_MENU_LABEL_MAX,
            APP_T("Voltar", "Back", "Volver"));
    items[n].action_id = FM_CTX_BACK;
    items[n].enabled = app->previous_path[0] ? 1 : 0;
    n++;
    kstrcpy(items[n].label, CONTEXT_MENU_LABEL_MAX,
            APP_T("Subir", "Up", "Subir"));
    items[n++].action_id = FM_CTX_NAV_UP;
  }
  kstrcpy(items[n].label, CONTEXT_MENU_LABEL_MAX,
          APP_T("Atualizar", "Refresh", "Actualizar"));
  items[n++].action_id = FM_CTX_REFRESH;

  int32_t sx = win->frame.x + lx;
  int32_t sy = win->frame.y + ly;
  context_menu_show(items, n, sx, sy, fm_ctx_pick, app);
}
