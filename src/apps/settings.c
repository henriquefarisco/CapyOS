#include "apps/settings.h"
#include "gui/compositor.h"
#include "gui/font.h"
#include "gui/widget.h"
#include "util/kstring.h"
#include "core/system_init.h"
#include "core/version.h"
#include "drivers/input/keyboard.h"
#include "net/stack.h"
#include "auth/user.h"
#include "services/update_agent.h"
#include "memory/kmem.h"
#include <stddef.h>

static void settings_u32_str(uint32_t v, char *buf, int len) {
  int p = 0;
  if (v == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
  char t[12]; int tp = 0;
  while (v && tp < 11) { t[tp++] = '0' + (v % 10); v /= 10; }
  for (int i = tp - 1; i >= 0 && p < len - 1; i--) buf[p++] = t[i];
  buf[p] = '\0';
}

static void ipv4_str(uint32_t ip, char *out, int len) {
  char tmp[4];
  int p = 0;
  for (int i = 3; i >= 0; i--) {
    settings_u32_str((ip >> (i * 8)) & 0xFF, tmp, 4);
    for (int j = 0; tmp[j] && p < len - 1; j++) out[p++] = tmp[j];
    if (i > 0 && p < len - 1) out[p++] = '.';
  }
  out[p] = '\0';
}

static struct settings_app g_settings;
static int g_settings_open = 0;

static const char *tab_names[SETTINGS_TAB_COUNT] = {
  "Display", "Network", "Keyboard", "Language", "Users", "Updates", "About"
};

static void settings_cleanup(void) {
  /* Free tab button widgets */
  for (int i = 0; i < SETTINGS_TAB_COUNT; i++) {
    if (g_settings.tab_buttons[i]) {
      widget_destroy(g_settings.tab_buttons[i]);
      g_settings.tab_buttons[i] = NULL;
    }
  }
  if (g_settings.content_panel) {
    widget_destroy(g_settings.content_panel);
    g_settings.content_panel = NULL;
  }
  g_settings.window = NULL;
  g_settings_open = 0;
}

static void settings_on_close(struct gui_window *win) {
  (void)win;
  settings_cleanup();
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
  kmemzero(&ev, sizeof(ev));
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

  /* If already open, just focus the existing window */
  if (g_settings_open && g_settings.window) {
    compositor_show_window(g_settings.window->id);
    compositor_focus_window(g_settings.window->id);
    return;
  }

  /* Clean up stale state */
  settings_cleanup();
  kmemzero(&g_settings, sizeof(g_settings));

  g_settings.window = compositor_create_window("Settings", 100, 70, width, height);
  if (!g_settings.window) return;
  g_settings.window->bg_color = theme->window_bg;
  g_settings.window->border_color = theme->window_border;
  g_settings.window->user_data = &g_settings;
  g_settings.window->on_paint = settings_window_paint;
  g_settings.window->on_mouse = settings_window_mouse;
  g_settings.window->on_close = settings_on_close;
  compositor_show_window(g_settings.window->id);
  compositor_focus_window(g_settings.window->id);

  /* Create tab buttons on the left sidebar */
  for (int i = 0; i < SETTINGS_TAB_COUNT; i++) {
    struct widget *btn = widget_create(WIDGET_BUTTON, g_settings.window);
    struct widget_style st;
    if (!btn) continue;
    widget_set_bounds(btn, 4, 4 + i * (int32_t)(tab_h + tab_gap), tab_w, tab_h);
    widget_set_text(btn, tab_names[i]);
    st = widget_button_style();
    st.bg_color = (i == 0) ? theme->accent : theme->accent_alt;
    st.text_color = (i == 0) ? theme->accent_text : theme->text;
    widget_set_style(btn, &st);
    widget_set_on_click(btn, on_tab_click, &g_settings);
    g_settings.tab_buttons[i] = btn;
  }

  g_settings.active_tab = SETTINGS_TAB_DISPLAY;
  g_settings_open = 1;
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
  case SETTINGS_TAB_DISPLAY: {
    struct system_settings live;
    char line[80];
    if (system_load_settings(&live) == 0) {
      line[0] = '\0'; kbuf_append(line, sizeof(line), "Theme: ");
      kbuf_append(line, sizeof(line), live.theme[0] ? live.theme : "capyos");
      font_draw_string(s, f, cx, cy, line, theme->text); cy += 18;
      font_draw_string(s, f, cx, cy, live.splash_enabled ? "Splash: enabled" : "Splash: disabled", theme->text); cy += 18;
    } else {
      font_draw_string(s, f, cx, cy, "(config not loaded)", theme->text_muted); cy += 18;
    }
    font_draw_string(s, f, cx, cy, "Themes: capyos, ocean, forest", theme->text_muted); cy += 18;
    font_draw_string(s, f, cx, cy, "Use CLI config-theme / config-splash", theme->text_muted); cy += 18;
    break;
  }
  case SETTINGS_TAB_NETWORK: {
    struct net_stack_status ns;
    char line[80];
    if (net_stack_status(&ns) == 0) {
      font_draw_string(s, f, cx, cy, ns.ready ? "Status: ready" : "Status: not ready", theme->text); cy += 18;
      line[0] = '\0'; kbuf_append(line, sizeof(line), "IPv4: ");
      { char ip[16]; ipv4_str(ns.ipv4.addr, ip, 16); kbuf_append(line, sizeof(line), ip); }
      font_draw_string(s, f, cx, cy, line, theme->text); cy += 18;
      line[0] = '\0'; kbuf_append(line, sizeof(line), "Gateway: ");
      { char ip[16]; ipv4_str(ns.ipv4.gateway, ip, 16); kbuf_append(line, sizeof(line), ip); }
      font_draw_string(s, f, cx, cy, line, theme->text); cy += 18;
      line[0] = '\0'; kbuf_append(line, sizeof(line), "DNS: ");
      { char ip[16]; ipv4_str(ns.ipv4.dns, ip, 16); kbuf_append(line, sizeof(line), ip); }
      font_draw_string(s, f, cx, cy, line, theme->text); cy += 18;
    } else {
      font_draw_string(s, f, cx, cy, "Network: unavailable", theme->text_muted); cy += 18;
    }
    font_draw_string(s, f, cx, cy, "Use CLI net-set to configure", theme->text_muted); cy += 18;
    break;
  }
  case SETTINGS_TAB_KEYBOARD: {
    char line[80];
    const char *layout = keyboard_current_layout();
    line[0] = '\0'; kbuf_append(line, sizeof(line), "Current: ");
    kbuf_append(line, sizeof(line), layout ? layout : "us");
    font_draw_string(s, f, cx, cy, line, theme->text); cy += 18;
    font_draw_string(s, f, cx, cy, "Available: us, br-abnt2", theme->text); cy += 18;
    font_draw_string(s, f, cx, cy, "Use CLI config-keyboard <layout>", theme->text_muted); cy += 18;
    break;
  }
  case SETTINGS_TAB_LANGUAGE: {
    struct system_settings live;
    char line[80];
    if (system_load_settings(&live) == 0 && live.language[0]) {
      line[0] = '\0'; kbuf_append(line, sizeof(line), "Current: ");
      kbuf_append(line, sizeof(line), live.language);
      font_draw_string(s, f, cx, cy, line, theme->text); cy += 18;
    } else {
      font_draw_string(s, f, cx, cy, "Current: en", theme->text); cy += 18;
    }
    font_draw_string(s, f, cx, cy, "Available: en, pt-BR, es", theme->text); cy += 18;
    font_draw_string(s, f, cx, cy, "Use CLI config-language <lang>", theme->text_muted); cy += 18;
    break;
  }
  case SETTINGS_TAB_USERS: {
    char line[80];
    int has_users = userdb_has_any_user();
    line[0] = '\0'; kbuf_append(line, sizeof(line), "Users: ");
    kbuf_append(line, sizeof(line), has_users ? "configured" : "none");
    font_draw_string(s, f, cx, cy, line, theme->text); cy += 18;
    font_draw_string(s, f, cx, cy, "Use CLI list-users to view", theme->text_muted); cy += 18;
    font_draw_string(s, f, cx, cy, "Use CLI add-user to create", theme->text_muted); cy += 18;
    break;
  }
  case SETTINGS_TAB_UPDATES: {
    struct system_update_status us;
    char line[80];
    update_agent_status_get(&us);
    line[0] = '\0'; kbuf_append(line, sizeof(line), "Channel: ");
    kbuf_append(line, sizeof(line), us.channel[0] ? us.channel : "stable");
    font_draw_string(s, f, cx, cy, line, theme->text); cy += 18;
    line[0] = '\0'; kbuf_append(line, sizeof(line), "Branch: ");
    kbuf_append(line, sizeof(line), us.branch[0] ? us.branch : "main");
    font_draw_string(s, f, cx, cy, line, theme->text); cy += 18;
    font_draw_string(s, f, cx, cy, us.update_available ? "Update: available" : "Update: up to date", theme->text); cy += 18;
    if (us.available_version[0]) {
      line[0] = '\0'; kbuf_append(line, sizeof(line), "Version: ");
      kbuf_append(line, sizeof(line), us.available_version);
      font_draw_string(s, f, cx, cy, line, theme->text); cy += 18;
    }
    break;
  }
  case SETTINGS_TAB_ABOUT:
    font_draw_string(s, f, cx, cy, "CapyOS " CAPYOS_VERSION_FULL, theme->text); cy += 18;
    font_draw_string(s, f, cx, cy, "Channel: " CAPYOS_VERSION_CHANNEL, theme->text); cy += 18;
    font_draw_string(s, f, cx, cy, "Developer: Henrique Schwarz Souza Farisco", theme->text); cy += 18;
    font_draw_string(s, f, cx, cy, "License: Apache-2.0", theme->text); cy += 18;
    font_draw_string(s, f, cx, cy, "Track: UEFI/GPT/x86_64", theme->text_muted); cy += 18;
    break;
  default:
    break;
  }
}
