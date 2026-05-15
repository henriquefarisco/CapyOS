/* src/gui/desktop/desktop_icons.c
 *
 * Etapa UX W7-ish (2026-05-03): implementacao do desktop icons.
 * Modulo segregado, sem libc, sem alocacao dinamica (usa storage
 * estatico para a listagem de entries).
 *
 * Comportamento:
 *   - Carrega vfs_listdir(path) na init e refresh.
 *   - Pinta cada entry como uma celula 88x86 com:
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
  int             drag_active;
  int             drag_source;
  int             drag_over;
  int             drag_open_on_release;
  int             drag_moved;
  int32_t         drag_start_x;
  int32_t         drag_start_y;
} g_di = {0};

static char g_di_delete_path[256];
static uint16_t g_di_delete_mode;

#define DI_CTX_OPEN     1u
#define DI_CTX_DELETE   2u
#define DI_CTX_RENAME   3u
#define DI_CTX_NEW_FILE 4u
#define DI_CTX_NEW_DIR  5u
#define DI_CTX_REFRESH  6u

static void di_sort_entries(void);

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

static const char *di_basename(const char *path) {
  const char *base = path;
  if (!path) return "";
  for (uint32_t i = 0; path[i]; i++) {
    if (path[i] == '/' && path[i + 1]) base = &path[i + 1];
  }
  return base ? base : "";
}

static int di_path_inside_or_same(const char *dir, const char *path) {
  uint32_t plen = 0;
  if (!dir || !path) return 0;
  if (di_streq(dir, path)) return 1;
  plen = di_strlen(path);
  if (plen == 0u) return 0;
  if (di_strlen(dir) <= plen) return 0;
  for (uint32_t i = 0; i < plen; i++) {
    if (dir[i] != path[i]) return 0;
  }
  return dir[plen] == '/';
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
  di_sort_entries();
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

static uint32_t di_mix_color(uint32_t color, uint32_t target,
                             uint8_t amount) {
  uint32_t r = (color >> 16) & 0xFFu;
  uint32_t g = (color >> 8) & 0xFFu;
  uint32_t b = color & 0xFFu;
  uint32_t tr = (target >> 16) & 0xFFu;
  uint32_t tg = (target >> 8) & 0xFFu;
  uint32_t tb = target & 0xFFu;
  int32_t nr = (int32_t)r + (((int32_t)tr - (int32_t)r) * amount) / 255;
  int32_t ng = (int32_t)g + (((int32_t)tg - (int32_t)g) * amount) / 255;
  int32_t nb = (int32_t)b + (((int32_t)tb - (int32_t)b) * amount) / 255;
  if (nr < 0) nr = 0;
  if (ng < 0) ng = 0;
  if (nb < 0) nb = 0;
  if (nr > 255) nr = 255;
  if (ng > 255) ng = 255;
  if (nb > 255) nb = 255;
  return ((uint32_t)nr << 16) | ((uint32_t)ng << 8) | (uint32_t)nb;
}

static char di_lower(char c) {
  if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
  return c;
}

static int di_entry_is_dir(const struct di_entry *e) {
  return e && ((e->mode & VFS_MODE_DIR) != 0);
}

static int di_entry_less(const struct di_entry *a, const struct di_entry *b) {
  uint32_t i = 0;
  int ad = di_entry_is_dir(a);
  int bd = di_entry_is_dir(b);
  if (ad != bd) return ad > bd;
  while (a && b && a->name[i] && b->name[i]) {
    char ca = di_lower(a->name[i]);
    char cb = di_lower(b->name[i]);
    if (ca != cb) return ca < cb;
    i++;
  }
  return a && b && a->name[i] == '\0' && b->name[i] != '\0';
}

static void di_sort_entries(void) {
  for (uint32_t i = 1; i < g_di.count; i++) {
    struct di_entry current = g_di.entries[i];
    uint32_t j = i;
    while (j > 0 && di_entry_less(&current, &g_di.entries[j - 1u])) {
      g_di.entries[j] = g_di.entries[j - 1u];
      j--;
    }
    g_di.entries[j] = current;
  }
}

static void di_ensure_screen(void) {
  if (g_di.screen_w == 0u || g_di.screen_h == 0u) {
    compositor_screen_size(&g_di.screen_w, &g_di.screen_h);
  }
}

static uint32_t di_usable_height(void) {
  uint32_t reserved = g_di.taskbar_h + DESKTOP_ICON_PAD_BOTTOM;
  di_ensure_screen();
  if (g_di.screen_h <= reserved) return 0;
  return g_di.screen_h - reserved;
}

static uint32_t di_usable_width(void) {
  di_ensure_screen();
  if (g_di.screen_w <= DESKTOP_ICON_PAD_RIGHT) return g_di.screen_w;
  return g_di.screen_w - DESKTOP_ICON_PAD_RIGHT;
}

static void di_draw_wallpaper(struct gui_surface *s,
                              const struct gui_theme_palette *theme) {
  uint32_t usable_h = 0;
  uint32_t bands = 8u;
  uint32_t top = 0;
  uint32_t bottom = 0;
  if (!s || !theme) return;
  g_di.screen_w = s->width;
  g_di.screen_h = s->height;
  usable_h = di_usable_height();
  if (usable_h == 0u) return;
  top = di_mix_color(theme->wallpaper, theme->accent_alt, 18u);
  bottom = di_mix_color(theme->wallpaper, 0x00000000, 32u);
  for (uint32_t band = 0; band < bands; band++) {
    uint32_t y0 = (usable_h * band) / bands;
    uint32_t y1 = (usable_h * (band + 1u)) / bands;
    uint8_t amount = (bands > 1u) ? (uint8_t)((band * 255u) / (bands - 1u)) : 0u;
    di_fill_rect(s, 0, (int32_t)y0, s->width, y1 - y0,
                 di_mix_color(top, bottom, amount));
  }
}

static uint32_t di_rows_per_column(void) {
  uint32_t usable_h = di_usable_height();
  uint32_t rows = 1;
  if (usable_h > DESKTOP_ICON_PAD_TOP) {
    rows = (usable_h - DESKTOP_ICON_PAD_TOP) / DESKTOP_ICON_CELL_H;
  }
  return rows ? rows : 1;
}

/* Calcula a posicao (px, py) do icone idx no grid responsivo. */
static void di_icon_position(uint32_t idx, int32_t *out_x, int32_t *out_y) {
  uint32_t rows = di_rows_per_column();
  uint32_t col = idx / rows;
  uint32_t row = idx % rows;
  *out_x = (int32_t)(DESKTOP_ICON_PAD_LEFT + col * DESKTOP_ICON_CELL_W);
  *out_y = (int32_t)(DESKTOP_ICON_PAD_TOP + row * DESKTOP_ICON_CELL_H);
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
  g_di.drag_active = 0;
  g_di.drag_source = -1;
  g_di.drag_over = -1;
  g_di.drag_open_on_release = 0;
  g_di.drag_moved = 0;
  g_di.drag_start_x = 0;
  g_di.drag_start_y = 0;
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
  uint32_t usable_h = di_usable_height();
  uint32_t usable_w = di_usable_width();
  di_draw_wallpaper(s, theme);
  if (!f || !theme || usable_h == 0u) return;

  for (uint32_t i = 0; i < g_di.count; i++) {
    int32_t x = 0, y = 0;
    di_icon_position(i, &x, &y);
    if (x + (int32_t)DESKTOP_ICON_CELL_W > (int32_t)usable_w) break;
    if (y + (int32_t)DESKTOP_ICON_CELL_H > (int32_t)usable_h) break;

    int sel = ((int)i == g_di.selected);
    int drop = ((int)i == g_di.drag_over);
    if (sel || drop) {
      uint32_t sel_bg = drop ? di_mix_color(theme->accent, theme->wallpaper, 42u)
                             : di_mix_color(theme->accent_alt, theme->wallpaper, 58u);
      di_fill_rect(s, x - 3, y - 3, DESKTOP_ICON_CELL_W,
                   DESKTOP_ICON_CELL_H - 2u, sel_bg);
      di_fill_rect(s, x - 3, y - 3, DESKTOP_ICON_CELL_W, 1u,
                   theme->accent);
      di_fill_rect(s, x - 3, y + (int32_t)DESKTOP_ICON_CELL_H - 5,
                   DESKTOP_ICON_CELL_W, 1u, theme->accent);
    }

    int32_t ix = x + (int32_t)((DESKTOP_ICON_CELL_W - 32u) / 2u);
    int32_t iy = y + 5;
    int is_dir = (g_di.entries[i].mode & VFS_MODE_DIR) != 0;
    uint32_t icon_bg = is_dir ? theme->accent : theme->accent_alt;
    uint32_t icon_border = theme->window_border;
    uint32_t shadow = di_mix_color(theme->wallpaper, 0x00000000, 55u);

    di_fill_rect(s, ix + 2, iy + 3, 32, 32, shadow);
    di_fill_rect(s, ix, iy, 32, 32, icon_bg);
    di_fill_rect(s, ix, iy, 32, 1, icon_border);
    di_fill_rect(s, ix, iy + 31, 32, 1, icon_border);
    di_fill_rect(s, ix, iy, 1, 32, icon_border);
    di_fill_rect(s, ix + 31, iy, 1, 32, icon_border);
    di_fill_rect(s, ix + 2, iy + 2, 28, 2,
                 di_mix_color(icon_bg, 0x00FFFFFF, 58u));
    di_fill_rect(s, ix + 2, iy + 28, 28, 2,
                 di_mix_color(icon_bg, 0x00000000, 36u));
    if (is_dir) {
      di_fill_rect(s, ix + 4, iy - 3, 13, 4, icon_bg);
      di_fill_rect(s, ix + 4, iy - 3, 13, 1, icon_border);
      di_fill_rect(s, ix + 4, iy - 3, 1, 4, icon_border);
      di_fill_rect(s, ix + 17, iy - 2, 1, 3, icon_border);
    } else {
      di_fill_rect(s, ix + 22, iy + 4, 6, 6, theme->window_bg);
      di_fill_rect(s, ix + 22, iy + 4, 6, 1, icon_border);
      di_fill_rect(s, ix + 22, iy + 4, 1, 6, icon_border);
    }

    char line1[16], line2[16];
    line1[0] = '\0'; line2[0] = '\0';
    uint32_t nlen = di_strlen(g_di.entries[i].name);
    uint32_t gw = f->glyph_width ? f->glyph_width : 8u;
    uint32_t text_area = DESKTOP_ICON_CELL_W - 10u;
    uint32_t chars_per_line = text_area / gw;
    if (chars_per_line < 3u) chars_per_line = 3u;
    if (chars_per_line > 15u) chars_per_line = 15u;
    uint32_t split = (nlen <= chars_per_line) ? nlen : chars_per_line;
    for (uint32_t k = 0; k < split && k < 15; k++)
      line1[k] = g_di.entries[i].name[k];
    line1[split < 15u ? split : 15u] = '\0';
    if (nlen > chars_per_line) {
      uint32_t rem = nlen - chars_per_line;
      uint32_t rs = (rem <= chars_per_line) ? rem : chars_per_line;
      for (uint32_t k = 0; k < rs && k < 15; k++) {
        line2[k] = g_di.entries[i].name[chars_per_line + k];
      }
      if (rem > chars_per_line && rs >= 3u) {
        line2[rs - 3u] = '.';
        line2[rs - 2u] = '.';
        line2[rs - 1u] = '.';
      }
      line2[rs < 15u ? rs : 15u] = '\0';
    }
    int32_t name_y = y + 43;
    uint32_t chip_h = line2[0] ? 27u : 15u;
    if (sel) {
      di_fill_rect(s, x + 4, name_y - 2, text_area, chip_h,
                   di_mix_color(theme->accent_alt, theme->wallpaper, 38u));
    }
    int32_t lw1 = (int32_t)(di_strlen(line1) * gw);
    int32_t name_x1 = x + 5;
    if ((uint32_t)lw1 < text_area) {
      name_x1 += (int32_t)((text_area - (uint32_t)lw1) / 2u);
    }
    font_draw_string(s, f, name_x1, name_y, line1,
                     sel ? theme->accent_text : theme->text);
    if (line2[0]) {
      int32_t lw2 = (int32_t)(di_strlen(line2) * gw);
      int32_t name_x2 = x + 5;
      if ((uint32_t)lw2 < text_area) {
        name_x2 += (int32_t)((text_area - (uint32_t)lw2) / 2u);
      }
      font_draw_string(s, f, name_x2, name_y + 12, line2,
                       sel ? theme->accent_text : theme->text);
    }
  }
}

