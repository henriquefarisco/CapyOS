/*
 * src/apps/settings_view.c
 *
 * Painting layer of the Settings application. Owns the tab layout
 * helpers and the per-tab rendering inside `settings_paint`.
 *
 * Carved out of the pre-split monolith `src/apps/settings.c` at the
 * 2026-05-15 refactor so each translation unit stays under the
 * 900-line layout limit.
 *
 * Public symbols owned here:
 *   - `settings_paint` (declared in `include/apps/settings.h`)
 *
 * Shared internals exposed via `internal/settings_internal.h`:
 *   - `settings_sidebar_width`, `settings_layout_tabs`
 *
 * Lifecycle, click routing and action callbacks live in
 * `settings.c` and `settings_actions.c`.
 */
#include "apps/settings.h"
#include "auth/session.h"
#include "auth/user.h"
#include "core/system_init.h"
#include "core/version.h"
#include "drivers/input/keyboard.h"
#include "gui/compositor.h"
#include "gui/font.h"
#include "gui/widget.h"
#include "lang/app_language.h"
#include "lang/localization.h"
#include "net/stack.h"
#include "services/update_agent.h"
#include "util/kstring.h"

#include "apps/internal/settings_internal.h"

#include <stddef.h>
#include <stdint.h>

static void settings_fit_text(const struct font *f, const char *src,
                              uint32_t max_width, char *out,
                              size_t out_len) {
  size_t len = 0;
  size_t max_chars = 0;
  if (!out || out_len == 0) return;
  out[0] = '\0';
  if (!f || !src || max_width == 0 || f->glyph_width == 0) return;
  max_chars = max_width / f->glyph_width;
  if (max_chars == 0) return;
  while (src[len]) len++;
  if (len <= max_chars) {
    kstrcpy(out, out_len, src);
    return;
  }
  if (max_chars <= 3) {
    size_t n = max_chars;
    if (n >= out_len) n = out_len - 1;
    for (size_t i = 0; i < n; i++) out[i] = '.';
    out[n] = '\0';
    return;
  }
  {
    size_t copy = max_chars - 3;
    if (copy > out_len - 4) copy = out_len - 4;
    for (size_t i = 0; i < copy; i++) out[i] = src[i];
    out[copy] = '.';
    out[copy + 1] = '.';
    out[copy + 2] = '.';
    out[copy + 3] = '\0';
  }
}

static void settings_draw_fit(struct gui_surface *s, const struct font *f,
                              int32_t x, int32_t y, uint32_t max_width,
                              const char *text, uint32_t color) {
  char fitted[96];
  settings_fit_text(f, text, max_width, fitted, sizeof(fitted));
  if (fitted[0]) font_draw_string(s, f, x, y, fitted, color);
}

int32_t settings_sidebar_width(uint32_t surface_w, uint8_t scale) {
  int32_t w = 130 + 28 * (scale - 1);
  if (surface_w < 360u) w = 104;
  if ((uint32_t)w > surface_w / 2u) w = (int32_t)(surface_w / 2u);
  if (w < 88) w = 88;
  return w;
}

void settings_layout_tabs(struct settings_app *app) {
  if (!app || !app->window) return;
  uint8_t scale = compositor_ui_scale();
  uint32_t surface_w = app->window->surface.width;
  int32_t sidebar_w = settings_sidebar_width(surface_w, scale);
  uint32_t tab_w = (sidebar_w > 8) ? (uint32_t)(sidebar_w - 8) : 80u;
  uint32_t tab_h = 28 + 6 * (scale - 1);
  uint32_t tab_gap = 4 + 2 * (scale - 1);
  for (int i = 0; i < SETTINGS_TAB_COUNT; i++) {
    if (app->tab_buttons[i]) {
      widget_set_bounds(app->tab_buttons[i], 4,
                        4 + i * (int32_t)(tab_h + tab_gap),
                        tab_w, tab_h);
      widget_set_text(app->tab_buttons[i],
                      tab_label((enum settings_tab)i));
    }
  }
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
    settings_draw_fit(s, f, x + 6, y + 7,
                      (w > 12) ? (uint32_t)(w - 12) : 0u, buf, fg);
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
  if (label) {
    settings_draw_fit(s, f, x + 6, y + 7,
                      (w > 12) ? (uint32_t)(w - 12) : 0u, label, fg);
  }
  rows_add(x, y, x + w, y + (int32_t)h, kind, NULL);
  return y + (int32_t)h + 4;
}

