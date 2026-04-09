#include "security/crypt.h"

#include <stddef.h>

#include "memory/kmem.h"

static const uint32_t k256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

static uint32_t rotr(uint32_t x, uint32_t n) {
  return (x >> n) | (x << (32 - n));
}

static void secure_clear(uint8_t *ptr, size_t len);

static void sha256_transform(struct sha256_ctx *ctx,
                             const uint8_t data[SHA256_BLOCK_SIZE]) {
  uint32_t m[64];
  for (uint32_t i = 0; i < 16; ++i) {
    m[i] = (uint32_t)data[i * 4 + 0] << 24 | (uint32_t)data[i * 4 + 1] << 16 |
           (uint32_t)data[i * 4 + 2] << 8 | (uint32_t)data[i * 4 + 3];
  }
  for (uint32_t i = 16; i < 64; ++i) {
    uint32_t s0 = rotr(m[i - 15], 7) ^ rotr(m[i - 15], 18) ^ (m[i - 15] >> 3);
    uint32_t s1 = rotr(m[i - 2], 17) ^ rotr(m[i - 2], 19) ^ (m[i - 2] >> 10);
    m[i] = m[i - 16] + s0 + m[i - 7] + s1;
  }

  uint32_t a = ctx->state[0];
  uint32_t b = ctx->state[1];
  uint32_t c = ctx->state[2];
  uint32_t d = ctx->state[3];
  uint32_t e = ctx->state[4];
  uint32_t f = ctx->state[5];
  uint32_t g = ctx->state[6];
  uint32_t h = ctx->state[7];

  for (uint32_t i = 0; i < 64; ++i) {
    uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    uint32_t ch = (e & f) ^ ((~e) & g);
    uint32_t temp1 = h + S1 + ch + k256[i] + m[i];
    uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t temp2 = S0 + maj;

    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
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
  ctx->datalen = 0;
  ctx->bitlen = 0;
  ctx->state[0] = 0x6a09e667;
  ctx->state[1] = 0xbb67ae85;
  ctx->state[2] = 0x3c6ef372;
  ctx->state[3] = 0xa54ff53a;
  ctx->state[4] = 0x510e527f;
  ctx->state[5] = 0x9b05688c;
  ctx->state[6] = 0x1f83d9ab;
  ctx->state[7] = 0x5be0cd19;
}

void sha256_update(struct sha256_ctx *ctx, const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    ctx->data[ctx->datalen++] = data[i];
    if (ctx->datalen == SHA256_BLOCK_SIZE) {
      sha256_transform(ctx, ctx->data);
      ctx->bitlen += SHA256_BLOCK_SIZE * 8;
      ctx->datalen = 0;
    }
  }
}

void sha256_final(struct sha256_ctx *ctx, uint8_t hash[SHA256_DIGEST_SIZE]) {
  uint32_t i = ctx->datalen;

  if (ctx->datalen < 56) {
    ctx->data[i++] = 0x80;
    while (i < 56) {
      ctx->data[i++] = 0x00;
    }
  } else {
    ctx->data[i++] = 0x80;
    while (i < SHA256_BLOCK_SIZE) {
      ctx->data[i++] = 0x00;
    }
    sha256_transform(ctx, ctx->data);
    i = 0;
    while (i < 56) {
      ctx->data[i++] = 0x00;
    }
  }

  ctx->bitlen += ctx->datalen * 8;
  ctx->data[63] = (uint8_t)(ctx->bitlen);
  ctx->data[62] = (uint8_t)(ctx->bitlen >> 8);
  ctx->data[61] = (uint8_t)(ctx->bitlen >> 16);
  ctx->data[60] = (uint8_t)(ctx->bitlen >> 24);
  ctx->data[59] = (uint8_t)(ctx->bitlen >> 32);
  ctx->data[58] = (uint8_t)(ctx->bitlen >> 40);
  ctx->data[57] = (uint8_t)(ctx->bitlen >> 48);
  ctx->data[56] = (uint8_t)(ctx->bitlen >> 56);
  sha256_transform(ctx, ctx->data);

  for (uint32_t j = 0; j < 4; ++j) {
    hash[j] = (uint8_t)(ctx->state[0] >> (24 - j * 8));
    hash[j + 4] = (uint8_t)(ctx->state[1] >> (24 - j * 8));
    hash[j + 8] = (uint8_t)(ctx->state[2] >> (24 - j * 8));
    hash[j + 12] = (uint8_t)(ctx->state[3] >> (24 - j * 8));
    hash[j + 16] = (uint8_t)(ctx->state[4] >> (24 - j * 8));
    hash[j + 20] = (uint8_t)(ctx->state[5] >> (24 - j * 8));
    hash[j + 24] = (uint8_t)(ctx->state[6] >> (24 - j * 8));
    hash[j + 28] = (uint8_t)(ctx->state[7] >> (24 - j * 8));
  }
  secure_clear(ctx->data, sizeof(ctx->data));
}

