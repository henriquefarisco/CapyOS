#include "security/ed25519.h"
#include "security/fe25519.h"
#include "security/sha512.h"
#include <stddef.h>
#include <stdint.h>

/*
 * Ed25519 (RFC 8032) — implementacao auditavel constant-time.
 *
 * Substitui o esqueleto fail-closed que estava aqui desde alpha.210.
 * Field arithmetic GF(2^255-19) e fe25519 compartilhado com X25519.
 *
 * APIs publicas (em include/security/ed25519.h):
 *   ed25519_create_keypair: deriva (pk, sk) de seed via SHA-512.
 *   ed25519_sign: assina mensagem (RFC 8032 §5.1.6 PureEd25519).
 *   ed25519_verify: verifica assinatura (§5.1.7 com cofator 8).
 *
 * Threat model:
 *  - Sign: constant-time em relacao a scalar secreto (sB e kS+r).
 *  - Verify: vartime aceitavel (entradas publicas), mas mantemos
 *    constant-time por higiene.
 *  - Wipe volatile-safe em todos os intermediarios derivados de seed.
 *  - Fail-closed: NULL/comprimento invalidos/encoded point invalido
 *    retornam -1 sem comprometer integridade.
 *  - Small-subgroup safety: verify multiplica por 8 (cofator) para
 *    rejeitar variantes adulteradas com componente torsao.
 */

/* ============================================================
 * Constantes do grupo Edwards25519.
 * Valores cross-verificados contra dalek-cryptography (5x51-bit limbs)
 * e RFC 8032 (encoded form do base point B).
 * ============================================================ */

/* d = -121665/121666 mod p (twisted Edwards curve constant). */
static const fe ED_D = {{
    929955233495203ull,
    466365720129213ull,
    1662059464998953ull,
    2033849074728123ull,
    1442794654840575ull,
}};

/* 2*d (usado em point addition formula). */
static const fe ED_D2 = {{
    1859910466990425ull,
    932731440258426ull,
    1072319116312658ull,
    1815898335770999ull,
    633789495995903ull,
}};

/* sqrt(-1) mod p = 2^((p-1)/4). Usado em point decompression. */
static const fe ED_SQRTM1 = {{
    1718705420411056ull,
    234908883556509ull,
    2233514472574048ull,
    2117202627021982ull,
    765476049583133ull,
}};

/* Base point B em coordenadas extended (X, Y, Z=1, T=XY). */
static const fe ED_B_X = {{
    1738742601995546ull,
    1146398526822698ull,
    2070867633025821ull,
    562264141797630ull,
    587772402128613ull,
}};
static const fe ED_B_Y = {{
    1801439850948184ull,
    1351079888211148ull,
    450359962737049ull,
    900719925474099ull,
    1801439850948198ull,
}};
static const fe ED_B_T = {{
    1841354044333475ull,
    16398895984059ull,
    755974180946558ull,
    900171276175154ull,
    1821297809914039ull,
}};

/* L = 2^252 + 27742317777372353535851937790883648493 (ordem do subgrupo
 * prime). Encoded little-endian 32 bytes. */
static const uint8_t ED_L_BYTES[32] = {
    0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
    0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
};

/* ============================================================
 * Edwards point in extended coordinates (X:Y:Z:T), T = X*Y/Z.
 * ============================================================ */

typedef struct {
  fe X, Y, Z, T;
} ge_p3;

static void ge_zero(ge_p3 *r) {
  fe_zero(&r->X);
  fe_one(&r->Y);
  fe_one(&r->Z);
  fe_zero(&r->T);
}

static void ge_copy(ge_p3 *r, const ge_p3 *p) {
  fe_copy(&r->X, &p->X);
  fe_copy(&r->Y, &p->Y);
  fe_copy(&r->Z, &p->Z);
  fe_copy(&r->T, &p->T);
}

static void ge_wipe(ge_p3 *r) {
  fe_wipe(&r->X);
  fe_wipe(&r->Y);
  fe_wipe(&r->Z);
  fe_wipe(&r->T);
}

static void wipe_bytes(void *p, size_t n) {
  volatile uint8_t *vp = (volatile uint8_t *)p;
  while (n--) {
    *vp++ = 0u;
  }
}

/*
 * Twisted Edwards doubling formula (Bernstein-Birkner-Lange 2008,
 * doubling-2008-hwcd). 4 squarings + 4 muls + 7 adds.
 *
 *   A = X1^2
 *   B = Y1^2
 *   C = 2 * Z1^2
 *   D = -A   (a = -1 for Ed25519)
 *   E = (X1+Y1)^2 - A - B
 *   G = D + B
 *   F = G - C
 *   H = D - B
 *   X3 = E * F
 *   Y3 = G * H
 *   T3 = E * H
 *   Z3 = F * G
 */
static void ge_dbl(ge_p3 *r, const ge_p3 *p) {
  fe A, B, C, D, E, F, G, H, t;
  fe_sq(&A, &p->X);
  fe_sq(&B, &p->Y);
  fe_sq(&C, &p->Z);
  fe_add(&C, &C, &C);
  fe_carry(&C);
  fe_neg(&D, &A);
  fe_add(&t, &p->X, &p->Y);
  fe_sq(&E, &t);
  fe_sub(&E, &E, &A);
  fe_sub(&E, &E, &B);
  fe_carry(&E);
  fe_add(&G, &D, &B);
  fe_carry(&G);
  fe_sub(&F, &G, &C);
  fe_carry(&F);
  fe_sub(&H, &D, &B);
  fe_carry(&H);
  fe_mul(&r->X, &E, &F);
  fe_mul(&r->Y, &G, &H);
  fe_mul(&r->T, &E, &H);
  fe_mul(&r->Z, &F, &G);
  fe_wipe(&A);
  fe_wipe(&B);
  fe_wipe(&C);
  fe_wipe(&D);
  fe_wipe(&E);
  fe_wipe(&F);
  fe_wipe(&G);
  fe_wipe(&H);
  fe_wipe(&t);
}

/*
 * Twisted Edwards general addition (Hisil-Wong-Carter-Dawson 2008,
 * add-2008-hwcd-3). 9 muls + 7 adds.
 *
 *   A = (Y1-X1) * (Y2-X2)
 *   B = (Y1+X1) * (Y2+X2)
 *   C = T1 * 2d * T2
 *   D = Z1 * 2 * Z2
 *   E = B - A
 *   F = D - C
 *   G = D + C
 *   H = B + A
 *   X3 = E * F
 *   Y3 = G * H
 *   T3 = E * H
 *   Z3 = F * G
 */
static void ge_add(ge_p3 *r, const ge_p3 *p, const ge_p3 *q) {
  fe A, B, C, D, E, F, G, H, t1, t2;
  fe_sub(&t1, &p->Y, &p->X);
  fe_sub(&t2, &q->Y, &q->X);
  fe_mul(&A, &t1, &t2);
  fe_add(&t1, &p->Y, &p->X);
  fe_add(&t2, &q->Y, &q->X);
  fe_mul(&B, &t1, &t2);
  fe_mul(&C, &p->T, &ED_D2);
  fe_mul(&C, &C, &q->T);
  fe_mul(&D, &p->Z, &q->Z);
  fe_add(&D, &D, &D);
  fe_carry(&D);
  fe_sub(&E, &B, &A);
  fe_sub(&F, &D, &C);
  fe_add(&G, &D, &C);
  fe_add(&H, &B, &A);
  fe_carry(&E);
  fe_carry(&F);
  fe_carry(&G);
  fe_carry(&H);
  fe_mul(&r->X, &E, &F);
  fe_mul(&r->Y, &G, &H);
  fe_mul(&r->T, &E, &H);
  fe_mul(&r->Z, &F, &G);
  fe_wipe(&A);
  fe_wipe(&B);
  fe_wipe(&C);
  fe_wipe(&D);
  fe_wipe(&E);
  fe_wipe(&F);
  fe_wipe(&G);
  fe_wipe(&H);
  fe_wipe(&t1);
  fe_wipe(&t2);
}

