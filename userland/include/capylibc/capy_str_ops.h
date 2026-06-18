#ifndef CAPYLIBC_CAPY_STR_OPS_H
#define CAPYLIBC_CAPY_STR_OPS_H

#include <stddef.h>
#include <stdint.h>

/*
 * Pure string/memory cores for the CapyOS userland C library.
 *
 * These back the freestanding `<string.h>` symbols defined in
 * `userland/lib/capylibc/string.c` (which ring-3 binaries and the
 * decoupled cores they embed — e.g. the CapyBrowser text core — call
 * through the standard names). They live as `static inline` in a header,
 * under non-libc names, for the same reason as `include/util/string_ops.h`
 * on the kernel side:
 *
 *   1. the wrapper in string.c inlines them (zero call overhead);
 *   2. host tests can `#include` and exercise the real logic under the
 *      `capy_str_*` / `capy_mem_*` names (tests/userland/test_capylibc_string.c)
 *      WITHOUT colliding with the host C library's strlen/strcmp/...
 *
 * memcpy/memset are NOT duplicated here: string.c reuses the audited
 * word-at-a-time `capy_word_memcpy`/`capy_word_memset` from
 * `include/util/string_ops.h` (already on the userland include path).
 *
 * Contracts mirror the C standard library: callers own buffer sizing;
 * `capy_str_cpy`/`capy_str_ncpy` do not bounds-check the destination,
 * exactly like strcpy/strncpy. `capy_mem_move` is the only overlap-safe
 * core (memmove); the memcpy path stays non-overlapping like libc.
 */

static inline size_t capy_str_len(const char *s) {
  const char *p = s;
  while (*p) {
    p++;
  }
  return (size_t)(p - s);
}

static inline int capy_str_cmp(const char *a, const char *b) {
  while (*a && (*a == *b)) {
    a++;
    b++;
  }
  return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static inline int capy_str_ncmp(const char *a, const char *b, size_t n) {
  while (n && *a && (*a == *b)) {
    a++;
    b++;
    n--;
  }
  if (n == 0u) {
    return 0;
  }
  return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static inline char *capy_str_cpy(char *dst, const char *src) {
  char *d = dst;
  while ((*d++ = *src++) != '\0') {
    /* copy including the terminating NUL */
  }
  return dst;
}

static inline char *capy_str_ncpy(char *dst, const char *src, size_t n) {
  size_t i = 0u;
  for (; i < n && src[i] != '\0'; i++) {
    dst[i] = src[i];
  }
  for (; i < n; i++) {
    dst[i] = '\0';
  }
  return dst;
}

static inline char *capy_str_chr(const char *s, int c) {
  char ch = (char)c;
  for (;; s++) {
    if (*s == ch) {
      return (char *)s;
    }
    if (*s == '\0') {
      return NULL; /* searching for '\0' returns the terminator above */
    }
  }
}

static inline int capy_mem_cmp(const void *a, const void *b, size_t n) {
  const unsigned char *pa = (const unsigned char *)a;
  const unsigned char *pb = (const unsigned char *)b;
  for (size_t i = 0u; i < n; i++) {
    if (pa[i] != pb[i]) {
      return (int)pa[i] - (int)pb[i];
    }
  }
  return 0;
}

static inline void *capy_mem_move(void *dst, const void *src, size_t n) {
  unsigned char *d = (unsigned char *)dst;
  const unsigned char *s = (const unsigned char *)src;
  if (d == s || n == 0u) {
    return dst;
  }
  if (d < s) {
    for (size_t i = 0u; i < n; i++) {
      d[i] = s[i];
    }
  } else {
    /* ranges overlap with d > s: copy backward so source bytes are read
     * before they are overwritten. */
    size_t i = n;
    while (i-- > 0u) {
      d[i] = s[i];
    }
  }
  return dst;
}

#endif /* CAPYLIBC_CAPY_STR_OPS_H */
