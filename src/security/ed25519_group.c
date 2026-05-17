/*
 * src/security/ed25519_group.c
 *
 * Edwards25519 group layer extracted from the pre-split
 * `src/security/ed25519.c` monolith (PR A.1 of the dedicated
 * monolith residual plan).
 *
 * Hosts:
 *   - The curve constants ED_D / ED_D2 / ED_SQRTM1 and the base point
 *     coordinates ED_B_X / ED_B_Y / ED_B_T.
 *   - The twisted Edwards point arithmetic in extended coordinates
 *     (X:Y:Z:T) with a = -1: ge_zero, ge_copy, ge_wipe, ge_dbl,
 *     ge_add, ge_neg_p, ge_cmov.
 *   - The constant-time scalar multiplication kernel
 *     ge_scalarmult and its public wrappers ge_scalarmult_base and
 *     ge_double_scalarmult.
 *
 * The implementations are byte-for-byte identical to the
 * pre-split originals; only the storage class changed for the
 * symbols that are now consumed by `src/security/ed25519.c` (encode/
 * decode + RFC 8032 entry points). See
 * `src/security/internal/ed25519_internal.h` for the exposed surface
 * and `docs/plans/active/monolith-residual-dedicated-plan.md` (§2,
 * Estagio A.1) for the rationale.
 *
 * Threat model preserved from the monolith:
 *   - Constant-time over secret scalars (sign / create_keypair).
 *     The Montgomery-style double-and-add iterates a fixed 256-bit
 *     window and selects the result via fe_cmov, never branching on
 *     a secret bit.
 *   - Volatile-safe wipe of every intermediate ge_p3 in scalarmult
 *     and the doubling/addition primitives, including the temporary
 *     base point copy used by ge_scalarmult_base /
 *     ge_double_scalarmult.
 *   - No dynamic allocation; every scratch lives on the stack so the
 *     wipe is final once the function returns.
 */
#include "security/internal/ed25519_internal.h"

#include "security/fe25519.h"

#include <stddef.h>
#include <stdint.h>

/* ============================================================
 * Constantes do grupo Edwards25519.
 * Valores cross-verificados contra dalek-cryptography (5x51-bit limbs)
 * e RFC 8032 (encoded form do base point B).
 * ============================================================ */

/* d = -121665/121666 mod p (twisted Edwards curve constant). */
const fe ED_D = {{
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
const fe ED_SQRTM1 = {{
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

/* ============================================================
 * Edwards point in extended coordinates (X:Y:Z:T), T = X*Y/Z.
 *
 * The typedef now lives in `internal/ed25519_internal.h` so encode/
 * decode and the public APIs can reference the same struct without
 * peeking into this translation unit.
 * ============================================================ */

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

void ge_wipe(ge_p3 *r) {
  fe_wipe(&r->X);
  fe_wipe(&r->Y);
  fe_wipe(&r->Z);
  fe_wipe(&r->T);
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
void ge_dbl(ge_p3 *r, const ge_p3 *p) {
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
void ge_neg_p(ge_p3 *r, const ge_p3 *p) {
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

void ge_scalarmult_base(ge_p3 *r, const uint8_t scalar[32]) {
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
void ge_double_scalarmult(ge_p3 *r,
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