static void hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *data,
                        size_t data_len, uint8_t out[SHA256_DIGEST_SIZE]) {
  uint8_t kopad[SHA256_BLOCK_SIZE];
  uint8_t kipad[SHA256_BLOCK_SIZE];
  uint8_t key_hash[SHA256_DIGEST_SIZE];

  if (key_len > SHA256_BLOCK_SIZE) {
    struct sha256_ctx key_ctx;
    sha256_init(&key_ctx);
    sha256_update(&key_ctx, key, key_len);
    sha256_final(&key_ctx, key_hash);
    key = key_hash;
    key_len = SHA256_DIGEST_SIZE;
  }

  for (size_t i = 0; i < SHA256_BLOCK_SIZE; ++i) {
    uint8_t kc = (i < key_len) ? key[i] : 0x00;
    kipad[i] = kc ^ 0x36;
    kopad[i] = kc ^ 0x5c;
  }

  struct sha256_ctx ctx;
  sha256_init(&ctx);
  sha256_update(&ctx, kipad, SHA256_BLOCK_SIZE);
  sha256_update(&ctx, data, data_len);
  sha256_final(&ctx, out);

  sha256_init(&ctx);
  sha256_update(&ctx, kopad, SHA256_BLOCK_SIZE);
  sha256_update(&ctx, out, SHA256_DIGEST_SIZE);
  sha256_final(&ctx, out);

  secure_clear(kopad, sizeof(kopad));
  secure_clear(kipad, sizeof(kipad));
  secure_clear(key_hash, sizeof(key_hash));
}

static void pbkdf2_hmac_sha256(const uint8_t *password, size_t password_len,
                               const uint8_t *salt, size_t salt_len,
                               uint32_t iterations, uint8_t *out,
                               size_t out_len) {
  uint32_t blocks = (out_len + SHA256_DIGEST_SIZE - 1) / SHA256_DIGEST_SIZE;
  uint8_t u[SHA256_DIGEST_SIZE];
  uint8_t t[SHA256_DIGEST_SIZE];
  uint8_t salt_block[SHA256_BLOCK_SIZE];

  for (uint32_t i = 1; i <= blocks; ++i) {
    size_t sb_len = salt_len;
    if (salt_len + 4 > sizeof(salt_block)) {
      sb_len = sizeof(salt_block) - 4;
    }
    for (size_t j = 0; j < sb_len; ++j) {
      salt_block[j] = salt[j];
    }
    salt_block[sb_len + 0] = (uint8_t)(i >> 24);
    salt_block[sb_len + 1] = (uint8_t)(i >> 16);
    salt_block[sb_len + 2] = (uint8_t)(i >> 8);
    salt_block[sb_len + 3] = (uint8_t)(i);

    hmac_sha256(password, password_len, salt_block, sb_len + 4, u);
    for (size_t j = 0; j < SHA256_DIGEST_SIZE; ++j) {
      t[j] = u[j];
    }

    for (uint32_t iter = 1; iter < iterations; ++iter) {
      hmac_sha256(password, password_len, u, SHA256_DIGEST_SIZE, u);
      for (size_t j = 0; j < SHA256_DIGEST_SIZE; ++j) {
        t[j] ^= u[j];
      }
    }

    size_t offset = (i - 1) * SHA256_DIGEST_SIZE;
    size_t to_copy = SHA256_DIGEST_SIZE;
    if (offset + to_copy > out_len) {
      to_copy = out_len - offset;
    }
    for (size_t j = 0; j < to_copy; ++j) {
      out[offset + j] = t[j];
    }
  }

  secure_clear(u, sizeof(u));
  secure_clear(t, sizeof(t));
  secure_clear(salt_block, sizeof(salt_block));
}

