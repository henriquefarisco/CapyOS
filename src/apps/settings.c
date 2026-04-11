#include "apps/settings.h"
#include "gui/compositor.h"
#include "gui/font.h"
#include "gui/widget.h"
#include "memory/kmem.h"
#include <stddef.h>

static struct settings_app g_settings;

static const char *tab_names[SETTINGS_TAB_COUNT] = {
  "Display", "Network", "Keyboard", "Language", "Users", "Updates", "About"
};

static void settings_memset(void *d, int v, size_t n) {
  uint8_t *p = (uint8_t *)d; for (size_t i = 0; i < n; i++) p[i] = (uint8_t)v;
}

static void on_tab_click(struct widget *w, void *data) {
  struct settings_app *app = (struct settings_app *)data;
  if (!app || !w) return;
  for (int i = 0; i < SETTINGS_TAB_COUNT; i++) {
    if (app->tab_buttons[i] == w) {
      settings_switch_tab(app, (enum settings_tab)i);
      return;
    }
  }
}

static void settings_window_paint(struct gui_window *win) {
  if (!win || !win->user_data) return;
  settings_paint((struct settings_app *)win->user_data);
}

static void settings_window_mouse(struct gui_window *win, int32_t x, int32_t y,
                                  uint8_t buttons) {
  struct settings_app *app = NULL;
  struct gui_event ev;
  if (!win || !win->user_data || !(buttons & 1)) return;
  app = (struct settings_app *)win->user_data;
  settings_memset(&ev, 0, sizeof(ev));
  ev.type = GUI_EVENT_MOUSE_DOWN;
  ev.mouse.x = x;
  ev.mouse.y = y;
  ev.mouse.buttons = buttons;
  for (int i = 0; i < SETTINGS_TAB_COUNT; i++) {
    if (app->tab_buttons[i] && widget_handle_event(app->tab_buttons[i], &ev)) break;
  }
}

void settings_open(void) {
  const struct gui_theme_palette *theme = compositor_theme();
  uint8_t scale = compositor_ui_scale();
  uint32_t width = 550 + 140 * (scale - 1);
  uint32_t height = 400 + 120 * (scale - 1);
  uint32_t tab_w = 120 + 32 * (scale - 1);
  uint32_t tab_h = 32 + 8 * (scale - 1);
  uint32_t tab_gap = 4 + 4 * (scale - 1);
  settings_memset(&g_settings, 0, sizeof(g_settings));
  g_settings.window = compositor_create_window("Settings", 100, 70, width, height);
  if (!g_settings.window) return;
  g_settings.window->bg_color = theme->window_bg;
  g_settings.window->border_color = theme->window_border;
  g_settings.window->user_data = &g_settings;
  g_settings.window->on_paint = settings_window_paint;
  g_settings.window->on_mouse = settings_window_mouse;
  compositor_show_window(g_settings.window->id);
  compositor_focus_window(g_settings.window->id);

  /* Create tab buttons on the left sidebar */
  for (int i = 0; i < SETTINGS_TAB_COUNT; i++) {
    struct widget *btn = widget_create(WIDGET_BUTTON, g_settings.window);
    if (!btn) continue;
    widget_set_bounds(btn, 4, 4 + i * (int32_t)(tab_h + tab_gap), tab_w, tab_h);
    widget_set_text(btn, tab_names[i]);
    struct widget_style st = widget_button_style();
    st.bg_color = (i == 0) ? theme->accent : theme->accent_alt;
    st.text_color = (i == 0) ? theme->accent_text : theme->text;
    widget_set_style(btn, &st);
    widget_set_on_click(btn, on_tab_click, &g_settings);
    g_settings.tab_buttons[i] = btn;
  }

  g_settings.active_tab = SETTINGS_TAB_DISPLAY;
}

void settings_switch_tab(struct settings_app *app, enum settings_tab tab) {
  if (!app || tab >= SETTINGS_TAB_COUNT) return;
  const struct gui_theme_palette *theme = compositor_theme();
  app->active_tab = tab;
  for (int i = 0; i < SETTINGS_TAB_COUNT; i++) {
    if (app->tab_buttons[i]) {
      struct widget_style st = widget_button_style();
      st.bg_color = (i == (int)tab) ? theme->accent : theme->accent_alt;
      st.text_color = (i == (int)tab) ? theme->accent_text : theme->text;
      widget_set_style(app->tab_buttons[i], &st);
    }
  }
}

