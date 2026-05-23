#include <stdio.h>

#include "gui/compositor.h"
#include "gui/core/internal/compositor_internal.h"
#include "gui/event.h"

static int tests_run = 0;
static int tests_passed = 0;
static uint32_t resize_calls = 0;
static uint32_t resize_w = 0;
static uint32_t resize_h = 0;
static uint32_t paint_calls = 0;
static uint32_t framebuffer[320 * 200];

struct paint_probe {
  uint32_t calls;
  uint32_t color;
};

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

static void on_paint_count(struct gui_window *win) {
  paint_calls++;
  if (win && win->surface.pixels) {
    win->surface.pixels[0] = 0x0000CC33u + paint_calls;
  }
}

static void on_paint_probe(struct gui_window *win) {
  struct paint_probe *probe = win ? (struct paint_probe *)win->user_data : 0;
  if (!probe) return;
  probe->calls++;
  if (win->surface.pixels) win->surface.pixels[0] = probe->color + probe->calls;
}

static void reset_fixture(void) {
  for (uint32_t i = 0; i < 320u * 200u; ++i) framebuffer[i] = 0u;
  gui_event_init();
  resize_calls = 0;
  resize_w = 0;
  resize_h = 0;
  paint_calls = 0;
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

  compositor_render();
  gui_event_flush();
  compositor_invalidate(win->id);
  TEST("compositor_invalidate: publishes paint event and client damage");
  if (gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_PAINT &&
      out.window_id == win->id &&
      comp_dirty_rect_count == 1u &&
      comp_dirty_rects[0].x == 10 &&
      comp_dirty_rects[0].y == 30 &&
      comp_dirty_rects[0].width == 96u &&
      comp_dirty_rects[0].height == 72u &&
      comp_full_redraw_pending == 0) PASS();
  else FAIL("paint event");

  compositor_render();
  gui_event_flush();
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

static void test_invalidate_rect_tracks_damage(void) {
  struct gui_window *win;
  struct gui_event out;
  struct gui_rect rect = { 2, 3, 5u, 4u };
  reset_fixture();
  win = compositor_create_window("D", 10, 30, 64, 48);
  if (!win) {
    TEST("compositor_invalidate_rect: fixture creates window");
    FAIL("create window");
    shutdown_fixture();
    return;
  }
  compositor_render();
  gui_event_flush();
  compositor_invalidate_rect(win->id, &rect);
  TEST("compositor_invalidate_rect: stores clipped screen damage");
  if (gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_PAINT &&
      out.window_id == win->id &&
      comp_dirty_rect_count == 1u &&
      comp_dirty_rects[0].x == 12 &&
      comp_dirty_rects[0].y == 33 &&
      comp_dirty_rects[0].width == 5u &&
      comp_dirty_rects[0].height == 4u &&
      comp_full_redraw_pending == 0) PASS();
  else FAIL("dirty rect");
  shutdown_fixture();
}

static void test_invalidate_rect_ignores_empty_and_outside_damage(void) {
  struct gui_window *win;
  struct gui_rect empty = { 2, 3, 0u, 4u };
  struct gui_rect outside = { 80, 90, 5u, 4u };
  reset_fixture();
  win = compositor_create_window("E", 10, 30, 64, 48);
  if (!win) {
    TEST("compositor_invalidate_rect invalid: fixture creates window");
    FAIL("create window");
    shutdown_fixture();
    return;
  }
  compositor_render();
  gui_event_flush();
  compositor_invalidate_rect(win->id, &empty);
  compositor_invalidate_rect(win->id, &outside);
  TEST("compositor_invalidate_rect: empty/outside rects are no-op");
  if (gui_event_pending() == 0 &&
      comp_dirty_rect_count == 0u &&
      comp_full_redraw_pending == 0) PASS();
  else FAIL("empty/outside dirty rect");
  shutdown_fixture();
}

static void test_invalidate_rect_null_falls_back_to_window_damage(void) {
  struct gui_window *win;
  struct gui_event out;
  reset_fixture();
  win = compositor_create_window("N", 10, 30, 64, 48);
  if (!win) {
    TEST("compositor_invalidate_rect NULL: fixture creates window");
    FAIL("create window");
    shutdown_fixture();
    return;
  }
  compositor_render();
  gui_event_flush();
  compositor_invalidate_rect(win->id, 0);
  TEST("compositor_invalidate_rect: NULL means full client damage");
  if (gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_PAINT &&
      out.window_id == win->id &&
      comp_dirty_rect_count == 1u &&
      comp_dirty_rects[0].x == 10 &&
      comp_dirty_rects[0].y == 30 &&
      comp_dirty_rects[0].width == 64u &&
      comp_dirty_rects[0].height == 48u &&
      comp_full_redraw_pending == 0) PASS();
  else FAIL("NULL dirty rect");
  shutdown_fixture();
}

static void test_invalidate_rect_coalesces_touching_damage(void) {
  struct gui_window *win;
  struct gui_event out;
  struct gui_rect a = { 2, 3, 5u, 4u };
  struct gui_rect b = { 7, 3, 4u, 4u };
  struct gui_rect c = { 20, 3, 2u, 2u };
  reset_fixture();
  win = compositor_create_window("M", 10, 30, 64, 48);
  if (!win) {
    TEST("compositor dirty coalesce: fixture creates window");
    FAIL("create window");
    shutdown_fixture();
    return;
  }
  compositor_render();
  gui_event_flush();
  compositor_invalidate_rect(win->id, &a);
  compositor_invalidate_rect(win->id, &b);
  compositor_invalidate_rect(win->id, &c);
  TEST("compositor_invalidate_rect: coalesces touching damage");
  if (gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_PAINT &&
      out.window_id == win->id &&
      gui_event_pending() == 0 &&
      comp_dirty_rect_count == 2u &&
      comp_dirty_rects[0].x == 12 &&
      comp_dirty_rects[0].y == 33 &&
      comp_dirty_rects[0].width == 9u &&
      comp_dirty_rects[0].height == 4u &&
      comp_dirty_rects[1].x == 30 &&
      comp_dirty_rects[1].y == 33 &&
      comp_dirty_rects[1].width == 2u &&
      comp_dirty_rects[1].height == 2u) PASS();
  else FAIL("dirty coalesce");
  shutdown_fixture();
}

static void test_invalidate_rect_overflow_falls_back_to_full_redraw(void) {
  struct gui_window *win;
  struct gui_event out;
  reset_fixture();
  win = compositor_create_window("O", 10, 30, 96, 48);
  if (!win) {
    TEST("compositor dirty overflow: fixture creates window");
    FAIL("create window");
    shutdown_fixture();
    return;
  }
  compositor_render();
  gui_event_flush();
  for (uint32_t i = 0u; i <= COMP_DIRTY_RECT_MAX; ++i) {
    struct gui_rect rect = { (int32_t)(i * 2u), 0, 1u, 1u };
    compositor_invalidate_rect(win->id, &rect);
  }
  TEST("compositor_invalidate_rect: overflow falls back to full redraw");
  if (gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_PAINT &&
      out.window_id == win->id &&
      gui_event_pending() == 0 &&
      comp_dirty_rect_count == 0u &&
      comp_full_redraw_pending == 1) PASS();
  else FAIL("dirty overflow");
  shutdown_fixture();
}

static void test_partial_present_copies_only_dirty_rect(void) {
  struct gui_window *win;
  struct gui_rect rect = { 0, 0, 1u, 1u };
  reset_fixture();
  win = compositor_create_window("P", 10, 30, 16, 16);
  if (!win) {
    TEST("compositor_render: fixture creates partial-present window");
    FAIL("create window");
    shutdown_fixture();
    return;
  }
  win->decorated = 0;
  win->corner_radius = 0;
  compositor_show_window(win->id);
  compositor_render();
  win->surface.pixels[0] = 0x0000AA11u;
  win->surface.pixels[1] = 0x0000BB22u;
  compositor_invalidate_rect(win->id, &rect);
  compositor_render();
  TEST("compositor_render: partial present copies only dirty rect");
  if (framebuffer[30u * 320u + 10u] == 0x0000AA11u &&
      framebuffer[30u * 320u + 11u] != 0x0000BB22u &&
      comp_dirty_rect_count == 0u &&
      comp_full_redraw_pending == 0) PASS();
  else FAIL("partial present");
  shutdown_fixture();
}

static void test_partial_compose_paints_window_once(void) {
  struct gui_window *win;
  struct gui_rect a = { 0, 0, 1u, 1u };
  struct gui_rect b = { 8, 8, 1u, 1u };
  reset_fixture();
  win = compositor_create_window("Q", 10, 30, 16, 16);
  if (!win) {
    TEST("compositor_render: fixture creates partial-compose window");
    FAIL("create window");
    shutdown_fixture();
    return;
  }
  win->decorated = 0;
  win->corner_radius = 0;
  win->on_paint = on_paint_count;
  compositor_show_window(win->id);
  compositor_render();
  paint_calls = 0;
  gui_event_flush();
  compositor_invalidate_rect(win->id, &a);
  compositor_invalidate_rect(win->id, &b);
  compositor_render();
  TEST("compositor_render: partial compose paints window once");
  if (paint_calls == 1u &&
      framebuffer[30u * 320u + 10u] == 0x0000CC34u &&
      comp_dirty_rect_count == 0u &&
      comp_full_redraw_pending == 0) PASS();
  else FAIL("partial compose paint count");
  shutdown_fixture();
}

static void test_partial_compose_paints_intersecting_windows_only(void) {
  struct gui_window *bottom;
  struct gui_window *top;
  struct gui_window *far;
  struct paint_probe bottom_probe = { 0u, 0x0000AA00u };
  struct paint_probe top_probe = { 0u, 0x0000BB00u };
  struct paint_probe far_probe = { 0u, 0x0000DD00u };
  struct gui_rect dirty = { 0, 0, 1u, 1u };
  reset_fixture();
  bottom = compositor_create_window("Bottom", 10, 30, 16, 16);
  top = compositor_create_window("Top", 10, 30, 16, 16);
  far = compositor_create_window("Far", 80, 30, 16, 16);
  if (!bottom || !top || !far) {
    TEST("compositor partial compose: fixture creates windows");
    FAIL("create window");
    shutdown_fixture();
    return;
  }
  bottom->decorated = 0;
  top->decorated = 0;
  far->decorated = 0;
  bottom->corner_radius = 0;
  top->corner_radius = 0;
  far->corner_radius = 0;
  bottom->user_data = &bottom_probe;
  top->user_data = &top_probe;
  far->user_data = &far_probe;
  bottom->on_paint = on_paint_probe;
  top->on_paint = on_paint_probe;
  far->on_paint = on_paint_probe;
  compositor_show_window(bottom->id);
  compositor_show_window(top->id);
  compositor_show_window(far->id);
  compositor_render();
  bottom_probe.calls = 0u;
  top_probe.calls = 0u;
  far_probe.calls = 0u;
  gui_event_flush();
  compositor_invalidate_rect(bottom->id, &dirty);
  compositor_render();
  TEST("compositor_render: partial compose paints intersecting windows only");
  if (bottom_probe.calls == 1u &&
      top_probe.calls == 1u &&
      far_probe.calls == 0u &&
      framebuffer[30u * 320u + 10u] == 0x0000BB01u &&
      comp_dirty_rect_count == 0u &&
      comp_full_redraw_pending == 0) PASS();
  else FAIL("partial compose intersection");
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
  test_invalidate_rect_tracks_damage();
  test_invalidate_rect_ignores_empty_and_outside_damage();
  test_invalidate_rect_null_falls_back_to_window_damage();
  test_invalidate_rect_coalesces_touching_damage();
  test_invalidate_rect_overflow_falls_back_to_full_redraw();
  test_partial_present_copies_only_dirty_rect();
  test_partial_compose_paints_window_once();
  test_partial_compose_paints_intersecting_windows_only();
  test_visibility_blur_events();
  test_destroy_events();
  printf("  -> %d/%d passed\n", tests_passed, tests_run);
  return tests_run - tests_passed;
}
