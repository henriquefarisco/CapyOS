#include <stdio.h>

#include "drivers/input/keyboard_layout.h"
#include "gui/context_menu.h"
#include "gui/compositor.h"
#include "gui/core/internal/compositor_internal.h"
#include "gui/event.h"
#include "gui/font.h"
#include "gui/inline_prompt.h"

static int tests_run = 0;
static int tests_passed = 0;
static char g_secret_submit_text[INLINE_PROMPT_TEXT_MAX];
static int g_secret_submit_called = 0;
static uint16_t g_context_pick_action = 0u;
static int g_context_pick_called = 0;

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

static int surface_has_color(const struct gui_surface *surface,
                             uint32_t color) {
  if (!surface || !surface->pixels) return 0;
  for (uint32_t y = 0u; y < surface->height; ++y) {
    for (uint32_t x = 0u; x < surface->width; ++x) {
      if (surface->pixels[y * surface->width + x] == color) return 1;
    }
  }
  return 0;
}

static int text_equals(const char *a, const char *b) {
  uint32_t i = 0u;
  if (!a || !b) return 0;
  while (a[i] && b[i]) {
    if (a[i] != b[i]) return 0;
    i++;
  }
  return a[i] == b[i];
}

static void secret_submit_cb(const char *text, void *ctx) {
  uint32_t i = 0u;
  (void)ctx;
  g_secret_submit_called++;
  while (text && text[i] && i + 1u < INLINE_PROMPT_TEXT_MAX) {
    g_secret_submit_text[i] = text[i];
    i++;
  }
  g_secret_submit_text[i] = '\0';
}

static void context_pick_cb(uint16_t action_id, void *ctx) {
  (void)ctx;
  g_context_pick_called++;
  g_context_pick_action = action_id;
}

static void test_context_menu_hover_uses_row_damage(void) {
  struct context_menu_item items[2] = {
    { "Open", 1u, 1u, 0u },
    { "Rename", 2u, 1u, 0u }
  };
  struct gui_event out;
  struct gui_window *popup;
  reset_fixture();
  if (context_menu_show(items, 2u, 10, 20, 0, 0) != 0) {
    TEST("context menu damage: fixture creates popup");
    FAIL("show");
    shutdown_fixture();
    return;
  }
  popup = compositor_window_at(12, 24);
  if (popup && popup->surface.pixels) popup->surface.pixels[0] = 0x00ABCDEFu;
  compositor_render();
  TEST("context menu paint refreshes popup surface");
  if (popup && popup->surface.pixels &&
      popup->surface.pixels[0] == compositor_theme()->window_bg) PASS();
  else FAIL("context paint");
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
  compositor_render();
  TEST("context menu hover repaint marks selected row");
  if (popup && popup->surface.pixels &&
      popup->surface.pixels[2u * popup->surface.width + 2u] ==
          compositor_theme()->accent) PASS();
  else FAIL("context hover repaint");
  shutdown_fixture();
}

