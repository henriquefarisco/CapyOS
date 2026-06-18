#ifndef CAPYLIBC_STRING_H
#define CAPYLIBC_STRING_H

/*
 * Freestanding <string.h> for the CapyOS userland (ring-3).
 *
 * CapyOS builds ring-3 code with -ffreestanding, so the toolchain does NOT
 * provide <string.h>; this header (on the -Iuserland/include path) supplies the
 * subset the userland and the decoupled cores it embeds actually use. The
 * implementations live in userland/lib/capylibc/string.c (linked via
 * $(CAPYLIBC_STRING_OBJS)) and delegate to the pure cores in
 * <capylibc/capy_str_ops.h> + <util/string_ops.h> (audited word-at-a-time
 * memcpy/memset). The CapyBrowser text core (capy-browser-core, Etapa 6 /
 * Slice 6.4) is the first consumer: its src/{url,text} use these symbols.
 *
 * HOST-TEST SAFETY: the host unit-test build (-DUNIT_TEST) also carries
 * -Iuserland/include, so this header would otherwise SHADOW the system
 * <string.h> for every existing host test (and drop functions they use, e.g.
 * strstr/memchr). In that build we defer to the real C library via
 * #include_next so the full standard surface is preserved; only the
 * freestanding ring-3 build (no UNIT_TEST) sees the capylibc subset below.
 */

#ifdef UNIT_TEST

#include_next <string.h>

#else

#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);

size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
char *strchr(const char *s, int c);

#endif /* UNIT_TEST */

#endif /* CAPYLIBC_STRING_H */
