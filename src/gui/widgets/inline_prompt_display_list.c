#include "gui/inline_prompt.h"
#include "gui/capyui_display_adapter.h"
#include "gui/compositor.h"
#include "gui/font.h"
#include "lang/app_language.h"

#if defined(CAPYOS_HAVE_CAPYUI_WIDGET)
#include "capy_display_list.h"
#include "util/kstring.h"

#define INLINE_PROMPT_DL_FRAME_COUNT 2u
#define INLINE_PROMPT_DL_CMD_CAP 96u
#define INLINE_PROMPT_DL_TEXT_CAP 768u

static struct capy_dl_cmd g_ip_dl_cmds[INLINE_PROMPT_DL_FRAME_COUNT][INLINE_PROMPT_DL_CMD_CAP];
static char g_ip_dl_text[INLINE_PROMPT_DL_FRAME_COUNT][INLINE_PROMPT_DL_TEXT_CAP];
static struct capy_display_list g_ip_dl_lists[INLINE_PROMPT_DL_FRAME_COUNT];
static int g_ip_dl_initialized = 0;
static int g_ip_dl_have_prev = 0;
static uint8_t g_ip_dl_prev_index = 0u;

struct ip_dl_producer {
  struct gui_window *win;
  const char *title;
  const char *text;
  uint32_t cursor;
  int secret;
};

static void ip_dl_prepare(uint8_t index) {
  struct capy_display_list *dl = &g_ip_dl_lists[index];
  dl->cmds = g_ip_dl_cmds[index];
  dl->count = 0u;
  dl->capacity = INLINE_PROMPT_DL_CMD_CAP;
  dl->text_pool = g_ip_dl_text[index];
  dl->text_used = 0u;
  dl->text_capacity = INLINE_PROMPT_DL_TEXT_CAP;
  dl->version = CAPY_DISPLAY_LIST_SCHEMA_VERSION;
  dl->theme = 0;
  dl->dpi_scale_x256 = 256u;
  dl->reserved_dpi = 0u;
}

static void ip_dl_init_once(void) {
  if (g_ip_dl_initialized) return;
  ip_dl_prepare(0u);
  ip_dl_prepare(1u);
  g_ip_dl_initialized = 1;
  g_ip_dl_have_prev = 0;
  g_ip_dl_prev_index = 0u;
}

void inline_prompt_display_list_reset(void) {
  g_ip_dl_have_prev = 0;
}

static struct capy_dl_cmd *ip_dl_push(struct capy_display_list *dl) {
  struct capy_dl_cmd *cmd;
  if (!dl || !dl->cmds || dl->count >= dl->capacity) return 0;
  cmd = &dl->cmds[dl->count++];
  kmemzero(cmd, sizeof(*cmd));
  return cmd;
}

static int ip_dl_emit_rect(struct capy_display_list *dl,
                           int32_t x,
                           int32_t y,
                           uint32_t width,
                           uint32_t height,
                           uint32_t color) {
  struct capy_dl_cmd *cmd;
  if (width == 0u || height == 0u) return 0;
  cmd = ip_dl_push(dl);
  if (!cmd) return -1;
  cmd->op = CAPY_DL_RECT;
  cmd->rect.x = x;
  cmd->rect.y = y;
  cmd->rect.width = width;
  cmd->rect.height = height;
  cmd->color = color;
  return 0;
}