static void test_context_menu_disabled_and_separator_do_not_hover(void) {
  struct context_menu_item items[3] = {
    { "Disabled", 1u, 0u, 0u },
    { "", 0u, 0u, 0u },
    { "Open", 2u, 1u, 0u }
  };
  struct gui_event out;
  struct gui_window *popup;
  uint32_t accent_count = 0u;
  reset_fixture();
  if (context_menu_show(items, 3u, 10, 20, 0, 0) != 0) {
    TEST("context menu disabled: fixture creates popup");
    FAIL("show");
    shutdown_fixture();
    return;
  }
  popup = compositor_window_at(12, 24);
  compositor_render();
  gui_event_flush();
  context_menu_handle_hover(12, 24);
  context_menu_handle_hover(12, 46);
  TEST("context menu disabled/separator hover stays inert");
  if (gui_event_pending() == 0 &&
      comp_dirty_rect_count == 0u &&
      comp_full_redraw_pending == 0) PASS();
  else FAIL("disabled separator hover");
  if (popup && popup->surface.pixels) {
    for (uint32_t y = 0u; y < popup->surface.height; ++y) {
      for (uint32_t x = 0u; x < popup->surface.width; ++x) {
        if (popup->surface.pixels[y * popup->surface.width + x] ==
            compositor_theme()->accent) {
          accent_count++;
        }
      }
    }
  }
  TEST("context menu disabled/separator paint has no hover accent");
  if (accent_count == 0u) PASS();
  else FAIL("disabled separator paint");
  TEST("context menu separator line is rendered");
  if (popup && popup->surface.pixels &&
      popup->surface.pixels[30u * popup->surface.width + 8u] ==
          compositor_theme()->window_border) PASS();
  else FAIL("separator paint");
  context_menu_handle_hover(12, 56);
  TEST("context menu enabled row after separator invalidates");
  if (gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_PAINT &&
      comp_dirty_rect_count == 1u &&
      comp_dirty_rects[0].x == 10 &&
      comp_dirty_rects[0].y == 54 &&
      comp_dirty_rects[0].width == CONTEXT_MENU_WIDTH &&
      comp_dirty_rects[0].height == CONTEXT_MENU_ITEM_H) PASS();
  else FAIL("enabled after separator hover");
  shutdown_fixture();
}

static void test_context_menu_click_ignores_disabled_and_separator(void) {
  struct context_menu_item items[3] = {
    { "Disabled", 1u, 0u, 0u },
    { "", 0u, 0u, 0u },
    { "Open", 2u, 1u, 0u }
  };
  reset_fixture();
  g_context_pick_action = 0u;
  g_context_pick_called = 0;
  if (context_menu_show(items, 3u, 10, 20, context_pick_cb, 0) != 0) {
    TEST("context menu click: fixture creates popup");
    FAIL("show");
    shutdown_fixture();
    return;
  }
  TEST("context menu disabled click is consumed without callback");
  if (context_menu_handle_click(12, 24) == 1 &&
      context_menu_is_open() &&
      g_context_pick_called == 0) PASS();
  else FAIL("disabled click");
  TEST("context menu separator click is consumed without callback");
  if (context_menu_handle_click(12, 48) == 1 &&
      context_menu_is_open() &&
      g_context_pick_called == 0) PASS();
  else FAIL("separator click");
  TEST("context menu enabled click invokes callback and closes");
  if (context_menu_handle_click(12, 56) == 1 &&
      !context_menu_is_open() &&
      g_context_pick_called == 1 &&
      g_context_pick_action == 2u) PASS();
  else FAIL("enabled click");
  shutdown_fixture();
}

static void test_context_menu_hover_outside_clears_row(void) {
  struct context_menu_item items[2] = {
    { "Open", 1u, 1u, 0u },
    { "Rename", 2u, 1u, 0u }
  };
  struct gui_event out;
  struct gui_window *popup;
  reset_fixture();
  if (context_menu_show(items, 2u, 10, 20, 0, 0) != 0) {
    TEST("context menu hover outside: fixture creates popup");
    FAIL("show");
    shutdown_fixture();
    return;
  }
  popup = compositor_window_at(12, 24);
  compositor_render();
  gui_event_flush();
  context_menu_handle_hover(12, 24);
  compositor_render();
  gui_event_flush();
  TEST("context menu hover outside invalidates old row only");
  if (context_menu_is_open() &&
      popup && popup->surface.pixels &&
      popup->surface.pixels[2u * popup->surface.width + 2u] ==
          compositor_theme()->accent &&
      (context_menu_handle_hover(1, 1), gui_event_poll(&out) == 0) &&
      out.type == GUI_EVENT_PAINT &&
      comp_dirty_rect_count == 1u &&
      comp_dirty_rects[0].x == 10 &&
      comp_dirty_rects[0].y == 22 &&
      comp_dirty_rects[0].width == CONTEXT_MENU_WIDTH &&
      comp_dirty_rects[0].height == CONTEXT_MENU_ITEM_H &&
      comp_full_redraw_pending == 0) PASS();
  else FAIL("context hover outside dirty");
  compositor_render();
  TEST("context menu hover outside repaint clears accent");
  if (popup && popup->surface.pixels &&
      popup->surface.pixels[2u * popup->surface.width + 2u] ==
          compositor_theme()->window_bg) PASS();
  else FAIL("context hover outside repaint");
  shutdown_fixture();
}

