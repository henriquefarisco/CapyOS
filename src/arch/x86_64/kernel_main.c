// Minimal x86_64 kernel for bringup: shows a framebuffer UI + a tiny command
// prompt. This is intentionally simple (no interrupts yet). It helps validate
// disk boot end-to-end after the UEFI installer provisions GPT/ESP/BOOT.
#pragma GCC optimize("O0")
#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/input_runtime.h"
#include "arch/x86_64/hyperv_runtime_coordinator.h"
#include "arch/x86_64/interrupts.h"
#include "arch/x86_64/native_runtime_gate.h"
#include "arch/x86_64/kernel_platform_runtime.h"
#include "arch/x86_64/kernel_shell_dispatch.h"
#include "arch/x86_64/kernel_shell_runtime.h"
#include "arch/x86_64/kernel_volume_runtime.h"
#include "arch/x86_64/platform_timer.h"
#include "arch/x86_64/storage_runtime.h"
#include "arch/x86_64/timebase.h"
#include "boot/boot_config.h"
#include "boot/boot_menu.h"
#include "boot/boot_ui.h"
#include "boot/handoff.h"
#include "branding/capyos_icon_mask.h"
#include "core/kcon.h"
#include "core/klog.h"
#include "core/klog_persist.h"
#include "core/login_runtime.h"
#include "core/network_bootstrap.h"
#include "core/service_manager.h"
#include "core/session.h"
#include "core/system_init.h"
#include "core/user.h"
#include "core/version.h"
#include "drivers/efi/efi_console.h"
#include "drivers/hyperv/hyperv.h"
#include "drivers/input/keyboard.h"
#include "drivers/input/keyboard_layout.h"
#include "drivers/nvme.h"
#include "drivers/pcie.h"
#include "drivers/storage/efi_block.h"
#include "drivers/timer/pit.h"
#include "drivers/usb/xhci.h"
#include "fs/block.h"
#include "fs/buffer.h"
#include "fs/capyfs.h"
#include "fs/ramdisk.h"
#include "fs/vfs.h"
#include "memory/kmem.h"
#include "net/stack.h"
#include "shell/commands.h"
#include "shell/core.h"

#define DEBUGCON_PORT 0xE9

void acpi_set_rsdp(uint64_t rsdp_addr);
void acpi_set_uefi_system_table(uint64_t system_table_addr);

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

static __attribute__((noreturn)) void kernel_halt_forever(void) {
  __asm__ volatile("cli");
  for (;;) {
    __asm__ volatile("hlt");
  }
}

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

enum fbcon_accent_style {
  FBCON_ACCENT_NONE = 0,
  FBCON_ACCENT_ACUTE,
  FBCON_ACCENT_GRAVE,
  FBCON_ACCENT_CIRCUMFLEX,
  FBCON_ACCENT_TILDE,
  FBCON_ACCENT_DIAERESIS,
  FBCON_ACCENT_CEDILLA,
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
static struct x64_input_runtime g_input_runtime;
static int g_exit_boot_services_attempted = 0;
static int g_exit_boot_services_done = 0;
static EFI_STATUS_K g_exit_boot_services_status = EFI_SUCCESS_K;
static int g_network_runtime_refresh_enabled = 0;
static uint32_t g_theme_splash_bg = 0x000A1713;
static uint32_t g_theme_splash_icon = 0x0000A651;
static uint32_t g_theme_splash_bar_border = 0x00213A31;
static uint32_t g_theme_splash_bar_bg = 0x0012221C;
static uint32_t g_theme_splash_bar_fill = 0x0000C364;

/* Forward declaration; COM1 implementation is defined later in this file. */
static void com1_putc(char c);
static int streq(const char *a, const char *b);
static void ui_draw_bars(void);
static void klog_print_adapter(const char *s);
static void klog_print_adapter_flush(void);
static void fbcon_fill_rect_px(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h,
                               uint32_t color);

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

static void fbcon_copy_glyph(uint8_t dst[8], const uint8_t *src) {
  for (uint32_t i = 0; i < 8u; ++i) {
    dst[i] = src[i];
  }
}

static void fbcon_apply_accent(uint8_t glyph[8], enum fbcon_accent_style accent) {
  if (!glyph) {
    return;
  }

  switch (accent) {
  case FBCON_ACCENT_ACUTE:
    glyph[0] |= 0x18;
    glyph[1] |= 0x0C;
    break;
  case FBCON_ACCENT_GRAVE:
    glyph[0] |= 0x18;
    glyph[1] |= 0x30;
    break;
  case FBCON_ACCENT_CIRCUMFLEX:
    glyph[0] |= 0x18;
    glyph[1] |= 0x24;
    break;
  case FBCON_ACCENT_TILDE:
    glyph[0] |= 0x36;
    glyph[1] |= 0x6C;
    break;
  case FBCON_ACCENT_DIAERESIS:
    glyph[0] |= 0x24;
    break;
  case FBCON_ACCENT_CEDILLA:
    glyph[7] |= 0x18;
    break;
  default:
    break;
  }
}

static int fbcon_lookup_extended_glyph(uint8_t uc, uint8_t *base,
                                       enum fbcon_accent_style *accent) {
  if (!base || !accent) {
    return 0;
  }

