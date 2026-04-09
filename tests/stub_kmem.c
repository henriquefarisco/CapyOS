#include <stdlib.h>

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

void kfree(void *ptr) {
    free(ptr);
}

size_t kheap_used(void) {
    return g_used;
}

size_t kheap_size(void) {
    return 0;
}
