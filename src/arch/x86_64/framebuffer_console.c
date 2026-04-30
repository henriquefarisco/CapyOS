/* framebuffer_console.c — Framebuffer text console for x86_64 kernel.
 *
 * Owns: fbcon_t g_con, serial mirror state, visual muting, all fbcon_* draw
 * functions, kernel_desktop_get_* accessors, theme apply/sync, and the
 * platform diag I/O builder.
 *
 * Split from kernel_main.c to keep each TU ≤ 500 lines.
 */
#pragma GCC optimize("O0")
#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/kernel_main_internal.h"
#include "drivers/serial/serial_com1.h"
#include "gui/font8x8.h"
#include "kernel/log/klog.h"
#include "arch/x86_64/kernel_platform_runtime.h"

/* ── global state (owned here, extern'd via internal header) ─────────── */

fbcon_t g_con;
int g_serial_mirror = 0;
int g_com1_ready = 0;

static int fbcon_range_ok(uint64_t addr, uint64_t size) {
  if (addr == 0 || size == 0) {
    return 0;
  }
  if (addr + size < addr) {
    return 0;
  }
  return addr + size <= 0x40000000ULL;
}

/* ── visual muting (redirects output to klog while splash is active) ── */

static int g_fbcon_visual_muted = 0;
static char g_fbcon_muted_line[KLOG_LINE_MAX];
static uint32_t g_fbcon_muted_len = 0;

/* ── init ────────────────────────────────────────────────────────────── */

void fbcon_init(const struct boot_handoff *h) {
  if (!h)
    return;
  g_con.fb = fbcon_range_ok(h->fb.base, h->fb.size)
                 ? (uint32_t *)(uintptr_t)h->fb.base
                 : NULL;
  g_con.width = h->fb.width;
  g_con.height = h->fb.height;
  g_con.stride = h->fb.pitch / 4u;
  g_con.size_bytes = h->fb.size;
  if (g_con.stride == 0) {
    g_con.width = 0;
    g_con.height = 0;
    return;
  }
  if (g_con.width > g_con.stride) {
    g_con.width = g_con.stride;
  }
  if (h->fb.pitch > 0 && h->fb.size > 0) {
    uint32_t max_rows = h->fb.size / h->fb.pitch;
    if (max_rows < g_con.height) {
      g_con.height = max_rows;
    }
  }
  g_con.origin_y = 0;
  g_con.cols = g_con.width / CELL_W;
  g_con.rows = g_con.height / CELL_H;
  g_con.col = 0;
  g_con.row = 0;
  g_con.fg = 0x00F0F0F0;
  g_con.bg = 0x00102030;
}

/* ── theme ───────────────────────────────────────────────────────────── */

void system_platform_apply_theme(const char *theme) {
  if (!theme || streq(theme, "capyos")) {
    g_con.bg = 0x00102030;
    g_con.fg = 0x00F0F0F0;
    g_theme_splash_bg = 0x000A1713;
    g_theme_splash_icon = 0x0000A651;
    g_theme_splash_bar_border = 0x00213A31;
    g_theme_splash_bar_bg = 0x0012221C;
    g_theme_splash_bar_fill = 0x0000C364;
    return;
  }

  if (streq(theme, "ocean")) {
    g_con.bg = 0x000A1B3A;
    g_con.fg = 0x00DDF6FF;
    g_theme_splash_bg = 0x00041024;
    g_theme_splash_icon = 0x0035B7FF;
    g_theme_splash_bar_border = 0x0021476A;
    g_theme_splash_bar_bg = 0x000C213A;
    g_theme_splash_bar_fill = 0x005FD5FF;
    return;
  }

  if (streq(theme, "forest")) {
    g_con.bg = 0x000F2415;
    g_con.fg = 0x00E9F8E7;
    g_theme_splash_bg = 0x000A1710;
    g_theme_splash_icon = 0x002FAE5B;
    g_theme_splash_bar_border = 0x00284A31;
    g_theme_splash_bar_bg = 0x0015231A;
    g_theme_splash_bar_fill = 0x0048D778;
    return;
  }

  system_platform_apply_theme("capyos");
}

void system_platform_sync_theme(const struct system_settings *settings) {
  (void)settings;
  if (!g_con.fb || g_con.width == 0 || g_con.height == 0) {
    return;
  }
  fbcon_fill_rect_px(0, 0, g_con.width, g_con.height, g_con.bg);
  ui_draw_bars();
  g_con.col = 0;
  g_con.row = 0;
}

/* ── low-level drawing ───────────────────────────────────────────────── */