  switch (uc) {
  case 0x80:
    *base = 'C';
    *accent = FBCON_ACCENT_CEDILLA;
    return 1;
  case 0x81:
    *base = 'u';
    *accent = FBCON_ACCENT_DIAERESIS;
    return 1;
  case 0x82:
    *base = 'e';
    *accent = FBCON_ACCENT_ACUTE;
    return 1;
  case 0x83:
    *base = 'a';
    *accent = FBCON_ACCENT_CIRCUMFLEX;
    return 1;
  case 0x85:
    *base = 'a';
    *accent = FBCON_ACCENT_GRAVE;
    return 1;
  case 0x87:
    *base = 'c';
    *accent = FBCON_ACCENT_CEDILLA;
    return 1;
  case 0x88:
    *base = 'e';
    *accent = FBCON_ACCENT_CIRCUMFLEX;
    return 1;
  case 0x8C:
    *base = 'i';
    *accent = FBCON_ACCENT_CIRCUMFLEX;
    return 1;
  case 0x90:
    *base = 'E';
    *accent = FBCON_ACCENT_ACUTE;
    return 1;
  case 0x93:
    *base = 'o';
    *accent = FBCON_ACCENT_CIRCUMFLEX;
    return 1;
  case 0x96:
    *base = 'u';
    *accent = FBCON_ACCENT_CIRCUMFLEX;
    return 1;
  case 0x9A:
    *base = 'U';
    *accent = FBCON_ACCENT_DIAERESIS;
    return 1;
  case 0xA0:
    *base = 'a';
    *accent = FBCON_ACCENT_ACUTE;
    return 1;
  case 0xA1:
    *base = 'i';
    *accent = FBCON_ACCENT_ACUTE;
    return 1;
  case 0xA2:
    *base = 'o';
    *accent = FBCON_ACCENT_ACUTE;
    return 1;
  case 0xA3:
    *base = 'u';
    *accent = FBCON_ACCENT_ACUTE;
    return 1;
  case 0xB5:
    *base = 'A';
    *accent = FBCON_ACCENT_ACUTE;
    return 1;
  case 0xB7:
    *base = 'A';
    *accent = FBCON_ACCENT_GRAVE;
    return 1;
  case 0xC6:
    *base = 'a';
    *accent = FBCON_ACCENT_TILDE;
    return 1;
  case 0xC7:
    *base = 'A';
    *accent = FBCON_ACCENT_TILDE;
    return 1;
  case 0xD6:
    *base = 'I';
    *accent = FBCON_ACCENT_ACUTE;
    return 1;
  case 0xE0:
    *base = 'O';
    *accent = FBCON_ACCENT_ACUTE;
    return 1;
  case 0xE4:
    *base = 'O';
    *accent = FBCON_ACCENT_TILDE;
    return 1;
  case 0xE5:
    *base = 'o';
    *accent = FBCON_ACCENT_TILDE;
    return 1;
  case 0xE9:
    *base = 'U';
    *accent = FBCON_ACCENT_ACUTE;
    return 1;
  default:
    *base = '?';
    *accent = FBCON_ACCENT_NONE;
    return 0;
  }
}

static inline const uint8_t *font_glyph(uint8_t uc) {
  static uint8_t extended_glyph[8];
  const uint8_t (*font)[8];
  uint8_t base = '?';
  enum fbcon_accent_style accent = FBCON_ACCENT_NONE;
  __asm__ __volatile__("lea font8x8_basic(%%rip), %0" : "=r"(font));
  if (uc < 128) {
    return font[uc];
  }
  if (fbcon_lookup_extended_glyph(uc, &base, &accent)) {
    fbcon_copy_glyph(extended_glyph, font[base]);
    fbcon_apply_accent(extended_glyph, accent);
    return extended_glyph;
  }
  return font['?'];
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

/* Render a single character at pixel coordinates (x, y) with explicit colors.
 * Used by boot_ui/boot_menu via callback; does NOT advance the console cursor. */
void fbcon_putch_at(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg) {
  uint32_t saved_fg = g_con.fg;
  uint32_t saved_bg = g_con.bg;
  g_con.fg = fg;
  g_con.bg = bg;
  fbcon_putch_px(x, y, c);
  g_con.fg = saved_fg;
  g_con.bg = saved_bg;
}

static int g_fbcon_visual_muted = 0;
static char g_fbcon_muted_line[KLOG_LINE_MAX];
static uint32_t g_fbcon_muted_len = 0;

static void fbcon_muted_flush_line(void) {
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

static void fbcon_set_visual_muted(int muted) {
  if (!muted) {
    fbcon_muted_flush_line();
  }
  g_fbcon_visual_muted = muted ? 1 : 0;
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

static struct x64_platform_diag_io kernel_platform_diag_io(void) {
  struct x64_platform_diag_io io;
  io.print = fbcon_print;
  io.print_hex64 = fbcon_print_hex64;
  io.print_dec_u32 = fbcon_print_dec_u32;
  io.putc = fbcon_putc;
  return io;
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
  (void)g_h;
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

void ui_draw_capyos_icon(uint32_t x0, uint32_t y0, uint32_t scale,
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

  fbcon_fill_rect_px(0, 0, g_con.width, g_con.height, g_theme_splash_bg);
  ui_draw_capyos_icon(icon_x, icon_y, scale, g_theme_splash_icon);
  fbcon_fill_rect_px(bar_x, bar_y, bar_w, bar_h, g_theme_splash_bar_border);

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
    fbcon_fill_rect_px(inner_x, inner_y, inner_w, inner_h, g_theme_splash_bar_bg);
    uint32_t fill_w = (inner_w * step) / 14u;
    if (fill_w > 0u) {
      fbcon_fill_rect_px(inner_x, inner_y, fill_w, inner_h,
                         g_theme_splash_bar_fill);
    }
    splash_spin_delay(2200000u);
  }

  splash_spin_delay(3200000u);
}

static uint32_t ui_banner_strlen(const char *s) {
  uint32_t len = 0;
  if (!s) {
    return 0;
  }
  while (s[len]) {
    ++len;
  }
  return len;
}

static void ui_banner_append(char *dst, uint32_t cap, const char *src) {
  uint32_t len = ui_banner_strlen(dst);
  uint32_t pos = 0;

  if (!dst || cap == 0 || !src || len >= cap - 1u) {
    return;
  }
  while (src[pos] && len < cap - 1u) {
    dst[len++] = src[pos++];
  }
  dst[len] = '\0';
}

static void ui_banner_rule(uint32_t inner_cols) {
  fbcon_putc('+');
  for (uint32_t i = 0; i < inner_cols + 2u; ++i) {
    fbcon_putc('-');
  }
  fbcon_print("+\n");
}

static void ui_banner_line(uint32_t inner_cols, const char *text) {
  uint32_t len = ui_banner_strlen(text);
  fbcon_print("| ");
  for (uint32_t i = 0; i < inner_cols; ++i) {
    fbcon_putc(i < len ? text[i] : ' ');
  }
  fbcon_print(" |\n");
}

static void ui_banner(void) {
  char version_line[64];
  uint32_t inner_cols =
      (g_con.cols > 6u) ? (g_con.cols - 4u) : 0u;

  if (inner_cols > 58u) {
    inner_cols = 58u;
  }

  if (inner_cols < 38u) {
    fbcon_print("CAPYOS\n");
    fbcon_print(CAPYOS_VERSION_EXTENDED);
    fbcon_print("  x86_64\n");
    return;
  }

  version_line[0] = '\0';
  ui_banner_append(version_line, sizeof(version_line), " Version: ");
  ui_banner_append(version_line, sizeof(version_line), CAPYOS_VERSION_EXTENDED);
  ui_banner_append(version_line, sizeof(version_line), "   Arch: x86_64");

ui_banner_rule(inner_cols);
  ui_banner_line(inner_cols, "  ###  ###  ###  # #  ###  ### ");
  ui_banner_line(inner_cols, " #     # #  # #  # #  # #  #    ");
  ui_banner_line(inner_cols, " #     ###  ###   #   # #   ##  ");
  ui_banner_line(inner_cols, " #     # #  #     #   # #     # ");
  ui_banner_line(inner_cols, "  ###  # #  #     #   ###  ###  ");
  ui_banner_line(inner_cols, "");
  ui_banner_line(inner_cols, "");
  ui_banner_line(inner_cols, "  CapyOS");
  ui_banner_line(inner_cols, "  Shell");

  ui_banner_line(inner_cols, "");
  ui_banner_line(inner_cols, version_line);
  ui_banner_rule(inner_cols);
}

static int handoff_boot_services_active(void);
static void maybe_exit_boot_services_after_native_runtime(void);
static int kernel_allow_hybrid_storage_prepare(void);

static void cmd_info(void) {
  struct x64_platform_diag_io io = kernel_platform_diag_io();
  x64_kernel_print_cmd_info(g_h, rsdp_is_valid(g_h ? g_h->rsdp : 0),
                            &g_input_runtime, &io);
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
static int g_shell_persistent_storage = 0;
static size_t kernel_readline(char *buf, size_t maxlen, int mask);
static char g_active_volume_key[X64_KERNEL_VOLUME_KEY_MAX];
static int g_active_volume_key_ready = 0;
static char g_handoff_volume_key[X64_KERNEL_VOLUME_KEY_MAX];
static int g_handoff_volume_key_ready = 0;
static uint8_t g_data_io_probe[CAPYFS_BLOCK_SIZE]
    __attribute__((aligned(64)));

static const uint8_t g_disk_salt[16] = {0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53,
                                        0x2d, 0x46, 0x53, 0x2d, 0x53, 0x61,
                                        0x6c, 0x74, 0x21, 0x00};
static const uint32_t g_kdf_iterations = 16000;

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

static int handoff_keyboard_layout(char *out, size_t out_size) {
  if (!out || out_size < 2 || !g_h || g_h->version < 3 ||
      !g_h->boot_keyboard_layout[0]) {
    return -1;
  }
  local_copy(out, out_size, g_h->boot_keyboard_layout);
  return 0;
}

static int handoff_language(char *out, size_t out_size) {
  if (!out || out_size < 2 || !g_h || g_h->version < 7 ||
      !g_h->boot_language[0]) {
    return -1;
  }
  local_copy(out, out_size, g_h->boot_language);
  return 0;
}

static int handoff_boot_services_active(void) {
  return x64_kernel_handoff_boot_services_active(g_h);
}

static int handoff_has_firmware_input(void) {
  return x64_kernel_handoff_has_firmware_input(g_h);
}

static int handoff_has_firmware_block_io(void) {
  return x64_kernel_handoff_has_firmware_block_io(g_h);
}

static int handoff_has_exit_boot_services_contract(void) {
  return x64_kernel_handoff_has_exit_boot_services_contract(g_h);
}

static __attribute__((unused)) void print_platform_runtime_mode(void) {
  struct x64_platform_diag_io io = kernel_platform_diag_io();
  x64_kernel_print_platform_runtime_mode(g_h, &io);
}

static __attribute__((unused)) void print_platform_tables_status(void) {
  struct x64_platform_diag_io io = kernel_platform_diag_io();
  x64_kernel_print_platform_tables_status(&io);
}

static __attribute__((unused)) void print_platform_timer_status(void) {
  struct x64_platform_diag_io io = kernel_platform_diag_io();
  x64_kernel_print_platform_timer_status(&io);
}

static void print_input_runtime_status(void) {
  struct x64_platform_diag_io io = kernel_platform_diag_io();
  x64_kernel_print_input_runtime_status(&g_input_runtime, &io);
}

static void print_storage_runtime_status(void) {
  struct x64_platform_diag_io io = kernel_platform_diag_io();
  x64_kernel_print_storage_runtime_status(g_h, &io);
}

static __attribute__((unused)) void print_native_runtime_gate_status(void) {
  struct x64_platform_diag_io io = kernel_platform_diag_io();
  x64_kernel_print_native_runtime_gate_status(
      g_h, &g_input_runtime, g_exit_boot_services_attempted,
      g_exit_boot_services_done, g_exit_boot_services_status, &io);
}

static void update_system_runtime_platform_status(void) {
  x64_kernel_update_system_runtime_platform_status(
      g_h, &g_input_runtime, g_exit_boot_services_attempted,
      g_exit_boot_services_done, g_exit_boot_services_status);
}

static void kernel_maybe_refresh_network_runtime(void) {
  if (!g_network_runtime_refresh_enabled) {
    return;
  }
  (void)net_stack_refresh_runtime();
}

static void kernel_update_logger_service_status(int rc) {
  if (rc == 0) {
    (void)service_manager_set_state(
        SYSTEM_SERVICE_LOGGER, SYSTEM_SERVICE_STATE_READY, 0,
        "persistent klog active in /var/log/capyos_klog.txt");
  } else {
    (void)service_manager_set_state(
        SYSTEM_SERVICE_LOGGER, SYSTEM_SERVICE_STATE_DEGRADED, rc,
        "persistent klog unavailable; ring buffer only");
  }
}

static void kernel_update_network_service_status(void) {
  struct net_stack_status net_status = {0};

  if (net_stack_status(&net_status) != 0) {
    (void)service_manager_set_state(
        SYSTEM_SERVICE_NETWORKD, SYSTEM_SERVICE_STATE_BLOCKED, -1,
        "network status unavailable");
    return;
  }
  if (!net_status.nic.found) {
    (void)service_manager_set_state(
        SYSTEM_SERVICE_NETWORKD, SYSTEM_SERVICE_STATE_BLOCKED, -2,
        "no network adapter detected");
    return;
  }
  if (!net_status.runtime_supported) {
    (void)service_manager_set_state(
        SYSTEM_SERVICE_NETWORKD, SYSTEM_SERVICE_STATE_BLOCKED, -3,
        "adapter detected but driver is not validated");
    return;
  }
  if (!net_status.ready) {
    (void)service_manager_set_state(
        SYSTEM_SERVICE_NETWORKD, SYSTEM_SERVICE_STATE_DEGRADED, -4,
        "validated network driver detected but not ready");
    return;
  }
  (void)service_manager_set_state(SYSTEM_SERVICE_NETWORKD,
                                  SYSTEM_SERVICE_STATE_READY, 0,
                                  "network stack ready");
}

static int kernel_service_poll_networkd(void *ctx) {
  (void)ctx;
  kernel_maybe_refresh_network_runtime();
  kernel_update_network_service_status();
  return 0;
}

static int kernel_service_start_networkd(void *ctx) {
  (void)ctx;
  g_network_runtime_refresh_enabled = 1;
  return kernel_service_poll_networkd(NULL);
}

static int kernel_service_stop_networkd(void *ctx) {
  (void)ctx;
  g_network_runtime_refresh_enabled = 0;
  return 0;
}

static int kernel_service_poll_logger(void *ctx) {
  int rc = 0;

  (void)ctx;
  rc = klog_persist_flush_default();
  kernel_update_logger_service_status(rc);
  return rc;
}

static int kernel_service_start_logger(void *ctx) {
  return kernel_service_poll_logger(ctx);
}

static int kernel_service_stop_logger(void *ctx) {
  (void)ctx;
  return klog_persist_flush_default();
}

static void kernel_service_poll(void) {
  (void)service_manager_poll_due(pit_ticks());
}

static void print_active_efi_runtime_trace(void) {
  struct x64_platform_diag_io io = kernel_platform_diag_io();
  x64_kernel_print_active_efi_runtime_trace(&io);
}

static struct x64_kernel_volume_runtime_state
kernel_volume_runtime_state(void) {
  struct x64_kernel_volume_runtime_state state;
  state.handoff = g_h;
  state.root_sb = &g_shell_root_sb;
  state.shell_persistent_storage = &g_shell_persistent_storage;
  state.active_volume_key = g_active_volume_key;
  state.active_volume_key_size = sizeof(g_active_volume_key);
  state.active_volume_key_ready = &g_active_volume_key_ready;
  state.handoff_volume_key = g_handoff_volume_key;
  state.handoff_volume_key_size = sizeof(g_handoff_volume_key);
  state.handoff_volume_key_ready = &g_handoff_volume_key_ready;
  state.data_io_probe = g_data_io_probe;
  state.data_io_probe_size = sizeof(g_data_io_probe);
  state.disk_salt = g_disk_salt;
  state.disk_salt_size = sizeof(g_disk_salt);
  state.kdf_iterations = g_kdf_iterations;
  return state;
}

static struct x64_kernel_volume_runtime_io kernel_volume_runtime_io(void) {
  struct x64_kernel_volume_runtime_io io;
  io.print = fbcon_print;
  io.print_hex = fbcon_print_hex;
  io.print_hex64 = fbcon_print_hex64;
  io.print_dec_u32 = fbcon_print_dec_u32;
  io.putc = fbcon_putc;
  io.readline = kernel_readline;
  io.print_active_efi_runtime_trace = print_active_efi_runtime_trace;
  return io;
}

static int load_handoff_volume_key(void) {
  struct x64_kernel_volume_runtime_state state = kernel_volume_runtime_state();
  return x64_kernel_volume_runtime_load_handoff_key(&state);
}

static int fs_ensure_dir_recursive(const char *path) {
  return x64_kernel_volume_runtime_ensure_dir_recursive(path);
}

static int fs_write_text_file(const char *path, const char *text) {
  return x64_kernel_volume_runtime_write_text_file(path, text);
}

static int persist_active_volume_key_hash(void) {
  struct x64_kernel_volume_runtime_state state = kernel_volume_runtime_state();
  return x64_kernel_volume_runtime_persist_active_key_hash(&state);
}

static int mount_encrypted_data_volume(struct block_device *data_dev) {
  struct x64_kernel_volume_runtime_state state = kernel_volume_runtime_state();
  struct x64_kernel_volume_runtime_io io = kernel_volume_runtime_io();
  return x64_kernel_volume_runtime_mount_encrypted_data_volume(&state, &io,
                                                               data_dev);
}

static int mount_root_CAPYFS(struct block_device *dev, const char *label) {
  struct x64_kernel_volume_runtime_state state = kernel_volume_runtime_state();
  struct x64_kernel_volume_runtime_io io = kernel_volume_runtime_io();
  return x64_kernel_volume_runtime_mount_root_capyfs(&state, &io, dev, label);
}

static struct x64_kernel_shell_runtime_state kernel_shell_runtime_state(void) {
  struct x64_kernel_shell_runtime_state state;
  state.handoff = g_h;
  state.shell_ctx = &g_shell_ctx;
  state.session_ctx = &g_session_ctx;
  state.settings = &g_shell_settings;
  state.shell_initialized = &g_shell_initialized;
  state.shell_fs_ready = &g_shell_fs_ready;
  state.shell_persistent_storage = &g_shell_persistent_storage;
  state.data_io_probe = g_data_io_probe;
  state.data_io_probe_size = sizeof(g_data_io_probe);
  return state;
}

static struct x64_kernel_shell_runtime_io kernel_shell_runtime_io(void) {
  struct x64_kernel_shell_runtime_io io;
  io.print = fbcon_print;
  io.print_hex = fbcon_print_hex;
  io.print_hex64 = fbcon_print_hex64;
  io.print_dec_u32 = fbcon_print_dec_u32;
  io.putc = fbcon_putc;
  return io;
}

static struct x64_hyperv_runtime_coordinator_ops
kernel_hyperv_runtime_coordinator_ops(void) {
  struct x64_hyperv_runtime_coordinator_ops ops;
  ops.boot_services_active = handoff_boot_services_active;
  ops.allow_hybrid_storage_prepare = kernel_allow_hybrid_storage_prepare;
  ops.maybe_exit_boot_services_after_native_runtime =
      maybe_exit_boot_services_after_native_runtime;
  ops.update_system_runtime_platform_status =
      update_system_runtime_platform_status;
  ops.print_input_runtime_status = print_input_runtime_status;
  ops.print_storage_runtime_status = print_storage_runtime_status;
  ops.print = fbcon_print;
  return ops;
}

static void kernel_shell_after_native_runtime_ready(void) {
  struct x64_hyperv_runtime_coordinator_ops ops =
      kernel_hyperv_runtime_coordinator_ops();
  x64_hyperv_runtime_after_native_ready(&ops);
}

int x64_kernel_manual_prepare_hyperv_input(void) {
  struct x64_native_runtime_gate_status gate;

  if (handoff_boot_services_active()) {
    update_system_runtime_platform_status();
    return 0;
  }

  if (g_input_runtime.hyperv_deferred) {
    int rc = x64_input_force_enable_hyperv_native(
        &g_input_runtime, handoff_boot_services_active(), klog_print_adapter);
    klog_print_adapter_flush();
    update_system_runtime_platform_status();
    return rc;
  }

  x64_native_runtime_gate_eval(g_h, &g_input_runtime,
                               g_exit_boot_services_attempted,
                               g_exit_boot_services_done,
                               g_exit_boot_services_status, &gate);
  if (gate.gate != SYSTEM_EXIT_BOOT_SERVICES_GATE_WAIT_INPUT) {
    update_system_runtime_platform_status();
    return 0;
  }

  {
    int rc = x64_input_try_prepare_hyperv_runtime(&g_input_runtime,
                                                  handoff_boot_services_active(),
                                                  klog_print_adapter);
    klog_print_adapter_flush();
    update_system_runtime_platform_status();
    return rc;
  }
}

int x64_kernel_manual_prepare_hyperv_storage(void) {
  int rc = x64_storage_runtime_manual_hyperv_step(
      handoff_boot_services_active(), klog_print_adapter);
  klog_print_adapter_flush();
  update_system_runtime_platform_status();
  return rc;
}

int x64_kernel_manual_prepare_native_bridge(void) {
  int rc = x64_platform_tables_prepare_bridge();
  if (rc > 0) {
    klog(KLOG_INFO,
         "[runtime] Bridge nativo x64 armado: GDT/IDT/PIC do kernel ativos antes do EBS.");
  }
  update_system_runtime_platform_status();
  return rc;
}

int x64_kernel_manual_prepare_hyperv_synic(void) {
  int rc = 0;

  if (!handoff_boot_services_active()) {
    update_system_runtime_platform_status();
    return 0;
  }
  if (!x64_platform_tables_active() && !x64_platform_tables_bridge_active()) {
    update_system_runtime_platform_status();
    return 0;
  }
  if (!vmbus_runtime_hypercall_prepared()) {
    update_system_runtime_platform_status();
    return 0;
  }

  rc = vmbus_runtime_prepare_synic();
  if (rc == 0) {
    klog(KLOG_INFO,
         "[hyperv] SynIC preparado em passo manual/controlado; conexao do VMBus segue desativada.");
    update_system_runtime_platform_status();
    return 1;
  }

  update_system_runtime_platform_status();
  return -1;
}

int x64_kernel_manual_try_exit_boot_services(void) {
  int exit_done_before = g_exit_boot_services_done;
  EFI_STATUS_K exit_status_before = g_exit_boot_services_status;
  struct x64_hyperv_runtime_coordinator_ops ops =
      kernel_hyperv_runtime_coordinator_ops();
  int rc = 0;

  maybe_exit_boot_services_after_native_runtime();
  if (g_exit_boot_services_done != exit_done_before) {
    rc = x64_hyperv_runtime_poll_promotions(&g_input_runtime, &ops);
    update_system_runtime_platform_status();
    return rc < 0 ? -1 : 1;
  }
  if (g_exit_boot_services_status != exit_status_before &&
      g_exit_boot_services_status != EFI_SUCCESS_K) {
    update_system_runtime_platform_status();
    return -1;
  }
  update_system_runtime_platform_status();
  return 0;
}

int x64_kernel_manual_native_runtime_step(void) {
  struct x64_hyperv_runtime_coordinator_ops ops =
      kernel_hyperv_runtime_coordinator_ops();
  struct x64_native_runtime_gate_status gate;
  int changed = 0;
  int failed = 0;
  int prepared_transition = 0;
  int exit_done_before = g_exit_boot_services_done;
  int rc = 0;

  x64_native_runtime_gate_eval(g_h, &g_input_runtime,
                               g_exit_boot_services_attempted,
                               g_exit_boot_services_done,
                               g_exit_boot_services_status, &gate);
  if (gate.gate == SYSTEM_EXIT_BOOT_SERVICES_GATE_WAIT_INPUT &&
      !handoff_boot_services_active()) {
    rc = x64_kernel_manual_prepare_hyperv_input();
    if (rc > 0) {
      changed = 1;
      prepared_transition = 1;
    } else if (rc < 0) {
      failed = 1;
    }
  } else if (gate.gate == SYSTEM_EXIT_BOOT_SERVICES_GATE_WAIT_STORAGE_FIRMWARE) {
    rc = x64_kernel_manual_prepare_hyperv_storage();
    if (rc > 0) {
      changed = 1;
      prepared_transition = 1;
    } else if (rc < 0) {
      failed = 1;
    }
  }
  if (!prepared_transition) {
    rc = x64_kernel_manual_try_exit_boot_services();
    if (rc > 0) {
      changed = 1;
    } else if (rc < 0) {
      failed = 1;
    }
  }
  if (exit_done_before != g_exit_boot_services_done) {
    changed = 1;
  }
  rc = x64_hyperv_runtime_poll_promotions(&g_input_runtime, &ops);
  if (rc > 0) {
    changed = 1;
  } else if (rc < 0) {
    failed = 1;
  }
  update_system_runtime_platform_status();
  if (changed) {
    return 1;
  }
  return failed ? -1 : 0;
}

static void kernel_note_shell_session_ready(void) {
  if (x64_storage_runtime_hyperv_present()) {
    x64_storage_runtime_allow_hyperv_hybrid_prepare(0);
  }
}

static int kernel_allow_hybrid_storage_prepare(void) {
  int allow = !handoff_boot_services_active();
  x64_storage_runtime_allow_hyperv_hybrid_prepare(allow);
  return allow;
}

static struct x64_kernel_shell_runtime_ops kernel_shell_runtime_ops(void) {
  struct x64_kernel_shell_runtime_ops ops;
  ops.mount_root_capyfs = mount_root_CAPYFS;
  ops.ensure_dir_recursive = fs_ensure_dir_recursive;
  ops.write_text_file = fs_write_text_file;
  ops.mount_encrypted_data_volume = mount_encrypted_data_volume;
  ops.persist_active_volume_key_hash = persist_active_volume_key_hash;
  ops.handoff_has_firmware_block_io = handoff_has_firmware_block_io;
  ops.boot_services_active = handoff_boot_services_active;
  ops.after_native_runtime_ready = kernel_shell_after_native_runtime_ready;
  return ops;
}

static EFI_STATUS_K kernel_exit_boot_services(void) {
  EFI_SYSTEM_TABLE_K *st = NULL;
  EFI_BOOT_SERVICES_K *bs = NULL;
  uint64_t map_key = 0;
  uint64_t desc_size = 0;
  uint32_t desc_ver = 0;

  if (!handoff_has_exit_boot_services_contract()) {
    return EFI_INVALID_PARAMETER_K;
  }

  st = (EFI_SYSTEM_TABLE_K *)(uintptr_t)g_h->efi_system_table;
  bs = st ? st->BootServices : NULL;
  if (!st || !bs || !bs->GetMemoryMap || !bs->ExitBootServices) {
    return EFI_INVALID_PARAMETER_K;
  }

  for (uint32_t attempt = 0; attempt < 4u; ++attempt) {
    uint64_t map_size = g_h->memmap_capacity;
    EFI_STATUS_K st_map =
        bs->GetMemoryMap(&map_size, (void *)(uintptr_t)g_h->memmap, &map_key,
                         &desc_size, &desc_ver);
    if (st_map == EFI_BUFFER_TOO_SMALL_K || map_size > g_h->memmap_capacity) {
      return EFI_BUFFER_TOO_SMALL_K;
    }
    if (st_map != EFI_SUCCESS_K) {
      return st_map;
    }

    {
      EFI_STATUS_K st_exit = bs->ExitBootServices(
          (EFI_HANDLE_K)(uintptr_t)g_h->efi_image_handle, map_key);
      if (st_exit == EFI_SUCCESS_K) {
        ((struct boot_handoff *)g_h)->memmap_size = map_size;
        ((struct boot_handoff *)g_h)->memmap_desc_size = (uint32_t)desc_size;
        ((struct boot_handoff *)g_h)->memmap_entries =
            desc_size ? (uint32_t)(map_size / desc_size) : 0;
        ((struct boot_handoff *)g_h)->efi_map_key = map_key;
        ((struct boot_handoff *)g_h)->runtime_flags &=
            ~(BOOT_HANDOFF_RUNTIME_BOOT_SERVICES_ACTIVE |
              BOOT_HANDOFF_RUNTIME_FIRMWARE_INPUT |
              BOOT_HANDOFF_RUNTIME_FIRMWARE_BLOCK_IO |
              BOOT_HANDOFF_RUNTIME_HYBRID_BOOT);
        return EFI_SUCCESS_K;
      }
      if (st_exit != EFI_INVALID_PARAMETER_K) {
        return st_exit;
      }
    }
  }

  return EFI_INVALID_PARAMETER_K;
}

static void maybe_exit_boot_services_after_native_runtime(void) {
  struct x64_native_runtime_gate_status gate;

  if (g_exit_boot_services_done || g_exit_boot_services_attempted) {
    return;
  }
  x64_native_runtime_gate_eval(g_h, &g_input_runtime,
                               g_exit_boot_services_attempted,
                               g_exit_boot_services_done,
                               g_exit_boot_services_status, &gate);
  if (!handoff_boot_services_active()) {
    return;
  }
  if (!x64_native_runtime_gate_is_ready(&gate)) {
    return;
  }

  g_exit_boot_services_attempted = 1;
  klog(KLOG_INFO, "[boot] Tentando ExitBootServices no kernel.");
  g_exit_boot_services_status = kernel_exit_boot_services();
  if (g_exit_boot_services_status != EFI_SUCCESS_K) {
    klog_hex(KLOG_WARN, "[boot] ExitBootServices falhou/adiado. status=",
             g_exit_boot_services_status);
    update_system_runtime_platform_status();
    return;
  }

  g_exit_boot_services_done = 1;
  x64_input_retire_firmware_backend(&g_input_runtime);
  x64_platform_tables_init(1);
  if (g_input_runtime.hyperv_deferred) {
    klog(KLOG_INFO,
         "[hyperv] Teclado VMBus adiado; promocao sera tentada no input loop.");
    x64_input_enable_auto_promotion();
  }
  (void)x64_storage_runtime_try_enable_hyperv_native(
      handoff_boot_services_active(), kernel_allow_hybrid_storage_prepare(),
      klog_print_adapter);
  klog_print_adapter_flush();
  update_system_runtime_platform_status();
  klog(KLOG_INFO, "[boot] ExitBootServices concluido no kernel.");
}

static int prepare_shell_runtime(void) {
  struct x64_kernel_shell_runtime_state state = kernel_shell_runtime_state();
  struct x64_kernel_shell_runtime_io io = kernel_shell_runtime_io();
  struct x64_kernel_shell_runtime_ops ops = kernel_shell_runtime_ops();
  return x64_kernel_prepare_shell_runtime(&state, &io, &ops);
}

/* Activate authenticated user session for shell command dispatch */
static int init_shell_context(const struct user_record *user) {
  struct x64_kernel_shell_runtime_state state = kernel_shell_runtime_state();
  int rc = 0;
  if (prepare_shell_runtime() != 0 || !user || !user->username[0]) {
    return -1;
  }
  rc = x64_kernel_begin_shell_session(&state, user);
  if (rc == 0) {
    kernel_note_shell_session_ready();
  }
  return rc;
}

/* x64 relocation-safe wrappers for external callbacks used via function pointers.
 * Keeping callbacks inside this TU avoids absolute address materialization for
 * external symbols in the login runtime ops table. */
static void login_session_reset(struct session_context *ctx) {
  session_reset(ctx);
}

static void login_session_set_active(struct session_context *ctx) {
  session_set_active(ctx);
}

static void login_shell_context_init(struct shell_context *ctx,
                                     struct session_context *session,
                                     const struct system_settings *settings) {
  shell_context_init(ctx, session, settings);
}

static int login_system_login(struct session_context *session,
                              const struct system_settings *settings) {
  return system_login(session, settings);
}

static void login_show_splash(const struct system_settings *settings) {
  if (!settings || !settings->splash_enabled) {
    return;
  }
  ui_boot_splash();
}

static const struct user_record *
login_session_user(const struct session_context *ctx) {
  return session_user(ctx);
}

static const char *login_session_cwd(const struct session_context *ctx) {
  return session_cwd(ctx);
}

static int
login_shell_context_should_logout(const struct shell_context *ctx) {
  return shell_context_should_logout(ctx);
}

/* Try to execute command through shell module system.
 * Returns 1 if command was handled, 0 if not found (fallback to inline). */
static int try_shell_command(char *line) {
  return x64_kernel_try_shell_command(&g_shell_ctx, g_shell_initialized, line);
}

static int run_shell_alias(const char *alias_line) {
  return x64_kernel_run_shell_alias(&g_shell_ctx, g_shell_initialized,
                                    alias_line);
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

int kernel_input_getc(char *out_char) {
  if (!out_char)
    return 0;
  for (;;) {
    struct x64_hyperv_runtime_coordinator_ops ops =
        kernel_hyperv_runtime_coordinator_ops();
    int hyperv_was_ready = g_input_runtime.has_hyperv;
    kernel_service_poll();
    (void)x64_hyperv_runtime_poll_promotions(&g_input_runtime, &ops);
    if (x64_input_poll_char(&g_input_runtime, out_char)) {
      return 1;
    }
    if (hyperv_was_ready && !g_input_runtime.has_hyperv &&
        g_input_runtime.hyperv_deferred) {
      klog(KLOG_WARN,
           "[hyperv] Backend VMBus degradado; mantendo fallback atual e reagendando promocao controlada.");
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

/* Adapter: routes print output to the klog ring buffer during the boot
 * splash phase so the framebuffer stays clean.  Buffers fragments until
 * a newline, then flushes to klog as a single entry. */
static char g_klog_adapter_buf[256];
static uint32_t g_klog_adapter_len = 0;

static void klog_print_adapter_flush(void) {
  if (g_klog_adapter_len == 0) {
    return;
  }
  g_klog_adapter_buf[g_klog_adapter_len] = '\0';
  klog(KLOG_INFO, g_klog_adapter_buf);
  g_klog_adapter_len = 0;
}

static void klog_print_adapter(const char *s) {
  if (!s) {
    return;
  }
  while (*s) {
    if (*s == '\n') {
      klog_print_adapter_flush();
      ++s;
      continue;
    }
    if (g_klog_adapter_len < sizeof(g_klog_adapter_buf) - 1) {
      g_klog_adapter_buf[g_klog_adapter_len++] = *s;
    } else {
      klog_print_adapter_flush();
      g_klog_adapter_buf[g_klog_adapter_len++] = *s;
    }
    ++s;
  }
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
  g_con.origin_y = 0;
  g_con.cols = (g_con.width / CELL_W);
  g_con.rows = (g_con.height > g_con.origin_y)
                   ? ((g_con.height - g_con.origin_y) / CELL_H)
                   : 0;
  g_con.col = 0;
  g_con.row = 0;

  acpi_set_rsdp(h->rsdp);
  acpi_set_uefi_system_table(h->efi_system_table);
  system_platform_apply_theme("capyos");

  /* --- Boot splash with live progress bar -------------------------------- */
  {
    struct boot_ui_io bui;
    bui.screen_w = g_con.width;
    bui.screen_h = g_con.height;
    bui.splash_bg = g_theme_splash_bg;
    bui.splash_icon = g_theme_splash_icon;
    bui.splash_bar_border = g_theme_splash_bar_border;
    bui.splash_bar_bg = g_theme_splash_bar_bg;
    bui.splash_bar_fill = g_theme_splash_bar_fill;
    bui.text_fg = 0x00CCCCCC;
    bui.text_muted_fg = 0x0090A8A0;
    bui.console_bg = g_con.bg;
    bui.fill_rect = fbcon_fill_rect_px;
    bui.putch_at = fbcon_putch_at;
    bui.draw_icon = ui_draw_capyos_icon;
    boot_ui_init(&bui);
  }
  fbcon_set_visual_muted(1);
  boot_ui_splash_begin();

  struct boot_warnings g_boot_warnings;
  boot_warnings_init(&g_boot_warnings);

  /* Stage 1/8: Platform tables */
  boot_ui_splash_set_status("Initializing platform...");
  boot_ui_splash_advance(1, 8);
  x64_platform_tables_init(!handoff_boot_services_active());

  /* Stage 2/8: Core services */
  boot_ui_splash_set_status("Starting core services...");
  boot_ui_splash_advance(2, 8);
  kcon_init();
  kinit();
  service_manager_init();

  /* Stage 3/8: Timers */
  boot_ui_splash_set_status("Calibrating timers...");
  boot_ui_splash_advance(3, 8);
  x64_timebase_init();
  x64_platform_timer_init(!handoff_boot_services_active());

  /* Stage 4/8: Keyboard */
  boot_ui_splash_set_status("Configuring keyboard...");
  boot_ui_splash_advance(4, 8);
  keyboard_set_layout_by_name("us");
  {
    char handoff_layout_name[16];
    char handoff_language_name[16];
    local_copy(handoff_layout_name, sizeof(handoff_layout_name), "us");
    local_copy(handoff_language_name, sizeof(handoff_language_name), "en");
    if (handoff_keyboard_layout(handoff_layout_name,
                                sizeof(handoff_layout_name)) == 0) {
      (void)keyboard_set_layout_by_name(handoff_layout_name);
    }
    if (handoff_language(handoff_language_name,
                         sizeof(handoff_language_name)) == 0) {
      /* language loaded silently */
    }
    system_set_boot_defaults(handoff_layout_name, handoff_language_name);
  }
  if (load_handoff_volume_key() == 0) {
    klog(KLOG_INFO, "[security] Volume key provisioned from installer.");
  }

  /* Stage 5/8: Storage */
  boot_ui_splash_set_status("Detecting storage...");
  boot_ui_splash_advance(5, 8);
  if (nvme_init() != 0) {
    klog(KLOG_INFO, "[nvme] No NVMe controller found.");
  }

  /* Stage 6/8: Serial */
  boot_ui_splash_set_status("Initializing serial...");
  boot_ui_splash_advance(6, 8);
  com1_init();
  com1_puts("[COM1] CapyOS 64-bit serial console ready\r\n");

  /* Stage 7/8: Input devices */
  boot_ui_splash_set_status("Detecting input devices...");
  boot_ui_splash_advance(7, 8);
  {
    int is_hyperv = hyperv_detect();
    struct x64_input_probe_result input_probe;
    x64_input_probe_backends(&input_probe, handoff_has_firmware_input(),
                             handoff_boot_services_active(),
                             g_h->efi_system_table, is_hyperv,
                             klog_print_adapter);

    int has_efi = input_probe.has_efi;
    int has_ps2 = input_probe.has_ps2;
    int has_com1 = input_probe.has_com1;
    int has_hyperv = input_probe.has_hyperv_ready;

    g_com1_ready = has_com1 ? 1 : 0;
    g_serial_mirror = has_com1 ? 1 : 0;

    {
      struct x64_input_config input_config;
      input_config.prefer_native = handoff_boot_services_active() ? 0 : 1;
      input_config.has_efi = has_efi;
      input_config.has_ps2 = has_ps2;
      input_config.has_hyperv = has_hyperv;
      input_config.hyperv_deferred = input_probe.has_hyperv_deferred;
      input_config.has_com1 = has_com1;
      input_config.efi_system_table = input_probe.efi_system_table;
      x64_input_runtime_init(&g_input_runtime, &input_config);
    }
    update_system_runtime_platform_status();

    if (!x64_input_has_any(&g_input_runtime)) {
      boot_warnings_add(&g_boot_warnings,
                        "No input device detected (keyboard/serial)");
    }
  }

  /* Stage 8/8: Network */
  boot_ui_splash_set_status("Configuring network...");
  boot_ui_splash_advance(8, 8);
  {
    int shell_runtime_rc;

    /* First boot setup is interactive and uses vga_* wrappers, so the
     * framebuffer must be visible while the shell runtime prepares the
     * persistent volume. Otherwise the setup clears the splash and then waits
     * for input on a blank screen. */
    fbcon_set_visual_muted(0);
    shell_runtime_rc = prepare_shell_runtime();
    if (shell_runtime_rc == 0) {
      int log_flush_rc = klog_persist_flush_default();
      kernel_update_logger_service_status(log_flush_rc);
      (void)service_manager_set_poll(SYSTEM_SERVICE_LOGGER,
                                     kernel_service_poll_logger, NULL);
      (void)service_manager_set_control(SYSTEM_SERVICE_LOGGER,
                                        kernel_service_start_logger,
                                        kernel_service_stop_logger, NULL);
      (void)service_manager_set_poll_interval(SYSTEM_SERVICE_LOGGER, 300u);
      (void)service_manager_set_restart_limit(SYSTEM_SERVICE_LOGGER, 0u);
    } else {
      (void)service_manager_set_state(
          SYSTEM_SERVICE_LOGGER, SYSTEM_SERVICE_STATE_DEGRADED,
          shell_runtime_rc, "filesystem unavailable; ring buffer only");
    }
    fbcon_set_visual_muted(1);

    if (shell_runtime_rc != 0) {
      klog(KLOG_INFO, "[net] Shell runtime unavailable; using defaults.");
      boot_warnings_add(&g_boot_warnings,
                        "Storage runtime unavailable; persistence may fail");
    } else if (!x64_storage_runtime_has_device()) {
      boot_warnings_add(&g_boot_warnings,
                        "No validated storage backend detected");
    }
    struct network_bootstrap_io net_io;
    net_io.print = klog_print_adapter;
    net_io.print_dec_u32 = fbcon_print_dec_u32;
    net_io.print_hex16 = fbcon_print_hex16;
    net_io.print_ipv4 = fbcon_print_ipv4;
    net_io.print_mac = fbcon_print_mac;
    net_io.putc = fbcon_putc;
    network_bootstrap_run(&net_io, &g_shell_settings);
    g_network_runtime_refresh_enabled = 1;
    (void)service_manager_set_poll(SYSTEM_SERVICE_NETWORKD,
                                   kernel_service_poll_networkd, NULL);
    (void)service_manager_set_control(SYSTEM_SERVICE_NETWORKD,
                                      kernel_service_start_networkd,
                                      kernel_service_stop_networkd, NULL);
    (void)service_manager_set_poll_interval(SYSTEM_SERVICE_NETWORKD, 10u);
    (void)service_manager_set_restart_limit(SYSTEM_SERVICE_NETWORKD, 3u);
    kernel_maybe_refresh_network_runtime();

    {
      struct net_stack_status net_status = {0};
      kernel_update_network_service_status();
      if (net_stack_status(&net_status) != 0) {
        boot_warnings_add(&g_boot_warnings,
                          "Network status unavailable; continuing offline");
      } else if (!net_status.nic.found) {
        boot_warnings_add(&g_boot_warnings,
                          "No network adapter detected");
      } else if (!net_status.runtime_supported) {
        if (net_status.nic.kind == NET_NIC_KIND_VMXNET3) {
          boot_warnings_add(&g_boot_warnings,
                            "VMXNET3 detected; use VMware E1000 for now");
        } else {
          boot_warnings_add(&g_boot_warnings,
                            "Detected network adapter has no validated driver");
        }
      } else if (!net_status.ready) {
        boot_warnings_add(&g_boot_warnings,
                          "Validated network driver detected but not ready");
      }
    }

    (void)service_manager_set_state(
        SYSTEM_SERVICE_UPDATE_AGENT, SYSTEM_SERVICE_STATE_BLOCKED, -5,
        "update pipeline not implemented yet");
    (void)service_manager_set_dependencies(
        SYSTEM_SERVICE_UPDATE_AGENT,
        (1u << SYSTEM_SERVICE_LOGGER) | (1u << SYSTEM_SERVICE_NETWORKD));
    (void)service_manager_set_restart_limit(SYSTEM_SERVICE_UPDATE_AGENT, 3u);
    (void)service_manager_target_apply(SYSTEM_SERVICE_TARGET_NETWORK);
  }

  /* --- End splash -------------------------------------------------------- */
  boot_ui_splash_end();
  fbcon_set_visual_muted(0);

  /* Restore normal console state for post-splash screens. */
  g_con.col = 0;
  g_con.row = 0;

  /* --- Hardware compatibility warnings ----------------------------------- */
  if (g_boot_warnings.count > 0) {
    int action = boot_ui_show_warnings(&g_boot_warnings, kernel_input_getc);
    (void)klog_persist_flush_default();
    if (action == 1) {
      kernel_halt_forever();
    }
  }

  (void)klog_persist_flush_default();

  /* --- Console ready ------------------------------------------------------ */
  fbcon_fill_rect_px(0, 0, g_con.width, g_con.height, g_con.bg);
  g_con.col = 0;
  g_con.row = 0;

  {
    struct login_runtime_ops login_ops;
    login_ops.has_any_input = x64_input_has_any(&g_input_runtime);
    login_ops.shell_ctx = &g_shell_ctx;
    login_ops.session_ctx = &g_session_ctx;
    login_ops.settings = &g_shell_settings;
    login_ops.prepare_shell_runtime = prepare_shell_runtime;
    login_ops.init_shell_context_user = init_shell_context;
    login_ops.dispatch_shell_command = try_shell_command;
    login_ops.run_shell_alias = run_shell_alias;
    login_ops.is_equal = streq;
    login_ops.readline = kernel_readline;
    login_ops.session_reset = login_session_reset;
    login_ops.session_set_active = login_session_set_active;
    login_ops.shell_context_init = login_shell_context_init;
    login_ops.system_login = login_system_login;
    login_ops.session_user = login_session_user;
    login_ops.session_cwd = login_session_cwd;
    login_ops.shell_context_should_logout = login_shell_context_should_logout;
    login_ops.print = fbcon_print;
    login_ops.putc = fbcon_putc;
    login_ops.clear_view = fbcon_clear_view;
    login_ops.show_splash = login_show_splash;
    login_ops.ui_banner = ui_banner;
    login_ops.cmd_info = cmd_info;
    login_ops.service_poll = kernel_service_poll;

    if (login_runtime_run(&login_ops) != 0) {
      kernel_halt_forever();
    }
  }
  kernel_halt_forever();
}
__asm__(".section .note.GNU-stack,\"\",@progbits");
