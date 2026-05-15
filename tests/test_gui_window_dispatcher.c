#include <stdio.h>

#include "drivers/input/mouse.h"
#include "gui/compositor.h"
#include "gui/event.h"
#include "gui/window_dispatcher.h"

static int tests_run = 0;
static int tests_passed = 0;
static uint32_t key_calls = 0;
static uint32_t key_last_code = 0;
static uint8_t key_last_mods = 0;
static uint32_t key_up_calls = 0;
static uint32_t key_up_last_code = 0;
static uint8_t key_up_last_mods = 0;
static uint32_t focus_calls = 0;
static uint32_t focus_last_id = 0;
static uint32_t blur_calls = 0;
static uint32_t blur_last_id = 0;
static uint32_t scroll_calls = 0;
static int32_t scroll_last_delta = 0;
static uint32_t paint_calls = 0;
static uint32_t hover_calls = 0;
static int32_t hover_last_x = 0;
static int32_t hover_last_y = 0;
static uint32_t mouse_calls = 0;
static int32_t mouse_last_x = 0;
static int32_t mouse_last_y = 0;
static uint8_t mouse_last_buttons = 0;
static uint32_t context_calls = 0;
static int32_t context_last_x = 0;
static int32_t context_last_y = 0;
static uint32_t timer_calls = 0;
static uint32_t timer_last_id = 0;
static uint32_t close_calls = 0;
static uint32_t resize_calls = 0;

#define TEST(name) \
  do { tests_run++; printf("  %-58s ", name); } while (0)
#define PASS() \
  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(msg) \
  do { printf("FAIL: %s\n", msg); } while (0)

static void reset_counters(void) {
  key_calls = 0;
  key_last_code = 0;
  key_last_mods = 0;
  key_up_calls = 0;
  key_up_last_code = 0;
  key_up_last_mods = 0;
  focus_calls = 0;
  focus_last_id = 0;
  blur_calls = 0;
  blur_last_id = 0;
  scroll_calls = 0;
  scroll_last_delta = 0;
  paint_calls = 0;
  hover_calls = 0;
  hover_last_x = 0;
  hover_last_y = 0;
  mouse_calls = 0;
  mouse_last_x = 0;
  mouse_last_y = 0;
  mouse_last_buttons = 0;
  context_calls = 0;
  context_last_x = 0;
  context_last_y = 0;
  timer_calls = 0;
  timer_last_id = 0;
  close_calls = 0;
  resize_calls = 0;
}

static void reset_fixture(void) {
  static uint32_t framebuffer[320 * 200];
  gui_event_init();
  gui_window_dispatcher_reset();
  reset_counters();
  compositor_init(framebuffer, 320, 200, 320 * 4);
}

static void shutdown_fixture(void) {
  compositor_shutdown();
  gui_event_init();
  gui_window_dispatcher_reset();
  reset_counters();
}

static void on_key(struct gui_window *win, uint32_t keycode, uint8_t mods) {
  (void)win;
  key_calls++;
  key_last_code = keycode;
  key_last_mods = mods;
}

static void on_key_up(struct gui_window *win, uint32_t keycode, uint8_t mods) {
  (void)win;
  key_up_calls++;
  key_up_last_code = keycode;
  key_up_last_mods = mods;
}

static void on_focus(struct gui_window *win) {
  focus_calls++;
  focus_last_id = win ? win->id : 0;
}

static void on_blur(struct gui_window *win) {
  blur_calls++;
  blur_last_id = win ? win->id : 0;
}

static void on_scroll(struct gui_window *win, int32_t delta) {
  (void)win;
  scroll_calls++;
  scroll_last_delta = delta;
}

static void on_paint(struct gui_window *win) {
  (void)win;
  paint_calls++;
}

static void on_hover(struct gui_window *win, int32_t x, int32_t y) {
  (void)win;
  hover_calls++;
  hover_last_x = x;
  hover_last_y = y;
}

static void on_mouse(struct gui_window *win, int32_t x, int32_t y,
                     uint8_t buttons) {
  (void)win;
  mouse_calls++;
  mouse_last_x = x;
  mouse_last_y = y;
  mouse_last_buttons = buttons;
}

static void on_context_menu(struct gui_window *win, int32_t x, int32_t y) {
  (void)win;
  context_calls++;
  context_last_x = x;
  context_last_y = y;
}