static void test_context_menu_outside_click_closes_without_callback(void) {
  struct context_menu_item items[1] = {
    { "Open", 1u, 1u, 0u }
  };
  reset_fixture();
  g_context_pick_action = 0u;
  g_context_pick_called = 0;
  if (context_menu_show(items, 1u, 10, 20, context_pick_cb, 0) != 0) {
    TEST("context menu outside click: fixture creates popup");
    FAIL("show");
    shutdown_fixture();
    return;
  }
  compositor_render();
  gui_event_flush();
  TEST("context menu outside click closes without callback");
  if (context_menu_handle_click(1, 1) == 1 &&
      !context_menu_is_open() &&
      g_context_pick_called == 0 &&
      g_context_pick_action == 0u &&
      context_menu_handle_click(1, 1) == 0) PASS();
  else FAIL("context outside click");
  shutdown_fixture();
}

static void test_context_menu_clamps_to_screen_bounds(void) {
  struct context_menu_item items[2] = {
    { "Open", 1u, 1u, 0u },
    { "Rename", 2u, 1u, 0u }
  };
  struct gui_window *popup;
  reset_fixture();
  g_context_pick_action = 0u;
  g_context_pick_called = 0;
  if (context_menu_show(items, 2u, 1000, 1000, context_pick_cb, 0) != 0) {
    TEST("context menu clamp: fixture creates popup");
    FAIL("show");
    shutdown_fixture();
    return;
  }
  popup = compositor_window_at(162, 150);
  if (popup && popup->surface.pixels) popup->surface.pixels[0] = 0x00ABCDEFu;
  compositor_render();
  TEST("context menu show clamps to screen bounds");
  if (popup &&
      popup->frame.x == 160 &&
      popup->frame.y == 148 &&
      popup->frame.width == CONTEXT_MENU_WIDTH &&
      popup->frame.height == 2u * CONTEXT_MENU_ITEM_H + 4u &&
      popup->surface.pixels &&
      popup->surface.pixels[0] == compositor_theme()->window_bg) PASS();
  else FAIL("context clamp");
  TEST("context menu clamped click uses adjusted coordinates");
  if (context_menu_handle_click(172, 152) == 1 &&
      !context_menu_is_open() &&
      g_context_pick_called == 1 &&
      g_context_pick_action == 1u) PASS();
  else FAIL("context clamp click");
  shutdown_fixture();
}

static void test_context_menu_truncates_to_max_items(void) {
  struct context_menu_item items[10] = {
    { "One", 1u, 1u, 0u },
    { "Two", 2u, 1u, 0u },
    { "Three", 3u, 1u, 0u },
    { "Four", 4u, 1u, 0u },
    { "Five", 5u, 1u, 0u },
    { "Six", 6u, 1u, 0u },
    { "Seven", 7u, 1u, 0u },
    { "Eight", 8u, 1u, 0u },
    { "Nine", 9u, 1u, 0u },
    { "Ten", 10u, 1u, 0u }
  };
  struct gui_window *popup;
  reset_fixture();
  g_context_pick_action = 0u;
  g_context_pick_called = 0;
  if (context_menu_show(items, 10u, 10, 0, context_pick_cb, 0) != 0) {
    TEST("context menu truncation: fixture creates popup");
    FAIL("show");
    shutdown_fixture();
    return;
  }
  popup = compositor_window_at(12, 2);
  compositor_render();
  TEST("context menu truncates item count to max");
  if (popup &&
      popup->frame.height == 4u + CONTEXT_MENU_MAX_ITEMS * CONTEXT_MENU_ITEM_H &&
      popup->surface.pixels &&
      popup->surface.pixels[0] == compositor_theme()->window_bg) PASS();
  else FAIL("context truncation height");
  TEST("context menu truncated list keeps last allowed item clickable");
  if (context_menu_handle_click(22, 172) == 1 &&
      !context_menu_is_open() &&
      g_context_pick_called == 1 &&
      g_context_pick_action == 8u) PASS();
  else FAIL("context truncation click");
  shutdown_fixture();
}

