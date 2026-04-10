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

void settings_open(void) {
  settings_memset(&g_settings, 0, sizeof(g_settings));
  g_settings.window = compositor_create_window("Settings", 120, 80, 550, 400);
  if (!g_settings.window) return;
  g_settings.window->bg_color = 0x1E1E2E;
  compositor_show_window(g_settings.window->id);
  compositor_focus_window(g_settings.window->id);

  /* Create tab buttons on the left sidebar */
  for (int i = 0; i < SETTINGS_TAB_COUNT; i++) {
    struct widget *btn = widget_create(WIDGET_BUTTON, g_settings.window);
    if (!btn) continue;
    widget_set_bounds(btn, 4, 4 + i * 36, 120, 32);
    widget_set_text(btn, tab_names[i]);
    struct widget_style st = widget_button_style();
    st.bg_color = (i == 0) ? 0x45475A : 0x313244;
    widget_set_style(btn, &st);
    widget_set_on_click(btn, on_tab_click, &g_settings);
    g_settings.tab_buttons[i] = btn;
  }

  g_settings.active_tab = SETTINGS_TAB_DISPLAY;
}

void settings_switch_tab(struct settings_app *app, enum settings_tab tab) {
  if (!app || tab >= SETTINGS_TAB_COUNT) return;
  app->active_tab = tab;
  for (int i = 0; i < SETTINGS_TAB_COUNT; i++) {
    if (app->tab_buttons[i]) {
      struct widget_style st = widget_button_style();
      st.bg_color = (i == (int)tab) ? 0x45475A : 0x313244;
      widget_set_style(app->tab_buttons[i], &st);
    }
  }
}

void settings_paint(struct settings_app *app) {
  if (!app || !app->window) return;
  struct gui_surface *s = &app->window->surface;
  const struct font *f = font_default();
  if (!f) return;

  /* Clear */
  for (uint32_t y = 0; y < s->height; y++) {
    uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + y * s->pitch);
    for (uint32_t x = 0; x < s->width; x++) line[x] = 0x1E1E2E;
  }

  /* Sidebar background */
  for (uint32_t y = 0; y < s->height; y++) {
    uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + y * s->pitch);
    for (uint32_t x = 0; x < 130; x++) line[x] = 0x181825;
  }

  /* Paint tab buttons */
  for (int i = 0; i < SETTINGS_TAB_COUNT; i++) {
    if (app->tab_buttons[i]) widget_paint(app->tab_buttons[i], s);
  }

  /* Content area */
  int32_t cx = 140;
  int32_t cy = 12;

  font_draw_string(s, f, cx, cy, tab_names[app->active_tab], 0xCBA6F7);
  cy += 24;

  /* Separator */
  for (uint32_t x = 140; x < s->width - 8; x++) {
    uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + (uint32_t)cy * s->pitch);
    line[x] = 0x313244;
  }
  cy += 8;

  switch (app->active_tab) {
  case SETTINGS_TAB_DISPLAY:
    font_draw_string(s, f, cx, cy, "Resolution: UEFI GOP default", 0xCDD6F4); cy += 18;
    font_draw_string(s, f, cx, cy, "Theme: capyos", 0xCDD6F4); cy += 18;
    font_draw_string(s, f, cx, cy, "Splash: enabled", 0xCDD6F4); cy += 18;
    break;
  case SETTINGS_TAB_NETWORK:
    font_draw_string(s, f, cx, cy, "Mode: DHCP", 0xCDD6F4); cy += 18;
    font_draw_string(s, f, cx, cy, "IPv4: (see net-status)", 0xCDD6F4); cy += 18;
    font_draw_string(s, f, cx, cy, "DNS: (see net-dns)", 0xCDD6F4); cy += 18;
    break;
  case SETTINGS_TAB_KEYBOARD:
    font_draw_string(s, f, cx, cy, "Layout: us", 0xCDD6F4); cy += 18;
    font_draw_string(s, f, cx, cy, "Available: us, br-abnt2", 0xCDD6F4); cy += 18;
    break;
  case SETTINGS_TAB_LANGUAGE:
    font_draw_string(s, f, cx, cy, "System: pt-BR", 0xCDD6F4); cy += 18;
    font_draw_string(s, f, cx, cy, "Available: pt-BR, en, es", 0xCDD6F4); cy += 18;
    break;
  case SETTINGS_TAB_USERS:
    font_draw_string(s, f, cx, cy, "admin (UID 0)", 0xCDD6F4); cy += 18;
    font_draw_string(s, f, cx, cy, "Use CLI 'add-user' to create users", 0x6C7086); cy += 18;
    break;
  case SETTINGS_TAB_UPDATES:
    font_draw_string(s, f, cx, cy, "Channel: stable", 0xCDD6F4); cy += 18;
    font_draw_string(s, f, cx, cy, "Use 'update-status' in CLI for details", 0x6C7086); cy += 18;
    break;
  case SETTINGS_TAB_ABOUT:
    font_draw_string(s, f, cx, cy, "CapyOS 0.8.0-alpha.0", 0xCDD6F4); cy += 18;
    font_draw_string(s, f, cx, cy, "Developer: Henrique Schwarz Souza Farisco", 0xCDD6F4); cy += 18;
    font_draw_string(s, f, cx, cy, "License: Apache-2.0", 0xCDD6F4); cy += 18;
    font_draw_string(s, f, cx, cy, "Track: UEFI/GPT/x86_64", 0x6C7086); cy += 18;
    break;
  default:
    break;
  }
}
