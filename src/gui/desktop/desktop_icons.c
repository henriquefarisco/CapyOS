/* src/gui/desktop/desktop_icons.c
 *
 * Etapa UX W7-ish (2026-05-03): implementacao do desktop icons.
 * Modulo segregado, sem libc, sem alocacao dinamica (usa storage
 * estatico para a listagem de entries).
 *
 * Comportamento:
 *   - Carrega vfs_listdir(path) na init e refresh.
 *   - Pinta cada entry como uma celula 80x80 com:
 *       icone 32x32 colorido (azul para dir, cinza p/ file)
 *       nome em duas linhas (truncated com "...")
 *   - Click esquerdo seleciona; click duplo (segundo click no mesmo
 *     icone) abre.
 *   - Right-click sobre icone: Open/Delete/Rename/Refresh.
 *   - Right-click em area vazia: New File/New Folder/Refresh.
 */
#include "gui/desktop_icons.h"
#include "gui/compositor.h"
#include "lang/app_language.h"
#include "lang/localization.h"
#include "gui/font.h"
#include "gui/context_menu.h"
#include "gui/inline_prompt.h"
#include "apps/text_editor.h"
#include "apps/file_manager.h"
#include "fs/vfs.h"
#include "auth/session.h"
#include <stddef.h>

/* Estado global. */
struct di_entry {
  char     name[DESKTOP_ICONS_NAME_MAX];
  uint16_t mode;
  uint8_t  reserved[2];
};

static struct {
  struct di_entry entries[DESKTOP_ICONS_MAX];
  uint32_t        count;
  char            path[256];
  uint32_t        taskbar_h;
  int             selected;          /* idx ou -1 */
  uint32_t        screen_w;
  uint32_t        screen_h;
  /* Contexto da operacao (rename/new) em curso. */
  int             pending_action;    /* DI_CTX_* */
  int             pending_target;    /* idx do icone alvo, ou -1 */
} g_di = {0};

#define DI_CTX_OPEN     1u
#define DI_CTX_DELETE   2u
#define DI_CTX_RENAME   3u
#define DI_CTX_NEW_FILE 4u
#define DI_CTX_NEW_DIR  5u
#define DI_CTX_REFRESH  6u

/* === Helpers ============================================================ */
static uint32_t di_strlen(const char *s) {
  uint32_t n = 0;
  while (s && s[n]) n++;
  return n;
}

static void di_strcpy(char *d, const char *s, uint32_t max) {
  uint32_t i = 0;
  if (!d || max == 0u) return;
  if (s) while (i + 1u < max && s[i]) { d[i] = s[i]; i++; }
  d[i] = '\0';
}

static int di_streq(const char *a, const char *b) {
  if (!a || !b) return 0;
  while (*a && *a == *b) { a++; b++; }
  return *a == 0 && *b == 0;
}

static void di_join(const char *dir, const char *name, char *out,
                     uint32_t max) {
  uint32_t i = 0, j = 0;
  if (!out || max == 0u) return;
  if (dir) while (j + 1u < max && dir[i]) { out[j++] = dir[i++]; }
  if (j > 0u && out[j - 1u] != '/' && j + 1u < max) out[j++] = '/';
  i = 0;
  if (name) while (j + 1u < max && name[i]) { out[j++] = name[i++]; }
  out[j] = '\0';
}

static int di_iter_cb(const char *name, uint16_t mode, void *ctx) {
  (void)ctx;
  if (g_di.count >= DESKTOP_ICONS_MAX) return -1;
  /* Ignora "." e "..". */
  if (di_streq(name, ".") || di_streq(name, "..")) return 0;
  struct di_entry *e = &g_di.entries[g_di.count++];
  di_strcpy(e->name, name, DESKTOP_ICONS_NAME_MAX);
  e->mode = mode;
  e->reserved[0] = 0;
  e->reserved[1] = 0;
  return 0;
}

static void di_load(void) {
  g_di.count = 0;
  g_di.selected = -1;
  if (g_di.path[0]) {
    vfs_listdir(g_di.path, di_iter_cb, NULL);
  }
}

