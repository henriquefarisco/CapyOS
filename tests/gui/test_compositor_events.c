#include <stdio.h>

#include "gui/compositor.h"
#include "gui/event.h"

static int tests_run = 0;
static int tests_passed = 0;
static uint32_t resize_calls = 0;
static uint32_t resize_w = 0;
static uint32_t resize_h = 0;

#define TEST(name) \
  do { tests_run++; printf("  %-58s ", name); } while (0)
#define PASS() \
  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(msg) \
  do { printf("FAIL: %s\n", msg); } while (0)

static void on_resize(struct gui_window *win, uint32_t w, uint32_t h) {
  (void)win;
  resize_calls++;
  resize_w = w;
  resize_h = h;
}

static void reset_fixture(void) {
  static uint32_t framebuffer[320 * 200];
  gui_event_init();
  resize_calls = 0;
  resize_w = 0;
  resize_h = 0;
  compositor_init(framebuffer, 320, 200, 320 * 4);
}

static void shutdown_fixture(void) {
  compositor_shutdown();
  gui_event_init();
}

static void test_focus_events(void) {
  struct gui_window *a;
  struct gui_window *b;
  struct gui_event out[3];

  reset_fixture();
  a = compositor_create_window("A", 10, 30, 64, 48);
  b = compositor_create_window("B", 20, 40, 64, 48);
  if (!a || !b) {
    TEST("compositor focus events: fixture creates windows");
    FAIL("create window");
    shutdown_fixture();
    return;
  }
  compositor_show_window(a->id);
  compositor_show_window(b->id);

  compositor_focus_window(a->id);
  TEST("compositor_focus_window: publishes focus event");
  if (gui_event_poll(&out[0]) == 0 &&
      out[0].type == GUI_EVENT_WINDOW_FOCUS &&
      out[0].window_id == a->id &&
      gui_event_pending() == 0) PASS();
  else FAIL("focus event");

  compositor_focus_window(a->id);
  TEST("compositor_focus_window: refocus does not duplicate event");
  if (gui_event_pending() == 0) PASS();
  else FAIL("duplicate focus");

  compositor_focus_window(b->id);
  TEST("compositor_focus_window: publishes blur then focus transition");
  if (gui_event_poll_many(out, 3) == 2 &&
      out[0].type == GUI_EVENT_WINDOW_BLUR &&
      out[0].window_id == a->id &&
      out[1].type == GUI_EVENT_WINDOW_FOCUS &&
      out[1].window_id == b->id) PASS();
  else FAIL("focus transition");

  shutdown_fixture();
}

static void test_focus_visibility_guards(void) {
  struct gui_window *a;
  struct gui_window *b;
  struct gui_event out[3];

  reset_fixture();
  a = compositor_create_window("A", 10, 30, 64, 48);
  b = compositor_create_window("B", 20, 40, 64, 48);
  if (!a || !b) {
    TEST("compositor focus visibility guards: fixture creates windows");
    FAIL("create window");
    shutdown_fixture();
    return;
  }
  compositor_show_window(a->id);
  compositor_show_window(b->id);

  compositor_focus_window(a->id);
  gui_event_flush();
  compositor_hide_window(b->id);
  compositor_focus_window(b->id);
  TEST("compositor_focus_window: hidden window cannot steal focus");
  if (gui_event_pending() == 0 &&
      a->focused &&
      !b->focused &&
      compositor_focused_window() == a) PASS();
  else FAIL("focus hidden");

  compositor_show_window(b->id);
  compositor_minimize_window(b->id);
  gui_event_flush();
  compositor_focus_window(b->id);
  TEST("compositor_focus_window: minimized window cannot steal focus");
  if (gui_event_pending() == 0 &&
      a->focused &&
      !b->focused &&
      b->minimized &&
      !b->visible &&
      compositor_focused_window() == a) PASS();
  else FAIL("focus minimized");

  compositor_show_window(b->id);
  compositor_focus_window(b->id);
  TEST("compositor_focus_window: restored window can receive focus");
  if (gui_event_poll_many(out, 3) == 2 &&
      out[0].type == GUI_EVENT_WINDOW_BLUR &&
      out[0].window_id == a->id &&
      out[1].type == GUI_EVENT_WINDOW_FOCUS &&
      out[1].window_id == b->id &&
      b->focused &&
      compositor_focused_window() == b) PASS();
  else FAIL("focus restored");

  shutdown_fixture();
}

