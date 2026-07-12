/*
 * tests/gui/test_syscall_gfx_backend.c — host tests for the PRODUCTION
 * graphical backend src/kernel/syscall_gfx_init.c (the compositor-backed
 * `struct syscall_gfx_ops` the ring-3 browser capygfx actually runs against).
 *
 * tests/kernel/test_syscall_gfx.c covers the handler/policy layer with a FAKE
 * backend and deliberately does NOT link the compositor. This suite links the
 * REAL backend + compositor + event queue so the alpha.311 fixes that until now
 * were exercised only by on-target QEMU/VMware smokes get host coverage:
 *
 *   1. The poll return-value fix. gui_event_poll returns 0 (dequeued a event)
 *      or -1 (queue empty) -- it NEVER returns 1. The pre-alpha.311 backend
 *      checked `!= 1`, which is always true, so it reported "empty" forever and
 *      delivered NO event to the ring-3 owner. The backend now checks `!= 0`.
 *   2. Window-gone -> CLOSE. Once the compositor window is destroyed (the
 *      desktop's title-bar X handler called compositor_destroy_window), every
 *      poll returns CAPY_GFX_EV_CLOSE so the owner reliably exits, independent
 *      of whether the queued WINDOW_CLOSE survived the shared queue.
 *   3. GUI_EVENT_* -> CAPY_GFX_EV_* translation with window-local coordinates.
 *   4. ownership-aware routing: a poll consumes only its window's oldest
 *      event and leaves foreign/global events in FIFO order for their owner.
 *   5. focus hook restores and raises an already-running instance.
 *
 * The backend id IS the compositor window id, so we drive the backend directly
 * through the installed vtable (syscall_gfx_get_ops()->poll_event), exactly how
 * sys_window_poll_event reaches it in production.
 */
#include <stdio.h>
#include <string.h>

#include "gui/compositor.h"
#include "gui/event.h"
#include "kernel/syscall_gfx.h"

static int tests_run = 0;
static int tests_passed = 0;
static uint32_t framebuffer[320 * 200];
static const struct syscall_gfx_ops *g_ops;

#define TEST(name)                                                             \
  do {                                                                         \
    tests_run++;                                                               \
    printf("  %-58s ", name);                                                  \
  } while (0)
#define PASS()                                                                 \
  do {                                                                         \
    printf("OK\n");                                                            \
    tests_passed++;                                                            \
  } while (0)
#define FAIL(msg)                                                              \
  do {                                                                         \
    printf("FAIL: %s\n", msg);                                                 \
  } while (0)

/* Bring up a compositor + the real backend and return a live, focused window.
 * The show/focus lifecycle events are flushed so each test starts on an empty
 * queue. */
static struct gui_window *fixture_window(void) {
  struct gui_window *win;
  uint32_t i;
  for (i = 0u; i < 320u * 200u; ++i) framebuffer[i] = 0u;
  gui_event_init();
  compositor_init(framebuffer, 320, 200, 320 * 4);
  syscall_gfx_install_default_ops();
  g_ops = syscall_gfx_get_ops();
  win = compositor_create_window("gfx", 20, 30, 64, 48);
  if (win) {
    compositor_show_window(win->id);
    compositor_focus_window(win->id);
    gui_event_init(); /* drop the show/focus lifecycle events */
  }
  return win;
}

static void fixture_teardown(void) {
  syscall_gfx_install_ops((const struct syscall_gfx_ops *)0);
  compositor_shutdown();
  gui_event_init();
}

/* 1. Empty queue -> 0. Pre-alpha.311 this ALSO returned 0, but for the wrong
 *    reason (`!= 1`); paired with test 2 it pins the corrected `!= 0` check. */
static void test_poll_empty_returns_zero(void) {
  struct gui_window *win = fixture_window();
  struct capy_gfx_event ev;
  TEST("backend: empty queue -> 0");
  if (win && g_ops && g_ops->poll_event &&
      g_ops->poll_event((int32_t)win->id, &ev) == 0)
    PASS();
  else
    FAIL("empty poll did not return 0");
  fixture_teardown();
}

/* 2. A queued KEY_DOWN is delivered and translated. This is the regression the
 *    `!= 1`->`!= 0` fix repaired: before, this returned 0 (no event) forever. */
static void test_poll_delivers_key(void) {
  struct gui_window *win = fixture_window();
  struct capy_gfx_event ev;
  int r;
  if (!win) {
    TEST("backend key: fixture");
    FAIL("no window");
    fixture_teardown();
    return;
  }
  gui_event_push_key(win->id, 65u, 0x2u, 'A', 0u);
  memset(&ev, 0, sizeof(ev));
  r = g_ops->poll_event((int32_t)win->id, &ev);
  TEST("backend: queued KEY_DOWN delivered + translated (the != 0 fix)");
  if (r == 1 && ev.kind == CAPY_GFX_EV_KEY_DOWN && ev.code == 65u &&
      ev.mods == 0x2u)
    PASS();
  else
    FAIL("key event was not delivered/translated");
  fixture_teardown();
}

/* 3. A scroll event addressed to the window translates to CAPY_GFX_EV_SCROLL,
 *    preserving dy and reporting window-local coordinates (x - frame.x, ...). */
