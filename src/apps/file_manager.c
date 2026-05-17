/*
 * src/apps/file_manager.c
 *
 * Entry point and lifecycle for the file manager application. After
 * the 2026-05-15 refactor this file holds:
 *
 *   - the singleton app state and module-level globals
 *   - status helpers (fm_set_status/fm_set_vfs_status/fm_set_ok_status)
 *   - filesystem operation helpers (path math, move, delete, unique)
 *   - directory-load + navigation (load_path, navigate, back, up)
 *   - window lifecycle (open, open_at, cleanup, on_close)
 *   - window event dispatchers (paint/resize/mouse/scroll handlers)
 *   - the public `file_manager_handle_click` toolbar router
 *
 * Painting + drawing primitives live in `file_manager_view.c`.
 * Drag-and-drop + right-click context menu live in
 * `file_manager_dnd.c`. The split was driven by the 900-line layout
 * limit documented in `docs/architecture/source-layout.md`.
 */
#include "apps/file_manager.h"
#include "apps/text_editor.h"
#include "gui/compositor.h"
#include "gui/font.h"
#include "gui/widget.h"
#include "gui/inline_prompt.h"
#include "fs/vfs.h"
#include "auth/session.h"
#include "lang/app_language.h"
#include "util/kstring.h"
#include "memory/kmem.h"

#include "apps/internal/file_manager_internal.h"

#include <stddef.h>
#include <stdint.h>

struct file_manager_app g_fm;
int g_fm_open = 0;
char g_fm_delete_path[FM_PATH_MAX];
uint16_t g_fm_delete_mode;

/* ── forward decls for window callbacks defined below ────────────────── */

static int fm_iter_cb(const char *name, uint16_t mode, void *ctx);
static void file_manager_window_paint(struct gui_window *win);
static void file_manager_window_resize(struct gui_window *win, uint32_t w,
                                       uint32_t h);
static void file_manager_window_mouse(struct gui_window *win, int32_t x,
                                      int32_t y, uint8_t buttons);
static void file_manager_window_scroll(struct gui_window *win, int32_t delta);
static void file_manager_on_close(struct gui_window *win);
static void fm_delete_confirm_submit(const char *text, void *ctx);
static void fm_append_int(char *buf, size_t buf_len, int value);

/* ── status helpers ──────────────────────────────────────────────────── */

void fm_set_status(struct file_manager_app *app, const char *text, uint32_t color) {
  if (!app) return;
  kstrcpy(app->status_text, sizeof(app->status_text), text ? text : "");
  app->status_color = color;
}

static void fm_append_int(char *buf, size_t buf_len, int value) {
  uint32_t magnitude = 0;

  if (value < 0) {
    kbuf_append(buf, buf_len, "-");
    magnitude = (uint32_t)(-(value + 1)) + 1u;
  } else {
    magnitude = (uint32_t)value;
  }
  kbuf_append_u32(buf, buf_len, magnitude);
}

void fm_set_vfs_status(struct file_manager_app *app, const char *prefix, int rc) {
  char buf[96];
  if (!app) return;
  buf[0] = '\0';
  kstrcpy(buf, sizeof(buf), prefix ? prefix : "");
  if (rc != 0) {
    kbuf_append(buf, sizeof(buf), " (rc=");
    fm_append_int(buf, sizeof(buf), rc);
    kbuf_append(buf, sizeof(buf), ")");
  }
  fm_set_status(app, buf, 0x00F38BA8);
}

void fm_set_ok_status(struct file_manager_app *app, const char *text) {
  fm_set_status(app, text, 0x00A6E3A1);
}

/* ── filesystem operations ───────────────────────────────────────────── */

void fm_open_entry(struct file_manager_app *app, int idx) {
  if (!app || idx < 0 || idx >= app->entry_count) return;
  struct fm_entry *e = &app->entries[idx];
  if (e->mode & VFS_MODE_DIR) {
    /* Navigate into the directory */
    char path[FM_PATH_MAX];
    fm_join_path(app->current_path, e->name, path, sizeof(path));
    file_manager_navigate(app, path);
  } else {
    /* Open file in text_editor */
    char path[FM_PATH_MAX];
    fm_join_path(app->current_path, e->name, path, sizeof(path));
    text_editor_open(path);
    fm_set_ok_status(app, APP_T("Arquivo aberto no editor",
                                "File opened in editor",
                                "Archivo abierto en el editor"));
    if (app->window) compositor_invalidate(app->window->id);
  }
}

