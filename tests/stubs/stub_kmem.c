#include <stdlib.h>
#include <stdint.h>

#include "memory/kmem.h"

static size_t g_used = 0;

void kinit(void) { }

void *kalloc(size_t size) {
    void *ptr = malloc(size);
    if (ptr) {
        g_used += size;
    }
    return ptr;
}

void *kmalloc_aligned(uint64_t size, uint64_t alignment) {
    void *raw;
    uintptr_t addr;
    uintptr_t aligned;
    if (size == 0 || alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return NULL;
    }
    raw = malloc((size_t)(size + alignment + sizeof(void *)));
    if (!raw) {
        return NULL;
    }
    addr = (uintptr_t)raw + sizeof(void *);
    aligned = (addr + (uintptr_t)alignment - 1u) & ~((uintptr_t)alignment - 1u);
    ((void **)aligned)[-1] = raw;
    return (void *)aligned;
}

void kfree_aligned(void *ptr) {
    if (!ptr) {
        return;
    }
    free(((void **)ptr)[-1]);
}

void kfree(void *ptr) {
    free(ptr);
}

size_t kheap_used(void) {
    return g_used;
}

size_t kheap_size(void) {
    return 0;
}