void crypt_pbkdf2_sha256(const uint8_t *password, size_t password_len,
                         const uint8_t *salt, size_t salt_len,
                         uint32_t iterations, uint8_t *out, size_t out_len) {
  if (!password || !salt || !out || out_len == 0 || iterations == 0) {
    return;
  }
  pbkdf2_hmac_sha256(password, password_len, salt, salt_len, iterations, out,
                     out_len);
}

void crypt_derive_xts_keys(const char *password, const uint8_t *salt,
                           size_t salt_len, uint32_t iterations,
                           uint8_t key1[CRYPT_KEY_SIZE],
                           uint8_t key2[CRYPT_KEY_SIZE]) {
  if (!password || !salt || !key1 || !key2 || iterations == 0) {
    return;
  }
  size_t pass_len = 0;
  while (password[pass_len]) {
    pass_len++;
  }

  uint8_t derived[CRYPT_KEY_SIZE * 2];
  pbkdf2_hmac_sha256((const uint8_t *)password, pass_len, salt, salt_len,
                     iterations, derived, sizeof(derived));

  for (size_t i = 0; i < CRYPT_KEY_SIZE; ++i) {
    key1[i] = derived[i];
    key2[i] = derived[i + CRYPT_KEY_SIZE];
  }

  secure_clear(derived, sizeof(derived));
}

#define AES_BLOCKLEN 16
#define AES_256_KEYLEN 32
#define AES_256_ROUNDKEY_SIZE 240
#define AES_256_NR 14
#define CRYPT_MAX_BLOCK_SIZE 4096

struct aes_ctx {
  uint8_t round_key[AES_256_ROUNDKEY_SIZE];
};

struct crypt_device {
  struct block_device dev;
  struct block_device *lower;
  struct aes_ctx data_ctx;
  struct aes_ctx tweak_ctx;
  // Scratch buffer to avoid large stack allocations (kernel stack is small)
  uint8_t scratch[CRYPT_MAX_BLOCK_SIZE];
};

static const uint8_t sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b,
    0xfe, 0xd7, 0xab, 0x76, 0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0,
    0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0, 0xb7, 0xfd, 0x93, 0x26,
    0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2,
    0xeb, 0x27, 0xb2, 0x75, 0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0,
    0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84, 0x53, 0xd1, 0x00, 0xed,
    0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f,
    0x50, 0x3c, 0x9f, 0xa8, 0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
    0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2, 0xcd, 0x0c, 0x13, 0xec,
    0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14,
    0xde, 0x5e, 0x0b, 0xdb, 0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
    0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79, 0xe7, 0xc8, 0x37, 0x6d,
    0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f,
    0x4b, 0xbd, 0x8b, 0x8a, 0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e,
    0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e, 0xe1, 0xf8, 0x98, 0x11,
    0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f,
    0xb0, 0x54, 0xbb, 0x16};

static const uint8_t rsbox[256] = {
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e,
    0x81, 0xf3, 0xd7, 0xfb, 0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87,
    0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb, 0x54, 0x7b, 0x94, 0x32,
    0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
    0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49,
    0x6d, 0x8b, 0xd1, 0x25, 0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16,
    0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92, 0x6c, 0x70, 0x48, 0x50,
    0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
    0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05,
    0xb8, 0xb3, 0x45, 0x06, 0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02,
    0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b, 0x3a, 0x91, 0x11, 0x41,
    0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
    0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8,
    0x1c, 0x75, 0xdf, 0x6e, 0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89,
    0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b, 0xfc, 0x56, 0x3e, 0x4b,
    0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
    0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59,
    0x27, 0x80, 0xec, 0x5f, 0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d,
    0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef, 0xa0, 0xe0, 0x3b, 0x4d,
    0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
    0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63,
    0x55, 0x21, 0x0c, 0x7d};

