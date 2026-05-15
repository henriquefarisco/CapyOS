#include "gui/desktop.h"
#include "gui/desktop_runtime.h"
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
#include "util/kstring.h"
#include "memory/kmem.h"
#include "apps/calculator.h"
#include "apps/file_manager.h"
#include "apps/text_editor.h"
#include "apps/settings.h"
#include "apps/task_manager.h"
#include "gui/context_menu.h"
#include "gui/desktop_icons.h"
#include "gui/inline_prompt.h"
#include "auth/session.h"
#include "lang/app_language.h"
#include "net/stack.h"
#include "arch/x86_64/apic.h"
#include <stddef.h>

static struct terminal g_desktop_terminal;
static struct terminal *g_shell_output_term = NULL;
static struct desktop_session *g_menu_desktop = NULL;
static int g_terminal_open = 0;

static void desktop_shell_write(const char *text) {
  if (g_shell_output_term && text) terminal_write_string(g_shell_output_term, text);
}

static void desktop_shell_putc(char ch) {
  if (g_shell_output_term) terminal_write_char(g_shell_output_term, ch);
}

/* Post-M5 W1: clear hook paired with desktop_shell_write/putc.
 * Routes the shell's mess builtin to the active terminal widget
 * instead of the framebuffer console hidden behind the GUI. */
static void desktop_shell_clear(void) {
  if (g_shell_output_term) {
    terminal_clear(g_shell_output_term);
  }
}

static void desktop_terminal_prompt(struct terminal *term) {
  char prompt[128];
  struct session_context *sess = kernel_desktop_shell_session();
  const struct user_record *user = sess ? session_user(sess) : NULL;
  const char *cwd = sess ? session_cwd(sess) : "/";
  if (!term) return;
  shell_build_prompt(user, g_menu_desktop ? g_menu_desktop->settings : NULL,
                     cwd, prompt, sizeof(prompt));
  terminal_write_string(term, prompt[0] ? prompt : "$ ");
}

static void desktop_shell_begin_terminal_output(struct terminal *term) {
  g_shell_output_term = term;
  shell_set_output_callbacks(desktop_shell_write, desktop_shell_putc);
  shell_set_clear_callback(desktop_shell_clear);
}

static void desktop_shell_end_terminal_output(void) {
  shell_set_output_callbacks(NULL, NULL);
  shell_set_clear_callback(NULL);
  g_shell_output_term = NULL;
}

static void desktop_terminal_paint(struct gui_window *win) {
  if (!win || !win->user_data) return;
  terminal_paint((struct terminal *)win->user_data);
}

/* 2026-05-02: repaint the terminal after a user resize drag. The
 * compositor has already cleared the new pixel buffer to bg_color;
 * the terminal painter walks the scrollback ring and re-renders
 * onto the surface so dropping the new dimensions in is enough. */