int desktop_icons_hit_test(int32_t sx, int32_t sy) {
  uint32_t usable_h = di_usable_height();
  uint32_t usable_w = di_usable_width();
  if (usable_h == 0u || sx < 0 || sy < 0) return -1;
  if ((uint32_t)sy >= usable_h) return -1;
  for (uint32_t i = 0; i < g_di.count; i++) {
    int32_t x = 0, y = 0;
    di_icon_position(i, &x, &y);
    if (x + (int32_t)DESKTOP_ICON_CELL_W > (int32_t)usable_w) break;
    if (y + (int32_t)DESKTOP_ICON_CELL_H > (int32_t)usable_h) break;
    if (sx >= x && sx < x + (int32_t)DESKTOP_ICON_CELL_W &&
        sy >= y && sy < y + (int32_t)DESKTOP_ICON_CELL_H) {
      return (int)i;
    }
  }
  return -1;
}


static void di_open_entry(int idx) {
  char path[256];
  if (idx < 0 || idx >= (int)g_di.count) return;
  di_join(g_di.path, g_di.entries[idx].name, path, sizeof(path));
  if (g_di.entries[idx].mode & VFS_MODE_DIR) {
    file_manager_open_at(path);
  } else if (di_is_text(g_di.entries[idx].name)) {
    text_editor_open(path);
  }
}