/* r = -p (negation in twisted Edwards: -P = (-X, Y, Z, -T)). */
static void ge_neg_p(ge_p3 *r, const ge_p3 *p) {
  fe_neg(&r->X, &p->X);
  fe_copy(&r->Y, &p->Y);
  fe_copy(&r->Z, &p->Z);
  fe_neg(&r->T, &p->T);
}

/*
 * Constant-time conditional copy: if cmov=1, r=p; else r unchanged.
 */
static void ge_cmov(ge_p3 *r, const ge_p3 *p, uint64_t cmov) {
  fe_cmov(&r->X, &p->X, cmov);
  fe_cmov(&r->Y, &p->Y, cmov);
  fe_cmov(&r->Z, &p->Z, cmov);
  fe_cmov(&r->T, &p->T, cmov);
}

/* ============================================================
 * Scalar multiplication: r = scalar * P, constant-time.
 * Algorithm: double-and-add com cmov. ~256 doubles + ~256 cond-adds.
 * ============================================================ */

static void ge_scalarmult(ge_p3 *r, const uint8_t scalar[32],
                          const ge_p3 *P) {
  ge_p3 R0;
  ge_zero(&R0);

  /* Double-and-add constant-time. Itera bits 255 -> 0. Cada iteracao:
   *   D = 2*R0; S = D + P; R0 = (bit ? S : D).
   * cmov constant-time previne side channels do timing do bit. */
  for (int t = 255; t >= 0; --t) {
    ge_p3 D, S;
    ge_dbl(&D, &R0);
    ge_add(&S, &D, P);
    uint64_t bit = (scalar[t >> 3] >> (t & 7)) & 1u;
    ge_copy(&R0, &D);
    ge_cmov(&R0, &S, bit);
    ge_wipe(&D);
    ge_wipe(&S);
  }
  ge_copy(r, &R0);
  ge_wipe(&R0);
}

static void ge_scalarmult_base(ge_p3 *r, const uint8_t scalar[32]) {
  ge_p3 B;
  fe_copy(&B.X, &ED_B_X);
  fe_copy(&B.Y, &ED_B_Y);
  fe_one(&B.Z);
  fe_copy(&B.T, &ED_B_T);
  ge_scalarmult(r, scalar, &B);
  ge_wipe(&B);
}

/* r = a*A + b*B (verify). Entradas publicas; usamos constant-time
 * para higiene mas o atacante so observa entradas publicas mesmo. */
static void ge_double_scalarmult(ge_p3 *r,
                                 const uint8_t a[32], const ge_p3 *A,
                                 const uint8_t b[32]) {
  ge_p3 B, aA, bB;
  fe_copy(&B.X, &ED_B_X);
  fe_copy(&B.Y, &ED_B_Y);
  fe_one(&B.Z);
  fe_copy(&B.T, &ED_B_T);
  ge_scalarmult(&aA, a, A);
  ge_scalarmult(&bB, b, &B);
  ge_add(r, &aA, &bB);
  ge_wipe(&B);
  ge_wipe(&aA);
  ge_wipe(&bB);
}

/* ============================================================
 * Encoding/decoding de pontos (compressed: 32 bytes).
 *
 * Encoded format (RFC 8032 §5.1.2):
 *   bytes[0..30] = y (LE) bits 0..247
 *   bytes[31] = bits 248..254 of y | (sign(x) << 7)
 * where sign(x) = x mod 2 (LSB of x).
 * ============================================================ */

static void ge_encode(uint8_t out[32], const ge_p3 *p) {
  fe x, y, zinv;
  fe_invert(&zinv, &p->Z);
  fe_mul(&x, &p->X, &zinv);
  fe_mul(&y, &p->Y, &zinv);
  fe_tobytes(out, &y);
  int sign = fe_isnegative(&x);
  out[31] |= (uint8_t)(sign << 7);
  fe_wipe(&x);
  fe_wipe(&y);
  fe_wipe(&zinv);
}

/*
 * Decoding (RFC 8032 §5.1.3):
 *  1. y = encoded[0..30] | (encoded[31] & 0x7F)
 *  2. x_0 = encoded[31] >> 7
 *  3. y^2 - 1 = u; d*y^2 + 1 = v
 *  4. x_candidate = u * v^3 * (u * v^7)^((p-5)/8)
 *  5. check v*x^2 == u → x = x_candidate
 *     elif v*x^2 == -u → x = x_candidate * sqrt(-1)
 *     else fail
 *  6. if sign(x) != x_0: x = -x
 *  7. if x == 0 and x_0 == 1: fail
 * Retorna 0 OK, -1 falha.
 */
static int ge_decode(ge_p3 *r, const uint8_t in[32]) {
  uint8_t buf[32];
  for (int i = 0; i < 32; ++i) {
    buf[i] = in[i];
  }
  int x_0 = buf[31] >> 7;
  buf[31] &= 0x7Fu;

  fe y, u, v, v3, vxx, check, x;
  fe_frombytes(&y, buf);
  /* y must be canonical: re-encode and compare. */
  uint8_t check_buf[32];
  fe_tobytes(check_buf, &y);
  /* Restore the cleared sign bit before comparison. */
  check_buf[31] |= (uint8_t)(x_0 << 7);
  int diff = 0;
  for (int i = 0; i < 32; ++i) {
    diff |= (int)(check_buf[i] ^ in[i]);
  }
  if (diff != 0) {
    wipe_bytes(buf, sizeof(buf));
    wipe_bytes(check_buf, sizeof(check_buf));
    fe_wipe(&y);
    return -1;
  }

  fe one_fe;
  fe_one(&one_fe);
  fe_sq(&u, &y);
  fe_sub(&u, &u, &one_fe);   /* u = y^2 - 1 */
  fe_sq(&v, &y);
  fe_mul(&v, &v, &ED_D);
  fe_add(&v, &v, &one_fe);   /* v = d*y^2 + 1 */
  fe_carry(&v);

  /* v3 = v^3 */
  fe_sq(&v3, &v);
  fe_mul(&v3, &v3, &v);
  /* x_candidate = u * v^3 * (u * v^7)^((p-5)/8)
   * Equivalent: x = (u * v^3) * (u * v^3 * v^4)^((p-5)/8)
   * since v^7 = v^3 * v^4 and we have v3 already.
   */
  fe v7, w, x_cand;
  fe_sq(&v7, &v3);            /* v^6 */
  fe_mul(&v7, &v7, &v);       /* v^7 */
  fe_mul(&w, &v7, &u);        /* u * v^7 */
  fe_pow22523(&w, &w);        /* (u v^7)^((p-5)/8) */
  fe_mul(&x_cand, &v3, &u);
  fe_mul(&x_cand, &x_cand, &w);

  /* check v*x^2 == u? */
  fe_sq(&vxx, &x_cand);
  fe_mul(&vxx, &vxx, &v);
  fe_sub(&check, &vxx, &u);
  fe_carry(&check);
  if (!fe_iszero(&check)) {
    /* else: v*x^2 == -u? then x = x_cand * sqrt(-1) */
    fe_add(&check, &vxx, &u);
    fe_carry(&check);
    if (!fe_iszero(&check)) {
      wipe_bytes(buf, sizeof(buf));
      wipe_bytes(check_buf, sizeof(check_buf));
      fe_wipe(&y);
      fe_wipe(&u);
      fe_wipe(&v);
      fe_wipe(&v3);
      fe_wipe(&v7);
      fe_wipe(&w);
      fe_wipe(&x_cand);
      fe_wipe(&vxx);
      fe_wipe(&check);
      return -1;
    }
    fe_mul(&x_cand, &x_cand, &ED_SQRTM1);
  }

  /* if sign(x_cand) != x_0, negate */
  if (fe_isnegative(&x_cand) != x_0) {
    fe_neg(&x_cand, &x_cand);
  }
  /* if x==0 and x_0==1, fail (RFC 8032 §5.1.3 step 5) */
  if (fe_iszero(&x_cand) && x_0 == 1) {
    wipe_bytes(buf, sizeof(buf));
    wipe_bytes(check_buf, sizeof(check_buf));
    fe_wipe(&y);
    fe_wipe(&u);
    fe_wipe(&v);
    fe_wipe(&v3);
    fe_wipe(&v7);
    fe_wipe(&w);
    fe_wipe(&x_cand);
    fe_wipe(&vxx);
    fe_wipe(&check);
    return -1;
  }
  fe_copy(&x, &x_cand);
  fe_copy(&r->X, &x);
  fe_copy(&r->Y, &y);
  fe_one(&r->Z);
  fe_mul(&r->T, &x, &y);

  wipe_bytes(buf, sizeof(buf));
  wipe_bytes(check_buf, sizeof(check_buf));
  fe_wipe(&y);
  fe_wipe(&u);
  fe_wipe(&v);
  fe_wipe(&v3);
  fe_wipe(&v7);
  fe_wipe(&w);
  fe_wipe(&x_cand);
  fe_wipe(&vxx);
  fe_wipe(&check);
  fe_wipe(&x);
  fe_wipe(&one_fe);
  return 0;
}

