#include "gui/terminal.h"
#include <stddef.h>

static void term_memset(void *dst, int val, size_t len) {
  uint8_t *d = (uint8_t *)dst; for (size_t i = 0; i < len; i++) d[i] = (uint8_t)val;
}

void terminal_init(struct terminal *term, struct gui_window *win) {
  if (!term) return;
  term_memset(term, 0, sizeof(*term));
  term->window = win;
  term->font = font_default();
  term->fg_color = 0xCCCCCC;
  term->bg_color = 0x1A1A2E;
  term->cursor_visible = 1;
  term->cursor_blink = 1;
  term->echo = 1;

  if (win && term->font) {
    term->cols = win->frame.width / term->font->glyph_width;
    term->rows = win->frame.height / term->font->glyph_height;
    if (term->cols > TERM_MAX_COLS) term->cols = TERM_MAX_COLS;
    if (term->rows > TERM_MAX_ROWS) term->rows = TERM_MAX_ROWS;
  } else {
    term->cols = 80;
    term->rows = 25;
  }

  for (uint32_t r = 0; r < term->rows; r++) {
    for (uint32_t c = 0; c < term->cols; c++) {
      term->cells[r][c].ch = ' ';
      term->cells[r][c].fg = term->fg_color;
      term->cells[r][c].bg = term->bg_color;
      term->cells[r][c].attrs = 0;
    }
  }
}

static void terminal_scroll_line(struct terminal *term) {
  if (term->scrollback_lines < TERM_SCROLLBACK) {
    for (uint32_t c = 0; c < term->cols; c++)
      term->scrollback[term->scrollback_lines][c] = term->cells[0][c];
    term->scrollback_lines++;
  } else {
    for (uint32_t r = 0; r < TERM_SCROLLBACK - 1; r++)
      for (uint32_t c = 0; c < term->cols; c++)
        term->scrollback[r][c] = term->scrollback[r + 1][c];
    for (uint32_t c = 0; c < term->cols; c++)
      term->scrollback[TERM_SCROLLBACK - 1][c] = term->cells[0][c];
  }

  for (uint32_t r = 0; r < term->rows - 1; r++)
    for (uint32_t c = 0; c < term->cols; c++)
      term->cells[r][c] = term->cells[r + 1][c];

  for (uint32_t c = 0; c < term->cols; c++) {
    term->cells[term->rows - 1][c].ch = ' ';
    term->cells[term->rows - 1][c].fg = term->fg_color;
    term->cells[term->rows - 1][c].bg = term->bg_color;
  }
}

void terminal_write_char(struct terminal *term, char c) {
  if (!term) return;

  if (c == '\n') {
    term->cursor_x = 0;
    term->cursor_y++;
    if (term->cursor_y >= term->rows) {
      terminal_scroll_line(term);
      term->cursor_y = term->rows - 1;
    }
    return;
  }
  if (c == '\r') { term->cursor_x = 0; return; }
  if (c == '\t') {
    uint32_t next = ((term->cursor_x / TERM_TAB_SIZE) + 1) * TERM_TAB_SIZE;
    while (term->cursor_x < next && term->cursor_x < term->cols)
      terminal_write_char(term, ' ');
    return;
  }
  if (c == '\b') {
    if (term->cursor_x > 0) term->cursor_x--;
    return;
  }

  if (term->cursor_x >= term->cols) {
    term->cursor_x = 0;
    term->cursor_y++;
    if (term->cursor_y >= term->rows) {
      terminal_scroll_line(term);
      term->cursor_y = term->rows - 1;
    }
  }

  term->cells[term->cursor_y][term->cursor_x].ch = c;
  term->cells[term->cursor_y][term->cursor_x].fg = term->fg_color;
  term->cells[term->cursor_y][term->cursor_x].bg = term->bg_color;
  term->cursor_x++;
}

void terminal_write(struct terminal *term, const char *data, size_t len) {
  for (size_t i = 0; i < len; i++) terminal_write_char(term, data[i]);
}

void terminal_write_string(struct terminal *term, const char *str) {
  if (!str) return;
  while (*str) terminal_write_char(term, *str++);
}