int fm_move_entry_to_dir(struct file_manager_app *app, int src_idx,
                         int dst_idx) {
  char src[FM_PATH_MAX];
  char dst_dir[FM_PATH_MAX];
  char dst_path[FM_PATH_MAX];
  if (!app) return 0;
  if (src_idx < 0 || src_idx >= app->entry_count) return 0;
  if (dst_idx < 0 || dst_idx >= app->entry_count) return 0;
  if (src_idx == dst_idx) return 0;
  if (!(app->entries[dst_idx].mode & VFS_MODE_DIR)) return 0;
  fm_join_path(app->current_path, app->entries[src_idx].name, src, sizeof(src));
  fm_join_path(app->current_path, app->entries[dst_idx].name, dst_dir,
               sizeof(dst_dir));
  fm_join_path(dst_dir, app->entries[src_idx].name, dst_path, sizeof(dst_path));
  int rc = vfs_rename(src, dst_path);
  if (rc != 0) {
    fm_set_vfs_status(app,
                      APP_T("Falha ao mover", "Move failed", "Error al mover"),
                      rc);
  } else {
    fm_set_ok_status(app,
                     APP_T("Movido", "Moved", "Movido"));
  }
  file_manager_load_path(app, app->current_path, 1);
  return 1;
}

void file_manager_load_path(struct file_manager_app *app, const char *path,
                            int preserve_status) {
  int rc = 0;
  char saved_status[96];
  uint32_t saved_color = 0;
  if (!app || !path) return;
  if (preserve_status) {
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
    fm_set_vfs_status(app,
                      APP_T("Falha ao listar", "List failed",
                            "Error al listar"), rc);
  } else if (!preserve_status) {
    app->status_text[0] = '\0';
    app->status_color = 0;
  } else {
    kstrcpy(app->status_text, sizeof(app->status_text), saved_status);
    app->status_color = saved_color;
  }
  if (app->window) compositor_invalidate(app->window->id);
}

void fm_join_path(const char *dir, const char *name, char *out, size_t out_len) {
  size_t len = 0;
  if (!out || out_len == 0) return;
  kstrcpy(out, out_len, dir ? dir : "/");
  len = kstrlen(out);
  if (len == 0 || out[len - 1] != '/') {
    kbuf_append(out, out_len, "/");
  }
  if (name) kbuf_append(out, out_len, name);
}

const char *fm_basename(const char *path) {
  if (!path) return "";
  const char *base = path;
  for (const char *p = path; *p; ++p) {
    if (*p == '/') base = p + 1;
  }
  return base ? base : "";
}

int fm_path_equal(const char *a, const char *b) {
  return a && b && kstreq(a, b);
}

int fm_path_inside_or_same(const char *dir, const char *path) {
  size_t plen = 0;
  if (!dir || !path) return 0;
  if (kstreq(dir, path)) return 1;
  plen = kstrlen(path);
  if (plen == 0) return 0;
  if (kstrlen(dir) <= plen) return 0;
  for (size_t i = 0; i < plen; ++i) {
    if (dir[i] != path[i]) return 0;
  }
  return dir[plen] == '/';
}

int fm_move_path_to_dir(struct file_manager_app *app, const char *src_path,
                        const char *dst_dir) {
  char dst_path[FM_PATH_MAX];
  const char *name = fm_basename(src_path);
  int rc = 0;
  if (!app || !src_path || !src_path[0] || !dst_dir || !dst_dir[0]) return 0;
  if (fm_path_inside_or_same(dst_dir, src_path)) return 0;
  fm_join_path(dst_dir, name, dst_path, sizeof(dst_path));
  if (fm_path_equal(src_path, dst_path)) return 0;
  rc = vfs_rename(src_path, dst_path);
  if (rc != 0) {
    fm_set_vfs_status(app,
                      APP_T("Falha ao mover", "Move failed",
                            "Error al mover"), rc);
    return 0;
  }
  fm_set_ok_status(app, APP_T("Movido", "Moved", "Movido"));
  /* Refresh the current directory if it is the destination or contains it. */
  if (fm_path_equal(app->current_path, dst_dir) ||
      fm_path_inside_or_same(app->current_path, dst_dir)) {
    file_manager_load_path(app, app->current_path, 1);
  }
  /* Also refresh if we just moved an entry OUT of the current directory. */
  {
    const char *src_basename = fm_basename(src_path);
    char src_dir[FM_PATH_MAX];
    size_t src_path_len = kstrlen(src_path);
    size_t src_basename_len = kstrlen(src_basename);
    size_t src_dir_len = (src_path_len > src_basename_len + 1)
                             ? (src_path_len - src_basename_len - 1)
                             : 0;
    if (src_dir_len > 0 && src_dir_len < sizeof(src_dir)) {
      for (size_t i = 0; i < src_dir_len; ++i) src_dir[i] = src_path[i];
      src_dir[src_dir_len] = '\0';
      if (fm_path_equal(app->current_path, src_dir)) {
        file_manager_load_path(app, app->current_path, 1);
      }
    }
  }
  return 1;
}

