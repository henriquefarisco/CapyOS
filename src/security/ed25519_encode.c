/*
 * src/security/ed25519_encode.c
 *
 * Edwards25519 compressed-point codec extracted from the pre-split
 * `src/security/ed25519.c` monolith (PR A.2 of the dedicated
 * monolith residual plan).
 *
 * Hosts:
 *   - `ge_encode` : extended-coordinates -> 32-byte compressed form
 *     (RFC 8032 §5.1.2).
 *   - `ge_decode` : 32-byte compressed form -> extended-coordinates
 *     (RFC 8032 §5.1.3) with fail-closed validation.
 *
 * The implementations are byte-for-byte identical to the
 * pre-split originals; only the storage class changed for the
 * symbols that are now consumed by `src/security/ed25519.c` (public
 * sign/verify entry points). See
 * `src/security/internal/ed25519_internal.h` for the exposed surface
 * and `docs/plans/active/monolith-residual-dedicated-plan.md` (§2,
 * Estagio A.2) for the rationale.
 *
 * Threat model preserved from the monolith:
 *   - Constant-time decode where it matters for secret material:
 *     `ge_decode` is invoked on the public key during sign (so the
 *     input is not secret) and on the signature R during verify (also
 *     public). The variable-time branches are therefore acceptable
 *     here; we still wipe every intermediate `fe` to keep
 *     memory hygiene aligned with the rest of the module.
 *   - Fail-closed on:
 *       * non-canonical y encoding (re-encode mismatch);
 *       * absent square root (v*x^2 differs from both u and -u);
 *       * x=0 paired with sign bit 1 (RFC 8032 §5.1.3 step 5).
 *   - No dynamic allocation; every scratch buffer lives on the stack
 *     so the wipe is final once the function returns.
 */
#include "security/internal/ed25519_internal.h"

#include "security/fe25519.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Volatile-safe scrub helper local to this translation unit. Mirrors
 * the `static` `wipe_bytes` in `ed25519.c` and the convention used in
 * `src/security/blake2b.c`, `argon2.c` and `x25519.c`. Duplicated by
 * design: each crypto translation unit owns its own scrubber to keep
 * the link-time symbol surface narrow and to make wipes inlinable
 * inside hot paths.
 */
static void wipe_bytes(void *p, size_t n) {
  volatile uint8_t *vp = (volatile uint8_t *)p;
  while (n--) {
    *vp++ = 0u;
  }
}

/* ============================================================
 * Encoding/decoding de pontos (compressed: 32 bytes).
 *
 * Encoded format (RFC 8032 §5.1.2):
 *   bytes[0..30] = y (LE) bits 0..247
 *   bytes[31] = bits 248..254 of y | (sign(x) << 7)
 * where sign(x) = x mod 2 (LSB of x).
 * ============================================================ */

void ge_encode(uint8_t out[32], const ge_p3 *p) {
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
int ge_decode(ge_p3 *r, const uint8_t in[32]) {
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