static void desktop_terminal_resize(struct gui_window *win,
                                    uint32_t w, uint32_t h) {
  (void)w;
  (void)h;
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

static void desktop_terminal_on_close(struct gui_window *win) {
  (void)win;
  if (g_menu_desktop && g_menu_desktop->active_terminal == &g_desktop_terminal) {
    g_menu_desktop->active_terminal = NULL;
  }
  terminal_set_output_callback(&g_desktop_terminal, NULL, NULL);
  g_desktop_terminal.window = NULL;
  if (g_shell_output_term == &g_desktop_terminal) desktop_shell_end_terminal_output();
  g_terminal_open = 0;
}

static int desktop_update_tray(struct desktop_session *ds) {
  char tray[TASKBAR_TRAY_TEXT_MAX];
  struct net_stack_status ns;
  if (!ds) return 0;
  if (net_stack_status(&ns) == 0) {
    kstrcpy(tray, sizeof(tray), ns.ready ? "net-on" :
            (ns.runtime_supported ? "net-wait" : "net-off"));
  } else {
    kstrcpy(tray, sizeof(tray), "net-off");
  }
  return taskbar_update_tray(&ds->taskbar, tray);
}

static int desktop_overlay_active(struct desktop_session *ds) {
  return inline_prompt_is_open() || context_menu_is_open() ||
         (ds && ds->taskbar.menu_open);
}

static void desktop_sample_dispatcher_health(struct desktop_session *ds) {
  if (!ds) return;
  if (gui_window_dispatcher_health_snapshot(&ds->dispatcher_health)) {
    ds->dispatcher_health_samples++;
  }
}

int desktop_session_health_snapshot(const struct desktop_session *ds,
                                    struct desktop_session_health *out) {
  struct gui_window *focused = NULL;
  if (!ds || !out) return 0;
  focused = compositor_focused_window();
  out->active = ds->active ? 1 : 0;
  out->framebuffer_ready = ds->framebuffer ? 1 : 0;
  out->dimensions_ready = (ds->screen_w > 0u && ds->screen_h > 0u &&
                           ds->pitch > 0u) ? 1 : 0;
  out->mouse_initialized = ds->mouse_initialized ? 1 : 0;
  out->cursor_valid = ds->cursor_valid ? 1 : 0;
  out->taskbar_ready = ds->taskbar.window ? 1 : 0;
  out->overlay_active = inline_prompt_is_open() || context_menu_is_open() ||
                        ds->taskbar.menu_open;
  out->taskbar_menu_open = ds->taskbar.menu_open ? 1 : 0;
  out->window_manager_drag_active =
      (ds->wm.drag_mode != WM_DRAG_NONE) ? 1 : 0;
  out->focused_window_id = focused ? focused->id : 0;
  out->dispatcher_health_samples = ds->dispatcher_health_samples;
  out->dispatcher = ds->dispatcher_health;
  out->dispatcher_health_ready =
      gui_window_dispatcher_health_snapshot(&out->dispatcher) ? 1 : 0;
  return 1;
}

int desktop_session_smoke_readiness_snapshot(
    const struct desktop_session *ds,
    struct desktop_session_smoke_readiness *out) {
  struct desktop_session_health health;
  if (!out) return 0;
  if (!desktop_session_health_snapshot(ds, &health)) {
    return desktop_session_smoke_readiness_from_health(NULL, out);
  }
  return desktop_session_smoke_readiness_from_health(&health, out);
}

static int desktop_handle_overlay_escape(struct desktop_session *ds,
                                         uint32_t keycode, char ch) {
  int closed = 0;
  if (ch != 0x1B && keycode != 0x1B) return 0;
  if (context_menu_is_open()) {
    context_menu_close();
    closed = 1;
  }
  if (ds && ds->taskbar.menu_open) {
    taskbar_toggle_menu(&ds->taskbar);
    closed = 1;
  }
  if (closed) compositor_invalidate_all();
  return closed;
}

static void desktop_apply_theme(struct desktop_session *ds) {
  const char *theme = NULL;
  const struct gui_theme_palette *palette = NULL;
  if (!ds) return;
  theme = (ds->settings && ds->settings->theme[0]) ? ds->settings->theme : "capyos";
  compositor_apply_theme(theme, ds->screen_w, ds->screen_h);
  palette = compositor_theme();
  ds->wallpaper_color = palette->wallpaper;
  kstrcpy(ds->theme_name, sizeof(ds->theme_name), theme);
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

/* Register only real app windows, never taskbar/menu overlays or stale focus. */
static void register_focused_in_taskbar(const char *expected_title,
                                        const char *name) {
  struct gui_window *win = compositor_focused_window();
  if (!win || !g_menu_desktop || !win->id || !win->visible || !win->decorated) return;
  if (expected_title && !kstreq(win->title, expected_title)) return;
  taskbar_add_window(&g_menu_desktop->taskbar, win->id, name);
  taskbar_set_focused(&g_menu_desktop->taskbar, win->id);
}

static void menu_action_file_manager(void *user_data) {
  (void)user_data;
  file_manager_open();
  register_focused_in_taskbar("File Manager", "Files");
}

static void menu_action_text_editor(void *user_data) {
  (void)user_data;
  text_editor_open(NULL);
  register_focused_in_taskbar("Text Editor", "Editor");
}

static void menu_action_calculator(void *user_data) {
  (void)user_data;
  calculator_open();
  register_focused_in_taskbar("Calculator", "Calculator");
}

static void menu_action_settings(void *user_data) {
  (void)user_data;
  settings_open();
  register_focused_in_taskbar("Settings", "Settings");
}

static void menu_action_task_manager(void *user_data) {
  (void)user_data;
  task_manager_open();
  register_focused_in_taskbar("Task Manager", "Tasks");
}

/* Browser menu entry erradicada na sessao 6 (2026-05-05). O
 * capybrowser (engine custom) foi removido; o sucessor sera um
 * port do Firefox. Ver docs/plans/active/firefox-port-roadmap.md. */

static void menu_action_logout(void *user_data) {
  (void)user_data;
  extern void desktop_stop(void);
  desktop_stop();
}

static void menu_action_reboot(void *user_data) {
  (void)user_data;
  extern void desktop_stop(void);
  desktop_stop();
  extern void kernel_request_reboot(void);
  kernel_request_reboot();
}

static void menu_action_shutdown(void *user_data) {
  (void)user_data;
  extern void desktop_stop(void);
  desktop_stop();
  extern void kernel_request_shutdown(void);
  kernel_request_shutdown();
}

void desktop_init(struct desktop_session *ds, uint32_t *fb, uint32_t w,
                  uint32_t h, uint32_t pitch,
                  const struct system_settings *settings) {
  if (!ds || !fb) return;
  kmemzero(ds, sizeof(*ds));
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

  /* Etapa UX W7-ish (2026-05-03): wallpaper renderiza icons das
   * pastas/arquivos do home do user (a la Desktop do W7). Para
   * usuarios sem home definido cai pra root. Compositor recebe o
   * paint callback; clique e right-click no espaco vazio sao
   * roteados pra desktop_icons em desktop_handle_mouse. */
  {
    struct session_context *sess = session_active();
    const struct user_record *user = sess ? session_user(sess) : NULL;
    const char *home = (user && user->home[0]) ? user->home : "/";
    desktop_icons_init(home, TASKBAR_HEIGHT);
    compositor_set_desktop_callback(desktop_icons_paint);
  }

  g_menu_desktop = ds;
  /* Etapa F4 i18n (2026-05-03): nomes localizados via APP_T no
   * idioma da sessao ativa. Note: o taskbar copia a string ao
   * adicionar -- mudancas de idioma em runtime so afetam a UI
   * apos relogin (re-init do desktop). */
  taskbar_add_menu_entry_pinned(&ds->taskbar,
                                APP_T("Terminal", "Terminal", "Terminal"),
                                menu_action_terminal, ds);
  taskbar_add_menu_entry_pinned(&ds->taskbar,
                                APP_T("Arquivos", "Files", "Archivos"),
                                menu_action_file_manager, ds);
  taskbar_add_menu_entry_pinned(&ds->taskbar,
                                APP_T("Editor", "Editor", "Editor"),
                                menu_action_text_editor, ds);
  taskbar_add_menu_entry_pinned(&ds->taskbar,
                                APP_T("Calculadora", "Calculator", "Calculadora"),
                                menu_action_calculator, ds);
  taskbar_add_menu_separator(&ds->taskbar);
  taskbar_add_menu_entry(&ds->taskbar,
                         APP_T("Configuracoes", "Settings", "Ajustes"),
                         menu_action_settings, ds);
  taskbar_add_menu_entry(&ds->taskbar,
                         APP_T("Tarefas", "Tasks", "Tareas"),
                         menu_action_task_manager, ds);
  taskbar_add_menu_separator(&ds->taskbar);
  taskbar_add_menu_entry(&ds->taskbar,
                         APP_T("Sair", "Logout", "Cerrar sesion"),
                         menu_action_logout, ds);
  taskbar_add_menu_entry(&ds->taskbar,
                         APP_T("Reiniciar", "Reboot", "Reiniciar"),
                         menu_action_reboot, ds);
  taskbar_add_menu_entry(&ds->taskbar,
                         APP_T("Desligar", "Shutdown", "Apagar"),
                         menu_action_shutdown, ds);

  desktop_update_tray(ds);

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
    desktop_terminal_prompt(term);
    return;
  }

  if (len >= sizeof(line)) len = sizeof(line) - 1;
  for (i = 0; i < len; i++) line[i] = data[i];
  line[len] = '\0';

  if (kstreq(line, "exit")) {
    desktop_stop();
    return;
  }

  if (kstreq(line, "clear")) {
    terminal_clear(term);
    desktop_terminal_prompt(term);
    return;
  }

  desktop_shell_begin_terminal_output(term);
  if (!kernel_desktop_dispatch_shell_command(line)) {
    desktop_shell_end_terminal_output();
    terminal_write_string(term, "[erro] comando desconhecido\n");
    terminal_write_string(term, "Use help-any para listar comandos.\n");
    desktop_terminal_prompt(term);
    return;
  }
  desktop_shell_end_terminal_output();
  if (kernel_desktop_shell_should_stop()) {
    desktop_stop();
    return;
  }
  desktop_terminal_prompt(term);
}

void desktop_open_terminal_here(const char *target_path) {
  struct session_context *sess = kernel_desktop_shell_session();
  if (!sess) sess = session_active();
  if (target_path && target_path[0] && sess) session_set_cwd(sess, target_path);
  if (g_menu_desktop) desktop_open_terminal(g_menu_desktop);
}

void desktop_open_terminal(struct desktop_session *ds) {
  uint8_t scale = compositor_ui_scale();
  int32_t margin_x = 28 + 24 * (scale - 1);
  int32_t margin_y = 28 + 16 * (scale - 1);
  uint32_t width = 0;
  uint32_t height = 0;
  if (!ds) return;

  /* If already open, just focus the existing window */
  if (g_terminal_open && g_desktop_terminal.window) {
    compositor_show_window(g_desktop_terminal.window->id);
    compositor_focus_window(g_desktop_terminal.window->id);
    return;
  }

  g_terminal_open = 0;

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
    terminal_write_string(&g_desktop_terminal, "Type 'exit' to return to CLI.\n");
    terminal_write_string(&g_desktop_terminal, "Type 'bye' to log out.\n\n");
    desktop_terminal_prompt(&g_desktop_terminal);
    terminal_set_output_callback(&g_desktop_terminal, desktop_terminal_command, NULL);
    win->user_data = &g_desktop_terminal;
    win->on_paint = desktop_terminal_paint;
    win->on_key = desktop_terminal_key;
    win->on_scroll = desktop_terminal_scroll;
    win->on_close = desktop_terminal_on_close;
    win->on_resize = desktop_terminal_resize;
    win->bg_color = compositor_theme()->terminal_bg;
    win->border_color = compositor_theme()->window_border;
    ds->active_terminal = &g_desktop_terminal;
    g_terminal_open = 1;
  }
}

