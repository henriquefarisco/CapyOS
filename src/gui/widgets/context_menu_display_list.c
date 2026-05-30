#include "gui/context_menu.h"
#include "gui/capyui_display_adapter.h"
#include "gui/compositor.h"
#include "gui/font.h"

#if defined(CAPYOS_HAVE_CAPYUI_WIDGET)
#include "capy_display_list.h"
#include "util/kstring.h"

#define CONTEXT_MENU_DL_FRAME_COUNT 2u
#define CONTEXT_MENU_DL_CMD_CAP 96u
#define CONTEXT_MENU_DL_TEXT_CAP 512u

static struct capy_dl_cmd g_cm_dl_cmds[CONTEXT_MENU_DL_FRAME_COUNT][CONTEXT_MENU_DL_CMD_CAP];
static char g_cm_dl_text[CONTEXT_MENU_DL_FRAME_COUNT][CONTEXT_MENU_DL_TEXT_CAP];
static struct capy_display_list g_cm_dl_lists[CONTEXT_MENU_DL_FRAME_COUNT];
static int g_cm_dl_initialized = 0;
static int g_cm_dl_have_prev = 0;
static uint8_t g_cm_dl_prev_index = 0u;

struct cm_dl_producer {
  struct gui_window *win;
  const struct context_menu_item *items;
  uint32_t count;
  int32_t hover_index;
};

static void cm_dl_prepare(uint8_t index) {
  struct capy_display_list *dl = &g_cm_dl_lists[index];
  dl->cmds = g_cm_dl_cmds[index];
  dl->count = 0u;
  dl->capacity = CONTEXT_MENU_DL_CMD_CAP;
  dl->text_pool = g_cm_dl_text[index];
  dl->text_used = 0u;
  dl->text_capacity = CONTEXT_MENU_DL_TEXT_CAP;
  dl->version = CAPY_DISPLAY_LIST_SCHEMA_VERSION;
  dl->theme = 0;
  dl->dpi_scale_x256 = 256u;
  dl->reserved_dpi = 0u;
}

static void cm_dl_init_once(void) {
  if (g_cm_dl_initialized) return;
  cm_dl_prepare(0u);
  cm_dl_prepare(1u);
  g_cm_dl_initialized = 1;
  g_cm_dl_have_prev = 0;
  g_cm_dl_prev_index = 0u;
}

void context_menu_display_list_reset(void) {
  g_cm_dl_have_prev = 0;
}

static struct capy_dl_cmd *cm_dl_push(struct capy_display_list *dl) {
  struct capy_dl_cmd *cmd;
  if (!dl || !dl->cmds || dl->count >= dl->capacity) return 0;
  cmd = &dl->cmds[dl->count++];
  kmemzero(cmd, sizeof(*cmd));
  return cmd;
}

static int cm_dl_emit_rect(struct capy_display_list *dl,
                           int32_t x,
                           int32_t y,
                           uint32_t width,
                           uint32_t height,
                           uint32_t color) {
  struct capy_dl_cmd *cmd;
  if (width == 0u || height == 0u) return 0;
  cmd = cm_dl_push(dl);
  if (!cmd) return -1;
  cmd->op = CAPY_DL_RECT;
  cmd->rect.x = x;
  cmd->rect.y = y;
  cmd->rect.width = width;
  cmd->rect.height = height;
  cmd->color = color;
  return 0;
}

