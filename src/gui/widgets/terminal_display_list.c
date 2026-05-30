#include "gui/terminal.h"
#include "gui/capyui_display_adapter.h"

#if defined(CAPYOS_HAVE_CAPYUI_WIDGET)
#include "capy_display_list.h"
#include "util/kstring.h"

#define TERMINAL_DL_FRAME_COUNT 2u
#define TERMINAL_DL_CMD_CAP 4096u
#define TERMINAL_DL_TEXT_CAP 16384u

static struct capy_dl_cmd g_terminal_dl_cmds[TERMINAL_DL_FRAME_COUNT][TERMINAL_DL_CMD_CAP];
static char g_terminal_dl_text[TERMINAL_DL_FRAME_COUNT][TERMINAL_DL_TEXT_CAP];
static struct capy_display_list g_terminal_dl_lists[TERMINAL_DL_FRAME_COUNT];
static int g_terminal_dl_initialized = 0;
static int g_terminal_dl_have_prev = 0;
static uint8_t g_terminal_dl_prev_index = 0u;
static uint32_t g_terminal_dl_prev_window_id = 0u;

static void terminal_dl_prepare(uint8_t index) {
  struct capy_display_list *dl = &g_terminal_dl_lists[index];
  dl->cmds = g_terminal_dl_cmds[index];
  dl->count = 0u;
  dl->capacity = TERMINAL_DL_CMD_CAP;
  dl->text_pool = g_terminal_dl_text[index];
  dl->text_used = 0u;
  dl->text_capacity = TERMINAL_DL_TEXT_CAP;
  dl->version = CAPY_DISPLAY_LIST_SCHEMA_VERSION;
  dl->theme = 0;
  dl->dpi_scale_x256 = 256u;
  dl->reserved_dpi = 0u;
}

static void terminal_dl_init_once(void) {
  if (g_terminal_dl_initialized) return;
  terminal_dl_prepare(0u);
  terminal_dl_prepare(1u);
  g_terminal_dl_initialized = 1;
  g_terminal_dl_have_prev = 0;
  g_terminal_dl_prev_index = 0u;
}

void terminal_display_list_reset(void) {
  g_terminal_dl_have_prev = 0;
  g_terminal_dl_prev_window_id = 0u;
}

static struct capy_dl_cmd *terminal_dl_push(struct capy_display_list *dl) {
  struct capy_dl_cmd *cmd;
  if (!dl || !dl->cmds || dl->count >= dl->capacity) return 0;
  cmd = &dl->cmds[dl->count++];
  kmemzero(cmd, sizeof(*cmd));
  return cmd;
}

static int terminal_dl_emit_rect(struct capy_display_list *dl,
                                 int32_t x,
                                 int32_t y,
                                 uint32_t width,
                                 uint32_t height,
                                 uint32_t color) {
  struct capy_dl_cmd *cmd;
  if (width == 0u || height == 0u) return 0;
  cmd = terminal_dl_push(dl);
  if (!cmd) return -1;
  cmd->op = CAPY_DL_RECT;
  cmd->rect.x = x;
  cmd->rect.y = y;
  cmd->rect.width = width;
  cmd->rect.height = height;
  cmd->color = color;
  return 0;
}

static int terminal_dl_copy_text(struct capy_display_list *dl,
                                 const char *text,
                                 uint32_t len,
                                 uint16_t *offset,
                                 uint16_t *out_len) {
  if (!dl || !offset || !out_len) return -1;
  *offset = 0u;
  *out_len = 0u;
  if (!text || len == 0u) return 0;
  if (len > 0xFFFFu) return -1;
  if (!dl->text_pool || dl->text_used + len > dl->text_capacity) return -1;
  *offset = (uint16_t)dl->text_used;
  *out_len = (uint16_t)len;
  for (uint32_t i = 0u; i < len; ++i) dl->text_pool[dl->text_used + i] = text[i];
  dl->text_used += len;
  return 0;
}

static int terminal_dl_emit_text_span(struct capy_display_list *dl,
                                      int32_t x,
                                      int32_t y,
                                      const char *text,
                                      uint32_t len,
                                      uint32_t color,
                                      uint32_t glyph_width,
                                      uint32_t glyph_height) {
  struct capy_dl_cmd *cmd;
  uint16_t text_offset = 0u;
  uint16_t text_len = 0u;
  if (!text || len == 0u || glyph_width == 0u || glyph_height == 0u) return 0;
  if (terminal_dl_copy_text(dl, text, len, &text_offset, &text_len) != 0) return -1;
  cmd = terminal_dl_push(dl);
  if (!cmd) return -1;
  cmd->op = CAPY_DL_TEXT;
  cmd->rect.x = x;
  cmd->rect.y = y;
  cmd->rect.width = len * glyph_width;
  cmd->rect.height = glyph_height;
  cmd->color = color;
  cmd->text_offset = text_offset;
  cmd->text_len = text_len;
  cmd->font_size = 16u;
  return 0;
}

