/* boot_ui.c: boot splash lifecycle + hardware warning screen.
 *
 * Draws the CapyOS icon, a live progress bar, and a status text line
 * on the framebuffer during the driver initialization sequence.
 * All rendering goes through the I/O callback struct so this module
 * has no direct dependency on kernel_main.c statics. */
#include "boot/boot_ui.h"

#include <stddef.h>
#include <stdint.h>

#include "boot/boot_menu.h"
#include "branding/capyos_icon_mask.h"

/* Font metrics — must match kernel_main.c FONT_* defines. */
#define FONT_W 8u
#define FONT_H 8u
#define FONT_SCALE 2u
#define CELL_W (FONT_W * FONT_SCALE)
#define CELL_H (FONT_H * FONT_SCALE)

/* Cached I/O and layout computed at boot_ui_init(). */
static struct boot_ui_io g_io;
static int g_ui_ready = 0;

static struct {
  uint32_t icon_x, icon_y;
  uint32_t icon_scale;
  uint32_t bar_x, bar_y, bar_w, bar_h;
  uint32_t inner_x, inner_y, inner_w, inner_h;
  uint32_t percent_y;
  uint32_t status_y;
} g_layout;

static inline void boot_ui_dbg_putc(char ch) {
  __asm__ volatile("outb %0, %1" : : "a"((uint8_t)ch), "Nd"((uint16_t)0xE9));
}

/* ---- helpers ------------------------------------------------------------ */

static uint32_t str_len(const char *s) {
  uint32_t n = 0;
  if (s) {
    while (s[n]) {
      ++n;
    }
  }
  return n;
}

static void str_copy(char *dst, uint32_t cap, const char *src) {
  uint32_t i = 0;
  if (!dst || cap == 0) {
    return;
  }
  if (src) {
    while (src[i] && i < cap - 1) {
      dst[i] = src[i];
      ++i;
    }
  }
  dst[i] = '\0';
}

static void draw_text_centered(const char *text, uint32_t y,
                               uint32_t fg, uint32_t bg) {
  uint32_t len = str_len(text);
  uint32_t text_w = len * CELL_W;
  uint32_t x = (g_io.screen_w > text_w) ? (g_io.screen_w - text_w) / 2u : 0;

  for (uint32_t i = 0; i < len; ++i) {
    g_io.putch_at(x + i * CELL_W, y, text[i], fg, bg);
  }
}

static void clear_text_line(uint32_t y) {
  g_io.fill_rect(0, y, g_io.screen_w, CELL_H, g_io.splash_bg);
}

static void draw_progress_percent(uint32_t stage, uint32_t total) {
  (void)stage;
  (void)total;
  clear_text_line(g_layout.percent_y);
}

/* ---- public API --------------------------------------------------------- */

void boot_warnings_init(struct boot_warnings *w) {
  if (!w) {
    return;
  }
  w->count = 0;
  for (uint32_t i = 0; i < BOOT_WARNING_MAX; ++i) {
    w->messages[i][0] = '\0';
  }
}

void boot_warnings_add(struct boot_warnings *w, const char *msg) {
  if (!w || !msg || w->count >= BOOT_WARNING_MAX) {
    return;
  }
  str_copy(w->messages[w->count], BOOT_WARNING_TEXT_MAX, msg);
  w->count++;
}

/* Keep the early splash path scalar. VMware/OVMF is sensitive to auto-
 * vectorized XMM moves before the kernel has fully stabilized CPU/runtime
 * state after ExitBootServices. */
__attribute__((optimize("O0")))
void boot_ui_init(const struct boot_ui_io *io) {
  if (!io) {
    return;
  }
  g_io.screen_w = io->screen_w;
  g_io.screen_h = io->screen_h;
  g_io.splash_bg = io->splash_bg;
  g_io.splash_icon = io->splash_icon;
  g_io.splash_bar_border = io->splash_bar_border;
  g_io.splash_bar_bg = io->splash_bar_bg;
  g_io.splash_bar_fill = io->splash_bar_fill;
  g_io.text_fg = io->text_fg;
  g_io.text_muted_fg = io->text_muted_fg;
  g_io.console_bg = io->console_bg;
  g_io.fill_rect = io->fill_rect;
  g_io.putch_at = io->putch_at;
  g_io.draw_icon = io->draw_icon;
  g_ui_ready = 1;
}

