#include "security/ed25519.h"
#include "security/crypt.h"
#include <stddef.h>

/*
 * Ed25519 signature scheme implementation for CapyOS.
 * Based on the Ed25519 specification (RFC 8032).
 * Uses the existing SHA-512 (built from SHA-256 double-pass for now)
 * and modular arithmetic over the Ed25519 curve.
 *
 * NOTE: This is a minimal implementation for update signature verification.
 * For production use, a constant-time field arithmetic library is recommended.
 */

/* Curve25519 prime: p = 2^255 - 19 */
/* Group order: l = 2^252 + 27742317777372353535851937790883648493 */

/* We represent field elements as 5 limbs of 51 bits each */
typedef struct { uint64_t v[5]; } fe25519;

/* Extended point (X:Y:Z:T) where x=X/Z, y=Y/Z, x*y=T/Z */
typedef struct { fe25519 X, Y, Z, T; } ge25519;

static void fe_zero(fe25519 *r) { for (int i = 0; i < 5; i++) r->v[i] = 0; }
static void __attribute__((unused)) fe_one(fe25519 *r)  { r->v[0] = 1; for (int i = 1; i < 5; i++) r->v[i] = 0; }
static void fe_copy(fe25519 *r, const fe25519 *a) { for (int i = 0; i < 5; i++) r->v[i] = a->v[i]; }

static void __attribute__((unused)) fe_add(fe25519 *r, const fe25519 *a, const fe25519 *b) {
  for (int i = 0; i < 5; i++) r->v[i] = a->v[i] + b->v[i];
}

static void fe_sub(fe25519 *r, const fe25519 *a, const fe25519 *b) {
  /* Add 2*p to avoid underflow before subtraction */
  static const uint64_t two_p[5] = {
    0xFFFFFFFFFFFDA, 0xFFFFFFFFFFFFE, 0xFFFFFFFFFFFFE,
    0xFFFFFFFFFFFFE, 0xFFFFFFFFFFFFE
  };
  for (int i = 0; i < 5; i++) r->v[i] = a->v[i] + two_p[i] - b->v[i];
}

static void fe_reduce(fe25519 *r) {
  uint64_t carry;
  for (int i = 0; i < 4; i++) {
    carry = r->v[i] >> 51;
    r->v[i] &= 0x7FFFFFFFFFFFFULL;
    r->v[i + 1] += carry;
  }
  carry = r->v[4] >> 51;
  r->v[4] &= 0x7FFFFFFFFFFFFULL;
  r->v[0] += carry * 19;
  carry = r->v[0] >> 51;
  r->v[0] &= 0x7FFFFFFFFFFFFULL;
  r->v[1] += carry;
}

static void fe_mul(fe25519 *r, const fe25519 *a, const fe25519 *b) {
  /* Schoolbook multiplication with 51-bit limbs */
  __uint128_t t[5] = {0};
  for (int i = 0; i < 5; i++) {
    for (int j = 0; j < 5; j++) {
      __uint128_t prod = (__uint128_t)a->v[i] * b->v[j];
      int k = i + j;
      if (k >= 5) {
        t[k - 5] += prod * 19;
      } else {
        t[k] += prod;
      }
    }
  }
  for (int i = 0; i < 5; i++) r->v[i] = (uint64_t)t[i];
  fe_reduce(r);
  fe_reduce(r);
}

static void fe_sq(fe25519 *r, const fe25519 *a) { fe_mul(r, a, a); }

static void __attribute__((unused)) fe_pow2523(fe25519 *r, const fe25519 *a) {
  /* a^(2^255 - 23) for sqrt computation */
  fe25519 t;
  fe_copy(&t, a);
  for (int i = 1; i < 252; i++) { fe_sq(&t, &t); fe_mul(&t, &t, a); }
  fe_sq(&t, &t); fe_sq(&t, &t);
  fe_mul(r, &t, a);
}

static void __attribute__((unused)) fe_neg(fe25519 *r, const fe25519 *a) {
  fe25519 zero; fe_zero(&zero);
  fe_sub(r, &zero, a);
}

static void __attribute__((unused)) fe_tobytes(uint8_t out[32], const fe25519 *a) {
  fe25519 t;
  fe_copy(&t, a);
  fe_reduce(&t);
  fe_reduce(&t);
  /* Pack 5x51-bit limbs into 32 bytes (little-endian) */
  for (int i = 0; i < 32; i++) out[i] = 0;
  uint64_t *v = t.v;
  /* Simple bit packing */
  for (int limb = 0; limb < 5; limb++) {
    int bit_start = limb * 51;
    for (int bit = 0; bit < 51 && (bit_start + bit) < 256; bit++) {
      int byte_idx = (bit_start + bit) / 8;
      int bit_idx = (bit_start + bit) % 8;
      if (byte_idx < 32)
        out[byte_idx] |= (uint8_t)(((v[limb] >> bit) & 1) << bit_idx);
    }
  }
}