static void test_poll_translates_scroll(void) {
  struct gui_window *win = fixture_window();
  struct capy_gfx_event ev;
  struct gui_event e;
  int r;
  if (!win) {
    TEST("backend scroll: fixture");
    FAIL("no window");
    fixture_teardown();
    return;
  }
  memset(&e, 0, sizeof(e));
  e.type = GUI_EVENT_MOUSE_SCROLL;
  e.window_id = win->id;
  e.mouse.x = win->frame.x + 5;
  e.mouse.y = win->frame.y + 7;
  e.mouse.dy = -3; /* wheel delta the browser scrolls by */
  gui_event_push(&e);
  memset(&ev, 0, sizeof(ev));
  r = g_ops->poll_event((int32_t)win->id, &ev);
  TEST("backend: MOUSE_SCROLL -> SCROLL, dy preserved, local coords");
  if (r == 1 && ev.kind == CAPY_GFX_EV_SCROLL && ev.dy == -3 && ev.x == 5 &&
      ev.y == 7)
    PASS();
  else
    FAIL("scroll not translated as expected");
  fixture_teardown();
}

/* 4. A queued WINDOW_CLOSE for a still-live window translates to CLOSE (the
 *    switch path, distinct from the window-gone shortcut in test 5). */
static void test_poll_translates_queued_close(void) {
  struct gui_window *win = fixture_window();
  struct capy_gfx_event ev;
  int r;
  if (!win) {
    TEST("backend queued close: fixture");
    FAIL("no window");
    fixture_teardown();
    return;
  }
  gui_event_push_window_close(win->id, 0u);
  memset(&ev, 0, sizeof(ev));
  r = g_ops->poll_event((int32_t)win->id, &ev);
  TEST("backend: queued WINDOW_CLOSE for a live window -> CLOSE");
  if (r == 1 && ev.kind == CAPY_GFX_EV_CLOSE)
    PASS();
  else
    FAIL("queued close was not translated");
  fixture_teardown();
}

/* 5. Once the compositor window is destroyed, EVERY poll returns CLOSE,
 *    repeatably and independent of the queue -- the belt-and-suspenders close
 *    guarantee that made capygfx reliably exit on the desktop's X button. */
static void test_poll_window_gone_returns_close(void) {
  struct gui_window *win = fixture_window();
  struct capy_gfx_event ev1, ev2;
  uint32_t id;
  int r1, r2;
  if (!win) {
    TEST("backend window-gone: fixture");
    FAIL("no window");
    fixture_teardown();
    return;
  }
  id = win->id;
  compositor_destroy_window(id);
  memset(&ev1, 0, sizeof(ev1));
  memset(&ev2, 0, sizeof(ev2));
  r1 = g_ops->poll_event((int32_t)id, &ev1);
  r2 = g_ops->poll_event((int32_t)id, &ev2); /* still CLOSE on the next poll */
  TEST("backend: destroyed window -> CLOSE, repeatably");
  if (r1 == 1 && ev1.kind == CAPY_GFX_EV_CLOSE && r2 == 1 &&
      ev2.kind == CAPY_GFX_EV_CLOSE)
    PASS();
  else
    FAIL("window-gone did not report CLOSE on every poll");
  fixture_teardown();
}

/* 6. A foreign event at the FIFO head must not be stolen. A matching event
 *    behind it is delivered while the foreign event remains queued. */
static void test_poll_preserves_foreign_window(void) {
  struct gui_window *win = fixture_window();
  struct capy_gfx_event ev;
  struct gui_event foreign;
  int r;
  if (!win) {
    TEST("backend foreign filter: fixture");
    FAIL("no window");
    fixture_teardown();
    return;
  }
  gui_event_push_key(win->id + 1000u, 66u, 0u, 'B', 0u);
  gui_event_push_key(win->id, 67u, 0x4u, 'C', 0u);
  memset(&ev, 0, sizeof(ev));
  r = g_ops->poll_event((int32_t)win->id, &ev);
  TEST("backend: target event bypasses foreign head without stealing it");
  if (r == 1 && ev.kind == CAPY_GFX_EV_KEY_DOWN && ev.code == 67u &&
      ev.mods == 0x4u && gui_event_pending() == 1 &&
      gui_event_poll(&foreign) == 0 &&
      foreign.window_id == win->id + 1000u && foreign.key.keycode == 66u)
    PASS();
  else
    FAIL("foreign event was consumed/reordered or target was not delivered");
  fixture_teardown();
}

/* 7. NULL out pointer -> -1 (fail-closed before touching the queue). */
static void test_poll_null_out(void) {
  struct gui_window *win = fixture_window();
  TEST("backend: NULL out -> -1");
  if (win && g_ops->poll_event((int32_t)win->id, (struct capy_gfx_event *)0) ==
                 -1)
    PASS();
  else
    FAIL("NULL out was not rejected");
  fixture_teardown();
}

static void test_focus_restores_existing_window(void) {
  struct gui_window *win = fixture_window();
  uint32_t id;
  if (!win) {
    TEST("backend focus: fixture");
    FAIL("no window");
    fixture_teardown();
    return;
  }
  id = win->id;
  compositor_minimize_window(id);
  TEST("backend: focus hook restores and raises existing window");
  if (g_ops && g_ops->win_focus) g_ops->win_focus((int32_t)id);
  win = compositor_get_window(id);
  if (win && win->visible && !win->minimized &&
      compositor_focused_window() == win)
    PASS();
  else
    FAIL("focus hook did not restore/focus window");
  fixture_teardown();
}

int test_syscall_gfx_backend_run(void) {
  printf("[test_syscall_gfx_backend]\n");
  tests_run = 0;
  tests_passed = 0;
  test_poll_empty_returns_zero();
  test_poll_delivers_key();
  test_poll_translates_scroll();
  test_poll_translates_queued_close();
  test_poll_window_gone_returns_close();
  test_poll_preserves_foreign_window();
  test_poll_null_out();
  test_focus_restores_existing_window();
  printf("  -> %d/%d passed\n", tests_passed, tests_run);
  return tests_run - tests_passed;
}
