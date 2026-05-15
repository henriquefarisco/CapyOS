#include "security/blake2b.h"
#include <stddef.h>
#include <stdint.h>

/*
 * BLAKE2b (RFC 7693): implementacao canonica audit-friendly.
 *
 * Layout interno:
 *  - state h[0..7]: 8 uint64_t chaining variables, inicializadas com
 *    IV XOR param_block per §2.5.
 *  - counter t[0..1]: bytes processados ate o momento. t[0] e o low,
 *    t[1] o high.
 *  - finalization flags f[0..1]: f[0] = 0xFF...FF marca ultimo bloco;
 *    f[1] = 0xFF...FF marca ultimo node em tree-mode (nao usado aqui).
 *  - buf[0..127]: input ainda nao comprimido. buflen indica quantos
 *    bytes em buf sao validos.
 *  - outlen: comprimento de saida desejado (1..64).
 *
 * Loop principal: cada bloco de 128 bytes e processado via compress()
 * que aplica 12 rodadas do round function. O ultimo bloco aplica
 * padding zero + finalization flag.
 *
 * Wipe hygiene: caller invoca blake2b_wipe apos blake2b_final para
 * zerar estado sensivel (state, buf, counter).
 */

static const uint64_t BLAKE2B_IV[8] = {
    0x6A09E667F3BCC908ULL, 0xBB67AE8584CAA73BULL,
    0x3C6EF372FE94F82BULL, 0xA54FF53A5F1D36F1ULL,
    0x510E527FADE682D1ULL, 0x9B05688C2B3E6C1FULL,
    0x1F83D9ABFB41BD6BULL, 0x5BE0CD19137E2179ULL,
};

/* Permutation table sigma per RFC 7693 §2.7 */
static const uint8_t BLAKE2B_SIGMA[12][16] = {
    { 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15},
    {14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3},
    {11,  8, 12,  0,  5,  2, 15, 13, 10, 14,  3,  6,  7,  1,  9,  4},
    { 7,  9,  3,  1, 13, 12, 11, 14,  2,  6,  5, 10,  4,  0, 15,  8},
    { 9,  0,  5,  7,  2,  4, 10, 15, 14,  1, 11, 12,  6,  8,  3, 13},
    { 2, 12,  6, 10,  0, 11,  8,  3,  4, 13,  7,  5, 15, 14,  1,  9},
    {12,  5,  1, 15, 14, 13,  4, 10,  0,  7,  6,  3,  9,  2,  8, 11},
    {13, 11,  7, 14, 12,  1,  3,  9,  5,  0, 15,  4,  8,  6,  2, 10},
    { 6, 15, 14,  9, 11,  3,  0,  8, 12,  2, 13,  7,  1,  4, 10,  5},
    {10,  2,  8,  4,  7,  6,  1,  5, 15, 11,  9, 14,  3, 12, 13,  0},
    { 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15},
    {14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3},
};

static uint64_t rotr64(uint64_t x, unsigned n) {
  return (x >> n) | (x << (64u - n));
}

static uint64_t load64_le(const uint8_t *p) {
  return  (uint64_t)p[0]        | ((uint64_t)p[1] << 8)  |
         ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 24) |
         ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) |
         ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
}

static void store64_le(uint8_t *p, uint64_t x) {
  p[0] = (uint8_t)(x       & 0xFFu);
  p[1] = (uint8_t)((x >>  8) & 0xFFu);
  p[2] = (uint8_t)((x >> 16) & 0xFFu);
  p[3] = (uint8_t)((x >> 24) & 0xFFu);
  p[4] = (uint8_t)((x >> 32) & 0xFFu);
  p[5] = (uint8_t)((x >> 40) & 0xFFu);
  p[6] = (uint8_t)((x >> 48) & 0xFFu);
  p[7] = (uint8_t)((x >> 56) & 0xFFu);
}

static void wipe_bytes(void *p, size_t n) {
  volatile uint8_t *vp = (volatile uint8_t *)p;
  while (n--) {
    *vp++ = 0u;
  }
}

