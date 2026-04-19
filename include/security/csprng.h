#ifndef SECURITY_CSPRNG_H
#define SECURITY_CSPRNG_H

#include <stdint.h>
#include <stddef.h>

/*
 * Inicializa o CSPRNG.
 */
void csprng_init(void);

/*
 * Adiciona entropia ao pool interno.
 * Deve ser chamado por manipuladores de interrupção (teclado, timer, disco).
 */
void csprng_feed_entropy(uint32_t data);

/*
 * Preenche o buffer com bytes aleatórios criptograficamente seguros.
 */
void csprng_get_bytes(void *buf, size_t len);

/*
 * Retorna um inteiro de 32 bits aleatório.
 */
uint32_t csprng_next_u32(void);

/* Compatibility alias used by newer kernel modules. */
static inline void csprng_fill(void *buf, size_t len) {
  csprng_get_bytes(buf, len);
}

#endif
