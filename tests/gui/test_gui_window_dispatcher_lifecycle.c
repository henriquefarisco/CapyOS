/*
 * tests/test_gui_window_dispatcher_lifecycle.c
 *
 * Snapshot/health, focus+blur lifecycle, compositor-owned lifecycle,
 * right-click context menu, timer routing and miss/ignore coverage
 * for the GUI window dispatcher. Carved out of
 * `tests/test_gui_window_dispatcher.c` at the 2026-05-15 monolith
 * refactor so each host-test translation unit stays under the
 * 900-line layout limit. The fixture (counter globals, callback
 * implementations, reset/shutdown helpers) and the first six dispatch
 * test cases live in `tests/test_gui_window_dispatcher.c`. Shared
 * TEST/PASS/FAIL macros, counter externs, callback declarations and
 * fixture-helper declarations come from
 * `tests/test_gui_window_dispatcher_internal.h`.
 */
#include "test_gui_window_dispatcher_internal.h"

static void test_dispatcher_snapshot_events(void) {
  struct gui_window *a;
  struct gui_window_dispatcher_snapshot snapshot;

  reset_fixture();
  a = compositor_create_window("A", 20, 40, 60, 40);
  if (!a) {
    TEST("gui_window_dispatcher_snapshot: fixture creates window");
    FAIL("create window");
    shutdown_fixture();
    return;
  }
  a->on_mouse = test_gwd_on_mouse;
  a->capture_mouse = 1;
  compositor_show_window(a->id);
  gui_event_flush();

  TEST("gui_window_dispatcher_snapshot: rejects null output");
  if (gui_window_dispatcher_snapshot(0) == 0) PASS();
  else FAIL("snapshot null");

  TEST("gui_window_dispatcher_snapshot: reports idle capture state");
  if (gui_window_dispatcher_snapshot(&snapshot) == 1 &&
      snapshot.captured_mouse_window_id == 0 &&
      snapshot.mouse_capture_active == 0 &&
      snapshot.stats.dispatched_total == 0) PASS();
  else FAIL("snapshot idle");

  {
    struct gui_window_dispatcher_health health;
    TEST("gui_window_dispatcher_health_snapshot: reports input route contract");
    if (gui_window_dispatcher_health_snapshot(&health) == 1 &&
        health.routes.key_down == 1 &&
        health.routes.key_up == 1 &&
        health.routes.mouse_scroll == 1 &&
        health.routes.mouse_hover == 1 &&
        health.routes.mouse_left_button == 1 &&
        health.routes.mouse_right_context == 1 &&
        health.routes.mouse_capture_opt_in == 1 &&
        health.routes.queue_mirror_free == 1 &&
        health.routes.overlays_direct == 1 &&
        health.routes.window_manager_direct == 1 &&
        health.routes.titlebar_direct == 1 &&
        health.routes.taskbar_direct == 1 &&
        health.routes.desktop_icons_direct == 1) PASS();
    else FAIL("health route contract");
  }

  gui_event_push_mouse_button(1, 30, 50, MOUSE_BUTTON_LEFT, 0);
  if (gui_window_dispatch(8) != 1 || mouse_calls != 1) {
    TEST("gui_window_dispatcher_snapshot: establishes fixture capture");
    FAIL("capture setup");
    shutdown_fixture();
    return;
  }
  gui_event_flush();

  TEST("gui_window_dispatcher_snapshot: reports active capture and stats");
  if (gui_window_dispatcher_snapshot(&snapshot) == 1 &&
      snapshot.captured_mouse_window_id == a->id &&
      snapshot.mouse_capture_active == 1 &&
      snapshot.stats.dispatched_total == 1 &&
      snapshot.stats.handled_total == 1) PASS();
  else FAIL("snapshot active");

  compositor_hide_window(a->id);
  gui_event_flush();
  TEST("gui_window_dispatcher_snapshot: detects stale capture target");
  if (gui_window_dispatcher_snapshot(&snapshot) == 1 &&
      snapshot.captured_mouse_window_id == a->id &&
      snapshot.mouse_capture_active == 0) PASS();
  else FAIL("snapshot stale capture");

  gui_window_dispatcher_reset();
  TEST("gui_window_dispatcher_snapshot: reset clears capture and metrics");
  if (gui_window_dispatcher_snapshot(&snapshot) == 1 &&
      snapshot.captured_mouse_window_id == 0 &&
      snapshot.mouse_capture_active == 0 &&
      snapshot.stats.dispatched_total == 0 &&
      snapshot.stats.handled_total == 0) PASS();
  else FAIL("snapshot reset");

  shutdown_fixture();
}

