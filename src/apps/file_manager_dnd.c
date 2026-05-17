/*
 * src/apps/file_manager_dnd.c
 *
 * Drag-and-drop event handlers and right-click context menu for the
 * file manager. Carved out of `src/apps/file_manager.c` at the
 * 2026-05-15 refactor so each translation unit stays under the
 * 900-line layout limit.
 *
 * Public symbols owned here:
 *   - `file_manager_handle_drag_move`
 *   - `file_manager_handle_mouse_up`
 *   - `file_manager_preview_drop_path_at`
 *   - `file_manager_drop_path_at`
 *   - `file_manager_clear_external_drop`
 *
 * Internal symbol exposed via `internal/file_manager_internal.h`:
 *   - `file_manager_window_context_menu` (called as `on_context_menu`
 *     callback from `file_manager_open` in the sister TU).
 *
 * All lifecycle, navigation and rendering concerns live in the sister
 * files `file_manager.c` and `file_manager_view.c`.
 */
#include "apps/file_manager.h"
#include "apps/text_editor.h"
#include "fs/vfs.h"
#include "gui/compositor.h"
#include "gui/context_menu.h"
#include "gui/desktop.h"
#include "gui/desktop_icons.h"
#include "gui/font.h"
#include "gui/inline_prompt.h"
#include "lang/app_language.h"
#include "util/kstring.h"

#include "apps/internal/file_manager_internal.h"

#include <stddef.h>
#include <stdint.h>

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
void file_manager_window_context_menu(struct gui_window *win, int32_t lx,
                                      int32_t ly) {
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
