#include "gui/desktop.h"
#include "gui/compositor.h"
#include "gui/taskbar.h"
#include "gui/terminal.h"
#include "gui/font.h"
#include "gui/event.h"
#include "gui/widget.h"
#include "shell/core.h"
#include "drivers/input/mouse.h"
#include "drivers/input/keyboard_layout.h"
#include "drivers/rtc/rtc.h"
#include "memory/kmem.h"
#include "apps/calculator.h"
#include "apps/file_manager.h"
#include "apps/text_editor.h"
#include "apps/settings.h"
#include "apps/task_manager.h"
#include "apps/html_viewer.h"
#include <stddef.h>

static void ds_memset(void *d, int v, size_t n) {
  uint8_t *p = (uint8_t *)d;
  for (size_t i = 0; i < n; i++) p[i] = (uint8_t)v;
}

static void ds_strcpy(char *dst, const char *src, size_t max) {
  size_t i = 0;
  if (!dst || max == 0) return;
  if (src) {
    while (src[i] && i + 1 < max) {
      dst[i] = src[i];
      i++;
    }
  }
  dst[i] = '\0';
}

static int ds_streq(const char *a, const char *b) {
  size_t i = 0;
  if (!a || !b) return 0;
  while (a[i] && b[i]) {
    if (a[i] != b[i]) return 0;
    i++;
  }
  return a[i] == b[i];
}

static struct terminal g_desktop_terminal;
static struct terminal *g_shell_output_term = NULL;
static struct desktop_session *g_menu_desktop = NULL;

static void desktop_shell_write(const char *text) {
  if (g_shell_output_term && text) terminal_write_string(g_shell_output_term, text);
}

static void desktop_shell_putc(char ch) {
  if (g_shell_output_term) terminal_write_char(g_shell_output_term, ch);
}

static void desktop_terminal_paint(struct gui_window *win) {
  if (!win || !win->user_data) return;
  terminal_paint((struct terminal *)win->user_data);
}

static void desktop_terminal_key(struct gui_window *win, uint32_t keycode,
                                 uint8_t mods) {
  (void)mods;
  if (!win || !win->user_data) return;
  struct terminal *term = (struct terminal *)win->user_data;

  /* Handle arrow / special keys as keycodes, not chars */
  if (keycode >= 0x80) {
    /* Arrow keys — for now just ignore in terminal (could scroll) */
    return;
  }

  terminal_handle_key(term, keycode, (char)keycode);
}

static void desktop_terminal_scroll(struct gui_window *win, int32_t delta) {
  if (!win || !win->user_data) return;
  terminal_handle_mouse_scroll((struct terminal *)win->user_data, (int)delta);
}

static void desktop_apply_theme(struct desktop_session *ds) {
  const char *theme = NULL;
  const struct gui_theme_palette *palette = NULL;
  if (!ds) return;
  theme = (ds->settings && ds->settings->theme[0]) ? ds->settings->theme : "capyos";
  compositor_apply_theme(theme, ds->screen_w, ds->screen_h);
  palette = compositor_theme();
  ds->wallpaper_color = palette->wallpaper;
  ds_strcpy(ds->theme_name, theme, sizeof(ds->theme_name));
  compositor_set_wallpaper(palette->wallpaper);
  ds->taskbar.bg_color = palette->taskbar_bg;
  ds->taskbar.fg_color = palette->taskbar_fg;
  ds->taskbar.highlight_color = palette->taskbar_highlight;
  if (ds->taskbar.window) {
    ds->taskbar.window->bg_color = palette->taskbar_bg;
    ds->taskbar.window->border_color = palette->window_border;
  }
  if (ds->taskbar.menu_popup) {
    ds->taskbar.menu_popup->bg_color = palette->window_bg;
    ds->taskbar.menu_popup->border_color = palette->window_border;
  }
  if (ds->active_terminal) {
    terminal_set_color(ds->active_terminal, palette->terminal_fg, palette->terminal_bg);
    if (ds->active_terminal->window) {
      ds->active_terminal->window->bg_color = palette->terminal_bg;
      ds->active_terminal->window->border_color = palette->window_border;
    }
  }
}

static void menu_action_terminal(void *user_data) {
  (void)user_data;
  if (g_menu_desktop) desktop_open_terminal(g_menu_desktop);
}

