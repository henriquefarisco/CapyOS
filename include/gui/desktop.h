#ifndef GUI_DESKTOP_H
#define GUI_DESKTOP_H

#include <stdint.h>
#include "core/system_init.h"
#include "gui/compositor.h"
#include "gui/taskbar.h"
#include "gui/terminal.h"
#include "gui/font.h"
#include "gui/event.h"
#include "gui/window_manager.h"
#include "drivers/input/mouse.h"

struct desktop_session {
  int active;
  uint32_t screen_w;
  uint32_t screen_h;
  uint32_t *framebuffer;
  uint32_t pitch;
  struct taskbar taskbar;
  struct window_manager wm;
  struct terminal *active_terminal;
  const struct system_settings *settings;
  uint32_t wallpaper_color;
  char theme_name[16];
  int mouse_initialized;
};

void desktop_init(struct desktop_session *ds, uint32_t *fb, uint32_t w,
                  uint32_t h, uint32_t pitch,
                  const struct system_settings *settings);
int desktop_run_frame(struct desktop_session *ds);
void desktop_open_terminal(struct desktop_session *ds);
void desktop_handle_input(struct desktop_session *ds, uint32_t keycode, char ch);
int desktop_handle_mouse(struct desktop_session *ds);
void desktop_shutdown(struct desktop_session *ds);

#endif /* GUI_DESKTOP_H */