static void test_context_menu_truncates_long_label_without_losing_click(void) {
  struct context_menu_item items[1] = {
    { "VeryLongContextMenuLabelName", 42u, 1u, 0u }
  };
  struct gui_window *popup;
  reset_fixture();
  g_context_pick_action = 0u;
  g_context_pick_called = 0;
  if (context_menu_show(items, 1u, 10, 20, context_pick_cb, 0) != 0) {
    TEST("context menu long label: fixture creates popup");
    FAIL("show");
    shutdown_fixture();
    return;
  }
  popup = compositor_window_at(12, 24);
  compositor_render();
  TEST("context menu truncates long label");
  if (popup && popup->surface.pixels &&
      surface_has_color(&popup->surface, compositor_theme()->text)) PASS();
  else FAIL("context long label paint");
  TEST("context menu long label remains clickable");
  if (context_menu_handle_click(12, 24) == 1 &&
      !context_menu_is_open() &&
      g_context_pick_called == 1 &&
      g_context_pick_action == 42u) PASS();
  else FAIL("context long label click");
  shutdown_fixture();
}

static void test_context_menu_reopen_refreshes_surface(void) {
  struct context_menu_item first[1] = {
    { "First", 1u, 1u, 0u }
  };
  struct context_menu_item second[1] = {
    { "Second", 2u, 1u, 0u }
  };
  struct gui_window *popup;
  reset_fixture();
  if (context_menu_show(first, 1u, 10, 20, 0, 0) != 0) {
    TEST("context menu reopen: fixture creates first popup");
    FAIL("show first");
    shutdown_fixture();
    return;
  }
  compositor_render();
  context_menu_close();
  if (context_menu_show(second, 1u, 40, 30, 0, 0) != 0) {
    TEST("context menu reopen: fixture creates second popup");
    FAIL("show second");
    shutdown_fixture();
    return;
  }
  popup = compositor_window_at(42, 32);
  if (popup && popup->surface.pixels) popup->surface.pixels[0] = 0x00ABCDEFu;
  compositor_render();
  TEST("context menu reopen refreshes popup surface");
  if (popup && popup->surface.pixels &&
      popup->surface.pixels[0] == compositor_theme()->window_bg) PASS();
  else FAIL("context reopen paint");
  shutdown_fixture();
}

static void test_inline_prompt_key_uses_input_damage(void) {
  struct gui_event out;
  struct gui_window *prompt;
  reset_fixture();
  if (inline_prompt_show("Name", "", 20, 15, 0, 0) != 0) {
    TEST("inline prompt damage: fixture creates popup");
    FAIL("show");
    shutdown_fixture();
    return;
  }
  prompt = compositor_window_at(22, 17);
  if (prompt && prompt->surface.pixels) prompt->surface.pixels[0] = 0x00ABCDEFu;
  compositor_render();
  TEST("inline prompt paint refreshes popup surface");
  if (prompt && prompt->surface.pixels &&
      prompt->surface.pixels[0] == compositor_theme()->accent) PASS();
  else FAIL("prompt paint");
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
  compositor_render();
  TEST("inline prompt key repaint draws input text");
  if (prompt && prompt->surface.pixels &&
      surface_has_color(&prompt->surface, compositor_theme()->terminal_fg)) PASS();
  else FAIL("prompt input repaint");
  shutdown_fixture();
}