static void on_timer(struct gui_window *win, uint32_t timer_id) {
  (void)win;
  timer_calls++;
  timer_last_id = timer_id;
}

static void on_close(struct gui_window *win) {
  (void)win;
  close_calls++;
}

static void on_resize(struct gui_window *win, uint32_t w, uint32_t h) {
  (void)win;
  (void)w;
  (void)h;
  resize_calls++;
}

static void test_dispatch_noop_and_key(void) {
  struct gui_window *win;
  struct gui_window_dispatcher_stats stats;

  reset_fixture();
  win = compositor_create_window("D", 10, 30, 80, 48);
  if (!win) {
    TEST("gui_window_dispatch: fixture creates window");
    FAIL("create window");
    shutdown_fixture();
    return;
  }
  win->on_key = on_key;
  win->on_key_up = on_key_up;
  win->on_paint = on_paint;
  compositor_show_window(win->id);
  compositor_focus_window(win->id);
  gui_event_flush();

  gui_event_push_key(win->id, 'A', 2, 'A', 0);
  TEST("gui_window_dispatch(max=0): does not drain queue");
  if (gui_window_dispatch(0) == 0 &&
      gui_event_pending() == 1) PASS();
  else FAIL("dispatch zero");

  TEST("gui_window_dispatch: routes key to target window");
  if (gui_window_dispatch(8) == 1 &&
      key_calls == 1 &&
      key_last_code == 'A' &&
      key_last_mods == 2 &&
      gui_event_pending() == 1) PASS();
  else FAIL("dispatch key");

  TEST("gui_window_dispatch: callback invalidation waits next pass");
  if (gui_window_dispatch(8) == 1 &&
      paint_calls == 1 &&
      gui_event_pending() == 0) PASS();
  else FAIL("dispatch deferred paint");

  gui_window_dispatcher_stats(&stats);
  TEST("gui_window_dispatcher_stats: tracks dispatched and handled events");
  if (stats.dispatched_total == 2 &&
      stats.handled_total == 2 &&
      stats.missing_target_total == 0 &&
      stats.missing_handler_total == 0 &&
      stats.ignored_total == 0) PASS();
  else FAIL("dispatch stats");

  shutdown_fixture();
}

static void test_dispatch_key_up_events(void) {
  struct gui_window *a;
  struct gui_window *b;
  struct gui_window_dispatcher_stats stats;

  reset_fixture();
  a = compositor_create_window("A", 10, 30, 80, 48);
  b = compositor_create_window("B", 100, 30, 80, 48);
  if (!a || !b) {
    TEST("gui_window_dispatch key up: fixture creates windows");
    FAIL("create window");
    shutdown_fixture();
    return;
  }
  a->on_key_up = on_key_up;
  b->on_key_up = on_key_up;
  b->on_paint = on_paint;
  compositor_show_window(a->id);
  compositor_show_window(b->id);
  compositor_focus_window(a->id);
  gui_event_flush();

  gui_event_push_key_up(b->id, 'B', 4, 'B', 0);
  TEST("gui_window_dispatch: key up routes explicit target");
  if (gui_window_dispatch(8) == 1 &&
      key_up_calls == 1 &&
      key_up_last_code == 'B' &&
      key_up_last_mods == 4 &&
      key_calls == 0 &&
      gui_event_pending() == 1) PASS();
  else FAIL("dispatch key up explicit");

  gui_event_flush();
  gui_event_push_key_up(0, 'A', 2, 'A', 0);
  TEST("gui_window_dispatch: key up falls back to focused target");
  if (gui_window_dispatch(8) == 1 &&
      key_up_calls == 2 &&
      key_up_last_code == 'A' &&
      key_up_last_mods == 2 &&
      gui_event_pending() == 1) PASS();
  else FAIL("dispatch key up focus");

  gui_event_flush();
  b->on_key_up = 0;
  gui_event_push_key_up(b->id, 'C', 1, 'C', 0);
  TEST("gui_window_dispatch: key up missing handler is counted safely");
  if (gui_window_dispatch(8) == 1 &&
      key_up_calls == 2 &&
      gui_event_pending() == 0) PASS();
  else FAIL("dispatch key up handler");

  compositor_hide_window(a->id);
  gui_event_flush();
  gui_event_push_key_up(a->id, 'D', 0, 'D', 0);
  TEST("gui_window_dispatch: key up hidden target is not dispatched");
  if (gui_window_dispatch(8) == 1 &&
      key_up_calls == 2 &&
      gui_event_pending() == 0) PASS();
  else FAIL("dispatch key up hidden");

  gui_window_dispatcher_stats(&stats);
  TEST("gui_window_dispatcher_stats: tracks key up categories");
  if (stats.dispatched_total == 4 &&
      stats.handled_total == 2 &&
      stats.missing_target_total == 1 &&
      stats.missing_handler_total == 1 &&
      stats.ignored_total == 0) PASS();
  else FAIL("dispatch key up stats");

  shutdown_fixture();
}

