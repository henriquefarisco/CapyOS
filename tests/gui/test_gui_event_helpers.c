/*
 * tests/test_gui_event_helpers.c
 *
 * Push-helper, coalescing, discard, snapshot, reset and overflow
 * coverage for the GUI event API. Carved out of
 * `tests/test_gui_event.c` at the 2026-05-15 monolith refactor so
 * each host-test translation unit stays under the 900-line layout
 * limit. The entry point (`test_gui_event_run`), the queue/FIFO/poll/
 * peek/dispatch/ready/backpressure coverage and the shared
 * counter globals live in `tests/test_gui_event.c`.
 *
 * Shared TEST/PASS/FAIL macros, counter externs, the `make_event`
 * helper and this file's entry forward declaration come from
 * `tests/test_gui_event_internal.h`.
 */
#include "test_gui_event_internal.h"

#define make_event test_gui_event_make_event

static void test_key_helper(void) {
  gui_event_init();

  TEST("gui_event_push_key: enqueues key-down payload");
  if (gui_event_push_key(7, 65, 3, 'A', 99) == 0 &&
      gui_event_pending() == 1) PASS();
  else FAIL("push key");

  struct gui_event out;
  TEST("gui_event_push_key: preserves window/key metadata");
  if (gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_KEY_DOWN &&
      out.window_id == 7 &&
      out.key.keycode == 65 &&
      out.key.modifiers == 3 &&
      out.key.ch == 'A' &&
      out.timestamp == 99) PASS();
  else FAIL("key metadata");

  TEST("gui_event_push_key_up: enqueues key-up payload");
  if (gui_event_push_key_up(8, 66, 1, 'B', 100) == 0 &&
      gui_event_pending() == 1) PASS();
  else FAIL("push key up");

  TEST("gui_event_push_key_up: preserves window/key metadata");
  if (gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_KEY_UP &&
      out.window_id == 8 &&
      out.key.keycode == 66 &&
      out.key.modifiers == 1 &&
      out.key.ch == 'B' &&
      out.timestamp == 100) PASS();
  else FAIL("key up metadata");
}

static void test_mouse_helpers(void) {
  gui_event_init();

  TEST("gui_event_push_mouse_move: enqueues motion payload");
  if (gui_event_push_mouse_move(10, 20, -3, 4, 5, 77) == 0 &&
      gui_event_pending() == 1) PASS();
  else FAIL("push move");

  struct gui_event out;
  TEST("gui_event_push_mouse_move: preserves motion metadata");
  if (gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_MOUSE_MOVE &&
      out.mouse.x == 10 &&
      out.mouse.y == 20 &&
      out.mouse.dx == -3 &&
      out.mouse.dy == 4 &&
      out.mouse.buttons == 5 &&
      out.timestamp == 77) PASS();
  else FAIL("move metadata");

  gui_event_push_mouse_button(1, 30, 40, 1, 88);
  gui_event_push_mouse_button(0, 31, 41, 0, 89);
  TEST("gui_event_push_mouse_button: preserves down/up types");
  if (gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_MOUSE_DOWN &&
      out.mouse.x == 30 &&
      out.mouse.y == 40 &&
      out.mouse.buttons == 1 &&
      out.timestamp == 88 &&
      gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_MOUSE_UP &&
      out.mouse.x == 31 &&
      out.mouse.y == 41 &&
      out.mouse.buttons == 0 &&
      out.timestamp == 89) PASS();
  else FAIL("button metadata");

  TEST("gui_event_push_mouse_scroll: stores delta in dy");
  if (gui_event_push_mouse_scroll(50, 60, -2, 4, 90) == 0 &&
      gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_MOUSE_SCROLL &&
      out.mouse.x == 50 &&
      out.mouse.y == 60 &&
      out.mouse.dx == 0 &&
      out.mouse.dy == -2 &&
      out.mouse.buttons == 4 &&
      out.timestamp == 90) PASS();
  else FAIL("scroll metadata");

  gui_event_init();
  gui_event_push_mouse_move(1, 1, 1, 0, 0, 1);
  gui_event_push_mouse_button(1, 1, 1, 1, 2);
  gui_event_push_mouse_scroll(1, 1, 1, 1, 3);
  TEST("gui_event mouse helpers: preserve FIFO type order");
  if (gui_event_poll(&out) == 0 && out.type == GUI_EVENT_MOUSE_MOVE &&
      gui_event_poll(&out) == 0 && out.type == GUI_EVENT_MOUSE_DOWN &&
      gui_event_poll(&out) == 0 && out.type == GUI_EVENT_MOUSE_SCROLL) PASS();
  else FAIL("mouse fifo");
}

