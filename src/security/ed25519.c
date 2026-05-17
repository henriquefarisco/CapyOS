#include "security/ed25519.h"
#include "security/fe25519.h"
#include "security/internal/ed25519_internal.h"
#include "security/sha512.h"
#include <stddef.h>
#include <stdint.h>

/*
 * Ed25519 (RFC 8032) — implementacao auditavel constant-time.
 *
 * Substitui o esqueleto fail-closed que estava aqui desde alpha.210.
 * Field arithmetic GF(2^255-19) e fe25519 compartilhado com X25519.
 * Group arithmetic (constants, point ops, scalar mult) extraido em
 * `src/security/ed25519_group.c` na PR A.1 do plano dedicado de
 * monolitos residuais; surface visivel via
 * `src/security/internal/ed25519_internal.h`. As funcoes da camada
 * scalar arithmetic mod L, encode/decode e SHA-512 helpers permanecem
 * neste arquivo ate as PRs A.2 e A.3 do mesmo plano (ver
 * docs/plans/active/monolith-residual-dedicated-plan.md §2).
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

/* `ED_L_BYTES`, `load_3`/`load_4` e a aritmetica scalar mod L
 * (`sc_reduce64`, `sc_muladd`, `sc_is_canonical`) ficam em
 * `src/security/ed25519_scalar.c` desde a PR A.3; prototipos em
 * `internal/ed25519_internal.h`.
 *
 * Volatile-safe scrub helper local a este arquivo. Apaga buffers
 * sensiveis (hash scratch h[], reduced scalars s/k/r, intermediate
 * raw_bytes) com semantica que o compilador nao pode otimizar fora.
 * Mantido `static` porque a camada group em `ed25519_group.c` usa
 * `fe_wipe`/`ge_wipe` ao inves; ed25519_encode.c e ed25519_scalar.c
 * mantem copia local propria seguindo a convencao do projeto
 * (blake2b.c / argon2.c / x25519.c). */
static void wipe_bytes(void *p, size_t n) {
  volatile uint8_t *vp = (volatile uint8_t *)p;
  while (n--) {
    *vp++ = 0u;
  }
}

/* Encoding/decoding de pontos (RFC 8032 §5.1.2/§5.1.3) ficam em
 * `src/security/ed25519_encode.c` desde a PR A.2; protótipos em
 * `internal/ed25519_internal.h`. */

/* Scalar arithmetic mod L (`ED_L_BYTES`, `load_3`/`load_4`,
 * `sc_reduce64`, `sc_muladd`, `sc_is_canonical`) movido para
 * `src/security/ed25519_scalar.c` na PR A.3; prototipos em
 * `internal/ed25519_internal.h`. */

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
