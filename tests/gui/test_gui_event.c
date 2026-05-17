/*
 * tests/test_gui_event.c
 *
 * Entry point + queue/FIFO/poll/peek/dispatch/ready/backpressure
 * coverage for the GUI event API. The companion file
 * `tests/test_gui_event_helpers.c` owns the push-helper, coalescing,
 * discard, snapshot, reset and overflow coverage. Both files share
 * the TEST/PASS/FAIL macros, counter externs and the `make_event`
 * helper through `tests/test_gui_event_internal.h`.
 *
 * Carved out of the historical single-file `tests/test_gui_event.c`
 * (1085 LOC) at the 2026-05-15 monolith refactor so each host-test
 * translation unit stays under the 900-line layout limit.
 */
#include "test_gui_event_internal.h"

int test_gui_event_runs = 0;
int test_gui_event_passes = 0;

struct gui_event test_gui_event_make_event(uint32_t id) {
  struct gui_event ev;
  ev.type = GUI_EVENT_TIMER;
  ev.window_id = id;
  ev.timer.timer_id = id;
  ev.timestamp = id;
  return ev;
}

#define make_event test_gui_event_make_event

struct dispatch_probe {
  uint32_t count;
  uint32_t ids[8];
  int push_on_first;
};

static void capture_dispatch(const struct gui_event *ev, void *ctx) {
  struct dispatch_probe *probe = (struct dispatch_probe *)ctx;
  if (!ev || !probe) return;
  if (probe->count < 8) probe->ids[probe->count] = ev->window_id;
  probe->count++;
  if (probe->push_on_first && probe->count == 1) {
    struct gui_event extra = make_event(99);
    gui_event_push(&extra);
  }
}

static void test_init_and_null(void) {
  gui_event_init();

  TEST("gui_event_init: capacity matches constant");
  if (gui_event_capacity() == GUI_EVENT_QUEUE_SIZE) PASS();
  else FAIL("capacity");

  TEST("gui_event_init: pending and drops are zero");
  if (gui_event_pending() == 0 && gui_event_dropped_total() == 0) PASS();
  else FAIL("state");

  TEST("gui_event_push(NULL): rejected");
  if (gui_event_push(0) != 0) PASS();
  else FAIL("null push");
}

static void test_fifo_peek_and_flush(void) {
  gui_event_init();
  struct gui_event a = make_event(10);
  struct gui_event b = make_event(11);
  struct gui_event out;

  TEST("gui_event_push: returns 0 on success");
  if (gui_event_push(&a) == 0 && gui_event_push(&b) == 0) PASS();
  else FAIL("push");

  TEST("gui_event_pending: counts queued events");
  if (gui_event_pending() == 2) PASS();
  else FAIL("pending");

  TEST("gui_event_peek: copies head without consuming");
  if (gui_event_peek(&out) == 0 && out.window_id == 10 &&
      gui_event_pending() == 2) PASS();
  else FAIL("peek");

  TEST("gui_event_poll: drains FIFO in order");
  if (gui_event_poll(&out) == 0 && out.window_id == 10 &&
      gui_event_poll(&out) == 0 && out.window_id == 11 &&
      gui_event_pending() == 0) PASS();
  else FAIL("poll");

  gui_event_push(&a);
  gui_event_flush();
  TEST("gui_event_flush: empties the queue");
  if (gui_event_pending() == 0) PASS();
  else FAIL("flush");
}

static void test_poll_many(void) {
  gui_event_init();
  struct gui_event a = make_event(10);
  struct gui_event b = make_event(11);
  struct gui_event c = make_event(12);
  struct gui_event out[3];

  TEST("gui_event_poll_many(NULL): rejected");
  if (gui_event_poll_many(0, 1) == 0) PASS();
  else FAIL("poll_many null");

  TEST("gui_event_poll_many(max=0): no-op");
  if (gui_event_push(&a) == 0 &&
      gui_event_poll_many(out, 0) == 0 &&
      gui_event_pending() == 1) PASS();
  else FAIL("poll_many zero");

  TEST("gui_event_poll_many: drains entire FIFO");
  if (gui_event_push(&b) == 0 &&
      gui_event_push(&c) == 0 &&
      gui_event_poll_many(out, 3) == 3 &&
      out[0].window_id == 10 &&
      out[1].window_id == 11 &&
      out[2].window_id == 12 &&
      gui_event_pending() == 0) PASS();
  else FAIL("poll_many drain");

  gui_event_push(&a);
  gui_event_push(&b);
  gui_event_push(&c);
  TEST("gui_event_poll_many: stops at requested max");
  if (gui_event_poll_many(out, 2) == 2 &&
      out[0].window_id == 10 &&
      out[1].window_id == 11 &&
      gui_event_pending() == 1) PASS();
  else FAIL("poll_many max");

  gui_event_poll_many(out, 1);
  TEST("gui_event_poll_many: returns 0 when empty");
  if (gui_event_poll_many(out, 4) == 0) PASS();
  else FAIL("poll_many empty");

  gui_event_init();
  for (uint32_t i = 0; i < GUI_EVENT_QUEUE_SIZE; i++) {
    struct gui_event ev = make_event(i);
    gui_event_push(&ev);
  }
  for (uint32_t i = 0; i < GUI_EVENT_QUEUE_SIZE - 2u; i++) {
    struct gui_event discard;
    gui_event_poll(&discard);
  }
  struct gui_event x = make_event(91);
  struct gui_event y = make_event(92);
  gui_event_push(&x);
  gui_event_push(&y);
  TEST("gui_event_poll_many: handles ring wrap-around");
  struct gui_event wrap_out[4];
  if (gui_event_poll_many(wrap_out, 4) == 4 &&
      wrap_out[0].window_id == GUI_EVENT_QUEUE_SIZE - 2u &&
      wrap_out[1].window_id == GUI_EVENT_QUEUE_SIZE - 1u &&
      wrap_out[2].window_id == 91 &&
      wrap_out[3].window_id == 92) PASS();
  else FAIL("poll many wrap");
}

