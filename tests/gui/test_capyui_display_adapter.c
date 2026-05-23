#include <stdio.h>
#include <stdint.h>

#include "gui/capyui_display_adapter.h"
#include "gui/event.h"

#if defined(CAPYOS_HAVE_CAPYUI_WIDGET)
#include "capy_display_list.h"
#include "capy_widget.h"
#endif

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { tests_run++; printf("  %-58s ", name); } while (0)
#define PASS() do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while (0)

static void clear_surface(uint32_t *pixels, uint32_t count, uint32_t color) {
  for (uint32_t i = 0u; i < count; ++i) pixels[i] = color;
}

#if defined(CAPYOS_HAVE_CAPYUI_WIDGET)

static void init_display_list(struct capy_display_list *dl,
                              struct capy_dl_cmd *cmds,
                              uint32_t cmd_cap,
                              char *text_pool,
                              uint32_t text_cap) {
  dl->cmds = cmds;
  dl->count = 0u;
  dl->capacity = cmd_cap;
  dl->text_pool = text_pool;
  dl->text_used = 0u;
  dl->text_capacity = text_cap;
  dl->version = CAPY_DISPLAY_LIST_SCHEMA_VERSION;
  dl->theme = 0;
  dl->dpi_scale_x256 = 256u;
  dl->reserved_dpi = 0u;
}

static void test_reports_schema(void) {
  TEST("capyui display adapter reports sibling schema v7");
  if (capyui_display_adapter_available() == 1 &&
      capyui_display_adapter_schema_version() == CAPY_DISPLAY_LIST_SCHEMA_VERSION &&
      capyui_display_adapter_schema_version() == 7u) PASS();
  else FAIL("schema");
}

static void test_renders_rect_and_border(void) {
  uint32_t pixels[20 * 12];
  struct gui_surface surface = { pixels, 20u, 12u, 20u * 4u };
  struct capy_dl_cmd cmds[3] = { { 0 } };
  struct capy_display_list dl;
  struct capyui_display_adapter_stats stats;
  clear_surface(pixels, 20u * 12u, 0u);
  init_display_list(&dl, cmds, 3u, 0, 0u);
  cmds[0].op = CAPY_DL_RECT;
  cmds[0].rect.x = 2;
  cmds[0].rect.y = 2;
  cmds[0].rect.width = 8u;
  cmds[0].rect.height = 5u;
  cmds[0].color = 0x00112233u;
  cmds[1].op = CAPY_DL_BORDER;
  cmds[1].rect = cmds[0].rect;
  cmds[1].color = 0x00AABBCCu;
  cmds[1].border_width = 1u;
  dl.count = 2u;
  TEST("capyui display adapter renders rect and border");
  if (capyui_display_adapter_render(&dl, &surface, 0, &stats) == 0 &&
      pixels[2u * 20u + 2u] == 0x00AABBCCu &&
      pixels[4u * 20u + 4u] == 0x00112233u &&
      pixels[0] == 0u &&
      stats.commands_seen == 2u &&
      stats.commands_rendered == 2u) PASS();
  else FAIL("rect/border render");
}

static void test_respects_clip_stack(void) {
  uint32_t pixels[12 * 8];
  struct gui_surface surface = { pixels, 12u, 8u, 12u * 4u };
  struct capy_dl_cmd cmds[3] = { { 0 } };
  struct capy_display_list dl;
  clear_surface(pixels, 12u * 8u, 0u);
  init_display_list(&dl, cmds, 3u, 0, 0u);
  cmds[0].op = CAPY_DL_CLIP_PUSH;
  cmds[0].rect.x = 3;
  cmds[0].rect.y = 2;
  cmds[0].rect.width = 4u;
  cmds[0].rect.height = 3u;
  cmds[1].op = CAPY_DL_RECT;
  cmds[1].rect.x = 0;
  cmds[1].rect.y = 0;
  cmds[1].rect.width = 10u;
  cmds[1].rect.height = 6u;
  cmds[1].color = 0x0000FF00u;
  cmds[2].op = CAPY_DL_CLIP_POP;
  dl.count = 3u;
  TEST("capyui display adapter clips drawing to CapyUI clip stack");
  if (capyui_display_adapter_render(&dl, &surface, 0, 0) == 0 &&
      pixels[2u * 12u + 3u] == 0x0000FF00u &&
      pixels[4u * 12u + 6u] == 0x0000FF00u &&
      pixels[1u * 12u + 3u] == 0u &&
      pixels[2u * 12u + 7u] == 0u) PASS();
  else FAIL("clip stack");
}

