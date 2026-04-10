#ifndef APPS_CALCULATOR_H
#define APPS_CALCULATOR_H

#include "gui/compositor.h"
#include "gui/widget.h"

struct calculator_app {
  struct gui_window *window;
  struct widget *display;
  struct widget *buttons[20];
  char expr[64];
  int expr_len;
  int64_t result;
  int has_result;
};

void calculator_open(void);
void calculator_handle_button(struct calculator_app *app, const char *label);
void calculator_paint(struct calculator_app *app);

#endif
