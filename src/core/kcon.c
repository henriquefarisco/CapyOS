#include "core/kcon.h"

#include <stdint.h>

#ifdef __x86_64__
extern void fbcon_print(const char *s);
extern void fbcon_putc(char c);
#define KCON_PUTC(c) fbcon_putc(c)
#define KCON_PUTS(s) fbcon_print(s)
#else
#include "drivers/video/vga.h"
#define KCON_PUTC(c) vga_putc(c)
#define KCON_PUTS(s) vga_write(s)
#endif

static int g_kcon_initialized = 0;
static int g_log_level = KLOG_INFO; /* Default: show INFO and above */

void kcon_init(void) { g_kcon_initialized = 1; }

void k_putc(char c) {
  if (g_kcon_initialized) {
    KCON_PUTC(c);
  }
}

void k_puts(const char *s) {
  if (g_kcon_initialized && s) {
    KCON_PUTS(s);
  }
}

void k_hex32(uint32_t val) {
  static const char hex[] = "0123456789ABCDEF";
  char buf[11];
  buf[0] = '0';
  buf[1] = 'x';
  for (int i = 0; i < 8; i++) {
    buf[2 + i] = hex[(val >> (28 - i * 4)) & 0xF];
  }
  buf[10] = '\0';
  k_puts(buf);
}

void k_hex64(uint64_t val) {
  k_hex32((uint32_t)(val >> 32));
  k_hex32((uint32_t)val);
}

void k_dec32(uint32_t val) {
  char buf[12];
  int pos = 0;
  if (val == 0) {
    buf[pos++] = '0';
  } else {
    char rev[12];
    int ri = 0;
    while (val > 0) {
      rev[ri++] = '0' + (val % 10);
      val /= 10;
    }
    while (ri > 0) {
      buf[pos++] = rev[--ri];
    }
  }
  buf[pos] = '\0';
  k_puts(buf);
}

void k_log(int level, const char *prefix, const char *msg) {
  if (level < g_log_level)
    return;
  if (!g_kcon_initialized)
    return;

  switch (level) {
  case KLOG_DEBUG:
    k_puts("[dbg] ");
    break;
  case KLOG_INFO:
    k_puts("[inf] ");
    break;
  case KLOG_WARN:
    k_puts("[wrn] ");
    break;
  case KLOG_ERROR:
    k_puts("[ERR] ");
    break;
  }

  if (prefix) {
    k_puts(prefix);
    k_puts(": ");
  }

  if (msg) {
    k_puts(msg);
  }

  k_putc('\n');
}