static const uint8_t Rcon[255] = {
    0x8d, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36, 0x6c,
    0xd8, 0xab, 0x4d, 0x9a, 0x2f, 0x5e, 0xbc, 0x63, 0xc6, 0x97, 0x35, 0x6a,
    0xd4, 0xb3, 0x7d, 0xfa, 0xef, 0xc5, 0x91, 0x39, 0x72, 0xe4, 0xd3, 0xbd,
    0x61, 0xc2, 0x9f, 0x25, 0x4a, 0x94, 0x33, 0x66, 0xcc, 0x83, 0x1d, 0x3a,
    0x74, 0xe8, 0xcb, 0x8d, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
    0x1b, 0x36, 0x6c, 0xd8, 0xab, 0x4d, 0x9a, 0x2f, 0x5e, 0xbc, 0x63, 0xc6,
    0x97, 0x35, 0x6a, 0xd4, 0xb3, 0x7d, 0xfa, 0xef, 0xc5, 0x91, 0x39, 0x72,
    0xe4, 0xd3, 0xbd, 0x61, 0xc2, 0x9f, 0x25, 0x4a, 0x94, 0x33, 0x66, 0xcc,
    0x83, 0x1d, 0x3a, 0x74, 0xe8, 0xcb, 0x8d};

static uint8_t xtime(uint8_t x) {
  return (uint8_t)((x << 1) ^ ((x >> 7) * 0x1b));
}

static uint8_t multiply(uint8_t x, uint8_t y) {
  uint8_t result = 0;
  while (y) {
    if (y & 1) {
      result ^= x;
    }
    x = xtime(x);
    y >>= 1;
  }
  return result;
}

static void secure_clear(uint8_t *ptr, size_t len) {
  volatile uint8_t *p = ptr;
  while (len--) {
    *p++ = 0;
  }
}

static void aes_key_expand(struct aes_ctx *ctx, const uint8_t *key) {
  uint8_t tempa[4];
  uint8_t *round_key = ctx->round_key;

  for (uint8_t i = 0; i < 8; ++i) {
    round_key[(i * 4) + 0] = key[(i * 4) + 0];
    round_key[(i * 4) + 1] = key[(i * 4) + 1];
    round_key[(i * 4) + 2] = key[(i * 4) + 2];
    round_key[(i * 4) + 3] = key[(i * 4) + 3];
  }

  for (uint32_t i = 8; i < 4 * (AES_256_NR + 1); ++i) {
    tempa[0] = round_key[(i - 1) * 4 + 0];
    tempa[1] = round_key[(i - 1) * 4 + 1];
    tempa[2] = round_key[(i - 1) * 4 + 2];
    tempa[3] = round_key[(i - 1) * 4 + 3];

    if (i % 8 == 0) {
      uint8_t tmp = tempa[0];
      tempa[0] = sbox[tempa[1]] ^ Rcon[i / 8];
      tempa[1] = sbox[tempa[2]];
      tempa[2] = sbox[tempa[3]];
      tempa[3] = sbox[tmp];
    } else if (i % 8 == 4) {
      tempa[0] = sbox[tempa[0]];
      tempa[1] = sbox[tempa[1]];
      tempa[2] = sbox[tempa[2]];
      tempa[3] = sbox[tempa[3]];
    }

    round_key[i * 4 + 0] = round_key[(i - 8) * 4 + 0] ^ tempa[0];
    round_key[i * 4 + 1] = round_key[(i - 8) * 4 + 1] ^ tempa[1];
    round_key[i * 4 + 2] = round_key[(i - 8) * 4 + 2] ^ tempa[2];
    round_key[i * 4 + 3] = round_key[(i - 8) * 4 + 3] ^ tempa[3];
  }
}