static void test_dispatch_lifecycle_events(void) {
  struct gui_window *a;
  struct gui_window *b;
  struct gui_window_dispatcher_stats stats;

  reset_fixture();
  a = compositor_create_window("A", 10, 30, 80, 48);
  b = compositor_create_window("B", 100, 40, 80, 48);
  if (!a || !b) {
    TEST("gui_window_dispatch lifecycle: fixture creates windows");
    FAIL("create window");
    shutdown_fixture();
    return;
  }
  a->on_focus = test_gwd_on_focus;
  a->on_blur = test_gwd_on_blur;
  b->on_focus = test_gwd_on_focus;
  b->on_blur = test_gwd_on_blur;
  compositor_show_window(a->id);
  compositor_show_window(b->id);
  gui_event_flush();

  gui_event_push_window_focus(a->id, 0);
  TEST("gui_window_dispatch: focus event routes to visible target");
  if (gui_window_dispatch(8) == 1 &&
      focus_calls == 1 &&
      focus_last_id == a->id &&
      gui_event_pending() == 1) PASS();
  else FAIL("dispatch focus");

  gui_event_flush();
  gui_event_push_window_blur(a->id, 0);
  TEST("gui_window_dispatch: blur event routes to visible target");
  if (gui_window_dispatch(8) == 1 &&
      blur_calls == 1 &&
      blur_last_id == a->id &&
      gui_event_pending() == 1) PASS();
  else FAIL("dispatch blur");

  gui_event_flush();
  compositor_focus_window(b->id);
  gui_event_flush();
  compositor_hide_window(b->id);
  TEST("gui_window_dispatch: blur routes after target becomes hidden");
  if (gui_window_dispatch(8) == 1 &&
      blur_calls == 2 &&
      blur_last_id == b->id &&
      gui_event_pending() == 0) PASS();
  else FAIL("dispatch hidden blur");

  gui_event_push_window_focus(b->id, 0);
  TEST("gui_window_dispatch: hidden focus target is not dispatched");
  if (gui_window_dispatch(8) == 1 &&
      focus_calls == 1 &&
      gui_event_pending() == 0) PASS();
  else FAIL("dispatch hidden focus");

  compositor_show_window(b->id);
  gui_event_flush();
  b->on_focus = 0;
  gui_event_push_window_focus(b->id, 0);
  TEST("gui_window_dispatch: focus missing handler is counted safely");
  if (gui_window_dispatch(8) == 1 &&
      focus_calls == 1 &&
      gui_event_pending() == 0) PASS();
  else FAIL("dispatch focus handler");

  gui_window_dispatcher_stats(&stats);
  TEST("gui_window_dispatcher_stats: tracks lifecycle categories");
  if (stats.dispatched_total == 5 &&
      stats.handled_total == 3 &&
      stats.missing_target_total == 1 &&
      stats.missing_handler_total == 1 &&
      stats.ignored_total == 0) PASS();
  else FAIL("dispatch lifecycle stats");

  shutdown_fixture();
}

static void test_dispatch_compositor_owned_lifecycle_events(void) {
  struct gui_window *win;
  struct gui_window_dispatcher_stats stats;

  reset_fixture();
  win = compositor_create_window("L", 20, 30, 80, 48);
  if (!win) {
    TEST("gui_window_dispatch owned lifecycle: fixture creates window");
    FAIL("create window");
    shutdown_fixture();
    return;
  }
  win->on_close = test_gwd_on_close;
  win->on_resize = test_gwd_on_resize;
  compositor_show_window(win->id);
  gui_event_flush();

  gui_event_push_window_resize(win->id, 96, 64, 0);
  gui_event_push_window_close(win->id, 0);
  TEST("gui_window_dispatch: compositor-owned lifecycle is ignored");
  if (gui_window_dispatch(8) == 2 &&
      close_calls == 0 &&
      resize_calls == 0 &&
      gui_event_pending() == 0) PASS();
  else FAIL("dispatch owned lifecycle");

  gui_window_dispatcher_stats(&stats);
  TEST("gui_window_dispatcher_stats: tracks owned lifecycle as ignored");
  if (stats.dispatched_total == 2 &&
      stats.handled_total == 0 &&
      stats.missing_target_total == 0 &&
      stats.missing_handler_total == 0 &&
      stats.ignored_total == 2) PASS();
  else FAIL("dispatch owned lifecycle stats");

  shutdown_fixture();
}