static void test_inline_prompt_home_delete_uses_input_damage(void) {
  struct gui_event out;
  reset_fixture();
  g_secret_submit_text[0] = '\0';
  g_secret_submit_called = 0;
  if (inline_prompt_show("Name", "abc", 20, 15, secret_submit_cb, 0) != 0) {
    TEST("inline prompt edit keys: fixture creates popup");
    FAIL("show");
    shutdown_fixture();
    return;
  }
  compositor_render();
  gui_event_flush();
  TEST("inline prompt HOME invalidates input box");
  if (inline_prompt_handle_key(KEY_HOME, 0) == 1 &&
      gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_PAINT &&
      comp_dirty_rect_count == 1u &&
      comp_dirty_rects[0].x == 28 &&
      comp_dirty_rects[0].y == 39 &&
      comp_dirty_rects[0].width == INLINE_PROMPT_WIDTH - 16u &&
      comp_dirty_rects[0].height == 18u &&
      comp_full_redraw_pending == 0) PASS();
  else FAIL("prompt home dirty");
  compositor_render();
  gui_event_flush();
  TEST("inline prompt DELETE invalidates input box");
  if (inline_prompt_handle_key(KEY_DELETE, 0) == 1 &&
      gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_PAINT &&
      comp_dirty_rect_count == 1u &&
      comp_dirty_rects[0].x == 28 &&
      comp_dirty_rects[0].y == 39 &&
      comp_dirty_rects[0].width == INLINE_PROMPT_WIDTH - 16u &&
      comp_dirty_rects[0].height == 18u &&
      comp_full_redraw_pending == 0) PASS();
  else FAIL("prompt delete dirty");
  compositor_render();
  gui_event_flush();
  TEST("inline prompt edit keys submit modified value");
  if (inline_prompt_handle_key(KEY_NONE, 'Z') == 1 &&
      inline_prompt_handle_key(KEY_NONE, '\n') == 1 &&
      g_secret_submit_called == 1 &&
      text_equals(g_secret_submit_text, "Zbc")) PASS();
  else FAIL("prompt edit submit");
  shutdown_fixture();
}

static void test_inline_prompt_reopen_refreshes_surface(void) {
  struct gui_window *prompt;
  reset_fixture();
  if (inline_prompt_show("First", "aa", 20, 15, 0, 0) != 0) {
    TEST("inline prompt reopen: fixture creates first popup");
    FAIL("show first");
    shutdown_fixture();
    return;
  }
  compositor_render();
  inline_prompt_close();
  if (inline_prompt_show("Second", "bb", 50, 25, 0, 0) != 0) {
    TEST("inline prompt reopen: fixture creates second popup");
    FAIL("show second");
    shutdown_fixture();
    return;
  }
  prompt = compositor_window_at(52, 27);
  if (prompt && prompt->surface.pixels) prompt->surface.pixels[0] = 0x00ABCDEFu;
  compositor_render();
  TEST("inline prompt reopen refreshes popup surface");
  if (prompt && prompt->surface.pixels &&
      prompt->surface.pixels[0] == compositor_theme()->accent) PASS();
  else FAIL("prompt reopen paint");
  shutdown_fixture();
}

static void test_inline_prompt_click_moves_caret(void) {
  struct gui_event out;
  struct gui_window *prompt;
  reset_fixture();
  g_secret_submit_text[0] = '\0';
  g_secret_submit_called = 0;
  if (inline_prompt_show("Name", "ab", 20, 15, secret_submit_cb, 0) != 0) {
    TEST("inline prompt click: fixture creates popup");
    FAIL("show");
    shutdown_fixture();
    return;
  }
  prompt = compositor_window_at(22, 17);
  compositor_render();
  gui_event_flush();
  TEST("inline prompt click invalidates only input box");
  if (inline_prompt_handle_click(28, 40) == 1 &&
      gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_PAINT &&
      comp_dirty_rect_count == 1u &&
      comp_dirty_rects[0].x == 28 &&
      comp_dirty_rects[0].y == 39 &&
      comp_dirty_rects[0].width == INLINE_PROMPT_WIDTH - 16u &&
      comp_dirty_rects[0].height == 18u &&
      comp_full_redraw_pending == 0) PASS();
  else FAIL("prompt click dirty");
  compositor_render();
  TEST("inline prompt click repaint moves caret");
  if (prompt && prompt->surface.pixels &&
      prompt->surface.pixels[27u * prompt->surface.width + 12u] ==
          compositor_theme()->accent) PASS();
  else FAIL("prompt click caret");
  gui_event_flush();
  TEST("inline prompt click inserts at caret");
  if (inline_prompt_handle_key(KEY_NONE, 'Z') == 1 &&
      inline_prompt_handle_key(KEY_NONE, '\n') == 1 &&
      g_secret_submit_called == 1 &&
      text_equals(g_secret_submit_text, "Zab")) PASS();
  else FAIL("prompt click insert");
  shutdown_fixture();
}