/* Helper: after an app calls compositor_create_window + compositor_focus_window,
 * we grab the focused window and register it in the taskbar. */
static void register_focused_in_taskbar(const char *name) {
  struct gui_window *win = compositor_focused_window();
  if (win && g_menu_desktop) {
    taskbar_add_window(&g_menu_desktop->taskbar, win->id, name);
    taskbar_set_focused(&g_menu_desktop->taskbar, win->id);
  }
}

static void menu_action_file_manager(void *user_data) {
  (void)user_data;
  file_manager_open();
  register_focused_in_taskbar("Files");
}

static void menu_action_text_editor(void *user_data) {
  (void)user_data;
  text_editor_open(NULL);
  register_focused_in_taskbar("Editor");
}

static void menu_action_calculator(void *user_data) {
  (void)user_data;
  calculator_open();
  register_focused_in_taskbar("Calculator");
}

static void menu_action_settings(void *user_data) {
  (void)user_data;
  settings_open();
  register_focused_in_taskbar("Settings");
}

static void menu_action_task_manager(void *user_data) {
  (void)user_data;
  task_manager_open();
  register_focused_in_taskbar("Tasks");
}

static void menu_action_browser(void *user_data) {
  (void)user_data;
  html_viewer_open();
  register_focused_in_taskbar("Browser");
}

void desktop_init(struct desktop_session *ds, uint32_t *fb, uint32_t w,
                  uint32_t h, uint32_t pitch,
                  const struct system_settings *settings) {
  if (!ds || !fb) return;
  ds_memset(ds, 0, sizeof(*ds));
  ds->framebuffer = fb;
  ds->screen_w = w;
  ds->screen_h = h;
  ds->pitch = pitch;
  ds->settings = settings;
  ds->active = 1;

  font_init();
  gui_event_init();
  widget_system_init();
  compositor_init(fb, w, h, pitch);
  taskbar_init(&ds->taskbar, w, h);
  wm_init(&ds->wm, w, h);
  desktop_apply_theme(ds);

  g_menu_desktop = ds;
  taskbar_add_menu_entry(&ds->taskbar, "Terminal",     menu_action_terminal,     ds);
  taskbar_add_menu_entry(&ds->taskbar, "Files",        menu_action_file_manager, ds);
  taskbar_add_menu_entry(&ds->taskbar, "Editor",       menu_action_text_editor,  ds);
  taskbar_add_menu_entry(&ds->taskbar, "Calculator",   menu_action_calculator,   ds);
  taskbar_add_menu_entry(&ds->taskbar, "Browser",      menu_action_browser,      ds);
  taskbar_add_menu_entry(&ds->taskbar, "Settings",     menu_action_settings,     ds);
  taskbar_add_menu_entry(&ds->taskbar, "Tasks",        menu_action_task_manager, ds);

  mouse_set_bounds((int32_t)w, (int32_t)h);
  mouse_set_position((int32_t)(w / 2), (int32_t)(h / 2));
  ds->mouse_initialized = 1;
  ds->active_terminal = NULL;
}

static void desktop_terminal_command(struct terminal *term, const char *data,
                                     size_t len) {
  char line[256];
  size_t i = 0;
  if (!term || !data) return;

  if (data[0] == '\0') {
    terminal_write_string(term, "$ ");
    return;
  }

  if (len >= sizeof(line)) len = sizeof(line) - 1;
  for (i = 0; i < len; i++) line[i] = data[i];
  line[len] = '\0';

  if (ds_streq(line, "exit")) {
    extern void desktop_stop(void);
    desktop_stop();
    return;
  }

  if (ds_streq(line, "clear")) {
    terminal_clear(term);
    terminal_write_string(term, "$ ");
    return;
  }

  extern int kernel_desktop_dispatch_shell_command(char *line);
  g_shell_output_term = term;
  shell_set_output_callbacks(desktop_shell_write, desktop_shell_putc);
  if (!kernel_desktop_dispatch_shell_command(line)) {
    terminal_write_string(term, "[erro] comando desconhecido\n");
    terminal_write_string(term, "Use help-any para listar comandos.\n");
  }
  shell_set_output_callbacks(NULL, NULL);
  g_shell_output_term = NULL;
  terminal_write_string(term, "$ ");
}

