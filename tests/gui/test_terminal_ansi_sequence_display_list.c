#include <stdio.h>
#include <stdint.h>

#include "gui/core/internal/compositor_internal.h"
#include "gui/event.h"
#include "gui/terminal.h"

static int terminal_ansi_sequence_dl_tests_run = 0;
static int terminal_ansi_sequence_dl_tests_passed = 0;

#define TEST(name) do { terminal_ansi_sequence_dl_tests_run++; printf("  %-62s ", name); } while (0)
#define PASS() do { printf("OK\n"); terminal_ansi_sequence_dl_tests_passed++; } while (0)
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

static void test_terminal_ansi_sequence_display_list_multi_param_background_reset(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t bright_green = 0x0073CC73u;
  uint32_t bright_red = 0x00CC7373u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSeqBgResetDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence display-list bg reset fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[101;49;92mG");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence display-list multi-param background reset");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bright_green) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     term.default_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bright_red)) PASS();
  else FAIL("terminal ANSI sequence multi-param background reset");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_display_list_multi_param_background_reset_final(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t bright_green = 0x0073CC73u;
  uint32_t bright_red = 0x00CC7373u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSeqBgResetFinalDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence display-list bg reset final fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[92;101;49mG");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence display-list multi-param background reset final");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bright_green) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     term.default_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bright_red)) PASS();
  else FAIL("terminal ANSI sequence multi-param background reset final");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_display_list_multi_param_foreground_reset_final(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t bright_green = 0x0073CC73u;
  uint32_t bright_red = 0x00CC7373u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSeqFgResetFinalDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence display-list fg reset final fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[101;92;39mD");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence display-list multi-param foreground reset final");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     term.default_fg) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bright_red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bright_green)) PASS();
  else FAIL("terminal ANSI sequence multi-param foreground reset final");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_display_list_multi_param_foreground_reset_then_color(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t bright_green = 0x0073CC73u;
  uint32_t bright_red = 0x00CC7373u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSeqFgResetColorDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence display-list fg reset color fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[101;39;92mG");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence display-list multi-param foreground reset then color");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bright_green) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bright_red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_fg)) PASS();
  else FAIL("terminal ANSI sequence multi-param foreground reset then color");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_display_list_multi_param_background_reset_then_color(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t bright_green = 0x0073CC73u;
  uint32_t bright_red = 0x00CC7373u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSeqBgResetColorDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence display-list bg reset color fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[92;49;101mG");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence display-list multi-param background reset then color");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bright_green) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bright_red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_bg)) PASS();
  else FAIL("terminal ANSI sequence multi-param background reset then color");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_display_list_multi_param_reset_middle_then_colors(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  uint32_t green_bg = 0x0033CC33u;
  uint32_t bright_green = 0x0073CC73u;
  uint32_t bright_red = 0x00CC7373u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSeqResetMiddleColorDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence display-list reset middle fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[31;42;0;92;101mG");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence display-list multi-param reset middle colors");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bright_green) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bright_red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height, red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      green_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_bg)) PASS();
  else FAIL("terminal ANSI sequence multi-param reset middle colors");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_display_list_multi_param_22_middle_preserves_background(void) {
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
  win = compositor_create_window("TermAnsiSeq22MiddleBgDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence display-list 22 middle fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;42;22;31mR");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence display-list multi-param 22 middle background");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height, red) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     green_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bold_red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_bg)) PASS();
  else FAIL("terminal ANSI sequence multi-param 22 middle background");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_display_list_multi_param_22_middle_clears_dim(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  uint32_t dim_red = 0x00661919u;
  uint32_t green_bg = 0x0033CC33u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSeq22MiddleDimDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence display-list 22 middle dim fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[2;42;22;31mR");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence display-list multi-param 22 middle clears dim");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height, red) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     green_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      dim_red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_bg)) PASS();
  else FAIL("terminal ANSI sequence multi-param 22 middle clears dim");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_display_list_multi_param_39_middle_preserves_intensity(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  uint32_t bold_red = 0x00CC7373u;
  uint32_t bright_green = 0x0073CC73u;
  uint32_t green_bg = 0x0033CC33u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSeq39MiddleIntensityDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence display-list 39 middle fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;42;92;39;31mR");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence display-list multi-param 39 middle intensity");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bold_red) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     green_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height, red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bright_green) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_bg)) PASS();
  else FAIL("terminal ANSI sequence multi-param 39 middle intensity");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_display_list_multi_param_39_middle_preserves_dim_intensity(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  uint32_t dim_red = 0x00661919u;
  uint32_t bright_green = 0x0073CC73u;
  uint32_t green_bg = 0x0033CC33u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSeq39MiddleDimDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence display-list 39 middle dim fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[2;42;92;39;31mR");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence display-list multi-param 39 middle dim");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     dim_red) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     green_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height, red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bright_green) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_bg)) PASS();
  else FAIL("terminal ANSI sequence multi-param 39 middle dim");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_display_list_multi_param_39_final_preserves_bold_background(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t bold_default = 0x00737373u;
  uint32_t bright_green = 0x0073CC73u;
  uint32_t green_bg = 0x0033CC33u;
  uint32_t default_fg = 0x00333333u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSeq39FinalBoldBgDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence display-list 39 final fixture creates window");
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
  terminal_write_string(&term, "\033[1;42;92;39mD");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence display-list multi-param 39 final bold background");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bold_default) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     green_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      default_fg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bright_green) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_bg)) PASS();
  else FAIL("terminal ANSI sequence multi-param 39 final bold background");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_display_list_multi_param_49_middle_preserves_foreground(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  uint32_t bold_red = 0x00CC7373u;
  uint32_t blue_bg = 0x003333CCu;
  uint32_t green_bg = 0x0033CC33u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSeq49MiddleFgDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence display-list 49 middle fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;31;44;49;42mR");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence display-list multi-param 49 middle foreground");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bold_red) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     green_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height, red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      blue_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_bg)) PASS();
  else FAIL("terminal ANSI sequence multi-param 49 middle foreground");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_display_list_multi_param_49_middle_preserves_dim_foreground(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  uint32_t dim_red = 0x00661919u;
  uint32_t blue_bg = 0x003333CCu;
  uint32_t green_bg = 0x0033CC33u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSeq49MiddleDimFgDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence display-list 49 middle dim fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[2;31;44;49;42mR");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence display-list multi-param 49 middle dim foreground");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     dim_red) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     green_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height, red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      blue_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_bg)) PASS();
  else FAIL("terminal ANSI sequence multi-param 49 middle dim foreground");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_display_list_empty_final_param_resets(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  uint32_t green_bg = 0x0033CC33u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSeqEmptyFinalDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence display-list empty final fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[31;42;mD");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence display-list empty final param resets");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     term.default_fg) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     term.default_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      green_bg)) PASS();
  else FAIL("terminal ANSI sequence empty final param resets");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_display_list_empty_middle_param_resets(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  uint32_t green_bg = 0x0033CC33u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSeqEmptyMiddleDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence display-list empty middle fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[31;;42mD");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence display-list empty middle param resets");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     term.default_fg) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     green_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      red)) PASS();
  else FAIL("terminal ANSI sequence empty middle param resets");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_display_list_empty_initial_param_resets(void) {
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
  win = compositor_create_window("TermAnsiSeqEmptyInitialDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence display-list empty initial fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;42mB\033[;31mR");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence display-list empty initial param resets");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     green_bg) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height, red) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                     term.default_bg) &&
      !cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                      bold_red) &&
      !cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                      green_bg)) PASS();
  else FAIL("terminal ANSI sequence empty initial param resets");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_display_list_unknown_param_preserves_sequence(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  uint32_t green_bg = 0x0033CC33u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSeqUnknownParamDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence display-list unknown param fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[31;999;42mD");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence display-list unknown param preserves sequence");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height, red) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     green_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_fg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_bg)) PASS();
  else FAIL("terminal ANSI sequence unknown param preserves sequence");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_display_list_unknown_final_param_preserves_state(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  uint32_t green_bg = 0x0033CC33u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSeqUnknownFinalDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence display-list unknown final fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[31;42;999mD");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence display-list unknown final param preserves state");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height, red) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     green_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_fg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_bg)) PASS();
  else FAIL("terminal ANSI sequence unknown final param preserves state");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_display_list_unknown_initial_param_preserves_state(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t bold_red = 0x00CC7373u;
  uint32_t green_bg = 0x0033CC33u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSeqUnknownInitialDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence display-list unknown initial fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;42mB\033[999;31mR");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence display-list unknown initial param preserves state");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     green_bg) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                     bold_red) &&
      cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                     green_bg) &&
      !cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                      term.default_fg) &&
      !cell_has_color(win, 1u, 0u, f->glyph_width, f->glyph_height,
                      term.default_bg)) PASS();
  else FAIL("terminal ANSI sequence unknown initial param preserves state");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_display_list_multi_param_49_final_preserves_bold_foreground(void) {
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
  win = compositor_create_window("TermAnsiSeq49FinalBoldFgDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence display-list 49 final fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;31;42;49mR");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence display-list multi-param 49 final bold foreground");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bold_red) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     term.default_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height, red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      green_bg)) PASS();
  else FAIL("terminal ANSI sequence multi-param 49 final bold foreground");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_display_list_multi_param_49_final_preserves_dim_foreground(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  uint32_t dim_red = 0x00661919u;
  uint32_t green_bg = 0x0033CC33u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSeq49FinalDimFgDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence display-list 49 final dim fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[2;31;42;49mR");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence display-list multi-param 49 final dim foreground");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     dim_red) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     term.default_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height, red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      green_bg)) PASS();
  else FAIL("terminal ANSI sequence multi-param 49 final dim foreground");
  compositor_shutdown();
  gui_event_init();
}
#else
static void test_terminal_ansi_sequence_display_list_legacy_skips_widget_cases(void) {
  TEST("terminal ANSI sequence display-list widget-only cases skipped");
  PASS();
}
#endif

