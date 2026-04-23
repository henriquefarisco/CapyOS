#include "apps/calculator.h"
#include "gui/compositor.h"
#include "gui/widget.h"
#include "gui/font.h"
#include "util/kstring.h"
#include "memory/kmem.h"
#include <stddef.h>

static struct calculator_app g_calc;
static int g_calc_open = 0;

static void calculator_cleanup(void) {
  /* Free all widgets before clearing the struct */
  if (g_calc.display) { widget_destroy(g_calc.display); g_calc.display = NULL; }
  for (int i = 0; i < 20; i++) {
    if (g_calc.buttons[i]) { widget_destroy(g_calc.buttons[i]); g_calc.buttons[i] = NULL; }
  }
  g_calc.window = NULL;
  g_calc_open = 0;
}

static void calculator_on_close(struct gui_window *win) {
  (void)win;
  calculator_cleanup();
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
  if (!app || !w || !app->window) return;
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
    if (app->has_result) {
      /* After a result: if user presses an operator, keep the result
       * as the left operand and just append the operator.  If user
       * presses a digit, start a completely new expression. */
      if (label[0] >= '0' && label[0] <= '9') {
        app->expr_len = 0;
        app->expr[0] = '\0';
      }
      app->has_result = 0;
    }
    if (app->expr_len < 62) {
      app->expr[app->expr_len++] = label[0];
      app->expr[app->expr_len] = '\0';
    }
  }

  if (app->display) {
    widget_set_text(app->display, app->expr[0] ? app->expr : "0");
  }
  if (app->window) compositor_invalidate(app->window->id);
}

static void calculator_window_paint(struct gui_window *win) {
  if (!win || !win->user_data) return;
  calculator_paint((struct calculator_app *)win->user_data);
}

static void calculator_window_mouse(struct gui_window *win, int32_t x, int32_t y,
                                    uint8_t buttons) {
  struct calculator_app *app = NULL;
  struct gui_event ev;
  if (!win || !win->user_data || !(buttons & 1)) return;
  app = (struct calculator_app *)win->user_data;
  kmemzero(&ev, sizeof(ev));
  ev.type = GUI_EVENT_MOUSE_DOWN;
  ev.mouse.x = x;
  ev.mouse.y = y;
  ev.mouse.buttons = buttons;
  for (int i = 0; i < 16; i++) {
    if (app->buttons[i] && widget_handle_event(app->buttons[i], &ev)) break;
  }
  if (app->display) (void)widget_handle_event(app->display, &ev);
}

