#ifndef UTIL_KSTRING_H
#define UTIL_KSTRING_H
/* Shared kernel string/memory utilities.
 * Replaces duplicated local_copy, streq, cstring_length, memory_zero, etc.
 * across kernel_main.c, kernel_shell_runtime.c, system_init.c and others. */
#include <stddef.h>
#include <stdint.h>

size_t kstrlen(const char *s);
void kstrcpy(char *dst, size_t dst_size, const char *src);
int kstreq(const char *a, const char *b);
void kmemzero(void *dst, size_t len);
void kmemcpy(void *dst, const void *src, size_t len);
void kmemmove(void *dst, const void *src, size_t len);
int kmemcmp(const void *a, const void *b, size_t len);

void kbuf_append(char *dst, size_t dst_size, const char *src);
void kbuf_append_u32(char *dst, size_t dst_size, uint32_t value);
void kbuf_append_u64(char *dst, size_t dst_size, uint64_t value);
void kbuf_append_yesno(char *dst, size_t dst_size, int value);

#endif /* UTIL_KSTRING_H */