static void test_dispatch_scroll_and_paint(void) {
  struct gui_window *win;

  reset_fixture();
  win = compositor_create_window("S", 10, 30, 80, 48);
  if (!win) {
    TEST("gui_window_dispatch scroll/paint: fixture creates window");
    FAIL("create window");
    shutdown_fixture();
    return;
  }
  win->on_scroll = on_scroll;
  win->on_paint = on_paint;
  compositor_show_window(win->id);
  compositor_focus_window(win->id);
  gui_event_flush();

  gui_event_push_mouse_scroll(20, 40, -3, 0, 0);
  TEST("gui_window_dispatch: scroll without window_id targets focus");
  if (gui_window_dispatch(8) == 1 &&
      scroll_calls == 1 &&
      scroll_last_delta == -3 &&
      gui_event_pending() == 1) PASS();
  else FAIL("dispatch scroll focus");

  gui_event_flush();
  gui_event_push_paint(win->id, 0);
  TEST("gui_window_dispatch: paint routes to explicit window");
  if (gui_window_dispatch(8) == 1 &&
      paint_calls == 1 &&
      gui_event_pending() == 0) PASS();
  else FAIL("dispatch paint");

  shutdown_fixture();
}

static void test_dispatch_mouse_events(void) {
  struct gui_window *a;
  struct gui_window *b;
  struct gui_window_dispatcher_stats stats;

  reset_fixture();
  a = compositor_create_window("A", 10, 40, 80, 48);
  b = compositor_create_window("B", 100, 60, 80, 48);
  if (!a || !b) {
    TEST("gui_window_dispatch mouse: fixture creates windows");
    FAIL("create window");
    shutdown_fixture();
    return;
  }
  b->on_hover = on_hover;
  b->on_mouse = on_mouse;
  compositor_show_window(a->id);
  compositor_show_window(b->id);
  compositor_focus_window(a->id);
  gui_event_flush();

  gui_event_push_mouse_move(110, 70, 1, 2, 0, 0);
  TEST("gui_window_dispatch: mouse move routes hover with local coords");
  if (gui_window_dispatch(8) == 1 &&
      hover_calls == 1 &&
      hover_last_x == 10 &&
      hover_last_y == 10 &&
      gui_event_pending() == 0) PASS();
  else FAIL("dispatch hover");

  gui_event_push_mouse_button(1, 112, 72, 1, 0);
  TEST("gui_window_dispatch: mouse down focuses and routes on_mouse");
  if (gui_window_dispatch(8) == 1 &&
      mouse_calls == 1 &&
      mouse_last_x == 12 &&
      mouse_last_y == 12 &&
      mouse_last_buttons == 1 &&
      compositor_focused_window() == b &&
      gui_event_pending() == 3) PASS();
  else FAIL("dispatch mouse down");

  gui_event_flush();
  gui_event_push_mouse_button(0, 113, 73, 0, 0);
  TEST("gui_window_dispatch: mouse up routes on_mouse without refocus");
  if (gui_window_dispatch(8) == 1 &&
      mouse_calls == 2 &&
      mouse_last_x == 13 &&
      mouse_last_y == 13 &&
      mouse_last_buttons == 0 &&
      compositor_focused_window() == b &&
      gui_event_pending() == 1) PASS();
  else FAIL("dispatch mouse up");

  gui_event_flush();
  compositor_focus_window(a->id);
  gui_event_flush();
  gui_event_push_mouse_button(1, b->frame.x + 5, b->frame.y - 1, 1, 0);
  TEST("gui_window_dispatch: titlebar mouse down focuses without app callback");
  if (gui_window_dispatch(8) == 1 &&
      mouse_calls == 2 &&
      compositor_focused_window() == b &&
      gui_event_pending() == 2) PASS();
  else FAIL("dispatch titlebar");

  gui_event_flush();
  gui_event_push_mouse_move(310, 190, 1, 1, 0, 0);
  TEST("gui_window_dispatch: mouse event without target is counted safely");
  if (gui_window_dispatch(8) == 1 &&
      hover_calls == 1 &&
      gui_event_pending() == 0) PASS();
  else FAIL("dispatch mouse miss");

  b->on_hover = 0;
  gui_event_push_mouse_move(110, 70, 1, 1, 0, 0);
  TEST("gui_window_dispatch: mouse event without handler is counted safely");
  if (gui_window_dispatch(8) == 1 &&
      hover_calls == 1 &&
      gui_event_pending() == 0) PASS();
  else FAIL("dispatch mouse handler");

  gui_window_dispatcher_stats(&stats);
  TEST("gui_window_dispatcher_stats: tracks mouse handled/miss categories");
  if (stats.dispatched_total == 6 &&
      stats.handled_total == 3 &&
      stats.missing_target_total == 1 &&
      stats.missing_handler_total == 1 &&
      stats.ignored_total == 1) PASS();
  else FAIL("dispatch mouse stats");

  shutdown_fixture();
}

