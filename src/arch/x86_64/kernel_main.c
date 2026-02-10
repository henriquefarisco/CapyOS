// Minimal x86_64 kernel for bringup: shows a framebuffer UI + a tiny command
// prompt. This is intentionally simple (no interrupts yet). It helps validate
// disk boot end-to-end after the UEFI installer provisions GPT/ESP/BOOT.
#pragma GCC optimize("O0")
#include <stddef.h>
#include <stdint.h>

#include "boot/handoff.h"
#include "branding/capyos_icon_mask.h"
#include "core/kcon.h"
#include "core/session.h"
#include "core/system_init.h"
#include "core/user.h"
#include "drivers/efi/efi_console.h"
#include "drivers/hyperv/hyperv.h"
#include "drivers/nvme.h"
#include "drivers/pcie.h"
#include "drivers/usb/xhci.h"
#include "fs/block.h"
#include "fs/buffer.h"
#include "fs/noirfs.h"
#include "fs/ramdisk.h"
#include "fs/vfs.h"
#include "memory/kmem.h"
#include "net/stack.h"
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
static int g_serial_mirror = 0;
static int g_com1_ready = 0;

/* Forward declaration; COM1 implementation is defined later in this file. */
static void com1_putc(char c);

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
  if (g_serial_mirror && g_com1_ready) {
    if (c == '\n') {
      com1_putc('\r');
    }
    if (c != '\r') {
      com1_putc(c);
    }
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
  fbcon_fill_rect_px(0, g_con.origin_y, g_con.width, g_con.height - g_con.origin_y,
                     g_con.bg);
  g_con.col = 0;
  g_con.row = 0;
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

static void fbcon_print_dec_u32(uint32_t v) {
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

static void fbcon_print_hex8(uint8_t v) {
  static const char hex[] = "0123456789ABCDEF";
  fbcon_putc(hex[(v >> 4) & 0xF]);
  fbcon_putc(hex[v & 0xF]);
}

static void fbcon_print_hex16(uint16_t v) {
  fbcon_print_hex8((uint8_t)(v >> 8));
  fbcon_print_hex8((uint8_t)(v & 0xFFu));
}

static void fbcon_print_ipv4(uint32_t ip) {
  fbcon_print_dec_u32((ip >> 24) & 0xFFu);
  fbcon_putc('.');
  fbcon_print_dec_u32((ip >> 16) & 0xFFu);
  fbcon_putc('.');
  fbcon_print_dec_u32((ip >> 8) & 0xFFu);
  fbcon_putc('.');
  fbcon_print_dec_u32(ip & 0xFFu);
}

static void fbcon_print_mac(const uint8_t mac[6]) {
  for (uint32_t i = 0; i < 6; ++i) {
    if (i) {
      fbcon_putc(':');
    }
    fbcon_print_hex8(mac[i]);
  }
}

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

static int capyos_icon_mask_get(uint32_t x, uint32_t y) {
  if (x >= CAPYOS_ICON_W || y >= CAPYOS_ICON_H) {
    return 0;
  }
  uint8_t byte = capyos_icon_mask[y * CAPYOS_ICON_STRIDE + (x / 8u)];
  return (byte & (uint8_t)(1u << (7u - (x & 7u)))) != 0;
}

static void splash_spin_delay(uint32_t loops) {
  for (volatile uint32_t i = 0; i < loops; ++i) {
    cpu_relax();
  }
}

static void ui_draw_capyos_icon(uint32_t x0, uint32_t y0, uint32_t scale,
                                uint32_t color) {
  if (scale == 0) {
    return;
  }
  for (uint32_t y = 0; y < CAPYOS_ICON_H; ++y) {
    for (uint32_t x = 0; x < CAPYOS_ICON_W; ++x) {
      if (capyos_icon_mask_get(x, y)) {
        fbcon_fill_rect_px(x0 + (x * scale), y0 + (y * scale), scale, scale,
                           color);
      }
    }
  }
}

static void ui_boot_splash(void) {
  if (!g_con.fb || g_con.width < 160 || g_con.height < 120) {
    return;
  }

  const uint32_t splash_bg = 0x000A1713;
  const uint32_t icon_color = 0x0000A651;
  const uint32_t bar_border = 0x00213A31;
  const uint32_t bar_bg = 0x0012221C;
  const uint32_t bar_fill = 0x0000C364;

  uint32_t scale = (g_con.height / 4u) / CAPYOS_ICON_H;
  if (scale == 0) {
    scale = 1;
  }
  if (scale > 4) {
    scale = 4;
  }
  uint32_t icon_w = CAPYOS_ICON_W * scale;
  uint32_t icon_h = CAPYOS_ICON_H * scale;

  uint32_t bar_w = g_con.width / 3u;
  if (bar_w < 140) {
    bar_w = 140;
  } else if (bar_w > 420) {
    bar_w = 420;
  }
  uint32_t bar_h = g_con.height / 96u;
  if (bar_h < 8) {
    bar_h = 8;
  } else if (bar_h > 14) {
    bar_h = 14;
  }

  uint32_t total_h = icon_h + (scale * 10u) + bar_h;
  uint32_t icon_x = (g_con.width > icon_w) ? (g_con.width - icon_w) / 2u : 0;
  uint32_t icon_y =
      (g_con.height > total_h) ? (g_con.height - total_h) / 2u : 0;
  uint32_t bar_x = (g_con.width > bar_w) ? (g_con.width - bar_w) / 2u : 0;
  uint32_t bar_y = icon_y + icon_h + (scale * 10u);

  fbcon_fill_rect_px(0, 0, g_con.width, g_con.height, splash_bg);
  ui_draw_capyos_icon(icon_x, icon_y, scale, icon_color);
  fbcon_fill_rect_px(bar_x, bar_y, bar_w, bar_h, bar_border);

  uint32_t inner_x = bar_x;
  uint32_t inner_y = bar_y;
  uint32_t inner_w = bar_w;
  uint32_t inner_h = bar_h;
  if (bar_w > 4u) {
    inner_x += 2u;
    inner_w -= 4u;
  }
  if (bar_h > 4u) {
    inner_y += 2u;
    inner_h -= 4u;
  }

  for (uint32_t step = 0; step <= 14; ++step) {
    fbcon_fill_rect_px(inner_x, inner_y, inner_w, inner_h, bar_bg);
    uint32_t fill_w = (inner_w * step) / 14u;
    if (fill_w > 0u) {
      fbcon_fill_rect_px(inner_x, inner_y, fill_w, inner_h, bar_fill);
    }
    splash_spin_delay(2200000u);
  }

  splash_spin_delay(3200000u);
}

static void ui_banner(void) {
  fbcon_print("CAPY64  ");
  fbcon_print("RSDP=");
  fbcon_print(rsdp_is_valid(g_h ? g_h->rsdp : 0) ? "OK " : "-- ");
  fbcon_print("rsdp=");
  fbcon_print_hex64(g_h ? g_h->rsdp : 0);
  fbcon_print(" fb=");
  fbcon_print_hex64(g_h ? g_h->fb.base : 0);
  fbcon_putc('\n');
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
static struct super_block g_shell_root_sb;
static struct system_settings g_shell_settings;
static int g_shell_initialized = 0;
static int g_shell_fs_ready = 0;

static size_t local_strlen(const char *s) {
  size_t len = 0;
  if (!s)
    return 0;
  while (s[len]) {
    ++len;
  }
  return len;
}

static void local_copy(char *dst, size_t dst_size, const char *src) {
  if (!dst || dst_size == 0) {
    return;
  }
  size_t i = 0;
  if (src) {
    while (src[i] && i < dst_size - 1) {
      dst[i] = src[i];
      ++i;
    }
  }
  dst[i] = '\0';
}

static int fs_ensure_dir_recursive(const char *path) {
  if (!path || path[0] != '/') {
    return -1;
  }
  char build[128];
  size_t build_len = 1;
  build[0] = '/';
  build[1] = '\0';
  const char *p = path;
  while (*p == '/') {
    ++p;
  }
  while (*p) {
    const char *start = p;
    size_t len = 0;
    while (start[len] && start[len] != '/') {
      ++len;
    }
    if (len > 0) {
      if (build_len > 1) {
        if (build_len + 1 >= sizeof(build)) {
          return -1;
        }
        build[build_len++] = '/';
      }
      if (build_len + len >= sizeof(build)) {
        return -1;
      }
      for (size_t i = 0; i < len; ++i) {
        build[build_len++] = start[i];
      }
      build[build_len] = '\0';
      struct dentry *d = NULL;
      if (vfs_lookup(build, &d) != 0) {
        if (vfs_create(build, VFS_MODE_DIR, NULL) != 0) {
          return -1;
        }
      } else if (d && d->refcount) {
        d->refcount--;
      }
    }
    p += len;
    while (*p == '/') {
      ++p;
    }
  }
  return 0;
}

static int fs_write_text_file(const char *path, const char *text) {
  if (!path || !text) {
    return -1;
  }
  (void)vfs_unlink(path);
  if (vfs_create(path, VFS_MODE_FILE, NULL) != 0) {
    return -1;
  }
  struct file *f = vfs_open(path, VFS_OPEN_WRITE);
  if (!f) {
    return -1;
  }
  size_t len = local_strlen(text);
  long written = vfs_write(f, text, len);
  vfs_close(f);
  return (written == (long)len) ? 0 : -1;
}

static int shell_bootstrap_filesystem(void) {
  if (g_shell_fs_ready) {
    return 0;
  }

  buffer_cache_init();
  vfs_init();
  ramdisk_init(512);
  struct block_device *ram = ramdisk_device();
  if (!ram) {
    fbcon_print("[fs] ERRO: ramdisk indisponivel.\n");
    return -1;
  }

  int fmt_rc = noirfs_format(ram, 128, ram->block_count, NULL);
  if (fmt_rc != 0) {
    fbcon_print("[fs] ERRO: falha ao formatar NoirFS em RAM. rc=");
    fbcon_print_hex((uint64_t)(uint32_t)fmt_rc);
    fbcon_putc('\n');
    return -1;
  }
  int mount_rc = mount_noirfs(ram, &g_shell_root_sb);
  int root_rc = (mount_rc == 0) ? vfs_mount_root(&g_shell_root_sb) : -1;
  if (mount_rc != 0 || root_rc != 0) {
    fbcon_print("[fs] ERRO: falha ao montar NoirFS em RAM. mount=");
    fbcon_print_hex((uint64_t)(uint32_t)mount_rc);
    fbcon_print(" root=");
    fbcon_print_hex((uint64_t)(uint32_t)root_rc);
    fbcon_putc('\n');
    return -1;
  }

  const char *dirs[] = {"/bin", "/docs", "/etc", "/home",
                        "/home/admin", "/system", "/tmp", "/var", "/var/log"};
  for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i) {
    if (fs_ensure_dir_recursive(dirs[i]) != 0) {
      fbcon_print("[fs] ERRO: falha ao criar estrutura base.\n");
      return -1;
    }
  }

  static const char *cli_doc =
      "CapyCLI x64 (early)\n"
      "Comandos principais:\n"
      "  list, go, mypath, mk-file, mk-dir, kill-file, kill-dir,\n"
      "  move, clone, print-file, open, hunt-any, find,\n"
      "  help-any, help-docs, print-version, print-envs,\n"
      "  shutdown-reboot, shutdown-off, do-sync\n";
  if (fs_write_text_file("/docs/noiros-cli-reference.txt", cli_doc) != 0) {
    fbcon_print("[fs] aviso: nao foi possivel gravar /docs/noiros-cli-reference.txt\n");
  }

  if (userdb_ensure() != 0) {
    fbcon_print("[fs] ERRO: nao foi possivel preparar /etc/users.db.\n");
    return -1;
  }
  struct user_record existing;
  if (userdb_find("admin", &existing) != 0) {
    struct user_record admin;
    if (user_record_init("admin", "admin", "admin", 0, 0, "/home/admin",
                         &admin) != 0 ||
        userdb_add(&admin) != 0) {
      fbcon_print("[fs] ERRO: nao foi possivel criar usuario admin padrao.\n");
      return -1;
    }
  }

  g_shell_fs_ready = 1;
  fbcon_print("[fs] NoirFS em RAM pronto para CLI.\n");
  return 0;
}

/* Initialize shell context for command dispatch */
static void init_shell_context(const struct user_record *user) {
  if (!g_shell_initialized) {
    if (shell_bootstrap_filesystem() != 0) {
      return;
    }
    if (system_load_settings(&g_shell_settings) != 0) {
      local_copy(g_shell_settings.hostname, sizeof(g_shell_settings.hostname),
                 "capyos64");
      local_copy(g_shell_settings.theme, sizeof(g_shell_settings.theme), "noir");
      local_copy(g_shell_settings.keyboard_layout,
                 sizeof(g_shell_settings.keyboard_layout), "us");
      g_shell_settings.splash_enabled = 0;
      g_shell_settings.diagnostics_enabled = 0;
    }
    g_shell_initialized = 1;
  }

  struct user_record fallback;
  user_record_clear(&fallback);
  local_copy(fallback.username, sizeof(fallback.username), "admin");
  fallback.uid = 0;
  fallback.gid = 0;
  local_copy(fallback.role, sizeof(fallback.role), "admin");
  local_copy(fallback.home, sizeof(fallback.home), "/home/admin");
  const struct user_record *active = user ? user : &fallback;

  if (session_begin(&g_session_ctx, active) != 0) {
    session_reset(&g_session_ctx);
    session_set_cwd(&g_session_ctx, "/");
  }
  session_set_active(&g_session_ctx);
  shell_context_init(&g_shell_ctx, &g_session_ctx, &g_shell_settings);
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

  size_t set_count = 0;
  const struct shell_command_set *sets = shell_command_sets(&set_count);

  for (size_t i = 0; i < set_count; i++) {
    for (size_t j = 0; j < sets[i].count; j++) {
      if (shell_string_equal(argv[0], sets[i].commands[j].name)) {
        (void)sets[i].commands[j].handler(&g_shell_ctx, argc, argv);
        return 1;
      }
    }
  }

  return 0;
}

static int run_shell_alias(const char *alias_line) {
  if (!alias_line) {
    return 0;
  }
  char tmp[64];
  size_t i = 0;
  while (alias_line[i] && i < sizeof(tmp) - 1) {
    tmp[i] = alias_line[i];
    ++i;
  }
  tmp[i] = '\0';
  return try_shell_command(tmp);
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
  for (uint32_t spin = 0; spin < 200000; ++spin) {
    if (inb(COM1_PORT + 5) & 0x20) {
      outb(COM1_PORT, (uint8_t)c);
      return;
    }
    cpu_relax();
  }
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
static int g_has_efi_input = 0;
static int g_has_hyperv_input = 0;
static uint64_t g_efi_system_table = 0;
static int g_shift = 0;

/* VMBus keyboard polling (from vmbus_keyboard.c) */
extern struct vmbus_keyboard *vmbus_get_keyboard(void);
extern int vmbus_keyboard_poll(struct vmbus_keyboard *kbd, uint8_t *scancode,
                               int *is_break);

/* Forward declaration */
static char scancode_to_ascii(uint8_t sc, int shift);

/* Polls one translated input character from available backends. */
static int poll_input_char(char *out_char) {
  if (!out_char) {
    return 0;
  }

  char c = 0;
  uint8_t sc = 0;

  /* Prefer firmware keyboard path in UEFI VMs (Hyper-V Gen2/OVMF). */
  if (g_has_efi_input && efi_poll_char(g_efi_system_table, &c)) {
    *out_char = c;
    return 1;
  }

  /* PS/2 fallback for legacy keyboards. */
  if (g_has_ps2_input && ps2_poll_scancode(&sc)) {
    if (sc == 0x2A || sc == 0x36) {
      g_shift = 1;
      return 0;
    }
    if (sc == 0xAA || sc == 0xB6) {
      g_shift = 0;
      return 0;
    }
    if (sc & 0x80) {
      return 0;
    }
    if (sc == 0x1C) {
      *out_char = '\n';
      return 1;
    }
    if (sc == 0x0E) {
      *out_char = '\b';
      return 1;
    }
    c = scancode_to_ascii(sc, g_shift);
    if (c) {
      *out_char = c;
      return 1;
    }
  }

  /* Hyper-V synthetic keyboard path (kept optional while driver matures). */
  if (g_has_hyperv_input) {
    struct vmbus_keyboard *hvkbd = vmbus_get_keyboard();
    if (hvkbd) {
      uint8_t hv_sc = 0;
      int hv_break = 0;
      if (vmbus_keyboard_poll(hvkbd, &hv_sc, &hv_break) && !hv_break) {
        if (hv_sc == 0x2A || hv_sc == 0x36) {
          g_shift = 1;
          return 0;
        }
        if (hv_sc == 0xAA || hv_sc == 0xB6) {
          g_shift = 0;
          return 0;
        }
        if (hv_sc == 0x1C) {
          *out_char = '\n';
          return 1;
        }
        if (hv_sc == 0x0E) {
          *out_char = '\b';
          return 1;
        }
        c = scancode_to_ascii(hv_sc, g_shift);
        if (c) {
          *out_char = c;
          return 1;
        }
      }
    }
  }

  /* COM1 remains only as last-resort fallback/debug channel. */
  if (g_has_com1_input && com1_poll_char(&c)) {
    if (c == '\r') {
      c = '\n';
    }
    *out_char = c;
    return 1;
  }

  return 0;
}

int kernel_input_getc(char *out_char) {
  if (!out_char)
    return 0;
  for (;;) {
    if (poll_input_char(out_char)) {
      return 1;
    }
    cpu_relax();
  }
}

size_t kernel_input_readline(char *buf, size_t maxlen, int mask) {
  if (!buf || maxlen < 2) {
    return 0;
  }

  size_t len = 0;
  buf[0] = 0;

  for (;;) {
    char ch = 0;
    if (!kernel_input_getc(&ch)) {
      continue;
    }

    if (ch == 127 || ch == '\b') {
      if (len > 0) {
        len--;
        buf[len] = 0;
        fbcon_putc('\b');
      }
      continue;
    }

    if (ch == '\r') {
      ch = '\n';
    }
    if (ch == '\n') {
      fbcon_putc('\n');
      buf[len] = 0;
      return len;
    }
    if (len + 1 < maxlen) {
      buf[len++] = ch;
      buf[len] = 0;
      fbcon_putc(mask ? '*' : ch);
    }
  }
}

static size_t kernel_readline(char *buf, size_t maxlen, int mask) {
  return kernel_input_readline(buf, maxlen, mask);
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

  ui_boot_splash();
  fbcon_fill_rect_px(0, 0, g_con.width, g_con.height, g_con.bg);
  ui_draw_bars();
  ui_banner();
  fbcon_print("CapyOS x86_64 kernel (early)\n");
  fbcon_print("Boot OK. Inicializando drivers...\n\n");

  kcon_init();
  kinit();

  fbcon_print("[pci] Scanning PCIe bus...\n");
  if (nvme_init() == 0) {
    struct block_device *nvme_dev = nvme_get_block_device(0);
    if (nvme_dev) {
      fbcon_print("[nvme] Detectado: ");
      fbcon_print(nvme_dev->name);
      fbcon_print(" (");
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

  fbcon_print("\nDigite 'help-any' para comandos.\n");
  cmd_info();
  fbcon_putc('\n');

  com1_init();
  com1_puts("[COM1] CapyOS 64-bit serial console ready\r\n");

  int has_efi = 0;
  uint64_t efi_system_table = 0;
  if (g_h && g_h->efi_system_table) {
    EFI_SYSTEM_TABLE_K *st =
        (EFI_SYSTEM_TABLE_K *)(uintptr_t)g_h->efi_system_table;
    efi_system_table = g_h->efi_system_table;
    if (st && st->ConIn && st->ConIn->ReadKeyStroke) {
      has_efi = 1;
    }
    fbcon_print("[info] EFI ConIn: ");
    fbcon_print(has_efi ? "disponivel.\n" : "indisponivel.\n");
  } else {
    fbcon_print("[info] EFI ConIn: indisponivel.\n");
  }

  ps2_flush();
  int has_ps2 = ps2_controller_detect();
  fbcon_print(has_ps2 ? "[info] PS/2 detectado.\n"
                      : "[info] PS/2 nao detectado.\n");

  int has_com1 = com1_detect();
  fbcon_print(has_com1 ? "[info] COM1 detectado.\n"
                       : "[info] COM1 nao detectado.\n");

  int is_hyperv = hyperv_detect();
  int has_hyperv = 0;
  extern int hyperv_keyboard_init(void);
  if (is_hyperv) {
    fbcon_print("[hyperv] Hyper-V detectado.\n");
    if (!has_efi && !has_ps2) {
      fbcon_print("[hyperv] Tentando teclado VMBus (experimental)...\n");
      if (hyperv_keyboard_init() == 0) {
        has_hyperv = 1;
        fbcon_print("[hyperv] Teclado VMBus ativo.\n");
      } else {
        fbcon_print("[hyperv] Falha no VMBus keyboard; usando fallback.\n");
      }
    } else {
      fbcon_print("[hyperv] Entrada por EFI/PS/2 ativa, VMBus opcional.\n");
    }
  }

  /* If no keyboard backend is available, keep COM1 as emergency channel even
   * when loopback detection fails (common in some VM serial backends). */
  if (!has_efi && !has_ps2 && !has_hyperv && !has_com1) {
    has_com1 = 1;
    fbcon_print("[info] COM1 habilitado em modo de emergencia.\n");
  }

  g_com1_ready = has_com1 ? 1 : 0;
  g_serial_mirror = has_com1 ? 1 : 0;

  int has_usb = 0;
  struct xhci_controller xhci = {0};
  if (!has_ps2 && !has_efi && !has_hyperv && !is_hyperv) {
    fbcon_print("[usb] Buscando controlador XHCI...\n");
    if (xhci_find(&xhci) == 0) {
      fbcon_print("[usb] XHCI encontrado.\n");
      if (xhci_init(&xhci) == 0) {
        fbcon_print("[usb] XHCI inicializado.\n");
        if (xhci_start(&xhci) == 0) {
          fbcon_print("[usb] XHCI rodando.\n");
          has_usb = 1;
          fbcon_print("[usb] Enumeracao HID pendente para teclado.\n");
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
  (void)has_usb;

  fbcon_print("[net] Inicializando stack TCP/IP...\n");
  int net_rc = net_stack_init();
  struct net_stack_status net_status;
  if (net_stack_status(&net_status) == 0) {
    if (net_rc == 0 && net_status.nic.found) {
      fbcon_print("[net] NIC: ");
      fbcon_print(net_driver_name(net_status.nic.kind));
      fbcon_print(" @ ");
      fbcon_print_dec_u32((uint32_t)net_status.nic.bus);
      fbcon_putc(':');
      fbcon_print_dec_u32((uint32_t)net_status.nic.device);
      fbcon_putc('.');
      fbcon_print_dec_u32((uint32_t)net_status.nic.function);
      fbcon_print(" vendor=");
      fbcon_print_hex16(net_status.nic.vendor_id);
      fbcon_print(" device=");
      fbcon_print_hex16(net_status.nic.device_id);
      fbcon_putc('\n');
    } else {
      fbcon_print("[net] Nenhuma NIC suportada detectada (e1000/rtl8139/virtio/hyperv).\n");
    }
    fbcon_print("[net] MAC: ");
    fbcon_print_mac(net_status.ipv4.mac);
    fbcon_print("  IPv4: ");
    fbcon_print_ipv4(net_status.ipv4.addr);
    fbcon_print("  GW: ");
    fbcon_print_ipv4(net_status.ipv4.gateway);
    fbcon_putc('\n');
  } else {
    fbcon_print("[net] Falha ao consultar estado da stack.\n");
  }

  if (net_stack_protocol_selftest() == 0) {
    fbcon_print("[net] Self-test de protocolos (ARP/IPv4/ICMP/UDP/TCP): OK\n");
  } else {
    fbcon_print("[net] Self-test de protocolos (ARP/IPv4/ICMP/UDP/TCP): FALHOU\n");
  }

  g_has_efi_input = has_efi;
  g_efi_system_table = efi_system_table;
  g_has_ps2_input = has_ps2;
  g_has_hyperv_input = has_hyperv;
  g_has_com1_input = has_com1;
  g_shift = 0;

  if (!has_efi && !has_ps2 && !has_hyperv && !has_com1) {
    fbcon_print("\n[erro] Nenhum dispositivo de entrada disponivel.\n");
  }

  /* Prepare filesystem/user database before login authentication. */
  init_shell_context(NULL);
  if (!g_shell_initialized) {
    fbcon_print("[erro] Falha ao preparar runtime do shell.\n");
    for (;;)
      cpu_relax();
  }

  char line[128];
  char user_input[USER_NAME_MAX];
  char pass_input[64];
  struct user_record login_user;
  int logged_in = 0;

login_prompt:
  fbcon_print("\n");
  fbcon_print("========================================\n");
  fbcon_print("             CapyOS 64-bit             \n");
  fbcon_print("========================================\n");
  fbcon_print("\n");

  if (!has_efi && !has_ps2 && !has_hyperv && !has_com1) {
    fbcon_print("[!] Sem dispositivo de entrada disponivel.\n\n");
  }

  logged_in = 0;
  user_record_clear(&login_user);
  for (int attempts = 0; attempts < 3 && !logged_in; attempts++) {
    fbcon_print("Usuario: ");
    kernel_readline(user_input, sizeof(user_input), 0);

    fbcon_print("Senha: ");
    kernel_readline(pass_input, sizeof(pass_input), 1);

    if (userdb_authenticate(user_input, pass_input, &login_user) == 0) {
      init_shell_context(&login_user);
      if (!g_shell_initialized) {
        fbcon_print("[erro] Falha ao inicializar shell.\n");
        break;
      }
      logged_in = 1;
      fbcon_print("\nBem-vindo, ");
      fbcon_print(login_user.username);
      fbcon_print("!\n\n");
    } else {
      fbcon_print("\nCredenciais invalidas.\n");
      if (attempts < 2) {
        fbcon_print("Tente novamente.\n\n");
      }
    }

    for (size_t i = 0; i < sizeof(pass_input); ++i) {
      pass_input[i] = '\0';
    }
  }

  if (!logged_in) {
    fbcon_print("\nMuitas tentativas falhas. Sistema bloqueado.\n");
    for (;;)
      cpu_relax();
  }

  for (;;) {
    const struct user_record *active_user = session_user(&g_session_ctx);
    const char *name =
        (active_user && active_user->username[0]) ? active_user->username
                                                   : "user";
    const char *host =
        g_shell_settings.hostname[0] ? g_shell_settings.hostname : "capy64";
    const char *cwd = session_cwd(&g_session_ctx);

    fbcon_print(name);
    fbcon_putc('@');
    fbcon_print(host);
    fbcon_putc(':');
    fbcon_print(cwd);
    fbcon_print("> ");

    kernel_readline(line, sizeof(line), 0);
    if (!line[0]) {
      continue;
    }

    if (try_shell_command(line)) {
      if (shell_context_should_logout(&g_shell_ctx)) {
        session_reset(&g_session_ctx);
        session_set_active(NULL);
        fbcon_clear_view();
        ui_banner();
        goto login_prompt;
      }
      continue;
    }

    if (streq(line, "help")) {
      (void)run_shell_alias("help-any");
      continue;
    }
    if (streq(line, "clear")) {
      (void)run_shell_alias("mess");
      continue;
    }
    if (streq(line, "reboot")) {
      (void)run_shell_alias("shutdown-reboot");
      continue;
    }
    if (streq(line, "halt")) {
      (void)run_shell_alias("shutdown-off");
      continue;
    }
    if (streq(line, "info")) {
      cmd_info();
      continue;
    }

    fbcon_print("Comando desconhecido: ");
    fbcon_print(line);
    fbcon_print("\nUse 'help-any' para listar comandos.\n");
  }
}
__asm__(".section .note.GNU-stack,\"\",@progbits");