void desktop_open_terminal(struct desktop_session *ds) {
  uint8_t scale = compositor_ui_scale();
  int32_t margin_x = 28 + 24 * (scale - 1);
  int32_t margin_y = 28 + 16 * (scale - 1);
  uint32_t width = 0;
  uint32_t height = 0;
  if (!ds) return;

  width = ds->screen_w > (uint32_t)(margin_x * 2)
              ? ds->screen_w - (uint32_t)(margin_x * 2)
              : ds->screen_w;
  height = ds->screen_h > (uint32_t)(margin_y * 2 + TASKBAR_HEIGHT)
               ? ds->screen_h - (uint32_t)(margin_y * 2 + TASKBAR_HEIGHT)
               : ds->screen_h - TASKBAR_HEIGHT;

  {
    struct gui_window *win = compositor_create_window(
        "Terminal", margin_x, margin_y, width, height);
    if (!win) return;
    compositor_show_window(win->id);
    compositor_focus_window(win->id);
    taskbar_add_window(&ds->taskbar, win->id, "Terminal");
    taskbar_set_focused(&ds->taskbar, win->id);

    terminal_init(&g_desktop_terminal, win);
    terminal_set_color(&g_desktop_terminal, compositor_theme()->terminal_fg,
                       compositor_theme()->terminal_bg);
    terminal_clear(&g_desktop_terminal);
    terminal_write_string(&g_desktop_terminal, "CapyOS Desktop Terminal\n");
    terminal_write_string(&g_desktop_terminal, "Type 'exit' to return to CLI.\n\n");
    terminal_write_string(&g_desktop_terminal, "$ ");
    terminal_set_output_callback(&g_desktop_terminal, desktop_terminal_command, NULL);
    win->user_data = &g_desktop_terminal;
    win->on_paint = desktop_terminal_paint;
    win->on_key = desktop_terminal_key;
    win->on_scroll = desktop_terminal_scroll;
    win->bg_color = compositor_theme()->terminal_bg;
    win->border_color = compositor_theme()->window_border;
    ds->active_terminal = &g_desktop_terminal;
  }
}

void desktop_handle_input(struct desktop_session *ds, uint32_t keycode,
                          char ch) {
  struct gui_window *focused = NULL;
  if (!ds) return;

  focused = compositor_focused_window();
  if (focused && focused->on_key) {
    /* For printable characters pass the ASCII value as keycode so that
     * window handlers can treat it uniformly.  For special keys (arrows,
     * function keys etc.) keycode already carries the KEY_* constant and
     * ch is 0, so we pass keycode directly. */
    uint32_t kc = ch ? (uint32_t)(uint8_t)ch : keycode;
    focused->on_key(focused, kc, 0);
    compositor_invalidate(focused->id);
  }
}

