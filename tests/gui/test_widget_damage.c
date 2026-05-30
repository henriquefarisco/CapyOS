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

static int surface_has_color(struct gui_surface *surface, uint32_t color) {
  if (!surface || !surface->pixels) return 0;
  for (uint32_t y = 0u; y < surface->height; ++y) {
    for (uint32_t x = 0u; x < surface->width; ++x) {
      if (surface->pixels[y * surface->width + x] == color) return 1;
    }
  }
  return 0;
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

static void test_widget_paint_refreshes_surface(void) {
  struct gui_window *win;
  struct widget *w;
  reset_fixture();
  win = compositor_create_window("P", 8, 12, 40u, 30u);
  w = widget_create(WIDGET_BUTTON, win);
  if (!win || !w) {
    TEST("widget paint: fixture creates window/widget");
    FAIL("fixture");
    widget_destroy(w);
    shutdown_fixture();
    return;
  }
  widget_set_bounds(w, 3, 4, 10u, 6u);
  win->surface.pixels[5u * win->surface.width + 4u] = 0x00ABCDEFu;
  widget_paint(w, &win->surface);
  TEST("widget_paint refreshes widget surface");
  if (win->surface.pixels[5u * win->surface.width + 4u] ==
      w->style.bg_color) PASS();
  else FAIL("widget paint");
  widget_destroy(w);
  shutdown_fixture();
}

static void test_widget_paint_skips_invisible_widget(void) {
  struct gui_window *win;
  struct widget *w;
  reset_fixture();
  win = compositor_create_window("IP", 8, 12, 40u, 30u);
  w = widget_create(WIDGET_BUTTON, win);
  if (!win || !w) {
    TEST("widget invisible paint: fixture creates window/widget");
    FAIL("fixture");
    widget_destroy(w);
    shutdown_fixture();
    return;
  }
  widget_set_bounds(w, 3, 4, 10u, 6u);
  widget_set_visible(w, 0);
  win->surface.pixels[5u * win->surface.width + 4u] = 0x00ABCDEFu;
  widget_paint(w, &win->surface);
  TEST("widget_paint skips invisible widget");
  if (win->surface.pixels[5u * win->surface.width + 4u] ==
      0x00ABCDEFu) PASS();
  else FAIL("widget invisible paint");
  widget_destroy(w);
  shutdown_fixture();
}

static void test_widget_paint_renders_hovered_state(void) {
  struct gui_window *win;
  struct widget *w;
  struct widget_style style;
  uint32_t hover_color = 0x0000AAEEu;
  reset_fixture();
  win = compositor_create_window("HP", 8, 12, 40u, 30u);
  w = widget_create(WIDGET_BUTTON, win);
  if (!win || !w) {
    TEST("widget hover paint: fixture creates window/widget");
    FAIL("fixture");
    widget_destroy(w);
    shutdown_fixture();
    return;
  }
  widget_set_bounds(w, 3, 4, 10u, 6u);
  style = w->style;
  style.hover_color = hover_color;
  style.border_width = 0u;
  widget_set_style(w, &style);
  w->hovered = 1;
  win->surface.pixels[5u * win->surface.width + 4u] = 0x00ABCDEFu;
  widget_paint(w, &win->surface);
  TEST("widget_paint renders hovered state");
  if (win->surface.pixels[5u * win->surface.width + 4u] ==
      hover_color) PASS();
  else FAIL("widget hover paint");
  widget_destroy(w);
  shutdown_fixture();
}

static void test_widget_paint_dims_disabled_root(void) {
  struct gui_window *win;
  struct widget *w;
  struct widget_style style;
  reset_fixture();
  win = compositor_create_window("DP", 8, 12, 40u, 30u);
  w = widget_create(WIDGET_BUTTON, win);
  if (!win || !w) {
    TEST("widget disabled root paint: fixture creates window/widget");
    FAIL("fixture");
    widget_destroy(w);
    shutdown_fixture();
    return;
  }
  widget_set_bounds(w, 3, 4, 10u, 6u);
  style = w->style;
  style.bg_color = 0x000055AAu;
  style.border_width = 0u;
  widget_set_style(w, &style);
  widget_set_enabled(w, 0);
  win->surface.pixels[5u * win->surface.width + 4u] = 0x00ABCDEFu;
  widget_paint(w, &win->surface);
  TEST("widget_paint dims disabled root");
  if (win->surface.pixels[5u * win->surface.width + 4u] ==
      0x00C0C0C0u) PASS();
  else FAIL("disabled root paint");
  widget_destroy(w);
  shutdown_fixture();
}

static void test_widget_paint_truncates_narrow_button_text(void) {
  struct gui_window *win;
  struct widget *w;
  struct widget_style style;
  uint32_t text_color = 0x000102EEu;
  reset_fixture();
  win = compositor_create_window("TP", 8, 12, 40u, 30u);
  w = widget_create(WIDGET_BUTTON, win);
  if (!win || !w) {
    TEST("widget narrow text paint: fixture creates window/widget");
    FAIL("fixture");
    widget_destroy(w);
    shutdown_fixture();
    return;
  }
  widget_set_bounds(w, 3, 4, 16u, 18u);
  style = w->style;
  style.text_color = text_color;
  style.border_width = 0u;
  widget_set_style(w, &style);
  widget_set_text(w, "LongButtonLabel");
  widget_paint(w, &win->surface);
  TEST("widget_paint truncates narrow button text");
  if (surface_has_color(&win->surface, text_color)) PASS();
  else FAIL("narrow text paint");
  widget_destroy(w);
  shutdown_fixture();
}

static void test_widget_paint_renders_specialized_widgets(void) {
  struct gui_window *win;
  struct widget *label;
  struct widget *checkbox;
  struct widget *progress;
  struct widget_style style;
  uint32_t text_color = 0x00123456u;
  reset_fixture();
  win = compositor_create_window("V", 8, 12, 80u, 48u);
  label = widget_create(WIDGET_LABEL, win);
  checkbox = widget_create(WIDGET_CHECKBOX, win);
  progress = widget_create(WIDGET_PROGRESS, win);
  if (!win || !label || !checkbox || !progress) {
    TEST("widget specialized paint: fixture creates widgets");
    FAIL("fixture");
    widget_destroy(label);
    widget_destroy(checkbox);
    widget_destroy(progress);
    shutdown_fixture();
    return;
  }
  widget_set_bounds(label, 3, 4, 40u, 18u);
  widget_set_text(label, "T");
  style = label->style;
  style.text_color = text_color;
  widget_set_style(label, &style);
  widget_paint(label, &win->surface);
  checkbox->checked = 1;
  widget_set_bounds(checkbox, 46, 4, 24u, 16u);
  widget_paint(checkbox, &win->surface);
  widget_set_bounds(progress, 3, 26, 40u, 18u);
  progress->min_value = 0;
  progress->max_value = 100;
  progress->value = 50;
  widget_paint(progress, &win->surface);
  TEST("widget_paint renders text checkbox and progress");
  if (surface_has_color(&win->surface, text_color) &&
      surface_has_color(&win->surface, compositor_theme()->accent) &&
      surface_has_color(&win->surface, compositor_theme()->accent_alt)) PASS();
  else FAIL("specialized widget paint");
  widget_destroy(label);
  widget_destroy(checkbox);
  widget_destroy(progress);
  shutdown_fixture();
}

static void test_widget_paint_clamps_progress_below_min(void) {
  struct gui_window *win;
  struct widget *progress;
  struct widget_style style;
  reset_fixture();
  win = compositor_create_window("PB", 8, 12, 80u, 48u);
  progress = widget_create(WIDGET_PROGRESS, win);
  if (!win || !progress) {
    TEST("widget progress clamp: fixture creates widget");
    FAIL("fixture");
    widget_destroy(progress);
    shutdown_fixture();
    return;
  }
  widget_set_bounds(progress, 3, 4, 40u, 18u);
  style = progress->style;
  style.padding = 0u;
  style.border_width = 0u;
  widget_set_style(progress, &style);
  progress->min_value = 10;
  progress->max_value = 20;
  progress->value = 5;
  widget_paint(progress, &win->surface);
  TEST("widget_paint clamps progress below min");
  if (win->surface.pixels[10u * win->surface.width + 3u] ==
      compositor_theme()->accent_alt) PASS();
  else FAIL("progress below min");
  widget_destroy(progress);
  shutdown_fixture();
}

static void test_widget_paint_clamps_progress_above_max(void) {
  struct gui_window *win;
  struct widget *progress;
  struct widget_style style;
  reset_fixture();
  win = compositor_create_window("PA", 8, 12, 80u, 48u);
  progress = widget_create(WIDGET_PROGRESS, win);
  if (!win || !progress) {
    TEST("widget progress max clamp: fixture creates widget");
    FAIL("fixture");
    widget_destroy(progress);
    shutdown_fixture();
    return;
  }
  widget_set_bounds(progress, 3, 4, 40u, 18u);
  style = progress->style;
  style.padding = 0u;
  style.border_width = 0u;
  widget_set_style(progress, &style);
  progress->min_value = 10;
  progress->max_value = 20;
  progress->value = 30;
  widget_paint(progress, &win->surface);
  TEST("widget_paint clamps progress above max");
  if (win->surface.pixels[10u * win->surface.width + 42u] ==
      compositor_theme()->accent) PASS();
  else FAIL("progress above max");
  widget_destroy(progress);
  shutdown_fixture();
}

static void test_widget_paint_handles_invalid_progress_range(void) {
  struct gui_window *win;
  struct widget *progress;
  struct widget_style style;
  reset_fixture();
  win = compositor_create_window("PI", 8, 12, 80u, 48u);
  progress = widget_create(WIDGET_PROGRESS, win);
  if (!win || !progress) {
    TEST("widget progress invalid range: fixture creates widget");
    FAIL("fixture");
    widget_destroy(progress);
    shutdown_fixture();
    return;
  }
  widget_set_bounds(progress, 3, 4, 40u, 18u);
  style = progress->style;
  style.padding = 0u;
  style.border_width = 0u;
  widget_set_style(progress, &style);
  progress->min_value = 20;
  progress->max_value = 10;
  progress->value = 100;
  widget_paint(progress, &win->surface);
  TEST("widget_paint handles invalid progress range");
  if (win->surface.pixels[10u * win->surface.width + 3u] ==
          compositor_theme()->accent_alt &&
      win->surface.pixels[10u * win->surface.width + 42u] ==
          compositor_theme()->accent_alt) PASS();
  else FAIL("progress invalid range");
  widget_destroy(progress);
  shutdown_fixture();
}

static void test_widget_paint_handles_progress_padding_overflow(void) {
  struct gui_window *win;
  struct widget *progress;
  struct widget_style style;
  uint32_t bg = 0x000A0B0Cu;
  reset_fixture();
  win = compositor_create_window("PP", 8, 12, 80u, 48u);
  progress = widget_create(WIDGET_PROGRESS, win);
  if (!win || !progress) {
    TEST("widget progress padding overflow: fixture creates widget");
    FAIL("fixture");
    widget_destroy(progress);
    shutdown_fixture();
    return;
  }
  widget_set_bounds(progress, 3, 4, 8u, 18u);
  style = progress->style;
  style.bg_color = bg;
  style.padding = 10u;
  style.border_width = 0u;
  widget_set_style(progress, &style);
  progress->min_value = 0;
  progress->max_value = 100;
  progress->value = 100;
  widget_paint(progress, &win->surface);
  TEST("widget_paint handles progress padding overflow");
  if (win->surface.pixels[10u * win->surface.width + 4u] == bg &&
      !surface_has_color(&win->surface, compositor_theme()->accent) &&
      !surface_has_color(&win->surface, compositor_theme()->accent_alt)) PASS();
  else FAIL("progress padding overflow");
  widget_destroy(progress);
  shutdown_fixture();
}

static void test_widget_paint_renders_child_tree(void) {
  struct gui_window *win;
  struct widget *parent;
  struct widget *child;
  struct widget_style child_style;
  uint32_t child_color = 0x0000CC44u;
  reset_fixture();
  win = compositor_create_window("Tree", 8, 12, 80u, 48u);
  parent = widget_create(WIDGET_PANEL, win);
  child = widget_create(WIDGET_LABEL, win);
  if (!win || !parent || !child) {
    TEST("widget tree paint: fixture creates widgets");
    FAIL("fixture");
    widget_destroy(parent);
    widget_destroy(child);
    shutdown_fixture();
    return;
  }
  widget_set_bounds(parent, 2, 3, 50u, 30u);
  widget_set_bounds(child, 7, 9, 12u, 8u);
  child_style = child->style;
  child_style.bg_color = child_color;
  child_style.border_width = 0u;
  widget_set_style(child, &child_style);
  widget_add_child(parent, child);
  win->surface.pixels[10u * win->surface.width + 8u] = 0x00ABCDEFu;
  widget_paint(parent, &win->surface);
  TEST("widget_paint renders child tree");
  if (win->surface.pixels[10u * win->surface.width + 8u] == child_color) PASS();
  else FAIL("child tree paint");
  widget_destroy(parent);
  shutdown_fixture();
}

static void test_widget_paint_respects_child_state(void) {
  struct gui_window *win;
  struct widget *parent;
  struct widget *hidden;
  struct widget *disabled;
  struct widget_style parent_style;
  struct widget_style hidden_style;
  struct widget_style disabled_style;
  uint32_t parent_color = 0x00001122u;
  uint32_t hidden_color = 0x0000CC44u;
  reset_fixture();
  win = compositor_create_window("StateTree", 8, 12, 80u, 48u);
  parent = widget_create(WIDGET_PANEL, win);
  hidden = widget_create(WIDGET_LABEL, win);
  disabled = widget_create(WIDGET_BUTTON, win);
  if (!win || !parent || !hidden || !disabled) {
    TEST("widget child state paint: fixture creates widgets");
    FAIL("fixture");
    widget_destroy(parent);
    widget_destroy(hidden);
    widget_destroy(disabled);
    shutdown_fixture();
    return;
  }
  widget_set_bounds(parent, 2, 3, 60u, 32u);
  parent_style = parent->style;
  parent_style.bg_color = parent_color;
  parent_style.border_width = 0u;
  widget_set_style(parent, &parent_style);
  widget_set_bounds(hidden, 7, 9, 12u, 8u);
  hidden_style = hidden->style;
  hidden_style.bg_color = hidden_color;
  hidden_style.border_width = 0u;
  widget_set_style(hidden, &hidden_style);
  widget_set_visible(hidden, 0);
  widget_set_bounds(disabled, 24, 9, 12u, 8u);
  disabled_style = disabled->style;
  disabled_style.bg_color = 0x000066AAu;
  disabled_style.border_width = 0u;
  widget_set_style(disabled, &disabled_style);
  widget_set_enabled(disabled, 0);
  widget_add_child(parent, hidden);
  widget_add_child(parent, disabled);
  win->surface.pixels[10u * win->surface.width + 8u] = 0x00ABCDEFu;
  win->surface.pixels[10u * win->surface.width + 25u] = 0x00ABCDEFu;
  widget_paint(parent, &win->surface);
  TEST("widget_paint skips hidden children and dims disabled children");
  if (win->surface.pixels[10u * win->surface.width + 8u] == parent_color &&
      win->surface.pixels[10u * win->surface.width + 25u] == 0x00C0C0C0u &&
      !surface_has_color(&win->surface, hidden_color)) PASS();
  else FAIL("child state paint");
  widget_destroy(parent);
  shutdown_fixture();
}

static void test_widget_paint_renders_large_child_tree(void) {
  struct gui_window *win;
  struct widget *root;
  struct widget *target = NULL;
  uint32_t target_color = 0x00AA5500u;
  int ok = 1;
  reset_fixture();
  win = compositor_create_window("LargeTree", 8, 12, 80u, 48u);
  root = widget_create(WIDGET_PANEL, win);
  if (!win || !root) {
    TEST("widget large tree paint: fixture creates root");
    FAIL("fixture");
    widget_destroy(root);
    shutdown_fixture();
    return;
  }
  root->bounds.x = 0;
  root->bounds.y = 0;
  root->bounds.width = win->surface.width;
  root->bounds.height = win->surface.height;
  root->style.border_width = 0u;
  for (uint32_t i = 0u; i < WIDGET_MAX_CHILDREN && ok; ++i) {
    struct widget *child = widget_create(WIDGET_PANEL, win);
    if (!child) {
      ok = 0;
      break;
    }
    child->bounds = root->bounds;
    child->style.border_width = 0u;
    widget_add_child(root, child);
    for (uint32_t j = 0u; j < WIDGET_MAX_CHILDREN; ++j) {
      struct widget *leaf = widget_create(WIDGET_LABEL, win);
      if (!leaf) {
        ok = 0;
        break;
      }
      leaf->bounds.x = 0;
      leaf->bounds.y = 0;
      leaf->bounds.width = 1u;
      leaf->bounds.height = 1u;
      leaf->style.border_width = 0u;
      if (i + 1u == WIDGET_MAX_CHILDREN &&
          j + 1u == WIDGET_MAX_CHILDREN) {
        leaf->bounds.x = 20;
        leaf->bounds.y = 20;
        leaf->bounds.width = 6u;
        leaf->bounds.height = 6u;
        leaf->style.bg_color = target_color;
        target = leaf;
      }
      widget_add_child(child, leaf);
    }
  }
  if (!ok || !target) {
    TEST("widget large tree paint: fixture creates tree");
    FAIL("fixture");
    widget_destroy(root);
    shutdown_fixture();
    return;
  }
  win->surface.pixels[21u * win->surface.width + 21u] = 0x00ABCDEFu;
  widget_paint(root, &win->surface);
  TEST("widget_paint renders large child tree");
  if (win->surface.pixels[21u * win->surface.width + 21u] == target_color) PASS();
  else FAIL("large child tree paint");
  widget_destroy(root);
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
  test_widget_paint_refreshes_surface();
  test_widget_paint_skips_invisible_widget();
  test_widget_paint_renders_hovered_state();
  test_widget_paint_dims_disabled_root();
  test_widget_paint_truncates_narrow_button_text();
  test_widget_paint_renders_specialized_widgets();
  test_widget_paint_clamps_progress_below_min();
  test_widget_paint_clamps_progress_above_max();
  test_widget_paint_handles_invalid_progress_range();
  test_widget_paint_handles_progress_padding_overflow();
  test_widget_paint_renders_child_tree();
  test_widget_paint_respects_child_state();
  test_widget_paint_renders_large_child_tree();
  test_hover_invalidates_widget_bounds();
  test_checkbox_click_invalidates_widget_bounds();
  test_set_text_invalidates_widget_bounds();
  test_set_bounds_invalidates_old_and_new_bounds();
  test_state_mutators_invalidate_widget_bounds();
  printf("[gui] Widget damage: %d/%d passed\n", tests_passed, tests_run);
  return tests_passed == tests_run ? 0 : 1;
}
