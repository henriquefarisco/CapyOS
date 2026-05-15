#ifndef SECURITY_FE25519_H
#define SECURITY_FE25519_H

/*
 * Field arithmetic em GF(2^255 - 19) — primitiva fundacional
 * compartilhada por X25519 (RFC 7748) e Ed25519 (RFC 8032).
 *
 * Representacao: 5 limbs de 51 bits cada (radix 2^51). Limbs sao
 * uint64_t mas tipicamente carregam < 2^52 entre carries. Operacoes
 * `fe_mul`/`fe_sq` aceitam inputs com limbs ate ~2^60 (margem para
 * empilhamento ate ~16 fe_add sucessivos sem carry).
 *
 * Todas as operacoes sao constant-time em relacao aos valores dos
 * limbs (mas o numero de iteracoes em `fe_invert`/`fe_pow22523` e
 * fixo, nao dependente de input).
 *
 * Wipe hygiene: todas as funcoes que produzem temporarios internos
 * fazem wipe volatile-safe ao retornar. Caller continua responsavel
 * pelos `fe` em seu proprio escopo.
 */

#include <stdint.h>
#include <stddef.h>

typedef struct {
  uint64_t v[5];
} fe;

void fe_zero(fe *r);
void fe_one(fe *r);
void fe_copy(fe *r, const fe *a);

/* r = a + b. Limbs crescem ~2x; chame fe_carry apos varios adds. */
void fe_add(fe *r, const fe *a, const fe *b);

/* r = a - b mod p. Soma 2*p para evitar underflow. */
void fe_sub(fe *r, const fe *a, const fe *b);

/* r = -a mod p. Equivale a 0 - a. */
void fe_neg(fe *r, const fe *a);

/* Propaga carries para garantir todos os limbs < 2^51 + small.
 * Chame 1 vez apos fe_mul/fe_sq se o resultado vai para fe_add
 * empilhada. Chame 3 vezes antes de fe_tobytes. */
void fe_carry(fe *r);

/* r = a * b mod p. Result limbs < 2^51 + 2. */
void fe_mul(fe *r, const fe *a, const fe *b);

/* r = a^2 mod p. */
void fe_sq(fe *r, const fe *a);

/* r = a * b mod p onde b cabe em 32 bits. */
void fe_mul_small(fe *r, const fe *a, uint32_t b);

/* r = a^(p-2) mod p = inverso multiplicativo (a != 0). 0 -> 0. */
void fe_invert(fe *r, const fe *a);

/* r = a^((p-5)/8) mod p = a^(2^252 - 3). Usado em sqrt. */
void fe_pow22523(fe *r, const fe *a);

/* Constant-time conditional swap. swap == 0 ou 1. */
void fe_cswap(uint64_t swap, fe *a, fe *b);

/* Constant-time conditional move. cmov == 0 ou 1. */
void fe_cmov(fe *r, const fe *a, uint64_t cmov);

/* Serializa em 32 bytes little-endian, canonicalizado em [0, p). */
void fe_tobytes(uint8_t out[32], const fe *h);

/* Desserializa 32 bytes little-endian. Mascara bit 255 (RFC 7748). */
void fe_frombytes(fe *h, const uint8_t in[32]);

/* Retorna 1 se o byte 0 de fe_tobytes(h) for impar (sign bit), 0 cc. */
int fe_isnegative(const fe *h);

/* Retorna 1 se h == 0 (apos canonicalizacao), 0 cc. Constant-time. */
int fe_iszero(const fe *h);

/* Compara fe canonicalizado em constant-time. Retorna 0 se a == b, 1 cc. */
int fe_notequal(const fe *a, const fe *b);

/* Wipe volatile-safe de fe. Forca compilador a nao otimizar. */
void fe_wipe(fe *r);

#endif /* SECURITY_FE25519_H */
