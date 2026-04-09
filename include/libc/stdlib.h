#ifndef LIBC_STDLIB_H
#define LIBC_STDLIB_H

#include <stddef.h>

void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);

int atoi(const char *s);
long atol(const char *s);
void exit(int status) __attribute__((noreturn));
void abort(void) __attribute__((noreturn));

#endif /* LIBC_STDLIB_H */