/* G mixing function per RFC 7693 §3.1 */
#define BLAKE2B_G(r, i, a, b, c, d) do {                  \
    a = a + b + m[BLAKE2B_SIGMA[r][2 * i + 0]];           \
    d = rotr64(d ^ a, 32);                                \
    c = c + d;                                            \
    b = rotr64(b ^ c, 24);                                \
    a = a + b + m[BLAKE2B_SIGMA[r][2 * i + 1]];           \
    d = rotr64(d ^ a, 16);                                \
    c = c + d;                                            \
    b = rotr64(b ^ c, 63);                                \
  } while (0)

#define BLAKE2B_ROUND(r) do {                             \
    BLAKE2B_G(r, 0, v[ 0], v[ 4], v[ 8], v[12]);          \
    BLAKE2B_G(r, 1, v[ 1], v[ 5], v[ 9], v[13]);          \
    BLAKE2B_G(r, 2, v[ 2], v[ 6], v[10], v[14]);          \
    BLAKE2B_G(r, 3, v[ 3], v[ 7], v[11], v[15]);          \
    BLAKE2B_G(r, 4, v[ 0], v[ 5], v[10], v[15]);          \
    BLAKE2B_G(r, 5, v[ 1], v[ 6], v[11], v[12]);          \
    BLAKE2B_G(r, 6, v[ 2], v[ 7], v[ 8], v[13]);          \
    BLAKE2B_G(r, 7, v[ 3], v[ 4], v[ 9], v[14]);          \
  } while (0)

/*
 * Compression function F per RFC 7693 §3.2.
 *
 * Mistura state h[0..7] com bloco de 128 bytes via 12 rodadas, levando
 * em conta o counter t[0..1] e flags f[0..1].
 */
static void blake2b_compress(struct blake2b_ctx *ctx,
                             const uint8_t block[BLAKE2B_BLOCK_SIZE]) {
  uint64_t m[16];
  uint64_t v[16];
  unsigned i;

  for (i = 0; i < 16u; ++i) {
    m[i] = load64_le(block + 8u * i);
  }
  for (i = 0; i < 8u; ++i) {
    v[i] = ctx->h[i];
    v[i + 8u] = BLAKE2B_IV[i];
  }
  v[12] ^= ctx->t[0];
  v[13] ^= ctx->t[1];
  v[14] ^= ctx->f[0];
  v[15] ^= ctx->f[1];

  BLAKE2B_ROUND(0); BLAKE2B_ROUND(1); BLAKE2B_ROUND(2);  BLAKE2B_ROUND(3);
  BLAKE2B_ROUND(4); BLAKE2B_ROUND(5); BLAKE2B_ROUND(6);  BLAKE2B_ROUND(7);
  BLAKE2B_ROUND(8); BLAKE2B_ROUND(9); BLAKE2B_ROUND(10); BLAKE2B_ROUND(11);

  for (i = 0; i < 8u; ++i) {
    ctx->h[i] = ctx->h[i] ^ v[i] ^ v[i + 8u];
  }
  wipe_bytes(m, sizeof(m));
  wipe_bytes(v, sizeof(v));
}

int blake2b_init(struct blake2b_ctx *ctx, size_t outlen,
                 const uint8_t *key, size_t keylen) {
  if (!ctx) {
    return -1;
  }
  if (outlen == 0u || outlen > BLAKE2B_DIGEST_SIZE) {
    return -1;
  }
  if (keylen > BLAKE2B_KEY_SIZE) {
    return -1;
  }
  if (keylen > 0u && !key) {
    return -1;
  }

  /*
   * Param block per RFC 7693 §2.5 (encoded as 8 LE uint64):
   *  byte 0:    digest_length
   *  byte 1:    key_length
   *  byte 2:    fanout = 1 (sequential)
   *  byte 3:    depth = 1
   *  bytes 4-7: leaf_length = 0
   *  bytes 8-15: node_offset = 0
   *  byte 16:   node_depth = 0
   *  byte 17:   inner_length = 0
   *  bytes 18-31: reserved = 0
   *  bytes 32-47: salt = 0
   *  bytes 48-63: personal = 0
   *
   * h[i] = IV[i] XOR P[i] (where P[i] is the i-th 8-byte chunk of
   * param_block). In sequential mode with no salt/personal, only
   * P[0] is non-zero, so:
   *   h[0] = IV[0] ^ (digest_length | (key_length << 8) | 0x01010000)
   *   h[i] = IV[i] for i=1..7
   */
  unsigned i;
  for (i = 0; i < 8u; ++i) {
    ctx->h[i] = BLAKE2B_IV[i];
  }
  ctx->h[0] ^= ((uint64_t)0x01010000ULL) ^
               ((uint64_t)keylen << 8) ^
               (uint64_t)outlen;
  ctx->t[0] = 0u;
  ctx->t[1] = 0u;
  ctx->f[0] = 0u;
  ctx->f[1] = 0u;
  for (i = 0; i < BLAKE2B_BLOCK_SIZE; ++i) {
    ctx->buf[i] = 0u;
  }
  ctx->buflen = 0u;
  ctx->outlen = outlen;

  if (keylen > 0u) {
    /* Key is padded with zeros to 128 bytes and absorbed as the first
     * block. RFC 7693 §2.9. */
    uint8_t block[BLAKE2B_BLOCK_SIZE];
    for (i = 0; i < keylen; ++i) {
      block[i] = key[i];
    }
    for (i = keylen; i < BLAKE2B_BLOCK_SIZE; ++i) {
      block[i] = 0u;
    }
    blake2b_update(ctx, block, BLAKE2B_BLOCK_SIZE);
    wipe_bytes(block, sizeof(block));
  }
  return 0;
}

