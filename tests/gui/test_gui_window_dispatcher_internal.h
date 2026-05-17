/*
 * tests/test_gui_window_dispatcher_internal.h
 *
 * Shared TEST/PASS/FAIL macros, run/pass counter externs, callback
 * counter externs, callback function declarations and fixture
 * lifecycle helpers used by the `test_gui_window_dispatcher` host
 * test. Created at the 2026-05-15 monolith refactor when the single
 * file `tests/test_gui_window_dispatcher.c` (985 LOC) was split into
 * `tests/test_gui_window_dispatcher.c` (fixture defs + entry + first
 * batch of tests) and `tests/test_gui_window_dispatcher_lifecycle.c`
 * (snapshot/lifecycle/compositor/context-menu/timer/miss coverage).
 *
 * Not a public test interface — only the two
 * `test_gui_window_dispatcher*.c` files include this header.
 */
#ifndef TEST_GUI_WINDOW_DISPATCHER_INTERNAL_H
#define TEST_GUI_WINDOW_DISPATCHER_INTERNAL_H

#include <stdio.h>

#include "drivers/input/mouse.h"
#include "gui/compositor.h"
#include "gui/event.h"
#include "gui/window_dispatcher.h"

extern int test_gui_window_dispatcher_runs;
extern int test_gui_window_dispatcher_passes;

#define TEST(name)                                                             \
  do { test_gui_window_dispatcher_runs++; printf("  %-58s ", name); } while (0)
#define PASS()                                                                 \
  do { printf("OK\n"); test_gui_window_dispatcher_passes++; } while (0)
#define FAIL(msg)                                                              \
  do { printf("FAIL: %s\n", msg); } while (0)

/* Callback counters captured by the shared callback set in the
 * fixture. Tests inspect them after dispatching events. */
extern uint32_t test_gwd_key_calls;
extern uint32_t test_gwd_key_last_code;
extern uint8_t test_gwd_key_last_mods;
extern uint32_t test_gwd_key_up_calls;
extern uint32_t test_gwd_key_up_last_code;
extern uint8_t test_gwd_key_up_last_mods;
extern uint32_t test_gwd_focus_calls;
extern uint32_t test_gwd_focus_last_id;
extern uint32_t test_gwd_blur_calls;
extern uint32_t test_gwd_blur_last_id;
extern uint32_t test_gwd_scroll_calls;
extern int32_t test_gwd_scroll_last_delta;
extern uint32_t test_gwd_paint_calls;
extern uint32_t test_gwd_hover_calls;
extern int32_t test_gwd_hover_last_x;
extern int32_t test_gwd_hover_last_y;
extern uint32_t test_gwd_mouse_calls;
extern int32_t test_gwd_mouse_last_x;
extern int32_t test_gwd_mouse_last_y;
extern uint8_t test_gwd_mouse_last_buttons;
extern uint32_t test_gwd_context_calls;
extern int32_t test_gwd_context_last_x;
extern int32_t test_gwd_context_last_y;
extern uint32_t test_gwd_timer_calls;
extern uint32_t test_gwd_timer_last_id;
extern uint32_t test_gwd_close_calls;
extern uint32_t test_gwd_resize_calls;

void test_gwd_reset_counters(void);
void test_gwd_reset_fixture(void);
void test_gwd_shutdown_fixture(void);

void test_gwd_on_key(struct gui_window *win, uint32_t keycode, uint8_t mods);
void test_gwd_on_key_up(struct gui_window *win, uint32_t keycode, uint8_t mods);
void test_gwd_on_focus(struct gui_window *win);
void test_gwd_on_blur(struct gui_window *win);
void test_gwd_on_scroll(struct gui_window *win, int32_t delta);
void test_gwd_on_paint(struct gui_window *win);
void test_gwd_on_hover(struct gui_window *win, int32_t x, int32_t y);
void test_gwd_on_mouse(struct gui_window *win, int32_t x, int32_t y,
                      uint8_t buttons);
void test_gwd_on_context_menu(struct gui_window *win, int32_t x, int32_t y);
void test_gwd_on_timer(struct gui_window *win, uint32_t timer_id);
void test_gwd_on_close(struct gui_window *win);
void test_gwd_on_resize(struct gui_window *win, uint32_t w, uint32_t h);

/* Companion file entry: runs the dispatcher tests that live in
 * `tests/test_gui_window_dispatcher_lifecycle.c`. */
void test_gui_window_dispatcher_lifecycle_cases(void);

/* Short aliases for shared counters and fixture helpers. Callback
 * symbols intentionally stay explicit so macro expansion does not
 * rewrite gui_window callback fields such as win->on_key. */
#define tests_run         test_gui_window_dispatcher_runs
#define tests_passed      test_gui_window_dispatcher_passes
#define reset_counters    test_gwd_reset_counters
#define reset_fixture     test_gwd_reset_fixture
#define shutdown_fixture  test_gwd_shutdown_fixture
#define key_calls         test_gwd_key_calls
#define key_last_code     test_gwd_key_last_code
#define key_last_mods     test_gwd_key_last_mods
#define key_up_calls      test_gwd_key_up_calls
#define key_up_last_code  test_gwd_key_up_last_code
#define key_up_last_mods  test_gwd_key_up_last_mods
#define focus_calls       test_gwd_focus_calls
#define focus_last_id     test_gwd_focus_last_id
#define blur_calls        test_gwd_blur_calls
#define blur_last_id      test_gwd_blur_last_id
#define scroll_calls      test_gwd_scroll_calls
#define scroll_last_delta test_gwd_scroll_last_delta
#define paint_calls       test_gwd_paint_calls
#define hover_calls       test_gwd_hover_calls
#define hover_last_x      test_gwd_hover_last_x
#define hover_last_y      test_gwd_hover_last_y
#define mouse_calls       test_gwd_mouse_calls
#define mouse_last_x      test_gwd_mouse_last_x
#define mouse_last_y      test_gwd_mouse_last_y
#define mouse_last_buttons test_gwd_mouse_last_buttons
#define context_calls     test_gwd_context_calls
#define context_last_x    test_gwd_context_last_x
#define context_last_y    test_gwd_context_last_y
#define timer_calls       test_gwd_timer_calls
#define timer_last_id     test_gwd_timer_last_id
#define close_calls       test_gwd_close_calls
#define resize_calls      test_gwd_resize_calls

#endif /* TEST_GUI_WINDOW_DISPATCHER_INTERNAL_H */