static void test_mouse_move_coalescing(void) {
  struct gui_event out[4];

  gui_event_init();
  gui_event_push_mouse_move(10, 20, 5, 6, 0, 100);
  gui_event_push_mouse_move(12, 25, -2, 3, 1, 101);
  TEST("gui_event_push_mouse_move: coalesces consecutive motion");
  if (gui_event_pending() == 1 &&
      gui_event_poll_many(out, 2) == 1 &&
      out[0].type == GUI_EVENT_MOUSE_MOVE &&
      out[0].mouse.x == 12 &&
      out[0].mouse.y == 25 &&
      out[0].mouse.dx == 3 &&
      out[0].mouse.dy == 9 &&
      out[0].mouse.buttons == 1 &&
      out[0].timestamp == 101) PASS();
  else FAIL("move coalesce");

  gui_event_init();
  gui_event_push_mouse_move(1, 1, 1, 1, 0, 1);
  gui_event_push_mouse_button(1, 1, 1, 1, 2);
  gui_event_push_mouse_move(2, 2, 2, 2, 1, 3);
  TEST("gui_event_push_mouse_move: does not cross button boundary");
  if (gui_event_poll_many(out, 4) == 3 &&
      out[0].type == GUI_EVENT_MOUSE_MOVE &&
      out[1].type == GUI_EVENT_MOUSE_DOWN &&
      out[2].type == GUI_EVENT_MOUSE_MOVE) PASS();
  else FAIL("move boundary");

  gui_event_init();
  {
    struct gui_event targeted;
    targeted.type = GUI_EVENT_MOUSE_MOVE;
    targeted.window_id = 44;
    targeted.mouse.x = 1;
    targeted.mouse.y = 1;
    targeted.mouse.dx = 1;
    targeted.mouse.dy = 1;
    targeted.mouse.buttons = 0;
    targeted.timestamp = 1;
    gui_event_push(&targeted);
  }
  gui_event_push_mouse_move(2, 2, 2, 2, 1, 2);
  TEST("gui_event_push_mouse_move: does not coalesce explicit target");
  if (gui_event_poll_many(out, 4) == 2 &&
      out[0].type == GUI_EVENT_MOUSE_MOVE &&
      out[0].window_id == 44 &&
      out[1].type == GUI_EVENT_MOUSE_MOVE &&
      out[1].window_id == 0) PASS();
  else FAIL("move target boundary");

  gui_event_init();
  gui_event_push_mouse_move(1, 1, 32000, 32000, 0, 1);
  gui_event_push_mouse_move(2, 2, 1000, 1000, 0, 2);
  TEST("gui_event_push_mouse_move: saturates accumulated delta");
  if (gui_event_poll_many(out, 1) == 1 &&
      out[0].mouse.dx == 32767 &&
      out[0].mouse.dy == 32767) PASS();
  else FAIL("move saturate");

  gui_event_init();
  for (uint32_t i = 0; i < GUI_EVENT_QUEUE_SIZE; i++) {
    struct gui_event ev = make_event(i);
    gui_event_push(&ev);
  }
  for (uint32_t i = 0; i < GUI_EVENT_QUEUE_SIZE - 1u; i++) {
    struct gui_event discard;
    gui_event_poll(&discard);
  }
  gui_event_push_mouse_move(3, 4, 1, 2, 0, 10);
  gui_event_push_mouse_move(5, 7, 3, 5, 1, 11);
  TEST("gui_event_push_mouse_move: coalesces after ring wrap");
  if (gui_event_poll_many(out, 4) == 2 &&
      out[0].window_id == GUI_EVENT_QUEUE_SIZE - 1u &&
      out[1].type == GUI_EVENT_MOUSE_MOVE &&
      out[1].mouse.x == 5 &&
      out[1].mouse.y == 7 &&
      out[1].mouse.dx == 4 &&
      out[1].mouse.dy == 7 &&
      out[1].mouse.buttons == 1) PASS();
  else FAIL("move wrap");
}