static void test_respects_damage_clip(void) {
  uint32_t pixels[10 * 6];
  struct gui_surface surface = { pixels, 10u, 6u, 10u * 4u };
  struct capy_dl_cmd cmd = { 0 };
  struct capy_display_list dl;
  struct gui_rect damage = { 4, 1, 3u, 3u };
  clear_surface(pixels, 10u * 6u, 0u);
  init_display_list(&dl, &cmd, 1u, 0, 0u);
  cmd.op = CAPY_DL_RECT;
  cmd.rect.x = 1;
  cmd.rect.y = 1;
  cmd.rect.width = 8u;
  cmd.rect.height = 4u;
  cmd.color = 0x00F00F00u;
  dl.count = 1u;
  TEST("capyui display adapter limits rendering to damage clip");
  if (capyui_display_adapter_render(&dl, &surface, &damage, 0) == 0 &&
      pixels[1u * 10u + 4u] == 0x00F00F00u &&
      pixels[3u * 10u + 6u] == 0x00F00F00u &&
      pixels[1u * 10u + 3u] == 0u &&
      pixels[4u * 10u + 4u] == 0u) PASS();
  else FAIL("damage clip");
}

static void test_text_range_is_validated(void) {
  uint32_t pixels[32 * 16];
  char text_pool[4] = { 'C', 'a', 'p', 'y' };
  struct gui_surface surface = { pixels, 32u, 16u, 32u * 4u };
  struct capy_dl_cmd cmd = { 0 };
  struct capy_display_list dl;
  clear_surface(pixels, 32u * 16u, 0u);
  init_display_list(&dl, &cmd, 1u, text_pool, sizeof(text_pool));
  cmd.op = CAPY_DL_TEXT;
  cmd.rect.x = 0;
  cmd.rect.y = 0;
  cmd.rect.width = 32u;
  cmd.rect.height = 16u;
  cmd.color = 0x00FFFFFFu;
  cmd.text_offset = 2u;
  cmd.text_len = 4u;
  dl.count = 1u;
  TEST("capyui display adapter rejects out-of-range text spans");
  if (capyui_display_adapter_render(&dl, &surface, 0, 0) ==
      CAPYUI_DISPLAY_ADAPTER_ERR_TEXT_RANGE) PASS();
  else FAIL("text range");
}

static void test_diff_damage_maps_rects(void) {
  struct capy_dl_cmd prev_cmd = { 0 };
  struct capy_dl_cmd next_cmd = { 0 };
  struct capy_display_list prev;
  struct capy_display_list next;
  struct gui_rect dirty[2];
  int rc;
  init_display_list(&prev, &prev_cmd, 1u, 0, 0u);
  init_display_list(&next, &next_cmd, 1u, 0, 0u);
  prev_cmd.op = CAPY_DL_RECT;
  prev_cmd.rect.x = 1;
  prev_cmd.rect.y = 1;
  prev_cmd.rect.width = 2u;
  prev_cmd.rect.height = 2u;
  prev_cmd.color = 0x00010101u;
  next_cmd = prev_cmd;
  next_cmd.rect.x = 4;
  next_cmd.color = 0x00020202u;
  prev.count = 1u;
  next.count = 1u;
  rc = capyui_display_adapter_diff_damage(&prev, &next, dirty, 2u);
  TEST("capyui display adapter maps CapyUI diff rects to gui_rect");
  if (rc == 2 && dirty[0].x == 4 && dirty[1].x == 1) PASS();
  else FAIL("diff damage");
}

