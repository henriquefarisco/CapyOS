#include "apps/settings.h"
#include "gui/compositor.h"
#include "gui/font.h"
#include "gui/widget.h"
#include "gui/inline_prompt.h"
#include "util/kstring.h"
#include "core/system_init.h"
#include "core/version.h"
#include "drivers/input/keyboard.h"
#include "net/stack.h"
#include "auth/user.h"
#include "auth/user_prefs.h"
#include "auth/session.h"
#include "lang/localization.h"
#include "lang/app_language.h"
#include "services/update_agent.h"
#include "memory/kmem.h"
#include <stddef.h>

/* Etapa F4 settings-actions (2026-05-03): acesso ao buffer de
 * settings em memoria do kernel (declarado em
 * arch/x86_64/kernel_io_helpers.c). Updates em runtime aqui
 * sao detectados pelo desktop loop em desktop_run_frame, que
 * compara contra `ds->theme_name` e re-aplica via
 * compositor_apply_theme. Sem essa atualizacao em memoria, mudancas
 * gravadas em disco (system_save_*) so apareceriam no proximo boot. */
extern struct system_settings g_shell_settings;

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

/* Etapa F4 settings (2026-05-03): inserido "Browser" entre Language
 * e Users. Mantenha sincronizado com `enum settings_tab`.
 * Etapa F4 i18n (2026-05-03): nomes traduzidos via tab_label() em
 * runtime; este array fica como fallback EN para debug/serialização. */
static const char *tab_names[SETTINGS_TAB_COUNT] = {
  "Display", "Network", "Keyboard", "Language", "Browser",
  "Users", "Updates", "About"
};

/* Retorna o label localizado da tab pelo idioma da sessao ativa. */
static const char *tab_label(enum settings_tab t) {
  const char *lang = app_current_language();
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
      return tab_names[t];
  }
}

/* Etapa F4 settings-actions (2026-05-03): infraestrutura de "linhas
 * clicaveis" (immediate-mode UI). O paint de cada tab registra os
 * retangulos clicaveis em `g_rows[]` via `rows_add()`; o handler
 * de mouse faz hit-test e dispatch. Tudo sem alocar widgets
 * dinamicos para nao acrescentar custo de paint em tabs sem
 * acoes (Network/Updates/About). */
#define SETTINGS_MAX_ROWS 24
#define SETTINGS_ROW_THEME      1u
#define SETTINGS_ROW_KEYBOARD   2u
#define SETTINGS_ROW_LANGUAGE   3u
#define SETTINGS_ROW_NEWUSER    4u
#define SETTINGS_ROW_HOMEPAGE   5u

struct settings_click_row {
  int32_t  x0, y0, x1, y1;
  uint16_t kind;
  char     arg[40];
};

static struct settings_click_row g_rows[SETTINGS_MAX_ROWS];
static uint32_t g_row_count = 0u;

/* Buffer estatico usado para encadeamento username -> password no
 * fluxo de criacao de usuario via inline_prompt. inline_prompt
 * dispara on_submit() e fecha; o callback de username re-abre um
 * novo prompt para password lendo de g_pending_username. */
static char g_pending_username[USER_NAME_MAX] = {0};

static void rows_reset(void) { g_row_count = 0u; }

static void rows_add(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                     uint16_t kind, const char *arg) {
  if (g_row_count >= SETTINGS_MAX_ROWS) return;
  struct settings_click_row *r = &g_rows[g_row_count++];
  r->x0 = x0; r->y0 = y0; r->x1 = x1; r->y1 = y1;
  r->kind = kind;
  r->arg[0] = '\0';
  if (arg) kstrcpy(r->arg, sizeof(r->arg), arg);
}

static const struct settings_click_row *rows_hit(int32_t x, int32_t y) {
  for (uint32_t i = 0; i < g_row_count; ++i) {
    const struct settings_click_row *r = &g_rows[i];
    if (x >= r->x0 && x < r->x1 && y >= r->y0 && y < r->y1) return r;
  }
  return NULL;
}

/* === Action helpers ====================================================
 * Cada acao atualiza /system/config.ini (persistencia) E o buffer
 * em memoria g_shell_settings (visibilidade imediata) sem precisar
 * de acesso ao desktop_session. */