static void test_mouse_scroll_coalescing(void) {
  struct gui_event out[4];

  gui_event_init();
  gui_event_push_mouse_scroll(10, 20, 5, 0, 100);
  gui_event_push_mouse_scroll(12, 25, -2, 1, 101);
  TEST("gui_event_push_mouse_scroll: coalesces consecutive scroll");
  if (gui_event_pending() == 1 &&
      gui_event_poll_many(out, 2) == 1 &&
      out[0].type == GUI_EVENT_MOUSE_SCROLL &&
      out[0].mouse.x == 12 &&
      out[0].mouse.y == 25 &&
      out[0].mouse.dx == 0 &&
      out[0].mouse.dy == 3 &&
      out[0].mouse.buttons == 1 &&
      out[0].timestamp == 101) PASS();
  else FAIL("scroll coalesce");

  gui_event_init();
  gui_event_push_mouse_scroll(1, 1, 1, 0, 1);
  gui_event_push_mouse_button(1, 1, 1, 1, 2);
  gui_event_push_mouse_scroll(2, 2, 2, 1, 3);
  TEST("gui_event_push_mouse_scroll: does not cross button boundary");
  if (gui_event_poll_many(out, 4) == 3 &&
      out[0].type == GUI_EVENT_MOUSE_SCROLL &&
      out[1].type == GUI_EVENT_MOUSE_DOWN &&
      out[2].type == GUI_EVENT_MOUSE_SCROLL) PASS();
  else FAIL("scroll boundary");

  gui_event_init();
  gui_event_push_mouse_scroll(1, 1, 1, 0, 1);
  gui_event_push_key(0, 'A', 0, 'A', 2);
  gui_event_push_mouse_scroll(2, 2, 2, 1, 3);
  TEST("gui_event_push_mouse_scroll: does not cross key boundary");
  if (gui_event_poll_many(out, 4) == 3 &&
      out[0].type == GUI_EVENT_MOUSE_SCROLL &&
      out[1].type == GUI_EVENT_KEY_DOWN &&
      out[2].type == GUI_EVENT_MOUSE_SCROLL) PASS();
  else FAIL("scroll key boundary");

  gui_event_init();
  gui_event_push_mouse_scroll(1, 1, 1, 0, 1);
  gui_event_push_mouse_move(2, 2, 1, 1, 0, 2);
  gui_event_push_mouse_scroll(3, 3, 2, 1, 3);
  TEST("gui_event_push_mouse_scroll: does not cross motion boundary");
  if (gui_event_poll_many(out, 4) == 3 &&
      out[0].type == GUI_EVENT_MOUSE_SCROLL &&
      out[1].type == GUI_EVENT_MOUSE_MOVE &&
      out[2].type == GUI_EVENT_MOUSE_SCROLL) PASS();
  else FAIL("scroll motion boundary");

  gui_event_init();
  {
    struct gui_event targeted;
    targeted.type = GUI_EVENT_MOUSE_SCROLL;
    targeted.window_id = 45;
    targeted.mouse.x = 1;
    targeted.mouse.y = 1;
    targeted.mouse.dx = 0;
    targeted.mouse.dy = 1;
    targeted.mouse.buttons = 0;
    targeted.timestamp = 1;
    gui_event_push(&targeted);
  }
  gui_event_push_mouse_scroll(2, 2, 2, 1, 2);
  TEST("gui_event_push_mouse_scroll: does not coalesce explicit target");
  if (gui_event_poll_many(out, 4) == 2 &&
      out[0].type == GUI_EVENT_MOUSE_SCROLL &&
      out[0].window_id == 45 &&
      out[1].type == GUI_EVENT_MOUSE_SCROLL &&
      out[1].window_id == 0) PASS();
  else FAIL("scroll target boundary");

  gui_event_init();
  gui_event_push_mouse_scroll(1, 1, 32000, 0, 1);
  gui_event_push_mouse_scroll(2, 2, 1000, 0, 2);
  TEST("gui_event_push_mouse_scroll: saturates accumulated delta");
  if (gui_event_poll_many(out, 1) == 1 &&
      out[0].mouse.dy == 32767) PASS();
  else FAIL("scroll saturate");

  gui_event_init();
  gui_event_push_mouse_scroll(1, 1, -32000, 0, 1);
  gui_event_push_mouse_scroll(2, 2, -1000, 0, 2);
  TEST("gui_event_push_mouse_scroll: saturates negative accumulated delta");
  if (gui_event_poll_many(out, 1) == 1 &&
      out[0].mouse.dy == -32768) PASS();
  else FAIL("scroll negative saturate");

  gui_event_init();
  for (uint32_t i = 0; i < GUI_EVENT_QUEUE_SIZE; i++) {
    struct gui_event ev = make_event(i);
    gui_event_push(&ev);
  }
  for (uint32_t i = 0; i < GUI_EVENT_QUEUE_SIZE - 1u; i++) {
    struct gui_event discard;
    gui_event_poll(&discard);
  }
  gui_event_push_mouse_scroll(3, 4, 2, 0, 10);
  gui_event_push_mouse_scroll(5, 7, 5, 1, 11);
  TEST("gui_event_push_mouse_scroll: coalesces after ring wrap");
  if (gui_event_poll_many(out, 4) == 2 &&
      out[0].window_id == GUI_EVENT_QUEUE_SIZE - 1u &&
      out[1].type == GUI_EVENT_MOUSE_SCROLL &&
      out[1].mouse.x == 5 &&
      out[1].mouse.y == 7 &&
      out[1].mouse.dx == 0 &&
      out[1].mouse.dy == 7 &&
      out[1].mouse.buttons == 1) PASS();
  else FAIL("scroll wrap");
}

