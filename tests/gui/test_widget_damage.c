#include <stdio.h>

#include "gui/compositor.h"
#include "gui/core/internal/compositor_internal.h"
#include "gui/event.h"
#include "gui/widget.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { tests_run++; printf("  %-58s ", name); } while (0)
#define PASS() do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while (0)

static void reset_fixture(void) {
  static uint32_t framebuffer[96 * 64];
  for (uint32_t i = 0u; i < 96u * 64u; ++i) framebuffer[i] = 0u;
  gui_event_init();
  widget_system_init();
  compositor_init(framebuffer, 96u, 64u, 96u * 4u);
}

static void shutdown_fixture(void) {
  compositor_shutdown();
  gui_event_init();
}

static void test_hover_invalidates_widget_bounds(void) {
  struct gui_window *win;
  struct widget *w;
  struct gui_event ev = { 0 };
  struct gui_event out;
  reset_fixture();
  win = compositor_create_window("W", 8, 12, 40u, 30u);
  w = widget_create(WIDGET_BUTTON, win);
  if (!win || !w) {
    TEST("widget hover damage: fixture creates window/widget");
    FAIL("fixture");
    widget_destroy(w);
    shutdown_fixture();
    return;
  }
  widget_set_bounds(w, 3, 4, 10u, 6u);
  compositor_render();
  gui_event_flush();
  ev.type = GUI_EVENT_MOUSE_MOVE;
  ev.mouse.x = 4;
  ev.mouse.y = 5;
  (void)widget_handle_event(w, &ev);
  TEST("widget hover damage invalidates widget bounds only");
  if (w->hovered == 1 &&
      gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_PAINT &&
      out.window_id == win->id &&
      comp_dirty_rect_count == 1u &&
      comp_dirty_rects[0].x == 11 &&
      comp_dirty_rects[0].y == 16 &&
      comp_dirty_rects[0].width == 10u &&
      comp_dirty_rects[0].height == 6u) PASS();
  else FAIL("hover damage");
  widget_destroy(w);
  shutdown_fixture();
}

static void test_checkbox_click_invalidates_widget_bounds(void) {
  struct gui_window *win;
  struct widget *w;
  struct gui_event ev = { 0 };
  struct gui_event out;
  reset_fixture();
  win = compositor_create_window("C", 20, 10, 40u, 30u);
  w = widget_create(WIDGET_CHECKBOX, win);
  if (!win || !w) {
    TEST("widget checkbox damage: fixture creates window/widget");
    FAIL("fixture");
    widget_destroy(w);
    shutdown_fixture();
    return;
  }
  widget_set_bounds(w, 5, 6, 18u, 12u);
  compositor_render();
  gui_event_flush();
  ev.type = GUI_EVENT_MOUSE_DOWN;
  ev.mouse.x = 7;
  ev.mouse.y = 8;
  ev.mouse.buttons = 1u;
  TEST("widget checkbox damage invalidates widget bounds only");
  if (widget_handle_event(w, &ev) == 1 &&
      w->checked == 1 &&
      gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_PAINT &&
      out.window_id == win->id &&
      comp_dirty_rect_count == 1u &&
      comp_dirty_rects[0].x == 25 &&
      comp_dirty_rects[0].y == 16 &&
      comp_dirty_rects[0].width == 18u &&
      comp_dirty_rects[0].height == 12u) PASS();
  else FAIL("checkbox damage");
  widget_destroy(w);
  shutdown_fixture();
}

static void test_set_text_invalidates_widget_bounds(void) {
  struct gui_window *win;
  struct widget *w;
  struct gui_event out;
  reset_fixture();
  win = compositor_create_window("T", 8, 12, 40u, 30u);
  w = widget_create(WIDGET_LABEL, win);
  if (!win || !w) {
    TEST("widget text damage: fixture creates window/widget");
    FAIL("fixture");
    widget_destroy(w);
    shutdown_fixture();
    return;
  }
  widget_set_bounds(w, 3, 4, 12u, 7u);
  compositor_render();
  gui_event_flush();
  widget_set_text(w, "hello");
  TEST("widget_set_text invalidates widget bounds only");
  if (gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_PAINT &&
      out.window_id == win->id &&
      comp_dirty_rect_count == 1u &&
      comp_dirty_rects[0].x == 11 &&
      comp_dirty_rects[0].y == 16 &&
      comp_dirty_rects[0].width == 12u &&
      comp_dirty_rects[0].height == 7u) PASS();
  else FAIL("set text damage");
  gui_event_flush();
  comp_dirty_rect_count = 0u;
  widget_set_text(w, "hello");
  TEST("widget_set_text unchanged text does not invalidate");
  if (gui_event_pending() == 0 && comp_dirty_rect_count == 0u) PASS();
  else FAIL("set text unchanged");
  widget_destroy(w);
  shutdown_fixture();
}