int fm_delete_path_now(struct file_manager_app *app, const char *path,
                       uint16_t mode) {
  int rc = 0;
  if (!app || !path || !path[0]) return 0;
  rc = (mode & VFS_MODE_DIR) ? vfs_rmdir_recursive(path) : vfs_unlink(path);
  if (rc != 0) {
    fm_set_vfs_status(app,
                      (mode & VFS_MODE_DIR)
                          ? APP_T("Falha ao apagar pasta",
                                  "Delete dir failed",
                                  "Error al borrar carpeta")
                          : APP_T("Falha ao apagar arquivo",
                                  "Delete file failed",
                                  "Error al borrar archivo"), rc);
    return 0;
  }
  fm_set_ok_status(app, APP_T("Apagado", "Deleted", "Borrado"));
  file_manager_load_path(app, app->current_path, 1);
  return 1;
}

static void fm_delete_confirm_submit(const char *text, void *ctx) {
  struct file_manager_app *app = (struct file_manager_app *)ctx;
  if (!app) return;
  if (!text || !kstreq(text, "DELETE")) {
    fm_set_status(app, APP_T("Cancelado", "Cancelled", "Cancelado"),
                  0x00CDD6F4);
    if (app->window) compositor_invalidate(app->window->id);
    return;
  }
  (void)fm_delete_path_now(app, g_fm_delete_path, g_fm_delete_mode);
  g_fm_delete_path[0] = '\0';
  g_fm_delete_mode = 0;
}

void fm_request_delete_selected(struct file_manager_app *app) {
  struct fm_entry *e = NULL;
  char path[FM_PATH_MAX];
  int32_t px = 0;
  int32_t py = 0;
  if (!app || app->selected < 0 || app->selected >= app->entry_count) return;
  e = &app->entries[app->selected];
  fm_join_path(app->current_path, e->name, path, sizeof(path));
  kstrcpy(g_fm_delete_path, sizeof(g_fm_delete_path), path);
  g_fm_delete_mode = e->mode;
  if (app->window) {
    px = app->window->frame.x + 16;
    py = app->window->frame.y + app->window->frame.height / 2;
  }
  inline_prompt_show(APP_T("Digite DELETE para confirmar:",
                           "Type DELETE to confirm:",
                           "Escriba DELETE para confirmar:"),
                     "", px, py, fm_delete_confirm_submit, app);
}

int fm_drop_target_dir_at(struct file_manager_app *app, int32_t screen_x,
                          int32_t screen_y, char *out, size_t out_len,
                          int *out_row) {
  int32_t lx = 0;
  int32_t ly = 0;
  if (!app || !app->window || !out || out_len == 0) return 0;
  lx = screen_x - app->window->frame.x;
  ly = screen_y - app->window->frame.y;
  if (lx < 0 || ly < FM_TOOLBAR_H + 4) return 0;
  int row = fm_row_at(app, lx, ly);
  if (row >= 0 && (app->entries[row].mode & VFS_MODE_DIR)) {
    fm_join_path(app->current_path, app->entries[row].name, out, out_len);
    if (out_row) *out_row = row;
    return 1;
  }
  kstrcpy(out, out_len, app->current_path);
  if (out_row) *out_row = -1;
  return 1;
}