/* ============================================================
 * Scalar arithmetic mod L.
 *
 * L = 2^252 + 27742317777372353535851937790883648493.
 *
 * Representacao em 24 limbs signed de 21 bits (252-bit / 21 = 12,
 * mais 12 extras para acomodar produtos ate 2*512 bits). Porte de
 * ref10/sc25519.c (Bernstein et al.), audit-verified.
 *
 * Fato chave: 2^252 ≡ -L_lo (mod L), onde L_lo = 27742...8493 expandido
 * em 21-bit signed limbs: [666643, 470296, 654183, -997805, 136657,
 * -683901]. Reducao folds high limbs s_i (i >= 12) em low limbs
 * subtraindo s_i * L_lo * 2^(21*(i-12)).
 * ============================================================ */

static int64_t load_3(const uint8_t *in) {
  return (int64_t)((uint64_t)in[0] | ((uint64_t)in[1] << 8) |
                   ((uint64_t)in[2] << 16));
}

static int64_t load_4(const uint8_t *in) {
  return (int64_t)((uint64_t)in[0] | ((uint64_t)in[1] << 8) |
                   ((uint64_t)in[2] << 16) | ((uint64_t)in[3] << 24));
}

/*
 * sc_reduce: reduz 64 bytes (LE) mod L. Output em 32 bytes LE
 * canonico em [0, L).
 *
 * Algoritmo: parse 24 21-bit limbs, fold high 12 em low 12 via
 * cascading multiply-and-add, recarregar, propagate carries,
 * pack em 32 bytes.
 */
