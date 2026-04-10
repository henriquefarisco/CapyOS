#include "libc/string.h"
#include <stddef.h>
#include <stdint.h>

void *memset(void *s, int c, size_t n) {
  uint8_t *p = (uint8_t *)s;
  for (size_t i = 0; i < n; i++) p[i] = (uint8_t)c;
  return s;
}

void *memcpy(void *dest, const void *src, size_t n) {
  uint8_t *d = (uint8_t *)dest;
  const uint8_t *s = (const uint8_t *)src;
  for (size_t i = 0; i < n; i++) d[i] = s[i];
  return dest;
}

void *memmove(void *dest, const void *src, size_t n) {
  uint8_t *d = (uint8_t *)dest;
  const uint8_t *s = (const uint8_t *)src;
  if (d < s) {
    for (size_t i = 0; i < n; i++) d[i] = s[i];
  } else if (d > s) {
    for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
  }
  return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
  const uint8_t *a = (const uint8_t *)s1;
  const uint8_t *b = (const uint8_t *)s2;
  for (size_t i = 0; i < n; i++) {
    if (a[i] != b[i]) return (int)a[i] - (int)b[i];
  }
  return 0;
}

size_t strlen(const char *s) {
  size_t n = 0;
  while (s[n]) n++;
  return n;
}

char *strcpy(char *dest, const char *src) {
  char *d = dest;
  while ((*d++ = *src++));
  return dest;
}

char *strncpy(char *dest, const char *src, size_t n) {
  size_t i = 0;
  while (i < n && src[i]) { dest[i] = src[i]; i++; }
  while (i < n) { dest[i] = '\0'; i++; }
  return dest;
}

int strcmp(const char *s1, const char *s2) {
  while (*s1 && *s1 == *s2) { s1++; s2++; }
  return (int)(uint8_t)*s1 - (int)(uint8_t)*s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (s1[i] != s2[i]) return (int)(uint8_t)s1[i] - (int)(uint8_t)s2[i];
    if (s1[i] == '\0') return 0;
  }
  return 0;
}

char *strcat(char *dest, const char *src) {
  char *d = dest;
  while (*d) d++;
  while ((*d++ = *src++));
  return dest;
}

char *strchr(const char *s, int c) {
  while (*s) {
    if (*s == (char)c) return (char *)s;
    s++;
  }
  return (c == '\0') ? (char *)s : NULL;
}

char *strrchr(const char *s, int c) {
  const char *last = NULL;
  while (*s) {
    if (*s == (char)c) last = s;
    s++;
  }
  if (c == '\0') return (char *)s;
  return (char *)last;
}

char *strstr(const char *haystack, const char *needle) {
  if (!*needle) return (char *)haystack;
  size_t nlen = strlen(needle);
  while (*haystack) {
    if (strncmp(haystack, needle, nlen) == 0) return (char *)haystack;
    haystack++;
  }
  return NULL;
}
