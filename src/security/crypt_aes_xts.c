/*
 * src/security/crypt_aes_xts.c
 *
 * AES-256 cipher core, XTS mode and the block_device adapter exposed
 * through `crypt_init`/`crypt_free`. Carved out of the pre-split
 * monolith `src/security/crypt.c` at the 2026-05-15 refactor so each
 * translation unit stays under the 900-line layout limit.
 *
 * Public symbols owned here:
 *   - `crypt_init` — wrap a lower block device with AES-XTS-256.
 *   - `crypt_free` — tear down the wrapper and wipe key material.
 *
 * Internal scope:
 *   - sbox / rsbox / Rcon AES constants.
 *   - aes_ctx, crypt_device structs.
 *   - aes_key_expand, aes_encrypt_block, aes_decrypt_block plus their
 *     round-state primitives (xtime, multiply, add_round_key,
 *     sub_bytes, shift_rows, mix_columns, inv_*).
 *   - XTS sector helpers (set_block_number, gf_mulx, xts_crypt).
 *   - block_device adapter callbacks (crypt_read_block,
 *     crypt_write_block) plus the static block_device_ops vtable.
 *
 * `crypt_secure_clear` is used pervasively for round-key + tweak
 * wipe; it is shared from `src/security/crypt.c` through the
 * internal header `src/security/internal/crypt_internal.h`.
 */
#include "security/crypt.h"

#include <stddef.h>
#include <stdint.h>

#include "memory/kmem.h"
#include "security/internal/crypt_internal.h"

static inline void dbg_putc(char ch) {
#ifdef UNIT_TEST
  (void)ch;
#else
  __asm__ volatile("outb %0, %1" : : "a"((uint8_t)ch), "Nd"((uint16_t)0xE9));
#endif
}

static void dbg_puts(const char *s) {
  while (s && *s) {
    dbg_putc(*s++);
  }
}

static void dbg_hex32(uint32_t value) {
  static const char hex[] = "0123456789ABCDEF";
  for (int shift = 28; shift >= 0; shift -= 4) {
    dbg_putc(hex[(value >> shift) & 0xFu]);
  }
}

static uint32_t dbg_be32(const uint8_t *buf) {
  if (!buf) {
    return 0;
  }
  return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
         ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
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
    0x83, 0x1d, 0x3a, 0x74, 0xe8, 0xcb, 0x8d, 0x01, 0x02, 0x04, 0x08, 0x10,
    0x20, 0x40, 0x80, 0x1b, 0x36, 0x6c, 0xd8, 0xab, 0x4d, 0x9a, 0x2f, 0x5e,
    0xbc, 0x63, 0xc6, 0x97, 0x35, 0x6a, 0xd4, 0xb3, 0x7d, 0xfa, 0xef, 0xc5,
    0x91, 0x39, 0x72, 0xe4, 0xd3, 0xbd, 0x61, 0xc2, 0x9f, 0x25, 0x4a, 0x94,
    0x33, 0x66, 0xcc, 0x83, 0x1d, 0x3a, 0x74, 0xe8, 0xcb, 0x8d, 0x01, 0x02,
    0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36, 0x6c, 0xd8, 0xab, 0x4d,
    0x9a, 0x2f, 0x5e, 0xbc, 0x63, 0xc6, 0x97, 0x35, 0x6a, 0xd4, 0xb3, 0x7d,
    0xfa, 0xef, 0xc5, 0x91, 0x39, 0x72, 0xe4, 0xd3, 0xbd, 0x61, 0xc2, 0x9f,
    0x25, 0x4a, 0x94, 0x33, 0x66, 0xcc, 0x83, 0x1d, 0x3a, 0x74, 0xe8, 0xcb,
    0x8d, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36, 0x6c,
    0xd8, 0xab, 0x4d, 0x9a, 0x2f, 0x5e, 0xbc, 0x63, 0xc6, 0x97, 0x35, 0x6a,
    0xd4, 0xb3, 0x7d, 0xfa, 0xef, 0xc5, 0x91, 0x39, 0x72, 0xe4, 0xd3, 0xbd,
    0x61, 0xc2, 0x9f, 0x25, 0x4a, 0x94, 0x33, 0x66, 0xcc, 0x83, 0x1d, 0x3a,
    0x74, 0xe8, 0xcb, 0x8d};

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
  crypt_secure_clear(state, sizeof(state));
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
  crypt_secure_clear(state, sizeof(state));
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
    crypt_secure_clear(buffer, sizeof(buffer));
  }
  crypt_secure_clear(tweak, sizeof(tweak));
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
    dbg_puts("[crypt] lower read fail blk=");
    dbg_hex32(block_no);
    dbg_putc('\n');
    crypt_secure_clear(temp, CRYPT_MAX_BLOCK_SIZE);
    return -1;
  }
  if (block_no == 0) {
    dbg_putc('r');
    dbg_putc(' ');
    dbg_hex32(dbg_be32(temp));
    dbg_putc(' ');
    dbg_hex32(dbg_be32(temp + 4));
    dbg_putc('\n');
  }
  xts_crypt(crypt, block_no, temp, (uint8_t *)buffer, 0);
  if (block_no == 0) {
    dbg_putc('D');
    dbg_putc(' ');
    dbg_hex32(dbg_be32((const uint8_t *)buffer));
    dbg_putc(' ');
    dbg_hex32(dbg_be32((const uint8_t *)buffer + 4));
    dbg_putc('\n');
  }
  crypt_secure_clear(temp, CRYPT_MAX_BLOCK_SIZE);
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
  if (block_no == 0) {
    dbg_putc('C');
    dbg_putc(' ');
    dbg_hex32(dbg_be32((const uint8_t *)buffer));
    dbg_putc(' ');
    dbg_hex32(dbg_be32((const uint8_t *)buffer + 4));
    dbg_putc('\n');
  }
  xts_crypt(crypt, block_no, (const uint8_t *)buffer, temp, 1);
  if (block_no == 0) {
    dbg_putc('c');
    dbg_putc(' ');
    dbg_hex32(dbg_be32(temp));
    dbg_putc(' ');
    dbg_hex32(dbg_be32(temp + 4));
    dbg_putc('\n');
  }
  int result = block_device_write(crypt->lower, block_no, temp);
  if (result != 0) {
    dbg_puts("[crypt] lower write fail blk=");
    dbg_hex32(block_no);
    dbg_putc('\n');
  }
  crypt_secure_clear(temp, CRYPT_MAX_BLOCK_SIZE);
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
  crypt_secure_clear((uint8_t *)crypt, sizeof(struct crypt_device));

  kfree(crypt);
}