static void test_window_timer_helpers(void) {
  gui_event_init();
  struct gui_event out;

  TEST("gui_event_push_window_close: preserves metadata");
  if (gui_event_push_window_close(77, 101) == 0 &&
      gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_WINDOW_CLOSE &&
      out.window_id == 77 &&
      out.timestamp == 101) PASS();
  else FAIL("window close");

  TEST("gui_event_push_window_resize: preserves dimensions");
  if (gui_event_push_window_resize(78, 640, 480, 102) == 0 &&
      gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_WINDOW_RESIZE &&
      out.window_id == 78 &&
      out.resize.width == 640 &&
      out.resize.height == 480 &&
      out.timestamp == 102) PASS();
  else FAIL("window resize");

  gui_event_push_window_focus(79, 103);
  gui_event_push_window_blur(80, 104);
  TEST("gui_event window focus/blur helpers: preserve type order");
  if (gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_WINDOW_FOCUS &&
      out.window_id == 79 &&
      out.timestamp == 103 &&
      gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_WINDOW_BLUR &&
      out.window_id == 80 &&
      out.timestamp == 104) PASS();
  else FAIL("window focus blur");

  TEST("gui_event_push_paint: preserves window target");
  if (gui_event_push_paint(81, 105) == 0 &&
      gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_PAINT &&
      out.window_id == 81 &&
      out.timestamp == 105) PASS();
  else FAIL("paint");

  TEST("gui_event_push_paint: rejects missing window target");
  if (gui_event_push_paint(0, 106) != 0 &&
      gui_event_pending() == 0) PASS();
  else FAIL("paint target");

  TEST("gui_event_push_timer: preserves timer metadata");
  if (gui_event_push_timer(82, 900, 107) == 0 &&
      gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_TIMER &&
      out.window_id == 82 &&
      out.timer.timer_id == 900 &&
      out.timestamp == 107) PASS();
  else FAIL("timer");

  TEST("gui_event_push_timer: rejects missing window target");
  if (gui_event_push_timer(0, 901, 108) != 0 &&
      gui_event_pending() == 0) PASS();
  else FAIL("timer target");

  gui_event_init();
  gui_event_push_window_close(1, 1);
  gui_event_push_window_resize(2, 3, 4, 2);
  gui_event_push_paint(3, 3);
  gui_event_push_timer(4, 5, 4);
  TEST("gui_event non-input helpers: preserve FIFO type order");
  if (gui_event_poll(&out) == 0 && out.type == GUI_EVENT_WINDOW_CLOSE &&
      gui_event_poll(&out) == 0 && out.type == GUI_EVENT_WINDOW_RESIZE &&
      gui_event_poll(&out) == 0 && out.type == GUI_EVENT_PAINT &&
      gui_event_poll(&out) == 0 && out.type == GUI_EVENT_TIMER) PASS();
  else FAIL("non-input fifo");
}

