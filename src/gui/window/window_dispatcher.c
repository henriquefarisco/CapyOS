#include "gui/window_dispatcher.h"

#include "drivers/input/mouse.h"
#include "gui/compositor.h"

static struct gui_window_dispatcher_stats dispatcher_stats;
static uint32_t captured_mouse_window_id;

static int window_can_receive_event(const struct gui_window *win) {
  return win && win->visible && !win->minimized;
}

static int window_client_coords(const struct gui_window *win, int32_t x,
                                int32_t y, int32_t *lx, int32_t *ly) {
  if (!window_can_receive_event(win)) return 0;
  if (x < win->frame.x || y < win->frame.y) return 0;
  if (x >= win->frame.x + (int32_t)win->frame.width) return 0;
  if (y >= win->frame.y + (int32_t)win->frame.height) return 0;
  if (lx) *lx = x - win->frame.x;
  if (ly) *ly = y - win->frame.y;
  return 1;
}

static void window_local_coords(const struct gui_window *win, int32_t x,
                                int32_t y, int32_t *lx, int32_t *ly) {
  if (lx) *lx = x - win->frame.x;
  if (ly) *ly = y - win->frame.y;
}

static void invalidate_if_receivable(uint32_t window_id) {
  struct gui_window *win = compositor_get_window(window_id);
  if (window_can_receive_event(win)) compositor_invalidate(window_id);
}

static struct gui_window *dispatcher_target_window(const struct gui_event *ev,
                                                   int allow_focus_fallback) {
  if (!ev) return 0;
  if (ev->window_id != 0) return compositor_get_window(ev->window_id);
  if (allow_focus_fallback) return compositor_focused_window();
  return 0;
}

static struct gui_window *dispatcher_mouse_target(const struct gui_event *ev) {
  if (!ev) return 0;
  if (ev->window_id != 0) return compositor_get_window(ev->window_id);
  return compositor_window_at(ev->mouse.x, ev->mouse.y);
}

static int dispatch_captured_mouse(const struct gui_event *ev) {
  struct gui_window *win = compositor_get_window(captured_mouse_window_id);
  uint32_t window_id = captured_mouse_window_id;
  int32_t lx = 0;
  int32_t ly = 0;
  if (!window_can_receive_event(win)) {
    captured_mouse_window_id = 0;
    dispatcher_stats.missing_target_total++;
    return 0;
  }
  if (!win->on_mouse) {
    captured_mouse_window_id = 0;
    dispatcher_stats.missing_handler_total++;
    return 0;
  }
  window_local_coords(win, ev->mouse.x, ev->mouse.y, &lx, &ly);
  win->on_mouse(win, lx, ly, ev->mouse.buttons);
  if (ev->type == GUI_EVENT_MOUSE_UP) captured_mouse_window_id = 0;
  invalidate_if_receivable(window_id);
  dispatcher_stats.handled_total++;
  return 1;
}

static int dispatch_key_down(const struct gui_event *ev) {
  struct gui_window *win = dispatcher_target_window(ev, 1);
  uint32_t window_id = 0;
  if (!window_can_receive_event(win)) {
    dispatcher_stats.missing_target_total++;
    return 0;
  }
  if (!win->on_key) {
    dispatcher_stats.missing_handler_total++;
    return 0;
  }
  window_id = win->id;
  win->on_key(win, ev->key.keycode, ev->key.modifiers);
  invalidate_if_receivable(window_id);
  dispatcher_stats.handled_total++;
  return 1;
}

static int dispatch_key_up(const struct gui_event *ev) {
  struct gui_window *win = dispatcher_target_window(ev, 1);
  uint32_t window_id = 0;
  if (!window_can_receive_event(win)) {
    dispatcher_stats.missing_target_total++;
    return 0;
  }
  if (!win->on_key_up) {
    dispatcher_stats.missing_handler_total++;
    return 0;
  }
  window_id = win->id;
  win->on_key_up(win, ev->key.keycode, ev->key.modifiers);
  invalidate_if_receivable(window_id);
  dispatcher_stats.handled_total++;
  return 1;
}

static int dispatch_mouse_move(const struct gui_event *ev) {
  struct gui_window *win = 0;
  int32_t lx = 0;
  int32_t ly = 0;
  if (captured_mouse_window_id != 0 && ev->mouse.buttons != 0) {
    return dispatch_captured_mouse(ev);
  }
  if (captured_mouse_window_id != 0 && ev->mouse.buttons == 0) {
    captured_mouse_window_id = 0;
  }
  win = dispatcher_mouse_target(ev);
  if (!window_can_receive_event(win)) {
    dispatcher_stats.missing_target_total++;
    return 0;
  }
  if (!window_client_coords(win, ev->mouse.x, ev->mouse.y, &lx, &ly)) {
    dispatcher_stats.ignored_total++;
    return 0;
  }
  if (!win->on_hover) {
    dispatcher_stats.missing_handler_total++;
    return 0;
  }
  win->on_hover(win, lx, ly);
  dispatcher_stats.handled_total++;
  return 1;
}

