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

static void calculator_window_paint(struct gui_window *win) {
  if (!win || !win->user_data) return;
  calculator_paint((struct calculator_app *)win->user_data);
}

static void calculator_window_key(struct gui_window *win, uint32_t keycode,
                                  uint8_t mods) {
  (void)mods;
  if (!win || !win->user_data) return;
  struct calculator_app *app = (struct calculator_app *)win->user_data;
  char ch = (keycode < 0x80) ? (char)keycode : 0;
  /* Map keyboard characters to button labels */
  const char *label = NULL;
  if (ch >= '0' && ch <= '9') { static char d[2]; d[0] = ch; d[1] = 0; label = d; }
  else if (ch == '+') label = "+";
  else if (ch == '-') label = "-";
  else if (ch == '*') label = "*";
  else if (ch == '/') label = "/";
  else if (ch == '=' || ch == '\n' || ch == '\r') label = "=";
  else if (ch == 'c' || ch == 'C' || ch == '\b') label = "C";
  else if (ch == ',') { static char d[2]; d[0] = '.'; d[1] = 0; label = d; }
  if (!label) return;
  /* Simulate button press by finding matching button */
  for (int i = 0; i < 16; i++) {
    if (app->buttons[i] && app->buttons[i]->text[0] == label[0]) {
      on_calc_button(app->buttons[i], app);
      return;
    }
  }
}

static void calculator_window_mouse(struct gui_window *win, int32_t x, int32_t y,
                                    uint8_t buttons) {
  struct calculator_app *app = NULL;
  struct gui_event ev;
  if (!win || !win->user_data || !(buttons & 1)) return;
  app = (struct calculator_app *)win->user_data;
  calc_memset(&ev, 0, sizeof(ev));
  ev.type = GUI_EVENT_MOUSE_DOWN;
  ev.mouse.x = x;
  ev.mouse.y = y;
  ev.mouse.buttons = buttons;
  if (app->display) (void)widget_handle_event(app->display, &ev);
  for (int i = 0; i < 16; i++) {
    if (app->buttons[i] && widget_handle_event(app->buttons[i], &ev)) break;
  }
}

void calculator_open(void) {
  const struct gui_theme_palette *theme = compositor_theme();
  uint8_t scale = compositor_ui_scale();
  int32_t x = 160 + 20 * (scale - 1);
  int32_t y = 90 + 10 * (scale - 1);
  uint32_t width = 240 + 72 * (scale - 1);
  uint32_t height = 320 + 96 * (scale - 1);
  uint32_t display_h = 40 + 12 * (scale - 1);
  uint32_t button_w = 52 + 14 * (scale - 1);
  uint32_t button_h = 55 + 12 * (scale - 1);
  uint32_t gap_x = 5 + 4 * (scale - 1);
  uint32_t gap_y = 7 + 4 * (scale - 1);
  calc_memset(&g_calc, 0, sizeof(g_calc));

  g_calc.window = compositor_create_window("Calculator", x, y, width, height);
  if (!g_calc.window) return;
  g_calc.window->bg_color = theme->window_bg;
  g_calc.window->border_color = theme->window_border;
  g_calc.window->user_data = &g_calc;
  g_calc.window->on_paint = calculator_window_paint;
  g_calc.window->on_mouse = calculator_window_mouse;
  g_calc.window->on_key = calculator_window_key;
  compositor_show_window(g_calc.window->id);
  compositor_focus_window(g_calc.window->id);

  g_calc.display = widget_create(WIDGET_LABEL, g_calc.window);
  if (g_calc.display) {
    widget_set_bounds(g_calc.display, 10, 10, width - 20, display_h);
    struct widget_style ds = widget_default_style();
    ds.bg_color = theme->terminal_bg;
    ds.text_color = theme->text;
    ds.font_size = 24;
    ds.border_color = theme->window_border;
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
    widget_set_bounds(btn, 10 + col * (int32_t)(button_w + gap_x),
                      20 + (int32_t)display_h + row * (int32_t)(button_h + gap_y),
                      button_w, button_h);
    widget_set_text(btn, labels[i]);
    widget_set_on_click(btn, on_calc_button, &g_calc);
    g_calc.buttons[i] = btn;
  }
}

void calculator_paint(struct calculator_app *app) {
  if (!app || !app->window) return;
  struct gui_surface *s = &app->window->surface;
  const struct gui_theme_palette *theme = compositor_theme();

  /* Fill background */
  for (uint32_t y = 0; y < s->height; y++) {
    uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + y * s->pitch);
    for (uint32_t x = 0; x < s->width; x++) line[x] = theme->window_bg;
  }

  if (app->display) {
    struct widget_style ds = widget_default_style();
    ds.bg_color = theme->terminal_bg;
    ds.text_color = theme->text;
    ds.border_color = theme->window_border;
    widget_set_style(app->display, &ds);
  }
  for (int i = 0; i < 16; i++) {
    if (app->buttons[i]) {
      struct widget_style bs = widget_button_style();
      widget_set_style(app->buttons[i], &bs);
    }
  }
  if (app->display) widget_paint(app->display, s);
  for (int i = 0; i < 16; i++) {
    if (app->buttons[i]) widget_paint(app->buttons[i], s);
  }
}
