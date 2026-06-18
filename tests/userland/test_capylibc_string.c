/*
 * tests/userland/test_capylibc_string.c — host tests for the CapyOS
 * userland string cores (Etapa 6 / Slice 6.4 prerequisite).
 *
 * Exercises the pure cores in <capylibc/capy_str_ops.h> directly, under
 * their non-libc names, so the host C library's strlen/strcmp/... are not
 * shadowed (the same isolation trick tests/util/test_string_ops.c uses for
 * capy_word_memcpy/memset). The freestanding <string.h> wrappers in
 * userland/lib/capylibc/string.c are thin 1-line delegations to these cores
 * (+ the audited capy_word_* memcpy/memset), so verifying the cores here
 * covers the behaviour that ring-3 binaries and the CapyBrowser text core
 * depend on. Expected values are hard-coded (no libc oracle), because in
 * this build <string.h> may resolve to the userland freestanding header.
 */

#include <stdio.h>

#include <capylibc/capy_str_ops.h>

static int g_runs;
static int g_passes;

#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    g_runs++;                                                                  \
    if (cond) {                                                                \
      g_passes++;                                                              \
    } else {                                                                   \
      printf("  FAIL: %s\n", (msg));                                           \
    }                                                                          \
  } while (0)

int test_capylibc_string_run(void) {
  printf("[test_capylibc_string]\n");
  g_runs = 0;
  g_passes = 0;

  /* capy_str_len */
  CHECK(capy_str_len("") == 0u, "strlen empty");
  CHECK(capy_str_len("hello") == 5u, "strlen hello");

  /* capy_str_cmp */
  CHECK(capy_str_cmp("abc", "abc") == 0, "strcmp equal");
  CHECK(capy_str_cmp("abc", "abd") < 0, "strcmp less");
  CHECK(capy_str_cmp("abd", "abc") > 0, "strcmp greater");
  CHECK(capy_str_cmp("ab", "abc") < 0, "strcmp prefix shorter");

  /* capy_str_ncmp */
  CHECK(capy_str_ncmp("abcX", "abcY", 3u) == 0, "strncmp first 3 equal");
  CHECK(capy_str_ncmp("abcX", "abcY", 4u) != 0, "strncmp 4 differ");
  CHECK(capy_str_ncmp("abc", "abc", 0u) == 0, "strncmp n=0 equal");

  /* capy_str_cpy */
  {
    char buf[16];
    char *r = capy_str_cpy(buf, "hi");
    CHECK(r == buf, "strcpy returns dst");
    CHECK(buf[0] == 'h' && buf[1] == 'i' && buf[2] == '\0', "strcpy content");
  }

  /* capy_str_ncpy: pads remaining bytes with NUL; no NUL when src >= n */
  {
    char nb[6];
    for (int i = 0; i < 6; i++) {
      nb[i] = 'X';
    }
    capy_str_ncpy(nb, "ab", 5u);
    CHECK(nb[0] == 'a' && nb[1] == 'b' && nb[2] == '\0' && nb[3] == '\0' &&
              nb[4] == '\0',
          "strncpy pads with NUL");
    char nb2[4];
    nb2[3] = 'Z';
    capy_str_ncpy(nb2, "abcdef", 3u);
    CHECK(nb2[0] == 'a' && nb2[1] == 'b' && nb2[2] == 'c' && nb2[3] == 'Z',
          "strncpy truncates without NUL");
  }

  /* capy_str_chr */
  {
    const char *s = "a.b.c";
    CHECK(capy_str_chr(s, '.') == s + 1, "strchr finds first match");
    CHECK(capy_str_chr(s, 'z') == NULL, "strchr miss returns NULL");
    CHECK(capy_str_chr(s, '\0') == s + 5, "strchr finds terminator");
  }

  /* capy_mem_cmp */
  CHECK(capy_mem_cmp("abc", "abc", 3u) == 0, "memcmp equal");
  CHECK(capy_mem_cmp("abc", "abd", 3u) < 0, "memcmp less");
  CHECK(capy_mem_cmp("abc", "abd", 2u) == 0, "memcmp partial equal");

  /* capy_mem_move overlap, dst > src (must copy backward) */
  {
    char m[8] = {'0', '1', '2', '3', '4', '5', '6', '7'};
    void *r = capy_mem_move(m + 2, m, 4u); /* -> 0 1 0 1 2 3 6 7 */
    CHECK(r == m + 2, "memmove returns dst");
    CHECK(m[2] == '0' && m[3] == '1' && m[4] == '2' && m[5] == '3',
          "memmove overlap dst>src");
  }
  /* capy_mem_move overlap, dst < src (must copy forward) */
  {
    char m2[8] = {'0', '1', '2', '3', '4', '5', '6', '7'};
    capy_mem_move(m2, m2 + 2, 4u); /* -> 2 3 4 5 4 5 6 7 */
    CHECK(m2[0] == '2' && m2[1] == '3' && m2[2] == '4' && m2[3] == '5',
          "memmove overlap dst<src");
  }
  /* capy_mem_move no-op cases */
  {
    char m3[3] = {'a', 'b', 'c'};
    capy_mem_move(m3, m3, 3u);
    CHECK(m3[0] == 'a' && m3[1] == 'b' && m3[2] == 'c', "memmove dst==src");
    capy_mem_move(m3, m3 + 1, 0u);
    CHECK(m3[0] == 'a' && m3[1] == 'b' && m3[2] == 'c', "memmove n=0");
  }

  printf("  -> %d/%d passed\n", g_passes, g_runs);
  return g_runs - g_passes;
}