static void add_round_key(uint8_t *state, const uint8_t *round_key) {
  for (uint8_t i = 0; i < 16; ++i) {
    state[i] ^= round_key[i];
  }
}

static void sub_bytes(uint8_t *state) {
  for (uint8_t i = 0; i < 16; ++i) {
    state[i] = sbox[state[i]];
  }
}

static void shift_rows(uint8_t *state) {
  uint8_t temp;
  temp = state[1];
  state[1] = state[5];
  state[5] = state[9];
  state[9] = state[13];
  state[13] = temp;

  temp = state[2];
  state[2] = state[10];
  state[10] = temp;
  temp = state[6];
  state[6] = state[14];
  state[14] = temp;

  temp = state[3];
  state[3] = state[15];
  state[15] = state[11];
  state[11] = state[7];
  state[7] = temp;
}

static void mix_columns(uint8_t *state) {
  for (uint8_t i = 0; i < 4; ++i) {
    uint8_t *column = &state[i * 4];
    uint8_t a0 = column[0];
    uint8_t a1 = column[1];
    uint8_t a2 = column[2];
    uint8_t a3 = column[3];

    column[0] = multiply(a0, 2) ^ multiply(a1, 3) ^ a2 ^ a3;
    column[1] = a0 ^ multiply(a1, 2) ^ multiply(a2, 3) ^ a3;
    column[2] = a0 ^ a1 ^ multiply(a2, 2) ^ multiply(a3, 3);
    column[3] = multiply(a0, 3) ^ a1 ^ a2 ^ multiply(a3, 2);
  }
}

static void inv_shift_rows(uint8_t *state) {
  uint8_t temp;
  temp = state[13];
  state[13] = state[9];
  state[9] = state[5];
  state[5] = state[1];
  state[1] = temp;

  temp = state[2];
  state[2] = state[10];
  state[10] = temp;
  temp = state[6];
  state[6] = state[14];
  state[14] = temp;

  temp = state[3];
  state[3] = state[7];
  state[7] = state[11];
  state[11] = state[15];
  state[15] = temp;
}

static void inv_sub_bytes(uint8_t *state) {
  for (uint8_t i = 0; i < 16; ++i) {
    state[i] = rsbox[state[i]];
  }
}

static void inv_mix_columns(uint8_t *state) {
  for (uint8_t i = 0; i < 4; ++i) {
    uint8_t *column = &state[i * 4];
    uint8_t a0 = column[0];
    uint8_t a1 = column[1];
    uint8_t a2 = column[2];
    uint8_t a3 = column[3];

    column[0] = multiply(a0, 0x0e) ^ multiply(a1, 0x0b) ^ multiply(a2, 0x0d) ^
                multiply(a3, 0x09);
    column[1] = multiply(a0, 0x09) ^ multiply(a1, 0x0e) ^ multiply(a2, 0x0b) ^
                multiply(a3, 0x0d);
    column[2] = multiply(a0, 0x0d) ^ multiply(a1, 0x09) ^ multiply(a2, 0x0e) ^
                multiply(a3, 0x0b);
    column[3] = multiply(a0, 0x0b) ^ multiply(a1, 0x0d) ^ multiply(a2, 0x09) ^
                multiply(a3, 0x0e);
  }
}

static void aes_encrypt_block(const struct aes_ctx *ctx,
                              const uint8_t input[16], uint8_t output[16]) {
  uint8_t state[16];
  for (uint8_t i = 0; i < 16; ++i) {
    state[i] = input[i];
  }

  add_round_key(state, ctx->round_key);

  for (uint8_t round = 1; round < AES_256_NR; ++round) {
    sub_bytes(state);
    shift_rows(state);
    mix_columns(state);
    add_round_key(state, &ctx->round_key[round * 16]);
  }

  sub_bytes(state);
  shift_rows(state);
  add_round_key(state, &ctx->round_key[AES_256_NR * 16]);

  for (uint8_t i = 0; i < 16; ++i) {
    output[i] = state[i];
  }
  secure_clear(state, sizeof(state));
}

