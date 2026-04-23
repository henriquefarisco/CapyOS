#include <stddef.h>
#include <stdint.h>
#include "memory/kmem.h"
#include "drivers/video/vga.h"

#ifndef KHEAP_SIZE
#define KHEAP_SIZE (16 * 1024 * 1024)    // 16 MiB
#endif

/*
 * Estrutura de cabeçalho de bloco.
 * O heap é uma lista encadeada de blocos.
 * O bit menos significativo de 'size' poderia ser usado para status,
 * mas por clareza usaremos um campo 'flags'.
 */
struct header {
    struct header *next;  // Próximo bloco na lista (físico/lógico)
    size_t size;          // Tamanho DADOS (excluindo header)
    uint8_t is_free;      // 1 = livre, 0 = ocupado
    uint8_t magic;        // 0xCC para detecção de corrupção simples
    uint8_t padding[2];   // Alinhamento para 16 bytes (se header for 8+4+pad)
                          // next(4) + size(4) + flags(4) = 12 -> pad(4) = 16
};

// Garantindo que o header seja alinhado a 16 bytes.
// sizeof(struct header) deve ser 16 em 32-bit.
// next(4) + size(4) + is_free(1) + magic(1) + padding(2) = 12.
// Ops, vamos ajustar para alinhar corretamente.
// O alinhamento solicitado é 16.
// Se usarmos ALIGN 16, o payload começa em header + 16.

#define HEADER_MAGIC 0xCC
#define ALIGN 16

static uint8_t kheap[KHEAP_SIZE] __attribute__((aligned(ALIGN)));
static struct header *free_list = NULL;
static int kmem_oom_warning = 0;

static void memzero_internal(void *ptr, size_t len) {
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    while (len--) {
        *p++ = 0;
    }
}

void kinit(void) {
    // Inicializa o heap com um único bloco gigante livre.
    free_list = (struct header *)kheap;
    free_list->size = KHEAP_SIZE - sizeof(struct header);
    free_list->next = NULL;
    free_list->is_free = 1;
    free_list->magic = HEADER_MAGIC;
    kmem_oom_warning = 0;
    vga_write("[kmem] Heap inicializado (Free List allocator).\n");
}

void *kalloc(size_t size) {
    if (size == 0) return NULL;

    // Alinhamento do tamanho solicitado
    size_t aligned_size = (size + (ALIGN - 1)) & ~(ALIGN - 1);

    struct header *curr = free_list;

    while (curr) {
        if (curr->magic != HEADER_MAGIC) {
            vga_write("[kmem] CORRUPCAO DE HEAP DETECTADA!\n");
            return NULL;
        }

        if (curr->is_free && curr->size >= aligned_size) {
            // Encontrou um bloco livre
            // Verifica se podemos dividir o bloco (split)
            if (curr->size >= aligned_size + sizeof(struct header) + ALIGN) {
                // Split
                struct header *new_block = (struct header *)((uint8_t *)curr + sizeof(struct header) + aligned_size);
                new_block->size = curr->size - aligned_size - sizeof(struct header);
                new_block->is_free = 1;
                new_block->magic = HEADER_MAGIC;
                new_block->next = curr->next;

                curr->size = aligned_size;
                curr->next = new_block;
            }

            curr->is_free = 0;
            
            // Zero-initialize memory (Security)
            void *ptr = (void *)((uint8_t *)curr + sizeof(struct header));
            memzero_internal(ptr, curr->size);
            
            return ptr;
        }
        curr = curr->next;
    }

    if (!kmem_oom_warning) {
        kmem_oom_warning = 1;
        vga_write("[kmem] OOM! Falha de alocacao.\n");
    }
    return NULL;
}

void kfree(void *ptr) {
    if (!ptr) return;

    // O ponteiro aponta para os dados. O header está antes.
    struct header *blk = (struct header *)((uint8_t *)ptr - sizeof(struct header));

    if (blk->magic != HEADER_MAGIC) {
        vga_write("[kmem] kfree: Double free ou corrupcao!\n");
        return;
    }

    blk->is_free = 1;

    // Coalesce (fundir blocos livres adjacentes)
    // Precisamos percorrer a lista porque é "singly linked" e simplificada.
    // Otimização: manteríamos prev_ptr se quiséssemos O(1).
    // Mas aqui vamos simplesmente iterar para mergear.
    
    struct header *curr = free_list;
    while (curr && curr->next) {
        if (curr->is_free && curr->next->is_free) {
            // Verifica adjacência física
             if ((uint8_t *)curr + sizeof(struct header) + curr->size == (uint8_t *)curr->next) {
                // Merge
                curr->size += sizeof(struct header) + curr->next->size;
                curr->next = curr->next->next;
                // Não avança 'curr' para tentar mergear novamente com o próximo
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

size_t kheap_size(void) {
    return KHEAP_SIZE;
}