static int di_move_entry_to_dir(int src_idx, int dst_idx) {
  char src[256];
  char dst_dir[256];
  char dst[256];
  int rc = 0;
  if (src_idx < 0 || src_idx >= (int)g_di.count ||
      dst_idx < 0 || dst_idx >= (int)g_di.count || src_idx == dst_idx)
    return 0;
  if (!(g_di.entries[dst_idx].mode & VFS_MODE_DIR)) return 0;
  di_join(g_di.path, g_di.entries[src_idx].name, src, sizeof(src));
  di_join(g_di.path, g_di.entries[dst_idx].name, dst_dir, sizeof(dst_dir));
  di_join(dst_dir, g_di.entries[src_idx].name, dst, sizeof(dst));
  rc = vfs_rename(src, dst);
  g_di.selected = -1;
  g_di.drag_over = -1;
  if (rc == 0) desktop_icons_refresh();
  else compositor_invalidate_all();
  return 1;
}

static int di_move_path_to_dir(const char *src_path, const char *dst_dir) {
  char dst[256];
  const char *name = di_basename(src_path);
  int rc = 0;
  if (!src_path || !src_path[0] || !dst_dir || !dst_dir[0] || !name[0])
    return 0;
  if (di_path_inside_or_same(dst_dir, src_path)) {
    compositor_invalidate_all();
    return 1;
  }
  di_join(dst_dir, name, dst, sizeof(dst));
  if (di_streq(src_path, dst)) {
    compositor_invalidate_all();
    return 1;
  }
  rc = vfs_rename(src_path, dst);
  if (rc == 0) desktop_icons_refresh();
  else compositor_invalidate_all();
  return 1;
}