static int cm_dl_copy_text(struct capy_display_list *dl,
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

static int cm_dl_emit_text(struct capy_display_list *dl,
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
  if (cm_dl_copy_text(dl, text, &text_offset, &text_len) != 0) return -1;
  cmd = cm_dl_push(dl);
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

static uint32_t cm_dl_lighten(uint32_t color, uint8_t amount) {
  uint32_t r = (color >> 16) & 0xFFu;
  uint32_t g = (color >> 8) & 0xFFu;
  uint32_t b = color & 0xFFu;
  uint32_t a = color & 0xFF000000u;
  r = r + ((255u - r) * amount) / 255u;
  g = g + ((255u - g) * amount) / 255u;
  b = b + ((255u - b) * amount) / 255u;
  if (r > 255u) r = 255u;
  if (g > 255u) g = 255u;
  if (b > 255u) b = 255u;
  return a | (r << 16) | (g << 8) | b;
}

static void cm_dl_strcpy(char *dst, const char *src, uint32_t max) {
  uint32_t i = 0u;
  if (!dst || max == 0u) return;
  if (src) {
    while (i + 1u < max && src[i]) {
      dst[i] = src[i];
      i++;
    }
  }
  dst[i] = '\0';
}

static void cm_dl_fit_text(const struct font *f,
                           const char *src,
                           uint32_t max_width,
                           char *out,
                           uint32_t out_len) {
  uint32_t len = 0u;
  uint32_t max_chars = 0u;
  if (!out || out_len == 0u) return;
  out[0] = '\0';
  if (!f || !src || max_width == 0u || f->glyph_width == 0u) return;
  max_chars = max_width / f->glyph_width;
  if (max_chars == 0u) return;
  while (src[len]) len++;
  if (len <= max_chars && len < out_len) {
    cm_dl_strcpy(out, src, out_len);
    return;
  }
  if (max_chars <= 3u || out_len <= 4u) {
    uint32_t n = max_chars;
    if (n >= out_len) n = out_len - 1u;
    for (uint32_t i = 0u; i < n; ++i) out[i] = '.';
    out[n] = '\0';
    return;
  }
  {
    uint32_t copy = max_chars - 3u;
    if (copy > out_len - 4u) copy = out_len - 4u;
    for (uint32_t i = 0u; i < copy; ++i) out[i] = src[i];
    out[copy] = '.';
    out[copy + 1u] = '.';
    out[copy + 2u] = '.';
    out[copy + 3u] = '\0';
  }
}

static int cm_dl_emit_fit(struct capy_display_list *dl,
                          const struct font *f,
                          int32_t x,
                          int32_t y,
                          uint32_t max_width,
                          const char *text,
                          uint32_t color) {
  char fitted[CONTEXT_MENU_LABEL_MAX];
  cm_dl_fit_text(f, text, max_width, fitted, sizeof(fitted));
  if (!fitted[0]) return 0;
  return cm_dl_emit_text(dl, x, y, max_width, f->glyph_height, fitted, color);
}

static int cm_emit_display_list(void *producer, struct capy_display_list *out) {
  const struct cm_dl_producer *p = (const struct cm_dl_producer *)producer;
  const struct gui_theme_palette *theme = compositor_theme();
  const struct font *f = font_default();
  int32_t ey = 2;
  if (!out || !p || !p->win || !p->items || !theme) return -1;
  out->count = 0u;
  out->text_used = 0u;
  if (cm_dl_emit_rect(out, 0, 0, p->win->surface.width, p->win->surface.height,
                      theme->window_bg) != 0) return -1;
  if (!f) return 0;
  for (uint32_t i = 0u; i < p->count; ++i) {
    int sep = (p->items[i].label[0] == '\0');
    uint32_t row_h = sep ? CONTEXT_MENU_SEP_H : CONTEXT_MENU_ITEM_H;
    if (sep) {
      uint32_t line_w = p->win->surface.width > 16u ? p->win->surface.width - 16u : 0u;
      if (cm_dl_emit_rect(out, 8, ey + (int32_t)(row_h / 2u), line_w, 1u,
                          theme->window_border) != 0) return -1;
    } else {
      uint32_t text_color = p->items[i].enabled ? theme->text : theme->text_muted;
      if ((int32_t)i == p->hover_index && p->items[i].enabled) {
        uint32_t hover_bg = cm_dl_lighten(theme->window_bg, 50u);
        uint32_t hover_w = p->win->surface.width > 4u ? p->win->surface.width - 4u : 0u;
        if (cm_dl_emit_rect(out, 2, ey, hover_w, row_h, hover_bg) != 0) return -1;
        if (cm_dl_emit_rect(out, 2, ey, 3u, row_h, theme->accent) != 0) return -1;
        text_color = theme->accent;
      }
      if (cm_dl_emit_fit(out, f, 12, ey + 4,
                         p->win->surface.width > 24u ? p->win->surface.width - 24u : 0u,
                         p->items[i].label, text_color) != 0) return -1;
    }
    ey += (int32_t)row_h;
  }
  return 0;
}

int context_menu_render_display_list(struct gui_window *win,
                                     const struct context_menu_item *items,
                                     uint32_t count,
                                     int32_t hover_index) {
  const struct capy_display_list *prev;
  struct cm_dl_producer producer;
  uint8_t next;
  int rc;
  if (!win || !items || count > CONTEXT_MENU_MAX_ITEMS) return -1;
  cm_dl_init_once();
  next = g_cm_dl_have_prev ? (uint8_t)(g_cm_dl_prev_index ^ 1u) : 0u;
  cm_dl_prepare(next);
  producer.win = win;
  producer.items = items;
  producer.count = count;
  producer.hover_index = hover_index;
  prev = g_cm_dl_have_prev ? &g_cm_dl_lists[g_cm_dl_prev_index] : 0;
  rc = capyui_display_adapter_render_producer_window(win, prev,
                                                      &g_cm_dl_lists[next],
                                                      cm_emit_display_list,
                                                      &producer,
                                                      0);
  if (rc != CAPYUI_DISPLAY_ADAPTER_OK) return rc;
  g_cm_dl_prev_index = next;
  g_cm_dl_have_prev = 1;
  return CAPYUI_DISPLAY_ADAPTER_OK;
}
#endif