static void sc_reduce64(uint8_t out[32], const uint8_t in[64]) {
  int64_t s0 = 2097151 & load_3(in);
  int64_t s1 = 2097151 & (load_4(in + 2) >> 5);
  int64_t s2 = 2097151 & (load_3(in + 5) >> 2);
  int64_t s3 = 2097151 & (load_4(in + 7) >> 7);
  int64_t s4 = 2097151 & (load_4(in + 10) >> 4);
  int64_t s5 = 2097151 & (load_3(in + 13) >> 1);
  int64_t s6 = 2097151 & (load_4(in + 15) >> 6);
  int64_t s7 = 2097151 & (load_3(in + 18) >> 3);
  int64_t s8 = 2097151 & load_3(in + 21);
  int64_t s9 = 2097151 & (load_4(in + 23) >> 5);
  int64_t s10 = 2097151 & (load_3(in + 26) >> 2);
  int64_t s11 = 2097151 & (load_4(in + 28) >> 7);
  int64_t s12 = 2097151 & (load_4(in + 31) >> 4);
  int64_t s13 = 2097151 & (load_3(in + 34) >> 1);
  int64_t s14 = 2097151 & (load_4(in + 36) >> 6);
  int64_t s15 = 2097151 & (load_3(in + 39) >> 3);
  int64_t s16 = 2097151 & load_3(in + 42);
  int64_t s17 = 2097151 & (load_4(in + 44) >> 5);
  int64_t s18 = 2097151 & (load_3(in + 47) >> 2);
  int64_t s19 = 2097151 & (load_4(in + 49) >> 7);
  int64_t s20 = 2097151 & (load_4(in + 52) >> 4);
  int64_t s21 = 2097151 & (load_3(in + 55) >> 1);
  int64_t s22 = 2097151 & (load_4(in + 57) >> 6);
  int64_t s23 = (load_4(in + 60) >> 3);
  int64_t carry0, carry1, carry2, carry3, carry4, carry5, carry6;
  int64_t carry7, carry8, carry9, carry10, carry11, carry12, carry13;
  int64_t carry14, carry15, carry16;

  s11 += s23 * 666643;
  s12 += s23 * 470296;
  s13 += s23 * 654183;
  s14 -= s23 * 997805;
  s15 += s23 * 136657;
  s16 -= s23 * 683901;
  s23 = 0;
  s10 += s22 * 666643;
  s11 += s22 * 470296;
  s12 += s22 * 654183;
  s13 -= s22 * 997805;
  s14 += s22 * 136657;
  s15 -= s22 * 683901;
  s22 = 0;
  s9 += s21 * 666643;
  s10 += s21 * 470296;
  s11 += s21 * 654183;
  s12 -= s21 * 997805;
  s13 += s21 * 136657;
  s14 -= s21 * 683901;
  s21 = 0;
  s8 += s20 * 666643;
  s9 += s20 * 470296;
  s10 += s20 * 654183;
  s11 -= s20 * 997805;
  s12 += s20 * 136657;
  s13 -= s20 * 683901;
  s20 = 0;
  s7 += s19 * 666643;
  s8 += s19 * 470296;
  s9 += s19 * 654183;
  s10 -= s19 * 997805;
  s11 += s19 * 136657;
  s12 -= s19 * 683901;
  s19 = 0;
  s6 += s18 * 666643;
  s7 += s18 * 470296;
  s8 += s18 * 654183;
  s9 -= s18 * 997805;
  s10 += s18 * 136657;
  s11 -= s18 * 683901;
  s18 = 0;
  carry6 = (s6 + (1LL << 20)) >> 21;
  s7 += carry6;
  s6 -= carry6 << 21;
  carry8 = (s8 + (1LL << 20)) >> 21;
  s9 += carry8;
  s8 -= carry8 << 21;
  carry10 = (s10 + (1LL << 20)) >> 21;
  s11 += carry10;
  s10 -= carry10 << 21;
  carry12 = (s12 + (1LL << 20)) >> 21;
  s13 += carry12;
  s12 -= carry12 << 21;
  carry14 = (s14 + (1LL << 20)) >> 21;
  s15 += carry14;
  s14 -= carry14 << 21;
  carry16 = (s16 + (1LL << 20)) >> 21;
  s17 += carry16;
  s16 -= carry16 << 21;
  carry7 = (s7 + (1LL << 20)) >> 21;
  s8 += carry7;
  s7 -= carry7 << 21;
  carry9 = (s9 + (1LL << 20)) >> 21;
  s10 += carry9;
  s9 -= carry9 << 21;
  carry11 = (s11 + (1LL << 20)) >> 21;
  s12 += carry11;
  s11 -= carry11 << 21;
  carry13 = (s13 + (1LL << 20)) >> 21;
  s14 += carry13;
  s13 -= carry13 << 21;
  carry15 = (s15 + (1LL << 20)) >> 21;
  s16 += carry15;
  s15 -= carry15 << 21;
  s5 += s17 * 666643;
  s6 += s17 * 470296;
  s7 += s17 * 654183;
  s8 -= s17 * 997805;
  s9 += s17 * 136657;
  s10 -= s17 * 683901;
  s17 = 0;
  s4 += s16 * 666643;
  s5 += s16 * 470296;
  s6 += s16 * 654183;
  s7 -= s16 * 997805;
  s8 += s16 * 136657;
  s9 -= s16 * 683901;
  s16 = 0;
  s3 += s15 * 666643;
  s4 += s15 * 470296;
  s5 += s15 * 654183;
  s6 -= s15 * 997805;
  s7 += s15 * 136657;
  s8 -= s15 * 683901;
  s15 = 0;
  s2 += s14 * 666643;
  s3 += s14 * 470296;
  s4 += s14 * 654183;
  s5 -= s14 * 997805;
  s6 += s14 * 136657;
  s7 -= s14 * 683901;
  s14 = 0;
  s1 += s13 * 666643;
  s2 += s13 * 470296;
  s3 += s13 * 654183;
  s4 -= s13 * 997805;
  s5 += s13 * 136657;
  s6 -= s13 * 683901;
  s13 = 0;
  s0 += s12 * 666643;
  s1 += s12 * 470296;
  s2 += s12 * 654183;
  s3 -= s12 * 997805;
  s4 += s12 * 136657;
  s5 -= s12 * 683901;
  s12 = 0;
  carry0 = (s0 + (1LL << 20)) >> 21;
  s1 += carry0;
  s0 -= carry0 << 21;
  carry2 = (s2 + (1LL << 20)) >> 21;
  s3 += carry2;
  s2 -= carry2 << 21;
  carry4 = (s4 + (1LL << 20)) >> 21;
  s5 += carry4;
  s4 -= carry4 << 21;
  carry6 = (s6 + (1LL << 20)) >> 21;
  s7 += carry6;
  s6 -= carry6 << 21;
  carry8 = (s8 + (1LL << 20)) >> 21;
  s9 += carry8;
  s8 -= carry8 << 21;
  carry10 = (s10 + (1LL << 20)) >> 21;
  s11 += carry10;
  s10 -= carry10 << 21;
  carry1 = (s1 + (1LL << 20)) >> 21;
  s2 += carry1;
  s1 -= carry1 << 21;
  carry3 = (s3 + (1LL << 20)) >> 21;
  s4 += carry3;
  s3 -= carry3 << 21;
  carry5 = (s5 + (1LL << 20)) >> 21;
  s6 += carry5;
  s5 -= carry5 << 21;
  carry7 = (s7 + (1LL << 20)) >> 21;
  s8 += carry7;
  s7 -= carry7 << 21;
  carry9 = (s9 + (1LL << 20)) >> 21;
  s10 += carry9;
  s9 -= carry9 << 21;
  carry11 = (s11 + (1LL << 20)) >> 21;
  s12 += carry11;
  s11 -= carry11 << 21;
  s0 += s12 * 666643;
  s1 += s12 * 470296;
  s2 += s12 * 654183;
  s3 -= s12 * 997805;
  s4 += s12 * 136657;
  s5 -= s12 * 683901;
  s12 = 0;
  carry0 = s0 >> 21;
  s1 += carry0;
  s0 -= carry0 << 21;
  carry1 = s1 >> 21;
  s2 += carry1;
  s1 -= carry1 << 21;
  carry2 = s2 >> 21;
  s3 += carry2;
  s2 -= carry2 << 21;
  carry3 = s3 >> 21;
  s4 += carry3;
  s3 -= carry3 << 21;
  carry4 = s4 >> 21;
  s5 += carry4;
  s4 -= carry4 << 21;
  carry5 = s5 >> 21;
  s6 += carry5;
  s5 -= carry5 << 21;
  carry6 = s6 >> 21;
  s7 += carry6;
  s6 -= carry6 << 21;
  carry7 = s7 >> 21;
  s8 += carry7;
  s7 -= carry7 << 21;
  carry8 = s8 >> 21;
  s9 += carry8;
  s8 -= carry8 << 21;
  carry9 = s9 >> 21;
  s10 += carry9;
  s9 -= carry9 << 21;
  carry10 = s10 >> 21;
  s11 += carry10;
  s10 -= carry10 << 21;
  carry11 = s11 >> 21;
  s12 += carry11;
  s11 -= carry11 << 21;
  s0 += s12 * 666643;
  s1 += s12 * 470296;
  s2 += s12 * 654183;
  s3 -= s12 * 997805;
  s4 += s12 * 136657;
  s5 -= s12 * 683901;
  s12 = 0;
  carry0 = s0 >> 21;
  s1 += carry0;
  s0 -= carry0 << 21;
  carry1 = s1 >> 21;
  s2 += carry1;
  s1 -= carry1 << 21;
  carry2 = s2 >> 21;
  s3 += carry2;
  s2 -= carry2 << 21;
  carry3 = s3 >> 21;
  s4 += carry3;
  s3 -= carry3 << 21;
  carry4 = s4 >> 21;
  s5 += carry4;
  s4 -= carry4 << 21;
  carry5 = s5 >> 21;
  s6 += carry5;
  s5 -= carry5 << 21;
  carry6 = s6 >> 21;
  s7 += carry6;
  s6 -= carry6 << 21;
  carry7 = s7 >> 21;
  s8 += carry7;
  s7 -= carry7 << 21;
  carry8 = s8 >> 21;
  s9 += carry8;
  s8 -= carry8 << 21;
  carry9 = s9 >> 21;
  s10 += carry9;
  s9 -= carry9 << 21;
  carry10 = s10 >> 21;
  s11 += carry10;
  s10 -= carry10 << 21;

  out[0] = (uint8_t)(s0 >> 0);
  out[1] = (uint8_t)(s0 >> 8);
  out[2] = (uint8_t)((s0 >> 16) | (s1 << 5));
  out[3] = (uint8_t)(s1 >> 3);
  out[4] = (uint8_t)(s1 >> 11);
  out[5] = (uint8_t)((s1 >> 19) | (s2 << 2));
  out[6] = (uint8_t)(s2 >> 6);
  out[7] = (uint8_t)((s2 >> 14) | (s3 << 7));
  out[8] = (uint8_t)(s3 >> 1);
  out[9] = (uint8_t)(s3 >> 9);
  out[10] = (uint8_t)((s3 >> 17) | (s4 << 4));
  out[11] = (uint8_t)(s4 >> 4);
  out[12] = (uint8_t)(s4 >> 12);
  out[13] = (uint8_t)((s4 >> 20) | (s5 << 1));
  out[14] = (uint8_t)(s5 >> 7);
  out[15] = (uint8_t)((s5 >> 15) | (s6 << 6));
  out[16] = (uint8_t)(s6 >> 2);
  out[17] = (uint8_t)(s6 >> 10);
  out[18] = (uint8_t)((s6 >> 18) | (s7 << 3));
  out[19] = (uint8_t)(s7 >> 5);
  out[20] = (uint8_t)(s7 >> 13);
  out[21] = (uint8_t)(s8 >> 0);
  out[22] = (uint8_t)(s8 >> 8);
  out[23] = (uint8_t)((s8 >> 16) | (s9 << 5));
  out[24] = (uint8_t)(s9 >> 3);
  out[25] = (uint8_t)(s9 >> 11);
  out[26] = (uint8_t)((s9 >> 19) | (s10 << 2));
  out[27] = (uint8_t)(s10 >> 6);
  out[28] = (uint8_t)((s10 >> 14) | (s11 << 7));
  out[29] = (uint8_t)(s11 >> 1);
  out[30] = (uint8_t)(s11 >> 9);
  out[31] = (uint8_t)(s11 >> 17);
}

