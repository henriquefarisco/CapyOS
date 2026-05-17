/*
 * src/apps/settings.c
 *
 * Entry point and lifecycle for the Settings application. After the
 * 2026-05-15 refactor this file holds:
 *
 *   - module-level singleton state and tab-label helpers
 *   - the click-row infrastructure shared with the view layer
 *   - small numeric formatters reused by view/actions (u32/IPv4)
 *   - window lifecycle (open, switch_tab, cleanup, on_close)
 *   - window event dispatchers (paint/resize/mouse handlers)
 *   - the click router that dispatches to action callbacks
 *
 * Painting + drawing primitives live in `settings_view.c`.
 * Inline-prompt action callbacks (theme/keyboard/language apply,
 * user creation, browser homepage edit) live in
 * `settings_actions.c`. The split was driven by the 900-line layout
 * limit documented in `docs/architecture/source-layout.md`.
 */
#include "apps/settings.h"
#include "gui/compositor.h"
#include "gui/font.h"
#include "gui/widget.h"
#include "lang/app_language.h"
#include "lang/localization.h"
#include "memory/kmem.h"
#include "util/kstring.h"

#include "apps/internal/settings_internal.h"

#include <stddef.h>
#include <stdint.h>

/* Singleton state. */
struct settings_app g_settings;
int g_settings_open = 0;

/* Click-row hit table populated each paint by paint_option_row /
 * paint_action_button (settings_view.c) and consulted by the mouse
 * handler below. */
struct settings_click_row g_rows[SETTINGS_MAX_ROWS];
uint32_t g_row_count = 0u;

/* ── numeric formatters ──────────────────────────────────────────────── */