static void apply_theme_choice(const char *theme) {
  if (!theme || !theme[0]) return;
  if (system_save_theme(theme) == 0) {
    kstrcpy(g_shell_settings.theme, sizeof(g_shell_settings.theme), theme);
  }
}

static void apply_keyboard_choice(const char *layout) {
  if (!layout || !layout[0]) return;
  /* Aplica imediatamente em runtime + persiste. */
  (void)keyboard_set_layout_by_name(layout);
  if (system_save_keyboard_layout(layout) == 0) {
    kstrcpy(g_shell_settings.keyboard_layout,
            sizeof(g_shell_settings.keyboard_layout), layout);
  }
}

static void apply_language_choice(const char *lang) {
  if (!lang || !lang[0]) return;
  const char *normalized = localization_normalize_language(lang);
  if (!normalized) normalized = lang;
  struct session_context *sess = session_active();
  if (sess) {
    kstrcpy(sess->prefs.language, sizeof(sess->prefs.language), normalized);
    const struct user_record *user = session_user(sess);
    if (user) (void)user_prefs_save_language(user, normalized);
  }
}

/* Callback do segundo prompt (password) na criacao de usuario. */
static void on_password_submit(const char *pwd, void *ctx) {
  (void)ctx;
  if (!g_pending_username[0]) return;
  if (!pwd || !pwd[0]) {
    g_pending_username[0] = '\0';
    return;
  }
  uint32_t uid = 0u, gid = 0u;
  if (userdb_next_ids(&uid, &gid) != 0) {
    g_pending_username[0] = '\0';
    return;
  }
  char home[USER_HOME_MAX];
  kstrcpy(home, sizeof(home), "/home/");
  size_t hl = kstrlen(home);
  for (size_t i = 0; g_pending_username[i] && hl + 1 < sizeof(home); ++i) {
    home[hl++] = g_pending_username[i];
  }
  home[hl] = '\0';
  struct user_record rec;
  if (user_record_init(g_pending_username, pwd, "user", uid, gid,
                        home, &rec) == 0) {
    (void)userdb_add(&rec);
  }
  g_pending_username[0] = '\0';
}

/* Callback do primeiro prompt (username). Re-abre um segundo
 * prompt para password mantendo o nome digitado em
 * g_pending_username. */
static void on_username_submit(const char *name, void *ctx) {
  (void)ctx;
  if (!name || !name[0]) {
    g_pending_username[0] = '\0';
    return;
  }
  kstrcpy(g_pending_username, sizeof(g_pending_username), name);
  inline_prompt_show("Password (chars only):", "",
                     200, 220, on_password_submit, NULL);
}

static void start_user_creation(void) {
  g_pending_username[0] = '\0';
  inline_prompt_show("New username:", "",
                     200, 200, on_username_submit, NULL);
}

/* Callback do prompt de homepage do browser. */
static void on_homepage_submit(const char *url, void *ctx) {
  (void)ctx;
  const char *value = (url && url[0]) ? url : "https://wikipedia.org";
  struct system_settings live;
  system_load_settings(&live);
  kstrcpy(live.browser_homepage, sizeof(live.browser_homepage), value);
  if (system_save_settings(&live) == 0) {
    kstrcpy(g_shell_settings.browser_homepage,
            sizeof(g_shell_settings.browser_homepage), value);
  }
}

static void start_homepage_edit(void) {
  struct system_settings live;
  const char *cur = "https://wikipedia.org";
  if (system_load_settings(&live) == 0 && live.browser_homepage[0]) {
    cur = live.browser_homepage;
  }
  inline_prompt_show("Homepage:", cur,
                     200, 220, on_homepage_submit, NULL);
}

/* Pinta uma "linha-opcao" 200x22 com label + indicador de
 * selecao corrente. `selected` muda bg e text color. Retorna o
 * proximo y para a linha seguinte. Tambem registra a area como
 * clicavel via rows_add() com kind/arg passados. */
