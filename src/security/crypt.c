/*
 * src/security/crypt.c
 *
 * Public-surface entry point for the crypt module. Hosts the small,
 * shared primitives that other crypt translation units depend on:
 *
 *   - `crypt_secure_clear`: volatile-safe credential wipe used across
 *     every crypt file. Declared in `internal/crypt_internal.h`.
 *   - `crypt_hmac_sha256_internal`: the dual-pad HMAC variant that
 *     `pbkdf2_hmac_sha256` (crypt_kdf.c) and
 *     `crypt_hkdf_sha256_extract` (crypt_hkdf.c) call into.
 *   - `crypt_constant_time_compare`: public side-channel-safe memcmp.
 *   - `crypt_hmac_sha256`: public one-shot HMAC for callers that want
 *     a single contiguous message; distinct from the internal flavour
 *     above so the public ABI stays stable.
 *
 * Everything else moved out into focused sister files at the
 * 2026-05-15 refactor:
 *
 *   - crypt_kdf.c    : PBKDF2 + AES-XTS volume key derivation.
 *   - crypt_aes_xts.c: AES-256 cipher core, XTS mode and the
 *                      block_device adapter for `crypt_init`/free.
 *   - crypt_hkdf.c   : HKDF-SHA256 extract/expand/oneshot.
 */
#include "security/crypt.h"

#include <stddef.h>
#include <stdint.h>

#include "security/internal/crypt_internal.h"

void crypt_secure_clear(uint8_t *ptr, size_t len) {
  volatile uint8_t *p = ptr;
  while (len--) {
    *p++ = 0;
  }
}

void crypt_hmac_sha256_internal(const uint8_t *key, size_t key_len,
                                const uint8_t *data, size_t data_len,
                                uint8_t out[SHA256_DIGEST_SIZE]) {
  uint8_t kopad[SHA256_BLOCK_SIZE];
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
    kopad[i] = kc ^ 0x5c;
  }

  struct sha256_ctx ctx;
  sha256_init(&ctx);
  sha256_update(&ctx, kipad, SHA256_BLOCK_SIZE);
  sha256_update(&ctx, data, data_len);
  sha256_final(&ctx, out);

  sha256_init(&ctx);
  sha256_update(&ctx, kopad, SHA256_BLOCK_SIZE);
  sha256_update(&ctx, out, SHA256_DIGEST_SIZE);
  sha256_final(&ctx, out);

  /* Wipe every secret-bearing scratch buffer before returning. The pad
   * buffers and any optional hashed key were already cleared; the new
   * `sha256_clear` calls also zero the HMAC inner/outer SHA-256 state,
   * which after `sha256_final` carries the produced hash in `state[]`
   * and a padded copy of the last block (which itself was derived from
   * the key XOR pad and the message digest) in `data[]`. Without these
   * wipes the inner/outer hash leaked across the PBKDF2 inner loop and
   * across the function boundary back to the caller's frame. */
  sha256_clear(&ctx);
  if (key_ctx_used) {
    sha256_clear(&key_ctx);
  }
  crypt_secure_clear(kopad, sizeof(kopad));
  crypt_secure_clear(kipad, sizeof(kipad));
  crypt_secure_clear(key_hash, sizeof(key_hash));
}

int crypt_constant_time_compare(const uint8_t *a, const uint8_t *b, size_t len) {
  volatile uint8_t diff = 0;
  for (size_t i = 0; i < len; i++) {
    diff |= a[i] ^ b[i];
  }
  return diff == 0 ? 0 : -1;
}

void crypt_hmac_sha256(const uint8_t *key, size_t key_len,
                       const uint8_t *data, size_t data_len,
                       uint8_t out[SHA256_DIGEST_SIZE]) {
  uint8_t k_pad[SHA256_BLOCK_SIZE];
  uint8_t tk[SHA256_DIGEST_SIZE];
  struct sha256_ctx ctx;
  if (key_len > SHA256_BLOCK_SIZE) {
    sha256_init(&ctx);
    sha256_update(&ctx, key, key_len);
    sha256_final(&ctx, tk);
    key = tk;
    key_len = SHA256_DIGEST_SIZE;
  }
  for (size_t i = 0; i < SHA256_BLOCK_SIZE; i++)
    k_pad[i] = (i < key_len ? key[i] : 0) ^ 0x36;
  sha256_init(&ctx);
  sha256_update(&ctx, k_pad, SHA256_BLOCK_SIZE);
  sha256_update(&ctx, data, data_len);
  sha256_final(&ctx, out);
  for (size_t i = 0; i < SHA256_BLOCK_SIZE; i++)
    k_pad[i] = (i < key_len ? key[i] : 0) ^ 0x5c;
  sha256_init(&ctx);
  sha256_update(&ctx, k_pad, SHA256_BLOCK_SIZE);
  sha256_update(&ctx, out, SHA256_DIGEST_SIZE);
  sha256_final(&ctx, out);
  /* `ctx` was reused three times to compute the optional key hash, the
   * inner HMAC and the outer HMAC; after the last `sha256_final` it
   * carries the produced MAC in `state[]` (which IS `out`) and the
   * padded outer block in `data[]`. Wipe alongside the k_pad/tk
   * buffers so the freed stack frame does not retain key material. */
  sha256_clear(&ctx);
  crypt_secure_clear(k_pad, sizeof(k_pad));
  crypt_secure_clear(tk, sizeof(tk));
}