void settings_u32_str(uint32_t v, char *buf, int len) {
  int p = 0;
  if (v == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
  char t[12]; int tp = 0;
  while (v && tp < 11) { t[tp++] = '0' + (v % 10); v /= 10; }
  for (int i = tp - 1; i >= 0 && p < len - 1; i--) buf[p++] = t[i];
  buf[p] = '\0';
}

void ipv4_str(uint32_t ip, char *out, int len) {
  char tmp[4];
  int p = 0;
  for (int i = 3; i >= 0; i--) {
    settings_u32_str((ip >> (i * 8)) & 0xFF, tmp, 4);
    for (int j = 0; tmp[j] && p < len - 1; j++) out[p++] = tmp[j];
    if (i > 0 && p < len - 1) out[p++] = '.';
  }
  out[p] = '\0';
}

/* ── tab labels ──────────────────────────────────────────────────────── */

/* Etapa F4 settings (2026-05-03): inserido "Browser" entre Language
 * e Users. Mantenha sincronizado com `enum settings_tab`.
 * Etapa F4 i18n (2026-05-03): nomes traduzidos via tab_label() em
 * runtime; este array fica como fallback EN para debug/serialização. */
static const char *tab_names[SETTINGS_TAB_COUNT] = {
  "Display", "Network", "Keyboard", "Language", "Browser",
  "Users", "Updates", "About"
};

const char *tab_label(enum settings_tab t) {
  const char *lang = app_current_language();
  int idx = (int)t;
  switch (t) {
    case SETTINGS_TAB_DISPLAY:
      return localization_select(lang, "Tela", "Display", "Pantalla");
    case SETTINGS_TAB_NETWORK:
      return localization_select(lang, "Rede", "Network", "Red");
    case SETTINGS_TAB_KEYBOARD:
      return localization_select(lang, "Teclado", "Keyboard", "Teclado");
    case SETTINGS_TAB_LANGUAGE:
      return localization_select(lang, "Idioma", "Language", "Idioma");
    case SETTINGS_TAB_BROWSER:
      return localization_select(lang, "Navegador", "Browser",
                                  "Navegador");
    case SETTINGS_TAB_USERS:
      return localization_select(lang, "Usuarios", "Users", "Usuarios");
    case SETTINGS_TAB_UPDATES:
      return localization_select(lang, "Atualizacoes", "Updates",
                                  "Actualizaciones");
    case SETTINGS_TAB_ABOUT:
      return localization_select(lang, "Sobre", "About", "Acerca de");
    default:
      if (idx >= 0 && idx < (int)SETTINGS_TAB_COUNT) return tab_names[idx];
      return tab_names[SETTINGS_TAB_DISPLAY];
  }
}

/* ── status helper ───────────────────────────────────────────────────── */

void settings_set_status(struct settings_app *app, const char *text,
                         uint32_t color) {
  if (!app) return;
  kstrcpy(app->status_text, sizeof(app->status_text), text ? text : "");
  app->status_color = color;
  if (app->window) compositor_invalidate(app->window->id);
}

/* ── click-row infrastructure ────────────────────────────────────────── */

void rows_reset(void) { g_row_count = 0u; }

void rows_add(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t kind,
              const char *arg) {
  if (g_row_count >= SETTINGS_MAX_ROWS) return;
  struct settings_click_row *r = &g_rows[g_row_count++];
  r->x0 = x0; r->y0 = y0; r->x1 = x1; r->y1 = y1;
  r->kind = kind;
  r->arg[0] = '\0';
  if (arg) kstrcpy(r->arg, sizeof(r->arg), arg);
}

const struct settings_click_row *rows_hit(int32_t x, int32_t y) {
  for (uint32_t i = 0; i < g_row_count; ++i) {
    const struct settings_click_row *r = &g_rows[i];
    if (x >= r->x0 && x < r->x1 && y >= r->y0 && y < r->y1) return r;
  }
  return NULL;
}

/* ── window lifecycle ────────────────────────────────────────────────── */

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

/* 2026-05-02: repaint after a user resize drag (see
 * src/apps/calculator.c for the rationale). */
static void settings_window_resize(struct gui_window *win,
                                   uint32_t w, uint32_t h) {
  (void)w;
  (void)h;
  if (!win || !win->user_data) return;
  settings_layout_tabs((struct settings_app *)win->user_data);
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
  /* Tabs primeiro: trocar de tab consome o click. */
  int handled = 0;
  for (int i = 0; i < SETTINGS_TAB_COUNT; i++) {
    if (app->tab_buttons[i] && widget_handle_event(app->tab_buttons[i], &ev)) {
      handled = 1;
      break;
    }
  }
  if (handled) return;

  /* Etapa F4 settings-actions (2026-05-03): hit-test das linhas
   * clicaveis registradas pelo paint da tab ativa. Cada acao faz
   * persistencia + atualizacao do buffer em memoria; o desktop
   * polling re-aplica o tema/idioma/keyboard automaticamente no
   * proximo frame. */
  const struct settings_click_row *r = rows_hit(x, y);
  if (!r) return;
  switch (r->kind) {
    case SETTINGS_ROW_THEME:    apply_theme_choice(r->arg);    break;
    case SETTINGS_ROW_KEYBOARD: apply_keyboard_choice(r->arg); break;
    case SETTINGS_ROW_LANGUAGE: apply_language_choice(r->arg); break;
    case SETTINGS_ROW_NEWUSER:  start_user_creation();         break;
    case SETTINGS_ROW_HOMEPAGE: start_homepage_edit();          break;
    default: break;
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
  g_settings.window->on_resize = settings_window_resize;
  compositor_show_window(g_settings.window->id);
  compositor_focus_window(g_settings.window->id);

  /* Create tab buttons on the left sidebar */
  for (int i = 0; i < SETTINGS_TAB_COUNT; i++) {
    struct widget *btn = widget_create(WIDGET_BUTTON, g_settings.window);
    struct widget_style st;
    if (!btn) continue;
    widget_set_bounds(btn, 4, 4 + i * (int32_t)(tab_h + tab_gap), tab_w, tab_h);
    widget_set_text(btn, tab_label((enum settings_tab)i));
    st = widget_button_style();
    st.bg_color = (i == 0) ? theme->accent : theme->accent_alt;
    st.text_color = (i == 0) ? theme->accent_text : theme->text;
    widget_set_style(btn, &st);
    widget_set_on_click(btn, on_tab_click, &g_settings);
    g_settings.tab_buttons[i] = btn;
  }

  settings_layout_tabs(&g_settings);
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