static int dispatch_mouse_button(const struct gui_event *ev) {
  struct gui_window *win = 0;
  uint32_t window_id = 0;
  int32_t lx = 0;
  int32_t ly = 0;
  if (ev->type == GUI_EVENT_MOUSE_UP && captured_mouse_window_id != 0) {
    return dispatch_captured_mouse(ev);
  }
  win = dispatcher_mouse_target(ev);
  if (!window_can_receive_event(win)) {
    dispatcher_stats.missing_target_total++;
    return 0;
  }
  window_id = win->id;
  if (ev->type == GUI_EVENT_MOUSE_DOWN) compositor_focus_window(window_id);
  if (!window_client_coords(win, ev->mouse.x, ev->mouse.y, &lx, &ly)) {
    dispatcher_stats.ignored_total++;
    return 0;
  }
  if (ev->type == GUI_EVENT_MOUSE_DOWN &&
      (ev->mouse.buttons & MOUSE_BUTTON_RIGHT) &&
      win->on_context_menu) {
    win->on_context_menu(win, lx, ly);
    invalidate_if_receivable(window_id);
    dispatcher_stats.handled_total++;
    return 1;
  }
  if (!win->on_mouse) {
    dispatcher_stats.missing_handler_total++;
    return 0;
  }
  if (ev->type == GUI_EVENT_MOUSE_DOWN && win->capture_mouse)
    captured_mouse_window_id = window_id;
  win->on_mouse(win, lx, ly, ev->mouse.buttons);
  invalidate_if_receivable(window_id);
  dispatcher_stats.handled_total++;
  return 1;
}

static int dispatch_mouse_scroll(const struct gui_event *ev) {
  struct gui_window *win = dispatcher_target_window(ev, 1);
  uint32_t window_id = 0;
  if (!window_can_receive_event(win)) {
    dispatcher_stats.missing_target_total++;
    return 0;
  }
  if (!win->on_scroll) {
    dispatcher_stats.missing_handler_total++;
    return 0;
  }
  window_id = win->id;
  win->on_scroll(win, (int32_t)ev->mouse.dy);
  invalidate_if_receivable(window_id);
  dispatcher_stats.handled_total++;
  return 1;
}

static int dispatch_paint(const struct gui_event *ev) {
  struct gui_window *win = dispatcher_target_window(ev, 0);
  if (!window_can_receive_event(win)) {
    dispatcher_stats.missing_target_total++;
    return 0;
  }
  if (!win->on_paint) {
    dispatcher_stats.missing_handler_total++;
    return 0;
  }
  win->on_paint(win);
  dispatcher_stats.handled_total++;
  return 1;
}

static int dispatch_timer(const struct gui_event *ev) {
  struct gui_window *win = dispatcher_target_window(ev, 0);
  uint32_t window_id = 0;
  if (!window_can_receive_event(win)) {
    dispatcher_stats.missing_target_total++;
    return 0;
  }
  if (!win->on_timer) {
    dispatcher_stats.missing_handler_total++;
    return 0;
  }
  window_id = win->id;
  win->on_timer(win, ev->timer.timer_id);
  invalidate_if_receivable(window_id);
  dispatcher_stats.handled_total++;
  return 1;
}

static int dispatch_window_focus(const struct gui_event *ev) {
  struct gui_window *win = dispatcher_target_window(ev, 0);
  uint32_t window_id = 0;
  if (!window_can_receive_event(win)) {
    dispatcher_stats.missing_target_total++;
    return 0;
  }
  if (!win->on_focus) {
    dispatcher_stats.missing_handler_total++;
    return 0;
  }
  window_id = win->id;
  win->on_focus(win);
  invalidate_if_receivable(window_id);
  dispatcher_stats.handled_total++;
  return 1;
}

static int dispatch_window_blur(const struct gui_event *ev) {
  struct gui_window *win = dispatcher_target_window(ev, 0);
  uint32_t window_id = 0;
  /* INVARIANT (do NOT replace with `!window_can_receive_event(win)`):
   * blur events are pushed during compositor_destroy_window,
   * compositor_minimize_window and compositor_focus_window AFTER the
   * window has been marked invisible / minimized. The callbacks here
   * release focus-owned resources (caret blinking, IME state, input
   * capture). Rejecting blur on !visible / minimized would leak those
   * resources and break the alpha.235 session handoff chain that
   * requires clean focus release before activating a new session. */
  if (!win) {
    dispatcher_stats.missing_target_total++;
    return 0;
  }
  if (!win->on_blur) {
    dispatcher_stats.missing_handler_total++;
    return 0;
  }
  window_id = win->id;
  win->on_blur(win);
  invalidate_if_receivable(window_id);
  dispatcher_stats.handled_total++;
  return 1;
}

