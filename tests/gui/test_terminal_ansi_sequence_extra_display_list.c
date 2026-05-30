#include <stdio.h>
#include <stdint.h>

#include "gui/core/internal/compositor_internal.h"
#include "gui/event.h"
#include "gui/terminal.h"

static int terminal_ansi_sequence_extra_dl_tests_run = 0;
static int terminal_ansi_sequence_extra_dl_tests_passed = 0;

#define TEST(name) do { terminal_ansi_sequence_extra_dl_tests_run++; printf("  %-62s ", name); } while (0)
#define PASS() do { printf("OK\n"); terminal_ansi_sequence_extra_dl_tests_passed++; } while (0)
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

static void test_terminal_ansi_sequence_extra_display_list_multi_param_0_foreground_empty_39_then_background(void) {
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
  win = compositor_create_window("TermAnsiSeqExtra0FgEmpty39BgDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence extra display-list 0 fg empty fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;34;42m\033[0;92;;39;101mG");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence extra display-list 0 foreground empty 39 background");
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
  else FAIL("terminal ANSI sequence extra 0 foreground empty 39 background");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_extra_display_list_multi_param_0_background_empty_49_then_foreground(void) {
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
  win = compositor_create_window("TermAnsiSeqExtra0BgEmpty49FgDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence extra display-list 0 bg empty fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;34;42m\033[0;101;;49;92mG");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence extra display-list 0 background empty 49 foreground");
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
  else FAIL("terminal ANSI sequence extra 0 background empty 49 foreground");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_extra_display_list_multi_param_0_foreground_unknown_empty_then_background(void) {
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
  win = compositor_create_window("TermAnsiSeqExtra0FgUnknownEmptyBgDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence extra display-list 0 fg unknown empty fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;34;42m\033[0;92;8;;101mG");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence extra display-list 0 foreground unknown empty background");
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
  else FAIL("terminal ANSI sequence extra 0 foreground unknown empty background");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_extra_display_list_multi_param_0_background_unknown_empty_then_foreground(void) {
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
  win = compositor_create_window("TermAnsiSeqExtra0BgUnknownEmptyFgDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence extra display-list 0 bg unknown empty fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;34;42m\033[0;101;8;;92mG");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence extra display-list 0 background unknown empty foreground");
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
  else FAIL("terminal ANSI sequence extra 0 background unknown empty foreground");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_extra_display_list_multi_param_colors_unknown_empty_final_reset(void) {
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
  win = compositor_create_window("TermAnsiSeqExtraColorsUnknownEmptyFinalDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence extra display-list colors unknown empty fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;34;42m\033[0;92;101;8;mG");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence extra display-list colors unknown empty final reset");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     term.default_fg) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     term.default_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bright_green) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bright_red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bold_blue) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      green_bg)) PASS();
  else FAIL("terminal ANSI sequence extra colors unknown empty final reset");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_extra_display_list_multi_param_empty_unknown_then_colors(void) {
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
  win = compositor_create_window("TermAnsiSeqExtraEmptyUnknownColorsDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence extra display-list empty unknown fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;34;42m\033[0;;8;92;101mG");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence extra display-list empty unknown then colors");
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
                      term.default_fg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_bg)) PASS();
  else FAIL("terminal ANSI sequence extra empty unknown then colors");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_extra_display_list_multi_param_multiple_empty_then_colors(void) {
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
  win = compositor_create_window("TermAnsiSeqExtraMultipleEmptyColorsDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence extra display-list multiple empty fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;34;42m\033[0;;;92;101mG");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence extra display-list multiple empty then colors");
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
                      term.default_fg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_bg)) PASS();
  else FAIL("terminal ANSI sequence extra multiple empty then colors");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_extra_display_list_multi_param_colors_multiple_empty_final_reset(void) {
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
  win = compositor_create_window("TermAnsiSeqExtraColorsMultipleEmptyFinalDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence extra display-list colors multiple empty fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;34;42m\033[0;92;101;;;mG");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence extra display-list colors multiple empty final reset");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     term.default_fg) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     term.default_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bright_green) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bright_red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bold_blue) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      green_bg)) PASS();
  else FAIL("terminal ANSI sequence extra colors multiple empty final reset");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_extra_display_list_multi_param_multiple_empty_then_bold_colors(void) {
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
  win = compositor_create_window("TermAnsiSeqExtraMultipleEmptyBoldColorsDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence extra display-list multiple empty bold fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[2;34;45m\033[0;;;1;31;42mR");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence extra display-list multiple empty then bold colors");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     bold_red) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     green_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      dim_blue) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      magenta_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_fg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_bg)) PASS();
  else FAIL("terminal ANSI sequence extra multiple empty then bold colors");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_extra_display_list_multi_param_multiple_empty_then_dim_colors(void) {
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
  win = compositor_create_window("TermAnsiSeqExtraMultipleEmptyDimColorsDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence extra display-list multiple empty dim fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;34;45m\033[0;;;2;31;42mR");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence extra display-list multiple empty then dim colors");
  if (f &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     dim_red) &&
      cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                     green_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      red) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      bold_blue) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      magenta_bg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_fg) &&
      !cell_has_color(win, 0u, 0u, f->glyph_width, f->glyph_height,
                      term.default_bg)) PASS();
  else FAIL("terminal ANSI sequence extra multiple empty then dim colors");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_extra_display_list_multi_param_multiple_empty_bold_22_then_colors(void) {
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
  win = compositor_create_window("TermAnsiSeqExtraMultipleEmptyBold22ColorsDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence extra display-list multiple empty bold 22 fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[2;34;45m\033[0;;;1;22;31;42mR");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence extra display-list multiple empty bold 22 colors");
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
  else FAIL("terminal ANSI sequence extra multiple empty bold 22 colors");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_extra_display_list_multi_param_multiple_empty_dim_22_then_colors(void) {
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
  win = compositor_create_window("TermAnsiSeqExtraMultipleEmptyDim22ColorsDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence extra display-list multiple empty dim 22 fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;34;45m\033[0;;;2;22;31;42mR");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence extra display-list multiple empty dim 22 colors");
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
  else FAIL("terminal ANSI sequence extra multiple empty dim 22 colors");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_extra_display_list_multi_param_multiple_empty_bold_unknown_22_then_colors(void) {
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
  win = compositor_create_window("TermAnsiSeqExtraMultipleEmptyBoldUnknown22ColorsDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence extra display-list multiple empty bold unknown 22 fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[2;34;45m\033[;;;1;8;22;31;42mR");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence extra display-list multiple empty bold unknown 22 colors");
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
  else FAIL("terminal ANSI sequence extra multiple empty bold unknown 22 colors");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_extra_display_list_multi_param_multiple_empty_dim_unknown_22_then_colors(void) {
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
  win = compositor_create_window("TermAnsiSeqExtraMultipleEmptyDimUnknown22ColorsDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence extra display-list multiple empty dim unknown 22 fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;34;45m\033[;;;2;8;22;31;42mR");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence extra display-list multiple empty dim unknown 22 colors");
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
  else FAIL("terminal ANSI sequence extra multiple empty dim unknown 22 colors");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_extra_display_list_multi_param_multiple_empty_foreground_39_then_background(void) {
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
  win = compositor_create_window("TermAnsiSeqExtraMultipleEmptyFg39BgDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence extra display-list multiple empty fg 39 fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;34;42m\033[;;;92;39;101mG");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence extra display-list multiple empty foreground 39 background");
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
  else FAIL("terminal ANSI sequence extra multiple empty foreground 39 background");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_extra_display_list_multi_param_multiple_empty_background_49_then_foreground(void) {
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
  win = compositor_create_window("TermAnsiSeqExtraMultipleEmptyBg49FgDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence extra display-list multiple empty bg 49 fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;34;42m\033[;;;101;49;92mG");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence extra display-list multiple empty background 49 foreground");
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
  else FAIL("terminal ANSI sequence extra multiple empty background 49 foreground");
  compositor_shutdown();
  gui_event_init();
}

static void test_terminal_ansi_sequence_extra_display_list_multi_param_multiple_empty_foreground_unknown_39_then_background(void) {
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
  win = compositor_create_window("TermAnsiSeqExtraMultipleEmptyFgUnknown39BgDL", 4, 24, 80u, 48u);
  if (!win) {
    TEST("terminal ANSI sequence extra display-list multiple empty fg unknown 39 fixture creates window");
    FAIL("create window");
    compositor_shutdown();
    gui_event_init();
    return;
  }
  terminal_init(&term, win);
  clear_pixels(win->surface.pixels, win->surface.width * win->surface.height, 0u);
  terminal_write_string(&term, "\033[1;34;42m\033[;;;92;8;39;101mG");
  terminal_paint(&term);
  f = term.font;
  TEST("terminal ANSI sequence extra display-list multiple empty foreground unknown 39 background");
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
  else FAIL("terminal ANSI sequence extra multiple empty foreground unknown 39 background");
  compositor_shutdown();
  gui_event_init();
}
#else
static void test_terminal_ansi_sequence_extra_display_list_legacy_skips_widget_cases(void) {
  TEST("terminal ANSI sequence extra display-list widget-only cases skipped");
  PASS();
}
#endif

int test_terminal_ansi_sequence_extra_display_list_run(void) {
  terminal_ansi_sequence_extra_dl_tests_run = 0;
  terminal_ansi_sequence_extra_dl_tests_passed = 0;
  printf("[gui] Terminal ANSI sequence extra display-list tests\n");
#if defined(CAPYOS_HAVE_CAPYUI_WIDGET)
  test_terminal_ansi_sequence_extra_display_list_multi_param_0_foreground_empty_39_then_background();
  test_terminal_ansi_sequence_extra_display_list_multi_param_0_background_empty_49_then_foreground();
  test_terminal_ansi_sequence_extra_display_list_multi_param_0_foreground_unknown_empty_then_background();
  test_terminal_ansi_sequence_extra_display_list_multi_param_0_background_unknown_empty_then_foreground();
  test_terminal_ansi_sequence_extra_display_list_multi_param_colors_unknown_empty_final_reset();
  test_terminal_ansi_sequence_extra_display_list_multi_param_empty_unknown_then_colors();
  test_terminal_ansi_sequence_extra_display_list_multi_param_multiple_empty_then_colors();
  test_terminal_ansi_sequence_extra_display_list_multi_param_colors_multiple_empty_final_reset();
  test_terminal_ansi_sequence_extra_display_list_multi_param_multiple_empty_then_bold_colors();
  test_terminal_ansi_sequence_extra_display_list_multi_param_multiple_empty_then_dim_colors();
  test_terminal_ansi_sequence_extra_display_list_multi_param_multiple_empty_bold_22_then_colors();
  test_terminal_ansi_sequence_extra_display_list_multi_param_multiple_empty_dim_22_then_colors();
  test_terminal_ansi_sequence_extra_display_list_multi_param_multiple_empty_bold_unknown_22_then_colors();
  test_terminal_ansi_sequence_extra_display_list_multi_param_multiple_empty_dim_unknown_22_then_colors();
  test_terminal_ansi_sequence_extra_display_list_multi_param_multiple_empty_foreground_39_then_background();
  test_terminal_ansi_sequence_extra_display_list_multi_param_multiple_empty_background_49_then_foreground();
  test_terminal_ansi_sequence_extra_display_list_multi_param_multiple_empty_foreground_unknown_39_then_background();
#else
  test_terminal_ansi_sequence_extra_display_list_legacy_skips_widget_cases();
#endif
  printf("[gui] Terminal ANSI sequence extra display-list: %d/%d passed\n",
         terminal_ansi_sequence_extra_dl_tests_passed,
         terminal_ansi_sequence_extra_dl_tests_run);
  return terminal_ansi_sequence_extra_dl_tests_run - terminal_ansi_sequence_extra_dl_tests_passed;
}
