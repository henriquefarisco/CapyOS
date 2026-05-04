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
};

void settings_open(void);
void settings_switch_tab(struct settings_app *app, enum settings_tab tab);
void settings_paint(struct settings_app *app);

#endif
