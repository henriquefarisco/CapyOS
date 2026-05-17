/*
 * src/security/crypt_kdf.c
 *
 * PBKDF2-SHA256 and AES-XTS volume key derivation. Carved out of the
 * pre-split monolith `src/security/crypt.c` at the 2026-05-15
 * refactor so each translation unit stays under the project's
 * 900-line layout limit.
 *
 * Public symbols owned here:
 *   - `crypt_pbkdf2_sha256` — RFC 8018 PBKDF2-HMAC-SHA256.
 *   - `crypt_derive_xts_keys` — legacy PBKDF2-based AES-XTS volume
 *     key derivation, kept for backward compatibility with volumes
 *     created before the Argon2id migration.
 *   - `crypt_derive_xts_keys_argon2id` — modern memory-hard variant
 *     using Argon2id (RFC 9106).
 *
 * The HMAC-SHA256 primitive itself lives in `src/security/crypt.c`
 * and is shared between this file and `src/security/crypt_hkdf.c`
 * via `src/security/internal/crypt_internal.h`.
 */
#include "security/crypt.h"

#include <stddef.h>
#include <stdint.h>

#include "memory/kmem.h"
#include "security/argon2.h"
#include "security/internal/crypt_internal.h"

static void pbkdf2_hmac_sha256(const uint8_t *password, size_t password_len,
                               const uint8_t *salt, size_t salt_len,
                               uint32_t iterations, uint8_t *out,
                               size_t out_len) {
  uint32_t blocks = (out_len + SHA256_DIGEST_SIZE - 1) / SHA256_DIGEST_SIZE;
  uint8_t u[SHA256_DIGEST_SIZE];
  uint8_t t[SHA256_DIGEST_SIZE];
  uint8_t salt_block[SHA256_BLOCK_SIZE];

  for (uint32_t i = 1; i <= blocks; ++i) {
    size_t sb_len = salt_len;
    if (salt_len + 4 > sizeof(salt_block)) {
      sb_len = sizeof(salt_block) - 4;
    }
    for (size_t j = 0; j < sb_len; ++j) {
      salt_block[j] = salt[j];
    }
    salt_block[sb_len + 0] = (uint8_t)(i >> 24);
    salt_block[sb_len + 1] = (uint8_t)(i >> 16);
    salt_block[sb_len + 2] = (uint8_t)(i >> 8);
    salt_block[sb_len + 3] = (uint8_t)(i);

    crypt_hmac_sha256_internal(password, password_len, salt_block, sb_len + 4, u);
    for (size_t j = 0; j < SHA256_DIGEST_SIZE; ++j) {
      t[j] = u[j];
    }

    for (uint32_t iter = 1; iter < iterations; ++iter) {
      crypt_hmac_sha256_internal(password, password_len, u, SHA256_DIGEST_SIZE, u);
      for (size_t j = 0; j < SHA256_DIGEST_SIZE; ++j) {
        t[j] ^= u[j];
      }
    }

    size_t offset = (i - 1) * SHA256_DIGEST_SIZE;
    size_t to_copy = SHA256_DIGEST_SIZE;
    if (offset + to_copy > out_len) {
      to_copy = out_len - offset;
    }
    for (size_t j = 0; j < to_copy; ++j) {
      out[offset + j] = t[j];
    }
  }

  crypt_secure_clear(u, sizeof(u));
  crypt_secure_clear(t, sizeof(t));
  crypt_secure_clear(salt_block, sizeof(salt_block));
}

void crypt_pbkdf2_sha256(const uint8_t *password, size_t password_len,
                         const uint8_t *salt, size_t salt_len,
                         uint32_t iterations, uint8_t *out, size_t out_len) {
  if (!password || !salt || !out || out_len == 0 || iterations == 0) {
    return;
  }
  pbkdf2_hmac_sha256(password, password_len, salt, salt_len, iterations, out,
                     out_len);
}

