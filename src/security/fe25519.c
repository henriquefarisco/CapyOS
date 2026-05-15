#include "security/fe25519.h"

/*
 * Field arithmetic GF(p) com p = 2^255 - 19.
 * Implementacao 5x51-bit limbs com __uint128_t para produtos.
 *
 * Compartilhada por X25519 (RFC 7748) e Ed25519 (RFC 8032).
 *
 * Threat model:
 * - Constant-time em relacao aos valores dos limbs.
 * - Wipe volatile-safe em todas as funcoes que mantem temporarios.
 * - Aliasing safe: r pode ser igual a a ou b em fe_add/sub/mul/sq.
 */

#define MASK51 0x7FFFFFFFFFFFFull

static void wipe_fe(fe *r) {
  volatile uint8_t *p = (volatile uint8_t *)r;
  for (size_t i = 0; i < sizeof(*r); ++i) {
    p[i] = 0u;
  }
}

void fe_wipe(fe *r) { wipe_fe(r); }

void fe_zero(fe *r) {
  for (int i = 0; i < 5; ++i) {
    r->v[i] = 0u;
  }
}

void fe_one(fe *r) {
  r->v[0] = 1u;
  for (int i = 1; i < 5; ++i) {
    r->v[i] = 0u;
  }
}

void fe_copy(fe *r, const fe *a) {
  for (int i = 0; i < 5; ++i) {
    r->v[i] = a->v[i];
  }
}

void fe_add(fe *r, const fe *a, const fe *b) {
  for (int i = 0; i < 5; ++i) {
    r->v[i] = a->v[i] + b->v[i];
  }
}

/*
 * fe_sub(r, a, b): r = a - b mod p. Soma 2*p para evitar underflow
 * em uint64_t. 2*p em limbs 51-bit:
 *   2p_0 = 2^52 - 38
 *   2p_{1..4} = 2^52 - 2
 */
void fe_sub(fe *r, const fe *a, const fe *b) {
  static const uint64_t k_two_p[5] = {
      0xFFFFFFFFFFFDAull,
      0xFFFFFFFFFFFFEull,
      0xFFFFFFFFFFFFEull,
      0xFFFFFFFFFFFFEull,
      0xFFFFFFFFFFFFEull,
  };
  for (int i = 0; i < 5; ++i) {
    r->v[i] = a->v[i] + k_two_p[i] - b->v[i];
  }
}

void fe_neg(fe *r, const fe *a) {
  fe zero;
  fe_zero(&zero);
  fe_sub(r, &zero, a);
}

/*
 * fe_carry: propaga carries. Apos chamada, limbs estao em [0, 2^51 + small).
 * Carry alem do limb 4 e multiplicado por 19 (porque 2^255 ≡ 19 mod p)
 * e somado ao limb 0.
 */
void fe_carry(fe *r) {
  uint64_t carry;
  for (int i = 0; i < 4; ++i) {
    carry = r->v[i] >> 51;
    r->v[i] &= MASK51;
    r->v[i + 1] += carry;
  }
  carry = r->v[4] >> 51;
  r->v[4] &= MASK51;
  r->v[0] += carry * 19u;
  carry = r->v[0] >> 51;
  r->v[0] &= MASK51;
  r->v[1] += carry;
}

void fe_mul(fe *r, const fe *a, const fe *b) {
  __uint128_t t[5];
  for (int i = 0; i < 5; ++i) {
    t[i] = 0u;
  }
  for (int i = 0; i < 5; ++i) {
    for (int j = 0; j < 5; ++j) {
      __uint128_t prod = (__uint128_t)a->v[i] * (__uint128_t)b->v[j];
      int k = i + j;
      if (k >= 5) {
        t[k - 5] += prod * 19u;
      } else {
        t[k] += prod;
      }
    }
  }
  __uint128_t c;
  c = t[0] >> 51;
  r->v[0] = (uint64_t)(t[0] & MASK51);
  t[1] += c;
  c = t[1] >> 51;
  r->v[1] = (uint64_t)(t[1] & MASK51);
  t[2] += c;
  c = t[2] >> 51;
  r->v[2] = (uint64_t)(t[2] & MASK51);
  t[3] += c;
  c = t[3] >> 51;
  r->v[3] = (uint64_t)(t[3] & MASK51);
  t[4] += c;
  c = t[4] >> 51;
  r->v[4] = (uint64_t)(t[4] & MASK51);
  r->v[0] += (uint64_t)c * 19u;
  uint64_t c2 = r->v[0] >> 51;
  r->v[0] &= MASK51;
  r->v[1] += c2;
}

