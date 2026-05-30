#include <stdio.h>
#include <stdint.h>

#include "gui/core/internal/compositor_internal.h"
#include "gui/event.h"
#include "gui/terminal.h"

static int terminal_ansi_dl_tests_run = 0;
static int terminal_ansi_dl_tests_passed = 0;

#define TEST(name) do { terminal_ansi_dl_tests_run++; printf("  %-58s ", name); } while (0)
#define PASS() do { printf("OK\n"); terminal_ansi_dl_tests_passed++; } while (0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while (0)

#if defined(CAPYOS_HAVE_CAPYUI_WIDGET)
static void clear_pixels(uint32_t *pixels, uint32_t count, uint32_t color) {
  for (uint32_t i = 0u; i < count; ++i) pixels[i] = color;
}

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

static void test_terminal_ansi_display_list_39_resets_bright_foreground(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t bright_red = 0x00CC7373u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsi39BrightFgDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI display-list 39 fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[91mR\033[39mD");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI display-list 39 resets bright foreground");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bright_red) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                     term.default_fg) &&
      !cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                      bright_red)) PASS();
  else FAIL("terminal ANSI 39 bright foreground reset");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_display_list_39_preserves_bright_background(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t bright_red = 0x00CC7373u;
  uint32_t bright_green = 0x0073CC73u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsi39BrightBgDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI display-list 39 bright bg fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[92m\033[101mB\033[39mD");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI display-list 39 preserves bright background");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bright_green) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bright_red) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                     term.default_fg) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                     bright_red) &&
      !cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                      bright_green)) PASS();
  else FAIL("terminal ANSI 39 preserves bright background");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_display_list_39_preserves_bold_intensity(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t bright_red = 0x00CC7373u;
  uint32_t bold_default = 0x00737373u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsi39BoldFgDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI display-list 39 bold fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  term.default_fg = 0x00333333u;
  term.fg_color = term.default_fg;
  term.fg_base_color = term.default_fg;
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1m\033[91mR\033[39mD");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI display-list 39 preserves bold intensity");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bright_red) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                     bold_default) &&
      !cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                      bright_red)) PASS();
  else FAIL("terminal ANSI 39 bold intensity");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_display_list_39_then_22_keeps_default_foreground(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t bright_red = 0x00CC7373u;
  uint32_t bold_default = 0x00737373u;
  uint32_t default_fg = 0x00333333u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsi3922DefaultFgDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI display-list 39+22 fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  term.default_fg = default_fg;
  term.fg_color = term.default_fg;
  term.fg_base_color = term.default_fg;
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1m\033[91mB\033[39mD\033[22mN");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI display-list 39 then 22 keeps default foreground");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bright_red) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                     bold_default) &&
      cell_has_color(win, 2u, 0u, f->glyph_width, f->glyph_height,
                     default_fg) &&
      !cell_has_color(win, 2u, 0u, f->glyph_width, f->glyph_height,
                      bright_red) &&
      !cell_has_color(win, 2u, 0u, f->glyph_width, f->glyph_height,
                      bold_default)) PASS();
  else FAIL("terminal ANSI 39 then 22 default foreground");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_display_list_39_preserves_dim_intensity(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t dim_bright_red = 0x00663939u;
  uint32_t dim_default = 0x00666666u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsi39DimFgDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI display-list 39 dim fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[2m\033[91mR\033[39mD");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI display-list 39 preserves dim intensity");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     dim_bright_red) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                     dim_default) &&
      !cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                      dim_bright_red)) PASS();
  else FAIL("terminal ANSI 39 dim intensity");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_display_list_22_clears_intensity_immediately(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  uint32_t bold_red = 0x00CC7373u;
  uint32_t dim_red = 0x00661919u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsi22ImmediateDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI display-list 22 fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1m\033[31mB\033[22mR\033[2mD\033[22mN");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI display-list 22 clears intensity immediately");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bold_red) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height, red) &&
      cell_has_color(win, 2u, 0u, f->glyph_width, f->glyph_height,
                     dim_red) &&
      cell_has_color(win, 3u, 0u, f->glyph_width, f->glyph_height, red) &&
      !cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                      bold_red) &&
      !cell_has_color(win, 3u, 0u, f->glyph_width, f->glyph_height,
                      dim_red)) PASS();
  else FAIL("terminal ANSI 22 clears intensity immediately");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_display_list_0_clears_intensity_and_colors(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  uint32_t bold_red = 0x00CC7373u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsi0ImmediateDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI display-list 0 fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1m\033[31m\033[41mB\033[0mD");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI display-list 0 clears intensity and colors");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bold_red) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height, red) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                     term.default_fg) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                     term.default_bg) &&
      !cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                      bold_red) &&
      !cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                      red)) PASS();
  else FAIL("terminal ANSI 0 clears intensity and colors");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_display_list_0_resets_bright_colors(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t bright_green = 0x0073CC73u;
  uint32_t bright_red = 0x00CC7373u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsi0BrightDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI display-list 0 bright fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[92m\033[101mB\033[0mD");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI display-list 0 resets bright colors");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bright_green) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bright_red) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                     term.default_fg) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                     term.default_bg) &&
      !cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                      bright_green) &&
      !cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                      bright_red)) PASS();
  else FAIL("terminal ANSI 0 resets bright colors");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_display_list_multi_param_selective_resets(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t bright_green = 0x0073CC73u;
  uint32_t bright_red = 0x00CC7373u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiMultiParamDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI display-list multi-param fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[92;101mB\033[39;49mD");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI display-list multi-param selective reset");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bright_green) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bright_red) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                     term.default_fg) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                     term.default_bg) &&
      !cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                      bright_green) &&
      !cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                      bright_red)) PASS();
  else FAIL("terminal ANSI multi-param selective reset");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_display_list_multi_param_intensity_color(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  uint32_t bold_red = 0x00CC7373u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiMultiIntensityDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI display-list multi-param intensity fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;31mB\033[22mR");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI display-list multi-param intensity color");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bold_red) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height, red) &&
      !cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                      bold_red)) PASS();
  else FAIL("terminal ANSI multi-param intensity color");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_display_list_multi_param_color_intensity(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  uint32_t bold_red = 0x00CC7373u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiMultiColorIntensityDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI display-list multi-param color intensity fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[31;1mB\033[22mR");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI display-list multi-param color intensity");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bold_red) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height, red) &&
      !cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                      bold_red)) PASS();
  else FAIL("terminal ANSI multi-param color intensity");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_display_list_multi_param_dim_color(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  uint32_t dim_red = 0x00661919u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiMultiDimColorDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI display-list multi-param dim fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[2;31mD\033[22mR");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI display-list multi-param dim color");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     dim_red) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height, red) &&
      !cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                      dim_red)) PASS();
  else FAIL("terminal ANSI multi-param dim color");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_display_list_multi_param_reset_then_color(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  uint32_t bold_red = 0x00CC7373u;
  uint32_t green_bg = 0x0033CC33u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiMultiResetColorDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI display-list multi-param reset color fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;31;42mB\033[0;31mR");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI display-list multi-param reset then color");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bold_red) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     green_bg) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height, red) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                     term.default_bg) &&
      !cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                      bold_red) &&
      !cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                      green_bg)) PASS();
  else FAIL("terminal ANSI multi-param reset then color");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_display_list_multi_param_color_then_reset(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  uint32_t bold_red = 0x00CC7373u;
  uint32_t green_bg = 0x0033CC33u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiMultiColorResetDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI display-list multi-param color reset fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;31;42mB\033[31;0mD");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI display-list multi-param color then reset");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bold_red) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     green_bg) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                     term.default_fg) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                     term.default_bg) &&
      !cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                      red) &&
      !cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                      green_bg)) PASS();
  else FAIL("terminal ANSI multi-param color then reset");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_display_list_multi_param_foreground_reset(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t bright_green = 0x0073CC73u;
  uint32_t bright_red = 0x00CC7373u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiMultiFgResetDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI display-list multi-param fg reset fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[92;39;101mD");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI display-list multi-param foreground reset");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     term.default_fg) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bright_red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bright_green)) PASS();
  else FAIL("terminal ANSI multi-param foreground reset");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_display_list_multi_param_intensity_then_22(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  uint32_t bold_red = 0x00CC7373u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiMulti22FinalDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI display-list multi-param 22 fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;31;22mR");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI display-list multi-param 22 final");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height, red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bold_red)) PASS();
  else FAIL("terminal ANSI multi-param 22 final");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_display_list_49_preserves_foreground(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  uint32_t bold_red = 0x00CC7373u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsi49PreserveFgDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI display-list 49 fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1m\033[31m\033[41mB\033[49mR");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI display-list 49 preserves foreground");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bold_red) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height, red) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                     bold_red) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                     term.default_bg) &&
      !cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                      red)) PASS();
  else FAIL("terminal ANSI 49 preserves foreground");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_display_list_49_preserves_bright_foreground(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t bright_green = 0x0073CC73u;
  uint32_t bright_red = 0x00CC7373u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsi49BrightFgDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI display-list 49 bright fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[92m\033[101mB\033[49mG");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI display-list 49 preserves bright foreground");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bright_green) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bright_red) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                     bright_green) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                     term.default_bg) &&
      !cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                      bright_red)) PASS();
  else FAIL("terminal ANSI 49 preserves bright foreground");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_display_list_49_preserves_dim_foreground(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  uint32_t dim_red = 0x00661919u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsi49DimFgDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI display-list 49 dim fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[2m\033[31m\033[41mD\033[49mN");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI display-list 49 preserves dim foreground");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     dim_red) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height, red) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                     dim_red) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                     term.default_bg) &&
      !cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                      red)) PASS();
  else FAIL("terminal ANSI 49 preserves dim foreground");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_display_list_22_preserves_bright_foreground(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  uint32_t bright_red = 0x00CC7373u;
  uint32_t dim_bright_red = 0x00663939u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsi22BrightFgDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI display-list 22 bright fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1m\033[91mB\033[22mR\033[2mD\033[22mN");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI display-list 22 preserves bright foreground");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bright_red) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                     bright_red) &&
      cell_has_color(win, 2u, 0u, f->glyph_width, f->glyph_height,
                     dim_bright_red) &&
      cell_has_color(win, 3u, 0u, f->glyph_width, f->glyph_height,
                     bright_red) &&
      !cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                      red) &&
      !cell_has_color(win, 3u, 0u, f->glyph_width, f->glyph_height,
                      dim_bright_red)) PASS();
  else FAIL("terminal ANSI 22 preserves bright foreground");
  compositor_shutdown();
  gui_event_init();
}
#else
static void test_terminal_ansi_display_list_legacy_skips_widget_cases(void) {
  TEST("terminal ANSI display-list widget-only cases skipped");
  PASS();
}
#endif

