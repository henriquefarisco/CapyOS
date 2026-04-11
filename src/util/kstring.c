#include "util/kstring.h"

size_t kstrlen(const char *s) {
  size_t len = 0;
  if (!s) return 0;
  while (s[len]) ++len;
  return len;
}

void kstrcpy(char *dst, size_t dst_size, const char *src) {
  size_t i = 0;
  if (!dst || dst_size == 0) return;
  if (src) {
    while (src[i] && i + 1 < dst_size) {
      dst[i] = src[i];
      ++i;
    }
  }
  dst[i] = '\0';
}

int kstreq(const char *a, const char *b) {
  if (!a || !b) return 0;
  while (*a && *b) {
    if (*a++ != *b++) return 0;
  }
  return *a == *b;
}

void kmemzero(void *dst, size_t len) {
  uint8_t *p = (uint8_t *)dst;
  while (len--) *p++ = 0;
}

void kmemcpy(void *dst, const void *src, size_t len) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  while (len--) *d++ = *s++;
}

void kmemmove(void *dst, const void *src, size_t len) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  if (d < s) {
    while (len--) *d++ = *s++;
  } else if (d > s) {
    d += len;
    s += len;
    while (len--) *--d = *--s;
  }
}

int kmemcmp(const void *a, const void *b, size_t len) {
  const uint8_t *pa = (const uint8_t *)a;
  const uint8_t *pb = (const uint8_t *)b;
  for (size_t i = 0; i < len; i++) {
    if (pa[i] != pb[i]) return (int)pa[i] - (int)pb[i];
  }
  return 0;
}

void kbuf_append(char *dst, size_t dst_size, const char *src) {
  size_t pos = kstrlen(dst);
  size_t idx = 0;
  if (!dst || dst_size == 0 || !src) return;
  while (src[idx] && pos + 1 < dst_size) {
    dst[pos++] = src[idx++];
  }
  dst[pos] = '\0';
}

void kbuf_append_u32(char *dst, size_t dst_size, uint32_t value) {
  char tmp[16];
  size_t len = 0;
  if (value == 0) {
    tmp[len++] = '0';
  } else {
    char rev[16];
    size_t rev_len = 0;
    while (value && rev_len < sizeof(rev)) {
      rev[rev_len++] = (char)('0' + (value % 10u));
      value /= 10u;
    }
    while (rev_len) tmp[len++] = rev[--rev_len];
  }
  tmp[len] = '\0';
  kbuf_append(dst, dst_size, tmp);
}

void kbuf_append_u64(char *dst, size_t dst_size, uint64_t value) {
  char tmp[24];
  size_t len = 0;
  if (value == 0) {
    tmp[len++] = '0';
  } else {
    char rev[24];
    size_t rev_len = 0;
    while (value && rev_len < sizeof(rev)) {
      rev[rev_len++] = (char)('0' + (char)(value % 10u));
      value /= 10u;
    }
    while (rev_len) tmp[len++] = rev[--rev_len];
  }
  tmp[len] = '\0';
  kbuf_append(dst, dst_size, tmp);
}

void kbuf_append_yesno(char *dst, size_t dst_size, int value) {
  kbuf_append(dst, dst_size, value ? "yes" : "no");
}