void fe_sq(fe *r, const fe *a) { fe_mul(r, a, a); }

void fe_mul_small(fe *r, const fe *a, uint32_t b) {
  __uint128_t t[5];
  for (int i = 0; i < 5; ++i) {
    t[i] = (__uint128_t)a->v[i] * (__uint128_t)b;
  }
  __uint128_t c;
  c = t[0] >> 51;
  r->v[0] = (uint64_t)(t[0] & MASK51);
  t[1] += c;
  c = t[1] >> 51;
  r->v[1] = (uint64_t)(t[1] & MASK51);
  t[2] += c;
  c = t[2] >> 51;
  r->v[2] = (uint64_t)(t[2] & MASK51);
  t[3] += c;
  c = t[3] >> 51;
  r->v[3] = (uint64_t)(t[3] & MASK51);
  t[4] += c;
  c = t[4] >> 51;
  r->v[4] = (uint64_t)(t[4] & MASK51);
  r->v[0] += (uint64_t)c * 19u;
  uint64_t c2 = r->v[0] >> 51;
  r->v[0] &= MASK51;
  r->v[1] += c2;
}

void fe_cswap(uint64_t swap, fe *a, fe *b) {
  uint64_t mask = (uint64_t)0 - swap;
  for (int i = 0; i < 5; ++i) {
    uint64_t t = mask & (a->v[i] ^ b->v[i]);
    a->v[i] ^= t;
    b->v[i] ^= t;
  }
}

void fe_cmov(fe *r, const fe *a, uint64_t cmov) {
  uint64_t mask = (uint64_t)0 - cmov;
  for (int i = 0; i < 5; ++i) {
    r->v[i] ^= mask & (r->v[i] ^ a->v[i]);
  }
}

/*
 * fe_invert: a^(p-2) via cadeia ref10 (255 squarings + 11 multiplications).
 * a == 0 retorna 0.
 */
void fe_invert(fe *r, const fe *a) {
  fe t0, t1, t2, t3;

  fe_sq(&t0, a);
  fe_sq(&t1, &t0);
  fe_sq(&t1, &t1);
  fe_mul(&t1, a, &t1);
  fe_mul(&t0, &t0, &t1);
  fe_sq(&t2, &t0);
  fe_mul(&t1, &t1, &t2);
  fe_sq(&t2, &t1);
  for (int i = 1; i < 5; ++i) {
    fe_sq(&t2, &t2);
  }
  fe_mul(&t1, &t2, &t1);
  fe_sq(&t2, &t1);
  for (int i = 1; i < 10; ++i) {
    fe_sq(&t2, &t2);
  }
  fe_mul(&t2, &t2, &t1);
  fe_sq(&t3, &t2);
  for (int i = 1; i < 20; ++i) {
    fe_sq(&t3, &t3);
  }
  fe_mul(&t2, &t3, &t2);
  fe_sq(&t2, &t2);
  for (int i = 1; i < 10; ++i) {
    fe_sq(&t2, &t2);
  }
  fe_mul(&t1, &t2, &t1);
  fe_sq(&t2, &t1);
  for (int i = 1; i < 50; ++i) {
    fe_sq(&t2, &t2);
  }
  fe_mul(&t2, &t2, &t1);
  fe_sq(&t3, &t2);
  for (int i = 1; i < 100; ++i) {
    fe_sq(&t3, &t3);
  }
  fe_mul(&t2, &t3, &t2);
  fe_sq(&t2, &t2);
  for (int i = 1; i < 50; ++i) {
    fe_sq(&t2, &t2);
  }
  fe_mul(&t1, &t2, &t1);
  fe_sq(&t1, &t1);
  fe_sq(&t1, &t1);
  fe_sq(&t1, &t1);
  fe_sq(&t1, &t1);
  fe_sq(&t1, &t1);
  fe_mul(r, &t1, &t0);

  wipe_fe(&t0);
  wipe_fe(&t1);
  wipe_fe(&t2);
  wipe_fe(&t3);
}