void fbcon_fill_rect_px(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h,
                        uint32_t color) {
  if (!g_con.fb)
    return;
  if (g_con.stride == 0)
    return;
  if (x0 >= g_con.width || y0 >= g_con.height)
    return;
  if (x0 + w > g_con.width)
    w = g_con.width - x0;
  if (y0 + h > g_con.height)
    h = g_con.height - y0;
  if (g_con.size_bytes > 0) {
    uint64_t max_pixels = (uint64_t)g_con.size_bytes / sizeof(uint32_t);
    uint64_t row_start = (uint64_t)y0 * g_con.stride + x0;
    if (row_start >= max_pixels || (uint64_t)w > max_pixels - row_start) {
      return;
    }
    uint64_t max_rows = ((max_pixels - row_start - w) / g_con.stride) + 1u;
    if (max_rows < h) {
      h = (uint32_t)max_rows;
    }
  }
  for (uint32_t y = 0; y < h; y++) {
    uint32_t *row = g_con.fb + (y0 + y) * g_con.stride;
    for (uint32_t x = 0; x < w; x++) {
      row[x0 + x] = color;
    }
  }
}

void fbcon_scroll(void) {
  const uint32_t ch = CELL_H;
  const uint32_t start = g_con.origin_y;
  const uint32_t end = g_con.height;
  if (end <= start + ch)
    return;

  /* Block-copy scroll region using rep movsq for speed. */
  {
    uint32_t *dst = g_con.fb + start * g_con.stride;
    uint32_t *src = g_con.fb + (start + ch) * g_con.stride;
    uint64_t count_dwords = (uint64_t)(end - start - ch) * g_con.stride;
    uint64_t count_qwords = count_dwords / 2;
    if (count_qwords > 0) {
      __asm__ volatile("rep movsq"
                       : "+D"(dst), "+S"(src), "+c"(count_qwords)
                       :
                       : "memory");
    }
    if (count_dwords & 1) {
      dst[count_qwords * 2] = src[count_qwords * 2];
    }
  }
  fbcon_fill_rect_px(0, end - ch, g_con.width, ch, g_con.bg);
}

void fbcon_putch_px(uint32_t x, uint32_t y, char c) {
  uint8_t uc = (uint8_t)c;
  const uint8_t *glyph = font_glyph(uc);
  if (!g_con.fb)
    return;
  if (x >= g_con.width || y >= g_con.height)
    return;
  for (uint32_t row = 0; row < FONT_H; row++) {
    uint32_t py = y + row * FONT_SCALE;
    if (py >= g_con.height)
      break;
    uint8_t bits = glyph[row];
    for (uint32_t dy = 0; dy < FONT_SCALE; dy++) {
      uint32_t yy = py + dy;
      if (yy >= g_con.height)
        break;
      uint32_t *dst = g_con.fb + yy * g_con.stride;
      for (uint32_t col = 0; col < FONT_W; col++) {
        uint32_t color = (bits & (1u << (7u - col))) ? g_con.fg : g_con.bg;
        uint32_t px = x + col * FONT_SCALE;
        for (uint32_t dx = 0; dx < FONT_SCALE; dx++) {
          if (px + dx >= g_con.width)
            break;
          dst[px + dx] = color;
        }
      }
    }
  }
}

void fbcon_putch_at(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg) {
  uint32_t saved_fg = g_con.fg;
  uint32_t saved_bg = g_con.bg;
  g_con.fg = fg;
  g_con.bg = bg;
  fbcon_putch_px(x, y, c);
  g_con.fg = saved_fg;
  g_con.bg = saved_bg;
}

/* ── visual muting helpers ───────────────────────────────────────────── */

void fbcon_muted_flush_line(void) {
  if (g_fbcon_muted_len == 0) {
    return;
  }
  g_fbcon_muted_line[g_fbcon_muted_len] = '\0';
  klog(KLOG_INFO, g_fbcon_muted_line);
  g_fbcon_muted_len = 0;
}

static void fbcon_capture_muted_char(char c) {
  if (c == '\r') {
    return;
  }
  if (c == '\n') {
    fbcon_muted_flush_line();
    return;
  }
  if (c == '\b') {
    if (g_fbcon_muted_len > 0) {
      --g_fbcon_muted_len;
    }
    return;
  }
  if ((uint8_t)c < 0x20u) {
    return;
  }
  if (g_fbcon_muted_len >= sizeof(g_fbcon_muted_line) - 1u) {
    fbcon_muted_flush_line();
  }
  g_fbcon_muted_line[g_fbcon_muted_len++] = c;
}

void fbcon_set_visual_muted(int muted) {
  if (!muted) {
    fbcon_muted_flush_line();
  }
  g_fbcon_visual_muted = muted ? 1 : 0;
}

/* ── main character output ───────────────────────────────────────────── */