static void test_dispatch_mouse_capture_events(void) {
  struct gui_window *a;
  struct gui_window *b;
  struct gui_window_dispatcher_stats stats;

  reset_fixture();
  a = compositor_create_window("A", 20, 40, 60, 40);
  b = compositor_create_window("B", 120, 40, 60, 40);
  if (!a || !b) {
    TEST("gui_window_dispatch capture: fixture creates windows");
    FAIL("create window");
    shutdown_fixture();
    return;
  }
  a->on_mouse = on_mouse;
  a->capture_mouse = 1;
  b->on_hover = on_hover;
  compositor_show_window(a->id);
  compositor_show_window(b->id);
  gui_event_flush();

  gui_event_push_mouse_button(1, 30, 50, MOUSE_BUTTON_LEFT, 0);
  TEST("gui_window_dispatch: mouse down establishes capture");
  if (gui_window_dispatch(8) == 1 &&
      mouse_calls == 1 &&
      mouse_last_x == 10 &&
      mouse_last_y == 10 &&
      mouse_last_buttons == MOUSE_BUTTON_LEFT &&
      gui_event_pending() == 2) PASS();
  else FAIL("dispatch capture down");

  gui_event_flush();
  gui_event_push_mouse_move(130, 70, 100, 20, MOUSE_BUTTON_LEFT, 0);
  TEST("gui_window_dispatch: captured move can leave client area");
  if (gui_window_dispatch(8) == 1 &&
      mouse_calls == 2 &&
      mouse_last_x == 110 &&
      mouse_last_y == 30 &&
      mouse_last_buttons == MOUSE_BUTTON_LEFT &&
      hover_calls == 0 &&
      gui_event_pending() == 1) PASS();
  else FAIL("dispatch capture move");

  gui_event_flush();
  gui_event_push_mouse_button(0, 130, 70, 0, 0);
  TEST("gui_window_dispatch: captured mouse up releases capture");
  if (gui_window_dispatch(8) == 1 &&
      mouse_calls == 3 &&
      mouse_last_x == 110 &&
      mouse_last_y == 30 &&
      mouse_last_buttons == 0 &&
      gui_event_pending() == 1) PASS();
  else FAIL("dispatch capture up");

  gui_event_flush();
  gui_event_push_mouse_move(130, 70, 1, 1, 0, 0);
  TEST("gui_window_dispatch: hover resumes after capture release");
  if (gui_window_dispatch(8) == 1 &&
      hover_calls == 1 &&
      hover_last_x == 10 &&
      hover_last_y == 30 &&
      gui_event_pending() == 0) PASS();
  else FAIL("dispatch capture release");

  gui_window_dispatcher_stats(&stats);
  TEST("gui_window_dispatcher_stats: tracks mouse capture dispatch");
  if (stats.dispatched_total == 4 &&
      stats.handled_total == 4 &&
      stats.missing_target_total == 0 &&
      stats.missing_handler_total == 0 &&
      stats.ignored_total == 0) PASS();
  else FAIL("dispatch capture stats");

  shutdown_fixture();
}