void crypt_derive_xts_keys(const char *password, const uint8_t *salt,
                           size_t salt_len, uint32_t iterations,
                           uint8_t key1[CRYPT_KEY_SIZE],
                           uint8_t key2[CRYPT_KEY_SIZE]) {
  if (!password || !salt || !key1 || !key2 || iterations == 0) {
    return;
  }
  size_t pass_len = 0;
  while (password[pass_len]) {
    pass_len++;
  }

  uint8_t derived[CRYPT_KEY_SIZE * 2];
  pbkdf2_hmac_sha256((const uint8_t *)password, pass_len, salt, salt_len,
                     iterations, derived, sizeof(derived));

  for (size_t i = 0; i < CRYPT_KEY_SIZE; ++i) {
    key1[i] = derived[i];
    key2[i] = derived[i + CRYPT_KEY_SIZE];
  }

  crypt_secure_clear(derived, sizeof(derived));
}

int crypt_derive_xts_keys_argon2id(const char *password,
                                   const uint8_t *salt, size_t salt_len,
                                   uint32_t t_cost, uint32_t m_cost,
                                   uint8_t key1[CRYPT_KEY_SIZE],
                                   uint8_t key2[CRYPT_KEY_SIZE]) {
  /*
   * Fail-closed first. Wipe the output buffers up front so a caller
   * that forgets to check the return value receives an unambiguous
   * all-zero "no key here" sentinel rather than uninitialised stack
   * or whatever state the buffers happened to carry from a previous
   * caller. Doing the wipe at the very top also covers the early
   * return paths below.
   */
  if (key1) {
    crypt_secure_clear(key1, CRYPT_KEY_SIZE);
  }
  if (key2) {
    crypt_secure_clear(key2, CRYPT_KEY_SIZE);
  }
  if (!password || !salt || !key1 || !key2) {
    return -1;
  }
  if (t_cost < ARGON2_MIN_T_COST || m_cost < ARGON2_MIN_M_COST) {
    return -1;
  }
  /*
   * RFC 9106 §3.1 mandates salt_len >= 8 (otherwise the salted H0
   * pre-hash leaks too little entropy). The CapyOS volume header
   * embeds a 16-byte salt today, so this lower bound is comfortably
   * met; rejecting shorter salts here keeps future callers from
   * unknowingly weakening the construction.
   */
  if (salt_len < 8u) {
    return -1;
  }

  size_t pass_len = 0;
  while (password[pass_len]) {
    pass_len++;
  }

  size_t memory_bytes = (size_t)m_cost * 1024u;
  uint8_t *memory = (uint8_t *)kalloc(memory_bytes);
  if (!memory) {
    return -1;
  }

  uint8_t derived[CRYPT_KEY_SIZE * 2];
  int rc = argon2id_hash((const uint8_t *)password, pass_len, salt, salt_len,
                         t_cost, m_cost, memory, memory_bytes, derived,
                         sizeof(derived));

  /*
   * Wipe the Argon2id work memory BEFORE freeing so the freed heap
   * region cannot retain material derived from the volume password
   * (the matrix walked by the data-dependent passes carries
   * password-derived state). Free order: wipe -> kfree, never the
   * reverse, because kfree may immediately hand the region to
   * another allocation that observes the residue.
   */
  crypt_secure_clear(memory, memory_bytes);
  kfree(memory);

  if (rc != 0) {
    /* `derived` may carry partial H' output if Argon2id failed mid
     * flight; wipe before returning to keep that scratch from
     * leaking back to the caller's frame, and leave the already-
     * zeroed `key1`/`key2` outputs as the documented "no key" state. */
    crypt_secure_clear(derived, sizeof(derived));
    return -1;
  }

  for (size_t i = 0; i < CRYPT_KEY_SIZE; ++i) {
    key1[i] = derived[i];
    key2[i] = derived[i + CRYPT_KEY_SIZE];
  }

  crypt_secure_clear(derived, sizeof(derived));
  return 0;
}