int desktop_handle_mouse(struct desktop_session *ds) {
  int handled = 0;
  if (!ds || !ds->mouse_initialized) return 0;

  /* Drain any pending PS/2 mouse bytes into the event queue before we
   * try to read events.  The keyboard input runtime only consumes one
   * byte per poll, so without this call a 3-byte mouse packet would be
   * spread across multiple frames and clicks would feel unresponsive. */
  mouse_ps2_poll();

  {
    struct mouse_event mev;
    while (mouse_poll(&mev) == 0) {
      struct mouse_state ms;
      struct gui_event ev;
      ds_memset(&ev, 0, sizeof(ev));
      mouse_get_state(&ms);
      ev.timestamp = 0;
      handled = 1;

      if (mev.dx != 0 || mev.dy != 0) {
        ev.type = GUI_EVENT_MOUSE_MOVE;
        ev.mouse.x = ms.x;
        ev.mouse.y = ms.y;
        ev.mouse.dx = mev.dx;
        ev.mouse.dy = mev.dy;
        ev.mouse.buttons = mev.buttons;
        gui_event_push(&ev);
        wm_handle_mouse_move(&ds->wm, ms.x, ms.y);
      }

      if (mev.changed & MOUSE_BUTTON_LEFT) {
        struct gui_window *win = NULL;
        ev.type = (mev.buttons & MOUSE_BUTTON_LEFT) ? GUI_EVENT_MOUSE_DOWN
                                                    : GUI_EVENT_MOUSE_UP;
        ev.mouse.x = ms.x;
        ev.mouse.y = ms.y;
        ev.mouse.buttons = mev.buttons;
        gui_event_push(&ev);

        if (ev.type == GUI_EVENT_MOUSE_DOWN) {
          /* If the menu popup is open, give it first chance at the click */
          if (ds->taskbar.menu_open &&
              taskbar_handle_menu_click(&ds->taskbar, ms.x, ms.y)) {
            /* Menu item was activated; event consumed */
          } else if (ms.y >= (int32_t)(ds->screen_h - TASKBAR_HEIGHT)) {
            taskbar_handle_click(&ds->taskbar, ms.x,
                                 ms.y - (int32_t)(ds->screen_h - TASKBAR_HEIGHT));
          } else {
            /* Click outside taskbar and outside menu popup -> close menu */
            if (ds->taskbar.menu_open) {
              taskbar_toggle_menu(&ds->taskbar);
            }
            win = compositor_window_at(ms.x, ms.y);
            if (win) {
              /* Check for close-button click in the title bar */
              if (compositor_hit_close_button(win, ms.x, ms.y)) {
                uint32_t wid = win->id;
                if (win->on_close) win->on_close(win);
                taskbar_remove_window(&ds->taskbar, wid);
                compositor_destroy_window(wid);
                /* If the closed window was the terminal, clear pointer */
                if (ds->active_terminal &&
                    ds->active_terminal->window == win) {
                  ds->active_terminal = NULL;
                }
              } else {
                compositor_focus_window(win->id);
                taskbar_set_focused(&ds->taskbar, win->id);
                wm_handle_mouse_down(&ds->wm, ms.x, ms.y, mev.buttons);
                if (win->on_mouse &&
                    ms.y >= win->frame.y &&
                    ms.y < win->frame.y + (int32_t)win->frame.height) {
                  win->on_mouse(win, ms.x - win->frame.x, ms.y - win->frame.y,
                                mev.buttons);
                  compositor_invalidate(win->id);
                }
              }
            }
          }
        } else {
          wm_handle_mouse_up(&ds->wm);
        }
      }

      if (mev.dz != 0) {
        struct gui_window *scroll_win = NULL;
        ev.type = GUI_EVENT_MOUSE_SCROLL;
        ev.mouse.x = ms.x;
        ev.mouse.y = ms.y;
        ev.mouse.dy = (int16_t)mev.dz;
        gui_event_push(&ev);

        scroll_win = compositor_focused_window();
        if (scroll_win && scroll_win->on_scroll) {
          scroll_win->on_scroll(scroll_win, (int32_t)mev.dz);
          compositor_invalidate(scroll_win->id);
        } else if (ds->active_terminal &&
                   ds->active_terminal->window == scroll_win) {
          terminal_handle_mouse_scroll(ds->active_terminal, mev.dz);
          compositor_invalidate(ds->active_terminal->window->id);
        }
      }
    }
  }

  return handled;
}

int desktop_run_frame(struct desktop_session *ds) {
  int work_done = 0;
  if (!ds || !ds->active) return 0;
  if (ds->settings) {
    const char *theme = ds->settings->theme[0] ? ds->settings->theme : "capyos";
    if (!ds_streq(ds->theme_name, theme)) {
      desktop_apply_theme(ds);
      work_done = 1;
    }
  }

  if (desktop_handle_mouse(ds)) work_done = 1;

  {
    struct rtc_time rtc;
    char clock_buf[16];
    rtc_read(&rtc);
    rtc_format_time(&rtc, clock_buf, sizeof(clock_buf));
    if (taskbar_update_clock(&ds->taskbar, clock_buf)) work_done = 1;
  }

  compositor_render();

  {
    struct mouse_state ms;
    mouse_get_state(&ms);
    compositor_render_cursor(ms.x, ms.y);
  }

  return work_done;
}

void desktop_shutdown(struct desktop_session *ds) {
  if (!ds) return;
  /* Reset taskbar menu popup pointer (compositor_init will free the surface) */
  ds->taskbar.menu_popup = NULL;
  ds->taskbar.menu_open = 0;
  ds->taskbar.menu_entry_count = 0;
  ds->taskbar.item_count = 0;
  ds->taskbar.window = NULL;
  ds->active_terminal = NULL;
  g_shell_output_term = NULL;
  g_menu_desktop = NULL;
  shell_set_output_callbacks(NULL, NULL);
  ds->active = 0;
}
