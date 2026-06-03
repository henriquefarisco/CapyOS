#ifndef UTIL_STRING_OPS_H
#define UTIL_STRING_OPS_H

#include <stddef.h>
#include <stdint.h>

/*
 * Word-at-a-time memset/memcpy cores, behavior-identical to the naive
 * byte loops but ~8x fewer store/load iterations on the bulk.
 *
 * These back the freestanding `memset`/`memcpy` in
 * `src/arch/x86_64/stubs.c` (the compiler lowers struct copies, array
 * inits and large zeroing kernel-wide to those symbols). They are
 * `static inline` in a header for two reasons:
 *   1. the call site in stubs.c inlines them, so there is zero extra
 *      call overhead versus an in-place implementation;
 *   2. host tests can `#include` and exercise the real code under a
 *      non-libc name (tests/util/test_string_ops.c) without colliding
 *      with the host C library's memset/memcpy.
 *
 * Alignment safety: every 8-byte access is performed only after the
 * destination (and, for memcpy, the co-aligned source) has been brought
 * to an 8-byte boundary by a byte prologue, so there is no unaligned
 * access and no strict-aliasing/alignment UB. memcpy falls back to a
 * byte copy when source and destination are not mutually 8-aligned
 * (rare; the common kernel buffers are page/struct/block aligned).
 * Neither helper supports overlapping ranges (same contract as the
 * libc memset/memcpy they implement; overlap stays with memmove).
 */

static inline void *capy_word_memset(void *dest, int c, size_t n) {
  uint8_t *d = (uint8_t *)dest;
  uint8_t b = (uint8_t)c;
  if (n >= 8u) {
    uint64_t fill = (uint64_t)b;
    fill |= fill << 8;
    fill |= fill << 16;
    fill |= fill << 32;
    while (((uintptr_t)d & 7u) != 0u) {
      *d++ = b;
      n--;
    }
    while (n >= 8u) {
      *(uint64_t *)d = fill;
      d += 8;
      n -= 8u;
    }
  }
  while (n-- > 0u) {
    *d++ = b;
  }
  return dest;
}

static inline void *capy_word_memcpy(void *dest, const void *src, size_t n) {
  uint8_t *d = (uint8_t *)dest;
  const uint8_t *s = (const uint8_t *)src;
  /* Word path only when src and dest share alignment mod 8: the byte
   * prologue then brings both to an 8-byte boundary together. */
  if (n >= 8u && ((((uintptr_t)d ^ (uintptr_t)s) & 7u) == 0u)) {
    while (((uintptr_t)d & 7u) != 0u) {
      *d++ = *s++;
      n--;
    }
    while (n >= 8u) {
      *(uint64_t *)d = *(const uint64_t *)s;
      d += 8;
      s += 8;
      n -= 8u;
    }
  }
  while (n-- > 0u) {
    *d++ = *s++;
  }
  return dest;
}

#endif /* UTIL_STRING_OPS_H */
