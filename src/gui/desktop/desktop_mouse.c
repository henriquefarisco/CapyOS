/*
 * Desktop session mouse handler.
 *
 * Sibling TU of `desktop.c` extracted in the 2026-05-16 preventive
 * refactor when `desktop.c` reached 859/900 LOC. The mouse event loop
 * is large (~250 LOC of dispatch logic for hover, cursor kind, click,
 * right-click and scroll across compositor/taskbar/inline_prompt/
 * context_menu/desktop_icons) and forms a natural unit independent
 * of the rest of the desktop lifecycle code. Moving it out gives
 * substantial headroom to both files.
 *
 * The function keeps its public signature: `desktop_handle_mouse` is
 * still declared in `include/gui/desktop.h` and called by the parent
 * `desktop_run_frame` (in `desktop.c`) via external linkage. Body was
 * moved verbatim from `desktop.c` (lines 527-779) and parity verified
 * via diff.
 *
 * The single static helper used by the mouse handler
 * (`desktop_overlay_active`, 4 lines) is duplicated as `static` here
 * to preserve the per-TU "no link-time coupling" pattern already used
 * across the GUI desktop files.
 */

#include "gui/desktop.h"
#include "gui/desktop_runtime.h"
#include "gui/compositor.h"
#include "gui/taskbar.h"
#include "gui/event.h"
#include "gui/context_menu.h"
#include "gui/desktop_icons.h"
#include "gui/inline_prompt.h"
#include "drivers/input/mouse.h"
#include <stddef.h>

