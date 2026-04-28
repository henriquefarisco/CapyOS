#include "internal/kernel_volume_runtime_internal.h"

static int ascii_is_alnum(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static char ascii_upper(char c) {
  return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

int normalize_volume_key_input(const char *input, char *out, size_t out_size) {
  if (!input || !out || out_size < 2) return -1;
  size_t n = 0;
  for (size_t i = 0; input[i]; ++i) {
    char c = input[i];
    if (c == '-' || c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
    if (!ascii_is_alnum(c) || n + 1 >= out_size) return -1;
    out[n++] = ascii_upper(c);
  }
  if (n < 4) return -1;
  out[n] = '\0';
  return 0;
}

void format_volume_key_groups(const char *normalized, char *out, size_t out_size) {
  size_t si = 0;
  size_t di = 0;
  if (!normalized || !out || out_size == 0) return;
  while (normalized[si] && di + 1 < out_size) {
    if (si > 0 && (si % 4) == 0) {
      if (di + 1 >= out_size) break;
      out[di++] = '-';
    }
    out[di++] = normalized[si++];
  }
  out[di] = '\0';
}

void bytes_to_hex_local(const uint8_t *src, size_t len, char *dst, size_t dst_size) {
  static const char hex[] = "0123456789abcdef";
  size_t di = 0;
  if (!src || !dst || dst_size == 0) return;
  for (size_t i = 0; i < len && (di + 2) < dst_size; ++i) {
    dst[di++] = hex[(src[i] >> 4) & 0x0F];
    dst[di++] = hex[src[i] & 0x0F];
  }
  dst[di] = '\0';
}

static int hex_value_local(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

int hex_to_bytes_local(const char *hex, size_t hex_len, uint8_t *dst, size_t dst_len) {
  if (!hex || !dst || hex_len != dst_len * 2) return -1;
  for (size_t i = 0; i < dst_len; ++i) {
    int hi = hex_value_local(hex[i * 2]);
    int lo = hex_value_local(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) return -1;
    dst[i] = (uint8_t)((hi << 4) | lo);
  }
  return 0;
}
