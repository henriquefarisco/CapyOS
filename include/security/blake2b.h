#ifndef SECURITY_BLAKE2B_H
#define SECURITY_BLAKE2B_H

#include <stddef.h>
#include <stdint.h>

/*
 * BLAKE2b (RFC 7693): hash criptografica de 64 bytes (configuravel ate
 * 1 e ate 64), com suporte opcional a chave (HMAC-like) de ate 64
 * bytes.
 *
 * Audit-friendly, alinhada com a higiene cripto do resto do projeto
 * (wipe volatile-safe, fail-closed em NULL/comprimento invalido). Esta
 * primitiva e a fundacao matematica do Argon2 (RFC 9106) — o hash
 * memory-hard usado pelo CapyOS para password hashing pos-alpha.218
 * (substituindo PBKDF2-SHA256 que e vulneravel a brute-force massivo
 * em GPU/ASIC).
 *
 * Comparacao com SHA-512:
 *  - Mesmo digest size (64 bytes).
 *  - BLAKE2b e ~2x mais rapido em CPUs sem aceleracao SHA dedicada.
 *  - Sem padding extra; permite "salt" e "personal" integrados no
 *    param block (nao usados aqui no slice inicial — placeholders
 *    zerados).
 *  - Suporte nativo a chave: blake2b(out, len, key, klen, msg, mlen)
 *    e equivalente a HMAC-BLAKE2b para keys curtas (<= 64 bytes).
 *
 * Modelo de ameacas (RFC 7693 §2.10):
 *  - Resistencia a colisoes: 2^256 operacoes (digest 64 bytes).
 *  - Resistencia a preimage: 2^512.
 *  - Indistinguibilidade de PRF quando chaveado.
 *  - Resistencia a length-extension (BLAKE2 e Merkle-Damgard com
 *    "finalization flag" — saida final usa flag distinta de blocos
 *    intermediarios).
 *
 * Limites:
 *  - Esta primitiva NAO substitui SHA-256 nos callsites existentes
 *    (HMAC-SHA256, HKDF-SHA256, PBKDF2-SHA256, AES-XTS key derivation).
 *    BLAKE2b e nova fundacao para Argon2 + uso futuro de hash rapido
 *    em paths nao-FIPS.
 *  - Sem suporte a tree-mode/streaming-parallel (RFC §3): no slice
 *    inicial fanout=1, depth=1, leaf_length=0 (sequencial).
 */

#define BLAKE2B_BLOCK_SIZE  128u
#define BLAKE2B_DIGEST_SIZE 64u
#define BLAKE2B_KEY_SIZE    64u

struct blake2b_ctx {
  uint64_t h[8];                       /* hash state (chaining variable) */
  uint64_t t[2];                       /* byte counter (low, high) */
  uint64_t f[2];                       /* finalization flags */
  uint8_t  buf[BLAKE2B_BLOCK_SIZE];    /* unprocessed input */
  size_t   buflen;                     /* bytes in buf (0..128) */
  size_t   outlen;                     /* desired output length */
};

/*
 * Inicializa contexto. Parametros:
 *  - outlen: 1..64 bytes (saida desejada).
 *  - key, keylen: opcional, 0..64 bytes. NULL key permitido se keylen=0.
 *
 * Retorna 0 em sucesso, -1 em parametros invalidos.
 *
 * Wipe hygiene: ctx e zerado antes da inicializacao para garantir
 * estado conhecido.
 */
int blake2b_init(struct blake2b_ctx *ctx, size_t outlen,
                 const uint8_t *key, size_t keylen);

/*
 * Absorve mais dados. Comportamento gracioso em ctx NULL, in NULL com
 * inlen > 0 (no-op), ou inlen == 0 (no-op).
 */
void blake2b_update(struct blake2b_ctx *ctx, const uint8_t *in, size_t inlen);

/*
 * Finaliza e emite digest de ctx->outlen bytes para out. Apos esta
 * chamada o ctx NAO pode ser reutilizado sem nova chamada a
 * blake2b_init.
 *
 * O ctx NAO e zerado automaticamente — caller deve chamar blake2b_wipe
 * apos extrair out se o ctx contiver material sensivel.
 */
void blake2b_final(struct blake2b_ctx *ctx, uint8_t *out);

/*
 * One-shot: equivalente a init + update + final + wipe. Retorna 0 em
 * sucesso, -1 em parametros invalidos.
 */
int blake2b(uint8_t *out, size_t outlen,
            const uint8_t *key, size_t keylen,
            const uint8_t *in, size_t inlen);

/*
 * Zera o contexto via volatile-safe wipe. Use apos extrair digest se
 * o input continha material sensivel (chave, password, segredo).
 */
void blake2b_wipe(struct blake2b_ctx *ctx);

#endif /* SECURITY_BLAKE2B_H */