static int desktop_overlay_active(struct desktop_session *ds) {
  return inline_prompt_is_open() || context_menu_is_open() ||
         (ds && ds->taskbar.menu_open);
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
      mouse_get_state(&ms);
      handled = 1;

      if (mev.dx != 0 || mev.dy != 0) {
        wm_handle_mouse_move(&ds->wm, ms.x, ms.y);
        if ((mev.buttons & MOUSE_BUTTON_LEFT) &&
            !desktop_overlay_active(ds) &&
            ds->wm.drag_mode == WM_DRAG_NONE) {
          desktop_icons_handle_drag_move(ms.x, ms.y, mev.buttons);
        }
        /* Etapa UX W7-ish (2026-05-03): hover dispatch.
         *   - Se um context_menu esta aberto, ele tem prioridade
         *     (atualiza hover sobre o popup).
         *   - Senao, se o menu start esta aberto, atualiza hover_entry.
         *   - Senao, encaminha pro on_hover da janela sob o cursor.
         * Coordenadas locais sao calculadas pela window. */
        if (!inline_prompt_is_open()) {
          if (context_menu_is_open()) {
            context_menu_handle_hover(ms.x, ms.y);
          } else if (ds->taskbar.menu_open) {
            taskbar_handle_menu_hover(&ds->taskbar, ms.x, ms.y);
          } else {
            struct gui_event ev;
            ev.type = GUI_EVENT_MOUSE_MOVE;
            ev.window_id = 0;
            ev.mouse.x = ms.x;
            ev.mouse.y = ms.y;
            ev.mouse.dx = mev.dx;
            ev.mouse.dy = mev.dy;
            ev.mouse.buttons = mev.buttons;
            ev.timestamp = 0;
            (void)gui_window_dispatch_event(&ev);
          }
        }
        /* Etapa F4 cursors (2026-05-03): cursor kind dispatch
         * baseado em hover. Prioridade:
         *   1. Resize zone (right/bottom edge / corner) -> RESIZE_*
         *   2. on_cursor_hint da janela (TEXT em URL bar, etc.)
         *   3. win->loading -> LOADING (ampulheta)
         *   4. Default ARROW.
         * O compositor evita re-render se o kind nao mudou. */
        {
          int overlay_active = desktop_overlay_active(ds);
          struct gui_window *cwin = overlay_active ? NULL
                                                   : compositor_window_at(ms.x, ms.y);
          enum comp_cursor_kind ck = COMP_CURSOR_ARROW;
          if (cwin) {
            int32_t fx0 = cwin->frame.x;
            int32_t fy0 = cwin->frame.y;
            int32_t fx1 = fx0 + (int32_t)cwin->frame.width;
            int32_t fy1 = fy0 + (int32_t)cwin->frame.height;
            int near_r = (ms.x >= fx1 - WM_RESIZE_GRIP_WIDTH && ms.x <= fx1);
            int near_b = (ms.y >= fy1 - WM_RESIZE_GRIP_WIDTH && ms.y <= fy1);
            int in_x = (ms.x >= fx0 && ms.x <= fx1);
            int in_y = (ms.y >= fy0 && ms.y <= fy1);
            if (cwin->resizable && in_x && in_y) {
              if (near_r && near_b) ck = COMP_CURSOR_RESIZE_DIAG;
              else if (near_r)      ck = COMP_CURSOR_RESIZE_H;
              else if (near_b)      ck = COMP_CURSOR_RESIZE_V;
            }
            if (ck == COMP_CURSOR_ARROW && cwin->on_cursor_hint &&
                ms.y >= fy0 && ms.y < fy1) {
              ck = cwin->on_cursor_hint(cwin, ms.x - fx0, ms.y - fy0);
            }
            if (ck == COMP_CURSOR_ARROW && cwin->loading) {
              ck = COMP_CURSOR_LOADING;
            }
          }
          compositor_set_cursor(ck);
        }
      }

      if (mev.changed & MOUSE_BUTTON_LEFT) {
        struct gui_window *win = NULL;
        int left_down = (mev.buttons & MOUSE_BUTTON_LEFT) ? 1 : 0;

        if (left_down) {
          /* Etapa UX W7-ish (2026-05-03): inline_prompt tem maior
           * prioridade. Click fora do popup -> cancela; click
           * dentro -> mantem (sem agir). */
          if (inline_prompt_is_open() &&
              inline_prompt_handle_click(ms.x, ms.y)) {
            /* prompt fechou ou absorveu o click; nao continua. */
          } else if (context_menu_is_open() &&
              context_menu_handle_click(ms.x, ms.y)) {
            /* Click consumido pelo context menu. */
          } else if (ds->taskbar.menu_open &&
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
                if (win->on_close) {
                  win->on_close(win);
                  win->on_close = NULL;
                }
                taskbar_remove_window(&ds->taskbar, wid);
                compositor_destroy_window(wid);
                /* If the closed window was the terminal, clear pointer */
                if (ds->active_terminal &&
                    ds->active_terminal->window == win) {
                  ds->active_terminal = NULL;
                }
              } else if (compositor_hit_minimize_button(win, ms.x, ms.y)) {
                /* Etapa F4 minimize/maximize (2026-05-03): minimize.
                 * Esconde a janela; o item do taskbar continua para
                 * permitir restore (clicar no item ja chama
                 * compositor_show_window). */
                compositor_minimize_window(win->id);
              } else if (compositor_hit_maximize_button(win, ms.x, ms.y)) {
                /* Etapa F4 minimize/maximize (2026-05-03): toggle
                 * maximize/restore. Subtrai a altura do taskbar para
                 * o "fullscreen" nao cobrir a barra de tarefas. */
                uint32_t avail = (ds->screen_h > TASKBAR_HEIGHT)
                                 ? ds->screen_h - TASKBAR_HEIGHT
                                 : ds->screen_h;
                compositor_toggle_maximize_window(win->id, ds->screen_w,
                                                   avail);
              } else {
                compositor_focus_window(win->id);
                taskbar_set_focused(&ds->taskbar, win->id);
                wm_handle_mouse_down(&ds->wm, ms.x, ms.y, mev.buttons);
                if (ds->wm.drag_mode == WM_DRAG_NONE &&
                    ms.y >= win->frame.y &&
                    ms.y < win->frame.y + (int32_t)win->frame.height) {
                  struct gui_event ev;
                  ev.type = GUI_EVENT_MOUSE_DOWN;
                  ev.window_id = win->id;
                  ev.mouse.x = ms.x;
                  ev.mouse.y = ms.y;
                  ev.mouse.dx = 0;
                  ev.mouse.dy = 0;
                  ev.mouse.buttons = mev.buttons;
                  ev.timestamp = 0;
                  (void)gui_window_dispatch_event(&ev);
                }
              }
            } else {
              /* Etapa UX W7-ish (2026-05-03): click no espaco vazio
               * do desktop (sem janela e fora do taskbar) -> roteia
               * para desktop_icons. Click sobre um icone seleciona
               * (e abre no segundo click). */
              desktop_icons_handle_click(ms.x, ms.y);
            }
          }
        } else {
          int was_wm_drag = (ds->wm.drag_mode != WM_DRAG_NONE);
          wm_handle_mouse_up(&ds->wm);
          if (!was_wm_drag && !desktop_overlay_active(ds)) {
            struct gui_event ev;
            ev.type = GUI_EVENT_MOUSE_UP;
            ev.window_id = 0;
            ev.mouse.x = ms.x;
            ev.mouse.y = ms.y;
            ev.mouse.dx = 0;
            ev.mouse.dy = 0;
            ev.mouse.buttons = mev.buttons;
            ev.timestamp = 0;
            (void)gui_window_dispatch_event(&ev);
            (void)desktop_icons_handle_mouse_up(ms.x, ms.y);
          }
        }
      }

      /* Etapa UX W7-ish (2026-05-03): right-click dispatch.
       *   - Fecha popups abertos antes (start menu / context menu)
       *     para que o novo right-click sempre crie um menu fresco.
       *   - Janela sob o cursor com `on_context_menu` -> dispatcher central.
       *   - Sem janela (desktop vazio) e fora do taskbar -> roteia
       *     pra desktop_icons (Open/Rename/Delete sobre icone, ou
       *     New File/Folder/Refresh sobre area vazia). */
      if ((mev.changed & MOUSE_BUTTON_RIGHT) &&
          (mev.buttons & MOUSE_BUTTON_RIGHT)) {
        if (inline_prompt_is_open()) {
          if (context_menu_is_open()) context_menu_close();
          if (ds->taskbar.menu_open) {
            taskbar_toggle_menu(&ds->taskbar);
          }
          if (inline_prompt_handle_click(ms.x, ms.y)) {
            compositor_invalidate_all();
          }
        } else {
          struct gui_window *rwin = NULL;
          if (context_menu_is_open()) context_menu_close();
          if (ds->taskbar.menu_open) {
            taskbar_toggle_menu(&ds->taskbar);
          }
          rwin = compositor_window_at(ms.x, ms.y);
          if (rwin && rwin->on_context_menu &&
              ms.y >= rwin->frame.y &&
              ms.y < rwin->frame.y + (int32_t)rwin->frame.height) {
            struct gui_event ev;
            ev.type = GUI_EVENT_MOUSE_DOWN;
            ev.window_id = rwin->id;
            ev.mouse.x = ms.x;
            ev.mouse.y = ms.y;
            ev.mouse.dx = 0;
            ev.mouse.dy = 0;
            ev.mouse.buttons = mev.buttons;
            ev.timestamp = 0;
            (void)gui_window_dispatch_event(&ev);
          } else if (!rwin && ms.y < (int32_t)(ds->screen_h - TASKBAR_HEIGHT)) {
            desktop_icons_handle_context(ms.x, ms.y);
          }
        }
      }

      if (mev.dz != 0) {
        if (ds->taskbar.menu_open &&
            taskbar_handle_menu_scroll(&ds->taskbar, ms.x, ms.y, mev.dz)) {
        } else if (!desktop_overlay_active(ds)) {
          struct gui_event ev;
          struct gui_window *scroll_win = compositor_focused_window();
          ev.type = GUI_EVENT_MOUSE_SCROLL;
          ev.window_id = scroll_win ? scroll_win->id : 0;
          ev.mouse.x = ms.x;
          ev.mouse.y = ms.y;
          ev.mouse.dx = 0;
          ev.mouse.dy = (int16_t)mev.dz;
          ev.mouse.buttons = mev.buttons;
          ev.timestamp = 0;
          (void)gui_window_dispatch_event(&ev);
        }
      }
    }
  }

  return handled;
}
