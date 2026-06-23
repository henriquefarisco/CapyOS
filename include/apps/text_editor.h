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

/* Etapa 6 / Slice 6.6: headless smoke roundtrip (no GUI). Returns 0 when the
 * editor's primary text-buffer logic (insert / backspace / newline via
 * text_editor_handle_key) behaves as expected, non-zero otherwise. Called by the
 * apps-basic-roundtrip orchestrator via the apps/apps_smoke.h contract. */
int text_editor_smoke_roundtrip(void);

#endif