static int32_t paint_option_row(struct gui_surface *s, const struct font *f,
                                 const struct gui_theme_palette *theme,
                                 int32_t x, int32_t y, int32_t w,
                                 const char *label, int selected,
                                 uint16_t kind, const char *arg) {
  uint32_t bg     = selected ? theme->accent : theme->accent_alt;
  uint32_t fg     = theme->accent_text;
  uint32_t border = theme->window_border;
  uint32_t h = 22u;
  /* Fundo. */
  for (uint32_t r = 0; r < h; ++r) {
    int32_t py = y + (int32_t)r;
    if (py < 0 || (uint32_t)py >= s->height) continue;
    uint32_t *row = (uint32_t *)((uint8_t *)s->pixels +
                                  (uint32_t)py * s->pitch);
    for (int32_t c = 0; c < w; ++c) {
      int32_t px = x + c;
      if (px < 0 || (uint32_t)px >= s->width) continue;
      int edge = (r == 0u || r == h - 1u || c == 0 || c == w - 1);
      row[px] = edge ? border : bg;
    }
  }
  if (label) {
    /* Indicador de "ativo": prefixa ">> " quando selected. */
    char buf[64];
    if (selected) kstrcpy(buf, sizeof(buf), ">> ");
    else          kstrcpy(buf, sizeof(buf), "   ");
    size_t bl = kstrlen(buf);
    for (size_t i = 0; label[i] && bl + 1 < sizeof(buf); ++i) {
      buf[bl++] = label[i];
    }
    buf[bl] = '\0';
    font_draw_string(s, f, x + 6, y + 7, buf, fg);
  }
  rows_add(x, y, x + w, y + (int32_t)h, kind, arg);
  return y + (int32_t)h + 4;
}

/* Pinta um botao simples ("Add user", "Edit homepage") sem indicador
 * de selecao -- so label + borda. Registra como clicavel. */