static void test_set_bounds_invalidates_old_and_new_bounds(void) {
  struct gui_window *win;
  struct widget *w;
  struct gui_event out;
  reset_fixture();
  win = compositor_create_window("B", 8, 12, 50u, 40u);
  w = widget_create(WIDGET_LABEL, win);
  if (!win || !w) {
    TEST("widget bounds damage: fixture creates window/widget");
    FAIL("fixture");
    widget_destroy(w);
    shutdown_fixture();
    return;
  }
  widget_set_bounds(w, 3, 4, 10u, 6u);
  compositor_render();
  gui_event_flush();
  widget_set_bounds(w, 8, 9, 12u, 7u);
  TEST("widget_set_bounds coalesces overlapping old and new bounds");
  if (gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_PAINT &&
      out.window_id == win->id &&
      comp_dirty_rect_count == 1u &&
      comp_dirty_rects[0].x == 11 &&
      comp_dirty_rects[0].y == 16 &&
      comp_dirty_rects[0].width == 17u &&
      comp_dirty_rects[0].height == 12u) PASS();
  else FAIL("set bounds damage");
  widget_destroy(w);
  shutdown_fixture();
}

static void test_state_mutators_invalidate_widget_bounds(void) {
  struct gui_window *win;
  struct widget *w;
  struct gui_event out;
  struct widget_style style;
  reset_fixture();
  win = compositor_create_window("S", 20, 10, 40u, 30u);
  w = widget_create(WIDGET_BUTTON, win);
  if (!win || !w) {
    TEST("widget state damage: fixture creates window/widget");
    FAIL("fixture");
    widget_destroy(w);
    shutdown_fixture();
    return;
  }
  widget_set_bounds(w, 5, 6, 18u, 12u);
  compositor_render();
  gui_event_flush();
  widget_set_enabled(w, 0);
  TEST("widget_set_enabled invalidates widget bounds only");
  if (gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_PAINT &&
      out.window_id == win->id &&
      comp_dirty_rect_count == 1u &&
      comp_dirty_rects[0].x == 25 &&
      comp_dirty_rects[0].y == 16 &&
      comp_dirty_rects[0].width == 18u &&
      comp_dirty_rects[0].height == 12u) PASS();
  else FAIL("set enabled damage");
  compositor_render();
  gui_event_flush();
  style = w->style;
  style.bg_color ^= 0x00010101u;
  widget_set_style(w, &style);
  TEST("widget_set_style invalidates widget bounds only");
  if (gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_PAINT &&
      out.window_id == win->id &&
      comp_dirty_rect_count == 1u &&
      comp_dirty_rects[0].x == 25 &&
      comp_dirty_rects[0].y == 16 &&
      comp_dirty_rects[0].width == 18u &&
      comp_dirty_rects[0].height == 12u) PASS();
  else FAIL("set style damage");
  widget_destroy(w);
  shutdown_fixture();
}

int test_widget_damage_run(void) {
  tests_run = 0;
  tests_passed = 0;
  printf("\n[gui] Widget damage tests\n");
  test_hover_invalidates_widget_bounds();
  test_checkbox_click_invalidates_widget_bounds();
  test_set_text_invalidates_widget_bounds();
  test_set_bounds_invalidates_old_and_new_bounds();
  test_state_mutators_invalidate_widget_bounds();
  printf("[gui] Widget damage: %d/%d passed\n", tests_passed, tests_run);
  return tests_passed == tests_run ? 0 : 1;
}
