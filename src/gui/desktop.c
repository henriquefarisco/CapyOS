#include "gui/desktop.h"
#include "gui/compositor.h"
#include "gui/taskbar.h"
#include "gui/terminal.h"
#include "gui/font.h"
#include "gui/event.h"
#include "gui/widget.h"
#include "drivers/input/mouse.h"
#include "drivers/rtc/rtc.h"
#include "memory/kmem.h"
#include <stddef.h>

static void ds_memset(void *d, int v, size_t n) {
  uint8_t *p = (uint8_t *)d;
  for (size_t i = 0; i < n; i++) p[i] = (uint8_t)v;
}

void desktop_init(struct desktop_session *ds, uint32_t *fb, uint32_t w,
                  uint32_t h, uint32_t pitch) {
  if (!ds || !fb) return;
  ds_memset(ds, 0, sizeof(*ds));
  ds->framebuffer = fb;
  ds->screen_w = w;
  ds->screen_h = h;
  ds->pitch = pitch;
  ds->wallpaper_color = 0x1A1A2E;
  ds->active = 1;

  font_init();
  gui_event_init();
  widget_system_init();
  compositor_init(fb, w, h, pitch);
  compositor_set_wallpaper(ds->wallpaper_color);
  taskbar_init(&ds->taskbar, w, h);

  mouse_set_bounds((int32_t)w, (int32_t)h);
  mouse_set_position((int32_t)(w / 2), (int32_t)(h / 2));
  ds->mouse_initialized = 1;

  ds->active_terminal = NULL;
}

void desktop_open_terminal(struct desktop_session *ds) {
  if (!ds) return;
  struct gui_window *win = compositor_create_window(
      "Terminal", 50, 50, ds->screen_w - 100,
      ds->screen_h - TASKBAR_HEIGHT - 100);
  if (!win) return;
  compositor_show_window(win->id);
  compositor_focus_window(win->id);
  taskbar_add_window(&ds->taskbar, win->id, "Terminal");
  taskbar_set_focused(&ds->taskbar, win->id);

  struct terminal *term = (struct terminal *)kmalloc(sizeof(struct terminal));
  if (term) {
    terminal_init(term, win);
    terminal_write_string(term, "CapyOS Terminal\n");
    terminal_write_string(term, "Type commands here.\n\n");
    terminal_write_string(term, "$ ");
    win->on_paint = NULL;
    ds->active_terminal = term;
  }
}

void desktop_handle_input(struct desktop_session *ds, uint32_t keycode,
                          char ch) {
  if (!ds) return;

  struct gui_window *focused = compositor_focused_window();
  if (focused && focused->on_key) {
    focused->on_key(focused, keycode, 0);
  }

  if (ds->active_terminal && ch) {
    terminal_handle_key(ds->active_terminal, keycode, ch);
  }
}

void desktop_handle_mouse(struct desktop_session *ds) {
  if (!ds || !ds->mouse_initialized) return;

  struct mouse_event mev;
  while (mouse_poll(&mev) == 0) {
    struct mouse_state ms;
    mouse_get_state(&ms);

    struct gui_event ev;
    ds_memset(&ev, 0, sizeof(ev));
    ev.timestamp = 0;

    if (mev.dx != 0 || mev.dy != 0) {
      ev.type = GUI_EVENT_MOUSE_MOVE;
      ev.mouse.x = ms.x;
      ev.mouse.y = ms.y;
      ev.mouse.dx = mev.dx;
      ev.mouse.dy = mev.dy;
      ev.mouse.buttons = mev.buttons;
      gui_event_push(&ev);
    }

    if (mev.changed & 0x01) {
      ev.type = (mev.buttons & 0x01) ? GUI_EVENT_MOUSE_DOWN : GUI_EVENT_MOUSE_UP;
      ev.mouse.x = ms.x;
      ev.mouse.y = ms.y;
      ev.mouse.buttons = mev.buttons;
      gui_event_push(&ev);

      if (ev.type == GUI_EVENT_MOUSE_DOWN) {
        /* Check taskbar click */
        if (ms.y >= (int32_t)(ds->screen_h - TASKBAR_HEIGHT)) {
          taskbar_handle_click(&ds->taskbar, ms.x,
                               ms.y - (int32_t)(ds->screen_h - TASKBAR_HEIGHT));
        } else {
          /* Focus window under cursor */
          struct gui_window *win = compositor_window_at(ms.x, ms.y);
          if (win) {
            compositor_focus_window(win->id);
            taskbar_set_focused(&ds->taskbar, win->id);
          }
        }
      }
    }

    if (mev.dz != 0) {
      ev.type = GUI_EVENT_MOUSE_SCROLL;
      ev.mouse.x = ms.x;
      ev.mouse.y = ms.y;
      ev.mouse.dy = (int16_t)mev.dz;
      gui_event_push(&ev);

      if (ds->active_terminal) {
        terminal_handle_mouse_scroll(ds->active_terminal, mev.dz);
      }
    }
  }
}

void desktop_run_frame(struct desktop_session *ds) {
  if (!ds || !ds->active) return;

  desktop_handle_mouse(ds);

  /* Update clock in taskbar */
  struct rtc_time rtc;
  rtc_read(&rtc);
  char clock_buf[16];
  rtc_format_time(&rtc, clock_buf, sizeof(clock_buf));
  taskbar_update_clock(&ds->taskbar, clock_buf);

  /* Paint terminal if active */
  if (ds->active_terminal) {
    terminal_paint(ds->active_terminal);
  }

  /* Render taskbar */
  taskbar_paint(&ds->taskbar);

  /* Composite everything */
  compositor_render();

  /* Draw mouse cursor on top */
  struct mouse_state ms;
  mouse_get_state(&ms);
  compositor_render_cursor(ms.x, ms.y);
}

void desktop_shutdown(struct desktop_session *ds) {
  if (!ds) return;
  if (ds->active_terminal) {
    kfree(ds->active_terminal);
    ds->active_terminal = NULL;
  }
  ds->active = 0;
}
