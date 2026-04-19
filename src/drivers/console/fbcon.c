/* fbcon.c: Framebuffer console driver.
 * Extracted from kernel_main.c. Renders 8x8 scaled bitmap text on the
 * UEFI GOP framebuffer with serial mirroring and visual-mute support. */
#include "drivers/console/fbcon.h"
#include "drivers/serial/com1.h"
#include "gui/font8x8.h"
#include "kernel/log/klog.h"

#include <stddef.h>
#include <stdint.h>

fbcon_t g_fbcon;
int g_fbcon_serial_mirror = 0;
int g_fbcon_serial_ready = 0;

static int g_visual_muted = 0;
static char g_muted_line[256];
static uint32_t g_muted_len = 0;

void fbcon_init(uint32_t *fb, uint32_t w, uint32_t h, uint32_t pitch) {
  g_fbcon.fb = fb;
  g_fbcon.width = w;
  g_fbcon.height = h;
  g_fbcon.stride = pitch / 4;
  g_fbcon.bg = 0x00102030;
  g_fbcon.fg = 0x00F0F0F0;
  g_fbcon.origin_y = 0;
  g_fbcon.cols = w / FBCON_CELL_W;
  g_fbcon.rows = (h > g_fbcon.origin_y)
                     ? ((h - g_fbcon.origin_y) / FBCON_CELL_H)
                     : 0;
  g_fbcon.col = 0;
  g_fbcon.row = 0;
}

void fbcon_fill_rect_px(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h,
                        uint32_t color) {
  if (!g_fbcon.fb)
    return;
  if (x0 >= g_fbcon.width || y0 >= g_fbcon.height)
    return;
  if (x0 + w > g_fbcon.width)
    w = g_fbcon.width - x0;
  if (y0 + h > g_fbcon.height)
    h = g_fbcon.height - y0;
  for (uint32_t y = 0; y < h; y++) {
    uint32_t *row = g_fbcon.fb + (y0 + y) * g_fbcon.stride;
    for (uint32_t x = 0; x < w; x++) {
      row[x0 + x] = color;
    }
  }
}

