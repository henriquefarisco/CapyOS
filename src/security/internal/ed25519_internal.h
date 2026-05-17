/*
 * src/security/internal/ed25519_internal.h
 *
 * Internal helpers shared inside the `security` module across the
 * Ed25519 translation units (`ed25519.c`, `ed25519_group.c`).
 *
 * NOT part of the public API: every consumer outside `src/security/`
 * must keep using the surface in `include/security/ed25519.h`.
 *
 * The declarations exposed here used to be `static` inside the
 * pre-split monolith `src/security/ed25519.c`. Exposing them as
 * extern is the only behavioural change in the PR A.1 split — the
 * implementations are byte-for-byte identical to the originals so
 * RFC 8032 §7 test vectors and the in-tree update verifier still
 * validate.
 *
 * See docs/plans/active/monolith-residual-dedicated-plan.md (§2,
 * Estagio A) for the full split rationale and validation gates.
 */
#ifndef SECURITY_INTERNAL_ED25519_INTERNAL_H
#define SECURITY_INTERNAL_ED25519_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "security/fe25519.h"

/* ----------------------------------------------------------------
 * Edwards point in extended coordinates (X:Y:Z:T), with T = X*Y/Z.
 *
 * Used by ed25519_group.c (the producer) and by ed25519.c (consumer
 * via encode/decode plus the public sign/verify entry points). The
 * field `T` carries the precomputed product X*Y/Z that the
 * Hisil-Wong-Carter-Dawson addition formulas require to stay at 8
 * multiplications instead of 9.
 * ---------------------------------------------------------------- */
typedef struct {
  fe X, Y, Z, T;
} ge_p3;

/* ----------------------------------------------------------------
 * Curve constants used by point decompression in `ed25519.c::ge_decode`.
 *
 * `ED_D`        : Edwards curve constant d = -121665/121666 mod p.
 * `ED_SQRTM1`   : sqrt(-1) mod p = 2^((p-1)/4), needed by the
 *                 point-decompression branch that recovers the x
 *                 coordinate from y.
 *
 * Both live inside `ed25519_group.c` and stay constant for the
 * lifetime of the binary. They are read-only and never wiped.
 * ---------------------------------------------------------------- */
extern const fe ED_D;
extern const fe ED_SQRTM1;

/* ----------------------------------------------------------------
 * Edwards group operations exposed to `ed25519.c`:
 *
 *   ge_wipe              : volatile-safe scrub of every limb of a
 *                          ge_p3 (used by create_keypair, sign and
 *                          verify after any sensitive intermediate).
 *   ge_dbl               : doubling on the twisted Edwards curve.
 *                          ed25519_verify needs 3 doublings on each
 *                          of R and check to apply the cofactor-8
 *                          multiplication mandated by RFC 8032 §5.1.7.
 *   ge_neg_p             : negate a point (-P = (-X, Y, Z, -T)),
 *                          used by ed25519_verify to compute -A
 *                          before the double-scalar multiplication.
 *   ge_scalarmult_base   : constant-time scalar*B with B = canonical
 *                          base point, used by create_keypair and
 *                          sign to derive the public point.
 *   ge_double_scalarmult : k*A + S*B for verify, accepts public
 *                          inputs; we still run constant-time as a
 *                          hygienic default since the same kernel
 *                          backs sign too.
 *
 * The remaining group helpers (`ge_zero`, `ge_copy`, `ge_add`,
 * `ge_cmov`, `ge_scalarmult`) stay `static` inside ed25519_group.c
 * because no other translation unit needs them. Keeping their scope
 * narrow protects the secret-scalar invariants.
 * ---------------------------------------------------------------- */
void ge_wipe(ge_p3 *r);
void ge_dbl(ge_p3 *r, const ge_p3 *p);
void ge_neg_p(ge_p3 *r, const ge_p3 *p);
void ge_scalarmult_base(ge_p3 *r, const uint8_t scalar[32]);
void ge_double_scalarmult(ge_p3 *r, const uint8_t a[32], const ge_p3 *A,
                          const uint8_t b[32]);

/* ----------------------------------------------------------------
 * Compressed point codec exposed by `ed25519_encode.c`:
 *
 *   ge_encode : produce the 32-byte little-endian compressed form
 *               (RFC 8032 §5.1.2) of `p`. The encoder normalises
 *               (X, Y, Z, T) by inverting Z so the byte stream is
 *               canonical even when callers pass projective points.
 *   ge_decode : recover an extended-coordinates point from the
 *               32-byte compressed form (RFC 8032 §5.1.3). Returns 0
 *               when the input round-trips through canonicalisation
 *               and the recovered x coordinate satisfies v*x^2 == ±u,
 *               -1 otherwise. Fails closed on every error path
 *               (non-canonical y, square-root absent, x=0 with sign
 *               bit set).
 *
 * Both are consumed by the public APIs in `ed25519.c`
 * (create_keypair, sign, verify) and never leave the security
 * module. They were extracted from the pre-split monolith in PR
 * A.2 of the dedicated plan; only the storage class changed from
 * `static` to extern.
 * ---------------------------------------------------------------- */
void ge_encode(uint8_t out[32], const ge_p3 *p);
int ge_decode(ge_p3 *r, const uint8_t in[32]);

/* ----------------------------------------------------------------
 * Scalar arithmetic modulo L exposed by `ed25519_scalar.c`:
 *
 *   sc_reduce64     : reduce 64 LE bytes (typically a SHA-512 digest)
 *                     to a canonical 32-byte scalar in [0, L). Used
 *                     by sign (for r = SHA-512(prefix || M) mod L and
 *                     for k = SHA-512(R || A || M) mod L) and verify
 *                     (for the verifier-side k recomputation).
 *   sc_muladd       : compute out = (a*b + c) mod L. Used by sign to
 *                     produce S = (r + k*s) mod L, where s is the
 *                     secret scalar.
 *   sc_is_canonical : returns 1 iff `s` interpreted as a little-endian
 *                     unsigned integer lies in [0, L). Constant-time.
 *                     Used by verify to reject non-canonical S
 *                     signature halves before any group work.
 *
 * The internal `load_3`/`load_4` byte readers and the `ED_L_BYTES`
 * order constant stay `static` inside `ed25519_scalar.c` because no
 * caller outside that file needs them. Keeping them private also
 * shields the modular-reduction implementation from accidental reuse
 * in callers that have not been audited against the ref10 layout.
 * ---------------------------------------------------------------- */
void sc_reduce64(uint8_t out[32], const uint8_t in[64]);
void sc_muladd(uint8_t out[32], const uint8_t a[32],
               const uint8_t b[32], const uint8_t c[32]);
int sc_is_canonical(const uint8_t s[32]);

#endif /* SECURITY_INTERNAL_ED25519_INTERNAL_H */
