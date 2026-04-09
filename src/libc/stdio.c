#include "libc/stdio.h"
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

/* Userspace printf that uses syscall write to fd 1 */
static int sys_write(int fd, const void *buf, size_t len) {
  int64_t ret;
  __asm__ volatile(
    "movq %1, %%rax\n"
    "movq %2, %%rdi\n"
    "movq %3, %%rsi\n"
    "movq %4, %%rdx\n"
    "syscall\n"
    "movq %%rax, %0\n"
    : "=r"(ret)
    : "i"((int64_t)1), "r"((int64_t)fd), "r"((int64_t)buf), "r"((int64_t)len)
    : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory"
  );
  return (int)ret;
}

int putchar(int c) {
  char ch = (char)c;
  sys_write(1, &ch, 1);
  return c;
}

int puts(const char *s) {
  size_t len = 0;
  while (s[len]) len++;
  sys_write(1, s, len);
  sys_write(1, "\n", 1);
  return 0;
}

static void itoa_buf(int64_t val, char *buf, int *pos, int base, int is_signed) {
  char tmp[24];
  int tp = 0;
  int neg = 0;

  if (is_signed && val < 0) { neg = 1; val = -val; }
  uint64_t uval = (uint64_t)val;

  if (uval == 0) tmp[tp++] = '0';
  else {
    while (uval > 0) {
      int d = (int)(uval % (uint64_t)base);
      tmp[tp++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
      uval /= (uint64_t)base;
    }
  }
  if (neg) buf[(*pos)++] = '-';
  for (int i = tp - 1; i >= 0; i--) buf[(*pos)++] = tmp[i];
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
  int pos = 0;
  int max = (int)size - 1;
  if (max < 0) max = 0;

  while (*fmt && pos < max) {
    if (*fmt != '%') { buf[pos++] = *fmt++; continue; }
    fmt++;
    if (*fmt == '\0') break;

    int long_flag = 0;
    if (*fmt == 'l') { long_flag = 1; fmt++; }
    if (*fmt == 'l') { long_flag = 2; fmt++; }

    switch (*fmt) {
    case 'd': case 'i': {
      int64_t val = (long_flag >= 2) ? va_arg(ap, int64_t) :
                    (long_flag == 1) ? (int64_t)va_arg(ap, long) :
                    (int64_t)va_arg(ap, int);
      char tmp[24]; int tp = 0;
      itoa_buf(val, tmp, &tp, 10, 1);
      for (int i = 0; i < tp && pos < max; i++) buf[pos++] = tmp[i];
      break;
    }
    case 'u': {
      uint64_t val = (long_flag >= 2) ? va_arg(ap, uint64_t) :
                     (long_flag == 1) ? (uint64_t)va_arg(ap, unsigned long) :
                     (uint64_t)va_arg(ap, unsigned int);
      char tmp[24]; int tp = 0;
      itoa_buf((int64_t)val, tmp, &tp, 10, 0);
      for (int i = 0; i < tp && pos < max; i++) buf[pos++] = tmp[i];
      break;
    }
    case 'x': case 'X': {
      uint64_t val = (long_flag >= 2) ? va_arg(ap, uint64_t) :
                     (long_flag == 1) ? (uint64_t)va_arg(ap, unsigned long) :
                     (uint64_t)va_arg(ap, unsigned int);
      char tmp[24]; int tp = 0;
      itoa_buf((int64_t)val, tmp, &tp, 16, 0);
      for (int i = 0; i < tp && pos < max; i++) buf[pos++] = tmp[i];
      break;
    }
    case 'p': {
      uint64_t val = (uint64_t)(uintptr_t)va_arg(ap, void *);
      if (pos + 1 < max) { buf[pos++] = '0'; buf[pos++] = 'x'; }
      char tmp[24]; int tp = 0;
      itoa_buf((int64_t)val, tmp, &tp, 16, 0);
      for (int i = 0; i < tp && pos < max; i++) buf[pos++] = tmp[i];
      break;
    }
    case 's': {
      const char *s = va_arg(ap, const char *);
      if (!s) s = "(null)";
      while (*s && pos < max) buf[pos++] = *s++;
      break;
    }
    case 'c':
      buf[pos++] = (char)va_arg(ap, int);
      break;
    case '%':
      buf[pos++] = '%';
      break;
    default:
      buf[pos++] = '%';
      if (pos < max) buf[pos++] = *fmt;
      break;
    }
    fmt++;
  }
  buf[pos] = '\0';
  return pos;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vsnprintf(buf, size, fmt, ap);
  va_end(ap);
  return r;
}

int printf(const char *fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  int len = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (len > 0) sys_write(1, buf, (size_t)len);
  return len;
}