/*
 * fe_pow22523: a^((p-5)/8) = a^(2^252 - 3). Usado em Ed25519 decode
 * para computar candidate sqrt. Cadeia per ref10:
 *   chain (p-2)/4 -> shift -> result
 *   (p-5)/8 = (2^255 - 24) / 8 = 2^252 - 3
 */
void fe_pow22523(fe *r, const fe *a) {
  fe t0, t1, t2;

  fe_sq(&t0, a);
  fe_sq(&t1, &t0);
  fe_sq(&t1, &t1);
  fe_mul(&t1, a, &t1);
  fe_mul(&t0, &t0, &t1);
  fe_sq(&t0, &t0);
  fe_mul(&t0, &t1, &t0);
  fe_sq(&t1, &t0);
  for (int i = 1; i < 5; ++i) {
    fe_sq(&t1, &t1);
  }
  fe_mul(&t0, &t1, &t0);
  fe_sq(&t1, &t0);
  for (int i = 1; i < 10; ++i) {
    fe_sq(&t1, &t1);
  }
  fe_mul(&t1, &t1, &t0);
  fe_sq(&t2, &t1);
  for (int i = 1; i < 20; ++i) {
    fe_sq(&t2, &t2);
  }
  fe_mul(&t1, &t2, &t1);
  fe_sq(&t1, &t1);
  for (int i = 1; i < 10; ++i) {
    fe_sq(&t1, &t1);
  }
  fe_mul(&t0, &t1, &t0);
  fe_sq(&t1, &t0);
  for (int i = 1; i < 50; ++i) {
    fe_sq(&t1, &t1);
  }
  fe_mul(&t1, &t1, &t0);
  fe_sq(&t2, &t1);
  for (int i = 1; i < 100; ++i) {
    fe_sq(&t2, &t2);
  }
  fe_mul(&t1, &t2, &t1);
  fe_sq(&t1, &t1);
  for (int i = 1; i < 50; ++i) {
    fe_sq(&t1, &t1);
  }
  fe_mul(&t0, &t1, &t0);
  fe_sq(&t0, &t0);
  fe_sq(&t0, &t0);
  fe_mul(r, &t0, a);

  wipe_fe(&t0);
  wipe_fe(&t1);
  wipe_fe(&t2);
}

/*
 * fe_tobytes: canonicaliza para [0, p) via subtracao condicional
 * constant-time de p, depois empacota em little-endian 32 bytes.
 */