static void test_inline_prompt_long_text_keeps_tail_visible(void) {
  const char *initial = "abcdefghijklmnopqrstuvwxyz0123456789";
  struct gui_window *prompt;
  const struct font *f;
  uint32_t visible_chars;
  uint32_t initial_len = 0u;
  uint32_t start;
  uint32_t caret_x;
  reset_fixture();
  g_secret_submit_text[0] = '\0';
  g_secret_submit_called = 0;
  while (initial[initial_len]) initial_len++;
  if (inline_prompt_show("Name", initial, 20, 15, secret_submit_cb, 0) != 0) {
    TEST("inline prompt long text: fixture creates popup");
    FAIL("show");
    shutdown_fixture();
    return;
  }
  prompt = compositor_window_at(22, 17);
  compositor_render();
  f = font_default();
  TEST("inline prompt long text keeps tail caret visible");
  if (prompt && prompt->surface.pixels && f && f->glyph_width) {
    visible_chars = (INLINE_PROMPT_WIDTH - 24u) / f->glyph_width;
    start = initial_len > visible_chars ? initial_len - visible_chars : 0u;
    caret_x = 12u + (initial_len - start) * f->glyph_width;
    if (caret_x < INLINE_PROMPT_WIDTH &&
        prompt->surface.pixels[27u * prompt->surface.width + caret_x] ==
            compositor_theme()->accent) PASS();
    else FAIL("prompt long caret");
  } else {
    FAIL("fixture");
  }
  TEST("inline prompt long text inserts at tail");
  if (inline_prompt_handle_key(KEY_NONE, 'Z') == 1 &&
      inline_prompt_handle_key(KEY_NONE, '\n') == 1 &&
      g_secret_submit_called == 1 &&
      text_equals(g_secret_submit_text,
                  "abcdefghijklmnopqrstuvwxyz0123456789Z")) PASS();
  else FAIL("prompt long insert");
  shutdown_fixture();
}

static void test_inline_prompt_non_input_click_stays_inert(void) {
  reset_fixture();
  g_secret_submit_text[0] = '\0';
  g_secret_submit_called = 0;
  if (inline_prompt_show("Name", "ab", 20, 15, secret_submit_cb, 0) != 0) {
    TEST("inline prompt non-input click: fixture creates popup");
    FAIL("show");
    shutdown_fixture();
    return;
  }
  compositor_render();
  gui_event_flush();
  comp_dirty_rect_count = 0u;
  comp_full_redraw_pending = 0;
  TEST("inline prompt non-input click does not invalidate");
  if (inline_prompt_handle_click(22, 17) == 1 &&
      inline_prompt_is_open() &&
      gui_event_pending() == 0 &&
      comp_dirty_rect_count == 0u &&
      comp_full_redraw_pending == 0) PASS();
  else FAIL("prompt non-input click");
  TEST("inline prompt non-input click leaves caret unchanged");
  if (inline_prompt_handle_key(KEY_NONE, 'Z') == 1 &&
      inline_prompt_handle_key(KEY_NONE, '\n') == 1 &&
      g_secret_submit_called == 1 &&
      text_equals(g_secret_submit_text, "abZ")) PASS();
  else FAIL("prompt non-input caret");
  shutdown_fixture();
}