static void test_resize_and_paint_events(void) {
  struct gui_window *win;
  struct gui_event out;

  reset_fixture();
  win = compositor_create_window("R", 10, 30, 64, 48);
  if (!win) {
    TEST("compositor resize/paint events: fixture creates window");
    FAIL("create window");
    shutdown_fixture();
    return;
  }
  win->on_resize = on_resize;

  compositor_resize_window(win->id, 96, 72);
  TEST("compositor_resize_window: publishes resize event and callback");
  if (gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_WINDOW_RESIZE &&
      out.window_id == win->id &&
      out.resize.width == 96 &&
      out.resize.height == 72 &&
      resize_calls == 1 && resize_w == 96 && resize_h == 72) PASS();
  else FAIL("resize event");

  compositor_resize_window(win->id, 96, 72);
  TEST("compositor_resize_window: unchanged size does not publish");
  if (gui_event_pending() == 0) PASS();
  else FAIL("resize duplicate");

  compositor_invalidate(win->id);
  TEST("compositor_invalidate: publishes paint event");
  if (gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_PAINT &&
      out.window_id == win->id) PASS();
  else FAIL("paint event");

  compositor_invalidate(win->id);
  compositor_invalidate(win->id);
  TEST("compositor_invalidate: coalesces duplicate paint event");
  if (gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_PAINT &&
      out.window_id == win->id &&
      gui_event_pending() == 0) PASS();
  else FAIL("paint coalesce");

  compositor_invalidate(9999);
  TEST("compositor_invalidate: missing window does not publish");
  if (gui_event_pending() == 0) PASS();
  else FAIL("paint missing");

  shutdown_fixture();
}

static void test_visibility_blur_events(void) {
  struct gui_window *a;
  struct gui_window *b;
  struct gui_event out;

  reset_fixture();
  a = compositor_create_window("A", 10, 30, 64, 48);
  b = compositor_create_window("B", 20, 40, 64, 48);
  if (!a || !b) {
    TEST("compositor visibility events: fixture creates windows");
    FAIL("create window");
    shutdown_fixture();
    return;
  }

  compositor_show_window(a->id);
  compositor_focus_window(a->id);
  gui_event_flush();
  compositor_hide_window(a->id);
  TEST("compositor_hide_window: focused window publishes blur");
  if (gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_WINDOW_BLUR &&
      out.window_id == a->id &&
      !a->focused &&
      compositor_focused_window() == 0) PASS();
  else FAIL("hide blur");

  compositor_hide_window(a->id);
  TEST("compositor_hide_window: hidden window does not duplicate blur");
  if (gui_event_pending() == 0) PASS();
  else FAIL("hide duplicate");

  compositor_show_window(b->id);
  compositor_minimize_window(b->id);
  TEST("compositor_minimize_window: non-focused window does not blur");
  if (gui_event_pending() == 0 &&
      b->minimized &&
      !b->visible) PASS();
  else FAIL("minimize nonfocused");

  compositor_show_window(b->id);
  compositor_focus_window(b->id);
  gui_event_flush();
  compositor_minimize_window(b->id);
  TEST("compositor_minimize_window: focused window publishes blur");
  if (gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_WINDOW_BLUR &&
      out.window_id == b->id &&
      !b->focused &&
      !b->visible &&
      b->minimized &&
      compositor_focused_window() == 0) PASS();
  else FAIL("minimize blur");

  shutdown_fixture();
}

static void test_destroy_events(void) {
  struct gui_window *a;
  struct gui_window *b;
  struct gui_event out[3];
  uint32_t aid;
  uint32_t bid;

  reset_fixture();
  a = compositor_create_window("A", 10, 30, 64, 48);
  b = compositor_create_window("B", 20, 40, 64, 48);
  if (!a || !b) {
    TEST("compositor destroy events: fixture creates windows");
    FAIL("create window");
    shutdown_fixture();
    return;
  }
  compositor_show_window(a->id);
  compositor_show_window(b->id);

  aid = a->id;
  bid = b->id;
  compositor_focus_window(a->id);
  gui_event_flush();
  compositor_invalidate(aid);
  compositor_destroy_window(aid);
  TEST("compositor_destroy_window: focused window publishes blur then close");
  if (gui_event_poll_many(out, 3) == 2 &&
      out[0].type == GUI_EVENT_WINDOW_BLUR &&
      out[0].window_id == aid &&
      out[1].type == GUI_EVENT_WINDOW_CLOSE &&
      out[1].window_id == aid &&
      !compositor_window_exists(aid)) PASS();
  else FAIL("focused destroy");

  gui_event_flush();
  compositor_invalidate(bid);
  compositor_destroy_window(bid);
  TEST("compositor_destroy_window: non-focused window publishes close only");
  if (gui_event_poll_many(out, 3) == 1 &&
      out[0].type == GUI_EVENT_WINDOW_CLOSE &&
      out[0].window_id == bid &&
      !compositor_window_exists(bid)) PASS();
  else FAIL("nonfocused destroy");

  shutdown_fixture();
}

int test_compositor_events_run(void) {
  printf("[test_compositor_events]\n");
  tests_run = 0;
  tests_passed = 0;
  test_focus_events();
  test_focus_visibility_guards();
  test_resize_and_paint_events();
  test_visibility_blur_events();
  test_destroy_events();
  printf("  -> %d/%d passed\n", tests_passed, tests_run);
  return tests_run - tests_passed;
}
