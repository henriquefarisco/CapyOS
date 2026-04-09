/* 64-bit kernel memory allocator.
 * Simple free-list allocator with static heap.
 * Adapted from 32-bit version for x86_64 kernel.
 */
#include "core/kcon.h"
#include "memory/kmem.h"
#include <stddef.h>
#include <stdint.h>

#ifndef KHEAP_SIZE
#define KHEAP_SIZE (4 * 1024 * 1024) /* 4 MiB for 64-bit kernel */
#endif

/* Block header structure */
struct header {
  struct header *next; /* Next block in list */
  size_t size;         /* Data size (excluding header) */
  uint8_t is_free;     /* 1 = free, 0 = allocated */
  uint8_t magic;       /* 0xCC for corruption detection */
  uint8_t padding[6];  /* Align to 24 bytes for 64-bit */
};

#define HEADER_MAGIC 0xCC
#define ALIGN 16

static uint8_t kheap[KHEAP_SIZE] __attribute__((aligned(ALIGN)));
static struct header *free_list = NULL;
static int kmem_oom_warning = 0;
static int g_kmem_initialized = 0;

static void memzero_internal(void *ptr, size_t len) {
  volatile uint8_t *p = (volatile uint8_t *)ptr;
  while (len--) {
    *p++ = 0;
  }
}

void kinit(void) {
  free_list = (struct header *)kheap;
  free_list->size = KHEAP_SIZE - sizeof(struct header);
  free_list->next = NULL;
  free_list->is_free = 1;
  free_list->magic = HEADER_MAGIC;
  kmem_oom_warning = 0;
  g_kmem_initialized = 1;
  K_INFO("kmem", "Heap initialized (4 MiB, free-list allocator)");
}

void *kalloc(size_t size) {
  if (size == 0)
    return NULL;
  if (!g_kmem_initialized)
    return NULL;

  /* Align size */
  size_t aligned_size = (size + (ALIGN - 1)) & ~(ALIGN - 1);

  struct header *curr = free_list;

  while (curr) {
    if (curr->magic != HEADER_MAGIC) {
      K_ERROR("kmem", "Heap corruption detected!");
      return NULL;
    }

    if (curr->is_free && curr->size >= aligned_size) {
      /* Check if we can split the block */
      if (curr->size >= aligned_size + sizeof(struct header) + ALIGN) {
        struct header *new_block =
            (struct header *)((uint8_t *)curr + sizeof(struct header) +
                              aligned_size);
        new_block->size = curr->size - aligned_size - sizeof(struct header);
        new_block->is_free = 1;
        new_block->magic = HEADER_MAGIC;
        new_block->next = curr->next;

        curr->size = aligned_size;
        curr->next = new_block;
      }

      curr->is_free = 0;

      /* Zero-initialize for security */
      void *ptr = (void *)((uint8_t *)curr + sizeof(struct header));
      memzero_internal(ptr, curr->size);

      return ptr;
    }
    curr = curr->next;
  }

  if (!kmem_oom_warning) {
    kmem_oom_warning = 1;
    K_ERROR("kmem", "Out of memory!");
  }
  return NULL;
}

void kfree(void *ptr) {
  if (!ptr)
    return;
  if (!g_kmem_initialized)
    return;

  struct header *blk =
      (struct header *)((uint8_t *)ptr - sizeof(struct header));

  if (blk->magic != HEADER_MAGIC) {
    K_ERROR("kmem", "Double free or corruption!");
    return;
  }

  blk->is_free = 1;

  /* Coalesce adjacent free blocks */
  struct header *curr = free_list;
  while (curr && curr->next) {
    if (curr->is_free && curr->next->is_free) {
      /* Check physical adjacency */
      if ((uint8_t *)curr + sizeof(struct header) + curr->size ==
          (uint8_t *)curr->next) {
        curr->size += sizeof(struct header) + curr->next->size;
        curr->next = curr->next->next;
        continue;
      }
    }
    curr = curr->next;
  }
}

size_t kheap_used(void) {
  size_t used = 0;
  struct header *curr = free_list;
  while (curr) {
    if (!curr->is_free) {
      used += curr->size;
    }
    curr = curr->next;
  }
  return used;
}

size_t kheap_size(void) { return KHEAP_SIZE; }

/* Allocate memory with specific alignment (for DMA/XHCI) */
void *kmalloc_aligned(uint64_t size, uint64_t alignment) {
  if (size == 0 || alignment == 0)
    return NULL;

  /* Ensure alignment is power of 2 */
  if (alignment & (alignment - 1))
    return NULL;

  /* Allocate extra space for alignment adjustment and header */
  uint64_t extra = alignment + sizeof(void *);
  void *raw = kalloc((size_t)(size + extra));
  if (!raw)
    return NULL;

  /* Calculate aligned address */
  uintptr_t addr = (uintptr_t)raw + sizeof(void *);
  uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);

  /* Store original pointer just before aligned address */
  ((void **)aligned)[-1] = raw;

  return (void *)aligned;
}

/* Free aligned memory */
void kfree_aligned(void *ptr) {
  if (!ptr)
    return;
  void *raw = ((void **)ptr)[-1];
  kfree(raw);
}