int fm_path_exists(const char *path) {
  struct dentry *d = NULL;
  if (vfs_lookup(path, &d) != 0) return 0;
  if (d && d->refcount) d->refcount--;
  return 1;
}

int fm_unique_path(struct file_manager_app *app, const char *base,
                   const char *suffix, char *out, size_t out_len) {
  char name[64];
  uint32_t attempt = 0;
  if (!app || !out || out_len == 0) return -1;
  for (;;) {
    name[0] = '\0';
    kstrcpy(name, sizeof(name), base ? base : "untitled");
    if (attempt > 0) {
      kbuf_append(name, sizeof(name), "_");
      kbuf_append_u32(name, sizeof(name), attempt);
    }
    if (suffix) kbuf_append(name, sizeof(name), suffix);
    fm_join_path(app->current_path, name, out, out_len);
    if (!fm_path_exists(out)) return 0;
    if (attempt >= 999) return -1;
    ++attempt;
  }
}

const char *fm_initial_path(void) {
  struct session_context *session = session_active();
  const struct user_record *user = session_user(session);
  if (user && user->home[0]) {
    return user->home;
  }
  return "/";
}

/* ── window lifecycle ────────────────────────────────────────────────── */

void file_manager_cleanup(void) {
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
  kstrcpy(e->name, sizeof(e->name), name);
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
static void file_manager_window_resize(struct gui_window *win, uint32_t w,
                                       uint32_t h) {
  (void)w;
  (void)h;
  if (!win || !win->user_data) return;
  file_manager_paint((struct file_manager_app *)win->user_data);
}

static void file_manager_window_mouse(struct gui_window *win, int32_t x, int32_t y,
                                      uint8_t buttons) {
  struct file_manager_app *app = NULL;
  int left_down = (buttons & 1) ? 1 : 0;
  if (!win || !win->user_data) return;
  app = (struct file_manager_app *)win->user_data;
  /* Cancel any active drag if the user released the button outside of
   * a valid drop target — see file_manager_handle_mouse_up. */
  if (app->drag_active && !left_down) {
    app->mouse_left_down = 0;
    file_manager_handle_mouse_up(win, win->frame.x + x, win->frame.y + y);
    return;
  }
  /* Forward drag-in-progress moves to the dnd path so external drop
   * targets get previews. */
  if (app->drag_active && left_down) {
    file_manager_handle_drag_move(win, win->frame.x + x, win->frame.y + y,
                                  buttons);
    return;
  }
  if (!left_down) {
    app->mouse_left_down = 0;
    return;
  }
  if (app->mouse_left_down) return;
  app->mouse_left_down = 1;
  file_manager_handle_click(app, x, y);
}

static void file_manager_window_scroll(struct gui_window *win, int32_t delta) {
  if (!win || !win->user_data) return;
  struct file_manager_app *app = (struct file_manager_app *)win->user_data;
  if (delta > 0 && app->scroll_offset > 0) {
    app->scroll_offset--;
    compositor_invalidate(app->window->id);
  } else if (delta < 0 && app->scroll_offset < app->entry_count - 1) {
    app->scroll_offset++;
    compositor_invalidate(app->window->id);
  }
  if (app->scroll_offset >= app->entry_count)
    app->scroll_offset = (app->entry_count > 0) ? app->entry_count - 1 : 0;
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

void file_manager_navigate_back(struct file_manager_app *app) {
  char target[FM_PATH_MAX];
  char current[FM_PATH_MAX];
  if (!app || !app->previous_path[0]) return;
  kstrcpy(current, FM_PATH_MAX, app->current_path);
  kstrcpy(target, FM_PATH_MAX, app->previous_path);
  file_manager_load_path(app, target, 0);
  kstrcpy(app->previous_path, FM_PATH_MAX, current);
}

void file_manager_navigate_up(struct file_manager_app *app) {
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

/* ── click router ────────────────────────────────────────────────────── */

void file_manager_handle_click(struct file_manager_app *app, int32_t x, int32_t y) {
  int rc = 0;
  int32_t toolbar_x = 0, button_w = 0, button_gap = 0;
  if (!app) return;
  if (app->window) {
    fm_toolbar_layout(app->window->surface.width, &toolbar_x,
                      &button_w, &button_gap);
  }
  if (y >= FM_BUTTON_Y && y < FM_BUTTON_Y + FM_BUTTON_H && app->window) {
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