void settings_paint(struct settings_app *app) {
  if (!app || !app->window) return;
  struct gui_surface *s = &app->window->surface;
  const struct font *f = font_default();
  const struct gui_theme_palette *theme = compositor_theme();
  uint8_t scale = compositor_ui_scale();
  int32_t sidebar_w = 130 + 28 * (scale - 1);
  if (!f) return;

  /* Clear */
  for (uint32_t y = 0; y < s->height; y++) {
    uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + y * s->pitch);
    for (uint32_t x = 0; x < s->width; x++) line[x] = theme->window_bg;
  }

  /* Sidebar background */
  for (uint32_t y = 0; y < s->height; y++) {
    uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + y * s->pitch);
    for (uint32_t x = 0; x < (uint32_t)sidebar_w && x < s->width; x++)
      line[x] = theme->terminal_bg;
  }

  /* Paint tab buttons */
  for (int i = 0; i < SETTINGS_TAB_COUNT; i++) {
    if (app->tab_buttons[i]) {
      struct widget_style st = widget_button_style();
      st.bg_color = (i == (int)app->active_tab) ? theme->accent : theme->accent_alt;
      st.text_color = (i == (int)app->active_tab) ? theme->accent_text : theme->text;
      widget_set_style(app->tab_buttons[i], &st);
      widget_paint(app->tab_buttons[i], s);
    }
  }

  /* Content area */
  int32_t cx = sidebar_w + 10;
  int32_t cy = 12;

  font_draw_string(s, f, cx, cy, tab_names[app->active_tab], theme->accent);
  cy += 24;

  /* Separator */
  for (uint32_t x = (uint32_t)cx; x < s->width - 8; x++) {
    uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + (uint32_t)cy * s->pitch);
    line[x] = theme->window_border;
  }
  cy += 8;

  switch (app->active_tab) {
  case SETTINGS_TAB_DISPLAY:
    font_draw_string(s, f, cx, cy, "Resolution: UEFI GOP default", theme->text); cy += 18;
    font_draw_string(s, f, cx, cy, "Theme: active user theme", theme->text); cy += 18;
    font_draw_string(s, f, cx, cy, "Splash: enabled", theme->text); cy += 18;
    break;
  case SETTINGS_TAB_NETWORK:
    font_draw_string(s, f, cx, cy, "Mode: DHCP", theme->text); cy += 18;
    font_draw_string(s, f, cx, cy, "IPv4: (see net-status)", theme->text); cy += 18;
    font_draw_string(s, f, cx, cy, "DNS: (see net-dns)", theme->text); cy += 18;
    break;
  case SETTINGS_TAB_KEYBOARD:
    font_draw_string(s, f, cx, cy, "Layout: us", theme->text); cy += 18;
    font_draw_string(s, f, cx, cy, "Available: us, br-abnt2", theme->text); cy += 18;
    break;
  case SETTINGS_TAB_LANGUAGE:
    font_draw_string(s, f, cx, cy, "System: pt-BR", theme->text); cy += 18;
    font_draw_string(s, f, cx, cy, "Available: pt-BR, en, es", theme->text); cy += 18;
    break;
  case SETTINGS_TAB_USERS:
    font_draw_string(s, f, cx, cy, "admin (UID 0)", theme->text); cy += 18;
    font_draw_string(s, f, cx, cy, "Use CLI 'add-user' to create users", theme->text_muted); cy += 18;
    break;
  case SETTINGS_TAB_UPDATES:
    font_draw_string(s, f, cx, cy, "Channel: stable", theme->text); cy += 18;
    font_draw_string(s, f, cx, cy, "Use 'update-status' in CLI for details", theme->text_muted); cy += 18;
    break;
  case SETTINGS_TAB_ABOUT:
    font_draw_string(s, f, cx, cy, "CapyOS 0.8.0-alpha.0", theme->text); cy += 18;
    font_draw_string(s, f, cx, cy, "Developer: Henrique Schwarz Souza Farisco", theme->text); cy += 18;
    font_draw_string(s, f, cx, cy, "License: Apache-2.0", theme->text); cy += 18;
    font_draw_string(s, f, cx, cy, "Track: UEFI/GPT/x86_64", theme->text_muted); cy += 18;
    break;
  default:
    break;
  }
}
