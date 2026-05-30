#include <stdio.h>
#include <stdint.h>

#include "gui/core/internal/compositor_internal.h"
#include "gui/event.h"
#include "gui/terminal.h"

static int terminal_ansi_sequence_more_dl_tests_run = 0;
static int terminal_ansi_sequence_more_dl_tests_passed = 0;

#define TEST(name) do { terminal_ansi_sequence_more_dl_tests_run++; printf("  %-62s ", name); } while (0)
#define PASS() do { printf("OK\n"); terminal_ansi_sequence_more_dl_tests_passed++; } while (0)
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

static void test_terminal_ansi_sequence_more_display_list_multi_param_0_final_resets_colors(void) {
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
  win = compositor_create_window("TermAnsiSeqMore0FinalDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence more display-list 0 final fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;31;42;0mR");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence more display-list multi-param 0 final reset");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     term.default_fg) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     term.default_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height, red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bold_red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      green_bg)) PASS();
  else FAIL("terminal ANSI sequence more multi-param 0 final reset");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_more_display_list_multi_param_0_initial_then_colors(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t bold_blue = 0x007373CCu;
  uint32_t green_bg = 0x0033CC33u;
  uint32_t bright_green = 0x0073CC73u;
  uint32_t bright_red = 0x00CC7373u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSeqMore0InitialDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence more display-list 0 initial fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;34;42m\033[0;92;101mG");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence more display-list multi-param 0 initial colors");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bright_green) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bright_red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bold_blue) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      green_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_bg)) PASS();
  else FAIL("terminal ANSI sequence more multi-param 0 initial colors");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_more_display_list_multi_param_0_then_unknown_then_color(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t bold_blue = 0x007373CCu;
  uint32_t green_bg = 0x0033CC33u;
  uint32_t bright_green = 0x0073CC73u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSeqMore0UnknownDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence more display-list 0 unknown fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;34;42m\033[0;999;92mG");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence more display-list 0 unknown then color");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bright_green) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     term.default_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bold_blue) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      green_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_fg)) PASS();
  else FAIL("terminal ANSI sequence more 0 unknown then color");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_more_display_list_multi_param_unknown_then_0_then_color(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t bold_blue = 0x007373CCu;
  uint32_t green_bg = 0x0033CC33u;
  uint32_t bright_green = 0x0073CC73u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSeqMoreUnknown0DL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence more display-list unknown 0 fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;34;42m\033[999;0;92mG");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence more display-list unknown 0 then color");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bright_green) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     term.default_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bold_blue) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      green_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_fg)) PASS();
  else FAIL("terminal ANSI sequence more unknown 0 then color");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_more_display_list_multi_param_0_then_empty_then_color(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t bold_blue = 0x007373CCu;
  uint32_t green_bg = 0x0033CC33u;
  uint32_t bright_green = 0x0073CC73u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSeqMore0EmptyDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence more display-list 0 empty fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;34;42m\033[0;;92mG");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence more display-list 0 empty then color");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bright_green) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     term.default_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bold_blue) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      green_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_fg)) PASS();
  else FAIL("terminal ANSI sequence more 0 empty then color");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_more_display_list_multi_param_0_color_empty_then_background(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t bold_blue = 0x007373CCu;
  uint32_t green_bg = 0x0033CC33u;
  uint32_t bright_green = 0x0073CC73u;
  uint32_t bright_red = 0x00CC7373u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSeqMore0ColorEmptyBgDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence more display-list 0 color empty fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;34;42m\033[0;92;;101mG");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence more display-list 0 color empty background");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     term.default_fg) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bright_red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bright_green) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bold_blue) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      green_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_bg)) PASS();
  else FAIL("terminal ANSI sequence more 0 color empty background");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_more_display_list_multi_param_0_background_49_then_foreground(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t bold_blue = 0x007373CCu;
  uint32_t green_bg = 0x0033CC33u;
  uint32_t bright_green = 0x0073CC73u;
  uint32_t bright_red = 0x00CC7373u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSeqMore0Bg49FgDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence more display-list 0 bg 49 fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;34;42m\033[0;101;49;92mG");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence more display-list 0 background 49 foreground");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bright_green) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     term.default_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bright_red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bold_blue) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      green_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_fg)) PASS();
  else FAIL("terminal ANSI sequence more 0 background 49 foreground");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_more_display_list_multi_param_0_foreground_39_then_background(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t bold_blue = 0x007373CCu;
  uint32_t green_bg = 0x0033CC33u;
  uint32_t bright_green = 0x0073CC73u;
  uint32_t bright_red = 0x00CC7373u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSeqMore0Fg39BgDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence more display-list 0 fg 39 fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;34;42m\033[0;92;39;101mG");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence more display-list 0 foreground 39 background");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     term.default_fg) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bright_red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bright_green) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bold_blue) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      green_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_bg)) PASS();
  else FAIL("terminal ANSI sequence more 0 foreground 39 background");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_more_display_list_multi_param_0_then_dim_colors(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  uint32_t dim_red = 0x00661919u;
  uint32_t bold_blue = 0x007373CCu;
  uint32_t green_bg = 0x0033CC33u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSeqMore0DimColorsDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence more display-list 0 dim colors fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;34;42m\033[0;2;31;42mR");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence more display-list 0 then dim colors");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     dim_red) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     green_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height, red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bold_blue) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_fg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_bg)) PASS();
  else FAIL("terminal ANSI sequence more 0 then dim colors");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_more_display_list_multi_param_0_then_bold_colors(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  uint32_t bold_red = 0x00CC7373u;
  uint32_t dim_blue = 0x00191966u;
  uint32_t magenta_bg = 0x00CC33CCu;
  uint32_t green_bg = 0x0033CC33u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSeqMore0BoldColorsDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence more display-list 0 bold colors fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[2;34;45m\033[0;1;31;42mR");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence more display-list 0 then bold colors");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bold_red) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     green_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height, red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      dim_blue) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      magenta_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_fg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_bg)) PASS();
  else FAIL("terminal ANSI sequence more 0 then bold colors");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_more_display_list_multi_param_0_bold_22_then_colors(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  uint32_t bold_red = 0x00CC7373u;
  uint32_t dim_blue = 0x00191966u;
  uint32_t magenta_bg = 0x00CC33CCu;
  uint32_t green_bg = 0x0033CC33u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSeqMore0Bold22ColorsDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence more display-list 0 bold 22 fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[2;34;45m\033[0;1;22;31;42mR");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence more display-list 0 bold 22 colors");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height, red) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     green_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bold_red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      dim_blue) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      magenta_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_fg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_bg)) PASS();
  else FAIL("terminal ANSI sequence more 0 bold 22 colors");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_more_display_list_multi_param_0_dim_22_then_colors(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  uint32_t dim_red = 0x00661919u;
  uint32_t bold_blue = 0x007373CCu;
  uint32_t magenta_bg = 0x00CC33CCu;
  uint32_t green_bg = 0x0033CC33u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSeqMore0Dim22ColorsDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence more display-list 0 dim 22 fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;34;45m\033[0;2;22;31;42mR");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence more display-list 0 dim 22 colors");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height, red) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     green_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      dim_red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bold_blue) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      magenta_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_fg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_bg)) PASS();
  else FAIL("terminal ANSI sequence more 0 dim 22 colors");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_more_display_list_multi_param_0_bold_unknown_22_then_colors(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  uint32_t bold_red = 0x00CC7373u;
  uint32_t dim_blue = 0x00191966u;
  uint32_t magenta_bg = 0x00CC33CCu;
  uint32_t green_bg = 0x0033CC33u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSeqMore0BoldUnknown22ColorsDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence more display-list 0 bold unknown 22 fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[2;34;45m\033[0;1;8;22;31;42mR");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence more display-list 0 bold unknown 22 colors");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height, red) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     green_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bold_red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      dim_blue) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      magenta_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_fg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_bg)) PASS();
  else FAIL("terminal ANSI sequence more 0 bold unknown 22 colors");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_more_display_list_multi_param_0_dim_unknown_22_then_colors(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  uint32_t dim_red = 0x00661919u;
  uint32_t bold_blue = 0x007373CCu;
  uint32_t magenta_bg = 0x00CC33CCu;
  uint32_t green_bg = 0x0033CC33u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSeqMore0DimUnknown22ColorsDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence more display-list 0 dim unknown 22 fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;34;45m\033[0;2;8;22;31;42mR");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence more display-list 0 dim unknown 22 colors");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height, red) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     green_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      dim_red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bold_blue) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      magenta_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_fg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_bg)) PASS();
  else FAIL("terminal ANSI sequence more 0 dim unknown 22 colors");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_more_display_list_multi_param_0_background_unknown_49_then_foreground(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t bold_blue = 0x007373CCu;
  uint32_t green_bg = 0x0033CC33u;
  uint32_t bright_green = 0x0073CC73u;
  uint32_t bright_red = 0x00CC7373u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSeqMore0BgUnknown49FgDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence more display-list 0 bg unknown 49 fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;34;42m\033[0;101;8;49;92mG");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence more display-list 0 background unknown 49 foreground");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bright_green) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     term.default_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bright_red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bold_blue) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      green_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_fg)) PASS();
  else FAIL("terminal ANSI sequence more 0 background unknown 49 foreground");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_more_display_list_multi_param_0_foreground_unknown_39_then_background(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t bold_blue = 0x007373CCu;
  uint32_t green_bg = 0x0033CC33u;
  uint32_t bright_green = 0x0073CC73u;
  uint32_t bright_red = 0x00CC7373u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSeqMore0FgUnknown39BgDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence more display-list 0 fg unknown 39 fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;34;42m\033[0;92;8;39;101mG");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence more display-list 0 foreground unknown 39 background");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     term.default_fg) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bright_red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bright_green) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bold_blue) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      green_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_bg)) PASS();
  else FAIL("terminal ANSI sequence more 0 foreground unknown 39 background");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_more_display_list_multi_param_0_bold_empty_then_colors(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  uint32_t bold_red = 0x00CC7373u;
  uint32_t dim_blue = 0x00191966u;
  uint32_t magenta_bg = 0x00CC33CCu;
  uint32_t green_bg = 0x0033CC33u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSeqMore0BoldEmptyColorsDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence more display-list 0 bold empty fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[2;34;45m\033[0;1;;31;42mR");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence more display-list 0 bold empty colors");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height, red) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     green_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bold_red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      dim_blue) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      magenta_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_fg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_bg)) PASS();
  else FAIL("terminal ANSI sequence more 0 bold empty colors");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_more_display_list_multi_param_0_dim_empty_then_colors(void) {
  static uint32_t framebuffer[128u * 96u];
  struct gui_window *win;
  struct terminal term;
  const struct font *f;
  uint32_t red = 0x00CC3333u;
  uint32_t dim_red = 0x00661919u;
  uint32_t bold_blue = 0x007373CCu;
  uint32_t magenta_bg = 0x00CC33CCu;
  uint32_t green_bg = 0x0033CC33u;
  gui_event_init();
  clear_pixels(framebuffer, 128u * 96u, 0u);
  compositor_init(framebuffer, 128u, 96u, 128u * 4u);
  win = compositor_create_window("TermAnsiSeqMore0DimEmptyColorsDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence more display-list 0 dim empty fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;34;45m\033[0;2;;31;42mR");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence more display-list 0 dim empty colors");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height, red) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     green_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      dim_red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bold_blue) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      magenta_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_fg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_bg)) PASS();
  else FAIL("terminal ANSI sequence more 0 dim empty colors");
  compositor_shutdown();
  gui_event_init();
}
#else
static void test_terminal_ansi_sequence_more_display_list_legacy_skips_widget_cases(void) {
  TEST("terminal ANSI sequence more display-list widget-only cases skipped");
  PASS();
}
#endif

