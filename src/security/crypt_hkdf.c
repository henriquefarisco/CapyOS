/*
 * src/security/crypt_hkdf.c
 *
 * HKDF-SHA256 — RFC 5869 Extract-then-Expand Key Derivation Function.
 *
 * Carved out of the pre-split monolith `src/security/crypt.c` at the
 * 2026-05-15 refactor so each translation unit stays under the
 * 900-line layout limit.
 *
 * Public symbols owned here:
 *   - `crypt_hkdf_sha256_extract`
 *   - `crypt_hkdf_sha256_expand`
 *   - `crypt_hkdf_sha256` (one-shot convenience wrapper)
 *
 * Internal scope:
 *   - `struct hkdf_hmac_ctx` + `hkdf_hmac_begin/update/end`
 *     stream the standard HMAC pad/inner/outer construction around
 *     the streaming SHA-256 API. This avoids buffering
 *     `T(i-1) || info || byte(i)` into a single contiguous array
 *     that would either need an unbounded stack frame or a kalloc.
 *
 * The internal HMAC one-shot used by extract is shared from
 * `src/security/crypt.c` through
 * `src/security/internal/crypt_internal.h`.
 */
#include "security/crypt.h"

#include <stddef.h>
#include <stdint.h>

#include "security/internal/crypt_internal.h"

/*
 * Streaming HMAC-SHA256 helper used by HKDF-SHA256 expand.
 *
 * Lifetime: the caller declares the context on the stack, calls begin,
 * then any number of updates, then end (which writes the 32-byte MAC).
 * After `end`, the context is fully wiped — calling begin again starts
 * a fresh HMAC. Each call to `end` is paired with one and only one
 * `begin`; mixing them across instances would mean two stacks of
 * unfinalised inner state which is not supported.
 */
struct hkdf_hmac_ctx {
  struct sha256_ctx inner;
  uint8_t kopad[SHA256_BLOCK_SIZE];
};

static void hkdf_hmac_begin(struct hkdf_hmac_ctx *ctx, const uint8_t *key,
                            size_t key_len) {
  uint8_t kipad[SHA256_BLOCK_SIZE];
  uint8_t key_hash[SHA256_DIGEST_SIZE];
  struct sha256_ctx key_ctx;
  int key_ctx_used = 0;

  if (key_len > SHA256_BLOCK_SIZE) {
    sha256_init(&key_ctx);
    sha256_update(&key_ctx, key, key_len);
    sha256_final(&key_ctx, key_hash);
    key_ctx_used = 1;
    key = key_hash;
    key_len = SHA256_DIGEST_SIZE;
  }

  for (size_t i = 0; i < SHA256_BLOCK_SIZE; ++i) {
    uint8_t kc = (i < key_len) ? key[i] : 0x00;
    kipad[i] = kc ^ 0x36;
    ctx->kopad[i] = kc ^ 0x5c;
  }

  sha256_init(&ctx->inner);
  sha256_update(&ctx->inner, kipad, SHA256_BLOCK_SIZE);

  /* `kipad` carried `key XOR 0x36`; wipe before the stack frame leaves
   * scope. `key_hash` (when used) carried the SHA-256 of the original
   * key. Both are sensitive. The inner SHA-256 context now holds the
   * absorbed kipad but `ctx->inner.state[]` is opaque MD state, not
   * the key itself. */
  crypt_secure_clear(kipad, sizeof(kipad));
  if (key_ctx_used) {
    sha256_clear(&key_ctx);
  }
  crypt_secure_clear(key_hash, sizeof(key_hash));
}

static void hkdf_hmac_update(struct hkdf_hmac_ctx *ctx, const uint8_t *data,
                             size_t data_len) {
  if (!data || data_len == 0) {
    return;
  }
  sha256_update(&ctx->inner, data, data_len);
}

static void hkdf_hmac_end(struct hkdf_hmac_ctx *ctx,
                          uint8_t out[SHA256_DIGEST_SIZE]) {
  /* Finalise inner = SHA256(kipad || message). */
  sha256_final(&ctx->inner, out);

  /* Outer = SHA256(kopad || inner_digest). */
  struct sha256_ctx outer;
  sha256_init(&outer);
  sha256_update(&outer, ctx->kopad, SHA256_BLOCK_SIZE);
  sha256_update(&outer, out, SHA256_DIGEST_SIZE);
  sha256_final(&outer, out);

  /* `outer.state[]` and `outer.data[]` carry the produced MAC and a
   * padded block derived from kopad — sensitive. `ctx->inner` has the
   * finalised inner hash in state[] and a padded last block (derived
   * from the kipad XOR message) in data[]. `ctx->kopad` is the key
   * XOR 0x5c. All three regions must be wiped before scope exit. */
  sha256_clear(&outer);
  sha256_clear(&ctx->inner);
  crypt_secure_clear(ctx->kopad, sizeof(ctx->kopad));
}