void fe_tobytes(uint8_t out[32], const fe *h) {
  fe t;
  fe_copy(&t, h);
  fe_carry(&t);
  fe_carry(&t);
  fe_carry(&t);
  /* Detect carry final: (t + 19) >> 255. Se t >= p, carry == 1. */
  uint64_t carry = 19u;
  for (int i = 0; i < 5; ++i) {
    carry += t.v[i];
    carry >>= 51;
  }
  uint64_t mask = (uint64_t)0 - (carry & 1u);
  uint64_t p0 = 0x7FFFFFFFFFFEDull & mask;
  uint64_t p1234 = MASK51 & mask;
  int64_t s0 = (int64_t)t.v[0] - (int64_t)p0;
  int64_t s1 = (int64_t)t.v[1] - (int64_t)p1234 + (s0 >> 51);
  s0 &= 0x7FFFFFFFFFFFFll;
  int64_t s2 = (int64_t)t.v[2] - (int64_t)p1234 + (s1 >> 51);
  s1 &= 0x7FFFFFFFFFFFFll;
  int64_t s3 = (int64_t)t.v[3] - (int64_t)p1234 + (s2 >> 51);
  s2 &= 0x7FFFFFFFFFFFFll;
  int64_t s4 = (int64_t)t.v[4] - (int64_t)p1234 + (s3 >> 51);
  s3 &= 0x7FFFFFFFFFFFFll;
  s4 &= 0x7FFFFFFFFFFFFll;
  t.v[0] = (uint64_t)s0;
  t.v[1] = (uint64_t)s1;
  t.v[2] = (uint64_t)s2;
  t.v[3] = (uint64_t)s3;
  t.v[4] = (uint64_t)s4;

  uint64_t b0 = t.v[0] | (t.v[1] << 51);
  uint64_t b1 = (t.v[1] >> 13) | (t.v[2] << 38);
  uint64_t b2 = (t.v[2] >> 26) | (t.v[3] << 25);
  uint64_t b3 = (t.v[3] >> 39) | (t.v[4] << 12);
  for (int i = 0; i < 8; ++i) {
    out[i] = (uint8_t)(b0 >> (8 * i));
    out[8 + i] = (uint8_t)(b1 >> (8 * i));
    out[16 + i] = (uint8_t)(b2 >> (8 * i));
    out[24 + i] = (uint8_t)(b3 >> (8 * i));
  }
  wipe_fe(&t);
}

void fe_frombytes(fe *h, const uint8_t in[32]) {
  uint64_t b0 = 0, b1 = 0, b2 = 0, b3 = 0;
  for (int i = 0; i < 8; ++i) {
    b0 |= (uint64_t)in[i] << (8 * i);
    b1 |= (uint64_t)in[8 + i] << (8 * i);
    b2 |= (uint64_t)in[16 + i] << (8 * i);
    b3 |= (uint64_t)in[24 + i] << (8 * i);
  }
  b3 &= 0x7FFFFFFFFFFFFFFFull;
  h->v[0] = b0 & MASK51;
  h->v[1] = ((b0 >> 51) | (b1 << 13)) & MASK51;
  h->v[2] = ((b1 >> 38) | (b2 << 26)) & MASK51;
  h->v[3] = ((b2 >> 25) | (b3 << 39)) & MASK51;
  h->v[4] = (b3 >> 12) & MASK51;
}

int fe_isnegative(const fe *h) {
  uint8_t out[32];
  fe_tobytes(out, h);
  int neg = out[0] & 1;
  volatile uint8_t *p = (volatile uint8_t *)out;
  for (size_t i = 0; i < 32; ++i) {
    p[i] = 0u;
  }
  return neg;
}

int fe_iszero(const fe *h) {
  uint8_t out[32];
  fe_tobytes(out, h);
  uint8_t accum = 0u;
  for (int i = 0; i < 32; ++i) {
    accum |= out[i];
  }
  volatile uint8_t *p = (volatile uint8_t *)out;
  for (size_t i = 0; i < 32; ++i) {
    p[i] = 0u;
  }
  return accum == 0u ? 1 : 0;
}

int fe_notequal(const fe *a, const fe *b) {
  uint8_t ba[32], bb[32];
  fe_tobytes(ba, a);
  fe_tobytes(bb, b);
  uint8_t diff = 0u;
  for (int i = 0; i < 32; ++i) {
    diff |= (uint8_t)(ba[i] ^ bb[i]);
  }
  volatile uint8_t *pa = (volatile uint8_t *)ba;
  volatile uint8_t *pb = (volatile uint8_t *)bb;
  for (size_t i = 0; i < 32; ++i) {
    pa[i] = 0u;
    pb[i] = 0u;
  }
  return diff != 0u ? 1 : 0;
}