static void test_dispatch_mouse_capture_reset_events(void) {
  struct gui_window *a;
  struct gui_window *b;
  struct gui_window_dispatcher_stats stats;

  reset_fixture();
  a = compositor_create_window("A", 20, 40, 60, 40);
  b = compositor_create_window("B", 120, 40, 60, 40);
  if (!a || !b) {
    TEST("gui_window_dispatch capture reset: fixture creates windows");
    FAIL("create window");
    shutdown_fixture();
    return;
  }
  a->on_mouse = on_mouse;
  a->capture_mouse = 1;
  b->on_hover = on_hover;
  compositor_show_window(a->id);
  compositor_show_window(b->id);
  gui_event_flush();

  gui_event_push_mouse_button(1, 30, 50, MOUSE_BUTTON_LEFT, 0);
  if (gui_window_dispatch(8) != 1 || mouse_calls != 1) {
    TEST("gui_window_dispatch capture reset: establishes fixture capture");
    FAIL("capture setup");
    shutdown_fixture();
    return;
  }
  gui_event_flush();

  gui_window_dispatcher_reset_stats();
  gui_event_push_mouse_move(130, 70, 100, 20, MOUSE_BUTTON_LEFT, 0);
  TEST("gui_window_dispatcher_reset_stats: preserves active capture");
  if (gui_window_dispatch(8) == 1 &&
      mouse_calls == 2 &&
      mouse_last_x == 110 &&
      mouse_last_y == 30 &&
      hover_calls == 0 &&
      gui_event_pending() == 1) PASS();
  else FAIL("dispatch capture stats reset");

  gui_window_dispatcher_stats(&stats);
  TEST("gui_window_dispatcher_reset_stats: resets metrics only");
  if (stats.dispatched_total == 1 &&
      stats.handled_total == 1 &&
      stats.missing_target_total == 0 &&
      stats.missing_handler_total == 0 &&
      stats.ignored_total == 0) PASS();
  else FAIL("dispatch capture reset stats");

  gui_event_flush();
  gui_window_dispatcher_reset();
  gui_event_push_mouse_move(130, 70, 1, 1, MOUSE_BUTTON_LEFT, 0);
  TEST("gui_window_dispatcher_reset: clears active capture state");
  if (gui_window_dispatch(8) == 1 &&
      mouse_calls == 2 &&
      hover_calls == 1 &&
      hover_last_x == 10 &&
      hover_last_y == 30 &&
      gui_event_pending() == 0) PASS();
  else FAIL("dispatch capture full reset");

  gui_window_dispatcher_stats(&stats);
  TEST("gui_window_dispatcher_reset: resets metrics and capture");
  if (stats.dispatched_total == 1 &&
      stats.handled_total == 1 &&
      stats.missing_target_total == 0 &&
      stats.missing_handler_total == 0 &&
      stats.ignored_total == 0) PASS();
  else FAIL("dispatch full reset stats");

  shutdown_fixture();
}

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
  a->on_mouse = on_mouse;
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
  a->on_focus = on_focus;
  a->on_blur = on_blur;
  b->on_focus = on_focus;
  b->on_blur = on_blur;
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
  win->on_close = on_close;
  win->on_resize = on_resize;
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
  b->on_mouse = on_mouse;
  b->on_context_menu = on_context_menu;
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
  b->on_context_menu = on_context_menu;
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
  win->on_timer = on_timer;
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

  win->on_timer = on_timer;
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

  win->on_key = on_key;
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

int test_gui_window_dispatcher_run(void) {
  printf("[test_gui_window_dispatcher]\n");
  tests_run = 0;
  tests_passed = 0;
  test_dispatch_noop_and_key();
  test_dispatch_key_up_events();
  test_dispatch_scroll_and_paint();
  test_dispatch_mouse_events();
  test_dispatch_mouse_capture_events();
  test_dispatch_mouse_capture_reset_events();
  test_dispatcher_snapshot_events();
  test_dispatch_lifecycle_events();
  test_dispatch_compositor_owned_lifecycle_events();
  test_dispatch_context_menu_events();
  test_dispatch_timer_events();
  test_dispatch_miss_and_ignore();
  printf("  -> %d/%d passed\n", tests_passed, tests_run);
  return tests_run - tests_passed;
}
