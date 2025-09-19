#include <stddef.h>
#include <stdint.h>
#include "kmem.h"

#ifndef KHEAP_SIZE
#define KHEAP_SIZE (256 * 1024)     // 256 KiB (ajuste se quiser)
#endif

#define ALIGN 16

static uint8_t kheap[KHEAP_SIZE] __attribute__((aligned(ALIGN)));
static size_t  bump = 0;

static inline size_t align_up(size_t x, size_t a){
    return (x + (a - 1)) & ~(a - 1);
}

void kinit(void){
    bump = 0;
}

void* kalloc(size_t size){
    if (size == 0) return NULL;
    size = align_up(size, ALIGN);
    size_t next = align_up(bump, ALIGN);
    if (next + size > KHEAP_SIZE) return NULL;   // (OOM = out of memory)
    void* p = &kheap[next];
    bump = next + size;
    return p;                                    // (memória NÃO zerada)
}

void kfree(void* ptr){
    (void)ptr;                                   // (no-op por enquanto)
}

size_t kheap_used(void){ return bump; }
size_t kheap_size(void){ return KHEAP_SIZE; }
