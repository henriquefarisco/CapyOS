#ifndef KMEM_H
#define KMEM_H
#include <stddef.h>
#include <stdint.h>

/* x86_64 default. Kept public so host budget tests can verify that mandatory
 * desktop surfaces fit without instantiating the kernel allocator. */
#define KHEAP_DEFAULT_SIZE (32u * 1024u * 1024u)

void   kinit(void);                 // inicializa o heap (bump = 0)
void*  kalloc(size_t size);         // aloca blocos (alinhado a 16B); retorna NULL se OOM
void   kfree(void* ptr);            // (no-op por enquanto)
size_t kheap_used(void);            // bytes usados (debug)
size_t kheap_size(void);            // tamanho total do heap

/* Compatibility alias used by newer kernel modules (Linux-style naming). */
static inline void *kmalloc(size_t size) { return kalloc(size); }

#endif
