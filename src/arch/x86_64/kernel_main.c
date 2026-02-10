// Minimal x86_64 kernel for bringup: shows a framebuffer UI + a tiny command
// prompt. This is intentionally simple (no interrupts yet). It helps validate
// disk boot end-to-end after the UEFI installer provisions GPT/ESP/BOOT.
#pragma GCC optimize("O0")
#include <stddef.h>
#include <stdint.h>

#include "boot/handoff.h"
#include "core/kcon.h"
#include "core/session.h"
#include "core/system_init.h"
#include "drivers/efi/efi_console.h"
#include "drivers/hyperv/hyperv.h"
#include "drivers/nvme.h"
#include "drivers/pcie.h"
#include "drivers/usb/xhci.h"
#include "fs/block.h"
#include "memory/kmem.h"
#include "shell/commands.h"
#include "shell/core.h"

#define DEBUGCON_PORT 0xE9

static inline void dbgcon_putc(uint8_t c) {
  __asm__ volatile("outb %0, %1" : : "a"(c), "Nd"((uint16_t)DEBUGCON_PORT));
}

static inline void outb(uint16_t port, uint8_t val) {
  __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
  uint8_t ret;
  __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

static inline void cpu_relax(void) { __asm__ volatile("pause"); }

/* PIT timer ticks - from stubs.c */
extern uint32_t pit_ticks(void);

static int range_ok(uint64_t addr, uint64_t size) {
  if (addr == 0 || size == 0)
    return 0;
  if (addr + size < addr)
    return 0;
  return 1;
}

static void dbg_hex64(uint64_t v) {
  for (int i = 60; i >= 0; i -= 4) {
    uint8_t nib = (v >> i) & 0xF;
    dbgcon_putc((uint8_t)(nib < 10 ? ('0' + nib) : ('A' + (nib - 10))));
  }
}

/* 8x8 ASCII font (first 128 glyphs) */
static const uint8_t font8x8_basic[128][8] __attribute__((used)) = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x00
    {0x7E, 0x81, 0xA5, 0x81, 0xBD, 0x99, 0x81, 0x7E}, // 0x01
    {0x7E, 0xFF, 0xDB, 0xFF, 0xC3, 0xE7, 0xFF, 0x7E}, // 0x02
    {0x6C, 0xFE, 0xFE, 0xFE, 0x7C, 0x38, 0x10, 0x00}, // 0x03
    {0x10, 0x38, 0x7C, 0xFE, 0x7C, 0x38, 0x10, 0x00}, // 0x04
    {0x38, 0x7C, 0x38, 0xFE, 0xFE, 0xD6, 0x10, 0x38}, // 0x05
    {0x10, 0x38, 0x7C, 0xFE, 0xFE, 0x7C, 0x10, 0x38}, // 0x06
    {0x00, 0x00, 0x18, 0x3C, 0x3C, 0x18, 0x00, 0x00}, // 0x07
    {0xFF, 0xFF, 0xE7, 0xC3, 0xC3, 0xE7, 0xFF, 0xFF}, // 0x08
    {0x00, 0x3C, 0x66, 0x42, 0x42, 0x66, 0x3C, 0x00}, // 0x09
    {0xFF, 0xC3, 0x99, 0xBD, 0xBD, 0x99, 0xC3, 0xFF}, // 0x0A
    {0x0F, 0x07, 0x0F, 0x7D, 0xCC, 0xCC, 0xCC, 0x78}, // 0x0B
    {0x3C, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x7E, 0x18}, // 0x0C
    {0x3F, 0x33, 0x3F, 0x30, 0x30, 0x70, 0xF0, 0xE0}, // 0x0D
    {0x7F, 0x63, 0x7F, 0x63, 0x63, 0x67, 0xE6, 0xC0}, // 0x0E
    {0x99, 0x5A, 0x3C, 0xE7, 0xE7, 0x3C, 0x5A, 0x99}, // 0x0F
    {0x80, 0xE0, 0xF8, 0xFE, 0xF8, 0xE0, 0x80, 0x00}, // 0x10
    {0x02, 0x0E, 0x3E, 0xFE, 0x3E, 0x0E, 0x02, 0x00}, // 0x11
    {0x18, 0x3C, 0x7E, 0x18, 0x18, 0x7E, 0x3C, 0x18}, // 0x12
    {0x66, 0x66, 0x66, 0x66, 0x66, 0x00, 0x66, 0x00}, // 0x13
    {0x7F, 0xDB, 0xDB, 0x7B, 0x1B, 0x1B, 0x1B, 0x00}, // 0x14
    {0x3E, 0x61, 0x3C, 0x66, 0x66, 0x3C, 0x86, 0x7C}, // 0x15
    {0x00, 0x00, 0x00, 0x00, 0x7E, 0x7E, 0x7E, 0x00}, // 0x16
    {0x18, 0x3C, 0x7E, 0x18, 0x7E, 0x3C, 0x18, 0xFF}, // 0x17
    {0x18, 0x3C, 0x7E, 0x18, 0x18, 0x18, 0x18, 0x00}, // 0x18
    {0x18, 0x18, 0x18, 0x18, 0x7E, 0x3C, 0x18, 0x00}, // 0x19
    {0x00, 0x18, 0x0C, 0xFE, 0x0C, 0x18, 0x00, 0x00}, // 0x1A
    {0x00, 0x30, 0x60, 0xFE, 0x60, 0x30, 0x00, 0x00}, // 0x1B
    {0x00, 0x00, 0xC0, 0xC0, 0xC0, 0xFE, 0x00, 0x00}, // 0x1C
    {0x00, 0x24, 0x66, 0xFF, 0x66, 0x24, 0x00, 0x00}, // 0x1D
    {0x00, 0x18, 0x3C, 0x7E, 0xFF, 0xFF, 0x00, 0x00}, // 0x1E
    {0x00, 0xFF, 0xFF, 0x7E, 0x3C, 0x18, 0x00, 0x00}, // 0x1F
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x20 ' '
    {0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00}, // 0x21 '!'
    {0x6C, 0x6C, 0x48, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x22 '"'
    {0x6C, 0x6C, 0xFE, 0x6C, 0xFE, 0x6C, 0x6C, 0x00}, // 0x23 '#'
    {0x18, 0x3E, 0x60, 0x3C, 0x06, 0x7C, 0x18, 0x00}, // 0x24 '$'
    {0x00, 0xC6, 0xCC, 0x18, 0x30, 0x66, 0xC6, 0x00}, // 0x25 '%'
    {0x38, 0x6C, 0x38, 0x76, 0xDC, 0xCC, 0x76, 0x00}, // 0x26 '&'
    {0x30, 0x30, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x27 '''
    {0x0C, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0C, 0x00}, // 0x28 '('
    {0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x18, 0x30, 0x00}, // 0x29 ')'
    {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00}, // 0x2A '*'
    {0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00}, // 0x2B '+'
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30}, // 0x2C ','
    {0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00}, // 0x2D '-'
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00}, // 0x2E '.'
    {0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0x80, 0x00}, // 0x2F '/'
    {0x7C, 0xC6, 0xCE, 0xDE, 0xF6, 0xE6, 0x7C, 0x00}, // 0x30 '0'
    {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00}, // 0x31 '1'
    {0x7C, 0xC6, 0x06, 0x1C, 0x70, 0xC0, 0xFE, 0x00}, // 0x32 '2'
    {0x7C, 0xC6, 0x06, 0x3C, 0x06, 0xC6, 0x7C, 0x00}, // 0x33 '3'
    {0x1C, 0x3C, 0x6C, 0xCC, 0xFE, 0x0C, 0x1E, 0x00}, // 0x34 '4'
    {0xFE, 0xC0, 0xFC, 0x06, 0x06, 0xC6, 0x7C, 0x00}, // 0x35 '5'
    {0x3C, 0x60, 0xC0, 0xFC, 0xC6, 0xC6, 0x7C, 0x00}, // 0x36 '6'
    {0xFE, 0xC6, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00}, // 0x37 '7'
    {0x7C, 0xC6, 0xC6, 0x7C, 0xC6, 0xC6, 0x7C, 0x00}, // 0x38 '8'
    {0x7C, 0xC6, 0xC6, 0x7E, 0x06, 0x0C, 0x78, 0x00}, // 0x39 '9'
    {0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x00}, // 0x3A ':'
    {0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x30}, // 0x3B ';'
    {0x0E, 0x1C, 0x38, 0x70, 0x38, 0x1C, 0x0E, 0x00}, // 0x3C '<'
    {0x00, 0x00, 0x7E, 0x00, 0x00, 0x7E, 0x00, 0x00}, // 0x3D '='
    {0x70, 0x38, 0x1C, 0x0E, 0x1C, 0x38, 0x70, 0x00}, // 0x3E '>'
    {0x7C, 0xC6, 0x0E, 0x1C, 0x18, 0x00, 0x18, 0x00}, // 0x3F '?'
    {0x7C, 0xC6, 0xDE, 0xDE, 0xDE, 0xC0, 0x78, 0x00}, // 0x40 '@'
    {0x38, 0x6C, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0x00}, // 0x41 'A'
    {0xFC, 0x66, 0x66, 0x7C, 0x66, 0x66, 0xFC, 0x00}, // 0x42 'B'
    {0x3C, 0x66, 0xC0, 0xC0, 0xC0, 0x66, 0x3C, 0x00}, // 0x43 'C'
    {0xF8, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0xF8, 0x00}, // 0x44 'D'
    {0xFE, 0x62, 0x68, 0x78, 0x68, 0x62, 0xFE, 0x00}, // 0x45 'E'
    {0xFE, 0x62, 0x68, 0x78, 0x68, 0x60, 0xF0, 0x00}, // 0x46 'F'
    {0x3C, 0x66, 0xC0, 0xC0, 0xCE, 0x66, 0x3E, 0x00}, // 0x47 'G'
    {0xC6, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0xC6, 0x00}, // 0x48 'H'
    {0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00}, // 0x49 'I'
    {0x1E, 0x0C, 0x0C, 0x0C, 0xCC, 0xCC, 0x78, 0x00}, // 0x4A 'J'
    {0xE6, 0x66, 0x6C, 0x78, 0x6C, 0x66, 0xE6, 0x00}, // 0x4B 'K'
    {0xF0, 0x60, 0x60, 0x60, 0x62, 0x66, 0xFE, 0x00}, // 0x4C 'L'
    {0xC6, 0xEE, 0xFE, 0xFE, 0xD6, 0xC6, 0xC6, 0x00}, // 0x4D 'M'
    {0xC6, 0xE6, 0xF6, 0xDE, 0xCE, 0xC6, 0xC6, 0x00}, // 0x4E 'N'
    {0x7C, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00}, // 0x4F 'O'
    {0xFC, 0x66, 0x66, 0x7C, 0x60, 0x60, 0xF0, 0x00}, // 0x50 'P'
    {0x7C, 0xC6, 0xC6, 0xC6, 0xD6, 0xCC, 0x7A, 0x00}, // 0x51 'Q'
    {0xFC, 0x66, 0x66, 0x7C, 0x6C, 0x66, 0xE6, 0x00}, // 0x52 'R'
    {0x7C, 0xC6, 0x60, 0x38, 0x0C, 0xC6, 0x7C, 0x00}, // 0x53 'S'
    {0x7E, 0x7E, 0x5A, 0x18, 0x18, 0x18, 0x3C, 0x00}, // 0x54 'T'
    {0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00}, // 0x55 'U'
    {0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x00}, // 0x56 'V'
    {0xC6, 0xC6, 0xC6, 0xD6, 0xFE, 0xEE, 0xC6, 0x00}, // 0x57 'W'
    {0xC6, 0xC6, 0x6C, 0x38, 0x38, 0x6C, 0xC6, 0x00}, // 0x58 'X'
    {0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x3C, 0x00}, // 0x59 'Y'
    {0xFE, 0xC6, 0x8C, 0x18, 0x32, 0x66, 0xFE, 0x00}, // 0x5A 'Z'
    {0x3C, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3C, 0x00}, // 0x5B '['
    {0xC0, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x02, 0x00}, // 0x5C '\\'
    {0x3C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3C, 0x00}, // 0x5D ']'
    {0x10, 0x38, 0x6C, 0xC6, 0x00, 0x00, 0x00, 0x00}, // 0x5E '^'
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF}, // 0x5F '_'
    {0x30, 0x18, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x60 '`'
    {0x00, 0x00, 0x7C, 0x06, 0x7E, 0xC6, 0x7E, 0x00}, // 0x61 'a'
    {0xE0, 0x60, 0x7C, 0x66, 0x66, 0x66, 0xDC, 0x00}, // 0x62 'b'
    {0x00, 0x00, 0x7C, 0xC6, 0xC0, 0xC6, 0x7C, 0x00}, // 0x63 'c'
    {0x1C, 0x0C, 0x7C, 0xCC, 0xCC, 0xCC, 0x76, 0x00}, // 0x64 'd'
    {0x00, 0x00, 0x7C, 0xC6, 0xFE, 0xC0, 0x7C, 0x00}, // 0x65 'e'
    {0x3C, 0x66, 0x60, 0xF8, 0x60, 0x60, 0xF0, 0x00}, // 0x66 'f'
    {0x00, 0x00, 0x76, 0xCC, 0xCC, 0x7C, 0x0C, 0xF8}, // 0x67 'g'
    {0xE0, 0x60, 0x6C, 0x76, 0x66, 0x66, 0xE6, 0x00}, // 0x68 'h'
    {0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3C, 0x00}, // 0x69 'i'
    {0x06, 0x00, 0x06, 0x06, 0x06, 0xC6, 0xC6, 0x7C}, // 0x6A 'j'
    {0xE0, 0x60, 0x66, 0x6C, 0x78, 0x6C, 0xE6, 0x00}, // 0x6B 'k'
    {0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00}, // 0x6C 'l'
    {0x00, 0x00, 0xCC, 0xFE, 0xFE, 0xD6, 0xC6, 0x00}, // 0x6D 'm'
    {0x00, 0x00, 0xDC, 0x66, 0x66, 0x66, 0x66, 0x00}, // 0x6E 'n'
    {0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0x7C, 0x00}, // 0x6F 'o'
    {0x00, 0x00, 0xDC, 0x66, 0x66, 0x7C, 0x60, 0xF0}, // 0x70 'p'
    {0x00, 0x00, 0x76, 0xCC, 0xCC, 0x7C, 0x0C, 0x1E}, // 0x71 'q'
    {0x00, 0x00, 0xDC, 0x76, 0x66, 0x60, 0xF0, 0x00}, // 0x72 'r'
    {0x00, 0x00, 0x7E, 0xC0, 0x7C, 0x06, 0xFC, 0x00}, // 0x73 's'
    {0x30, 0x30, 0xFC, 0x30, 0x30, 0x36, 0x1C, 0x00}, // 0x74 't'
    {0x00, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0x76, 0x00}, // 0x75 'u'
    {0x00, 0x00, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x00}, // 0x76 'v'
    {0x00, 0x00, 0xC6, 0xD6, 0xFE, 0xFE, 0x6C, 0x00}, // 0x77 'w'
    {0x00, 0x00, 0xC6, 0x6C, 0x38, 0x6C, 0xC6, 0x00}, // 0x78 'x'
    {0x00, 0x00, 0xC6, 0xC6, 0xC6, 0x7E, 0x06, 0xFC}, // 0x79 'y'
    {0x00, 0x00, 0xFE, 0x8C, 0x18, 0x32, 0xFE, 0x00}, // 0x7A 'z'
    {0x0E, 0x18, 0x18, 0x70, 0x18, 0x18, 0x0E, 0x00}, // 0x7B '{'
    {0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00}, // 0x7C '|'
    {0x70, 0x18, 0x18, 0x0E, 0x18, 0x18, 0x70, 0x00}, // 0x7D '}'
    {0x76, 0xDC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x7E '~'
    {0x00, 0x10, 0x38, 0x6C, 0xC6, 0xC6, 0xFE, 0x00}, // 0x7F
};

typedef struct {
  uint32_t *fb;
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  uint32_t origin_y;
  uint32_t cols;
  uint32_t rows;
  uint32_t col;
  uint32_t row;
  uint32_t fg;
  uint32_t bg;
} fbcon_t;

#define FONT_W 8u
#define FONT_H 8u
#define FONT_SCALE 2u
#define CELL_W (FONT_W * FONT_SCALE)
#define CELL_H (FONT_H * FONT_SCALE)

static fbcon_t g_con;
static const struct boot_handoff *g_h = NULL;

static inline const uint8_t *font_glyph(uint8_t uc) {
  const uint8_t (*font)[8];
  __asm__ __volatile__("lea font8x8_basic(%%rip), %0" : "=r"(font));
  return font[(uc < 128) ? uc : '?'];
}

static void fbcon_fill_rect_px(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h,
                               uint32_t color) {
  if (!g_h || !g_con.fb)
    return;
  if (x0 >= g_con.width || y0 >= g_con.height)
    return;
  if (x0 + w > g_con.width)
    w = g_con.width - x0;
  if (y0 + h > g_con.height)
    h = g_con.height - y0;
  for (uint32_t y = 0; y < h; y++) {
    uint32_t *row = g_con.fb + (y0 + y) * g_con.stride;
    for (uint32_t x = 0; x < w; x++) {
      row[x0 + x] = color;
    }
  }
}

static void fbcon_scroll(void) {
  const uint32_t ch = CELL_H;
  const uint32_t start = g_con.origin_y;
  const uint32_t end = g_con.height;
  if (end <= start + ch)
    return;

  for (uint32_t y = start; y + ch < end; y++) {
    uint32_t *dst = g_con.fb + y * g_con.stride;
    uint32_t *src = g_con.fb + (y + ch) * g_con.stride;
    for (uint32_t x = 0; x < g_con.width; x++) {
      dst[x] = src[x];
    }
  }
  fbcon_fill_rect_px(0, end - ch, g_con.width, ch, g_con.bg);
}

static void fbcon_putch_px(uint32_t x, uint32_t y, char c) {
  uint8_t uc = (uint8_t)c;
  const uint8_t *glyph = font_glyph(uc);
  for (uint32_t row = 0; row < FONT_H; row++) {
    uint8_t bits = glyph[row];
    for (uint32_t dy = 0; dy < FONT_SCALE; dy++) {
      uint32_t *dst = g_con.fb + (y + row * FONT_SCALE + dy) * g_con.stride;
      for (uint32_t col = 0; col < FONT_W; col++) {
        uint32_t color = (bits & (1u << (7u - col))) ? g_con.fg : g_con.bg;
        uint32_t px = x + col * FONT_SCALE;
        for (uint32_t dx = 0; dx < FONT_SCALE; dx++) {
          dst[px + dx] = color;
        }
      }
    }
  }
}

void fbcon_putc(char c) {
  if (!g_con.fb || g_con.cols == 0 || g_con.rows == 0)
    return;
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

static int streq(const char *a, const char *b) {
  if (!a || !b)
    return 0;
  while (*a && *b) {
    if (*a++ != *b++)
      return 0;
  }
  return *a == *b;
}

struct acpi_rsdp {
  char signature[8]; /* "RSD PTR " */
  uint8_t checksum;
  char oemid[6];
  uint8_t revision;
  uint32_t rsdt;
  uint32_t length;
  uint64_t xsdt;
  uint8_t ext_checksum;
  uint8_t reserved[3];
} __attribute__((packed));

static uint8_t sum8(const uint8_t *p, uint32_t len) {
  uint8_t s = 0;
  for (uint32_t i = 0; i < len; i++)
    s = (uint8_t)(s + p[i]);
  return s;
}

static int rsdp_is_valid(uint64_t rsdp_addr) {
  if (!range_ok(rsdp_addr, sizeof(struct acpi_rsdp)))
    return 0;
  const struct acpi_rsdp *r = (const struct acpi_rsdp *)(uintptr_t)rsdp_addr;
  const char sig[8] = {'R', 'S', 'D', ' ', 'P', 'T', 'R', ' '};
  for (int i = 0; i < 8; i++) {
    if (r->signature[i] != sig[i])
      return 0;
  }
  if (!range_ok(rsdp_addr, 20))
    return 0;
  if (sum8((const uint8_t *)r, 20) != 0)
    return 0;
  if (r->revision >= 2) {
    uint32_t len = r->length;
    if (len < 36 || len > 4096) {
      return 1;
    }
    if (!range_ok(rsdp_addr, len))
      return 0;
    return sum8((const uint8_t *)r, len) == 0;
  }
  return 1;
}

static void ui_draw_bars(void) {
  const uint32_t bar_h = (g_con.height > 40) ? 40 : g_con.height;
  fbcon_fill_rect_px(0, 0, g_con.width, bar_h, 0x0040FF40);
  int rsdp_ok = g_h && g_h->rsdp && rsdp_is_valid(g_h->rsdp);
  fbcon_fill_rect_px(0, 48, g_con.width, 24, rsdp_ok ? 0x004040FF : 0x00FF4040);
}

static void ui_banner(void) {
  fbcon_print("NOIR64  ");
  fbcon_print("RSDP=");
  fbcon_print(rsdp_is_valid(g_h ? g_h->rsdp : 0) ? "OK " : "-- ");
  fbcon_print("rsdp=");
  fbcon_print_hex64(g_h ? g_h->rsdp : 0);
  fbcon_print(" fb=");
  fbcon_print_hex64(g_h ? g_h->fb.base : 0);
  fbcon_putc('\n');
}

static void cmd_help(void) {
  fbcon_print("Comandos: help, info, clear, reboot, halt\n");
}

static void cmd_info(void) {
  fbcon_print("handoff.magic=");
  fbcon_print_hex64(g_h ? (uint64_t)g_h->magic : 0);
  fbcon_print(" ver=");
  fbcon_print_hex64(g_h ? (uint64_t)g_h->version : 0);
  fbcon_putc('\n');

  fbcon_print("rsdp=");
  fbcon_print_hex64(g_h ? g_h->rsdp : 0);
  fbcon_print(" valid=");
  fbcon_print_hex64(rsdp_is_valid(g_h ? g_h->rsdp : 0) ? 1 : 0);
  fbcon_putc('\n');

  fbcon_print("fb.base=");
  fbcon_print_hex64(g_h ? g_h->fb.base : 0);
  fbcon_print(" w=");
  fbcon_print_hex64(g_h ? (uint64_t)g_h->fb.width : 0);
  fbcon_print(" h=");
  fbcon_print_hex64(g_h ? (uint64_t)g_h->fb.height : 0);
  fbcon_print(" pitch=");
  fbcon_print_hex64(g_h ? (uint64_t)g_h->fb.pitch : 0);
  fbcon_putc('\n');

  fbcon_print("memmap.ptr=");
  fbcon_print_hex64(g_h ? g_h->memmap : 0);
  fbcon_print(" desc=");
  fbcon_print_hex64(g_h ? (uint64_t)g_h->memmap_desc_size : 0);
  fbcon_print(" entries=");
  fbcon_print_hex64(g_h ? (uint64_t)g_h->memmap_entries : 0);
  fbcon_putc('\n');
}

/* ============================================================================
 * Shell Command Dispatch
 * Routes commands through the shell module system for list, go, mk-file, etc.
 * ============================================================================
 */

static struct shell_context g_shell_ctx;
static struct session_context g_session_ctx;
static int g_shell_initialized = 0;

/* Initialize shell context for command dispatch */
static void init_shell_context(const char *username) {
  fbcon_print("[shell] init_shell_context called\n");

  if (g_shell_initialized)
    return;

  /* Initialize minimal session */
  session_reset(&g_session_ctx);
  session_set_cwd(&g_session_ctx, "/");

  /* Initialize shell context */
  shell_context_init(&g_shell_ctx, &g_session_ctx, NULL);

  g_shell_initialized = 1;
  fbcon_print("[shell] initialized OK\n");
  (void)username;
}

/* Try to execute command through shell module system.
 * Returns 1 if command was handled, 0 if not found (fallback to inline). */
static int try_shell_command(char *line) {
  if (!g_shell_initialized)
    return 0;
  if (!line || !line[0])
    return 0;

  /* Parse command line into argc/argv */
  char *argv[16];
  int argc = shell_parse_line(line, argv, 16);
  if (argc == 0)
    return 0;

  /* Look up command in shell module registry */
  size_t set_count = 0;
  const struct shell_command_set *sets = shell_command_sets(&set_count);

  /* Debug: show what we're looking for */
  fbcon_print("[shell] cmd=");
  fbcon_print(argv[0]);
  fbcon_print(" sets=");
  fbcon_print_hex(set_count);
  fbcon_print("\n");

  for (size_t i = 0; i < set_count; i++) {
    /* Debug: show first command of this set */
    if (sets[i].count > 0 && sets[i].commands[0].name) {
      fbcon_print("[shell] set");
      fbcon_print_hex(i);
      fbcon_print(" first=");
      fbcon_print(sets[i].commands[0].name);
      fbcon_print("\n");
    }

    for (size_t j = 0; j < sets[i].count; j++) {
      if (shell_string_equal(argv[0], sets[i].commands[j].name)) {
        /* Found command - execute it */
        fbcon_print("[shell] MATCH! executing...\n");
        int result = sets[i].commands[j].handler(&g_shell_ctx, argc, argv);
        if (result != 0) {
          fbcon_print("[!] comando retornou erro\n");
        }
        return 1; /* Command was handled */
      }
    }
  }

  return 0; /* Command not found in shell modules */
}

/* COM1 Serial UART for input fallback (Hyper-V Gen 2 has no PS/2) */
#define COM1_PORT 0x3F8

static void com1_init(void) {
  outb(COM1_PORT + 1, 0x00); /* Disable interrupts */
  outb(COM1_PORT + 3, 0x80); /* Enable DLAB (set baud rate divisor) */
  outb(COM1_PORT + 0, 0x03); /* Divisor lo: 38400 baud */
  outb(COM1_PORT + 1, 0x00); /* Divisor hi */
  outb(COM1_PORT + 3, 0x03); /* 8 bits, no parity, one stop bit */
  outb(COM1_PORT + 2, 0xC7); /* Enable FIFO, clear, 14-byte threshold */
  outb(COM1_PORT + 4, 0x0B); /* IRQs enabled, RTS/DSR set */
}

static int com1_poll_char(char *ch) {
  if (inb(COM1_PORT + 5) & 0x01) {
    *ch = (char)inb(COM1_PORT);
    return 1;
  }
  return 0;
}

static void com1_putc(char c) {
  while ((inb(COM1_PORT + 5) & 0x20) == 0)
    ;
  outb(COM1_PORT, (uint8_t)c);
}

static void com1_puts(const char *s) {
  while (*s) {
    if (*s == '\n')
      com1_putc('\r');
    com1_putc(*s++);
  }
}

/* COM1 loopback self-test: enable loopback mode, send a byte, verify reception.
 * Returns 1 if COM1 is working (connected), 0 otherwise (disconnected/garbage).
 * This avoids polling garbage data on Hyper-V where COM1 may not be configured.
 */
static int g_has_com1 = -1; /* -1 = not checked, 0 = no, 1 = yes */

static int com1_detect(void) {
  if (g_has_com1 >= 0)
    return g_has_com1;

  /* Enable loopback mode (bit 4 of MCR) */
  outb(COM1_PORT + 4, 0x1E);

  /* Send test byte 0xAE */
  outb(COM1_PORT, 0xAE);

  /* Wait briefly and check if we receive it back */
  for (int i = 0; i < 10000; i++) {
    if (inb(COM1_PORT + 5) & 0x01) {
      uint8_t received = inb(COM1_PORT);
      /* Disable loopback, restore normal mode */
      outb(COM1_PORT + 4, 0x0B);
      if (received == 0xAE) {
        g_has_com1 = 1;
        return 1;
      }
      break;
    }
    cpu_relax();
  }

  /* Disable loopback, no working COM1 */
  outb(COM1_PORT + 4, 0x0B);
  g_has_com1 = 0;
  return 0;
}

static int ps2_poll_scancode(uint8_t *out_sc) {
  if (!out_sc)
    return 0;
  if (inb(0x64) & 0x01) {
    *out_sc = inb(0x60);
    return 1;
  }
  return 0;
}

static void ps2_flush(void) {
  uint8_t d;
  for (uint32_t i = 0; i < 1024; i++) {
    if (!ps2_poll_scancode(&d))
      break;
  }
}

static void ps2_reboot(void) {
  // Try the classic keyboard controller reset.
  outb(0x64, 0xFE);
  for (;;)
    cpu_relax();
}

/* PS/2 controller detection:
 * Send 0xAA (self-test) command and check for 0x55 response.
 * Returns 1 if PS/2 controller is present, 0 otherwise.
 * Hyper-V Gen2 has no PS/2 controller - this detects that. */
static int g_has_ps2 = -1; /* -1 = not checked, 0 = no, 1 = yes */

static int ps2_controller_detect(void) {
  if (g_has_ps2 >= 0)
    return g_has_ps2;

  /* Wait for input buffer to be ready */
  for (int i = 0; i < 10000; i++) {
    if (!(inb(0x64) & 0x02))
      break;
    cpu_relax();
  }

  /* Send self-test command 0xAA */
  outb(0x64, 0xAA);

  /* Wait for output buffer to have data */
  for (int i = 0; i < 100000; i++) {
    if (inb(0x64) & 0x01) {
      uint8_t response = inb(0x60);
      if (response == 0x55) {
        g_has_ps2 = 1;
        return 1;
      }
      /* Got some response but not 0x55 - no PS/2 */
      break;
    }
    cpu_relax();
  }

  g_has_ps2 = 0;
  return 0;
}

/* Global input state for readline */
static int g_has_ps2_input = 0;
static int g_has_com1_input = 0;
static int g_has_hyperv_input = 0;
static int g_shift = 0;

/* VMBus keyboard polling (from vmbus_keyboard.c) */
extern struct vmbus_keyboard *vmbus_get_keyboard(void);
extern int vmbus_keyboard_poll(struct vmbus_keyboard *kbd, uint8_t *scancode,
                               int *is_break);

/* Forward declaration */
static char scancode_to_ascii(uint8_t sc, int shift);

/* Kernel readline: read a line with optional password masking */
static size_t kernel_readline(char *buf, size_t maxlen, int mask) {
  if (!buf || maxlen < 2)
    return 0;

  size_t len = 0;
  buf[0] = 0;

  for (;;) {
    uint8_t sc = 0;
    char c_com = 0;

    /* COM1 Input */
    if (g_has_com1_input && com1_poll_char(&c_com)) {
      if (c_com == '\r')
        c_com = '\n';
      if (c_com == 127 || c_com == '\b') {
        if (len > 0) {
          len--;
          buf[len] = 0;
          fbcon_putc('\b');
        }
      } else if (c_com == '\n') {
        fbcon_putc('\n');
        buf[len] = 0;
        return len;
      } else if (len + 1 < maxlen) {
        buf[len++] = c_com;
        buf[len] = 0;
        fbcon_putc(mask ? '*' : c_com);
      }
      continue;
    }

    /* PS/2 Input */
    if (g_has_ps2_input && ps2_poll_scancode(&sc)) {
      /* Shift handling */
      if (sc == 0x2A || sc == 0x36) {
        g_shift = 1;
        continue;
      }
      if (sc == 0xAA || sc == 0xB6) {
        g_shift = 0;
        continue;
      }
      if (sc & 0x80)
        continue; /* ignore key-up */

      /* Enter */
      if (sc == 0x1C) {
        fbcon_putc('\n');
        buf[len] = 0;
        return len;
      }
      /* Backspace */
      if (sc == 0x0E) {
        if (len > 0) {
          len--;
          buf[len] = 0;
          fbcon_putc('\b');
        }
        continue;
      }

      char ch = scancode_to_ascii(sc, g_shift);
      if (ch && len + 1 < maxlen) {
        buf[len++] = ch;
        buf[len] = 0;
        fbcon_putc(mask ? '*' : ch);
      }
      continue;
    }

    /* VMBus/Hyper-V Synthetic Keyboard Input */
    if (g_has_hyperv_input) {
      struct vmbus_keyboard *hvkbd = vmbus_get_keyboard();
      if (hvkbd) {
        uint8_t hv_sc = 0;
        int hv_break = 0;
        if (vmbus_keyboard_poll(hvkbd, &hv_sc, &hv_break) && !hv_break) {
          /* Process Hyper-V scancode (similar to PS/2) */
          if (hv_sc == 0x2A || hv_sc == 0x36) {
            g_shift = 1;
            continue;
          }
          if (hv_sc == 0xAA || hv_sc == 0xB6) {
            g_shift = 0;
            continue;
          }
          if (hv_sc == 0x1C) { /* Enter */
            fbcon_putc('\n');
            buf[len] = 0;
            return len;
          }
          if (hv_sc == 0x0E) { /* Backspace */
            if (len > 0) {
              len--;
              buf[len] = 0;
              fbcon_putc('\b');
            }
            continue;
          }
          char ch = scancode_to_ascii(hv_sc, g_shift);
          if (ch && len + 1 < maxlen) {
            buf[len++] = ch;
            buf[len] = 0;
            fbcon_putc(mask ? '*' : ch);
          }
          continue;
        }
      }
    }

    /* Yield CPU to avoid Hyper-V watchdog during input wait */
    cpu_relax();
    __asm__ volatile("hlt");
  }
}

static char scancode_to_ascii(uint8_t sc, int shift) {
  static const char base[128] = {
      [0x01] = 27,
      [0x02] = '1',
      [0x03] = '2',
      [0x04] = '3',
      [0x05] = '4',
      [0x06] = '5',
      [0x07] = '6',
      [0x08] = '7',
      [0x09] = '8',
      [0x0A] = '9',
      [0x0B] = '0',
      [0x0C] = '-',
      [0x0D] = '=',
      [0x0E] = '\b',
      [0x0F] = '\t',
      [0x10] = 'q',
      [0x11] = 'w',
      [0x12] = 'e',
      [0x13] = 'r',
      [0x14] = 't',
      [0x15] = 'y',
      [0x16] = 'u',
      [0x17] = 'i',
      [0x18] = 'o',
      [0x19] = 'p',
      [0x1A] = '[',
      [0x1B] = ']',
      [0x1C] = '\n',
      [0x1E] = 'a',
      [0x1F] = 's',
      [0x20] = 'd',
      [0x21] = 'f',
      [0x22] = 'g',
      [0x23] = 'h',
      [0x24] = 'j',
      [0x25] = 'k',
      [0x26] = 'l',
      [0x27] = ';',
      [0x28] = '\'',
      [0x29] = '`',
      [0x2B] = '\\',
      [0x2C] = 'z',
      [0x2D] = 'x',
      [0x2E] = 'c',
      [0x2F] = 'v',
      [0x30] = 'b',
      [0x31] = 'n',
      [0x32] = 'm',
      [0x33] = ',',
      [0x34] = '.',
      [0x35] = '/',
      [0x37] = '*',
      [0x39] = ' ',
      // keypad
      [0x47] = '7',
      [0x48] = '8',
      [0x49] = '9',
      [0x4A] = '-',
      [0x4B] = '4',
      [0x4C] = '5',
      [0x4D] = '6',
      [0x4E] = '+',
      [0x4F] = '1',
      [0x50] = '2',
      [0x51] = '3',
      [0x52] = '0',
      [0x53] = '.',
  };
  static const char sh[128] = {
      [0x01] = 27,
      [0x02] = '!',
      [0x03] = '@',
      [0x04] = '#',
      [0x05] = '$',
      [0x06] = '%',
      [0x07] = '^',
      [0x08] = '&',
      [0x09] = '*',
      [0x0A] = '(',
      [0x0B] = ')',
      [0x0C] = '_',
      [0x0D] = '+',
      [0x0E] = '\b',
      [0x0F] = '\t',
      [0x10] = 'Q',
      [0x11] = 'W',
      [0x12] = 'E',
      [0x13] = 'R',
      [0x14] = 'T',
      [0x15] = 'Y',
      [0x16] = 'U',
      [0x17] = 'I',
      [0x18] = 'O',
      [0x19] = 'P',
      [0x1A] = '{',
      [0x1B] = '}',
      [0x1C] = '\n',
      [0x1E] = 'A',
      [0x1F] = 'S',
      [0x20] = 'D',
      [0x21] = 'F',
      [0x22] = 'G',
      [0x23] = 'H',
      [0x24] = 'J',
      [0x25] = 'K',
      [0x26] = 'L',
      [0x27] = ':',
      [0x28] = '"',
      [0x29] = '~',
      [0x2B] = '|',
      [0x2C] = 'Z',
      [0x2D] = 'X',
      [0x2E] = 'C',
      [0x2F] = 'V',
      [0x30] = 'B',
      [0x31] = 'N',
      [0x32] = 'M',
      [0x33] = '<',
      [0x34] = '>',
      [0x35] = '?',
      [0x37] = '*',
      [0x39] = ' ',
      // keypad (same)
      [0x47] = '7',
      [0x48] = '8',
      [0x49] = '9',
      [0x4A] = '-',
      [0x4B] = '4',
      [0x4C] = '5',
      [0x4D] = '6',
      [0x4E] = '+',
      [0x4F] = '1',
      [0x50] = '2',
      [0x51] = '3',
      [0x52] = '0',
      [0x53] = '.',
  };
  if (sc >= 128)
    return 0;
  return shift ? sh[sc] : base[sc];
}

__attribute__((noreturn)) void kernel_main64(const struct boot_handoff *h) {
  dbgcon_putc('H');
  dbgcon_putc('O');
  dbgcon_putc('K');

  g_h = h;
  if (!h || h->magic != BOOT_HANDOFF_MAGIC) {
    dbgcon_putc('!');
    for (;;)
      cpu_relax();
  }

  // Setup framebuffer console (below the status bars)
  if (!range_ok(h->fb.base, h->fb.size) || h->fb.bpp != 32 ||
      h->fb.pitch == 0) {
    dbgcon_putc('F');
    dbg_hex64(h->fb.base);
    for (;;)
      cpu_relax();
  }

  g_con.fb = (uint32_t *)(uintptr_t)h->fb.base;
  g_con.width = h->fb.width;
  g_con.height = h->fb.height;
  g_con.stride = h->fb.pitch / 4;
  g_con.bg = 0x00102030;
  g_con.fg = 0x00F0F0F0;
  g_con.origin_y = 80;
  g_con.cols = (g_con.width / CELL_W);
  g_con.rows = (g_con.height > g_con.origin_y)
                   ? ((g_con.height - g_con.origin_y) / CELL_H)
                   : 0;
  g_con.col = 0;
  g_con.row = 0;

  fbcon_fill_rect_px(0, 0, g_con.width, g_con.height, g_con.bg);
  ui_draw_bars();
  ui_banner();
  fbcon_print("NoirOS x86_64 kernel (early)\n");
  fbcon_print("Boot OK. Inicializando drivers...\n\n");

  /* Initialize kernel console and memory allocator */
  kcon_init();
  kinit();

  /* Initialize NVMe driver */
  fbcon_print("[pci] Scanning PCIe bus...\n");
  if (nvme_init() == 0) {
    struct block_device *nvme_dev = nvme_get_block_device(0);
    if (nvme_dev) {
      fbcon_print("[nvme] Detectado: ");
      fbcon_print(nvme_dev->name);
      fbcon_print(" (");
      /* Print size in MB */
      uint32_t size_mb = (nvme_dev->block_count / 2048);
      char size_buf[16];
      int pos = 0;
      if (size_mb == 0) {
        size_buf[pos++] = '0';
      } else {
        char rev[16];
        int ri = 0;
        uint32_t tmp = size_mb;
        while (tmp > 0) {
          rev[ri++] = '0' + (tmp % 10);
          tmp /= 10;
        }
        while (ri > 0) {
          size_buf[pos++] = rev[--ri];
        }
      }
      size_buf[pos] = '\0';
      fbcon_print(size_buf);
      fbcon_print(" MB)\n");
    }
  } else {
    fbcon_print("[nvme] Nenhum controlador NVMe encontrado.\n");
    fbcon_print("       (Normal em sistemas legados ou com AHCI)\n");
  }

  fbcon_print("\nDigite 'help' para comandos.\n");
  fbcon_print(
      "Obs: em Hyper-V Gen2 o teclado pode ser sintetico (sem PS/2),\n");
  fbcon_print("entao a linha de comando pode nao receber input ainda.\n\n");
  cmd_info();
  fbcon_putc('\n');

  /* Initialize COM1 for serial console fallback */
  com1_init();
  fbcon_print("[dbg] COM1 inicializado (38400 8N1)\n");
  com1_puts("[COM1] NoirOS 64-bit serial console ready\r\n");

  /* Debug: show EFI pointers */
  if (g_h && g_h->efi_system_table) {
    EFI_SYSTEM_TABLE_K *st =
        (EFI_SYSTEM_TABLE_K *)(uintptr_t)g_h->efi_system_table;
    fbcon_print("[dbg] EFI SystemTable=");
    fbcon_print_hex64(g_h->efi_system_table);
    fbcon_print(" ConIn=");
    fbcon_print_hex64((uint64_t)(uintptr_t)st->ConIn);
    if (st->ConIn) {
      fbcon_print(" ReadKey=");
      fbcon_print_hex64((uint64_t)(uintptr_t)st->ConIn->ReadKeyStroke);
    }
    fbcon_putc('\n');
  } else {
    fbcon_print("[dbg] EFI SystemTable not available\n");
  }

  ps2_flush();

  /* Detect PS/2 controller presence - Hyper-V Gen2 has none */
  int has_ps2 = ps2_controller_detect();
  if (has_ps2) {
    fbcon_print("[info] PS/2 controller detectado.\n");
  } else {
    fbcon_print("[info] PS/2 nao detectado.\n");
  }

  /* Detect COM1 serial port - may return garbage on some VMs */
  int has_com1 = com1_detect();
  if (has_com1) {
    fbcon_print("[info] COM1 serial detectado.\n");
  } else {
    fbcon_print("[info] COM1 nao detectado.\n");
  }

  /* ===============================================================
   * HYPER-V DETECTION - Enable COM1 fallback if Hyper-V detected
   * Hyper-V Gen2 may have emulated COM1 that fails loopback test
   * =============================================================== */
  int is_hyperv = hyperv_detect();
  if (is_hyperv && !has_com1) {
    fbcon_print("[hyperv] Hyper-V detectado. Forçando COM1 habilitado.\n");
    has_com1 = 1; /* Trust Hyper-V COM1 emulation even without loopback */
  }

  if (!has_ps2 && !has_com1) {
    fbcon_print("[AVISO] Nenhum dispositivo de entrada detectado!\n");
    fbcon_print("        Hyper-V Gen2 precisa de driver USB/VMBus.\n");
  }

  /* ===============================================================
   * HYPER-V SYNTHETIC KEYBOARD DETECTION
   * Hyper-V Gen2 uses VMBus synthetic keyboard, not USB
   * Try VMBus keyboard but don't block boot if it fails
   * =============================================================== */
  int has_hyperv = 0;
  extern int hyperv_keyboard_init(void);

  if (is_hyperv) {
    /* DISABLED: VMBus triggers Hyper-V watchdog even with short timeouts.
     * Multiple version negotiation attempts accumulate and cause reboot.
     * Use COM1 serial input until VMBus is properly fixed.
     */
    fbcon_print("[hyperv] VMBus desabilitado (watchdog issue).\n");
    fbcon_print("[hyperv] Use COM1 serial para input.\n");
  }

  fbcon_print("[dbg] CP1: post-hyperv\n");

  /* ===============================================================
   * XHCI USB Controller Detection
   * Fallback for real hardware without PS/2 (not Hyper-V)
   * =============================================================== */
  int has_usb = 0;
  struct xhci_controller xhci = {0};

  if (!has_ps2 && !has_com1 && !has_hyperv) {
    fbcon_print("[usb] Buscando controlador XHCI...\n");
    if (xhci_find(&xhci) == 0) {
      char msg[64];
      msg[0] = '[';
      msg[1] = 'u';
      msg[2] = 's';
      msg[3] = 'b';
      msg[4] = ']';
      msg[5] = ' ';
      msg[6] = 'X';
      msg[7] = 'H';
      msg[8] = 'C';
      msg[9] = 'I';
      msg[10] = ' ';
      msg[11] = 'e';
      msg[12] = 'n';
      msg[13] = 'c';
      msg[14] = 'o';
      msg[15] = 'n';
      msg[16] = 't';
      msg[17] = 'r';
      msg[18] = 'a';
      msg[19] = 'd';
      msg[20] = 'o';
      msg[21] = '!';
      msg[22] = '\n';
      msg[23] = 0;
      fbcon_print(msg);

      if (xhci_init(&xhci) == 0) {
        fbcon_print("[usb] XHCI inicializado.\n");
        if (xhci_start(&xhci) == 0) {
          fbcon_print("[usb] XHCI rodando.\n");
          has_usb = 1;
          /* TODO: Enumerate USB devices and find keyboard */
          fbcon_print("[usb] Prox. passo: enum dispositivos...\n");
        } else {
          fbcon_print("[usb] Falha ao iniciar XHCI.\n");
        }
      } else {
        fbcon_print("[usb] Falha ao inicializar XHCI.\n");
      }
    } else {
      fbcon_print("[usb] XHCI nao encontrado via PCIe.\n");
    }
  }
  (void)has_usb; /* Will be used when USB keyboard polling is added */

  /* ===============================================================
   * KERNEL BOOT FLOW
   *
   * The UEFI loader handles installation when booted from ISO.
   * When we reach here, the system is installed and ready.
   * Show login prompt (or CLI for now until login is implemented).
   * =============================================================== */

  if (!has_ps2 && !has_com1 && !has_usb) {
    /* No input device - warn user but continue (for display purposes) */
    fbcon_print("\n[ERRO] Nenhum teclado disponivel.\n");
    fbcon_print("USB HID keyboard em desenvolvimento.\n");
  }

  char line[128];
  size_t len = 0;
  int shift = 0;
  uint32_t heartbeat = 0;

  /* Initialize global input flags */
  g_has_ps2_input = has_ps2;
  g_has_com1_input = has_com1;

  /* Current session state */
  int logged_in = 0;
  char current_user[64] = {0};
  char user_input[64], pass_input[64];

login_prompt:
  /* ========== LOGIN SCREEN ========== */
  fbcon_print("\n");
  fbcon_print("========================================\n");
  fbcon_print("             NoirOS 64-bit             \n");
  fbcon_print("========================================\n");
  fbcon_print("\n");

  if (!has_ps2 && !has_com1) {
    fbcon_print("[!] Sem dispositivo de entrada.\n\n");
  }

  logged_in = 0;

  /* Login loop - try up to 3 times */
  for (int attempts = 0; attempts < 3 && !logged_in; attempts++) {
    /* Get username */
    fbcon_print("Usuario: ");
    kernel_readline(user_input, sizeof(user_input), 0);

    /* Get password (masked) */
    fbcon_print("Senha: ");
    kernel_readline(pass_input, sizeof(pass_input), 1);

    /* Validate credentials - hardcoded admin/admin for now */
    /* TODO: Read from userdb in NoirFS once mounted */
    if (streq(user_input, "admin") && streq(pass_input, "admin")) {
      logged_in = 1;
      for (size_t i = 0; user_input[i] && i < 63; i++) {
        current_user[i] = user_input[i];
      }
      current_user[63] = 0;
      fbcon_print("\nBem-vindo, ");
      fbcon_print(current_user);
      fbcon_print("!\n\n");
      /* Initialize shell context for command dispatch */
      init_shell_context(current_user);
    } else {
      fbcon_print("\nCredenciais invalidas.\n");
      if (attempts < 2) {
        fbcon_print("Tente novamente.\n\n");
      }
    }
  }

  if (!logged_in) {
    fbcon_print("\nMuitas tentativas falhas. Sistema bloqueado.\n");
    for (;;)
      cpu_relax();
  }

  /* ========== CLI PROMPT LOOP ========== */
  for (;;) {
    /* Show prompt with username */
    fbcon_print(current_user);
    fbcon_print("@noir64> ");

    /* Read command using kernel_readline */
    len = kernel_readline(line, sizeof(line), 0);
    (void)shift;
    (void)heartbeat; /* Suppress unused warnings */

    /* Command dispatch - aligned with noiros-cli-reference.md */
    if (streq(line, "help") || streq(line, "help-any")) {
      fbcon_print("Comandos disponiveis:\n");
      fbcon_print(" - help-any       Lista todos os comandos\n");
      fbcon_print(" - help-docs      Documentacao (em breve)\n");
      fbcon_print(" - mess           Limpa a tela\n");
      fbcon_print(" - mypath         Diretorio atual\n");
      fbcon_print(" - print-host     Hostname\n");
      fbcon_print(" - print-me       Usuario atual\n");
      fbcon_print(" - print-id       UID/GID\n");
      fbcon_print(" - print-version  Versao do NoirOS\n");
      fbcon_print(" - print-time     Horario desde boot\n");
      fbcon_print(" - print-insomnia Uptime\n");
      fbcon_print(" - print-envs     Variaveis de ambiente\n");
      fbcon_print(" - bye            Logout (retorna ao login)\n");
      fbcon_print(" - shutdown-reboot Reinicia o sistema\n");
      fbcon_print(" - shutdown-off   Desliga o sistema\n");
      fbcon_print(" - info           Info tecnica do kernel\n");
    } else if (streq(line, "info")) {
      cmd_info();
    } else if (streq(line, "mess") || streq(line, "clear")) {
      fbcon_fill_rect_px(0, g_con.origin_y, g_con.width,
                         g_con.height - g_con.origin_y, g_con.bg);
      g_con.col = 0;
      g_con.row = 0;
      ui_banner();
    } else if (streq(line, "shutdown-reboot") || streq(line, "reboot")) {
      fbcon_print("Sincronizando buffers...\n");
      fbcon_print("Reiniciando...\n");
      ps2_reboot();
    } else if (streq(line, "shutdown-off") || streq(line, "halt")) {
      fbcon_print("Sincronizando buffers...\n");
      fbcon_print("Desligando...\n");
      for (;;)
        __asm__ volatile("hlt");
    } else if (streq(line, "bye")) {
      fbcon_print("Encerrando sessao...\n");
      fbcon_fill_rect_px(0, g_con.origin_y, g_con.width,
                         g_con.height - g_con.origin_y, g_con.bg);
      g_con.col = 0;
      g_con.row = 0;
      goto login_prompt;
    } else if (streq(line, "print-host")) {
      fbcon_print("noiros64\n");
    } else if (streq(line, "mypath")) {
      fbcon_print("/\n");
    } else if (streq(line, "print-me")) {
      fbcon_print(current_user);
      fbcon_print("\n");
    } else if (streq(line, "print-id")) {
      fbcon_print("uid=0(admin) gid=0(admin)\n");
    } else if (streq(line, "print-version")) {
      fbcon_print("NoirOS 64-bit\n");
      fbcon_print("Version: 0.1.0-alpha\n");
      fbcon_print("Channel: dev\n");
    } else if (streq(line, "print-time")) {
      /* Fake time based on PIT ticks */
      uint32_t ticks = pit_ticks();
      uint32_t secs = ticks / 100; /* Approximate */
      uint32_t mins = secs / 60;
      uint32_t hours = mins / 60;
      char buf[16];
      buf[0] = '0' + (hours / 10) % 10;
      buf[1] = '0' + hours % 10;
      buf[2] = ':';
      buf[3] = '0' + (mins % 60) / 10;
      buf[4] = '0' + (mins % 60) % 10;
      buf[5] = ':';
      buf[6] = '0' + (secs % 60) / 10;
      buf[7] = '0' + (secs % 60) % 10;
      buf[8] = '\n';
      buf[9] = '\0';
      fbcon_print(buf);
    } else if (streq(line, "print-insomnia")) {
      uint32_t ticks = pit_ticks();
      uint32_t secs = ticks / 100;
      uint32_t mins = secs / 60;
      uint32_t hours = mins / 60;
      fbcon_print("Uptime: ");
      char buf[16];
      buf[0] = '0' + (hours / 10) % 10;
      buf[1] = '0' + hours % 10;
      buf[2] = ':';
      buf[3] = '0' + (mins % 60) / 10;
      buf[4] = '0' + (mins % 60) % 10;
      buf[5] = ':';
      buf[6] = '0' + (secs % 60) / 10;
      buf[7] = '0' + (secs % 60) % 10;
      buf[8] = '\n';
      buf[9] = '\0';
      fbcon_print(buf);
    } else if (streq(line, "print-envs")) {
      fbcon_print("USER=admin\n");
      fbcon_print("HOME=/home/admin\n");
      fbcon_print("HOST=noiros64\n");
      fbcon_print("VERSION=0.1.0-alpha\n");
      fbcon_print("CHANNEL=dev\n");
    } else if (line[0] == '\0') {
      /* Empty line - ignore */
    } else if (try_shell_command(line)) {
      /* Command handled by shell module */
    } else {
      fbcon_print("Comando desconhecido: ");
      fbcon_print(line);
      fbcon_print("\nDigite 'help-any' para lista de comandos.\n");
    }

    fbcon_putc('\n');
  }
}

__asm__(".section .note.GNU-stack,\"\",@progbits");