static void test_inline_prompt_outside_click_closes_without_submit(void) {
  reset_fixture();
  g_secret_submit_text[0] = '\0';
  g_secret_submit_called = 0;
  if (inline_prompt_show("Name", "ab", 20, 15, secret_submit_cb, 0) != 0) {
    TEST("inline prompt outside click: fixture creates popup");
    FAIL("show");
    shutdown_fixture();
    return;
  }
  compositor_render();
  gui_event_flush();
  TEST("inline prompt outside click closes without submit");
  if (inline_prompt_handle_click(1, 1) == 1 &&
      !inline_prompt_is_open() &&
      g_secret_submit_called == 0 &&
      inline_prompt_handle_key(KEY_NONE, '\n') == 0) PASS();
  else FAIL("prompt outside click");
  shutdown_fixture();
}

static void test_inline_prompt_secret_uses_input_damage(void) {
  struct gui_event out;
  struct gui_window *prompt;
  reset_fixture();
  g_secret_submit_text[0] = '\0';
  g_secret_submit_called = 0;
  if (inline_prompt_show_secret("Key", "ab", 20, 15,
                                secret_submit_cb, 0) != 0) {
    TEST("inline prompt secret damage: fixture creates popup");
    FAIL("show");
    shutdown_fixture();
    return;
  }
  prompt = compositor_window_at(22, 17);
  compositor_render();
  TEST("inline prompt secret paint draws masked text");
  if (prompt && prompt->surface.pixels &&
      surface_has_color(&prompt->surface, compositor_theme()->terminal_fg)) PASS();
  else FAIL("secret prompt paint");
  gui_event_flush();
  TEST("inline prompt secret key invalidates input box");
  if (inline_prompt_handle_key(KEY_NONE, 'c') == 1 &&
      gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_PAINT &&
      comp_dirty_rect_count == 1u &&
      comp_dirty_rects[0].x == 28 &&
      comp_dirty_rects[0].y == 39 &&
      comp_dirty_rects[0].width == INLINE_PROMPT_WIDTH - 16u &&
      comp_dirty_rects[0].height == 18u &&
      comp_full_redraw_pending == 0) PASS();
  else FAIL("secret prompt input dirty");
  compositor_render();
  TEST("inline prompt secret submit preserves typed value");
  if (inline_prompt_handle_key(KEY_NONE, '\n') == 1 &&
      g_secret_submit_called == 1 &&
      text_equals(g_secret_submit_text, "abc")) PASS();
  else FAIL("secret prompt submit");
  shutdown_fixture();
}

int test_overlay_damage_run(void) {
  tests_run = 0;
  tests_passed = 0;
  printf("\n[gui] Overlay damage tests\n");
  test_context_menu_hover_uses_row_damage();
  test_context_menu_disabled_and_separator_do_not_hover();
  test_context_menu_click_ignores_disabled_and_separator();
  test_context_menu_hover_outside_clears_row();
  test_context_menu_outside_click_closes_without_callback();
  test_context_menu_clamps_to_screen_bounds();
  test_context_menu_truncates_to_max_items();
  test_context_menu_truncates_long_label_without_losing_click();
  test_context_menu_reopen_refreshes_surface();
  test_inline_prompt_key_uses_input_damage();
  test_inline_prompt_home_delete_uses_input_damage();
  test_inline_prompt_reopen_refreshes_surface();
  test_inline_prompt_click_moves_caret();
  test_inline_prompt_long_text_keeps_tail_visible();
  test_inline_prompt_non_input_click_stays_inert();
  test_inline_prompt_outside_click_closes_without_submit();
  test_inline_prompt_secret_uses_input_damage();
  printf("[gui] Overlay damage: %d/%d passed\n", tests_passed, tests_run);
  return tests_passed == tests_run ? 0 : 1;
}