static void test_diff_damage_detects_text_content_changes(void) {
  struct capy_dl_cmd prev_cmd = { 0 };
  struct capy_dl_cmd next_cmd = { 0 };
  char prev_text[1] = { '1' };
  char next_text[1] = { '2' };
  struct capy_display_list prev;
  struct capy_display_list next;
  struct gui_rect dirty[1];
  int rc;
  init_display_list(&prev, &prev_cmd, 1u, prev_text, sizeof(prev_text));
  init_display_list(&next, &next_cmd, 1u, next_text, sizeof(next_text));
  prev_cmd.op = CAPY_DL_TEXT;
  prev_cmd.rect.x = 3;
  prev_cmd.rect.y = 4;
  prev_cmd.rect.width = 12u;
  prev_cmd.rect.height = 8u;
  prev_cmd.color = 0x00FFFFFFu;
  prev_cmd.text_len = 1u;
  next_cmd = prev_cmd;
  prev.count = 1u;
  next.count = 1u;
  rc = capyui_display_adapter_diff_damage(&prev, &next, dirty, 1u);
  TEST("capyui display adapter diffs text content at stable offset");
  if (rc == 1 && dirty[0].x == 3 && dirty[0].y == 4 &&
      dirty[0].width == 12u && dirty[0].height == 8u) PASS();
  else FAIL("text diff");
}

static void test_diff_damage_reports_overflow(void) {
  struct capy_dl_cmd prev_cmd = { 0 };
  struct capy_dl_cmd next_cmd = { 0 };
  struct capy_display_list prev;
  struct capy_display_list next;
  struct gui_rect dirty[1];
  init_display_list(&prev, &prev_cmd, 1u, 0, 0u);
  init_display_list(&next, &next_cmd, 1u, 0, 0u);
  prev_cmd.op = CAPY_DL_RECT;
  prev_cmd.rect.x = 1;
  prev_cmd.rect.y = 1;
  prev_cmd.rect.width = 2u;
  prev_cmd.rect.height = 2u;
  next_cmd = prev_cmd;
  next_cmd.rect.x = 8;
  prev.count = 1u;
  next.count = 1u;
  TEST("capyui display adapter reports dirty rect overflow distinctly");
  if (capyui_display_adapter_diff_damage(&prev, &next, dirty, 1u) ==
      CAPYUI_DISPLAY_ADAPTER_ERR_DIRTY_OVERFLOW) PASS();
  else FAIL("dirty overflow");
}