static void test_paint_coalescing(void) {
  struct gui_event out[8];

  gui_event_init();
  TEST("gui_event_push_paint: coalesces duplicate window paint");
  if (gui_event_push_paint(42, 100) == 0 &&
      gui_event_push_paint(42, 101) == 0 &&
      gui_event_pending() == 1 &&
      gui_event_poll_many(out, 2) == 1 &&
      out[0].type == GUI_EVENT_PAINT &&
      out[0].window_id == 42 &&
      out[0].timestamp == 100) PASS();
  else FAIL("paint duplicate");

  gui_event_init();
  gui_event_push_paint(1, 10);
  gui_event_push_paint(2, 20);
  TEST("gui_event_push_paint: keeps distinct window paints");
  if (gui_event_poll_many(out, 3) == 2 &&
      out[0].type == GUI_EVENT_PAINT &&
      out[0].window_id == 1 &&
      out[1].type == GUI_EVENT_PAINT &&
      out[1].window_id == 2) PASS();
  else FAIL("paint distinct");

  gui_event_init();
  gui_event_push_paint(3, 30);
  gui_event_poll_many(out, 1);
  TEST("gui_event_push_paint: allows repaint after drain");
  if (gui_event_push_paint(3, 31) == 0 &&
      gui_event_poll_many(out, 1) == 1 &&
      out[0].type == GUI_EVENT_PAINT &&
      out[0].window_id == 3 &&
      out[0].timestamp == 31) PASS();
  else FAIL("paint requeue");

  gui_event_init();
  for (uint32_t i = 0; i < GUI_EVENT_QUEUE_SIZE; i++) {
    struct gui_event ev = make_event(i);
    gui_event_push(&ev);
  }
  for (uint32_t i = 0; i < GUI_EVENT_QUEUE_SIZE - 2u; i++) {
    struct gui_event discard;
    gui_event_poll(&discard);
  }
  gui_event_push_paint(77, 1);
  gui_event_push_timer(88, 2, 2);
  gui_event_push_paint(77, 3);
  TEST("gui_event_push_paint: coalesces across wrap-around");
  if (gui_event_poll_many(out, 8) == 4 &&
      out[0].window_id == GUI_EVENT_QUEUE_SIZE - 2u &&
      out[1].window_id == GUI_EVENT_QUEUE_SIZE - 1u &&
      out[2].type == GUI_EVENT_PAINT &&
      out[2].window_id == 77 &&
      out[3].type == GUI_EVENT_TIMER &&
      out[3].window_id == 88) PASS();
  else FAIL("paint wrap");
}