static void test_dispatch_context_menu_events(void) {
  struct gui_window *a;
  struct gui_window *b;
  struct gui_window_dispatcher_stats stats;

  reset_fixture();
  a = compositor_create_window("A", 10, 40, 80, 48);
  b = compositor_create_window("B", 100, 60, 80, 48);
  if (!a || !b) {
    TEST("gui_window_dispatch context: fixture creates windows");
    FAIL("create window");
    shutdown_fixture();
    return;
  }
  b->on_mouse = test_gwd_on_mouse;
  b->on_context_menu = test_gwd_on_context_menu;
  compositor_show_window(a->id);
  compositor_show_window(b->id);
  compositor_focus_window(a->id);
  gui_event_flush();

  gui_event_push_mouse_button(1, 118, 78, MOUSE_BUTTON_RIGHT, 0);
  TEST("gui_window_dispatch: right-click routes context menu");
  if (gui_window_dispatch(8) == 1 &&
      context_calls == 1 &&
      context_last_x == 18 &&
      context_last_y == 18 &&
      mouse_calls == 0 &&
      compositor_focused_window() == b &&
      gui_event_pending() == 3) PASS();
  else FAIL("dispatch context");

  gui_event_flush();
  b->on_context_menu = 0;
  gui_event_push_mouse_button(1, 119, 79, MOUSE_BUTTON_RIGHT, 0);
  TEST("gui_window_dispatch: right-click falls back to on_mouse");
  if (gui_window_dispatch(8) == 1 &&
      context_calls == 1 &&
      mouse_calls == 1 &&
      mouse_last_x == 19 &&
      mouse_last_y == 19 &&
      mouse_last_buttons == MOUSE_BUTTON_RIGHT &&
      gui_event_pending() == 1) PASS();
  else FAIL("dispatch context fallback");

  gui_event_flush();
  b->on_context_menu = test_gwd_on_context_menu;
  gui_event_push_mouse_button(1, 120, 80, MOUSE_BUTTON_LEFT, 0);
  TEST("gui_window_dispatch: left-click stays on mouse path");
  if (gui_window_dispatch(8) == 1 &&
      context_calls == 1 &&
      mouse_calls == 2 &&
      mouse_last_x == 20 &&
      mouse_last_y == 20 &&
      mouse_last_buttons == MOUSE_BUTTON_LEFT &&
      gui_event_pending() == 1) PASS();
  else FAIL("dispatch context left");

  gui_window_dispatcher_stats(&stats);
  TEST("gui_window_dispatcher_stats: tracks context menu dispatch");
  if (stats.dispatched_total == 3 &&
      stats.handled_total == 3 &&
      stats.missing_target_total == 0 &&
      stats.missing_handler_total == 0 &&
      stats.ignored_total == 0) PASS();
  else FAIL("dispatch context stats");

  shutdown_fixture();
}

