/*
 * tests/test_gui_event_internal.h
 *
 * Shared TEST/PASS/FAIL macros, counter externs and `make_event`
 * helper for the `test_gui_event` host test. Created at the
 * 2026-05-15 monolith refactor when `tests/test_gui_event.c` (1085
 * LOC) was split into two translation units. The main entry
 * (`test_gui_event_run`) plus the queue/FIFO/dispatch coverage live in
 * `tests/test_gui_event.c`; helper APIs + coalescing + discard +
 * snapshot + reset + overflow coverage live in
 * `tests/test_gui_event_helpers.c`.
 *
 * Not a public test interface — only the two `test_gui_event*.c`
 * files include this header.
 */
#ifndef TEST_GUI_EVENT_INTERNAL_H
#define TEST_GUI_EVENT_INTERNAL_H

#include <stdio.h>

#include "gui/event.h"

extern int test_gui_event_runs;
extern int test_gui_event_passes;

#define TEST(name) \
  do { test_gui_event_runs++; printf("  %-58s ", name); } while (0)
#define PASS() \
  do { printf("OK\n"); test_gui_event_passes++; } while (0)
#define FAIL(msg) \
  do { printf("FAIL: %s\n", msg); } while (0)

struct gui_event test_gui_event_make_event(uint32_t id);

void test_gui_event_helper_cases(void);

#endif /* TEST_GUI_EVENT_INTERNAL_H */