static void fbcon_scroll(void) {
  const uint32_t ch = FBCON_CELL_H;
  const uint32_t start = g_fbcon.origin_y;
  const uint32_t end = g_fbcon.height;
  if (end <= start + ch)
    return;

  {
    uint32_t *dst = g_fbcon.fb + start * g_fbcon.stride;
    uint32_t *src = g_fbcon.fb + (start + ch) * g_fbcon.stride;
    uint64_t count_dwords = (uint64_t)(end - start - ch) * g_fbcon.stride;
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
  fbcon_fill_rect_px(0, end - ch, g_fbcon.width, ch, g_fbcon.bg);
}

static void fbcon_putch_px(uint32_t x, uint32_t y, char c) {
  uint8_t uc = (uint8_t)c;
  const uint8_t *glyph = font_glyph(uc);
  for (uint32_t row = 0; row < FBCON_FONT_H; row++) {
    uint8_t bits = glyph[row];
    for (uint32_t dy = 0; dy < FBCON_FONT_SCALE; dy++) {
      uint32_t *dst = g_fbcon.fb + (y + row * FBCON_FONT_SCALE + dy) * g_fbcon.stride;
      for (uint32_t col = 0; col < FBCON_FONT_W; col++) {
        uint32_t color = (bits & (1u << (7u - col))) ? g_fbcon.fg : g_fbcon.bg;
        uint32_t px = x + col * FBCON_FONT_SCALE;
        for (uint32_t dx = 0; dx < FBCON_FONT_SCALE; dx++) {
          dst[px + dx] = color;
        }
      }
    }
  }
}

void fbcon_putch_at(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg) {
  uint32_t saved_fg = g_fbcon.fg;
  uint32_t saved_bg = g_fbcon.bg;
  g_fbcon.fg = fg;
  g_fbcon.bg = bg;
  fbcon_putch_px(x, y, c);
  g_fbcon.fg = saved_fg;
  g_fbcon.bg = saved_bg;
}

static void muted_flush_line(void) {
  if (g_muted_len == 0) return;
  g_muted_line[g_muted_len] = '\0';
  klog(KLOG_INFO, g_muted_line);
  g_muted_len = 0;
}

static void muted_capture_char(char c) {
  if (c == '\r') return;
  if (c == '\n') { muted_flush_line(); return; }
  if (c == '\b') { if (g_muted_len > 0) --g_muted_len; return; }
  if ((uint8_t)c < 0x20u) return;
  if (g_muted_len >= sizeof(g_muted_line) - 1u) muted_flush_line();
  g_muted_line[g_muted_len++] = c;
}

void fbcon_set_visual_muted(int muted) {
  if (!muted) muted_flush_line();
  g_visual_muted = muted ? 1 : 0;
}

void fbcon_putc(char c) {
  if (!g_fbcon.fb || g_fbcon.cols == 0 || g_fbcon.rows == 0)
    return;
  if (g_fbcon_serial_mirror && g_fbcon_serial_ready) {
    if (c == '\n') com1_putc('\r');
    if (c != '\r') com1_putc(c);
  }
  if (g_visual_muted) {
    muted_capture_char(c);
    return;
  }
  if (c == '\r') return;
  if (c == '\n') {
    g_fbcon.col = 0;
    g_fbcon.row++;
    if (g_fbcon.row >= g_fbcon.rows) {
      fbcon_scroll();
      g_fbcon.row = g_fbcon.rows - 1;
    }
    return;
  }
  if (c == '\b') {
    if (g_fbcon.col > 0) {
      g_fbcon.col--;
    } else if (g_fbcon.row > 0) {
      g_fbcon.row--;
      g_fbcon.col = g_fbcon.cols - 1;
    }
    uint32_t x = g_fbcon.col * FBCON_CELL_W;
    uint32_t y = g_fbcon.origin_y + g_fbcon.row * FBCON_CELL_H;
    fbcon_putch_px(x, y, ' ');
    return;
  }
  uint32_t x = g_fbcon.col * FBCON_CELL_W;
  uint32_t y = g_fbcon.origin_y + g_fbcon.row * FBCON_CELL_H;
  if (y + FBCON_CELL_H > g_fbcon.height) return;
  fbcon_putch_px(x, y, c);
  g_fbcon.col++;
  if (g_fbcon.col >= g_fbcon.cols) {
    fbcon_putc('\n');
  }
}

void fbcon_print(const char *s) {
  if (!s) return;
  while (*s) fbcon_putc(*s++);
}

void fbcon_clear_view(void) {
  if (!g_fbcon.fb || g_fbcon.height <= g_fbcon.origin_y) return;
  fbcon_fill_rect_px(0, g_fbcon.origin_y, g_fbcon.width,
                     g_fbcon.height - g_fbcon.origin_y, g_fbcon.bg);
  g_fbcon.col = 0;
  g_fbcon.row = 0;
}

void fbcon_print_hex64(uint64_t v) {
  static const char hex[] = "0123456789ABCDEF";
  char buf[17];
  for (int i = 0; i < 16; i++) buf[i] = hex[(v >> (60 - i * 4)) & 0xF];
  buf[16] = 0;
  fbcon_print(buf);
}

void fbcon_print_hex(uint64_t v) { fbcon_print_hex64(v); }

void fbcon_print_dec_u32(uint32_t v) {
  char rev[16];
  uint32_t n = 0;
  if (v == 0) { fbcon_putc('0'); return; }
  while (v > 0 && n < sizeof(rev)) {
    rev[n++] = (char)('0' + (v % 10u));
    v /= 10u;
  }
  while (n > 0) fbcon_putc(rev[--n]);
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
  fbcon_print_dec_u32((ip >> 24) & 0xFFu); fbcon_putc('.');
  fbcon_print_dec_u32((ip >> 16) & 0xFFu); fbcon_putc('.');
  fbcon_print_dec_u32((ip >> 8) & 0xFFu);  fbcon_putc('.');
  fbcon_print_dec_u32(ip & 0xFFu);
}

void fbcon_print_mac(const uint8_t mac[6]) {
  for (uint32_t i = 0; i < 6; ++i) {
    if (i) fbcon_putc(':');
    fbcon_print_hex8(mac[i]);
  }
}