static int dispatch_compositor_owned_lifecycle(const struct gui_event *ev) {
  (void)ev;
  dispatcher_stats.ignored_total++;
  return 0;
}

void gui_window_dispatcher_reset_stats(void) {
  dispatcher_stats.dispatched_total = 0;
  dispatcher_stats.handled_total = 0;
  dispatcher_stats.missing_target_total = 0;
  dispatcher_stats.missing_handler_total = 0;
  dispatcher_stats.ignored_total = 0;
}

void gui_window_dispatcher_reset(void) {
  gui_window_dispatcher_reset_stats();
  captured_mouse_window_id = 0;
}

void gui_window_dispatcher_stats(struct gui_window_dispatcher_stats *out) {
  if (out) *out = dispatcher_stats;
}

int gui_window_dispatcher_snapshot(struct gui_window_dispatcher_snapshot *out) {
  struct gui_window *win = 0;
  if (!out) return 0;
  out->stats = dispatcher_stats;
  out->captured_mouse_window_id = captured_mouse_window_id;
  if (captured_mouse_window_id != 0) {
    win = compositor_get_window(captured_mouse_window_id);
  }
  out->mouse_capture_active =
      (win && window_can_receive_event(win) && win->on_mouse) ? 1 : 0;
  return 1;
}

static void dispatcher_input_routes_snapshot(
    struct gui_window_dispatcher_input_routes *routes) {
  if (!routes) return;
  routes->key_down = 1;
  routes->key_up = 1;
  routes->mouse_scroll = 1;
  routes->mouse_hover = 1;
  routes->mouse_left_button = 1;
  routes->mouse_right_context = 1;
  routes->mouse_capture_opt_in = 1;
  routes->queue_mirror_free = 1;
  routes->overlays_direct = 1;
  routes->window_manager_direct = 1;
  routes->titlebar_direct = 1;
  routes->taskbar_direct = 1;
  routes->desktop_icons_direct = 1;
}

int gui_window_dispatcher_health_snapshot(struct gui_window_dispatcher_health *out) {
  if (!out) return 0;
  if (!gui_window_dispatcher_snapshot(&out->dispatcher)) return 0;
  dispatcher_input_routes_snapshot(&out->routes);
  out->queue_snapshot_available = gui_event_snapshot(&out->queue);
  if (!out->queue_snapshot_available) {
    out->queue.capacity = 0;
    out->queue.pending = 0;
    out->queue.space_available = 0;
    out->queue.high_watermark = 0;
    out->queue.dropped_total = 0;
  }
  out->backlog_warning =
      (out->queue_snapshot_available && out->queue.capacity > 0 &&
       out->queue.pending > (out->queue.capacity / 2u)) ? 1 : 0;
  out->drop_warning =
      (out->queue_snapshot_available && out->queue.dropped_total != 0) ? 1 : 0;
  out->stale_capture_warning =
      (out->dispatcher.captured_mouse_window_id != 0 &&
       !out->dispatcher.mouse_capture_active) ? 1 : 0;
  return 1;
}

int gui_window_dispatch_event(const struct gui_event *ev) {
  if (!ev) return 0;
  dispatcher_stats.dispatched_total++;
  switch (ev->type) {
    case GUI_EVENT_KEY_DOWN:
      return dispatch_key_down(ev);
    case GUI_EVENT_KEY_UP:
      return dispatch_key_up(ev);
    case GUI_EVENT_MOUSE_MOVE:
      return dispatch_mouse_move(ev);
    case GUI_EVENT_MOUSE_DOWN:
    case GUI_EVENT_MOUSE_UP:
      return dispatch_mouse_button(ev);
    case GUI_EVENT_MOUSE_SCROLL:
      return dispatch_mouse_scroll(ev);
    case GUI_EVENT_PAINT:
      return dispatch_paint(ev);
    case GUI_EVENT_TIMER:
      return dispatch_timer(ev);
    case GUI_EVENT_WINDOW_CLOSE:
    case GUI_EVENT_WINDOW_RESIZE:
      return dispatch_compositor_owned_lifecycle(ev);
    case GUI_EVENT_WINDOW_FOCUS:
      return dispatch_window_focus(ev);
    case GUI_EVENT_WINDOW_BLUR:
      return dispatch_window_blur(ev);
    default:
      dispatcher_stats.ignored_total++;
      return 0;
  }
}

static void dispatch_from_queue(const struct gui_event *ev, void *ctx) {
  (void)ctx;
  gui_window_dispatch_event(ev);
}

uint32_t gui_window_dispatch(uint32_t max_events) {
  return gui_event_dispatch(dispatch_from_queue, 0, max_events);
}