static void test_timer_coalescing(void) {
  struct gui_event out[8];

  gui_event_init();
  gui_event_push_timer(10, 1, 100);
  gui_event_push_timer(10, 1, 101);
  TEST("gui_event_push_timer: coalesces duplicate window timer");
  if (gui_event_pending() == 1 &&
      gui_event_poll_many(out, 2) == 1 &&
      out[0].type == GUI_EVENT_TIMER &&
      out[0].window_id == 10 &&
      out[0].timer.timer_id == 1 &&
      out[0].timestamp == 100) PASS();
  else FAIL("timer duplicate");

  gui_event_init();
  gui_event_push_timer(10, 1, 10);
  gui_event_push_timer(10, 2, 20);
  TEST("gui_event_push_timer: keeps distinct timer ids");
  if (gui_event_poll_many(out, 3) == 2 &&
      out[0].type == GUI_EVENT_TIMER &&
      out[0].window_id == 10 &&
      out[0].timer.timer_id == 1 &&
      out[1].type == GUI_EVENT_TIMER &&
      out[1].window_id == 10 &&
      out[1].timer.timer_id == 2) PASS();
  else FAIL("timer ids");

  gui_event_init();
  gui_event_push_timer(10, 1, 10);
  gui_event_push_timer(11, 1, 20);
  TEST("gui_event_push_timer: keeps distinct window timers");
  if (gui_event_poll_many(out, 3) == 2 &&
      out[0].window_id == 10 &&
      out[0].timer.timer_id == 1 &&
      out[1].window_id == 11 &&
      out[1].timer.timer_id == 1) PASS();
  else FAIL("timer windows");

  gui_event_init();
  gui_event_push_timer(12, 3, 30);
  gui_event_poll_many(out, 1);
  TEST("gui_event_push_timer: allows timer after drain");
  if (gui_event_push_timer(12, 3, 31) == 0 &&
      gui_event_poll_many(out, 1) == 1 &&
      out[0].type == GUI_EVENT_TIMER &&
      out[0].window_id == 12 &&
      out[0].timer.timer_id == 3 &&
      out[0].timestamp == 31) PASS();
  else FAIL("timer requeue");

  gui_event_init();
  for (uint32_t i = 0; i < GUI_EVENT_QUEUE_SIZE; i++) {
    struct gui_event ev = make_event(i);
    gui_event_push(&ev);
  }
  for (uint32_t i = 0; i < GUI_EVENT_QUEUE_SIZE - 2u; i++) {
    struct gui_event discard;
    gui_event_poll(&discard);
  }
  gui_event_push_timer(77, 1, 1);
  gui_event_push_timer(88, 2, 2);
  gui_event_push_timer(77, 1, 3);
  TEST("gui_event_push_timer: coalesces across wrap-around");
  if (gui_event_poll_many(out, 8) == 4 &&
      out[0].window_id == GUI_EVENT_QUEUE_SIZE - 2u &&
      out[1].window_id == GUI_EVENT_QUEUE_SIZE - 1u &&
      out[2].type == GUI_EVENT_TIMER &&
      out[2].window_id == 77 &&
      out[2].timer.timer_id == 1 &&
      out[2].timestamp == 1 &&
      out[3].type == GUI_EVENT_TIMER &&
      out[3].window_id == 88 &&
      out[3].timer.timer_id == 2) PASS();
  else FAIL("timer wrap");
}

static void test_discard_window(void) {
  struct gui_event out[8];
  struct gui_event_snapshot snap;

  gui_event_init();
  gui_event_push_timer(1, 10, 1);
  gui_event_push_timer(2, 20, 2);
  TEST("gui_event_discard_window: missing target is no-op");
  if (gui_event_discard_window(99) == 0 &&
      gui_event_pending() == 2 &&
      gui_event_poll_many(out, 2) == 2 &&
      out[0].window_id == 1 &&
      out[1].window_id == 2) PASS();
  else FAIL("discard missing");

  gui_event_init();
  gui_event_push_mouse_move(1, 2, 3, 4, 0, 1);
  gui_event_push_timer(1, 10, 2);
  TEST("gui_event_discard_window(0): global events are protected");
  if (gui_event_discard_window(0) == 0 &&
      gui_event_pending() == 2 &&
      gui_event_poll_many(out, 2) == 2 &&
      out[0].window_id == 0 &&
      out[1].window_id == 1) PASS();
  else FAIL("discard global");

  gui_event_init();
  gui_event_push_timer(10, 1, 1);
  gui_event_push_timer(20, 2, 2);
  gui_event_push_timer(10, 3, 3);
  gui_event_push_timer(30, 4, 4);
  TEST("gui_event_discard_window: removes target and preserves FIFO");
  if (gui_event_discard_window(10) == 2 &&
      gui_event_pending() == 2 &&
      gui_event_poll_many(out, 4) == 2 &&
      out[0].window_id == 20 &&
      out[1].window_id == 30) PASS();
  else FAIL("discard fifo");

  gui_event_init();
  for (uint32_t i = 0; i < GUI_EVENT_QUEUE_SIZE; i++) {
    struct gui_event ev = make_event(i);
    gui_event_push(&ev);
  }
  for (uint32_t i = 0; i < GUI_EVENT_QUEUE_SIZE - 3u; i++) {
    struct gui_event discard;
    gui_event_poll(&discard);
  }
  gui_event_push_timer(77, 1, 1);
  gui_event_push_timer(88, 2, 2);
  gui_event_push_timer(77, 3, 3);
  TEST("gui_event_discard_window: preserves FIFO across wrap-around");
  if (gui_event_discard_window(77) == 2 &&
      gui_event_poll_many(out, 8) == 4 &&
      out[0].window_id == GUI_EVENT_QUEUE_SIZE - 3u &&
      out[1].window_id == GUI_EVENT_QUEUE_SIZE - 2u &&
      out[2].window_id == GUI_EVENT_QUEUE_SIZE - 1u &&
      out[3].window_id == 88) PASS();
  else FAIL("discard wrap");

  TEST("gui_event_discard_window: preserves diagnostic window");
  if (gui_event_snapshot(&snap) == 1 &&
      snap.pending == 0 &&
      snap.high_watermark == GUI_EVENT_QUEUE_SIZE &&
      snap.dropped_total == 0) PASS();
  else FAIL("discard diagnostics");
}

