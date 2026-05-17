/* src/gui/desktop/desktop_icons_context.c
 *
 * Right-click context menu for the desktop icons grid.
 *
 * Carved out of `desktop_icons.c` at the 2026-05-16 preventive
 * refactor when the parent TU reached 823/900 LOC. The context
 * menu callbacks (`di_ctx_pick`, `di_rename_submit`,
 * `di_create_submit`) plus the public entry
 * `desktop_icons_handle_context` form a cohesive ~140 LOC block
 * with their own dependencies on apps/file_manager, apps/text_editor,
 * gui/context_menu and gui/inline_prompt that the rest of
 * desktop_icons.c does not need.
 *
 * Shared state (`g_di`), the `DI_CTX_*` action constants and the
 * `di_*` helpers consumed here are declared in
 * `src/gui/desktop/internal/desktop_icons_internal.h`.
 *
 * Public ABI preserved: `desktop_icons_handle_context` continues to
 * be declared in `include/gui/desktop_icons.h` and called by
 * `desktop_handle_mouse` (in `src/gui/desktop/desktop_mouse.c`).
 */

#include "gui/desktop_icons.h"
#include "gui/desktop/internal/desktop_icons_internal.h"
#include "gui/context_menu.h"
#include "gui/inline_prompt.h"
#include "apps/text_editor.h"
#include "apps/file_manager.h"
#include "lang/app_language.h"
#include "fs/vfs.h"
#include <stddef.h>

static void di_rename_submit(const char *new_name, void *ctx);
static void di_create_submit(const char *new_name, void *ctx);

static void di_ctx_pick(uint16_t action_id, void *ctx) {
  (void)ctx;
  switch (action_id) {
    case DI_CTX_OPEN: {
      if (g_di.selected < 0 || g_di.selected >= (int)g_di.count) return;
      char path[256];
      di_join(g_di.path, g_di.entries[g_di.selected].name, path,
              sizeof(path));
      if (g_di.entries[g_di.selected].mode & VFS_MODE_DIR) {
        file_manager_open_at(path);
      } else if (di_is_text(g_di.entries[g_di.selected].name)) {
        text_editor_open(path);
      }
      break;
    }
    case DI_CTX_DELETE: {
      if (g_di.selected < 0 || g_di.selected >= (int)g_di.count) return;
      di_request_delete_selected();
      break;
    }
    case DI_CTX_RENAME: {
      if (g_di.selected < 0 || g_di.selected >= (int)g_di.count) return;
      g_di.pending_action = DI_CTX_RENAME;
      g_di.pending_target = g_di.selected;
      /* Posiciona o prompt no canto inferior do icone. */
      int32_t ix = 0, iy = 0;
      di_icon_position((uint32_t)g_di.selected, &ix, &iy);
      inline_prompt_show(APP_T("Renomear:", "Rename:", "Renombrar:"),
                         g_di.entries[g_di.selected].name,
                         ix, iy + (int32_t)DESKTOP_ICON_CELL_H,
                         di_rename_submit, NULL);
      break;
    }
    case DI_CTX_NEW_FILE:
      g_di.pending_action = DI_CTX_NEW_FILE;
      g_di.pending_target = -1;
      inline_prompt_show(APP_T("Nome do arquivo:", "New file name:",
                                "Nombre del archivo:"),
                         "untitled.txt",
                         (int32_t)DESKTOP_ICON_PAD_LEFT,
                         (int32_t)DESKTOP_ICON_PAD_TOP,
                         di_create_submit, NULL);
      break;
    case DI_CTX_NEW_DIR:
      g_di.pending_action = DI_CTX_NEW_DIR;
      g_di.pending_target = -1;
      inline_prompt_show(APP_T("Nome da pasta:", "New folder name:",
                                "Nombre de la carpeta:"),
                         "new_folder",
                         (int32_t)DESKTOP_ICON_PAD_LEFT,
                         (int32_t)DESKTOP_ICON_PAD_TOP,
                         di_create_submit, NULL);
      break;
    case DI_CTX_REFRESH:
      desktop_icons_refresh();
      break;
    default: break;
  }
}

