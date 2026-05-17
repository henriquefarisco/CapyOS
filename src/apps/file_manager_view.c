/*
 * src/apps/file_manager_view.c
 *
 * Drawing primitives and the main paint routine for the file manager
 * window. Carved out of `src/apps/file_manager.c` at the 2026-05-15
 * refactor so each translation unit stays under the 900-line layout
 * limit.
 *
 * Public symbols owned here:
 *   - `file_manager_paint` (declared in `include/apps/file_manager.h`)
 *
 * Internal helpers exposed via `internal/file_manager_internal.h`:
 *   - `fm_row_height`, `fm_fit_text`, `fm_draw_fit`, `fm_fill_rect`
 *   - `fm_toolbar_layout`, `fm_paint_button`, `fm_row_at`
 *
 * All other concerns (lifecycle, ops, drag-and-drop) live in sister
 * files: `file_manager.c` and `file_manager_dnd.c`.
 */
#include "apps/file_manager.h"
#include "gui/compositor.h"
#include "gui/font.h"
#include "lang/app_language.h"
#include "util/kstring.h"

#include "apps/internal/file_manager_internal.h"

#include <stddef.h>
#include <stdint.h>

int32_t fm_row_height(const struct font *f) {
  return f ? (int32_t)f->glyph_height + 4 : 20;
}

void fm_fit_text(const struct font *f, const char *src, uint32_t max_width,
                 char *out, size_t out_len) {
  size_t len = 0;
  size_t max_chars = 0;
  if (!out || out_len == 0) return;
  out[0] = '\0';
  if (!f || !src || !src[0]) return;
  if (f->glyph_width == 0 || max_width == 0) return;
  max_chars = max_width / f->glyph_width;
  if (max_chars == 0) return;
  if (max_chars >= out_len) max_chars = out_len - 1;
  while (src[len] && len < max_chars) {
    out[len] = src[len];
    ++len;
  }
  out[len] = '\0';
  /* Add ellipsis "..." if truncated */
  if (src[len] && max_chars >= 4) {
    size_t k = max_chars - 3;
    out[k++] = '.';
    out[k++] = '.';
    out[k++] = '.';
    out[k] = '\0';
  }
}

void fm_draw_fit(struct gui_surface *s, const struct font *f, int32_t x,
                 int32_t y, uint32_t max_width, const char *text,
                 uint32_t color) {
  char fitted[96];
  fm_fit_text(f, text, max_width, fitted, sizeof(fitted));
  if (fitted[0]) font_draw_string(s, f, x, y, fitted, color);
}

void fm_fill_rect(struct gui_surface *s, int32_t x, int32_t y, uint32_t w,
                  uint32_t h, uint32_t color) {
  if (!s) return;
  for (uint32_t r = 0; r < h; r++) {
    int32_t py = y + (int32_t)r;
    if (py < 0 || (uint32_t)py >= s->height) continue;
    uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + (uint32_t)py * s->pitch);
    for (uint32_t c = 0; c < w; c++) {
      int32_t px = x + (int32_t)c;
      if (px < 0 || (uint32_t)px >= s->width) continue;
      line[px] = color;
    }
  }
}

void fm_toolbar_layout(uint32_t width, int32_t *out_x, int32_t *out_w,
                       int32_t *out_gap) {
  int32_t button_w = 60;
  int32_t gap = 5;
  int32_t toolbar_x = (int32_t)width - button_w * 6 - gap * 5 - 8;
  if (toolbar_x < 36) {
    button_w = (((int32_t)width - 36) - gap * 5) / 6;
    if (button_w < 24) button_w = 24;
    toolbar_x = 36;
  }
  if (out_x) *out_x = toolbar_x;
  if (out_w) *out_w = button_w;
  if (out_gap) *out_gap = gap;
}

void fm_paint_button(struct gui_surface *s, const struct font *f, int32_t x,
                     int32_t y, int32_t w, const char *label, uint32_t bg,
                     uint32_t fg, uint32_t border) {
  if (!s) return;
  fm_fill_rect(s, x, y, (uint32_t)w, FM_TOOLBAR_H - 8u, bg);
  fm_fill_rect(s, x, y, (uint32_t)w, 1u, border);
  fm_fill_rect(s, x, y + FM_TOOLBAR_H - 9, (uint32_t)w, 1u, border);
  fm_fill_rect(s, x, y, 1u, FM_TOOLBAR_H - 8u, border);
  fm_fill_rect(s, x + w - 1, y, 1u, FM_TOOLBAR_H - 8u, border);
  if (label) fm_draw_fit(s, f, x + 4, y + 5, (uint32_t)w - 8, label, fg);
}

int fm_row_at(struct file_manager_app *app, int32_t x, int32_t y) {
  int32_t row_h = 0;
  int32_t status_y = 0;
  int idx = -1;
  if (!app || !app->window) return -1;
  if (x < 0 || y < FM_TOOLBAR_H + 4) return -1;
  row_h = fm_row_height(font_default());
  if (row_h <= 0) return -1;
  status_y = (app->window->surface.height > 18u)
                 ? (int32_t)app->window->surface.height - 18 : 0;
  if (y >= status_y) return -1;
  idx = app->scroll_offset + (y - (FM_TOOLBAR_H + 4)) / row_h;
  return (idx >= 0 && idx < app->entry_count) ? idx : -1;
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