static void di_fill_rect(struct gui_surface *s, int32_t x, int32_t y,
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

/* Calcula a posicao (px, py) do icone idx no grid. screen_h e usado
 * apenas para clamp; assume coluna unica esquerda. */
static void di_icon_position(uint32_t idx, int32_t *out_x, int32_t *out_y) {
  /* Single column (x = PAD_LEFT). y avanca em CELL_H. */
  *out_x = (int32_t)DESKTOP_ICON_PAD_LEFT;
  *out_y = (int32_t)(DESKTOP_ICON_PAD_TOP + idx * DESKTOP_ICON_CELL_H);
}

/* Detecta se um arquivo .txt/.md (heuristic case-insensitive). */
static int di_is_text(const char *name) {
  uint32_t n = di_strlen(name);
  if (n >= 4 && name[n - 4] == '.' &&
      (name[n - 3] == 't' || name[n - 3] == 'T') &&
      (name[n - 2] == 'x' || name[n - 2] == 'X') &&
      (name[n - 1] == 't' || name[n - 1] == 'T')) return 1;
  if (n >= 3 && name[n - 3] == '.' &&
      (name[n - 2] == 'm' || name[n - 2] == 'M') &&
      (name[n - 1] == 'd' || name[n - 1] == 'D')) return 1;
  return 0;
}

/* === Public API ========================================================= */

void desktop_icons_init(const char *path, uint32_t taskbar_height) {
  /* Por enquanto so guardamos o path desejado; carregamos quando
   * a primeira paint chega (compositor ja inicializado). */
  g_di.count = 0;
  g_di.selected = -1;
  g_di.taskbar_h = taskbar_height;
  g_di.pending_action = 0;
  g_di.pending_target = -1;
  if (path) {
    di_strcpy(g_di.path, path, sizeof(g_di.path));
  } else {
    di_strcpy(g_di.path, "/", sizeof(g_di.path));
  }
  di_load();
}

void desktop_icons_refresh(void) {
  di_load();
  compositor_invalidate_all();
}

void desktop_icons_paint(struct gui_surface *s) {
  if (!s) return;
  g_di.screen_w = s->width;
  g_di.screen_h = s->height;
  const struct gui_theme_palette *theme = compositor_theme();
  const struct font *f = font_default();
  if (!f) return;

  for (uint32_t i = 0; i < g_di.count; i++) {
    int32_t x = 0, y = 0;
    di_icon_position(i, &x, &y);
    if (y + (int32_t)DESKTOP_ICON_CELL_H >
        (int32_t)(s->height - g_di.taskbar_h)) break;

    int sel = ((int)i == g_di.selected);
    /* Highlight da selecao: caixa accent_alt semi-transparente
     * simulada com solid fill. Cells ocupam 76x76 internos. */
    if (sel) {
      di_fill_rect(s, x - 2, y - 2, DESKTOP_ICON_CELL_W,
                   DESKTOP_ICON_CELL_H, theme->accent_alt);
    }

    /* Icone 32x32 centralizado horizontalmente na celula. */
    int32_t ix = x + (int32_t)((DESKTOP_ICON_CELL_W - 32u) / 2u) - 2;
    int32_t iy = y + 4;
    int is_dir = (g_di.entries[i].mode & VFS_MODE_DIR) != 0;
    uint32_t icon_bg = is_dir ? theme->accent : theme->accent_alt;
    uint32_t icon_border = theme->window_border;

    /* Fundo do icone. */
    di_fill_rect(s, ix, iy, 32, 32, icon_bg);
    /* Border 1 px. */
    di_fill_rect(s, ix, iy, 32, 1, icon_border);
    di_fill_rect(s, ix, iy + 31, 32, 1, icon_border);
    di_fill_rect(s, ix, iy, 1, 32, icon_border);
    di_fill_rect(s, ix + 31, iy, 1, 32, icon_border);
    /* Pasta tem uma "aba" 8 px no topo (W7 folder feel). */
    if (is_dir) {
      di_fill_rect(s, ix + 4, iy - 3, 12, 4, icon_bg);
      di_fill_rect(s, ix + 4, iy - 3, 1, 4, icon_border);
      di_fill_rect(s, ix + 16, iy - 3, 1, 4, icon_border);
      di_fill_rect(s, ix + 4, iy - 3, 12, 1, icon_border);
    } else {
      /* Arquivo: dobra de canto. */
      di_fill_rect(s, ix + 22, iy + 4, 6, 6, theme->window_bg);
      di_fill_rect(s, ix + 22, iy + 4, 6, 1, icon_border);
      di_fill_rect(s, ix + 22, iy + 4, 1, 6, icon_border);
    }

    /* Nome (truncated). 2 linhas no max, GLYPH_W ~= 8 px. */
    char line1[16], line2[16];
    line1[0] = '\0'; line2[0] = '\0';
    uint32_t nlen = di_strlen(g_di.entries[i].name);
    /* split em 9 chars (76/8 ~ 9). */
    uint32_t split = (nlen <= 9u) ? nlen : 9u;
    for (uint32_t k = 0; k < split && k < 15; k++)
      line1[k] = g_di.entries[i].name[k];
    line1[split < 15u ? split : 15u] = '\0';
    if (nlen > 9u) {
      uint32_t rem = nlen - 9u;
      uint32_t rs = (rem <= 9u) ? rem : 9u;
      for (uint32_t k = 0; k < rs && k < 15; k++) {
        line2[k] = g_di.entries[i].name[9u + k];
      }
      if (rem > 9u && rs >= 3u) {
        /* trunca com "..." */
        line2[rs - 3u] = '.';
        line2[rs - 2u] = '.';
        line2[rs - 1u] = '.';
      }
      line2[rs < 15u ? rs : 15u] = '\0';
    }
    int32_t name_y = y + 40;
    /* Centraliza horizontalmente, glyph_width ~ 8 px. */
    int32_t lw1 = (int32_t)(di_strlen(line1) * 8u);
    int32_t name_x1 = x + (int32_t)((DESKTOP_ICON_CELL_W - 4u -
                                      (uint32_t)lw1) / 2u);
    font_draw_string(s, f, name_x1, name_y, line1,
                     sel ? theme->accent_text : theme->text);
    if (line2[0]) {
      int32_t lw2 = (int32_t)(di_strlen(line2) * 8u);
      int32_t name_x2 = x + (int32_t)((DESKTOP_ICON_CELL_W - 4u -
                                        (uint32_t)lw2) / 2u);
      font_draw_string(s, f, name_x2, name_y + 12, line2,
                       sel ? theme->accent_text : theme->text);
    }
  }
}

int desktop_icons_hit_test(int32_t sx, int32_t sy) {
  for (uint32_t i = 0; i < g_di.count; i++) {
    int32_t x = 0, y = 0;
    di_icon_position(i, &x, &y);
    if (sx >= x && sx < x + (int32_t)DESKTOP_ICON_CELL_W &&
        sy >= y && sy < y + (int32_t)DESKTOP_ICON_CELL_H) {
      return (int)i;
    }
  }
  return -1;
}

void desktop_icons_clear_selection(void) {
  if (g_di.selected != -1) {
    g_di.selected = -1;
    compositor_invalidate_all();
  }
}

void desktop_icons_handle_click(int32_t sx, int32_t sy) {
  int hit = desktop_icons_hit_test(sx, sy);
  int prev = g_di.selected;
  if (hit < 0) {
    g_di.selected = -1;
    if (prev != -1) compositor_invalidate_all();
    return;
  }
  /* Click no mesmo icone duas vezes -> abre.
   * (Sem timer, "double-click" = qualquer click subsequente no
   * mesmo icone enquanto ele estiver selecionado. Nao e ideal mas
   * funciona sem framework de eventos temporais.) */
  if (g_di.selected == hit) {
    char path[256];
    di_join(g_di.path, g_di.entries[hit].name, path, sizeof(path));
    if (g_di.entries[hit].mode & VFS_MODE_DIR) {
      file_manager_open_at(path);
    } else if (di_is_text(g_di.entries[hit].name)) {
      text_editor_open(path);
    }
    g_di.selected = -1;
    compositor_invalidate_all();
  } else {
    g_di.selected = hit;
    compositor_invalidate_all();
  }
}

/* === Context menu callbacks ============================================ */

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
      char path[256];
      di_join(g_di.path, g_di.entries[g_di.selected].name, path,
              sizeof(path));
      if (g_di.entries[g_di.selected].mode & VFS_MODE_DIR) {
        vfs_rmdir(path);
      } else {
        vfs_unlink(path);
      }
      g_di.selected = -1;
      desktop_icons_refresh();
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