static void aes_decrypt_block(const struct aes_ctx *ctx,
                              const uint8_t input[16], uint8_t output[16]) {
  uint8_t state[16];
  for (uint8_t i = 0; i < 16; ++i) {
    state[i] = input[i];
  }

  add_round_key(state, &ctx->round_key[AES_256_NR * 16]);

  for (int round = AES_256_NR - 1; round > 0; --round) {
    inv_shift_rows(state);
    inv_sub_bytes(state);
    add_round_key(state, &ctx->round_key[round * 16]);
    inv_mix_columns(state);
  }

  inv_shift_rows(state);
  inv_sub_bytes(state);
  add_round_key(state, ctx->round_key);

  for (uint8_t i = 0; i < 16; ++i) {
    output[i] = state[i];
  }
  secure_clear(state, sizeof(state));
}

static void set_block_number(uint8_t tweak[16], uint32_t block_no) {
  for (uint8_t i = 0; i < 16; ++i) {
    tweak[i] = 0;
  }
  for (uint8_t i = 0; i < 4; ++i) {
    tweak[i] = (uint8_t)(block_no & 0xFF);
    block_no >>= 8;
  }
}

static void gf_mulx(uint8_t tweak[16]) {
  uint8_t carry = 0;
  for (uint8_t i = 0; i < 16; ++i) {
    uint8_t new_carry = (uint8_t)(tweak[i] >> 7);
    tweak[i] = (uint8_t)((tweak[i] << 1) | carry);
    carry = new_carry;
  }
  if (carry) {
    tweak[0] ^= 0x87;
  }
}

static void xts_crypt(const struct crypt_device *crypt, uint32_t block_no,
                      const uint8_t *input, uint8_t *output, int encrypt) {
  uint8_t tweak[16];
  set_block_number(tweak, block_no);
  aes_encrypt_block(&crypt->tweak_ctx, tweak, tweak);

  uint32_t blocks = crypt->dev.block_size / AES_BLOCKLEN;
  for (uint32_t i = 0; i < blocks; ++i) {
    uint8_t buffer[16];
    for (uint8_t j = 0; j < 16; ++j) {
      buffer[j] = input[i * 16 + j] ^ tweak[j];
    }
    if (encrypt) {
      aes_encrypt_block(&crypt->data_ctx, buffer, buffer);
    } else {
      aes_decrypt_block(&crypt->data_ctx, buffer, buffer);
    }
    for (uint8_t j = 0; j < 16; ++j) {
      output[i * 16 + j] = buffer[j] ^ tweak[j];
    }
    gf_mulx(tweak);
    secure_clear(buffer, sizeof(buffer));
  }
  secure_clear(tweak, sizeof(tweak));
}

static int crypt_read_block(void *ctx, uint32_t block_no, void *buffer) {
  struct crypt_device *crypt = (struct crypt_device *)ctx;
  if (!crypt || !buffer) {
    return -1;
  }
  if (crypt->dev.block_size != CRYPT_MAX_BLOCK_SIZE) {
    return -1;
  }
  uint8_t *temp = crypt->scratch;
  if (block_device_read(crypt->lower, block_no, temp) != 0) {
    secure_clear(temp, CRYPT_MAX_BLOCK_SIZE);
    return -1;
  }
  xts_crypt(crypt, block_no, temp, (uint8_t *)buffer, 0);
  secure_clear(temp, CRYPT_MAX_BLOCK_SIZE);
  return 0;
}

static int crypt_write_block(void *ctx, uint32_t block_no, const void *buffer) {
  struct crypt_device *crypt = (struct crypt_device *)ctx;
  if (!crypt || !buffer) {
    return -1;
  }
  if (crypt->dev.block_size != CRYPT_MAX_BLOCK_SIZE) {
    return -1;
  }
  uint8_t *temp = crypt->scratch;
  xts_crypt(crypt, block_no, (const uint8_t *)buffer, temp, 1);
  int result = block_device_write(crypt->lower, block_no, temp);
  secure_clear(temp, CRYPT_MAX_BLOCK_SIZE);
  return result;
}