/*
 * sc_muladd: out = (a*b + c) mod L. Porte de ref10/sc25519.c.
 * Todos os scalars sao 32 bytes LE em [0, L).
 */
static void sc_muladd(uint8_t out[32], const uint8_t a[32],
                      const uint8_t b[32], const uint8_t c[32]) {
  int64_t a0 = 2097151 & load_3(a);
  int64_t a1 = 2097151 & (load_4(a + 2) >> 5);
  int64_t a2 = 2097151 & (load_3(a + 5) >> 2);
  int64_t a3 = 2097151 & (load_4(a + 7) >> 7);
  int64_t a4 = 2097151 & (load_4(a + 10) >> 4);
  int64_t a5 = 2097151 & (load_3(a + 13) >> 1);
  int64_t a6 = 2097151 & (load_4(a + 15) >> 6);
  int64_t a7 = 2097151 & (load_3(a + 18) >> 3);
  int64_t a8 = 2097151 & load_3(a + 21);
  int64_t a9 = 2097151 & (load_4(a + 23) >> 5);
  int64_t a10 = 2097151 & (load_3(a + 26) >> 2);
  int64_t a11 = (load_4(a + 28) >> 7);
  int64_t b0 = 2097151 & load_3(b);
  int64_t b1 = 2097151 & (load_4(b + 2) >> 5);
  int64_t b2 = 2097151 & (load_3(b + 5) >> 2);
  int64_t b3 = 2097151 & (load_4(b + 7) >> 7);
  int64_t b4 = 2097151 & (load_4(b + 10) >> 4);
  int64_t b5 = 2097151 & (load_3(b + 13) >> 1);
  int64_t b6 = 2097151 & (load_4(b + 15) >> 6);
  int64_t b7 = 2097151 & (load_3(b + 18) >> 3);
  int64_t b8 = 2097151 & load_3(b + 21);
  int64_t b9 = 2097151 & (load_4(b + 23) >> 5);
  int64_t b10 = 2097151 & (load_3(b + 26) >> 2);
  int64_t b11 = (load_4(b + 28) >> 7);
  int64_t c0 = 2097151 & load_3(c);
  int64_t c1 = 2097151 & (load_4(c + 2) >> 5);
  int64_t c2 = 2097151 & (load_3(c + 5) >> 2);
  int64_t c3 = 2097151 & (load_4(c + 7) >> 7);
  int64_t c4 = 2097151 & (load_4(c + 10) >> 4);
  int64_t c5 = 2097151 & (load_3(c + 13) >> 1);
  int64_t c6 = 2097151 & (load_4(c + 15) >> 6);
  int64_t c7 = 2097151 & (load_3(c + 18) >> 3);
  int64_t c8 = 2097151 & load_3(c + 21);
  int64_t c9 = 2097151 & (load_4(c + 23) >> 5);
  int64_t c10 = 2097151 & (load_3(c + 26) >> 2);
  int64_t c11 = (load_4(c + 28) >> 7);
  int64_t s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
  int64_t s12, s13, s14, s15, s16, s17, s18, s19, s20, s21, s22, s23;
  int64_t carry0, carry1, carry2, carry3, carry4, carry5, carry6;
  int64_t carry7, carry8, carry9, carry10, carry11, carry12, carry13;
  int64_t carry14, carry15, carry16, carry17, carry18, carry19, carry20;
  int64_t carry21, carry22;

  s0 = c0 + a0 * b0;
  s1 = c1 + a0 * b1 + a1 * b0;
  s2 = c2 + a0 * b2 + a1 * b1 + a2 * b0;
  s3 = c3 + a0 * b3 + a1 * b2 + a2 * b1 + a3 * b0;
  s4 = c4 + a0 * b4 + a1 * b3 + a2 * b2 + a3 * b1 + a4 * b0;
  s5 = c5 + a0 * b5 + a1 * b4 + a2 * b3 + a3 * b2 + a4 * b1 + a5 * b0;
  s6 = c6 + a0 * b6 + a1 * b5 + a2 * b4 + a3 * b3 + a4 * b2 + a5 * b1 +
       a6 * b0;
  s7 = c7 + a0 * b7 + a1 * b6 + a2 * b5 + a3 * b4 + a4 * b3 + a5 * b2 +
       a6 * b1 + a7 * b0;
  s8 = c8 + a0 * b8 + a1 * b7 + a2 * b6 + a3 * b5 + a4 * b4 + a5 * b3 +
       a6 * b2 + a7 * b1 + a8 * b0;
  s9 = c9 + a0 * b9 + a1 * b8 + a2 * b7 + a3 * b6 + a4 * b5 + a5 * b4 +
       a6 * b3 + a7 * b2 + a8 * b1 + a9 * b0;
  s10 = c10 + a0 * b10 + a1 * b9 + a2 * b8 + a3 * b7 + a4 * b6 +
        a5 * b5 + a6 * b4 + a7 * b3 + a8 * b2 + a9 * b1 + a10 * b0;
  s11 = c11 + a0 * b11 + a1 * b10 + a2 * b9 + a3 * b8 + a4 * b7 +
        a5 * b6 + a6 * b5 + a7 * b4 + a8 * b3 + a9 * b2 + a10 * b1 +
        a11 * b0;
  s12 = a1 * b11 + a2 * b10 + a3 * b9 + a4 * b8 + a5 * b7 + a6 * b6 +
        a7 * b5 + a8 * b4 + a9 * b3 + a10 * b2 + a11 * b1;
  s13 = a2 * b11 + a3 * b10 + a4 * b9 + a5 * b8 + a6 * b7 + a7 * b6 +
        a8 * b5 + a9 * b4 + a10 * b3 + a11 * b2;
  s14 = a3 * b11 + a4 * b10 + a5 * b9 + a6 * b8 + a7 * b7 + a8 * b6 +
        a9 * b5 + a10 * b4 + a11 * b3;
  s15 = a4 * b11 + a5 * b10 + a6 * b9 + a7 * b8 + a8 * b7 + a9 * b6 +
        a10 * b5 + a11 * b4;
  s16 = a5 * b11 + a6 * b10 + a7 * b9 + a8 * b8 + a9 * b7 + a10 * b6 +
        a11 * b5;
  s17 = a6 * b11 + a7 * b10 + a8 * b9 + a9 * b8 + a10 * b7 + a11 * b6;
  s18 = a7 * b11 + a8 * b10 + a9 * b9 + a10 * b8 + a11 * b7;
  s19 = a8 * b11 + a9 * b10 + a10 * b9 + a11 * b8;
  s20 = a9 * b11 + a10 * b10 + a11 * b9;
  s21 = a10 * b11 + a11 * b10;
  s22 = a11 * b11;
  s23 = 0;

  carry0 = (s0 + (1LL << 20)) >> 21;
  s1 += carry0;
  s0 -= carry0 << 21;
  carry2 = (s2 + (1LL << 20)) >> 21;
  s3 += carry2;
  s2 -= carry2 << 21;
  carry4 = (s4 + (1LL << 20)) >> 21;
  s5 += carry4;
  s4 -= carry4 << 21;
  carry6 = (s6 + (1LL << 20)) >> 21;
  s7 += carry6;
  s6 -= carry6 << 21;
  carry8 = (s8 + (1LL << 20)) >> 21;
  s9 += carry8;
  s8 -= carry8 << 21;
  carry10 = (s10 + (1LL << 20)) >> 21;
  s11 += carry10;
  s10 -= carry10 << 21;
  carry12 = (s12 + (1LL << 20)) >> 21;
  s13 += carry12;
  s12 -= carry12 << 21;
  carry14 = (s14 + (1LL << 20)) >> 21;
  s15 += carry14;
  s14 -= carry14 << 21;
  carry16 = (s16 + (1LL << 20)) >> 21;
  s17 += carry16;
  s16 -= carry16 << 21;
  carry18 = (s18 + (1LL << 20)) >> 21;
  s19 += carry18;
  s18 -= carry18 << 21;
  carry20 = (s20 + (1LL << 20)) >> 21;
  s21 += carry20;
  s20 -= carry20 << 21;
  carry22 = (s22 + (1LL << 20)) >> 21;
  s23 += carry22;
  s22 -= carry22 << 21;
  carry1 = (s1 + (1LL << 20)) >> 21;
  s2 += carry1;
  s1 -= carry1 << 21;
  carry3 = (s3 + (1LL << 20)) >> 21;
  s4 += carry3;
  s3 -= carry3 << 21;
  carry5 = (s5 + (1LL << 20)) >> 21;
  s6 += carry5;
  s5 -= carry5 << 21;
  carry7 = (s7 + (1LL << 20)) >> 21;
  s8 += carry7;
  s7 -= carry7 << 21;
  carry9 = (s9 + (1LL << 20)) >> 21;
  s10 += carry9;
  s9 -= carry9 << 21;
  carry11 = (s11 + (1LL << 20)) >> 21;
  s12 += carry11;
  s11 -= carry11 << 21;
  carry13 = (s13 + (1LL << 20)) >> 21;
  s14 += carry13;
  s13 -= carry13 << 21;
  carry15 = (s15 + (1LL << 20)) >> 21;
  s16 += carry15;
  s15 -= carry15 << 21;
  carry17 = (s17 + (1LL << 20)) >> 21;
  s18 += carry17;
  s17 -= carry17 << 21;
  carry19 = (s19 + (1LL << 20)) >> 21;
  s20 += carry19;
  s19 -= carry19 << 21;
  carry21 = (s21 + (1LL << 20)) >> 21;
  s22 += carry21;
  s21 -= carry21 << 21;

  s11 += s23 * 666643;
  s12 += s23 * 470296;
  s13 += s23 * 654183;
  s14 -= s23 * 997805;
  s15 += s23 * 136657;
  s16 -= s23 * 683901;
  s23 = 0;
  s10 += s22 * 666643;
  s11 += s22 * 470296;
  s12 += s22 * 654183;
  s13 -= s22 * 997805;
  s14 += s22 * 136657;
  s15 -= s22 * 683901;
  s22 = 0;
  s9 += s21 * 666643;
  s10 += s21 * 470296;
  s11 += s21 * 654183;
  s12 -= s21 * 997805;
  s13 += s21 * 136657;
  s14 -= s21 * 683901;
  s21 = 0;
  s8 += s20 * 666643;
  s9 += s20 * 470296;
  s10 += s20 * 654183;
  s11 -= s20 * 997805;
  s12 += s20 * 136657;
  s13 -= s20 * 683901;
  s20 = 0;
  s7 += s19 * 666643;
  s8 += s19 * 470296;
  s9 += s19 * 654183;
  s10 -= s19 * 997805;
  s11 += s19 * 136657;
  s12 -= s19 * 683901;
  s19 = 0;
  s6 += s18 * 666643;
  s7 += s18 * 470296;
  s8 += s18 * 654183;
  s9 -= s18 * 997805;
  s10 += s18 * 136657;
  s11 -= s18 * 683901;
  s18 = 0;
  carry6 = (s6 + (1LL << 20)) >> 21;
  s7 += carry6;
  s6 -= carry6 << 21;
  carry8 = (s8 + (1LL << 20)) >> 21;
  s9 += carry8;
  s8 -= carry8 << 21;
  carry10 = (s10 + (1LL << 20)) >> 21;
  s11 += carry10;
  s10 -= carry10 << 21;
  carry12 = (s12 + (1LL << 20)) >> 21;
  s13 += carry12;
  s12 -= carry12 << 21;
  carry14 = (s14 + (1LL << 20)) >> 21;
  s15 += carry14;
  s14 -= carry14 << 21;
  carry16 = (s16 + (1LL << 20)) >> 21;
  s17 += carry16;
  s16 -= carry16 << 21;
  carry7 = (s7 + (1LL << 20)) >> 21;
  s8 += carry7;
  s7 -= carry7 << 21;
  carry9 = (s9 + (1LL << 20)) >> 21;
  s10 += carry9;
  s9 -= carry9 << 21;
  carry11 = (s11 + (1LL << 20)) >> 21;
  s12 += carry11;
  s11 -= carry11 << 21;
  carry13 = (s13 + (1LL << 20)) >> 21;
  s14 += carry13;
  s13 -= carry13 << 21;
  carry15 = (s15 + (1LL << 20)) >> 21;
  s16 += carry15;
  s15 -= carry15 << 21;

  s5 += s17 * 666643;
  s6 += s17 * 470296;
  s7 += s17 * 654183;
  s8 -= s17 * 997805;
  s9 += s17 * 136657;
  s10 -= s17 * 683901;
  s17 = 0;
  s4 += s16 * 666643;
  s5 += s16 * 470296;
  s6 += s16 * 654183;
  s7 -= s16 * 997805;
  s8 += s16 * 136657;
  s9 -= s16 * 683901;
  s16 = 0;
  s3 += s15 * 666643;
  s4 += s15 * 470296;
  s5 += s15 * 654183;
  s6 -= s15 * 997805;
  s7 += s15 * 136657;
  s8 -= s15 * 683901;
  s15 = 0;
  s2 += s14 * 666643;
  s3 += s14 * 470296;
  s4 += s14 * 654183;
  s5 -= s14 * 997805;
  s6 += s14 * 136657;
  s7 -= s14 * 683901;
  s14 = 0;
  s1 += s13 * 666643;
  s2 += s13 * 470296;
  s3 += s13 * 654183;
  s4 -= s13 * 997805;
  s5 += s13 * 136657;
  s6 -= s13 * 683901;
  s13 = 0;
  s0 += s12 * 666643;
  s1 += s12 * 470296;
  s2 += s12 * 654183;
  s3 -= s12 * 997805;
  s4 += s12 * 136657;
  s5 -= s12 * 683901;
  s12 = 0;

  carry0 = (s0 + (1LL << 20)) >> 21;
  s1 += carry0;
  s0 -= carry0 << 21;
  carry2 = (s2 + (1LL << 20)) >> 21;
  s3 += carry2;
  s2 -= carry2 << 21;
  carry4 = (s4 + (1LL << 20)) >> 21;
  s5 += carry4;
  s4 -= carry4 << 21;
  carry6 = (s6 + (1LL << 20)) >> 21;
  s7 += carry6;
  s6 -= carry6 << 21;
  carry8 = (s8 + (1LL << 20)) >> 21;
  s9 += carry8;
  s8 -= carry8 << 21;
  carry10 = (s10 + (1LL << 20)) >> 21;
  s11 += carry10;
  s10 -= carry10 << 21;
  carry1 = (s1 + (1LL << 20)) >> 21;
  s2 += carry1;
  s1 -= carry1 << 21;
  carry3 = (s3 + (1LL << 20)) >> 21;
  s4 += carry3;
  s3 -= carry3 << 21;
  carry5 = (s5 + (1LL << 20)) >> 21;
  s6 += carry5;
  s5 -= carry5 << 21;
  carry7 = (s7 + (1LL << 20)) >> 21;
  s8 += carry7;
  s7 -= carry7 << 21;
  carry9 = (s9 + (1LL << 20)) >> 21;
  s10 += carry9;
  s9 -= carry9 << 21;
  carry11 = (s11 + (1LL << 20)) >> 21;
  s12 += carry11;
  s11 -= carry11 << 21;
  s0 += s12 * 666643;
  s1 += s12 * 470296;
  s2 += s12 * 654183;
  s3 -= s12 * 997805;
  s4 += s12 * 136657;
  s5 -= s12 * 683901;
  s12 = 0;
  carry0 = s0 >> 21;
  s1 += carry0;
  s0 -= carry0 << 21;
  carry1 = s1 >> 21;
  s2 += carry1;
  s1 -= carry1 << 21;
  carry2 = s2 >> 21;
  s3 += carry2;
  s2 -= carry2 << 21;
  carry3 = s3 >> 21;
  s4 += carry3;
  s3 -= carry3 << 21;
  carry4 = s4 >> 21;
  s5 += carry4;
  s4 -= carry4 << 21;
  carry5 = s5 >> 21;
  s6 += carry5;
  s5 -= carry5 << 21;
  carry6 = s6 >> 21;
  s7 += carry6;
  s6 -= carry6 << 21;
  carry7 = s7 >> 21;
  s8 += carry7;
  s7 -= carry7 << 21;
  carry8 = s8 >> 21;
  s9 += carry8;
  s8 -= carry8 << 21;
  carry9 = s9 >> 21;
  s10 += carry9;
  s9 -= carry9 << 21;
  carry10 = s10 >> 21;
  s11 += carry10;
  s10 -= carry10 << 21;
  carry11 = s11 >> 21;
  s12 += carry11;
  s11 -= carry11 << 21;
  s0 += s12 * 666643;
  s1 += s12 * 470296;
  s2 += s12 * 654183;
  s3 -= s12 * 997805;
  s4 += s12 * 136657;
  s5 -= s12 * 683901;
  s12 = 0;
  carry0 = s0 >> 21;
  s1 += carry0;
  s0 -= carry0 << 21;
  carry1 = s1 >> 21;
  s2 += carry1;
  s1 -= carry1 << 21;
  carry2 = s2 >> 21;
  s3 += carry2;
  s2 -= carry2 << 21;
  carry3 = s3 >> 21;
  s4 += carry3;
  s3 -= carry3 << 21;
  carry4 = s4 >> 21;
  s5 += carry4;
  s4 -= carry4 << 21;
  carry5 = s5 >> 21;
  s6 += carry5;
  s5 -= carry5 << 21;
  carry6 = s6 >> 21;
  s7 += carry6;
  s6 -= carry6 << 21;
  carry7 = s7 >> 21;
  s8 += carry7;
  s7 -= carry7 << 21;
  carry8 = s8 >> 21;
  s9 += carry8;
  s8 -= carry8 << 21;
  carry9 = s9 >> 21;
  s10 += carry9;
  s9 -= carry9 << 21;
  carry10 = s10 >> 21;
  s11 += carry10;
  s10 -= carry10 << 21;

  out[0] = (uint8_t)(s0 >> 0);
  out[1] = (uint8_t)(s0 >> 8);
  out[2] = (uint8_t)((s0 >> 16) | (s1 << 5));
  out[3] = (uint8_t)(s1 >> 3);
  out[4] = (uint8_t)(s1 >> 11);
  out[5] = (uint8_t)((s1 >> 19) | (s2 << 2));
  out[6] = (uint8_t)(s2 >> 6);
  out[7] = (uint8_t)((s2 >> 14) | (s3 << 7));
  out[8] = (uint8_t)(s3 >> 1);
  out[9] = (uint8_t)(s3 >> 9);
  out[10] = (uint8_t)((s3 >> 17) | (s4 << 4));
  out[11] = (uint8_t)(s4 >> 4);
  out[12] = (uint8_t)(s4 >> 12);
  out[13] = (uint8_t)((s4 >> 20) | (s5 << 1));
  out[14] = (uint8_t)(s5 >> 7);
  out[15] = (uint8_t)((s5 >> 15) | (s6 << 6));
  out[16] = (uint8_t)(s6 >> 2);
  out[17] = (uint8_t)(s6 >> 10);
  out[18] = (uint8_t)((s6 >> 18) | (s7 << 3));
  out[19] = (uint8_t)(s7 >> 5);
  out[20] = (uint8_t)(s7 >> 13);
  out[21] = (uint8_t)(s8 >> 0);
  out[22] = (uint8_t)(s8 >> 8);
  out[23] = (uint8_t)((s8 >> 16) | (s9 << 5));
  out[24] = (uint8_t)(s9 >> 3);
  out[25] = (uint8_t)(s9 >> 11);
  out[26] = (uint8_t)((s9 >> 19) | (s10 << 2));
  out[27] = (uint8_t)(s10 >> 6);
  out[28] = (uint8_t)((s10 >> 14) | (s11 << 7));
  out[29] = (uint8_t)(s11 >> 1);
  out[30] = (uint8_t)(s11 >> 9);
  out[31] = (uint8_t)(s11 >> 17);
}