static void di_rename_submit(const char *new_name, void *ctx) {
  (void)ctx;
  if (g_di.pending_target < 0 || g_di.pending_target >= (int)g_di.count)
    return;
  if (!new_name || new_name[0] == '\0') return;
  char src[256], dst[256];
  di_join(g_di.path, g_di.entries[g_di.pending_target].name, src,
          sizeof(src));
  di_join(g_di.path, new_name, dst, sizeof(dst));
  vfs_rename(src, dst);
  g_di.pending_action = 0;
  g_di.pending_target = -1;
  g_di.selected = -1;
  desktop_icons_refresh();
}

static void di_create_submit(const char *new_name, void *ctx) {
  (void)ctx;
  if (!new_name || new_name[0] == '\0') return;
  char path[256];
  di_join(g_di.path, new_name, path, sizeof(path));
  if (g_di.pending_action == DI_CTX_NEW_FILE) {
    vfs_create(path, VFS_MODE_FILE, NULL);
  } else if (g_di.pending_action == DI_CTX_NEW_DIR) {
    vfs_create(path, VFS_MODE_DIR, NULL);
  }
  g_di.pending_action = 0;
  g_di.pending_target = -1;
  desktop_icons_refresh();
}

void desktop_icons_handle_context(int32_t sx, int32_t sy) {
  int hit = desktop_icons_hit_test(sx, sy);
  if (hit >= 0) g_di.selected = hit;
  else g_di.selected = -1;

  struct context_menu_item items[CONTEXT_MENU_MAX_ITEMS];
  for (uint32_t i = 0; i < CONTEXT_MENU_MAX_ITEMS; ++i) {
    items[i].label[0] = '\0';
    items[i].action_id = 0;
    items[i].enabled = 1;
    items[i].reserved = 0;
  }
  uint32_t n = 0;
  /* Etapa F4 i18n (2026-05-03): labels do context menu localizados. */
  if (hit >= 0) {
    di_strcpy(items[n].label, APP_T("Abrir", "Open", "Abrir"),
              CONTEXT_MENU_LABEL_MAX);
    items[n++].action_id = DI_CTX_OPEN;
    di_strcpy(items[n].label, APP_T("Renomear", "Rename", "Renombrar"),
              CONTEXT_MENU_LABEL_MAX);
    items[n++].action_id = DI_CTX_RENAME;
    di_strcpy(items[n].label, APP_T("Apagar", "Delete", "Borrar"),
              CONTEXT_MENU_LABEL_MAX);
    items[n++].action_id = DI_CTX_DELETE;
    items[n++].label[0] = '\0';
    di_strcpy(items[n].label, APP_T("Atualizar", "Refresh", "Actualizar"),
              CONTEXT_MENU_LABEL_MAX);
    items[n++].action_id = DI_CTX_REFRESH;
  } else {
    di_strcpy(items[n].label,
              APP_T("Novo arquivo", "New File", "Nuevo archivo"),
              CONTEXT_MENU_LABEL_MAX);
    items[n++].action_id = DI_CTX_NEW_FILE;
    di_strcpy(items[n].label,
              APP_T("Nova pasta", "New Folder", "Nueva carpeta"),
              CONTEXT_MENU_LABEL_MAX);
    items[n++].action_id = DI_CTX_NEW_DIR;
    items[n++].label[0] = '\0';
    di_strcpy(items[n].label, APP_T("Atualizar", "Refresh", "Actualizar"),
              CONTEXT_MENU_LABEL_MAX);
    items[n++].action_id = DI_CTX_REFRESH;
  }
  context_menu_show(items, n, sx, sy, di_ctx_pick, NULL);
}