static void calculator_window_key(struct gui_window *win, uint32_t keycode,
                                  uint8_t mods) {
  (void)mods;
  if (!win || !win->user_data) return;
  struct calculator_app *app = (struct calculator_app *)win->user_data;
  char ch = (keycode < 0x80) ? (char)keycode : 0;

  /* Map keyboard input to calculator button actions */
  if (ch >= '0' && ch <= '9') {
    /* Digit keys */
    char label[2] = { ch, '\0' };
    struct widget fake;
    kmemzero(&fake, sizeof(fake));
    kstrcpy(fake.text, WIDGET_MAX_TEXT, label);
    on_calc_button(&fake, app);
  } else if (ch == '+' || ch == '-' || ch == '*' || ch == '/') {
    char label[2] = { ch, '\0' };
    struct widget fake;
    kmemzero(&fake, sizeof(fake));
    kstrcpy(fake.text, WIDGET_MAX_TEXT, label);
    on_calc_button(&fake, app);
  } else if (ch == '=' || ch == '\n' || ch == '\r') {
    struct widget fake;
    kmemzero(&fake, sizeof(fake));
    kstrcpy(fake.text, WIDGET_MAX_TEXT, "=");
    on_calc_button(&fake, app);
  } else if (ch == '\b' || keycode == 0x7F) {
    /* Backspace: delete last character */
    if (app->expr_len > 0) {
      app->expr_len--;
      app->expr[app->expr_len] = '\0';
      app->has_result = 0;
      if (app->display)
        widget_set_text(app->display, app->expr[0] ? app->expr : "0");
      if (app->window) compositor_invalidate(app->window->id);
    }
  } else if (ch == 'c' || ch == 'C') {
    struct widget fake;
    kmemzero(&fake, sizeof(fake));
    kstrcpy(fake.text, WIDGET_MAX_TEXT, "C");
    on_calc_button(&fake, app);
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
  struct widget_style ds;

  /* If already open, just focus the existing window */
  if (g_calc_open && g_calc.window) {
    compositor_show_window(g_calc.window->id);
    compositor_focus_window(g_calc.window->id);
    return;
  }

  /* Clean up any stale state */
  calculator_cleanup();
  kmemzero(&g_calc, sizeof(g_calc));

  g_calc.window = compositor_create_window("Calculator", x, y, width, height);
  if (!g_calc.window) return;
  g_calc.window->bg_color = theme->window_bg;
  g_calc.window->border_color = theme->window_border;
  g_calc.window->user_data = &g_calc;
  g_calc.window->on_paint = calculator_window_paint;
  g_calc.window->on_mouse = calculator_window_mouse;
  g_calc.window->on_key = calculator_window_key;
  g_calc.window->on_close = calculator_on_close;
  compositor_show_window(g_calc.window->id);
  compositor_focus_window(g_calc.window->id);

  g_calc.display = widget_create(WIDGET_LABEL, g_calc.window);
  if (g_calc.display) {
    widget_set_bounds(g_calc.display, 10, 10, width - 20, display_h);
    ds = widget_default_style();
    ds.bg_color = theme->terminal_bg;
    ds.text_color = theme->text;
    ds.font_size = 24;
    ds.border_color = theme->window_border;
    ds.border_width = 1;
    widget_set_style(g_calc.display, &ds);
    widget_set_text(g_calc.display, "0");
  }

  {
    static const char *labels[] = {
      "7","8","9","/",
      "4","5","6","*",
      "1","2","3","-",
      "C","0","=","+"
    };

    for (int i = 0; i < 16; i++) {
      struct widget *btn = widget_create(WIDGET_BUTTON, g_calc.window);
      struct widget_style bs;
      int row, col;
      if (!btn) continue;
      row = i / 4;
      col = i % 4;
      widget_set_bounds(btn, 10 + col * (int32_t)(button_w + gap_x),
                        20 + (int32_t)display_h + row * (int32_t)(button_h + gap_y),
                        button_w, button_h);
      widget_set_text(btn, labels[i]);
      /* Use a visible, contrasting style for button text */
      bs = widget_button_style();
      bs.text_color = theme->accent_text;
      bs.bg_color = theme->accent;
      /* Operator buttons get a different color */
      if (labels[i][0] == '/' || labels[i][0] == '*' ||
          labels[i][0] == '-' || labels[i][0] == '+' ||
          labels[i][0] == '=' || labels[i][0] == 'C') {
        bs.bg_color = theme->accent_alt;
        bs.text_color = theme->text;
      }
      widget_set_style(btn, &bs);
      widget_set_on_click(btn, on_calc_button, &g_calc);
      g_calc.buttons[i] = btn;
    }
  }

  g_calc_open = 1;
}

void calculator_paint(struct calculator_app *app) {
  if (!app || !app->window) return;
  {
    struct gui_surface *s = &app->window->surface;
    const struct gui_theme_palette *theme = compositor_theme();

    /* Fill background */
    for (uint32_t py = 0; py < s->height; py++) {
      uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + py * s->pitch);
      for (uint32_t px = 0; px < s->width; px++) line[px] = theme->window_bg;
    }

    /* Paint display */
    if (app->display) widget_paint(app->display, s);

    /* Paint buttons - they already have their styles set at creation time */
    for (int i = 0; i < 16; i++) {
      if (app->buttons[i]) widget_paint(app->buttons[i], s);
    }
  }
}