static void test_render_window_invalidates_dirty_region(void) {
  static uint32_t framebuffer[64 * 64];
  struct capy_dl_cmd cmd = { 0 };
  struct capy_display_list dl;
  struct capyui_display_adapter_stats stats;
  struct gui_window *win;
  struct gui_event out;
  gui_event_init();
  compositor_init(framebuffer, 64u, 64u, 64u * 4u);
  win = compositor_create_window("DL", 4, 24, 16u, 16u);
  if (!win) {
    TEST("capyui display adapter renders into compositor window");
    FAIL("window fixture");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  clear_surface(win->surface.pixels, 16u * 16u, 0u);
  init_display_list(&dl, &cmd, 1u, 0, 0u);
  cmd.op = CAPY_DL_RECT;
  cmd.rect.x = 2;
  cmd.rect.y = 3;
  cmd.rect.width = 5u;
  cmd.rect.height = 4u;
  cmd.color = 0x00012345u;
  dl.count = 1u;
  TEST("capyui display adapter renders into compositor window");
  if (capyui_display_adapter_render_window(win, 0, &dl, &stats) == 0 &&
      win->surface.pixels[3u * 16u + 2u] == 0x00012345u &&
      win->surface.pixels[0] == 0u &&
      gui_event_poll(&out) == 0 &&
      out.type == GUI_EVENT_PAINT &&
      out.window_id == win->id &&
      stats.commands_seen == 1u &&
      stats.commands_rendered == 1u) PASS();
  else FAIL("window render");
  compositor_shutdown();
  gui_event_init();
}

static int emit_widget_tree(void *producer, struct capy_display_list *out) {
  capy_display_list_reset(out);
  return capy_widget_emit((struct capy_widget *)producer, out);
}

static void test_render_producer_window_uses_real_widget_emit(void) {
  static uint32_t framebuffer[64 * 64];
  struct capy_dl_cmd cmds[8] = { { 0 } };
  char text_pool[64];
  struct capy_display_list dl;
  struct capy_widget root = { 0 };
  struct capy_widget_style style;
  struct capyui_display_adapter_stats stats;
  struct gui_window *win;
  gui_event_init();
  compositor_init(framebuffer, 64u, 64u, 64u * 4u);
  win = compositor_create_window("Widget DL", 4, 24, 20u, 18u);
  if (!win) {
    TEST("capyui adapter renders real CapyUI widget producer");
    FAIL("window fixture");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  clear_surface(win->surface.pixels, 20u * 18u, 0u);
  capy_display_list_init(&dl, cmds, 8u, text_pool, sizeof(text_pool));
  style = capy_widget_default_style();
  style.bg_color = 0x0000CC44u;
  style.border_width = 0u;
  root.type = CAPY_WIDGET_PANEL;
  root.bounds.x = 2;
  root.bounds.y = 2;
  root.bounds.width = 9u;
  root.bounds.height = 6u;
  root.style = style;
  root.visible = 1u;
  root.enabled = 1u;
  TEST("capyui adapter renders real CapyUI widget producer");
  if (capyui_display_adapter_render_producer_window(
          win, 0, &dl, emit_widget_tree, &root, &stats) == 0 &&
      win->surface.pixels[2u * 20u + 2u] == 0x0000CC44u &&
      win->surface.pixels[0] == 0u &&
      stats.commands_seen >= 3u &&
      stats.commands_rendered == 1u) PASS();
  else FAIL("producer render");
  compositor_shutdown();
  gui_event_init();
}

#else

static void test_fallback_unavailable(void) {
  uint32_t pixels[4];
  struct gui_surface surface = { pixels, 2u, 2u, 8u };
  struct capyui_display_adapter_stats stats;
  clear_surface(pixels, 4u, 0u);
  TEST("capyui display adapter fails closed without sibling ABI");
  if (capyui_display_adapter_available() == 0 &&
      capyui_display_adapter_schema_version() == 0u &&
      capyui_display_adapter_render(0, &surface, 0, &stats) ==
          CAPYUI_DISPLAY_ADAPTER_ERR_UNAVAILABLE &&
      stats.commands_seen == 0u && stats.commands_rendered == 0u) PASS();
  else FAIL("fallback unavailable");
}

#endif

int test_capyui_display_adapter_run(void) {
  tests_run = 0;
  tests_passed = 0;
  printf("\n[gui] CapyUI display adapter tests\n");
#if defined(CAPYOS_HAVE_CAPYUI_WIDGET)
  test_reports_schema();
  test_renders_rect_and_border();
  test_respects_clip_stack();
  test_respects_damage_clip();
  test_text_range_is_validated();
  test_diff_damage_maps_rects();
  test_diff_damage_detects_text_content_changes();
  test_diff_damage_reports_overflow();
  test_render_window_invalidates_dirty_region();
  test_render_producer_window_uses_real_widget_emit();
#else
  test_fallback_unavailable();
#endif
  printf("[gui] CapyUI display adapter: %d/%d passed\n", tests_passed, tests_run);
  return tests_passed == tests_run ? 0 : 1;
}
