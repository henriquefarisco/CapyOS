#include "apps/calculator.h"
#include "gui/compositor.h"
#include "gui/widget.h"
#include "gui/font.h"
#include "memory/kmem.h"
#include <stddef.h>

static struct calculator_app g_calc;

static void calc_memset(void *d, int v, size_t n) {
  uint8_t *p = (uint8_t *)d; for (size_t i = 0; i < n; i++) p[i] = (uint8_t)v;
}

static int64_t calc_eval(const char *expr, int len) {
  int64_t result = 0, current = 0;
  char op = '+';
  for (int i = 0; i <= len; i++) {
    char c = (i < len) ? expr[i] : '\0';
    if (c >= '0' && c <= '9') {
      current = current * 10 + (c - '0');
    } else {
      switch (op) {
        case '+': result += current; break;
        case '-': result -= current; break;
        case '*': result *= current; break;
        case '/': if (current != 0) result /= current; break;
      }
      current = 0;
      op = c;
    }
  }
  return result;
}

static void on_calc_button(struct widget *w, void *data) {
  struct calculator_app *app = (struct calculator_app *)data;
  if (!app || !w) return;
  const char *label = w->text;

  if (label[0] == 'C' && label[1] == '\0') {
    app->expr_len = 0;
    app->expr[0] = '\0';
    app->has_result = 0;
  } else if (label[0] == '=' && label[1] == '\0') {
    app->result = calc_eval(app->expr, app->expr_len);
    app->has_result = 1;
    /* Convert result to display string */
    int64_t v = app->result;
    app->expr_len = 0;
    if (v < 0) { app->expr[app->expr_len++] = '-'; v = -v; }
    if (v == 0) { app->expr[app->expr_len++] = '0'; }
    else {
      char tmp[20]; int tp = 0;
      while (v > 0) { tmp[tp++] = '0' + (char)(v % 10); v /= 10; }
      for (int i = tp - 1; i >= 0; i--) app->expr[app->expr_len++] = tmp[i];
    }
    app->expr[app->expr_len] = '\0';
  } else {
    if (app->expr_len < 62) {
      app->expr[app->expr_len++] = label[0];
      app->expr[app->expr_len] = '\0';
    }
  }

  if (app->display) widget_set_text(app->display, app->expr);
}

void calculator_open(void) {
  calc_memset(&g_calc, 0, sizeof(g_calc));

  g_calc.window = compositor_create_window("Calculator", 200, 100, 240, 320);
  if (!g_calc.window) return;
  g_calc.window->bg_color = 0x2D2D3D;
  compositor_show_window(g_calc.window->id);
  compositor_focus_window(g_calc.window->id);

  g_calc.display = widget_create(WIDGET_LABEL, g_calc.window);
  if (g_calc.display) {
    widget_set_bounds(g_calc.display, 10, 10, 220, 40);
    struct widget_style ds = widget_default_style();
    ds.bg_color = 0x1E1E2E;
    ds.text_color = 0xCDD6F4;
    ds.font_size = 24;
    widget_set_style(g_calc.display, &ds);
    widget_set_text(g_calc.display, "0");
  }

  static const char *labels[] = {
    "7","8","9","/",
    "4","5","6","*",
    "1","2","3","-",
    "C","0","=","+"
  };

  for (int i = 0; i < 16; i++) {
    struct widget *btn = widget_create(WIDGET_BUTTON, g_calc.window);
    if (!btn) continue;
    int row = i / 4;
    int col = i % 4;
    widget_set_bounds(btn, 10 + col * 57, 60 + row * 62, 52, 55);
    widget_set_text(btn, labels[i]);
    widget_set_on_click(btn, on_calc_button, &g_calc);
    g_calc.buttons[i] = btn;
  }
}

void calculator_paint(struct calculator_app *app) {
  if (!app || !app->window) return;
  struct gui_surface *s = &app->window->surface;

  /* Fill background */
  for (uint32_t y = 0; y < s->height; y++) {
    uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + y * s->pitch);
    for (uint32_t x = 0; x < s->width; x++) line[x] = 0x2D2D3D;
  }

  if (app->display) widget_paint(app->display, s);
  for (int i = 0; i < 16; i++) {
    if (app->buttons[i]) widget_paint(app->buttons[i], s);
  }
}