/*
 * sc_is_canonical: retorna 1 se s em [0, L), 0 cc. Constant-time.
 */
static int sc_is_canonical(const uint8_t s[32]) {
  /* Compare s vs L (LE byte arrays). Walk from MSB to LSB. */
  uint8_t accept = 0u; /* 0 = undecided, 1 = s < L confirmed */
  uint8_t reject = 0u; /* 1 = s >= L confirmed */
  for (int i = 31; i >= 0; --i) {
    uint8_t si = s[i];
    uint8_t li = ED_L_BYTES[i];
    /* if undecided: if si < li, accept; if si > li, reject; equal -> continue */
    uint8_t lt = (uint8_t)((si - li) >> 7); /* 1 if si<li, else 0 (works for unsigned subtract bit) */
    uint8_t gt = (uint8_t)((li - si) >> 7);
    uint8_t undecided = (uint8_t)(1u - (accept | reject));
    accept |= undecided & lt;
    reject |= undecided & gt;
  }
  /* If neither accept nor reject (s == L), reject. */
  return accept ? 1 : 0;
}

/* ============================================================
 * Helpers SHA-512.
 * ============================================================ */

static void sha512_two_block(uint8_t out[64], const uint8_t *a, size_t alen,
                             const uint8_t *b, size_t blen) {
  struct sha512_ctx ctx;
  sha512_init(&ctx);
  if (a && alen) {
    sha512_update(&ctx, a, alen);
  }
  if (b && blen) {
    sha512_update(&ctx, b, blen);
  }
  sha512_final(&ctx, out);
  volatile uint8_t *p = (volatile uint8_t *)&ctx;
  for (size_t i = 0; i < sizeof(ctx); ++i) {
    p[i] = 0u;
  }
}

