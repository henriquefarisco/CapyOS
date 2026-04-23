#include "arch/x86_64/kernel_volume_runtime.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/storage_runtime.h"
#include "boot/boot_config.h"
#include "drivers/storage/efi_block.h"
#include "fs/buffer.h"
#include "fs/capyfs.h"
#include "memory/kmem.h"
#include "security/crypt.h"

#define X64_VOLUME_KEY_HASH_HEX_LEN 64
#define X64_VOLUME_KEY_HASH_PATH "/system/volume.key.sha256"

static size_t local_strlen(const char *s) {
  size_t len = 0;
  if (!s) {
    return 0;
  }
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
    while (src[i] && i + 1 < dst_size) {
      dst[i] = src[i];
      ++i;
    }
  }
  dst[i] = '\0';
}

static int streq(const char *a, const char *b) {
  size_t i = 0;
  if (!a || !b) {
    return 0;
  }
  while (a[i] && b[i]) {
    if (a[i] != b[i]) {
      return 0;
    }
    ++i;
  }
  return a[i] == '\0' && b[i] == '\0';
}

static void secure_memzero(void *ptr, size_t len) {
  volatile uint8_t *p = (volatile uint8_t *)ptr;
  while (len--) {
    *p++ = 0;
  }
}

static void io_print(const struct x64_kernel_volume_runtime_io *io,
                     const char *message) {
  if (io && io->print && message) {
    io->print(message);
  }
}

static void io_print_hex(const struct x64_kernel_volume_runtime_io *io,
                         uint64_t value) {
  if (io && io->print_hex) {
    io->print_hex(value);
  }
}

static void io_print_hex64(const struct x64_kernel_volume_runtime_io *io,
                           uint64_t value) {
  if (io && io->print_hex64) {
    io->print_hex64(value);
  }
}

static void io_print_dec_u32(const struct x64_kernel_volume_runtime_io *io,
                             uint32_t value) {
  if (io && io->print_dec_u32) {
    io->print_dec_u32(value);
  }
}

static void io_putc(const struct x64_kernel_volume_runtime_io *io, char ch) {
  if (io && io->putc) {
    io->putc(ch);
  }
}

static inline void dbg_putc(char ch) {
  __asm__ volatile("outb %0, %1" : : "a"((uint8_t)ch), "Nd"((uint16_t)0xE9));
}

static void dbg_puts(const char *s) {
  while (s && *s) {
    dbg_putc(*s++);
  }
}

static void dbg_hex32(uint32_t value) {
  static const char hex[] = "0123456789ABCDEF";
  for (int shift = 28; shift >= 0; shift -= 4) {
    dbg_putc(hex[(value >> shift) & 0xFu]);
  }
}

static uint32_t dbg_be32_local(const uint8_t *buf) {
  if (!buf) {
    return 0;
  }
  return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
         ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
}

static size_t io_readline(const struct x64_kernel_volume_runtime_io *io,
                          char *buf, size_t maxlen, int mask) {
  if (!io || !io->readline) {
    return 0;
  }
  return io->readline(buf, maxlen, mask);
}

static void io_print_active_efi_runtime_trace(
    const struct x64_kernel_volume_runtime_io *io) {
  if (io && io->print_active_efi_runtime_trace) {
    io->print_active_efi_runtime_trace();
  }
}

static int ascii_is_alnum(char c) {
  if (c >= '0' && c <= '9') {
    return 1;
  }
  if (c >= 'a' && c <= 'z') {
    return 1;
  }
  if (c >= 'A' && c <= 'Z') {
    return 1;
  }
  return 0;
}

static char ascii_upper(char c) {
  if (c >= 'a' && c <= 'z') {
    return (char)(c - 'a' + 'A');
  }
  return c;
}

static int normalize_volume_key_input(const char *input, char *out,
                                      size_t out_size) {
  if (!input || !out || out_size < 2) {
    return -1;
  }

  size_t n = 0;
  for (size_t i = 0; input[i]; ++i) {
    char c = input[i];
    if (c == '-' || c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      continue;
    }
    if (!ascii_is_alnum(c) || n + 1 >= out_size) {
      return -1;
    }
    out[n++] = ascii_upper(c);
  }

  if (n < 4) {
    return -1;
  }
  out[n] = '\0';
  return 0;
}

static void format_volume_key_groups(const char *normalized, char *out,
                                     size_t out_size) {
  size_t si = 0;
  size_t di = 0;

  if (!normalized || !out || out_size == 0) {
    return;
  }

  while (normalized[si] && di + 1 < out_size) {
    if (si > 0 && (si % 4) == 0) {
      if (di + 1 >= out_size) {
        break;
      }
      out[di++] = '-';
    }
    out[di++] = normalized[si++];
  }
  out[di] = '\0';
}

static void bytes_to_hex_local(const uint8_t *src, size_t len, char *dst,
                               size_t dst_size) {
  static const char hex[] = "0123456789abcdef";
  size_t di = 0;

  if (!src || !dst || dst_size == 0) {
    return;
  }

  for (size_t i = 0; i < len && (di + 2) < dst_size; ++i) {
    dst[di++] = hex[(src[i] >> 4) & 0x0F];
    dst[di++] = hex[src[i] & 0x0F];
  }
  dst[di] = '\0';
}

static int hex_value_local(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return 10 + (c - 'a');
  }
  if (c >= 'A' && c <= 'F') {
    return 10 + (c - 'A');
  }
  return -1;
}

static int hex_to_bytes_local(const char *hex, size_t hex_len, uint8_t *dst,
                              size_t dst_len) {
  if (!hex || !dst || hex_len != dst_len * 2) {
    return -1;
  }
  for (size_t i = 0; i < dst_len; ++i) {
    int hi = hex_value_local(hex[i * 2]);
    int lo = hex_value_local(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) {
      return -1;
    }
    dst[i] = (uint8_t)((hi << 4) | lo);
  }
  return 0;
}