void desktop_handle_input(struct desktop_session *ds, uint32_t keycode,
                          char ch) {
  struct gui_window *focused = NULL;
  if (!ds) return;

  if (keycode == KEY_SUPER) {
    if (inline_prompt_is_open()) return;
    if (context_menu_is_open()) context_menu_close();
    taskbar_toggle_menu(&ds->taskbar);
    compositor_invalidate_all();
    return;
  }

  /* Etapa UX W7-ish (2026-05-03): inline_prompt absorve teclas
   * antes do dispatch normal (Enter/Esc/Backspace/printable). Util
   * para Rename/New no desktop_icons + file_manager. */
  if (inline_prompt_is_open()) {
    if (inline_prompt_handle_key(keycode, ch)) {
      compositor_invalidate_all();
      return;
    }
  }
  if (desktop_handle_overlay_escape(ds, keycode, ch)) return;
  if (ds->taskbar.menu_open &&
      taskbar_handle_menu_key(&ds->taskbar, keycode, ch)) {
    return;
  }

  focused = compositor_focused_window();
  {
    struct gui_event ev;
    uint32_t wid = focused ? focused->id : 0;
    /* For printable characters pass the ASCII value as keycode so that
     * window handlers can treat it uniformly.  For special keys (arrows,
     * function keys etc.) keycode already carries the KEY_* constant and
     * ch is 0, so we pass keycode directly. */
    uint32_t kc = ch ? (uint32_t)(uint8_t)ch : keycode;
    ev.type = GUI_EVENT_KEY_DOWN;
    ev.window_id = wid;
    ev.key.keycode = kc;
    ev.key.modifiers = 0;
    ev.key.ch = ch;
    ev.timestamp = 0;
    (void)gui_window_dispatch_event(&ev);
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

int desktop_run_frame(struct desktop_session *ds) {
  int work_done = 0;
  if (!ds || !ds->active) return 0;
  if (ds->settings) {
    const char *theme = ds->settings->theme[0] ? ds->settings->theme : "capyos";
    if (!kstreq(ds->theme_name, theme)) {
      desktop_apply_theme(ds);
      work_done = 1;
    }
  }

  if (desktop_handle_mouse(ds)) work_done = 1;
  desktop_sample_dispatcher_health(ds);

  {
    struct rtc_time rtc;
    char clock_buf[16];
    int clock_changed = 0;
    rtc_read(&rtc);
    rtc_format_time(&rtc, clock_buf, sizeof(clock_buf));
    clock_changed = taskbar_update_clock(&ds->taskbar, clock_buf);
    if (clock_changed) work_done = 1;
    if (clock_changed && desktop_update_tray(ds)) work_done = 1;
  }

  /* Post-M5 W2: Task Manager auto-refresh tick. Cheap when the
   * window is closed (single load + branch); when open it
   * invalidates ~every TASK_MANAGER_AUTO_REFRESH_FRAMES frames so
   * apps started after the window opened (and processes that
   * exited) reflect in the row list within ~0.5s on real hw. */
  task_manager_tick();

  /* Browser per-frame tick erradicado na sessao 6 (2026-05-05).
   * Quando o port do Firefox aterrissar, o tick voltara como uma
   * chamada para `firefox_app_tick(...)`. */

  {
    struct mouse_state ms;
    int scene_dirty = compositor_needs_render();
    int cursor_changed = 0;
    mouse_get_state(&ms);
    cursor_changed = !ds->cursor_valid || ds->cursor_x != ms.x ||
                     ds->cursor_y != ms.y ||
                     compositor_cursor_needs_render(ms.x, ms.y);
    if (scene_dirty) {
      compositor_render();
      work_done = 1;
    }
    if (scene_dirty || cursor_changed) {
      compositor_render_cursor(ms.x, ms.y);
      ds->cursor_valid = 1;
      ds->cursor_x = ms.x;
      ds->cursor_y = ms.y;
      if (cursor_changed) work_done = 1;
    }
  }

  return work_done;
}

void desktop_shutdown(struct desktop_session *ds) {
  if (!ds) return;
  compositor_shutdown();
  /* Reset taskbar menu popup pointer (compositor_init will free the surface) */
  ds->taskbar.menu_popup = NULL;
  ds->taskbar.menu_open = 0;
  ds->taskbar.menu_entry_count = 0;
  ds->taskbar.recent_count = 0;
  ds->taskbar.item_count = 0;
  ds->taskbar.window = NULL;
  ds->active_terminal = NULL;
  g_desktop_terminal.window = NULL;
  g_terminal_open = 0;
  g_shell_output_term = NULL;
  g_menu_desktop = NULL;
  shell_set_output_callbacks(NULL, NULL);
  shell_set_clear_callback(NULL);
  ds->active = 0;
}