static void di_delete_confirm_submit(const char *text, void *ctx) {
  int rc = 0;
  (void)ctx;
  if (!text || !di_streq(text, "DELETE")) {
    g_di_delete_path[0] = '\0';
    g_di_delete_mode = 0;
    compositor_invalidate_all();
    return;
  }
  if (g_di_delete_mode & VFS_MODE_DIR) rc = vfs_rmdir_recursive(g_di_delete_path);
  else rc = vfs_unlink(g_di_delete_path);
  (void)rc;
  g_di_delete_path[0] = '\0';
  g_di_delete_mode = 0;
  g_di.selected = -1;
  desktop_icons_refresh();
}

static void di_request_delete_selected(void) {
  int32_t ix = 0;
  int32_t iy = 0;
  if (g_di.selected < 0 || g_di.selected >= (int)g_di.count) return;
  di_join(g_di.path, g_di.entries[g_di.selected].name, g_di_delete_path,
          sizeof(g_di_delete_path));
  g_di_delete_mode = g_di.entries[g_di.selected].mode;
  if (!(g_di_delete_mode & VFS_MODE_DIR)) {
    (void)vfs_unlink(g_di_delete_path);
    g_di_delete_path[0] = '\0';
    g_di_delete_mode = 0;
    g_di.selected = -1;
    desktop_icons_refresh();
    return;
  }
  di_icon_position((uint32_t)g_di.selected, &ix, &iy);
  (void)inline_prompt_show(APP_T("Digite DELETE", "Type DELETE",
                                 "Escriba DELETE"),
                           "", ix, iy + (int32_t)DESKTOP_ICON_CELL_H,
                           di_delete_confirm_submit, NULL);
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
    g_di.drag_active = 0;
    g_di.drag_source = -1;
    g_di.drag_over = -1;
    g_di.drag_open_on_release = 0;
    g_di.drag_moved = 0;
    g_di.drag_start_x = 0;
    g_di.drag_start_y = 0;
    if (prev != -1) compositor_invalidate_all();
    return;
  }
  g_di.drag_active = 1;
  g_di.drag_source = hit;
  g_di.drag_over = -1;
  g_di.drag_open_on_release = (g_di.selected == hit) ? 1 : 0;
  g_di.drag_moved = 0;
  g_di.drag_start_x = sx;
  g_di.drag_start_y = sy;
  g_di.selected = hit;
  compositor_invalidate_all();
}

