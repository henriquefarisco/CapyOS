#include "gui/widget.h"
#include "gui/capyui_display_adapter.h"
#include "gui/font.h"
#include "util/kstring.h"

#if defined(CAPYOS_HAVE_CAPYUI_WIDGET)
#include "capy_display_list.h"

#define WIDGET_DL_CMD_CAP 1024u
#define WIDGET_DL_TEXT_CAP 8192u

static struct capy_dl_cmd g_widget_dl_cmds[WIDGET_DL_CMD_CAP];
static char g_widget_dl_text[WIDGET_DL_TEXT_CAP];
static struct capy_display_list g_widget_dl;

static void widget_dl_prepare(void) {
  g_widget_dl.cmds = g_widget_dl_cmds;
  g_widget_dl.count = 0u;
  g_widget_dl.capacity = WIDGET_DL_CMD_CAP;
  g_widget_dl.text_pool = g_widget_dl_text;
  g_widget_dl.text_used = 0u;
  g_widget_dl.text_capacity = WIDGET_DL_TEXT_CAP;
  g_widget_dl.version = CAPY_DISPLAY_LIST_SCHEMA_VERSION;
  g_widget_dl.theme = 0;
  g_widget_dl.dpi_scale_x256 = 256u;
  g_widget_dl.reserved_dpi = 0u;
}

static struct capy_dl_cmd *widget_dl_push(struct capy_display_list *dl) {
  struct capy_dl_cmd *cmd;
  if (!dl || !dl->cmds || dl->count >= dl->capacity) return 0;
  cmd = &dl->cmds[dl->count++];
  kmemzero(cmd, sizeof(*cmd));
  return cmd;
}

static int widget_dl_emit_rect(struct capy_display_list *dl,
                               int32_t x,
                               int32_t y,
                               uint32_t width,
                               uint32_t height,
                               uint32_t color) {
  struct capy_dl_cmd *cmd;
  if (width == 0u || height == 0u) return 0;
  cmd = widget_dl_push(dl);
  if (!cmd) return -1;
  cmd->op = CAPY_DL_RECT;
  cmd->rect.x = x;
  cmd->rect.y = y;
  cmd->rect.width = width;
  cmd->rect.height = height;
  cmd->color = color;
  return 0;
}

static int widget_dl_emit_border_rects(struct capy_display_list *dl,
                                       int32_t x,
                                       int32_t y,
                                       uint32_t width,
                                       uint32_t height,
                                       uint32_t color,
                                       uint8_t border_width) {
  if (border_width == 0u) return 0;
  if (widget_dl_emit_rect(dl, x, y, width, border_width, color) != 0) return -1;
  if (widget_dl_emit_rect(dl, x, y + (int32_t)height - border_width, width,
                          border_width, color) != 0) return -1;
  if (widget_dl_emit_rect(dl, x, y, border_width, height, color) != 0) return -1;
  if (widget_dl_emit_rect(dl, x + (int32_t)width - border_width, y,
                          border_width, height, color) != 0) return -1;
  return 0;
}

static int widget_dl_copy_text(struct capy_display_list *dl,
                               const char *text,
                               uint16_t *offset,
                               uint16_t *out_len) {
  uint32_t len = 0u;
  if (!dl || !offset || !out_len) return -1;
  *offset = 0u;
  *out_len = 0u;
  if (!text || !text[0]) return 0;
  while (text[len] && len < 0xFFFFu) ++len;
  if (!dl->text_pool || dl->text_used + len > dl->text_capacity) return -1;
  *offset = (uint16_t)dl->text_used;
  *out_len = (uint16_t)len;
  for (uint32_t i = 0u; i < len; ++i) dl->text_pool[dl->text_used + i] = text[i];
  dl->text_used += len;
  return 0;
}

static int widget_dl_emit_text(struct capy_display_list *dl,
                               int32_t x,
                               int32_t y,
                               uint32_t width,
                               uint32_t height,
                               const char *text,
                               uint32_t color) {
  struct capy_dl_cmd *cmd;
  uint16_t text_offset = 0u;
  uint16_t text_len = 0u;
  if (!text || !text[0]) return 0;
  if (widget_dl_copy_text(dl, text, &text_offset, &text_len) != 0) return -1;
  cmd = widget_dl_push(dl);
  if (!cmd) return -1;
  cmd->op = CAPY_DL_TEXT;
  cmd->rect.x = x;
  cmd->rect.y = y;
  cmd->rect.width = width;
  cmd->rect.height = height;
  cmd->color = color;
  cmd->text_offset = text_offset;
  cmd->text_len = text_len;
  cmd->font_size = 16u;
  return 0;
}

static void widget_dl_fit_text_for_width(const struct font *f,
                                         const char *src,
                                         uint32_t max_width,
                                         char *out,
                                         size_t out_len) {
  size_t len = 0u;
  size_t max_chars = 0u;
  if (!out || out_len == 0u) return;
  out[0] = '\0';
  if (!f || !src || max_width == 0u || f->glyph_width == 0u) return;
  max_chars = max_width / f->glyph_width;
  if (max_chars == 0u) return;
  while (src[len]) len++;
  if (len <= max_chars) {
    kstrcpy(out, out_len, src);
    return;
  }
  if (max_chars <= 3u) {
    size_t n = max_chars;
    if (n >= out_len) n = out_len - 1u;
    for (size_t i = 0u; i < n; ++i) out[i] = '.';
    out[n] = '\0';
    return;
  }
  {
    size_t copy = max_chars - 3u;
    if (copy > out_len - 4u) copy = out_len - 4u;
    for (size_t i = 0u; i < copy; ++i) out[i] = src[i];
    out[copy] = '.';
    out[copy + 1u] = '.';
    out[copy + 2u] = '.';
    out[copy + 3u] = '\0';
  }
}

