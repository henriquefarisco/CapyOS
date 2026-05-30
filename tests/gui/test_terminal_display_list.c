#include <stdio.h>
#include <stdint.h>

#include "gui/core/internal/compositor_internal.h"
#include "gui/terminal.h"
#include "gui/event.h"

static int terminal_dl_tests_run = 0;
static int terminal_dl_tests_passed = 0;

#define TEST(name) do { terminal_dl_tests_run++; printf("  %-58s ", name); } while (0)
#define PASS() do { printf("OK\n"); terminal_dl_tests_passed++; } while (0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while (0)

static void clear_pixels(uint32_t *pixels, uint32_t count, uint32_t color) {
  for (uint32_t i = 0u; i < count; ++i) pixels[i] = color;
}

#if defined(CAPYOS_HAVE_CAPYUI_WIDGET)
static int cell_has_color(const struct gui_window *win,
                          uint32_t cell_col,
                          uint32_t cell_row,
                          uint32_t glyph_w,
                          uint32_t glyph_h,
                          uint32_t color) {
  uint32_t x0 = cell_col * glyph_w;
  uint32_t y0 = cell_row * glyph_h;
  for (uint32_t y = 0u; y < glyph_h; ++y) {
    uint32_t py = y0 + y;
    if (py >= win->surface.height) continue;
    for (uint32_t x = 0u; x < glyph_w; ++x) {
      uint32_t px = x0 + x;
      if (px >= win->surface.width) continue;
      if (win->surface.pixels[py * win->surface.width + px] == color) return 1;
    }
  }
  return 0;
}

static int dirty_rects_cover_point(int32_t x, int32_t y) {
  for (uint32_t i = 0u; i < comp_dirty_rect_count; ++i) {
    const struct gui_rect *r = &comp_dirty_rects[i];
    if (x >= r->x && y >= r->y &&
        x < r->x + (int32_t)r->width &&
        y < r->y + (int32_t)r->height) {
      return 1;
    }
  }
  return 0;
}

