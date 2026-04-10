#ifndef KMEM_H
#define KMEM_H
#include <stddef.h>
#include <stdint.h>

void   kinit(void);                 // inicializa o heap (bump = 0)
void*  kalloc(size_t size);         // aloca blocos (alinhado a 16B); retorna NULL se OOM
void   kfree(void* ptr);            // (no-op por enquanto)
size_t kheap_used(void);            // bytes usados (debug)
size_t kheap_size(void);            // tamanho total do heap

/* Compatibility alias used by newer kernel modules (Linux-style naming). */
static inline void *kmalloc(size_t size) { return kalloc(size); }

#endif