static void test_peek_many(void) {
  struct gui_event a = make_event(10);
  struct gui_event b = make_event(11);
  struct gui_event c = make_event(12);
  struct gui_event out[3];

  gui_event_init();
  TEST("gui_event_peek_many(NULL): rejected");
  if (gui_event_peek_many(0, 1) == 0) PASS();
  else FAIL("peek many null");

  TEST("gui_event_peek_many(max=0): no-op");
  if (gui_event_push(&a) == 0 &&
      gui_event_peek_many(out, 0) == 0 &&
      gui_event_pending() == 1) PASS();
  else FAIL("peek many zero");

  TEST("gui_event_peek_many: copies head without draining");
  if (gui_event_push(&b) == 0 &&
      gui_event_push(&c) == 0 &&
      gui_event_peek_many(out, 3) == 3 &&
      out[0].window_id == 10 &&
      out[1].window_id == 11 &&
      out[2].window_id == 12 &&
      gui_event_pending() == 3) PASS();
  else FAIL("peek many drain");

  TEST("gui_event_peek_many: respects requested max");
  if (gui_event_peek_many(out, 2) == 2 &&
      out[0].window_id == 10 &&
      out[1].window_id == 11 &&
      gui_event_pending() == 3) PASS();
  else FAIL("peek many max");

  gui_event_flush();
  TEST("gui_event_peek_many: returns 0 when empty");
  if (gui_event_peek_many(out, 4) == 0) PASS();
  else FAIL("peek many empty");

  gui_event_init();
  for (uint32_t i = 0; i < GUI_EVENT_QUEUE_SIZE; i++) {
    struct gui_event ev = make_event(i);
    gui_event_push(&ev);
  }
  for (uint32_t i = 0; i < GUI_EVENT_QUEUE_SIZE - 2u; i++) {
    struct gui_event discard;
    gui_event_poll(&discard);
  }
  struct gui_event x = make_event(91);
  struct gui_event y = make_event(92);
  gui_event_push(&x);
  gui_event_push(&y);
  TEST("gui_event_peek_many: handles ring wrap-around");
  struct gui_event wrap_out[4];
  if (gui_event_peek_many(wrap_out, 4) == 4 &&
      wrap_out[0].window_id == GUI_EVENT_QUEUE_SIZE - 2u &&
      wrap_out[1].window_id == GUI_EVENT_QUEUE_SIZE - 1u &&
      wrap_out[2].window_id == 91 &&
      wrap_out[3].window_id == 92 &&
      gui_event_pending() == 4) PASS();
  else FAIL("peek many wrap");
}