void terminal_clear(struct terminal *term) {
  if (!term) return;
  for (uint32_t r = 0; r < term->rows; r++) {
    for (uint32_t c = 0; c < term->cols; c++) {
      term->cells[r][c].ch = ' ';
      term->cells[r][c].fg = term->fg_color;
      term->cells[r][c].bg = term->bg_color;
    }
  }
  term->cursor_x = 0;
  term->cursor_y = 0;
}

void terminal_scroll_up(struct terminal *term, uint32_t lines) {
  if (!term) return;
  if (term->scroll_offset + lines > term->scrollback_lines)
    term->scroll_offset = term->scrollback_lines;
  else
    term->scroll_offset += lines;
}

void terminal_scroll_down(struct terminal *term, uint32_t lines) {
  if (!term) return;
  if (lines > term->scroll_offset) term->scroll_offset = 0;
  else term->scroll_offset -= lines;
}

void terminal_set_cursor(struct terminal *term, uint32_t x, uint32_t y) {
  if (!term) return;
  if (x < term->cols) term->cursor_x = x;
  if (y < term->rows) term->cursor_y = y;
}

void terminal_set_color(struct terminal *term, uint32_t fg, uint32_t bg) {
  if (!term) return;
  term->fg_color = fg;
  term->bg_color = bg;
}

void terminal_paint(struct terminal *term) {
  if (!term || !term->window || !term->font) return;
  struct gui_surface *s = &term->window->surface;

  uint32_t gw = term->font->glyph_width;
  uint32_t gh = term->font->glyph_height;

  for (uint32_t r = 0; r < term->rows; r++) {
    for (uint32_t c = 0; c < term->cols; c++) {
      int32_t px = (int32_t)(c * gw);
      int32_t py = (int32_t)(r * gh);

      struct terminal_cell *cell = &term->cells[r][c];

      for (uint32_t y = 0; y < gh; y++) {
        int32_t dy = py + (int32_t)y;
        if (dy < 0 || (uint32_t)dy >= s->height) continue;
        uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + dy * s->pitch);
        for (uint32_t x = 0; x < gw; x++) {
          int32_t dx = px + (int32_t)x;
          if (dx >= 0 && (uint32_t)dx < s->width) line[dx] = cell->bg;
        }
      }

      if (cell->ch != ' ')
        font_draw_char(s, term->font, px, py, cell->ch, cell->fg);
    }
  }

  if (term->cursor_visible && term->scroll_offset == 0) {
    int32_t cx = (int32_t)(term->cursor_x * gw);
    int32_t cy = (int32_t)(term->cursor_y * gh + gh - 2);
    for (uint32_t x = 0; x < gw; x++) {
      int32_t dx = cx + (int32_t)x;
      if (dx >= 0 && (uint32_t)dx < s->width && cy >= 0 && (uint32_t)cy < s->height) {
        uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + cy * s->pitch);
        line[dx] = term->fg_color;
      }
    }
  }
}

void terminal_handle_key(struct terminal *term, uint32_t keycode, char ch) {
  if (!term) return;
  (void)keycode;

  if (ch == '\b') {
    if (term->input_pos > 0) {
      term->input_pos--;
      term->input_len--;
      if (term->echo) terminal_write_char(term, '\b');
    }
    return;
  }

  if (ch == '\n' || ch == '\r') {
    term->input_buf[term->input_len] = '\0';
    if (term->echo) terminal_write_char(term, '\n');
    if (term->output_callback)
      term->output_callback(term, term->input_buf, term->input_len);
    term->input_len = 0;
    term->input_pos = 0;
    return;
  }

  if (ch >= 32 && ch < 127 && term->input_len < TERM_INPUT_BUF - 1) {
    term->input_buf[term->input_len++] = ch;
    term->input_pos++;
    if (term->echo) terminal_write_char(term, ch);
  }
}

void terminal_handle_mouse_scroll(struct terminal *term, int delta) {
  if (!term) return;
  if (delta > 0) terminal_scroll_up(term, (uint32_t)delta);
  else if (delta < 0) terminal_scroll_down(term, (uint32_t)(-delta));
}

size_t terminal_read_line(struct terminal *term, char *buf, size_t maxlen) {
  (void)term; (void)buf; (void)maxlen;
  return 0;
}

void terminal_set_output_callback(struct terminal *term,
                                   void (*cb)(struct terminal *, const char *, size_t),
                                   void *user_data) {
  if (!term) return;
  term->output_callback = cb;
  term->user_data = user_data;
}
