#include "apps/file_manager.h"
#include "apps/text_editor.h"
#include "gui/compositor.h"
#include "gui/font.h"
#include "gui/widget.h"
#include "gui/context_menu.h"
#include "gui/inline_prompt.h"
#include "fs/vfs.h"
#include "auth/session.h"
#include "lang/app_language.h"
#include "util/kstring.h"
#include "memory/kmem.h"
#include <stddef.h>

static struct file_manager_app g_fm;
static int g_fm_open = 0;

static int fm_iter_cb(const char *name, uint16_t mode, void *ctx);

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
    font_draw_string(s, f, bx + 4, 4,
                     APP_T("Novo Arq", "New File", "Nuevo Arch"),
                     theme->text);
    font_draw_string(s, f, bx + 80, 4,
                     APP_T("Nova Pasta", "New Dir", "Nueva Carp"),
                     theme->text);
    font_draw_string(s, f, bx + 156, 4,
                     APP_T("Apagar", "Delete", "Borrar"),
                     (app->selected >= 0) ? 0x00FFFFFF : theme->text_muted);
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
    if (x >= bx + 76 && x < bx + 144) {
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
    if (x >= bx + 152 && x < bx + 220 && app->selected >= 0 &&
        app->selected < app->entry_count) {
      struct fm_entry *e = &app->entries[app->selected];
      char path[FM_PATH_MAX];
      fm_join_path(app->current_path, e->name, path, sizeof(path));
      if (e->mode & VFS_MODE_DIR) rc = vfs_rmdir(path);
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
    } else {
      /* Etapa UX W7-ish (2026-05-03): file_manager opens .txt files
       * in the text editor on click. Heuristico simples: se o nome
       * termina em ".txt" ou ".md" abre no text_editor. */
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
      struct fm_entry *e = &app->entries[app->selected];
      char path[FM_PATH_MAX];
      int rc = 0;
      fm_join_path(app->current_path, e->name, path, sizeof(path));
      if (e->mode & VFS_MODE_DIR) rc = vfs_rmdir(path);
      else rc = vfs_unlink(path);
      if (rc != 0)
        fm_set_vfs_status(app,
                          APP_T("Falha ao apagar", "Delete failed",
                                "Error al borrar"), rc);
      else
        fm_set_ok_status(app,
                         APP_T("Item removido", "Entry removed",
                               "Elemento eliminado"));
      file_manager_load_path(app, app->current_path, 1);
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
    case FM_CTX_RENAME: {
      if (app->selected < 0 || app->selected >= app->entry_count) return;
      struct fm_entry *e = &app->entries[app->selected];
      /* Posiciona o prompt sob a linha selecionada. */
      int32_t px = app->window ? app->window->frame.x : 0;
      int32_t py = app->window ? app->window->frame.y : 0;
      int32_t row_y = 24 + (app->selected - app->scroll_offset) * 18 + 18;
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
  if (ly >= 24) {
    int idx = app->scroll_offset + (ly - 24) / 18;
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

  /* Etapa F4 i18n (2026-05-03): labels do context menu localizados. */
  if (target_row >= 0) {
    /* Sobre uma entrada: Open / Rename / Delete / -- / Refresh */
    kstrcpy(items[n].label, CONTEXT_MENU_LABEL_MAX,
            APP_T("Abrir", "Open", "Abrir"));
    items[n++].action_id = FM_CTX_OPEN;
    kstrcpy(items[n].label, CONTEXT_MENU_LABEL_MAX,
            APP_T("Renomear", "Rename", "Renombrar"));
    items[n++].action_id = FM_CTX_RENAME;
    kstrcpy(items[n].label, CONTEXT_MENU_LABEL_MAX,
            APP_T("Apagar", "Delete", "Borrar"));
    items[n++].action_id = FM_CTX_DELETE;
    items[n++].label[0] = '\0'; /* sep */
    kstrcpy(items[n].label, CONTEXT_MENU_LABEL_MAX,
            APP_T("Atualizar", "Refresh", "Actualizar"));
    items[n++].action_id = FM_CTX_REFRESH;
  } else {
    /* Area vazia: criacao + navigation. */
    kstrcpy(items[n].label, CONTEXT_MENU_LABEL_MAX,
            APP_T("Novo arquivo", "New File", "Nuevo archivo"));
    items[n++].action_id = FM_CTX_NEW_FILE;
    kstrcpy(items[n].label, CONTEXT_MENU_LABEL_MAX,
            APP_T("Nova pasta", "New Folder", "Nueva carpeta"));
    items[n++].action_id = FM_CTX_NEW_DIR;
    items[n++].label[0] = '\0';
    kstrcpy(items[n].label, CONTEXT_MENU_LABEL_MAX,
            APP_T("Subir", "Up", "Subir"));
    items[n++].action_id = FM_CTX_NAV_UP;
    kstrcpy(items[n].label, CONTEXT_MENU_LABEL_MAX,
            APP_T("Atualizar", "Refresh", "Actualizar"));
    items[n++].action_id = FM_CTX_REFRESH;
  }

  int32_t sx = win->frame.x + lx;
  int32_t sy = win->frame.y + ly;
  context_menu_show(items, n, sx, sy, fm_ctx_pick, app);
}