static void sha512_three_block(uint8_t out[64],
                               const uint8_t *a, size_t alen,
                               const uint8_t *b, size_t blen,
                               const uint8_t *c, size_t clen) {
  struct sha512_ctx ctx;
  sha512_init(&ctx);
  if (a && alen) {
    sha512_update(&ctx, a, alen);
  }
  if (b && blen) {
    sha512_update(&ctx, b, blen);
  }
  if (c && clen) {
    sha512_update(&ctx, c, clen);
  }
  sha512_final(&ctx, out);
  volatile uint8_t *p = (volatile uint8_t *)&ctx;
  for (size_t i = 0; i < sizeof(ctx); ++i) {
    p[i] = 0u;
  }
}

/* ============================================================
 * APIs publicas (RFC 8032 §5.1.5 / §5.1.6 / §5.1.7).
 * ============================================================ */

void ed25519_create_keypair(uint8_t public_key[ED25519_PUBLIC_KEY_SIZE],
                            uint8_t private_key[ED25519_PRIVATE_KEY_SIZE],
                            const uint8_t seed[ED25519_SEED_SIZE]) {
  if (!public_key || !private_key || !seed) {
    if (public_key) {
      volatile uint8_t *pk = (volatile uint8_t *)public_key;
      for (size_t i = 0; i < ED25519_PUBLIC_KEY_SIZE; ++i) {
        pk[i] = 0u;
      }
    }
    if (private_key) {
      volatile uint8_t *sk = (volatile uint8_t *)private_key;
      for (size_t i = 0; i < ED25519_PRIVATE_KEY_SIZE; ++i) {
        sk[i] = 0u;
      }
    }
    return;
  }
  /* h = SHA-512(seed); s = clamped(h[0..32]); prefix = h[32..64] */
  uint8_t h[64];
  sha512_two_block(h, seed, 32, NULL, 0);
  uint8_t s[32];
  for (int i = 0; i < 32; ++i) {
    s[i] = h[i];
  }
  s[0] &= 248u;
  s[31] &= 127u;
  s[31] |= 64u;

  /* A = s*B, encoded compressed */
  ge_p3 A;
  ge_scalarmult_base(&A, s);
  ge_encode(public_key, &A);

  /* private_key = seed || public_key (per RFC 8032 convention used here) */
  for (int i = 0; i < 32; ++i) {
    private_key[i] = seed[i];
    private_key[32 + i] = public_key[i];
  }

  ge_wipe(&A);
  wipe_bytes(h, sizeof(h));
  wipe_bytes(s, sizeof(s));
}

