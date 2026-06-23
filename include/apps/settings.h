#ifndef APPS_SETTINGS_H
#define APPS_SETTINGS_H

#include "gui/compositor.h"
#include "gui/widget.h"

enum settings_tab {
  SETTINGS_TAB_DISPLAY = 0,
  SETTINGS_TAB_NETWORK,
  SETTINGS_TAB_KEYBOARD,
  SETTINGS_TAB_LANGUAGE,
  /* Etapa F4 settings (2026-05-03): novo tab Browser para exibir
   * a homepage configurada (em /system/config.ini). Posicionado
   * antes de Users/Updates/About porque pertence a "preferencias"
   * de UI mais imediatas que o usuario quer customizar. */
  SETTINGS_TAB_BROWSER,
  SETTINGS_TAB_USERS,
  SETTINGS_TAB_UPDATES,
  SETTINGS_TAB_ABOUT,
  SETTINGS_TAB_COUNT
};

struct settings_app {
  struct gui_window *window;
  enum settings_tab active_tab;
  struct widget *tab_buttons[SETTINGS_TAB_COUNT];
  struct widget *content_panel;
  char status_text[96];
  uint32_t status_color;
};

void settings_open(void);
void settings_switch_tab(struct settings_app *app, enum settings_tab tab);
void settings_paint(struct settings_app *app);

/* Etapa 6 / Slice 6.6: headless smoke roundtrip (no GUI/persistence). Returns 0
 * when the settings username-policy validator (charset + length) accepts valid
 * names and rejects invalid ones, non-zero otherwise. Called by the
 * apps-basic-roundtrip orchestrator via the apps/apps_smoke.h contract. */
int settings_smoke_roundtrip(void);

#endif
