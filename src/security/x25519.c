#include "security/x25519.h"
#include "security/fe25519.h"
#include <stddef.h>
#include <stdint.h>

/*
 * X25519 (RFC 7748): Elliptic Curve Diffie-Hellman sobre Curve25519.
 *
 * Implementacao auditavel constant-time:
 * - Field arithmetic GF(2^255 - 19) compartilhada com Ed25519 via
 *   include/security/fe25519.h (refatorada em alpha.217).
 * - Montgomery ladder de 255 iteracoes sem branches sobre bits
 *   secretos do scalar.
 * - Scalar clamping per RFC 7748 §5 (zera bits 0,1,2,255; seta
 *   bit 254 — cofator 8 da curva absorvido).
 * - U-coord top-bit masking per RFC 7748 §5 (fe_frombytes).
 * - Small-subgroup detection per RFC 7748 §6.1 (rejeita shared=0
 *   fail-closed).
 * - Wipe volatile-safe em todos os intermediarios em todos os
 *   exits.
 *
 * Threat model:
 * - Confidencialidade do shared sob CDH assumption.
 * - Resistente a small-order point attacks (gate shared!=0).
 * - Caller responsavel por autenticidade das public keys
 *   (Ed25519, certificate pinning, out-of-band).
 */

static void wipe_bytes(void *p, size_t n) {
  volatile uint8_t *vp = (volatile uint8_t *)p;
  while (n--) {
    *vp++ = 0u;
  }
}

/*
 * Montgomery ladder per RFC 7748 §5. Constante a24 = (486662 - 2) / 4
 * = 121665.
 */
static void x25519_ladder(fe *x_out, fe *z_out, const fe *u,
                          const uint8_t scalar[32]) {
  fe x_1;
  fe x_2, z_2;
  fe x_3, z_3;
  fe A, AA, B, BB, E, C, D, DA, CB, tmp1, tmp2;

  fe_copy(&x_1, u);
  fe_one(&x_2);
  fe_zero(&z_2);
  fe_copy(&x_3, u);
  fe_one(&z_3);

  uint64_t swap = 0u;

  /* Iteracao do bit 254 ate o bit 0 (255 iteracoes total). O bit 255
   * e sempre 0 apos o clamping (scalar[31] & 127), entao pulamos. */
  for (int t = 254; t >= 0; --t) {
    uint64_t k_t = (scalar[t >> 3] >> (t & 7)) & 1u;
    swap ^= k_t;
    fe_cswap(swap, &x_2, &x_3);
    fe_cswap(swap, &z_2, &z_3);
    swap = k_t;

    fe_add(&A, &x_2, &z_2);
    fe_sq(&AA, &A);
    fe_sub(&B, &x_2, &z_2);
    fe_sq(&BB, &B);
    fe_sub(&E, &AA, &BB);
    fe_add(&C, &x_3, &z_3);
    fe_sub(&D, &x_3, &z_3);
    fe_mul(&DA, &D, &A);
    fe_mul(&CB, &C, &B);
    fe_add(&tmp1, &DA, &CB);
    fe_sq(&x_3, &tmp1);
    fe_sub(&tmp2, &DA, &CB);
    fe_sq(&tmp1, &tmp2);
    fe_mul(&z_3, &tmp1, &x_1);
    fe_mul(&x_2, &AA, &BB);
    fe_mul_small(&tmp1, &E, 121665u);
    fe_add(&tmp2, &AA, &tmp1);
    fe_mul(&z_2, &E, &tmp2);
  }
  fe_cswap(swap, &x_2, &x_3);
  fe_cswap(swap, &z_2, &z_3);

  fe_copy(x_out, &x_2);
  fe_copy(z_out, &z_2);

  /* Wipe todos os intermediarios sensiveis. */
  fe_wipe(&x_1);
  fe_wipe(&x_2);
  fe_wipe(&z_2);
  fe_wipe(&x_3);
  fe_wipe(&z_3);
  fe_wipe(&A);
  fe_wipe(&AA);
  fe_wipe(&B);
  fe_wipe(&BB);
  fe_wipe(&E);
  fe_wipe(&C);
  fe_wipe(&D);
  fe_wipe(&DA);
  fe_wipe(&CB);
  fe_wipe(&tmp1);
  fe_wipe(&tmp2);
}

static int x25519_internal(const uint8_t scalar[X25519_SCALAR_SIZE],
                           const uint8_t u_coord[X25519_POINT_SIZE],
                           uint8_t shared[X25519_POINT_SIZE]) {
  if (!scalar || !u_coord || !shared) {
    return -1;
  }

  /* Clamping per RFC 7748 §5. Trabalha em copia para nao mutar
   * o buffer do caller. */
  uint8_t e[32];
  for (int i = 0; i < 32; ++i) {
    e[i] = scalar[i];
  }
  e[0] &= 248u;
  e[31] &= 127u;
  e[31] |= 64u;

  fe u;
  fe_frombytes(&u, u_coord);

  fe x_res, z_res;
  x25519_ladder(&x_res, &z_res, &u, e);

  /* result = x_res / z_res */
  fe z_inv, out_fe;
  fe_invert(&z_inv, &z_res);
  fe_mul(&out_fe, &x_res, &z_inv);
  fe_tobytes(shared, &out_fe);

  /* Wipe intermediarios. */
  wipe_bytes(e, sizeof(e));
  fe_wipe(&u);
  fe_wipe(&x_res);
  fe_wipe(&z_res);
  fe_wipe(&z_inv);
  fe_wipe(&out_fe);

  return 0;
}

int x25519(const uint8_t scalar[X25519_SCALAR_SIZE],
           const uint8_t u_coord[X25519_POINT_SIZE],
           uint8_t shared[X25519_POINT_SIZE]) {
  if (x25519_internal(scalar, u_coord, shared) != 0) {
    return -1;
  }
  /* Small-subgroup detection per RFC 7748 §6.1: rejeita all-zero
   * shared para resistir a small-order point attacks. */
  uint8_t accum = 0u;
  for (int i = 0; i < 32; ++i) {
    accum |= shared[i];
  }
  if (accum == 0u) {
    for (int i = 0; i < 32; ++i) {
      ((volatile uint8_t *)shared)[i] = 0u;
    }
    return -1;
  }
  return 0;
}

int x25519_base(const uint8_t scalar[X25519_SCALAR_SIZE],
                uint8_t public_key[X25519_POINT_SIZE]) {
  if (!scalar || !public_key) {
    return -1;
  }
  /* Base point u = 9 (RFC 7748 §4.1). Base point tem ordem prima —
   * small-subgroup detection nao dispara em uso correto. */
  static const uint8_t k_base_point[32] = {
      9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  };
  return x25519_internal(scalar, k_base_point, public_key);
}