void settings_paint(struct settings_app *app) {
  if (!app || !app->window) return;
  struct gui_surface *s = &app->window->surface;
  const struct font *f = font_default();
  const struct gui_theme_palette *theme = compositor_theme();
  uint8_t scale = compositor_ui_scale();
  int32_t sidebar_w = settings_sidebar_width(s->width, scale);
  uint32_t content_w = (s->width > (uint32_t)sidebar_w + 22u)
                           ? s->width - (uint32_t)sidebar_w - 22u
                           : 0u;
  if (!f) return;
  settings_layout_tabs(app);

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
  int32_t row_w = (content_w > 240u) ? 220 : (int32_t)content_w;
  int32_t action_w = (content_w > 190u) ? 170 : (int32_t)content_w;
  if (row_w < 96) row_w = 96;
  if (action_w < 96) action_w = 96;

  settings_draw_fit(s, f, cx, cy, content_w, tab_label(app->active_tab),
                    theme->accent);
  cy += 24;

  /* Separator */
  if ((uint32_t)cy < s->height) {
    uint32_t x_end = (s->width > 8u) ? s->width - 8u : s->width;
    for (uint32_t x = (uint32_t)cx; x < x_end; x++) {
      uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + (uint32_t)cy * s->pitch);
      line[x] = theme->window_border;
    }
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
      "capyos", "classic-modern", "ocean", "forest", "love",
      "high-contrast"
    };
    for (size_t i = 0; i < sizeof(k_themes)/sizeof(k_themes[0]); ++i) {
      cy = paint_option_row(s, f, theme, cx, cy, row_w, k_themes[i],
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
      cy = paint_option_row(s, f, theme, cx, cy, row_w, name,
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
      cy = paint_option_row(s, f, theme, cx, cy, row_w, k_langs[i],
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
    settings_draw_fit(s, f, cx, cy, content_w, line, theme->text); cy += 18;
    cy = paint_action_button(s, f, theme, cx, cy, action_w,
                             localization_select(lang,
                                                  "Editar pagina inicial",
                                                  "Edit homepage",
                                                  "Editar pagina principal"),
                             SETTINGS_ROW_HOMEPAGE);
    cy += 4;
    settings_draw_fit(s, f, cx, cy, content_w,
                      localization_select(lang,
                                           "Fallback offline: file://capyos/wikipedia",
                                           "Offline fallback: file://capyos/wikipedia",
                                           "Respaldo offline: file://capyos/wikipedia"),
                      theme->text_muted); cy += 18;
    settings_draw_fit(s, f, cx, cy, content_w,
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
    settings_draw_fit(s, f, cx, cy, content_w, line, theme->text); cy += 18;
    cy = paint_action_button(s, f, theme, cx, cy, action_w,
                             localization_select(lang,
                                                  "Adicionar usuario...",
                                                  "Add user...",
                                                  "Anadir usuario..."),
                             SETTINGS_ROW_NEWUSER);
    cy += 4;
    settings_draw_fit(s, f, cx, cy, content_w,
                      localization_select(lang,
                                           "Armazenado em /etc/users.db (PBKDF2-SHA256, 64k)",
                                           "Stored in /etc/users.db (PBKDF2-SHA256, 64k rounds)",
                                           "Almacenado en /etc/users.db (PBKDF2-SHA256, 64k)"),
                      theme->text_muted); cy += 18;
    settings_draw_fit(s, f, cx, cy, content_w,
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
  if (app->status_text[0] && s->height > f->glyph_height + 6u) {
    int32_t status_y = (int32_t)s->height - (int32_t)f->glyph_height - 3;
    settings_draw_fit(s, f, cx, status_y, content_w, app->status_text,
                      app->status_color ? app->status_color : theme->text_muted);
  }
}