int test_terminal_ansi_sequence_display_list_run(void) {
  terminal_ansi_sequence_dl_tests_run = 0;
  terminal_ansi_sequence_dl_tests_passed = 0;
  printf("\n[gui] Terminal ANSI sequence display-list tests\n");
#if defined(CAPYOS_HAVE_CAPYUI_WIDGET)
  test_terminal_ansi_sequence_display_list_multi_param_background_reset();
  test_terminal_ansi_sequence_display_list_multi_param_background_reset_final();
  test_terminal_ansi_sequence_display_list_multi_param_foreground_reset_final();
  test_terminal_ansi_sequence_display_list_multi_param_foreground_reset_then_color();
  test_terminal_ansi_sequence_display_list_multi_param_background_reset_then_color();
  test_terminal_ansi_sequence_display_list_multi_param_reset_middle_then_colors();
  test_terminal_ansi_sequence_display_list_multi_param_22_middle_preserves_background();
  test_terminal_ansi_sequence_display_list_multi_param_22_middle_clears_dim();
  test_terminal_ansi_sequence_display_list_multi_param_39_middle_preserves_intensity();
  test_terminal_ansi_sequence_display_list_multi_param_39_middle_preserves_dim_intensity();
  test_terminal_ansi_sequence_display_list_multi_param_39_final_preserves_bold_background();
  test_terminal_ansi_sequence_display_list_multi_param_49_middle_preserves_foreground();
  test_terminal_ansi_sequence_display_list_multi_param_49_middle_preserves_dim_foreground();
  test_terminal_ansi_sequence_display_list_multi_param_49_final_preserves_bold_foreground();
  test_terminal_ansi_sequence_display_list_multi_param_49_final_preserves_dim_foreground();
  test_terminal_ansi_sequence_display_list_empty_final_param_resets();
  test_terminal_ansi_sequence_display_list_empty_middle_param_resets();
  test_terminal_ansi_sequence_display_list_empty_initial_param_resets();
  test_terminal_ansi_sequence_display_list_unknown_param_preserves_sequence();
  test_terminal_ansi_sequence_display_list_unknown_final_param_preserves_state();
  test_terminal_ansi_sequence_display_list_unknown_initial_param_preserves_state();
#else
  test_terminal_ansi_sequence_display_list_legacy_skips_widget_cases();
#endif
  printf("[gui] Terminal ANSI sequence display-list: %d/%d passed\n",
         terminal_ansi_sequence_dl_tests_passed,
         terminal_ansi_sequence_dl_tests_run);
  return terminal_ansi_sequence_dl_tests_passed == terminal_ansi_sequence_dl_tests_run ? 0 : 1;
}