__attribute__((optimize("O0")))
void boot_ui_splash_begin(void) {
  uint32_t scale;
  uint32_t icon_w, icon_h;
  uint32_t bar_w, bar_h;
  uint32_t total_h;

  boot_ui_dbg_putc('a');

  if (!g_ui_ready) {
    return;
  }

  /* Icon scale — same logic as the original ui_boot_splash(). */
  scale = (g_io.screen_h / 4u) / CAPYOS_ICON_H;
  if (scale == 0) {
    scale = 1;
  }
  if (scale > 4) {
    scale = 4;
  }
  icon_w = CAPYOS_ICON_W * scale;
  icon_h = CAPYOS_ICON_H * scale;

  bar_w = g_io.screen_w / 3u;
  if (bar_w < 140) {
    bar_w = 140;
  } else if (bar_w > 420) {
    bar_w = 420;
  }
  bar_h = g_io.screen_h / 96u;
  if (bar_h < 8) {
    bar_h = 8;
  } else if (bar_h > 14) {
    bar_h = 14;
  }

  total_h = icon_h + (scale * 10u) + bar_h + (CELL_H * 2u) + 18u;

  g_layout.icon_scale = scale;
  g_layout.icon_x = (g_io.screen_w > icon_w)
                         ? (g_io.screen_w - icon_w) / 2u
                         : 0;
  g_layout.icon_y = (g_io.screen_h > total_h)
                        ? (g_io.screen_h - total_h) / 2u
                        : 0;
  g_layout.bar_w = bar_w;
  g_layout.bar_h = bar_h;
  g_layout.bar_x = (g_io.screen_w > bar_w)
                       ? (g_io.screen_w - bar_w) / 2u
                       : 0;
  g_layout.bar_y = g_layout.icon_y + icon_h + (scale * 10u);

  g_layout.inner_x = g_layout.bar_x;
  g_layout.inner_y = g_layout.bar_y;
  g_layout.inner_w = bar_w;
  g_layout.inner_h = bar_h;
  if (bar_w > 4u) {
    g_layout.inner_x += 2u;
    g_layout.inner_w -= 4u;
  }
  if (bar_h > 4u) {
    g_layout.inner_y += 2u;
    g_layout.inner_h -= 4u;
  }

  g_layout.percent_y = g_layout.bar_y + bar_h + 8u;
  g_layout.status_y = g_layout.percent_y + CELL_H + 4u;

  /* Draw initial splash: background + icon + empty bar. */
  g_io.fill_rect(0, 0, g_io.screen_w, g_io.screen_h, g_io.splash_bg);
  boot_ui_dbg_putc('b');
  g_io.draw_icon(g_layout.icon_x, g_layout.icon_y, g_layout.icon_scale,
                 g_io.splash_icon);
  boot_ui_dbg_putc('c');
  g_io.fill_rect(g_layout.bar_x, g_layout.bar_y, g_layout.bar_w,
                 g_layout.bar_h, g_io.splash_bar_border);
  boot_ui_dbg_putc('d');
  g_io.fill_rect(g_layout.inner_x, g_layout.inner_y, g_layout.inner_w,
                 g_layout.inner_h, g_io.splash_bar_bg);
  boot_ui_dbg_putc('e');
  draw_progress_percent(0, 100u);
  boot_ui_dbg_putc('f');
}

__attribute__((optimize("O0")))
void boot_ui_splash_advance(uint32_t stage, uint32_t total) {
  uint32_t fill_w;

  if (!g_ui_ready || total == 0) {
    return;
  }

  if (stage > total) {
    stage = total;
  }
  fill_w = (g_layout.inner_w * stage) / total;

  /* Redraw inner bar. */
  g_io.fill_rect(g_layout.inner_x, g_layout.inner_y, g_layout.inner_w,
                 g_layout.inner_h, g_io.splash_bar_bg);
  if (fill_w > 0) {
    g_io.fill_rect(g_layout.inner_x, g_layout.inner_y, fill_w,
                   g_layout.inner_h, g_io.splash_bar_fill);
  }
  draw_progress_percent(stage, total);
}

__attribute__((optimize("O0")))
void boot_ui_splash_set_status(const char *text) {
  if (!g_ui_ready) {
    return;
  }
  clear_text_line(g_layout.status_y);
  (void)text;
}

__attribute__((optimize("O0")))
void boot_ui_splash_end(void) {
  if (!g_ui_ready) {
    return;
  }
  g_io.fill_rect(0, 0, g_io.screen_w, g_io.screen_h, g_io.console_bg);
}

int boot_ui_show_warnings(const struct boot_warnings *w,
                          int (*getc)(char *out)) {
  struct boot_menu menu;
  struct boot_menu_io mio;
  uint32_t y;

  if (!g_ui_ready || !w || w->count == 0) {
    return 0;
  }

  g_io.fill_rect(0, 0, g_io.screen_w, g_io.screen_h, g_io.console_bg);

  /* Title. */
  y = CELL_H * 2u;
  draw_text_centered("Hardware Compatibility", y, 0x00FFCC00, g_io.console_bg);
  y += CELL_H + 4u;

  /* Warnings list. */
  y += CELL_H;
  for (uint32_t i = 0; i < w->count; ++i) {
    uint32_t len = str_len(w->messages[i]);
    /* "! " prefix + message text, left-aligned with padding. */
    uint32_t x = CELL_W * 4u;
    g_io.putch_at(x, y, '!', 0x00FF6666, g_io.console_bg);
    x += CELL_W * 2u;
    for (uint32_t j = 0; j < len; ++j) {
      g_io.putch_at(x + j * CELL_W, y, w->messages[i][j],
                    g_io.text_fg, g_io.console_bg);
    }
    y += CELL_H + 2u;
  }

  y += CELL_H;
  draw_text_centered("Some components were not detected.",
                     y, 0x00AAAAAA, g_io.console_bg);
  y += CELL_H;
  draw_text_centered("The system may not work correctly.",
                     y, 0x00AAAAAA, g_io.console_bg);

  /* Decision menu. */
  mio.screen_w = g_io.screen_w;
  mio.screen_h = g_io.screen_h;
  mio.bg = g_io.console_bg;
  mio.fg = g_io.text_fg;
  mio.highlight_bg = 0x00335577;
  mio.highlight_fg = 0x00FFFFFF;
  mio.title_fg = 0x0000C364;
  mio.fill_rect = g_io.fill_rect;
  mio.putch_at = g_io.putch_at;

  boot_menu_init(&menu, NULL);
  boot_menu_add(&menu, "Continue anyway", 0);
  boot_menu_add(&menu, "Halt system", 1);

  return boot_menu_run(&menu, &mio, getc);
}