static struct block_device_ops crypt_ops;
static int crypt_ops_initialized = 0;

static void crypt_init_ops(void) {
  if (crypt_ops_initialized) {
    return;
  }
  crypt_ops.read_block = crypt_read_block;
  crypt_ops.write_block = crypt_write_block;
  crypt_ops_initialized = 1;
}

struct block_device *crypt_init(struct block_device *lower,
                                const uint8_t key1[CRYPT_KEY_SIZE],
                                const uint8_t key2[CRYPT_KEY_SIZE]) {
  crypt_init_ops();
  if (!lower || !key1 || !key2) {
    return NULL;
  }
  if (lower->block_size != CRYPT_MAX_BLOCK_SIZE) {
    return NULL;
  }

  struct crypt_device *crypt =
      (struct crypt_device *)kalloc(sizeof(struct crypt_device));
  if (!crypt) {
    return NULL;
  }
  for (size_t i = 0; i < sizeof(struct crypt_device); ++i) {
    ((uint8_t *)crypt)[i] = 0;
  }

  aes_key_expand(&crypt->data_ctx, key1);
  aes_key_expand(&crypt->tweak_ctx, key2);

  crypt->lower = lower;
  crypt->dev.name = "crypt";
  crypt->dev.block_size = lower->block_size;
  crypt->dev.block_count = lower->block_count;
  crypt->dev.ctx = crypt;
  crypt->dev.ops = &crypt_ops;

  return &crypt->dev;
}

void crypt_free(struct block_device *dev) {
  if (!dev || dev->ops != &crypt_ops) {
    return;
  }
  // dev->ctx points to the start of struct crypt_device
  struct crypt_device *crypt = (struct crypt_device *)dev->ctx;

  // Clear sensitive data before freeing
  secure_clear((uint8_t *)crypt, sizeof(struct crypt_device));

  kfree(crypt);
}

int crypt_constant_time_compare(const uint8_t *a, const uint8_t *b, size_t len) {
  volatile uint8_t diff = 0;
  for (size_t i = 0; i < len; i++) {
    diff |= a[i] ^ b[i];
  }
  return diff == 0 ? 0 : -1;
}

void crypt_hmac_sha256(const uint8_t *key, size_t key_len,
                       const uint8_t *data, size_t data_len,
                       uint8_t out[SHA256_DIGEST_SIZE]) {
  uint8_t k_pad[SHA256_BLOCK_SIZE];
  uint8_t tk[SHA256_DIGEST_SIZE];
  struct sha256_ctx ctx;
  if (key_len > SHA256_BLOCK_SIZE) {
    sha256_init(\&ctx);
    sha256_update(\&ctx, key, key_len);
    sha256_final(\&ctx, tk);
    key = tk;
    key_len = SHA256_DIGEST_SIZE;
  }
  for (size_t i = 0; i < SHA256_BLOCK_SIZE; i++)
    k_pad[i] = (i < key_len ? key[i] : 0) ^ 0x36;
  sha256_init(\&ctx);
  sha256_update(\&ctx, k_pad, SHA256_BLOCK_SIZE);
  sha256_update(\&ctx, data, data_len);
  sha256_final(\&ctx, out);
  for (size_t i = 0; i < SHA256_BLOCK_SIZE; i++)
    k_pad[i] = (i < key_len ? key[i] : 0) ^ 0x5c;
  sha256_init(\&ctx);
  sha256_update(\&ctx, k_pad, SHA256_BLOCK_SIZE);
  sha256_update(\&ctx, out, SHA256_DIGEST_SIZE);
  sha256_final(\&ctx, out);
  secure_clear(k_pad, sizeof(k_pad));
  secure_clear(tk, sizeof(tk));
}