static int widget_dl_emit_label(struct capy_display_list *dl,
                                const struct widget *w,
                                const struct font *f) {
  char fitted[WIDGET_MAX_TEXT];
  uint32_t text_area = 0u;
  int32_t tx = w->bounds.x + w->style.padding + w->style.border_width;
  int32_t ty;
  const char *label = w->text;
  uint32_t text_width;
  if (!f || !w->text[0]) return 0;
  ty = w->bounds.y + (int32_t)(w->bounds.height / 2u) -
       (int32_t)(f->glyph_height / 2u);
  if (w->bounds.width > (uint32_t)(2u * w->style.border_width + 4u)) {
    text_area = w->bounds.width - (uint32_t)(2u * w->style.border_width + 4u);
  }
  widget_dl_fit_text_for_width(f, w->text, text_area, fitted, sizeof(fitted));
  if (fitted[0]) label = fitted;
  text_width = font_string_width(f, label);
  if (w->type == WIDGET_BUTTON) {
    if (text_width < w->bounds.width) {
      tx = w->bounds.x + (int32_t)((w->bounds.width - text_width) / 2u);
    } else {
      tx = w->bounds.x + 2;
    }
  }
  return widget_dl_emit_text(dl, tx, ty, text_width, f->glyph_height, label,
                             w->style.text_color);
}

static int widget_dl_emit_checkbox(struct capy_display_list *dl,
                                   const struct widget *w) {
  int32_t cx = w->bounds.x + (int32_t)w->bounds.width - 20;
  int32_t cy = w->bounds.y + (int32_t)(w->bounds.height / 2u) - 6;
  const struct gui_theme_palette *theme = compositor_theme();
  if (widget_dl_emit_rect(dl, cx, cy, 12u, 12u, theme->window_bg) != 0) return -1;
  if (widget_dl_emit_border_rects(dl, cx, cy, 12u, 12u, theme->window_border,
                                  1u) != 0) return -1;
  if (w->checked) {
    if (widget_dl_emit_rect(dl, cx + 3, cy + 3, 6u, 6u, theme->accent) != 0) {
      return -1;
    }
  }
  return 0;
}

static int widget_dl_emit_progress(struct capy_display_list *dl,
                                   const struct widget *w) {
  int32_t bx = w->bounds.x + w->style.padding;
  int32_t by = w->bounds.y + (int32_t)w->bounds.height - 12;
  uint32_t inset = 2u * w->style.padding;
  uint32_t bar_w = w->bounds.width > inset ? w->bounds.width - inset : 0u;
  uint32_t fill_w = 0u;
  const struct gui_theme_palette *theme = compositor_theme();
  if (widget_dl_emit_rect(dl, bx, by, bar_w, 8u, theme->accent_alt) != 0) return -1;
  if (w->max_value > w->min_value) {
    if (w->value <= w->min_value) {
      fill_w = 0u;
    } else if (w->value >= w->max_value) {
      fill_w = bar_w;
    } else {
      fill_w = (uint32_t)((int64_t)(w->value - w->min_value) *
                          (int64_t)bar_w /
                          (w->max_value - w->min_value));
    }
  }
  if (widget_dl_emit_rect(dl, bx, by, fill_w, 8u, theme->accent) != 0) return -1;
  return 0;
}

static int widget_dl_emit_tree(struct capy_display_list *dl,
                               const struct widget *w,
                               const struct font *f) {
  uint32_t bg;
  if (!w || !w->visible) return 0;
  bg = w->hovered ? w->style.hover_color : w->style.bg_color;
  if (!w->enabled) bg = 0xC0C0C0u;
  if (widget_dl_emit_rect(dl, w->bounds.x, w->bounds.y, w->bounds.width,
                          w->bounds.height, bg) != 0) return -1;
  if (widget_dl_emit_border_rects(dl, w->bounds.x, w->bounds.y,
                                  w->bounds.width, w->bounds.height,
                                  w->style.border_color,
                                  w->style.border_width) != 0) return -1;
  if (widget_dl_emit_label(dl, w, f) != 0) return -1;
  if (w->type == WIDGET_CHECKBOX && widget_dl_emit_checkbox(dl, w) != 0) return -1;
  if (w->type == WIDGET_PROGRESS && widget_dl_emit_progress(dl, w) != 0) return -1;
  for (uint32_t i = 0u; i < w->child_count; ++i) {
    if (widget_dl_emit_tree(dl, w->children[i], f) != 0) return -1;
  }
  return 0;
}

int widget_render_display_list(struct widget *w, struct gui_surface *surface) {
  const struct font *f;
  if (!w || !surface || !surface->pixels || !w->visible) return -1;
  f = font_default();
  widget_dl_prepare();
  if (widget_dl_emit_tree(&g_widget_dl, w, f) != 0) return -1;
  return capyui_display_adapter_render(&g_widget_dl, surface, 0, 0);
}
#endif