static int terminal_dl_emit_bg_run(struct capy_display_list *dl,
                                   struct terminal *term,
                                   uint32_t row,
                                   uint32_t col,
                                   uint32_t glyph_width,
                                   uint32_t glyph_height,
                                   uint32_t base_bg) {
  uint32_t bg = term->cells[row][col].bg;
  uint32_t start = col;
  while (col < term->cols && term->cells[row][col].bg == bg) col++;
  if (bg == base_bg) return 0;
  return terminal_dl_emit_rect(dl,
                               (int32_t)(start * glyph_width),
                               (int32_t)(row * glyph_height),
                               (col - start) * glyph_width,
                               glyph_height,
                               bg);
}

static int terminal_dl_emit_text_run(struct capy_display_list *dl,
                                     struct terminal *term,
                                     uint32_t row,
                                     uint32_t *col_io,
                                     uint32_t glyph_width,
                                     uint32_t glyph_height) {
  char text[TERM_MAX_COLS];
  uint32_t col = *col_io;
  uint32_t start = col;
  uint32_t len = 0u;
  uint32_t fg = term->cells[row][col].fg;
  while (col < term->cols && term->cells[row][col].ch != ' ' &&
         term->cells[row][col].fg == fg) {
    text[len++] = term->cells[row][col].ch;
    col++;
  }
  *col_io = col;
  return terminal_dl_emit_text_span(dl,
                                    (int32_t)(start * glyph_width),
                                    (int32_t)(row * glyph_height),
                                    text,
                                    len,
                                    fg,
                                    glyph_width,
                                    glyph_height);
}

static int terminal_emit_display_list(void *producer, struct capy_display_list *out) {
  struct terminal *term = (struct terminal *)producer;
  struct gui_surface *s;
  uint32_t gw;
  uint32_t gh;
  uint32_t base_bg;
  if (!term || !term->window || !term->font || !out) return -1;
  s = &term->window->surface;
  gw = term->font->glyph_width;
  gh = term->font->glyph_height;
  if (!s->pixels || gw == 0u || gh == 0u) return -1;
  base_bg = term->window->bg_color ? term->window->bg_color : term->default_bg;
  out->count = 0u;
  out->text_used = 0u;
  if (terminal_dl_emit_rect(out, 0, 0, s->width, s->height, base_bg) != 0) return -1;
  for (uint32_t r = 0u; r < term->rows; ++r) {
    uint32_t c = 0u;
    while (c < term->cols) {
      uint32_t start = c;
      if (terminal_dl_emit_bg_run(out, term, r, c, gw, gh, base_bg) != 0) return -1;
      while (c < term->cols && term->cells[r][c].bg == term->cells[r][start].bg) c++;
    }
  }
  for (uint32_t r = 0u; r < term->rows; ++r) {
    uint32_t c = 0u;
    while (c < term->cols) {
      if (term->cells[r][c].ch == ' ') {
        c++;
        continue;
      }
      if (terminal_dl_emit_text_run(out, term, r, &c, gw, gh) != 0) return -1;
    }
  }
  if (term->cursor_visible && term->scroll_offset == 0u) {
    if (terminal_dl_emit_rect(out,
                              (int32_t)(term->cursor_x * gw),
                              (int32_t)(term->cursor_y * gh + gh - 2u),
                              gw,
                              1u,
                              term->fg_color) != 0) return -1;
  }
  return 0;
}

int terminal_render_display_list(struct terminal *term) {
  const struct capy_display_list *prev;
  uint8_t next;
  int rc;
  int same_window;
  if (!term || !term->window) return -1;
  terminal_dl_init_once();
  next = g_terminal_dl_have_prev ? (uint8_t)(g_terminal_dl_prev_index ^ 1u) : 0u;
  terminal_dl_prepare(next);
  same_window = g_terminal_dl_have_prev &&
                g_terminal_dl_prev_window_id == term->window->id;
  prev = same_window ? &g_terminal_dl_lists[g_terminal_dl_prev_index] : 0;
  rc = capyui_display_adapter_render_producer_window(term->window, prev,
                                                      &g_terminal_dl_lists[next],
                                                      terminal_emit_display_list,
                                                      term,
                                                      0);
  if (rc != CAPYUI_DISPLAY_ADAPTER_OK) return rc;
  g_terminal_dl_prev_index = next;
  g_terminal_dl_have_prev = 1;
  g_terminal_dl_prev_window_id = term->window->id;
  return CAPYUI_DISPLAY_ADAPTER_OK;
}
#endif