void ed25519_sign(uint8_t signature[ED25519_SIGNATURE_SIZE],
                  const uint8_t *message, size_t message_len,
                  const uint8_t public_key[ED25519_PUBLIC_KEY_SIZE],
                  const uint8_t private_key[ED25519_PRIVATE_KEY_SIZE]) {
  if (!signature || !public_key || !private_key) {
    if (signature) {
      volatile uint8_t *o = (volatile uint8_t *)signature;
      for (size_t i = 0; i < ED25519_SIGNATURE_SIZE; ++i) {
        o[i] = 0u;
      }
    }
    return;
  }
  /* h = SHA-512(seed); s = clamp(h[0..32]); prefix = h[32..64] */
  uint8_t h[64];
  sha512_two_block(h, private_key, 32, NULL, 0);
  uint8_t s[32];
  for (int i = 0; i < 32; ++i) {
    s[i] = h[i];
  }
  s[0] &= 248u;
  s[31] &= 127u;
  s[31] |= 64u;

  /* r = SHA-512(prefix || M); r = r mod L */
  uint8_t r_wide[64];
  sha512_two_block(r_wide, h + 32, 32, message, message_len);
  uint8_t r[32];
  sc_reduce64(r, r_wide);

  /* R = r * B */
  ge_p3 R;
  ge_scalarmult_base(&R, r);
  ge_encode(signature, &R); /* signature[0..32] = R */

  /* k = SHA-512(R || A || M); k = k mod L */
  uint8_t k_wide[64];
  sha512_three_block(k_wide, signature, 32, public_key, 32, message,
                     message_len);
  uint8_t k[32];
  sc_reduce64(k, k_wide);

  /* S = (r + k*s) mod L */
  sc_muladd(signature + 32, k, s, r);

  /* Wipe intermediarios. */
  ge_wipe(&R);
  wipe_bytes(h, sizeof(h));
  wipe_bytes(s, sizeof(s));
  wipe_bytes(r_wide, sizeof(r_wide));
  wipe_bytes(r, sizeof(r));
  wipe_bytes(k_wide, sizeof(k_wide));
  wipe_bytes(k, sizeof(k));
  (void)public_key;
}

int ed25519_verify(const uint8_t signature[ED25519_SIGNATURE_SIZE],
                   const uint8_t *message, size_t message_len,
                   const uint8_t public_key[ED25519_PUBLIC_KEY_SIZE]) {
  if (!signature || !public_key) {
    return -1;
  }
  /* Step 1: R = signature[0..32], decoded; S = signature[32..64]. */
  const uint8_t *R_bytes = signature;
  const uint8_t *S_bytes = signature + 32;

  /* S must be canonical (in [0, L)). Reject if S >= L. */
  if (!sc_is_canonical(S_bytes)) {
    return -1;
  }
  /* Reject signatures where R fails decoding. */
  ge_p3 R, A;
  if (ge_decode(&R, R_bytes) != 0) {
    return -1;
  }
  if (ge_decode(&A, public_key) != 0) {
    ge_wipe(&R);
    return -1;
  }
  /* k = SHA-512(R || A || M); k = k mod L */
  uint8_t k_wide[64];
  sha512_three_block(k_wide, R_bytes, 32, public_key, 32, message,
                     message_len);
  uint8_t k[32];
  sc_reduce64(k, k_wide);

  /* Compute check = S*B - k*A. Verifier accepts iff check == R
   * after multiplication by cofactor 8 (RFC 8032 §5.1.7 step 4
   * mandates checking [8]SB == [8]R + [8](kA)). */
  ge_p3 minus_A;
  ge_neg_p(&minus_A, &A);

  ge_p3 check;
  ge_double_scalarmult(&check, k, &minus_A, S_bytes);

  /* Multiply both R and check by 8 (cofactor) and compare. */
  ge_p3 R8, check8;
  ge_dbl(&R8, &R);
  ge_dbl(&R8, &R8);
  ge_dbl(&R8, &R8);
  ge_dbl(&check8, &check);
  ge_dbl(&check8, &check8);
  ge_dbl(&check8, &check8);

  /* Compare 8R and 8(SB - kA) via projective equality:
   *   R8.X * check8.Z == check8.X * R8.Z   AND
   *   R8.Y * check8.Z == check8.Y * R8.Z
   */
  fe t1, t2;
  fe_mul(&t1, &R8.X, &check8.Z);
  fe_mul(&t2, &check8.X, &R8.Z);
  int diffx = fe_notequal(&t1, &t2);
  fe_mul(&t1, &R8.Y, &check8.Z);
  fe_mul(&t2, &check8.Y, &R8.Z);
  int diffy = fe_notequal(&t1, &t2);

  int result = (diffx | diffy) == 0 ? 0 : -1;

  ge_wipe(&R);
  ge_wipe(&A);
  ge_wipe(&minus_A);
  ge_wipe(&check);
  ge_wipe(&R8);
  ge_wipe(&check8);
  fe_wipe(&t1);
  fe_wipe(&t2);
  wipe_bytes(k_wide, sizeof(k_wide));
  wipe_bytes(k, sizeof(k));
  return result;
}