static void test_snapshot(void) {
  gui_event_init();

  TEST("gui_event_snapshot(NULL): returns 0");
  if (gui_event_snapshot(0) == 0) PASS();
  else FAIL("snapshot null");

  struct gui_event_snapshot snap;
  TEST("gui_event_snapshot: init state is coherent");
  if (gui_event_snapshot(&snap) == 1 &&
      snap.capacity == GUI_EVENT_QUEUE_SIZE &&
      snap.pending == 0 &&
      snap.space_available == GUI_EVENT_QUEUE_SIZE &&
      snap.high_watermark == 0 &&
      snap.dropped_total == 0) PASS();
  else FAIL("snapshot init");

  struct gui_event a = make_event(1);
  struct gui_event b = make_event(2);
  gui_event_push(&a);
  gui_event_push(&b);
  TEST("gui_event_snapshot: active state tracks occupancy");
  if (gui_event_snapshot(&snap) == 1 &&
      snap.pending == 2 &&
      snap.space_available == GUI_EVENT_QUEUE_SIZE - 2u &&
      snap.high_watermark == 2 &&
      snap.dropped_total == 0) PASS();
  else FAIL("snapshot active");

  struct gui_event out;
  gui_event_poll(&out);
  gui_event_poll(&out);
  TEST("gui_event_snapshot: drain preserves high-watermark");
  if (gui_event_snapshot(&snap) == 1 &&
      snap.pending == 0 &&
      snap.space_available == GUI_EVENT_QUEUE_SIZE &&
      snap.high_watermark == 2) PASS();
  else FAIL("snapshot drained");

  gui_event_init();
  for (uint32_t i = 0; i < GUI_EVENT_QUEUE_SIZE + 1u; i++) {
    struct gui_event ev = make_event(i);
    gui_event_push(&ev);
  }
  TEST("gui_event_snapshot: overflow reports full queue and drops");
  if (gui_event_snapshot(&snap) == 1 &&
      snap.pending == GUI_EVENT_QUEUE_SIZE &&
      snap.space_available == 0 &&
      snap.high_watermark == GUI_EVENT_QUEUE_SIZE &&
      snap.dropped_total == 1) PASS();
  else FAIL("snapshot overflow");

  gui_event_flush();
  TEST("gui_event_snapshot: flush clears pending but preserves diagnostics");
  if (gui_event_snapshot(&snap) == 1 &&
      snap.pending == 0 &&
      snap.space_available == GUI_EVENT_QUEUE_SIZE &&
      snap.high_watermark == GUI_EVENT_QUEUE_SIZE &&
      snap.dropped_total == 1) PASS();
  else FAIL("snapshot flush");
}