static void test_terminal_paint_uses_display_list(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t bg = 0x00010203u;
  uint32_t fg = 0x00EEDDCCu;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal display-list fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  win->bg_color = term.default_bg;
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_set_color(&term, fg, bg);
  terminal_write_char(&term, ' ');
  terminal_write_char(&term, 'A');
  terminal_paint(&term);
  f = term.font;
  TEST("terminal display-list renders cell bg text and cursor");
  if (f &&
      win->surface.pixels[0] == bg &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height, fg) &&
      win->surface.pixels[(f->glyph_height - 2u) * win->surface.width +
                          term.cursor_x * f->glyph_width] == fg) PASS();
  else FAIL("terminal render");
  if (f) {
    gui_event_flush();
    comp_dirty_rect_count = 0u;
    comp_full_redraw_pending = 0;
    terminal_write_char(&term, 'B');
    terminal_paint(&term);
    TEST("terminal display-list second paint uses diff damage");
    if (cell_has_color(win, 2u, 0u, f->glyph_width, f->glyph_height, fg) &&
        comp_dirty_rect_count > 0u &&
        comp_dirty_rect_count < 8u &&
        comp_full_redraw_pending == 0 &&
        dirty_rects_cover_point(win->frame.x + (int32_t)(2u * f->glyph_width),
                                win->frame.y + 1)) PASS();
    else FAIL("terminal diff damage");
  }
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_display_list_preserves_ansi_colors(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  uint32_t green = 0x0033CC33u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal display-list ANSI fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[31mR\033[32mG");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal display-list preserves ANSI cell colors");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height, red) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height, green)) PASS();
  else FAIL("terminal ANSI colors");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_display_list_preserves_ansi_bold_color(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t bold_red = 0x00CC7373u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiBoldDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal display-list ANSI bold fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[31;1mB");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal display-list preserves ANSI bold color");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bold_red)) PASS();
  else FAIL("terminal ANSI bold color");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_display_list_preserves_ansi_bold_before_color(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t bold_red = 0x00CC7373u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiBoldBeforeDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal display-list ANSI bold-before-color fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;31mB");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal display-list preserves ANSI bold before color");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bold_red)) PASS();
  else FAIL("terminal ANSI bold before color");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_display_list_preserves_ansi_bold_across_sequences(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t bold_red = 0x00CC7373u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiBoldSeqDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal display-list ANSI bold sequence fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1m\033[31mB");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal display-list preserves ANSI bold across sequences");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bold_red)) PASS();
  else FAIL("terminal ANSI bold across sequences");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_display_list_ansi_22_clears_bold_for_next_color(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  uint32_t bold_red = 0x00CC7373u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsi22DL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal display-list ANSI 22 fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1m\033[31mB\033[22m\033[31mR");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal display-list ANSI 22 clears bold for next color");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bold_red) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height, red) &&
      !cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                      bold_red)) PASS();
  else FAIL("terminal ANSI 22 clears bold");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_display_list_ansi_dim_and_22_for_next_color(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  uint32_t dim_red = 0x00661919u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiDimDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal display-list ANSI dim fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[2m\033[31mD\033[22m\033[31mR");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal display-list ANSI dim and 22 for next color");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     dim_red) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height, red) &&
      !cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                      dim_red)) PASS();
  else FAIL("terminal ANSI dim and 22");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_display_list_preserves_ansi_bright_foreground(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t bright_red = 0x00CC7373u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiBrightFgDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal display-list ANSI bright fg fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[91mR");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal display-list preserves ANSI bright foreground");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bright_red)) PASS();
  else FAIL("terminal ANSI bright foreground");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_display_list_paints_ansi_bright_background_spaces(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t bright_red = 0x00CC7373u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiBrightBgDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal display-list ANSI bright bg fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[101m ");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal display-list paints ANSI bright background spaces");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bright_red)) PASS();
  else FAIL("terminal ANSI bright background space");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_display_list_ansi_49_resets_bright_background(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t bright_red = 0x00CC7373u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsi49BrightBgDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal display-list ANSI 49 bright bg fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[101m \033[49m ");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal display-list ANSI 49 resets bright background");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bright_red) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                     term.default_bg) &&
      !cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                      bright_red)) PASS();
  else FAIL("terminal ANSI 49 bright background reset");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_display_list_ignores_unknown_ansi_sgr(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiUnknownDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal display-list ANSI unknown fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[31mR\033[99mU");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal display-list ignores unknown ANSI SGR");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height, red) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height, red)) PASS();
  else FAIL("terminal ANSI unknown SGR");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_display_list_ansi_foreground_diff_damage(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  uint32_t green = 0x0033CC33u;
  int had_red;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiFgDiffDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal display-list ANSI fg diff fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  term.cursor_visible = 0;
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[31mA");
  terminal_paint(&term);
  f = term.font;
  had_red = f ? cell_has_color(win, 0u, 0u, f->glyph_width,
                               f->glyph_height, red) : 0;
  gui_event_flush();
  comp_dirty_rect_count = 0u;
  comp_full_redraw_pending = 0;
  terminal_set_cursor(&term, 0u, 0u);
  terminal_write_string(&term, "\033[32mA");
  terminal_paint(&term);
  TEST("terminal display-list ANSI foreground diff damage");
  if (f &&
      had_red &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height, green) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height, red) &&
      comp_dirty_rect_count > 0u &&
      comp_dirty_rect_count < 8u &&
      comp_full_redraw_pending == 0 &&
      dirty_rects_cover_point(win->frame.x, win->frame.y + 1)) PASS();
  else FAIL("terminal ANSI foreground diff");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_display_list_paints_ansi_background_spaces(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiBgDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal display-list ANSI bg fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  term.cursor_visible = 0;
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[41m ");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal display-list paints ANSI background spaces");
  if (f &&
      win->surface.pixels[0] == red &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height, red)) PASS();
  else FAIL("terminal ANSI background");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_display_list_ansi_background_diff_damage(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  uint32_t red = 0x00CC3333u;
  uint32_t green = 0x0033CC33u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiBgDiffDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal display-list ANSI bg diff fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  term.cursor_visible = 0;
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[41m ");
  terminal_paint(&term);
  gui_event_flush();
  comp_dirty_rect_count = 0u;
  comp_full_redraw_pending = 0;
  terminal_set_cursor(&term, 0u, 0u);
  terminal_write_string(&term, "\033[42m ");
  terminal_paint(&term);
  TEST("terminal display-list ANSI background diff damage");
  if (win->surface.pixels[0] == green &&
      win->surface.pixels[0] != red &&
      comp_dirty_rect_count > 0u &&
      comp_dirty_rect_count < 8u &&
      comp_full_redraw_pending == 0 &&
      dirty_rects_cover_point(win->frame.x, win->frame.y + 1)) PASS();
  else FAIL("terminal ANSI background diff");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_display_list_ansi_reset_restores_background(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiResetDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal display-list ANSI reset fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  term.cursor_visible = 0;
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[41m \033[0m ");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal display-list ANSI reset restores background");
  if (f &&
      win->surface.pixels[0] == red &&
      win->surface.pixels[f->glyph_width] == term.default_bg) PASS();
  else FAIL("terminal ANSI reset background");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_display_list_ansi_reset_restores_foreground(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiFgResetDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal display-list ANSI fg reset fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  term.cursor_visible = 0;
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[31mR\033[0mD");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal display-list ANSI reset restores foreground");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height, red) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                     term.default_fg)) PASS();
  else FAIL("terminal ANSI reset foreground");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_display_list_empty_ansi_sgr_resets_foreground(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiEmptyResetDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal display-list empty ANSI reset fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  term.cursor_visible = 0;
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[31mR\033[mD");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal display-list empty ANSI SGR resets foreground");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height, red) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                     term.default_fg)) PASS();
  else FAIL("terminal ANSI empty SGR reset");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_display_list_ansi_selective_resets(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  uint32_t green = 0x0033CC33u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSelectiveDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal display-list ANSI selective fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  term.cursor_visible = 0;
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[31;42mR\033[39mD\033[49mE");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal display-list ANSI selective resets");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height, red) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                     term.default_fg) &&
      win->surface.pixels[f->glyph_width] == green &&
      win->surface.pixels[2u * f->glyph_width] == term.default_bg) PASS();
  else FAIL("terminal ANSI selective reset");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_display_list_clear_uses_diff_damage(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t bg = 0x00020304u;
  uint32_t fg = 0x00DDCCBBu;
  int had_text;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermClearDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal display-list clear fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  term.cursor_visible = 0;
  win->bg_color = term.default_bg;
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_set_color(&term, fg, bg);
  terminal_write_char(&term, 'A');
  terminal_paint(&term);
  f = term.font;
  had_text = f ? cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height, fg) : 0;
  gui_event_flush();
  comp_dirty_rect_count = 0u;
  comp_full_redraw_pending = 0;
  terminal_clear(&term);
  terminal_paint(&term);
  TEST("terminal display-list clear removes text with diff damage");
  if (f &&
      had_text &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height, fg) &&
      win->surface.pixels[0] == bg &&
      comp_dirty_rect_count > 0u &&
      comp_dirty_rect_count < 8u &&
      comp_full_redraw_pending == 0 &&
      dirty_rects_cover_point(win->frame.x, win->frame.y + 1)) PASS();
  else FAIL("terminal clear damage");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_display_list_scroll_offset_hides_cursor(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t bg = 0x00010203u;
  uint32_t fg = 0x00FEDCBAu;
  uint32_t cursor_px;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermScrollDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal display-list scroll fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  win->bg_color = term.default_bg;
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_set_color(&term, fg, bg);
  terminal_write_char(&term, 'A');
  term.scroll_offset = 1u;
  terminal_paint(&term);
  f = term.font;
  TEST("terminal display-list scroll offset hides cursor");
  if (f && f->glyph_height >= 2u) {
    cursor_px = (f->glyph_height - 2u) * win->surface.width +
                term.cursor_x * f->glyph_width;
    if (cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height, fg) &&
        win->surface.pixels[cursor_px] != fg) PASS();
    else FAIL("terminal scroll cursor");
  } else {
    FAIL("font");
  }
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_display_list_cache_is_window_scoped(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *first;
  struct gui_window *second;
  struct terminal term_first;
  struct terminal term_second;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  first = compositor_create_window("TermOne", 4, 24, 80u, 48u);
  second = compositor_create_window("TermTwo", 44, 32, 80u, 48u);
  if (!first || !second) {
    TEST("terminal display-list multi-window fixture creates windows");
    FAIL("create windows");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term_first, first);
  terminal_init(&term_second, second);
  term_first.cursor_visible = 0;
  term_second.cursor_visible = 0;
  first->bg_color = term_first.default_bg;
  second->bg_color = term_second.default_bg;
  clear_pixels(first->surface.pixels,
               first->surface.width * first->surface.height,
               0u);
  clear_pixels(second->surface.pixels,
               second->surface.width * second->surface.height,
               0u);
  terminal_paint(&term_first);
  terminal_paint(&term_second);
  TEST("terminal display-list cache is scoped per window");
  if (first->surface.pixels[0] == term_first.default_bg &&
      second->surface.pixels[0] == term_second.default_bg) PASS();
  else FAIL("terminal window cache");
  compositor_shutdown();
  gui_event_init();
}
#else
static void test_terminal_legacy_paint_still_available(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  uint32_t bg = 0x00010203u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermLegacy", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal legacy fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_set_color(&term, term.default_fg, bg);
  terminal_write_char(&term, ' ');
  terminal_paint(&term);
  TEST("terminal legacy paint remains available without widget ABI");
  if (win->surface.pixels[0] == bg) PASS();
  else FAIL("legacy terminal render");
  compositor_shutdown();
  gui_event_init();
}
#endif

int test_terminal_display_list_run(void) {
  terminal_dl_tests_run = 0;
  terminal_dl_tests_passed = 0;
  printf("\n[gui] Terminal display-list tests\n");
#if defined(CAPYOS_HAVE_CAPYUI_WIDGET)
  test_terminal_paint_uses_display_list();
  test_terminal_display_list_preserves_ansi_colors();
  test_terminal_display_list_preserves_ansi_bold_color();
  test_terminal_display_list_preserves_ansi_bold_before_color();
  test_terminal_display_list_preserves_ansi_bold_across_sequences();
  test_terminal_display_list_ansi_22_clears_bold_for_next_color();
  test_terminal_display_list_ansi_dim_and_22_for_next_color();
  test_terminal_display_list_preserves_ansi_bright_foreground();
  test_terminal_display_list_paints_ansi_bright_background_spaces();
  test_terminal_display_list_ansi_49_resets_bright_background();
  test_terminal_display_list_ignores_unknown_ansi_sgr();
  test_terminal_display_list_ansi_foreground_diff_damage();
  test_terminal_display_list_paints_ansi_background_spaces();
  test_terminal_display_list_ansi_background_diff_damage();
  test_terminal_display_list_ansi_reset_restores_background();
  test_terminal_display_list_ansi_reset_restores_foreground();
  test_terminal_display_list_empty_ansi_sgr_resets_foreground();
  test_terminal_display_list_ansi_selective_resets();
  test_terminal_display_list_clear_uses_diff_damage();
  test_terminal_display_list_scroll_offset_hides_cursor();
  test_terminal_display_list_cache_is_window_scoped();
#else
  test_terminal_legacy_paint_still_available();
#endif
  printf("[gui] Terminal display-list: %d/%d passed\n",
         terminal_dl_tests_passed, terminal_dl_tests_run);
  return terminal_dl_tests_passed == terminal_dl_tests_run ? 0 : 1;
}
