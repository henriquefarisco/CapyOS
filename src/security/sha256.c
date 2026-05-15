#include "security/sha256.h"

static const uint32_t k256[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

static uint32_t rotr32(uint32_t x, uint32_t n) {
  return (x >> n) | (x << (32u - n));
}

static void sha256_transform(struct sha256_ctx *ctx,
                             const uint8_t block[SHA256_BLOCK_SIZE]) {
  uint32_t w[64];
  uint32_t a;
  uint32_t b;
  uint32_t c;
  uint32_t d;
  uint32_t e;
  uint32_t f;
  uint32_t g;
  uint32_t h;

  for (uint32_t i = 0; i < 16u; ++i) {
    w[i] = ((uint32_t)block[i * 4u] << 24) |
           ((uint32_t)block[i * 4u + 1u] << 16) |
           ((uint32_t)block[i * 4u + 2u] << 8) |
           ((uint32_t)block[i * 4u + 3u]);
  }
  for (uint32_t i = 16u; i < 64u; ++i) {
    uint32_t s0 = rotr32(w[i - 15u], 7u) ^ rotr32(w[i - 15u], 18u) ^
                  (w[i - 15u] >> 3u);
    uint32_t s1 = rotr32(w[i - 2u], 17u) ^ rotr32(w[i - 2u], 19u) ^
                  (w[i - 2u] >> 10u);
    w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
  }

  a = ctx->state[0];
  b = ctx->state[1];
  c = ctx->state[2];
  d = ctx->state[3];
  e = ctx->state[4];
  f = ctx->state[5];
  g = ctx->state[6];
  h = ctx->state[7];

  for (uint32_t i = 0; i < 64u; ++i) {
    uint32_t s1 = rotr32(e, 6u) ^ rotr32(e, 11u) ^ rotr32(e, 25u);
    uint32_t ch = (e & f) ^ ((~e) & g);
    uint32_t t1 = h + s1 + ch + k256[i] + w[i];
    uint32_t s0 = rotr32(a, 2u) ^ rotr32(a, 13u) ^ rotr32(a, 22u);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t t2 = s0 + maj;

    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }

  ctx->state[0] += a;
  ctx->state[1] += b;
  ctx->state[2] += c;
  ctx->state[3] += d;
  ctx->state[4] += e;
  ctx->state[5] += f;
  ctx->state[6] += g;
  ctx->state[7] += h;
}

void sha256_init(struct sha256_ctx *ctx) {
  if (!ctx) return;
  ctx->state[0] = 0x6a09e667u;
  ctx->state[1] = 0xbb67ae85u;
  ctx->state[2] = 0x3c6ef372u;
  ctx->state[3] = 0xa54ff53au;
  ctx->state[4] = 0x510e527fu;
  ctx->state[5] = 0x9b05688cu;
  ctx->state[6] = 0x1f83d9abu;
  ctx->state[7] = 0x5be0cd19u;
  ctx->bitlen = 0u;
  ctx->datalen = 0u;
}

void sha256_update(struct sha256_ctx *ctx, const uint8_t *data, size_t len) {
  if (!ctx || (!data && len > 0u)) return;
  for (size_t i = 0u; i < len; ++i) {
    ctx->data[ctx->datalen++] = data[i];
    if (ctx->datalen == SHA256_BLOCK_SIZE) {
      sha256_transform(ctx, ctx->data);
      ctx->bitlen += 512u;
      ctx->datalen = 0u;
    }
  }
}

void sha256_clear(struct sha256_ctx *ctx) {
  if (!ctx) return;
  volatile uint8_t *p = (volatile uint8_t *)ctx;
  for (size_t i = 0; i < sizeof(*ctx); ++i) {
    p[i] = 0u;
  }
}

void sha256_final(struct sha256_ctx *ctx, uint8_t hash[SHA256_DIGEST_SIZE]) {
  uint32_t i;
  uint64_t total_bits;

  if (!ctx || !hash) return;
  i = ctx->datalen;
  ctx->data[i++] = 0x80u;
  if (i > 56u) {
    while (i < SHA256_BLOCK_SIZE) ctx->data[i++] = 0u;
    sha256_transform(ctx, ctx->data);
    i = 0u;
  }
  while (i < 56u) ctx->data[i++] = 0u;

  total_bits = ctx->bitlen + ((uint64_t)ctx->datalen * 8u);
  for (uint32_t b = 0u; b < 8u; ++b) {
    ctx->data[63u - b] = (uint8_t)(total_bits >> (b * 8u));
  }
  sha256_transform(ctx, ctx->data);

  for (uint32_t j = 0u; j < 8u; ++j) {
    hash[j * 4u] = (uint8_t)(ctx->state[j] >> 24);
    hash[j * 4u + 1u] = (uint8_t)(ctx->state[j] >> 16);
    hash[j * 4u + 2u] = (uint8_t)(ctx->state[j] >> 8);
    hash[j * 4u + 3u] = (uint8_t)(ctx->state[j]);
  }
}

void sha256_hash(const uint8_t *data, size_t len,
                 uint8_t hash[SHA256_DIGEST_SIZE]) {
  struct sha256_ctx ctx;
  sha256_init(&ctx);
  sha256_update(&ctx, data, len);
  sha256_final(&ctx, hash);
  /* The convenience wrapper has no way for the caller to retrieve the
   * intermediate context, so the context's `state[]` (which IS the
   * produced digest) and `data[]` (last padded block) would otherwise
   * linger on the caller's frame. Defensive wipe matches the contract
   * documented for `sha256_clear` and aligns this wrapper with the
   * hygiene applied in CSPRNG/PBKDF2/HMAC paths. */
  sha256_clear(&ctx);
}

void sha256_hex(const uint8_t hash[SHA256_DIGEST_SIZE], char out_hex[65]) {
  static const char digits[] = "0123456789abcdef";
  if (!hash || !out_hex) return;
  for (uint32_t i = 0u; i < SHA256_DIGEST_SIZE; ++i) {
    out_hex[i * 2u] = digits[(hash[i] >> 4) & 0x0fu];
    out_hex[i * 2u + 1u] = digits[hash[i] & 0x0fu];
  }
  out_hex[64] = '\0';
}