static void test_reset_diagnostics(void) {
  gui_event_init();
  struct gui_event a = make_event(1);
  struct gui_event b = make_event(2);
  struct gui_event out;
  struct gui_event_snapshot snap;
  gui_event_push(&a);
  gui_event_push(&b);
  gui_event_poll(&out);

  gui_event_reset_diagnostics();
  TEST("gui_event_reset_diagnostics: rebases high-watermark");
  if (gui_event_snapshot(&snap) == 1 &&
      snap.pending == 1 &&
      snap.high_watermark == 1 &&
      snap.dropped_total == 0) PASS();
  else FAIL("reset rebase");

  TEST("gui_event_reset_diagnostics: preserves FIFO contents");
  if (gui_event_poll(&out) == 0 &&
      out.window_id == 2 &&
      gui_event_pending() == 0) PASS();
  else FAIL("reset fifo");

  gui_event_init();
  for (uint32_t i = 0; i < GUI_EVENT_QUEUE_SIZE + 2u; i++) {
    struct gui_event ev = make_event(i);
    gui_event_push(&ev);
  }
  gui_event_reset_diagnostics();
  TEST("gui_event_reset_diagnostics: clears drops while full");
  if (gui_event_snapshot(&snap) == 1 &&
      snap.pending == GUI_EVENT_QUEUE_SIZE &&
      snap.high_watermark == GUI_EVENT_QUEUE_SIZE &&
      snap.dropped_total == 0) PASS();
  else FAIL("reset drops");

  struct gui_event extra = make_event(999);
  gui_event_push(&extra);
  TEST("gui_event_reset_diagnostics: later overflow is counted fresh");
  if (gui_event_snapshot(&snap) == 1 &&
      snap.pending == GUI_EVENT_QUEUE_SIZE &&
      snap.dropped_total == 1) PASS();
  else FAIL("fresh drop");

  while (gui_event_poll(&out) == 0) { }
  gui_event_reset_diagnostics();
  TEST("gui_event_reset_diagnostics: empty queue rebases to zero");
  if (gui_event_snapshot(&snap) == 1 &&
      snap.pending == 0 &&
      snap.high_watermark == 0 &&
      snap.dropped_total == 0) PASS();
  else FAIL("reset empty");
}

static void test_overflow_drop_oldest(void) {
  gui_event_init();
  for (uint32_t i = 0; i < GUI_EVENT_QUEUE_SIZE + 2u; i++) {
    struct gui_event ev = make_event(i);
    if (gui_event_push(&ev) != 0) {
      TEST("gui_event_overflow: push accepts newest events");
      FAIL("push failed");
      return;
    }
  }

  TEST("gui_event_overflow: count stays capped and drops counted");
  if (gui_event_pending() == (int)GUI_EVENT_QUEUE_SIZE &&
      gui_event_dropped_total() == 2) PASS();
  else FAIL("overflow state");

  struct gui_event out;
  TEST("gui_event_overflow: oldest events are discarded first");
  if (gui_event_poll(&out) == 0 && out.window_id == 2) PASS();
  else FAIL("oldest");

  uint32_t last = out.window_id;
  while (gui_event_poll(&out) == 0) last = out.window_id;
  TEST("gui_event_overflow: newest event is retained");
  if (last == GUI_EVENT_QUEUE_SIZE + 1u) PASS();
  else FAIL("newest");

  gui_event_flush();
  TEST("gui_event_flush: preserves drop diagnostics");
  if (gui_event_pending() == 0 && gui_event_dropped_total() == 2) PASS();
  else FAIL("flush drops");

  gui_event_init();
  TEST("gui_event_init: resets drop diagnostics");
  if (gui_event_dropped_total() == 0) PASS();
  else FAIL("drop reset");
}

void test_gui_event_helper_cases(void) {
  test_key_helper();
  test_mouse_helpers();
  test_mouse_move_coalescing();
  test_mouse_scroll_coalescing();
  test_window_timer_helpers();
  test_paint_coalescing();
  test_timer_coalescing();
  test_discard_window();
  test_snapshot();
  test_reset_diagnostics();
  test_overflow_drop_oldest();
}
