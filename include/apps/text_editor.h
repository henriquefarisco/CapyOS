#ifndef APPS_TEXT_EDITOR_H
#define APPS_TEXT_EDITOR_H

#include "gui/compositor.h"

#define EDITOR_MAX_LINES 1024
#define EDITOR_LINE_MAX  256
#define EDITOR_PATH_MAX  256

struct text_editor_app {
  struct gui_window *window;
  char path[EDITOR_PATH_MAX];
  char lines[EDITOR_MAX_LINES][EDITOR_LINE_MAX];
  int line_count;
  int cursor_line;
  int cursor_col;
  int scroll_offset;
  int modified;
};

void text_editor_open(const char *path);
void text_editor_save(struct text_editor_app *app);
void text_editor_handle_key(struct text_editor_app *app, uint32_t keycode, char ch);
void text_editor_paint(struct text_editor_app *app);

#endif