void fbcon_putc(char c) {
  if (!g_con.fb || g_con.cols == 0 || g_con.rows == 0)
    return;
  if (g_serial_mirror && g_com1_ready) {
    if (c == '\n') {
      com1_putc('\r');
    }
    if (c != '\r') {
      com1_putc(c);
    }
  }
  if (g_fbcon_visual_muted) {
    fbcon_capture_muted_char(c);
    return;
  }
  if (c == '\r')
    return;
  if (c == '\n') {
    g_con.col = 0;
    g_con.row++;
    if (g_con.row >= g_con.rows) {
      fbcon_scroll();
      g_con.row = g_con.rows - 1;
    }
    return;
  }
  if (c == '\b') {
    if (g_con.col > 0) {
      g_con.col--;
    } else if (g_con.row > 0) {
      g_con.row--;
      g_con.col = g_con.cols - 1;
    }
    uint32_t x = g_con.col * CELL_W;
    uint32_t y = g_con.origin_y + g_con.row * CELL_H;
    fbcon_putch_px(x, y, ' ');
    return;
  }

  uint32_t x = g_con.col * CELL_W;
  uint32_t y = g_con.origin_y + g_con.row * CELL_H;
  if (y + CELL_H > g_con.height)
    return;
  fbcon_putch_px(x, y, c);
  g_con.col++;
  if (g_con.col >= g_con.cols) {
    fbcon_putc('\n');
  }
}

void fbcon_print(const char *s) {
  if (!s)
    return;
  while (*s) {
    fbcon_putc(*s++);
  }
}

void fbcon_clear_view(void) {
  if (!g_con.fb || g_con.height <= g_con.origin_y) {
    return;
  }
  fbcon_fill_rect_px(0, g_con.origin_y, g_con.width,
                     g_con.height - g_con.origin_y, g_con.bg);
  g_con.col = 0;
  g_con.row = 0;
}

/* ── numeric formatters ──────────────────────────────────────────────── */

void fbcon_print_hex64(uint64_t v) {
  static const char hex[] = "0123456789ABCDEF";
  char buf[17];
  for (int i = 0; i < 16; i++) {
    buf[i] = hex[(v >> (60 - i * 4)) & 0xF];
  }
  buf[16] = 0;
  fbcon_print(buf);
}

void fbcon_print_hex(uint64_t v) { fbcon_print_hex64(v); }

void fbcon_print_dec_u32(uint32_t v) {
  char rev[16];
  uint32_t n = 0;
  if (v == 0) {
    fbcon_putc('0');
    return;
  }
  while (v > 0 && n < sizeof(rev)) {
    rev[n++] = (char)('0' + (v % 10u));
    v /= 10u;
  }
  while (n > 0) {
    fbcon_putc(rev[--n]);
  }
}

void fbcon_print_hex8(uint8_t v) {
  static const char hex[] = "0123456789ABCDEF";
  fbcon_putc(hex[(v >> 4) & 0xF]);
  fbcon_putc(hex[v & 0xF]);
}

void fbcon_print_hex16(uint16_t v) {
  fbcon_print_hex8((uint8_t)(v >> 8));
  fbcon_print_hex8((uint8_t)(v & 0xFFu));
}

void fbcon_print_ipv4(uint32_t ip) {
  fbcon_print_dec_u32((ip >> 24) & 0xFFu);
  fbcon_putc('.');
  fbcon_print_dec_u32((ip >> 16) & 0xFFu);
  fbcon_putc('.');
  fbcon_print_dec_u32((ip >> 8) & 0xFFu);
  fbcon_putc('.');
  fbcon_print_dec_u32(ip & 0xFFu);
}

void fbcon_print_mac(const uint8_t mac[6]) {
  for (uint32_t i = 0; i < 6; ++i) {
    if (i) {
      fbcon_putc(':');
    }
    fbcon_print_hex8(mac[i]);
  }
}

/* ── diag I/O builder ────────────────────────────────────────────────── */

void kernel_platform_diag_io_init(struct x64_platform_diag_io *out) {
  if (!out) {
    return;
  }
  out->print = fbcon_print;
  out->print_hex64 = fbcon_print_hex64;
  out->print_dec_u32 = fbcon_print_dec_u32;
  out->putc = fbcon_putc;
}

/* ── desktop framebuffer accessors ───────────────────────────────────── */

uint32_t *kernel_desktop_get_fb(void) { return g_con.fb; }
uint32_t kernel_desktop_get_width(void) { return g_con.width; }
uint32_t kernel_desktop_get_height(void) { return g_con.height; }
uint32_t kernel_desktop_get_pitch(void) { return g_con.stride * 4u; }
