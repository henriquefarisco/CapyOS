/*
 * tests/test_gui_window_dispatcher.c
 *
 * Fixture (callback counters, callback set, reset/shutdown helpers),
 * run-counter globals, entry point and the first six dispatcher test
 * cases: noop+key, key-up, scroll+paint, mouse, mouse capture and
 * mouse capture reset. The companion file
 * `tests/test_gui_window_dispatcher_lifecycle.c` owns
 * snapshot/health, focus+blur lifecycle, compositor-owned lifecycle,
 * context-menu, timer and miss/ignore coverage. Shared
 * TEST/PASS/FAIL macros, counter externs, callback declarations and
 * fixture-helper declarations come from
 * `tests/test_gui_window_dispatcher_internal.h`.
 *
 * Carved out of the historical single-file
 * `tests/test_gui_window_dispatcher.c` (985 LOC) at the 2026-05-15
 * monolith refactor so each host-test translation unit stays under
 * the 900-line layout limit.
 */
#include "test_gui_window_dispatcher_internal.h"

int test_gui_window_dispatcher_runs = 0;
int test_gui_window_dispatcher_passes = 0;

uint32_t test_gwd_key_calls = 0;
uint32_t test_gwd_key_last_code = 0;
uint8_t test_gwd_key_last_mods = 0;
uint32_t test_gwd_key_up_calls = 0;
uint32_t test_gwd_key_up_last_code = 0;
uint8_t test_gwd_key_up_last_mods = 0;
uint32_t test_gwd_focus_calls = 0;
uint32_t test_gwd_focus_last_id = 0;
uint32_t test_gwd_blur_calls = 0;
uint32_t test_gwd_blur_last_id = 0;
uint32_t test_gwd_scroll_calls = 0;
int32_t test_gwd_scroll_last_delta = 0;
uint32_t test_gwd_paint_calls = 0;
uint32_t test_gwd_hover_calls = 0;
int32_t test_gwd_hover_last_x = 0;
int32_t test_gwd_hover_last_y = 0;
uint32_t test_gwd_mouse_calls = 0;
int32_t test_gwd_mouse_last_x = 0;
int32_t test_gwd_mouse_last_y = 0;
uint8_t test_gwd_mouse_last_buttons = 0;
uint32_t test_gwd_context_calls = 0;
int32_t test_gwd_context_last_x = 0;
int32_t test_gwd_context_last_y = 0;
uint32_t test_gwd_timer_calls = 0;
uint32_t test_gwd_timer_last_id = 0;
uint32_t test_gwd_close_calls = 0;
uint32_t test_gwd_resize_calls = 0;

void reset_counters(void) {
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

void reset_fixture(void) {
  static uint32_t framebuffer[320 * 200];
  gui_event_init();
  gui_window_dispatcher_reset();
  reset_counters();
  compositor_init(framebuffer, 320, 200, 320 * 4);
}

void shutdown_fixture(void) {
  compositor_shutdown();
  gui_event_init();
  gui_window_dispatcher_reset();
  reset_counters();
}

void test_gwd_on_key(struct gui_window *win, uint32_t keycode, uint8_t mods) {
  (void)win;
  key_calls++;
  key_last_code = keycode;
  key_last_mods = mods;
}

void test_gwd_on_key_up(struct gui_window *win, uint32_t keycode, uint8_t mods) {
  (void)win;
  key_up_calls++;
  key_up_last_code = keycode;
  key_up_last_mods = mods;
}

void test_gwd_on_focus(struct gui_window *win) {
  focus_calls++;
  focus_last_id = win ? win->id : 0;
}

void test_gwd_on_blur(struct gui_window *win) {
  blur_calls++;
  blur_last_id = win ? win->id : 0;
}

void test_gwd_on_scroll(struct gui_window *win, int32_t delta) {
  (void)win;
  scroll_calls++;
  scroll_last_delta = delta;
}

void test_gwd_on_paint(struct gui_window *win) {
  (void)win;
  paint_calls++;
}

void test_gwd_on_hover(struct gui_window *win, int32_t x, int32_t y) {
  (void)win;
  hover_calls++;
  hover_last_x = x;
  hover_last_y = y;
}

void test_gwd_on_mouse(struct gui_window *win, int32_t x, int32_t y,
                       uint8_t buttons) {
  (void)win;
  mouse_calls++;
  mouse_last_x = x;
  mouse_last_y = y;
  mouse_last_buttons = buttons;
}

void test_gwd_on_context_menu(struct gui_window *win, int32_t x, int32_t y) {
  (void)win;
  context_calls++;
  context_last_x = x;
  context_last_y = y;
}

void test_gwd_on_timer(struct gui_window *win, uint32_t timer_id) {
  (void)win;
  timer_calls++;
  timer_last_id = timer_id;
}

void test_gwd_on_close(struct gui_window *win) {
  (void)win;
  close_calls++;
}

void test_gwd_on_resize(struct gui_window *win, uint32_t w, uint32_t h) {
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
  win->on_key = test_gwd_on_key;
  win->on_key_up = test_gwd_on_key_up;
  win->on_paint = test_gwd_on_paint;
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
  a->on_key_up = test_gwd_on_key_up;
  b->on_key_up = test_gwd_on_key_up;
  b->on_paint = test_gwd_on_paint;
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
  win->on_scroll = test_gwd_on_scroll;
  win->on_paint = test_gwd_on_paint;
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
  b->on_hover = test_gwd_on_hover;
  b->on_mouse = test_gwd_on_mouse;
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
  a->on_mouse = test_gwd_on_mouse;
  a->capture_mouse = 1;
  b->on_hover = test_gwd_on_hover;
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
  a->on_mouse = test_gwd_on_mouse;
  a->capture_mouse = 1;
  b->on_hover = test_gwd_on_hover;
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
  test_gui_window_dispatcher_lifecycle_cases();
  printf("  -> %d/%d passed\n", tests_passed, tests_run);
  return tests_run - tests_passed;
}