void blake2b_update(struct blake2b_ctx *ctx, const uint8_t *in, size_t inlen) {
  if (!ctx || inlen == 0u || !in) {
    return;
  }
  /*
   * Lazy compression: ao iniciar uma iteracao com buffer cheio,
   * comprime imediatamente (sabemos que ha mais dados — nao e o
   * ultimo bloco). Caso contrario, acumula no buffer ate enche-lo.
   */
  while (inlen > 0u) {
    if (ctx->buflen == BLAKE2B_BLOCK_SIZE) {
      ctx->t[0] += BLAKE2B_BLOCK_SIZE;
      if (ctx->t[0] < BLAKE2B_BLOCK_SIZE) {
        ctx->t[1] += 1u;
      }
      blake2b_compress(ctx, ctx->buf);
      ctx->buflen = 0u;
    }
    size_t avail = BLAKE2B_BLOCK_SIZE - ctx->buflen;
    size_t take  = (inlen < avail) ? inlen : avail;
    for (size_t i = 0; i < take; ++i) {
      ctx->buf[ctx->buflen + i] = in[i];
    }
    ctx->buflen += take;
    in    += take;
    inlen -= take;
  }
}

void blake2b_final(struct blake2b_ctx *ctx, uint8_t *out) {
  if (!ctx || !out) {
    return;
  }
  /*
   * Padding: zero-fill restante do buffer; counter += buflen; set
   * f[0] = 0xFF..FF (ultimo bloco); compress; emite out.
   */
  ctx->t[0] += ctx->buflen;
  if (ctx->t[0] < ctx->buflen) {
    ctx->t[1] += 1u;
  }
  for (size_t i = ctx->buflen; i < BLAKE2B_BLOCK_SIZE; ++i) {
    ctx->buf[i] = 0u;
  }
  ctx->f[0] = 0xFFFFFFFFFFFFFFFFULL;
  blake2b_compress(ctx, ctx->buf);

  uint8_t state_bytes[BLAKE2B_DIGEST_SIZE];
  for (size_t i = 0; i < 8u; ++i) {
    store64_le(state_bytes + 8u * i, ctx->h[i]);
  }
  for (size_t i = 0; i < ctx->outlen; ++i) {
    out[i] = state_bytes[i];
  }
  wipe_bytes(state_bytes, sizeof(state_bytes));
}

int blake2b(uint8_t *out, size_t outlen,
            const uint8_t *key, size_t keylen,
            const uint8_t *in, size_t inlen) {
  if (!out) {
    return -1;
  }
  struct blake2b_ctx ctx;
  int rc = blake2b_init(&ctx, outlen, key, keylen);
  if (rc != 0) {
    blake2b_wipe(&ctx);
    return -1;
  }
  blake2b_update(&ctx, in, inlen);
  blake2b_final(&ctx, out);
  blake2b_wipe(&ctx);
  return 0;
}

void blake2b_wipe(struct blake2b_ctx *ctx) {
  if (!ctx) {
    return;
  }
  wipe_bytes(ctx, sizeof(*ctx));
}
