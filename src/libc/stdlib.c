#include "libc/stdlib.h"
#include <stddef.h>
#include <stdint.h>

/* Userspace memory allocation via brk syscall */
static uint64_t heap_current = 0;

static int64_t sys_brk(uint64_t addr) {
  int64_t ret;
  __asm__ volatile(
    "movq $10, %%rax\n"
    "movq %1, %%rdi\n"
    "syscall\n"
    "movq %%rax, %0\n"
    : "=r"(ret)
    : "r"((int64_t)addr)
    : "rax", "rdi", "rcx", "r11", "memory"
  );
  return ret;
}

static void __attribute__((noreturn)) sys_exit(int code) {
  __asm__ volatile(
    "movq $0, %%rax\n"
    "movq %0, %%rdi\n"
    "syscall\n"
    : : "r"((int64_t)code)
    : "rax", "rdi", "rcx", "r11"
  );
  for (;;) __asm__ volatile("hlt");
}

/* Simple bump allocator for userspace.
 * A real implementation would use a free list with coalescing. */

#define ALLOC_HEADER_MAGIC 0xA110CA7E
struct alloc_header {
  uint32_t magic;
  uint32_t size;
};

void *malloc(size_t size) {
  if (size == 0) return NULL;

  if (heap_current == 0) {
    heap_current = (uint64_t)sys_brk(0);
    if (heap_current == 0) return NULL;
  }

  size = (size + 7) & ~7ULL;
  size_t total = sizeof(struct alloc_header) + size;

  uint64_t new_brk = heap_current + total;
  int64_t result = sys_brk(new_brk);
  if (result < 0 || (uint64_t)result < new_brk) return NULL;

  struct alloc_header *hdr = (struct alloc_header *)(uintptr_t)heap_current;
  hdr->magic = ALLOC_HEADER_MAGIC;
  hdr->size = (uint32_t)size;

  void *ptr = (void *)(uintptr_t)(heap_current + sizeof(struct alloc_header));
  heap_current = new_brk;
  return ptr;
}

void free(void *ptr) {
  if (!ptr) return;
  /* Bump allocator: free is a no-op.
   * A real implementation would add the block to a free list. */
  struct alloc_header *hdr =
    (struct alloc_header *)((uint8_t *)ptr - sizeof(struct alloc_header));
  if (hdr->magic == ALLOC_HEADER_MAGIC) {
    hdr->magic = 0;
  }
}

void *calloc(size_t nmemb, size_t size) {
  size_t total = nmemb * size;
  if (nmemb != 0 && total / nmemb != size) return NULL;
  void *ptr = malloc(total);
  if (ptr) {
    uint8_t *p = (uint8_t *)ptr;
    for (size_t i = 0; i < total; i++) p[i] = 0;
  }
  return ptr;
}

void *realloc(void *ptr, size_t size) {
  if (!ptr) return malloc(size);
  if (size == 0) { free(ptr); return NULL; }

  struct alloc_header *hdr =
    (struct alloc_header *)((uint8_t *)ptr - sizeof(struct alloc_header));
  if (hdr->magic != ALLOC_HEADER_MAGIC) return NULL;

  if (hdr->size >= (uint32_t)size) return ptr;

  void *new_ptr = malloc(size);
  if (!new_ptr) return NULL;

  uint8_t *s = (uint8_t *)ptr;
  uint8_t *d = (uint8_t *)new_ptr;
  for (uint32_t i = 0; i < hdr->size; i++) d[i] = s[i];

  free(ptr);
  return new_ptr;
}

int atoi(const char *s) {
  int neg = 0, val = 0;
  while (*s == ' ' || *s == '\t') s++;
  if (*s == '-') { neg = 1; s++; }
  else if (*s == '+') s++;
  while (*s >= '0' && *s <= '9') { val = val * 10 + (*s - '0'); s++; }
  return neg ? -val : val;
}

long atol(const char *s) {
  long neg = 0, val = 0;
  while (*s == ' ' || *s == '\t') s++;
  if (*s == '-') { neg = 1; s++; }
  else if (*s == '+') s++;
  while (*s >= '0' && *s <= '9') { val = val * 10 + (*s - '0'); s++; }
  return neg ? -val : val;
}

void exit(int status) {
  sys_exit(status);
}

void abort(void) {
  sys_exit(134);
}