static int ip_dl_copy_text(struct capy_display_list *dl,
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

static int ip_dl_emit_text(struct capy_display_list *dl,
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
  if (ip_dl_copy_text(dl, text, &text_offset, &text_len) != 0) return -1;
  cmd = ip_dl_push(dl);
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

static uint32_t ip_dl_strlen(const char *s) {
  uint32_t n = 0u;
  while (s && s[n]) n++;
  return n;
}

static void ip_dl_strcpy(char *dst, const char *src, uint32_t max) {
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

static void ip_dl_fit_text(const struct font *f,
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
  if (len <= max_chars) {
    ip_dl_strcpy(out, src, out_len);
    return;
  }
  if (max_chars <= 3u) {
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

static int ip_dl_emit_fit(struct capy_display_list *dl,
                          const struct font *f,
                          int32_t x,
                          int32_t y,
                          uint32_t max_width,
                          const char *text,
                          uint32_t color) {
  char fitted[80];
  ip_dl_fit_text(f, text, max_width, fitted, sizeof(fitted));
  if (!fitted[0]) return 0;
  return ip_dl_emit_text(dl, x, y, max_width, f->glyph_height, fitted, color);
}

static uint32_t ip_dl_visible_start(uint32_t cursor, uint32_t visible_chars) {
  if (visible_chars == 0u || cursor <= visible_chars) return 0u;
  return cursor - visible_chars;
}

static void ip_dl_visible_text(const char *text,
                               int secret,
                               uint32_t start,
                               uint32_t count,
                               char *out,
                               uint32_t out_len) {
  uint32_t len = ip_dl_strlen(text);
  uint32_t copied = 0u;
  if (!out || out_len == 0u) return;
  out[0] = '\0';
  if (start > len) start = len;
  while (copied < count && copied + 1u < out_len && text && text[start + copied]) {
    out[copied] = secret ? '*' : text[start + copied];
    copied++;
  }
  out[copied] = '\0';
}

static int ip_emit_display_list(void *producer, struct capy_display_list *out) {
  const struct ip_dl_producer *p = (const struct ip_dl_producer *)producer;
  const struct gui_theme_palette *theme = compositor_theme();
  const struct font *f = font_default();
  int32_t bx = 8;
  int32_t by = 24;
  int32_t bw;
  int32_t bh = 18;
  uint32_t gw;
  uint32_t visible_chars;
  uint32_t start;
  char visible[INLINE_PROMPT_TEXT_MAX];
  int32_t cx;
  if (!out || !p || !p->win || !theme || !f) return -1;
  bw = (int32_t)p->win->surface.width - 16;
  gw = f->glyph_width ? f->glyph_width : 8u;
  visible_chars = (bw > 8 && gw > 0u) ? (uint32_t)(bw - 8) / gw : 0u;
  start = ip_dl_visible_start(p->cursor, visible_chars);
  out->count = 0u;
  out->text_used = 0u;
  if (ip_dl_emit_rect(out, 0, 0, p->win->surface.width, p->win->surface.height,
                      theme->window_bg) != 0) return -1;
  if (ip_dl_emit_rect(out, 0, 0, p->win->surface.width, 4u,
                      theme->accent) != 0) return -1;
  if (ip_dl_emit_fit(out, f, 8, 8,
                     p->win->surface.width > 16u ? p->win->surface.width - 16u : 0u,
                     p->title, theme->text) != 0) return -1;
  if (bw > 0) {
    if (ip_dl_emit_rect(out, bx, by, (uint32_t)bw, (uint32_t)bh,
                        theme->terminal_bg) != 0) return -1;
    if (ip_dl_emit_rect(out, bx, by, (uint32_t)bw, 1u,
                        theme->accent_alt) != 0) return -1;
    if (ip_dl_emit_rect(out, bx, by + bh - 1, (uint32_t)bw, 1u,
                        theme->accent_alt) != 0) return -1;
    if (ip_dl_emit_rect(out, bx, by, 1u, (uint32_t)bh,
                        theme->accent_alt) != 0) return -1;
    if (ip_dl_emit_rect(out, bx + bw - 1, by, 1u, (uint32_t)bh,
                        theme->accent_alt) != 0) return -1;
  }
  ip_dl_visible_text(p->text, p->secret, start, visible_chars, visible,
                     sizeof(visible));
  if (ip_dl_emit_text(out, bx + 4, by + 4,
                      bw > 8 ? (uint32_t)bw - 8u : 0u,
                      f->glyph_height, visible,
                      theme->terminal_fg) != 0) return -1;
  cx = bx + 4 + (int32_t)((p->cursor - start) * gw);
  if (bw > 0 && cx < bx + bw - 2) {
    if (ip_dl_emit_rect(out, cx, by + 3, 1u, (uint32_t)bh - 6u,
                        theme->accent) != 0) return -1;
  }
  if (p->win->surface.height > 50u) {
    if (ip_dl_emit_fit(out, f, 8, 46,
                       p->win->surface.width > 16u ? p->win->surface.width - 16u : 0u,
                       APP_T("Enter: ok   Esc: cancelar",
                             "Enter: ok   Esc: cancel",
                             "Enter: ok   Esc: cancelar"),
                       theme->text_muted) != 0) return -1;
  }
  return 0;
}

int inline_prompt_render_display_list(struct gui_window *win,
                                      const char *title,
                                      const char *text,
                                      uint32_t cursor,
                                      int secret) {
  const struct capy_display_list *prev;
  struct ip_dl_producer producer;
  uint8_t next;
  int rc;
  if (!win) return -1;
  ip_dl_init_once();
  next = g_ip_dl_have_prev ? (uint8_t)(g_ip_dl_prev_index ^ 1u) : 0u;
  ip_dl_prepare(next);
  producer.win = win;
  producer.title = title ? title : "";
  producer.text = text ? text : "";
  producer.cursor = cursor;
  producer.secret = secret ? 1 : 0;
  prev = g_ip_dl_have_prev ? &g_ip_dl_lists[g_ip_dl_prev_index] : 0;
  rc = capyui_display_adapter_render_producer_window(win, prev,
                                                      &g_ip_dl_lists[next],
                                                      ip_emit_display_list,
                                                      &producer,
                                                      0);
  if (rc != CAPYUI_DISPLAY_ADAPTER_OK) return rc;
  g_ip_dl_prev_index = next;
  g_ip_dl_have_prev = 1;
  return CAPYUI_DISPLAY_ADAPTER_OK;
}
#endif