int test_terminal_ansi_display_list_run(void) {
  terminal_ansi_dl_tests_run = 0;
  terminal_ansi_dl_tests_passed = 0;
  printf("\n[gui] Terminal ANSI display-list tests\n");
#if defined(CAPYOS_HAVE_CAPYUI_WIDGET)
  test_terminal_ansi_display_list_39_resets_bright_foreground();
  test_terminal_ansi_display_list_39_preserves_bright_background();
  test_terminal_ansi_display_list_39_preserves_bold_intensity();
  test_terminal_ansi_display_list_39_then_22_keeps_default_foreground();
  test_terminal_ansi_display_list_39_preserves_dim_intensity();
  test_terminal_ansi_display_list_22_clears_intensity_immediately();
  test_terminal_ansi_display_list_0_clears_intensity_and_colors();
  test_terminal_ansi_display_list_0_resets_bright_colors();
  test_terminal_ansi_display_list_multi_param_selective_resets();
  test_terminal_ansi_display_list_multi_param_intensity_color();
  test_terminal_ansi_display_list_multi_param_color_intensity();
  test_terminal_ansi_display_list_multi_param_dim_color();
  test_terminal_ansi_display_list_multi_param_reset_then_color();
  test_terminal_ansi_display_list_multi_param_color_then_reset();
  test_terminal_ansi_display_list_multi_param_foreground_reset();
  test_terminal_ansi_display_list_multi_param_intensity_then_22();
  test_terminal_ansi_display_list_49_preserves_foreground();
  test_terminal_ansi_display_list_49_preserves_bright_foreground();
  test_terminal_ansi_display_list_49_preserves_dim_foreground();
  test_terminal_ansi_display_list_22_preserves_bright_foreground();
#else
  test_terminal_ansi_display_list_legacy_skips_widget_cases();
#endif
  printf("[gui] Terminal ANSI display-list: %d/%d passed\n",
         terminal_ansi_dl_tests_passed, terminal_ansi_dl_tests_run);
  return terminal_ansi_dl_tests_passed == terminal_ansi_dl_tests_run ? 0 : 1;
}
