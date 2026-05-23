#include <stdio.h>

#include "drivers/input/keyboard_layout.h"
#include "gui/context_menu.h"
#include "gui/compositor.h"
#include "gui/core/internal/compositor_internal.h"
#include "gui/event.h"
#include "gui/inline_prompt.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { tests_run++; printf("  %-58s ", name); } while (0)
#define PASS() do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while (0)

static void reset_fixture(void) {
  static uint32_t framebuffer[320 * 200];
  for (uint32_t i = 0u; i < 320u * 200u; ++i) framebuffer[i] = 0u;
  gui_event_init();
  compositor_init(framebuffer, 320u, 200u, 320u * 4u);
}

static void shutdown_fixture(void) {
  context_menu_close();
  inline_prompt_close();
  compositor_shutdown();
  gui_event_init();
}

static void test_context_menu_hover_uses_row_damage(void) {
  struct context_menu_item items[2] = {
    { "Open", 1u, 1u, 0u },
    { "Rename", 2u, 1u, 0u }
  };
  struct gui_event out;
  reset_fixture();
  if (context_menu_show(items, 2u, 10, 20, 0, 0) != 0) {
    TEST("context menu damage: fixture creates popup");
    FAIL("show");
    shutdown_fixture();
    return;
  }
  compositor_render();
  gui_event_flush();
  context_menu_handle_hover(12, 24);
  TEST("context menu hover invalidates only hovered row");
  if (gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_PAINT &&
      comp_dirty_rect_count == 1u &&
      comp_dirty_rects[0].x == 10 &&
      comp_dirty_rects[0].y == 22 &&
      comp_dirty_rects[0].width == CONTEXT_MENU_WIDTH &&
      comp_dirty_rects[0].height == CONTEXT_MENU_ITEM_H &&
      comp_full_redraw_pending == 0) PASS();
  else FAIL("context hover dirty");
  shutdown_fixture();
}

static void test_inline_prompt_key_uses_input_damage(void) {
  struct gui_event out;
  reset_fixture();
  if (inline_prompt_show("Name", "", 20, 15, 0, 0) != 0) {
    TEST("inline prompt damage: fixture creates popup");
    FAIL("show");
    shutdown_fixture();
    return;
  }
  compositor_render();
  gui_event_flush();
  TEST("inline prompt key invalidates only input box");
  if (inline_prompt_handle_key(KEY_NONE, 'a') == 1 &&
      gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_PAINT &&
      comp_dirty_rect_count == 1u &&
      comp_dirty_rects[0].x == 28 &&
      comp_dirty_rects[0].y == 39 &&
      comp_dirty_rects[0].width == INLINE_PROMPT_WIDTH - 16u &&
      comp_dirty_rects[0].height == 18u &&
      comp_full_redraw_pending == 0) PASS();
  else FAIL("prompt input dirty");
  shutdown_fixture();
}

int test_overlay_damage_run(void) {
  tests_run = 0;
  tests_passed = 0;
  printf("\n[gui] Overlay damage tests\n");
  test_context_menu_hover_uses_row_damage();
  test_inline_prompt_key_uses_input_damage();
  printf("[gui] Overlay damage: %d/%d passed\n", tests_passed, tests_run);
  return tests_passed == tests_run ? 0 : 1;
}