static int32_t paint_action_button(struct gui_surface *s,
                                    const struct font *f,
                                    const struct gui_theme_palette *theme,
                                    int32_t x, int32_t y, int32_t w,
                                    const char *label, uint16_t kind) {
  uint32_t bg     = theme->accent;
  uint32_t fg     = theme->accent_text;
  uint32_t border = theme->window_border;
  uint32_t h = 22u;
  for (uint32_t r = 0; r < h; ++r) {
    int32_t py = y + (int32_t)r;
    if (py < 0 || (uint32_t)py >= s->height) continue;
    uint32_t *row = (uint32_t *)((uint8_t *)s->pixels +
                                  (uint32_t)py * s->pitch);
    for (int32_t c = 0; c < w; ++c) {
      int32_t px = x + c;
      if (px < 0 || (uint32_t)px >= s->width) continue;
      int edge = (r == 0u || r == h - 1u || c == 0 || c == w - 1);
      row[px] = edge ? border : bg;
    }
  }
  if (label) font_draw_string(s, f, x + 6, y + 7, label, fg);
  rows_add(x, y, x + w, y + (int32_t)h, kind, NULL);
  return y + (int32_t)h + 4;
}

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

  /* Etapa F4 settings-actions (2026-05-03): zera lista de rows
   * clicaveis no inicio de cada paint. Cada tab re-registra suas
   * linhas via paint_option_row/paint_action_button conforme pinta. */
  rows_reset();

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

  font_draw_string(s, f, cx, cy, tab_label(app->active_tab), theme->accent);
  cy += 24;

  /* Separator */
  for (uint32_t x = (uint32_t)cx; x < s->width - 8; x++) {
    uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + (uint32_t)cy * s->pitch);
    line[x] = theme->window_border;
  }
  cy += 8;

  switch (app->active_tab) {
  case SETTINGS_TAB_DISPLAY: {
    /* Etapa F4 settings-actions (2026-05-03): cada tema vira uma
     * linha clicavel. Click aplica imediatamente via
     * apply_theme_choice -> system_save_theme + g_shell_settings
     * update; o desktop loop re-aplica visualmente no proximo frame. */
    const char *lang = app_current_language();
    struct system_settings live;
    const char *current = "capyos";
    if (system_load_settings(&live) == 0 && live.theme[0]) {
      current = live.theme;
    }
    font_draw_string(s, f, cx, cy,
                     localization_select(lang, "Tema (clique para aplicar):",
                                          "Theme (click to apply):",
                                          "Tema (clic para aplicar):"),
                     theme->text);
    cy += 18;
    static const char *const k_themes[] = {
      "capyos", "ocean", "forest", "love", "high-contrast"
    };
    for (size_t i = 0; i < sizeof(k_themes)/sizeof(k_themes[0]); ++i) {
      cy = paint_option_row(s, f, theme, cx, cy, 220, k_themes[i],
                             kstreq(current, k_themes[i]),
                             SETTINGS_ROW_THEME, k_themes[i]);
    }
    cy += 4;
    char line[80];
    line[0] = '\0';
    kbuf_append(line, sizeof(line),
                 localization_select(lang, "Splash: ", "Splash: ",
                                      "Splash: "));
    int splash_on = (system_load_settings(&live) == 0 && live.splash_enabled);
    kbuf_append(line, sizeof(line),
                 splash_on
                 ? localization_select(lang, "habilitado", "enabled",
                                        "habilitado")
                 : localization_select(lang, "desabilitado", "disabled",
                                        "deshabilitado"));
    font_draw_string(s, f, cx, cy, line, theme->text_muted); cy += 18;
    break;
  }
  case SETTINGS_TAB_NETWORK: {
    const char *lang = app_current_language();
    struct net_stack_status ns;
    char line[80];
    if (net_stack_status(&ns) == 0) {
      const char *st_label = ns.ready
          ? localization_select(lang, "Status: pronto", "Status: ready",
                                 "Estado: listo")
          : localization_select(lang, "Status: indisponivel",
                                 "Status: not ready", "Estado: no listo");
      font_draw_string(s, f, cx, cy, st_label, theme->text); cy += 18;
      line[0] = '\0'; kbuf_append(line, sizeof(line), "IPv4: ");
      { char ip[16]; ipv4_str(ns.ipv4.addr, ip, 16); kbuf_append(line, sizeof(line), ip); }
      font_draw_string(s, f, cx, cy, line, theme->text); cy += 18;
      line[0] = '\0';
      kbuf_append(line, sizeof(line),
                   localization_select(lang, "Gateway: ", "Gateway: ",
                                        "Puerta: "));
      { char ip[16]; ipv4_str(ns.ipv4.gateway, ip, 16); kbuf_append(line, sizeof(line), ip); }
      font_draw_string(s, f, cx, cy, line, theme->text); cy += 18;
      line[0] = '\0'; kbuf_append(line, sizeof(line), "DNS: ");
      { char ip[16]; ipv4_str(ns.ipv4.dns, ip, 16); kbuf_append(line, sizeof(line), ip); }
      font_draw_string(s, f, cx, cy, line, theme->text); cy += 18;
    } else {
      font_draw_string(s, f, cx, cy,
                       localization_select(lang, "Rede: indisponivel",
                                            "Network: unavailable",
                                            "Red: no disponible"),
                       theme->text_muted); cy += 18;
    }
    font_draw_string(s, f, cx, cy,
                     localization_select(lang,
                                          "Use o CLI 'net-set' para configurar",
                                          "Use CLI net-set to configure",
                                          "Use el CLI 'net-set' para configurar"),
                     theme->text_muted); cy += 18;
    break;
  }
  case SETTINGS_TAB_KEYBOARD: {
    /* Etapa F4 settings-actions (2026-05-03): keyboard layouts
     * descobertos via keyboard_layout_count/name. Cada um e
     * clicavel; aplica imediatamente em runtime + persiste. */
    const char *lang = app_current_language();
    const char *current = keyboard_current_layout();
    if (!current) current = "us";
    font_draw_string(s, f, cx, cy,
                     localization_select(lang,
                                          "Layout (clique para aplicar):",
                                          "Layout (click to apply):",
                                          "Distribucion (clic para aplicar):"),
                     theme->text);
    cy += 18;
    size_t n = keyboard_layout_count();
    for (size_t i = 0; i < n; ++i) {
      const char *name = keyboard_layout_name(i);
      if (!name) continue;
      cy = paint_option_row(s, f, theme, cx, cy, 220, name,
                             kstreq(current, name),
                             SETTINGS_ROW_KEYBOARD, name);
    }
    break;
  }
  case SETTINGS_TAB_LANGUAGE: {
    /* Etapa F4 settings-actions (2026-05-03): idioma e por-usuario
     * (user_prefs). apply_language_choice atualiza session->prefs +
     * persiste em user_prefs.ini. */
    const char *lang = app_current_language();
    const char *current = lang;
    font_draw_string(s, f, cx, cy,
                     localization_select(lang,
                                          "Idioma (clique para aplicar):",
                                          "Language (click to apply):",
                                          "Idioma (clic para aplicar):"),
                     theme->text);
    cy += 18;
    static const char *const k_langs[] = { "pt-BR", "en", "es" };
    for (size_t i = 0; i < sizeof(k_langs)/sizeof(k_langs[0]); ++i) {
      cy = paint_option_row(s, f, theme, cx, cy, 220, k_langs[i],
                             kstreq(current, k_langs[i]),
                             SETTINGS_ROW_LANGUAGE, k_langs[i]);
    }
    cy += 4;
    font_draw_string(s, f, cx, cy,
                     localization_select(lang,
                                          "Salvo por-usuario em /home/<user>/.capyos/prefs.ini",
                                          "Saved per-user in /home/<user>/.capyos/prefs.ini",
                                          "Guardado por usuario en /home/<user>/.capyos/prefs.ini"),
                     theme->text_muted); cy += 18;
    break;
  }
  /* Etapa F4 settings (2026-05-03): tab Browser. Mostra a homepage
   * configurada no /system/config.ini (browser_homepage=). Usa
   * uma linha enxuta com prefixo "Home: " seguido do URL. Para
   * URLs longas (>54 chars apos "Home: "), trunca com "..." para
   * caber em line[80]. Mostra hint sobre como editar (CLI por
   * enquanto; futuramente in-place via inline_prompt). */
  case SETTINGS_TAB_BROWSER: {
    /* Etapa F4 settings-actions (2026-05-03): tab Browser com
     * botao "Edit homepage" que abre inline_prompt e persiste o
     * novo URL via on_homepage_submit. */
    const char *lang = app_current_language();
    struct system_settings live;
    char line[80];
    const char *hp = "https://wikipedia.org";
    if (system_load_settings(&live) == 0 && live.browser_homepage[0]) {
      hp = live.browser_homepage;
    }
    line[0] = '\0';
    kbuf_append(line, sizeof(line),
                 localization_select(lang, "Pagina inicial: ",
                                      "Home: ", "Pagina principal: "));
    int prefix_len = (int)kstrlen(line);
    int remain = (int)sizeof(line) - 1 - prefix_len;
    int hp_len = 0;
    while (hp[hp_len] && hp_len < 256) hp_len++;
    if (hp_len <= remain) {
      kbuf_append(line, sizeof(line), hp);
    } else {
      char trunc[64];
      int copy = remain - 3;
      if (copy < 0) copy = 0;
      if (copy > (int)sizeof(trunc) - 1) copy = (int)sizeof(trunc) - 1;
      for (int i = 0; i < copy; i++) trunc[i] = hp[i];
      trunc[copy] = '\0';
      kbuf_append(line, sizeof(line), trunc);
      kbuf_append(line, sizeof(line), "...");
    }
    font_draw_string(s, f, cx, cy, line, theme->text); cy += 18;
    cy = paint_action_button(s, f, theme, cx, cy, 160,
                              localization_select(lang,
                                                   "Editar pagina inicial",
                                                   "Edit homepage",
                                                   "Editar pagina principal"),
                              SETTINGS_ROW_HOMEPAGE);
    cy += 4;
    font_draw_string(s, f, cx, cy,
                     localization_select(lang,
                                          "Fallback offline: file://capyos/wikipedia",
                                          "Offline fallback: file://capyos/wikipedia",
                                          "Respaldo offline: file://capyos/wikipedia"),
                     theme->text_muted); cy += 18;
    font_draw_string(s, f, cx, cy,
                     localization_select(lang,
                                          "Toolbar: < Voltar  > Avancar  R Recarregar  H Inicial  Ir",
                                          "Toolbar: < Back  > Forward  R Reload  H Home  Go",
                                          "Toolbar: < Atras  > Adelante  R Recargar  H Inicio  Ir"),
                     theme->text_muted); cy += 18;
    break;
  }
  case SETTINGS_TAB_USERS: {
    /* Etapa F4 settings-actions (2026-05-03): tab Users com botao
     * "Add user" que dispara fluxo username -> password via
     * inline_prompt encadeado (on_username_submit -> on_password_submit). */
    const char *lang = app_current_language();
    char line[80];
    int has_users = userdb_has_any_user();
    line[0] = '\0';
    kbuf_append(line, sizeof(line),
                 localization_select(lang, "Usuarios: ",
                                      "Users: ", "Usuarios: "));
    kbuf_append(line, sizeof(line),
                 has_users
                 ? localization_select(lang, "configurados", "configured",
                                        "configurados")
                 : localization_select(lang, "nenhum", "none", "ninguno"));
    font_draw_string(s, f, cx, cy, line, theme->text); cy += 18;
    cy = paint_action_button(s, f, theme, cx, cy, 160,
                              localization_select(lang,
                                                   "Adicionar usuario...",
                                                   "Add user...",
                                                   "Anadir usuario..."),
                              SETTINGS_ROW_NEWUSER);
    cy += 4;
    font_draw_string(s, f, cx, cy,
                     localization_select(lang,
                                          "Armazenado em /etc/users.db (PBKDF2-SHA256, 64k)",
                                          "Stored in /etc/users.db (PBKDF2-SHA256, 64k rounds)",
                                          "Almacenado en /etc/users.db (PBKDF2-SHA256, 64k)"),
                     theme->text_muted); cy += 18;
    font_draw_string(s, f, cx, cy,
                     localization_select(lang,
                                          "CLI: list-users, passwd <usuario>",
                                          "CLI: list-users, passwd <user>",
                                          "CLI: list-users, passwd <usuario>"),
                     theme->text_muted); cy += 18;
    break;
  }
  case SETTINGS_TAB_UPDATES: {
    const char *lang = app_current_language();
    struct system_update_status us;
    char line[80];
    update_agent_status_get(&us);
    line[0] = '\0';
    kbuf_append(line, sizeof(line),
                 localization_select(lang, "Canal: ", "Channel: ", "Canal: "));
    kbuf_append(line, sizeof(line), us.channel[0] ? us.channel : "stable");
    font_draw_string(s, f, cx, cy, line, theme->text); cy += 18;
    line[0] = '\0';
    kbuf_append(line, sizeof(line),
                 localization_select(lang, "Branch: ", "Branch: ", "Rama: "));
    kbuf_append(line, sizeof(line), us.branch[0] ? us.branch : "main");
    font_draw_string(s, f, cx, cy, line, theme->text); cy += 18;
    const char *upd_label = us.update_available
        ? localization_select(lang, "Atualizacao: disponivel",
                               "Update: available",
                               "Actualizacion: disponible")
        : localization_select(lang, "Atualizacao: em dia",
                               "Update: up to date",
                               "Actualizacion: al dia");
    font_draw_string(s, f, cx, cy, upd_label, theme->text); cy += 18;
    if (us.available_version[0]) {
      line[0] = '\0';
      kbuf_append(line, sizeof(line),
                   localization_select(lang, "Versao: ", "Version: ",
                                        "Version: "));
      kbuf_append(line, sizeof(line), us.available_version);
      font_draw_string(s, f, cx, cy, line, theme->text); cy += 18;
    }
    break;
  }
  case SETTINGS_TAB_ABOUT: {
    const char *lang = app_current_language();
    font_draw_string(s, f, cx, cy, "CapyOS " CAPYOS_VERSION_FULL,
                     theme->text); cy += 18;
    {
      char line[80];
      line[0] = '\0';
      kbuf_append(line, sizeof(line),
                   localization_select(lang, "Canal: ", "Channel: ",
                                        "Canal: "));
      kbuf_append(line, sizeof(line), CAPYOS_VERSION_CHANNEL);
      font_draw_string(s, f, cx, cy, line, theme->text); cy += 18;
    }
    font_draw_string(s, f, cx, cy,
                     localization_select(lang,
                                          "Desenvolvedor: Henrique Schwarz Souza Farisco",
                                          "Developer: Henrique Schwarz Souza Farisco",
                                          "Desarrollador: Henrique Schwarz Souza Farisco"),
                     theme->text); cy += 18;
    font_draw_string(s, f, cx, cy,
                     localization_select(lang, "Licenca: Apache-2.0",
                                          "License: Apache-2.0",
                                          "Licencia: Apache-2.0"),
                     theme->text); cy += 18;
    font_draw_string(s, f, cx, cy,
                     localization_select(lang, "Plataforma: UEFI/GPT/x86_64",
                                          "Track: UEFI/GPT/x86_64",
                                          "Plataforma: UEFI/GPT/x86_64"),
                     theme->text_muted); cy += 18;
    break;
  }
  default:
    break;
  }
}