int crypt_hkdf_sha256_extract(const uint8_t *salt, size_t salt_len,
                              const uint8_t *ikm, size_t ikm_len,
                              uint8_t prk[SHA256_DIGEST_SIZE]) {
  if (!prk) {
    return -1;
  }
  /* RFC 5869 §2.2: if salt is not provided (NULL or zero-length), it
   * is set to a string of HashLen zero octets. The substitution is
   * mandatory — using the raw NULL/0-length pair would degenerate
   * HMAC's key into the empty string and lose the universal-hash
   * property that HKDF relies on for output uniformity. */
  uint8_t zero_salt[SHA256_DIGEST_SIZE];
  const uint8_t *effective_salt = salt;
  size_t effective_salt_len = salt_len;
  if (!effective_salt || effective_salt_len == 0) {
    for (size_t i = 0; i < SHA256_DIGEST_SIZE; ++i) {
      zero_salt[i] = 0u;
    }
    effective_salt = zero_salt;
    effective_salt_len = SHA256_DIGEST_SIZE;
  }
  /* IKM may legitimately be empty (e.g., domain separation use cases);
   * HMAC handles zero-length data without special treatment. We do
   * normalise a NULL pointer to a non-NULL empty buffer just to
   * satisfy strict pointer arithmetic discipline downstream. */
  static const uint8_t empty_ikm = 0u;
  const uint8_t *effective_ikm = ikm ? ikm : &empty_ikm;
  size_t effective_ikm_len = ikm ? ikm_len : 0u;

  crypt_hmac_sha256_internal(effective_salt, effective_salt_len, effective_ikm,
                             effective_ikm_len, prk);

  crypt_secure_clear(zero_salt, sizeof(zero_salt));
  return 0;
}

int crypt_hkdf_sha256_expand(const uint8_t *prk, size_t prk_len,
                             const uint8_t *info, size_t info_len,
                             uint8_t *out, size_t out_len) {
  if (!prk || !out) {
    return -1;
  }
  if (out_len == 0u) {
    /* RFC 5869 does not explicitly forbid L = 0 but it is
     * semantically vacuous. Treat as success without writing. */
    return 0;
  }
  /* RFC 5869 §2.3: L MUST be <= 255 * HashLen. Reject above the
   * boundary so callers cannot trick HKDF into wrapping the counter
   * byte and silently truncating output. */
  if (out_len > (size_t)255u * (size_t)SHA256_DIGEST_SIZE) {
    return -1;
  }
  /* The PRK MUST be at least HashLen bytes (RFC 5869 §2.3). Shorter
   * PRKs would still produce output, but with degraded entropy bound;
   * fail-closed enforces the security parameter expected by callers. */
  if (prk_len < SHA256_DIGEST_SIZE) {
    return -1;
  }

  uint8_t t_prev[SHA256_DIGEST_SIZE];
  size_t t_prev_len = 0u; /* T(0) = empty */
  size_t produced = 0u;
  uint32_t counter = 1u;

  while (produced < out_len) {
    if (counter > 255u) {
      /* Defence-in-depth: the L bound above already prevents this,
       * but the explicit check makes the invariant local-visible. */
      crypt_secure_clear(t_prev, sizeof(t_prev));
      return -1;
    }

    struct hkdf_hmac_ctx hctx;
    hkdf_hmac_begin(&hctx, prk, prk_len);
    if (t_prev_len > 0u) {
      hkdf_hmac_update(&hctx, t_prev, t_prev_len);
    }
    if (info && info_len > 0u) {
      hkdf_hmac_update(&hctx, info, info_len);
    }
    uint8_t counter_byte = (uint8_t)counter;
    hkdf_hmac_update(&hctx, &counter_byte, 1u);
    hkdf_hmac_end(&hctx, t_prev);
    t_prev_len = SHA256_DIGEST_SIZE;

    size_t chunk = SHA256_DIGEST_SIZE;
    if (produced + chunk > out_len) {
      chunk = out_len - produced;
    }
    for (size_t i = 0u; i < chunk; ++i) {
      out[produced + i] = t_prev[i];
    }
    produced += chunk;
    counter++;
  }

  crypt_secure_clear(t_prev, sizeof(t_prev));
  return 0;
}

int crypt_hkdf_sha256(const uint8_t *salt, size_t salt_len,
                      const uint8_t *ikm, size_t ikm_len,
                      const uint8_t *info, size_t info_len, uint8_t *out,
                      size_t out_len) {
  uint8_t prk[SHA256_DIGEST_SIZE];
  int rc = crypt_hkdf_sha256_extract(salt, salt_len, ikm, ikm_len, prk);
  if (rc != 0) {
    crypt_secure_clear(prk, sizeof(prk));
    return rc;
  }
  rc = crypt_hkdf_sha256_expand(prk, sizeof(prk), info, info_len, out, out_len);
  /* Wipe the PRK before returning regardless of expand outcome — the
   * PRK is the secret bridge between IKM and OKM and must not linger
   * on the stack. */
  crypt_secure_clear(prk, sizeof(prk));
  return rc;
}