int test_terminal_ansi_sequence_more_display_list_run(void) {
  terminal_ansi_sequence_more_dl_tests_run = 0;
  terminal_ansi_sequence_more_dl_tests_passed = 0;
  printf("\n[gui] Terminal ANSI sequence more display-list tests\n");
#if defined(CAPYOS_HAVE_CAPYUI_WIDGET)
  test_terminal_ansi_sequence_more_display_list_multi_param_0_final_resets_colors();
  test_terminal_ansi_sequence_more_display_list_multi_param_0_initial_then_colors();
  test_terminal_ansi_sequence_more_display_list_multi_param_0_then_unknown_then_color();
  test_terminal_ansi_sequence_more_display_list_multi_param_unknown_then_0_then_color();
  test_terminal_ansi_sequence_more_display_list_multi_param_0_then_empty_then_color();
  test_terminal_ansi_sequence_more_display_list_multi_param_0_color_empty_then_background();
  test_terminal_ansi_sequence_more_display_list_multi_param_0_background_49_then_foreground();
  test_terminal_ansi_sequence_more_display_list_multi_param_0_foreground_39_then_background();
  test_terminal_ansi_sequence_more_display_list_multi_param_0_then_dim_colors();
  test_terminal_ansi_sequence_more_display_list_multi_param_0_then_bold_colors();
  test_terminal_ansi_sequence_more_display_list_multi_param_0_bold_22_then_colors();
  test_terminal_ansi_sequence_more_display_list_multi_param_0_dim_22_then_colors();
  test_terminal_ansi_sequence_more_display_list_multi_param_0_bold_unknown_22_then_colors();
  test_terminal_ansi_sequence_more_display_list_multi_param_0_dim_unknown_22_then_colors();
  test_terminal_ansi_sequence_more_display_list_multi_param_0_background_unknown_49_then_foreground();
  test_terminal_ansi_sequence_more_display_list_multi_param_0_foreground_unknown_39_then_background();
  test_terminal_ansi_sequence_more_display_list_multi_param_0_bold_empty_then_colors();
  test_terminal_ansi_sequence_more_display_list_multi_param_0_dim_empty_then_colors();
#else
  test_terminal_ansi_sequence_more_display_list_legacy_skips_widget_cases();
#endif
  printf("[gui] Terminal ANSI sequence more display-list: %d/%d passed\n",
         terminal_ansi_sequence_more_dl_tests_passed,
         terminal_ansi_sequence_more_dl_tests_run);
  return terminal_ansi_sequence_more_dl_tests_run - terminal_ansi_sequence_more_dl_tests_passed;
}
