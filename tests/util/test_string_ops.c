/* Host tests for the word-at-a-time memset/memcpy cores
 * (performance hardening 2026-05-29). Proves capy_word_memset /
 * capy_word_memcpy in util/string_ops.h are BYTE-FOR-BYTE identical to
 * a naive reference across every size, alignment and (for memcpy)
 * source/destination alignment combination, and that neither writes
 * outside the requested range (guard bytes). These cores back the
 * freestanding memcpy/memset in src/arch/x86_64/stubs.c.
 *
 * Distinct names (capy_word_*) avoid colliding with the host libc's
 * memset/memcpy, which the test itself uses for setup/compare. */

#include "util/string_ops.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

static void fail(const char *msg) {
  printf("[string-ops] FAIL: %s\n", msg);
  g_failures++;
}

#define BUFSZ 512u

/* 64-byte aligned so that base+off has actual alignment == off (mod 8),
 * exercising the word/byte-prologue paths deterministically. */
static uint8_t g_dst[BUFSZ] __attribute__((aligned(64)));
static uint8_t g_ref[BUFSZ] __attribute__((aligned(64)));
static uint8_t g_src[BUFSZ] __attribute__((aligned(64)));

static void ref_memset(uint8_t *d, int c, size_t n) {
  for (size_t i = 0; i < n; i++) d[i] = (uint8_t)c;
}

static void ref_memcpy(uint8_t *d, const uint8_t *s, size_t n) {
  for (size_t i = 0; i < n; i++) d[i] = s[i];
}

static const size_t k_sizes[] = {0u,  1u,  2u,  3u,  4u,   5u,   6u,  7u,
                                 8u,  9u,  15u, 16u, 17u,  23u,  24u, 31u,
                                 32u, 33u, 63u, 64u, 100u, 255u, 256u};
#define N_SIZES (sizeof(k_sizes) / sizeof(k_sizes[0]))

static void test_memset_equiv(void) {
  const int cs[] = {0x00, 0xFF, 0x5A, 0x01};
  for (size_t si = 0; si < N_SIZES; si++) {
    size_t n = k_sizes[si];
    for (unsigned off = 0; off < 8u; off++) {
      for (size_t ci = 0; ci < sizeof(cs) / sizeof(cs[0]); ci++) {
        void *r;
        memset(g_dst, 0xCC, BUFSZ); /* sentinel incl. guard bytes */
        memset(g_ref, 0xCC, BUFSZ);
        r = capy_word_memset(g_dst + off, cs[ci], n);
        ref_memset(g_ref + off, cs[ci], n);
        if (r != g_dst + off) fail("memset must return dest");
        if (memcmp(g_dst, g_ref, BUFSZ) != 0)
          fail("memset content or guard byte mismatch");
      }
    }
  }
}

static void test_memcpy_equiv(void) {
  for (size_t i = 0; i < BUFSZ; i++) g_src[i] = (uint8_t)(i * 7u + 3u);
  for (size_t si = 0; si < N_SIZES; si++) {
    size_t n = k_sizes[si];
    for (unsigned doff = 0; doff < 8u; doff++) {
      for (unsigned soff = 0; soff < 8u; soff++) {
        void *r;
        memset(g_dst, 0xCC, BUFSZ);
        memset(g_ref, 0xCC, BUFSZ);
        r = capy_word_memcpy(g_dst + doff, g_src + soff, n);
        ref_memcpy(g_ref + doff, g_src + soff, n);
        if (r != g_dst + doff) fail("memcpy must return dest");
        if (memcmp(g_dst, g_ref, BUFSZ) != 0)
          fail("memcpy content or guard byte mismatch");
      }
    }
  }
}

static void test_memset_returns_dest_zero_len(void) {
  uint8_t x = 0x42;
  if (capy_word_memset(&x, 0, 0u) != &x) fail("memset(n=0) must return dest");
  if (x != 0x42) fail("memset(n=0) must not write");
  if (capy_word_memcpy(&x, &x, 0u) != &x) fail("memcpy(n=0) must return dest");
}

int run_string_ops_tests(void) {
  g_failures = 0;
  test_memset_equiv();
  test_memcpy_equiv();
  test_memset_returns_dest_zero_len();
  if (g_failures == 0) printf("[tests] string_ops OK\n");
  return g_failures;
}