void desktop_icons_handle_drag_move(int32_t sx, int32_t sy, uint8_t buttons) {
  int hit = -1;
  int next_over = -1;
  char src_path[256];
  if (!g_di.drag_active) return;
  if (!(buttons & 1)) {
    (void)desktop_icons_handle_mouse_up(sx, sy);
    return;
  }
  if (sx < g_di.drag_start_x - 2 || sx > g_di.drag_start_x + 2 ||
      sy < g_di.drag_start_y - 2 || sy > g_di.drag_start_y + 2) {
    g_di.drag_moved = 1;
  }
  hit = desktop_icons_hit_test(sx, sy);
  if (hit >= 0 && hit != g_di.drag_source &&
      (g_di.entries[hit].mode & VFS_MODE_DIR)) {
    next_over = hit;
  }
  if (g_di.drag_source >= 0 && g_di.drag_source < (int)g_di.count) {
    di_join(g_di.path, g_di.entries[g_di.drag_source].name, src_path,
            sizeof(src_path));
    if (file_manager_preview_drop_path_at(sx, sy, src_path)) {
      next_over = -1;
    }
  } else {
    file_manager_clear_external_drop();
  }
  if (next_over != g_di.drag_over) {
    g_di.drag_over = next_over;
    compositor_invalidate_all();
  }
}

int desktop_icons_handle_mouse_up(int32_t sx, int32_t sy) {
  int src = 0;
  int dst = 0;
  int open_on_release = 0;
  int moved = 0;
  char src_path[256];
  src_path[0] = '\0';
  if (!g_di.drag_active) return 0;
  src = g_di.drag_source;
  dst = g_di.drag_over;
  open_on_release = g_di.drag_open_on_release;
  moved = g_di.drag_moved;
  if (src >= 0 && src < (int)g_di.count) {
    di_join(g_di.path, g_di.entries[src].name, src_path, sizeof(src_path));
  }
  g_di.drag_active = 0;
  g_di.drag_source = -1;
  g_di.drag_over = -1;
  g_di.drag_open_on_release = 0;
  g_di.drag_moved = 0;
  g_di.drag_start_x = 0;
  g_di.drag_start_y = 0;
  file_manager_clear_external_drop();
  if (dst >= 0 && di_move_entry_to_dir(src, dst)) return 1;
  if (moved && src_path[0] && file_manager_drop_path_at(sx, sy, src_path)) {
    desktop_icons_refresh();
    return 1;
  }
  if (open_on_release && !moved) {
    di_open_entry(src);
    g_di.selected = -1;
  }
  compositor_invalidate_all();
  return 1;
}

int desktop_icons_preview_external_drop(int32_t sx, int32_t sy) {
  int hit = -1;
  int next_over = -1;
  if (compositor_window_at(sx, sy)) {
    desktop_icons_clear_external_drop();
    return 0;
  }
  hit = desktop_icons_hit_test(sx, sy);
  if (hit >= 0 && (g_di.entries[hit].mode & VFS_MODE_DIR)) {
    next_over = hit;
  }
  if (next_over != g_di.drag_over) {
    g_di.drag_over = next_over;
    compositor_invalidate_all();
  }
  return 1;
}

int desktop_icons_drop_path_at(int32_t sx, int32_t sy, const char *src_path) {
  int hit = -1;
  char dst_dir[256];
  if (!src_path || !src_path[0]) return 0;
  if (compositor_window_at(sx, sy)) {
    desktop_icons_clear_external_drop();
    return 0;
  }
  if ((uint32_t)sy >= di_usable_height()) {
    desktop_icons_clear_external_drop();
    return 0;
  }
  hit = desktop_icons_hit_test(sx, sy);
  if (hit >= 0 && (g_di.entries[hit].mode & VFS_MODE_DIR)) {
    di_join(g_di.path, g_di.entries[hit].name, dst_dir, sizeof(dst_dir));
  } else {
    di_strcpy(dst_dir, g_di.path, sizeof(dst_dir));
  }
  g_di.drag_over = -1;
  return di_move_path_to_dir(src_path, dst_dir);
}

void desktop_icons_clear_external_drop(void) {
  if (g_di.drag_over != -1 && !g_di.drag_active) {
    g_di.drag_over = -1;
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
