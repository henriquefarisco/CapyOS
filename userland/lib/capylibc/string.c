/*
 * userland/lib/capylibc/string.c — freestanding <string.h> for ring-3.
 *
 * Defines the subset of the C string/memory API that CapyOS ring-3 code
 * and the decoupled cores it embeds rely on. The CapyBrowser text core
 * (capy-browser-core, Etapa 6 / Slice 6.4) is the first consumer: its
 * src/{url,text,html} call memcpy/memset/memcmp/strlen/strcmp/strcpy.
 *
 * memcpy/memset reuse the audited word-at-a-time cores from
 * include/util/string_ops.h (same code the kernel links in
 * src/arch/x86_64/stubs.c). The string cores live in
 * <capylibc/capy_str_ops.h> so host tests exercise them under non-libc
 * names without colliding with the host C library.
 *
 * BUILD NOTE: this TU is compiled with -fno-builtin
 * -fno-tree-loop-distribute-patterns (see the dedicated Makefile rule).
 * Without it, GCC's loop-idiom recognition can rewrite a core's own loop
 * (e.g. strlen's `while (*p) p++`) into a *call* to that same libc symbol
 * -> infinite recursion. The word-at-a-time memcpy/memset are not affected
 * (they are not recognized as the simple idiom), matching the kernel stub.
 */

#include <string.h>

#include <capylibc/capy_str_ops.h>
#include <util/string_ops.h>

void *memcpy(void *dst, const void *src, size_t n) {
  return capy_word_memcpy(dst, src, n);
}

void *memset(void *dst, int c, size_t n) {
  return capy_word_memset(dst, c, n);
}

void *memmove(void *dst, const void *src, size_t n) {
  return capy_mem_move(dst, src, n);
}

int memcmp(const void *a, const void *b, size_t n) {
  return capy_mem_cmp(a, b, n);
}

size_t strlen(const char *s) { return capy_str_len(s); }

int strcmp(const char *a, const char *b) { return capy_str_cmp(a, b); }

int strncmp(const char *a, const char *b, size_t n) {
  return capy_str_ncmp(a, b, n);
}

char *strcpy(char *dst, const char *src) { return capy_str_cpy(dst, src); }

char *strncpy(char *dst, const char *src, size_t n) {
  return capy_str_ncpy(dst, src, n);
}

char *strchr(const char *s, int c) { return capy_str_chr(s, c); }