static void test_dispatch_timer_events(void) {
  struct gui_window *win;
  struct gui_window_dispatcher_stats stats;

  reset_fixture();
  win = compositor_create_window("T", 10, 30, 80, 48);
  if (!win) {
    TEST("gui_window_dispatch timer: fixture creates window");
    FAIL("create window");
    shutdown_fixture();
    return;
  }
  win->on_timer = test_gwd_on_timer;
  compositor_show_window(win->id);
  gui_event_flush();

  gui_event_push_timer(win->id, 7, 0);
  TEST("gui_window_dispatch: timer routes to explicit target");
  if (gui_window_dispatch(8) == 1 &&
      timer_calls == 1 &&
      timer_last_id == 7 &&
      gui_event_pending() == 1) PASS();
  else FAIL("dispatch timer");

  gui_event_flush();
  TEST("gui_event_push_timer: timer requires explicit window target");
  if (gui_event_push_timer(0, 8, 0) != 0 &&
      gui_window_dispatch(8) == 0 &&
      timer_calls == 1 &&
      gui_event_pending() == 0) PASS();
  else FAIL("dispatch timer target");

  win->on_timer = 0;
  gui_event_push_timer(win->id, 9, 0);
  TEST("gui_window_dispatch: timer missing handler is counted safely");
  if (gui_window_dispatch(8) == 1 &&
      timer_calls == 1 &&
      gui_event_pending() == 0) PASS();
  else FAIL("dispatch timer handler");

  win->on_timer = test_gwd_on_timer;
  compositor_minimize_window(win->id);
  gui_event_flush();
  gui_event_push_timer(win->id, 10, 0);
  TEST("gui_window_dispatch: timer minimized target is not dispatched");
  if (gui_window_dispatch(8) == 1 &&
      timer_calls == 1 &&
      gui_event_pending() == 0) PASS();
  else FAIL("dispatch timer minimized");

  gui_window_dispatcher_stats(&stats);
  TEST("gui_window_dispatcher_stats: tracks timer categories");
  if (stats.dispatched_total == 3 &&
      stats.handled_total == 1 &&
      stats.missing_target_total == 1 &&
      stats.missing_handler_total == 1 &&
      stats.ignored_total == 0) PASS();
  else FAIL("dispatch timer stats");

  shutdown_fixture();
}

static void test_dispatch_miss_and_ignore(void) {
  struct gui_window *win;
  struct gui_window_dispatcher_stats stats;

  reset_fixture();
  win = compositor_create_window("M", 10, 30, 80, 48);
  if (!win) {
    TEST("gui_window_dispatch miss/ignore: fixture creates window");
    FAIL("create window");
    shutdown_fixture();
    return;
  }
  compositor_show_window(win->id);
  gui_event_flush();

  gui_event_push_key(9999, 'X', 0, 'X', 0);
  TEST("gui_window_dispatch: missing target is dropped safely");
  if (gui_window_dispatch(8) == 1 &&
      key_calls == 0 &&
      gui_event_pending() == 0) PASS();
  else FAIL("dispatch missing target");

  gui_event_push_key(win->id, 'Y', 0, 'Y', 0);
  TEST("gui_window_dispatch: missing handler is counted safely");
  if (gui_window_dispatch(8) == 1 &&
      key_calls == 0 &&
      gui_event_pending() == 0) PASS();
  else FAIL("dispatch missing handler");

  gui_event_push_window_resize(win->id, 80, 48, 0);
  TEST("gui_window_dispatch: unsupported lifecycle event is ignored");
  if (gui_window_dispatch(8) == 1 &&
      gui_event_pending() == 0) PASS();
  else FAIL("dispatch ignored");

  win->on_key = test_gwd_on_key;
  compositor_hide_window(win->id);
  gui_event_push_key(win->id, 'Z', 0, 'Z', 0);
  TEST("gui_window_dispatch: hidden target is not dispatched");
  if (gui_window_dispatch(8) == 1 &&
      key_calls == 0 &&
      gui_event_pending() == 0) PASS();
  else FAIL("dispatch hidden");

  compositor_show_window(win->id);
  compositor_minimize_window(win->id);
  gui_event_push_key(win->id, 'W', 0, 'W', 0);
  TEST("gui_window_dispatch: minimized target is not dispatched");
  if (gui_window_dispatch(8) == 1 &&
      key_calls == 0 &&
      gui_event_pending() == 0) PASS();
  else FAIL("dispatch minimized");

  gui_window_dispatcher_stats(&stats);
  TEST("gui_window_dispatcher_stats: tracks miss and ignore categories");
  if (stats.dispatched_total == 5 &&
      stats.handled_total == 0 &&
      stats.missing_target_total == 3 &&
      stats.missing_handler_total == 1 &&
      stats.ignored_total == 1) PASS();
  else FAIL("dispatch miss stats");

  shutdown_fixture();
}

void test_gui_window_dispatcher_lifecycle_cases(void) {
  test_dispatcher_snapshot_events();
  test_dispatch_lifecycle_events();
  test_dispatch_compositor_owned_lifecycle_events();
  test_dispatch_context_menu_events();
  test_dispatch_timer_events();
  test_dispatch_miss_and_ignore();
}