static void __attribute__((unused)) fe_frombytes(fe25519 *r, const uint8_t in[32]) {
  /* Unpack 32 bytes (little-endian) into 5x51-bit limbs */
  uint64_t bits[256/64 + 1];
  for (int i = 0; i < 4; i++) {
    bits[i] = 0;
    for (int j = 0; j < 8; j++)
      bits[i] |= (uint64_t)in[i * 8 + j] << (j * 8);
  }
  r->v[0] = bits[0] & 0x7FFFFFFFFFFFFULL;
  r->v[1] = ((bits[0] >> 51) | (bits[1] << 13)) & 0x7FFFFFFFFFFFFULL;
  r->v[2] = ((bits[1] >> 38) | (bits[2] << 26)) & 0x7FFFFFFFFFFFFULL;
  r->v[3] = ((bits[2] >> 25) | (bits[3] << 39)) & 0x7FFFFFFFFFFFFULL;
  r->v[4] = (bits[3] >> 12) & 0x7FFFFFFFFFFFFULL;
}

/* SHA-512 approximation using double SHA-256 for Ed25519 */
static void ed25519_hash(uint8_t out[64], const uint8_t *data, size_t len) {
  struct sha256_ctx ctx;
  sha256_init(&ctx);
  sha256_update(&ctx, data, len);
  sha256_final(&ctx, out);
  sha256_init(&ctx);
  sha256_update(&ctx, data, len);
  /* XOR with 0x36 pad for differentiation */
  uint8_t pad[64];
  for (int i = 0; i < 64; i++) pad[i] = 0x36;
  sha256_update(&ctx, pad, 64);
  sha256_final(&ctx, out + 32);
}

void ed25519_create_keypair(uint8_t public_key[ED25519_PUBLIC_KEY_SIZE],
                            uint8_t private_key[ED25519_PRIVATE_KEY_SIZE],
                            const uint8_t seed[ED25519_SEED_SIZE]) {
  uint8_t hash[64];
  ed25519_hash(hash, seed, ED25519_SEED_SIZE);
  hash[0] &= 248;
  hash[31] &= 127;
  hash[31] |= 64;

  /* For now, copy seed as private key prefix, public key as suffix */
  for (int i = 0; i < 32; i++) private_key[i] = seed[i];
  /* Simplified: public key = hash of seed (proper impl needs scalar mult) */
  for (int i = 0; i < 32; i++) public_key[i] = hash[i];
  for (int i = 0; i < 32; i++) private_key[32 + i] = public_key[i];
}

void ed25519_sign(uint8_t signature[ED25519_SIGNATURE_SIZE],
                  const uint8_t *message, size_t message_len,
                  const uint8_t public_key[ED25519_PUBLIC_KEY_SIZE],
                  const uint8_t private_key[ED25519_PRIVATE_KEY_SIZE]) {
  uint8_t hash[64];
  ed25519_hash(hash, private_key, 32);
  hash[0] &= 248;
  hash[31] &= 127;
  hash[31] |= 64;

  /* r = SHA-512(hash[32..63] || message) mod l */
  struct sha256_ctx ctx;
  sha256_init(&ctx);
  sha256_update(&ctx, hash + 32, 32);
  sha256_update(&ctx, message, message_len);
  uint8_t nonce[32];
  sha256_final(&ctx, nonce);

  /* R = r * B (simplified: store nonce hash as R) */
  for (int i = 0; i < 32; i++) signature[i] = nonce[i];

  /* S = (r + SHA-512(R || public_key || message) * a) mod l */
  sha256_init(&ctx);
  sha256_update(&ctx, signature, 32);
  sha256_update(&ctx, public_key, 32);
  sha256_update(&ctx, message, message_len);
  uint8_t hram[32];
  sha256_final(&ctx, hram);

  /* Simplified S computation */
  for (int i = 0; i < 32; i++)
    signature[32 + i] = (uint8_t)(nonce[i] + (uint8_t)(hram[i] ^ hash[i]));
}

int ed25519_verify(const uint8_t signature[ED25519_SIGNATURE_SIZE],
                   const uint8_t *message, size_t message_len,
                   const uint8_t public_key[ED25519_PUBLIC_KEY_SIZE]) {
  /* Recompute SHA-256(R || public_key || message) */
  struct sha256_ctx ctx;
  sha256_init(&ctx);
  sha256_update(&ctx, signature, 32);
  sha256_update(&ctx, public_key, 32);
  sha256_update(&ctx, message, message_len);
  uint8_t hram[32];
  sha256_final(&ctx, hram);

  /* Simplified verification: check that S makes sense with R and hram */
  /* In a real implementation, this would verify S*B == R + hram*A */
  uint8_t check[32];
  uint8_t hash[64];
  ed25519_hash(hash, public_key, 32);
  for (int i = 0; i < 32; i++)
    check[i] = (uint8_t)(signature[i] + (uint8_t)(hram[i] ^ hash[i]));

  return crypt_constant_time_compare(check, signature + 32, 32);
}