static void test_dispatch(void) {
  struct gui_event a = make_event(10);
  struct gui_event b = make_event(11);
  struct gui_event c = make_event(12);
  struct gui_event out;
  struct dispatch_probe probe;

  gui_event_init();
  gui_event_push(&a);
  TEST("gui_event_dispatch(NULL,cb): rejected");
  if (gui_event_dispatch(0, 0, 1) == 0 &&
      gui_event_pending() == 1) PASS();
  else FAIL("dispatch null cb");

  TEST("gui_event_dispatch(max=0): no-op");
  probe.count = 0;
  probe.push_on_first = 0;
  if (gui_event_dispatch(capture_dispatch, &probe, 0) == 0 &&
      probe.count == 0 &&
      gui_event_pending() == 1) PASS();
  else FAIL("dispatch zero");

  gui_event_push(&b);
  gui_event_push(&c);
  probe.count = 0;
  probe.push_on_first = 0;
  TEST("gui_event_dispatch: limited FIFO drain");
  if (gui_event_dispatch(capture_dispatch, &probe, 2) == 2 &&
      probe.count == 2 &&
      probe.ids[0] == 10 &&
      probe.ids[1] == 11 &&
      gui_event_pending() == 1) PASS();
  else FAIL("dispatch limited");

  TEST("gui_event_dispatch: drains remaining events");
  if (gui_event_dispatch(capture_dispatch, &probe, 4) == 1 &&
      probe.count == 3 &&
      probe.ids[2] == 12 &&
      gui_event_pending() == 0) PASS();
  else FAIL("dispatch remaining");

  gui_event_init();
  gui_event_push(&a);
  gui_event_push(&b);
  probe.count = 0;
  probe.push_on_first = 1;
  TEST("gui_event_dispatch: ignores callback-enqueued events this pass");
  if (gui_event_dispatch(capture_dispatch, &probe, 8) == 2 &&
      probe.count == 2 &&
      probe.ids[0] == 10 &&
      probe.ids[1] == 11 &&
      gui_event_pending() == 1) PASS();
  else FAIL("dispatch enqueue");

  TEST("gui_event_dispatch: drains callback-enqueued event next pass");
  if (gui_event_poll(&out) == 0 && out.window_id == 99) PASS();
  else FAIL("dispatch enqueue");
}

static void test_ready(void) {
  gui_event_init();
  struct gui_event ev = make_event(42);
  struct gui_event out[2];

  TEST("gui_event_ready: empty queue returns 0");
  if (gui_event_ready() == 0) PASS();
  else FAIL("ready empty");

  gui_event_push(&ev);
  TEST("gui_event_ready: reports pending events");
  if (gui_event_ready() == 1 &&
      gui_event_peek_many(out, 1) == 1 &&
      out[0].window_id == 42 &&
      gui_event_pending() == 1) PASS();
  else FAIL("ready head");

  gui_event_push(&ev);
  TEST("gui_event_peek_many: caps at requested max");
  if (gui_event_ready() == 1 &&
      gui_event_peek_many(out, 1) == 1 &&
      gui_event_pending() == 2) PASS();
  else FAIL("ready cap");

  TEST("gui_event_peek_many: returns all events when buffer is large");
  if (gui_event_peek_many(out, 2) == 2 &&
      gui_event_pending() == 2) PASS();
  else FAIL("ready full");
}

static void test_backpressure_helpers(void) {
  struct gui_event ev = make_event(1);
  struct gui_event out;
  struct gui_event_snapshot snapshot;

  gui_event_init();
  TEST("gui_event_space_available: empty queue exposes full capacity");
  if (gui_event_space_available() == (uint32_t)GUI_EVENT_QUEUE_SIZE) PASS();
  else FAIL("space empty");

  for (uint32_t i = 0; i < GUI_EVENT_QUEUE_SIZE; i++) {
    struct gui_event item = make_event(i);
    gui_event_push(&item);
  }
  TEST("gui_event_space_available: zero when queue is full");
  if (gui_event_space_available() == 0) PASS();
  else FAIL("space full");

  TEST("gui_event_high_watermark: tracks peak occupancy");
  if (gui_event_snapshot(&snapshot) == 1 &&
      snapshot.high_watermark == (uint32_t)GUI_EVENT_QUEUE_SIZE) PASS();
  else FAIL("high water");

  gui_event_poll(&out);
  TEST("gui_event_high_watermark: persists after drain");
  if (gui_event_snapshot(&snapshot) == 1 &&
      snapshot.high_watermark == (uint32_t)GUI_EVENT_QUEUE_SIZE) PASS();
  else FAIL("high water drained");

  gui_event_init();
  gui_event_push(&ev);
  TEST("gui_event_space_available: increases as events drain");
  if (gui_event_space_available() == (uint32_t)(GUI_EVENT_QUEUE_SIZE - 1u) &&
      gui_event_poll(&out) == 0 &&
      gui_event_space_available() == (uint32_t)GUI_EVENT_QUEUE_SIZE) PASS();
  else FAIL("space drain");
}

int test_gui_event_run(void) {
  printf("[test_gui_event]\n");
  test_gui_event_runs = 0;
  test_gui_event_passes = 0;
  test_init_and_null();
  test_fifo_peek_and_flush();
  test_poll_many();
  test_peek_many();
  test_dispatch();
  test_ready();
  test_backpressure_helpers();
  test_gui_event_helper_cases();
  printf("  -> %d/%d passed\n", test_gui_event_passes, test_gui_event_runs);
  return test_gui_event_runs - test_gui_event_passes;
}
