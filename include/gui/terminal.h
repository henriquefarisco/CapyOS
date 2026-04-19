#ifndef GUI_TERMINAL_H
#define GUI_TERMINAL_H

#include <stdint.h>
#include <stddef.h>
#include "gui/compositor.h"
#include "gui/font.h"

#define TERM_MAX_COLS  200
#define TERM_MAX_ROWS  60
#define TERM_SCROLLBACK 64
#define TERM_INPUT_BUF  256
#define TERM_TAB_SIZE   8

struct terminal_cell {
  char ch;
  uint32_t fg;
  uint32_t bg;
  uint8_t attrs;
};

struct terminal {
  struct gui_window *window;
  const struct font *font;
  struct terminal_cell cells[TERM_MAX_ROWS][TERM_MAX_COLS];
  struct terminal_cell scrollback[TERM_SCROLLBACK][TERM_MAX_COLS];
  uint32_t cols;
  uint32_t rows;
  uint32_t cursor_x;
  uint32_t cursor_y;
  uint32_t scroll_offset;
  uint32_t scrollback_lines;
  uint32_t fg_color;
  uint32_t bg_color;
  int cursor_visible;
  int cursor_blink;
  uint64_t cursor_blink_tick;
  char input_buf[TERM_INPUT_BUF];
  uint32_t input_len;
  uint32_t input_pos;
  int echo;
  uint32_t default_fg;
  uint32_t default_bg;
  int ansi_state;
  char ansi_buf[16];
  int ansi_len;
  void (*output_callback)(struct terminal *term, const char *data, size_t len);
  void *user_data;
};

void terminal_init(struct terminal *term, struct gui_window *win);
void terminal_write(struct terminal *term, const char *data, size_t len);
void terminal_write_char(struct terminal *term, char c);
void terminal_write_string(struct terminal *term, const char *str);
void terminal_clear(struct terminal *term);
void terminal_scroll_up(struct terminal *term, uint32_t lines);
void terminal_scroll_down(struct terminal *term, uint32_t lines);
void terminal_set_cursor(struct terminal *term, uint32_t x, uint32_t y);
void terminal_set_color(struct terminal *term, uint32_t fg, uint32_t bg);
void terminal_paint(struct terminal *term);
void terminal_handle_key(struct terminal *term, uint32_t keycode, char ch);
void terminal_handle_mouse_scroll(struct terminal *term, int delta);
size_t terminal_read_line(struct terminal *term, char *buf, size_t maxlen);
void terminal_set_output_callback(struct terminal *term,
                                   void (*cb)(struct terminal *, const char *, size_t),
                                   void *user_data);

#endif /* GUI_TERMINAL_H */
