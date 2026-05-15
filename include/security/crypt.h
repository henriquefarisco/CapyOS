#ifndef CRYPT_H
#define CRYPT_H

#include <stddef.h>
#include <stdint.h>

#include "fs/block.h"
#include "security/sha256.h"

#define CRYPT_KEY_SIZE 32

/*
 * Default Argon2id parameters for AES-XTS volume key derivation.
 *
 * The volume password is the LUKS-like passphrase the user types at
 * boot to unlock the encrypted root filesystem. Like the userdb
 * passwords protected since alpha.219, it benefits from memory-hard
 * derivation: an attacker with a stolen disk that brute-forces the
 * passphrase offline pays the full 8 MiB memory cost per candidate,
 * which reduces the GPU/ASIC speedup over CPU from >10000x (PBKDF2)
 * to <10x (Argon2id) per RFC 9106 §1.2.
 *
 * Chosen tuning matches the userdb defaults (`USER_ARGON2ID_T_COST`,
 * `USER_ARGON2ID_M_COST`) so the kernel only needs to budget a
 * single 8 MiB scratch region for either path. The 16 MiB kernel
 * heap (`KHEAP_SIZE` in `src/arch/x86_64/kmem64.c`) accommodates one
 * 8 MiB derivation comfortably while leaving the other 8 MiB for
 * the rest of the kernel.
 */
#ifdef UNIT_TEST
#define CRYPT_VOLUME_ARGON2ID_T_COST 1u
#define CRYPT_VOLUME_ARGON2ID_M_COST 8u
#else
#define CRYPT_VOLUME_ARGON2ID_T_COST 3u
#define CRYPT_VOLUME_ARGON2ID_M_COST 8192u
#endif

struct block_device *crypt_init(struct block_device *lower,
                                const uint8_t key1[CRYPT_KEY_SIZE],
                                const uint8_t key2[CRYPT_KEY_SIZE]);

void crypt_free(struct block_device *dev);

void crypt_pbkdf2_sha256(const uint8_t *password, size_t password_len,
                         const uint8_t *salt, size_t salt_len,
                         uint32_t iterations, uint8_t *out, size_t out_len);

void crypt_derive_xts_keys(const char *password, const uint8_t *salt,
                           size_t salt_len, uint32_t iterations,
                           uint8_t key1[CRYPT_KEY_SIZE],
                           uint8_t key2[CRYPT_KEY_SIZE]);

/*
 * Argon2id-based AES-XTS volume key derivation.
 *
 * Memory-hard alternative to `crypt_derive_xts_keys` introduced in
 * alpha.220. Designed to be drop-in replaceable once the on-disk
 * volume header gains an algorithm marker (currently stored only
 * implicitly by the kernel build); until then this function is
 * exposed as a primitive so future installers and volume creation
 * tools can opt into Argon2id without recompiling the dispatcher.
 *
 * Internally the function:
 *   1. Validates parameters fail-closed.
 *   2. Allocates `m_cost * 1024` bytes of Argon2id work memory via
 *      `kalloc`. Caller is expected to run after `kinit()` — the
 *      function returns -1 immediately if `kalloc` fails.
 *   3. Calls `argon2id_hash` with output length `2 *
 *      CRYPT_KEY_SIZE = 64 bytes`.
 *   4. Splits the 64-byte output into `key1` (first 32 bytes) and
 *      `key2` (last 32 bytes) to match the AES-XTS data/tweak key
 *      schedule.
 *   5. Wipes the work memory volatile-safe before `kfree`, plus the
 *      intermediate 64-byte buffer before returning.
 *
 * On any error path (allocation failure, invalid params, NULL
 * pointers), `key1` and `key2` are wiped to zero so a caller that
 * forgets to check the return value cannot accidentally use a
 * partially-initialised key for AES-XTS setup.
 *
 * Returns 0 on success, -1 on failure.
 *
 * `t_cost` MUST be >= 1, `m_cost` MUST be >= 8 (RFC 9106 §3.1).
 * `salt_len` MUST be >= 8 per RFC 9106 §3.1; the existing CapyOS
 * volume header uses a 16-byte salt which exceeds the lower bound.
 */
int crypt_derive_xts_keys_argon2id(const char *password,
                                   const uint8_t *salt, size_t salt_len,
                                   uint32_t t_cost, uint32_t m_cost,
                                   uint8_t key1[CRYPT_KEY_SIZE],
                                   uint8_t key2[CRYPT_KEY_SIZE]);

int crypt_constant_time_compare(const uint8_t *a, const uint8_t *b, size_t len);
void crypt_hmac_sha256(const uint8_t *key, size_t key_len,
                       const uint8_t *data, size_t data_len,
                       uint8_t out[SHA256_DIGEST_SIZE]);

/*
 * HKDF-SHA256 — RFC 5869 Extract-then-Expand Key Derivation Function.
 *
 * HKDF is the canonical KDF for deriving context-bound subkeys from a
 * shared secret (a PRNG output, a Diffie-Hellman shared secret, a
 * passphrase post-PBKDF2). It is NOT a replacement for PBKDF2 in
 * password hashing — HKDF assumes its input keying material is already
 * uniformly random (or close to it). For raw passphrases, derive a PRK
 * via PBKDF2 first, then use HKDF to fan it out into context-bound
 * subkeys.
 *
 * Conventional split:
 *   - `extract(salt, ikm) -> PRK`: compresses IKM into a 32-byte
 *     pseudorandom key. Salt is optional (NULL or 0-length means
 *     "use 32 zero bytes" per RFC 5869).
 *   - `expand(PRK, info, L) -> OKM`: produces up to 255*32 = 8160
 *     output bytes bound to a context label `info`.
 *
 * `crypt_hkdf_sha256` is the convenience wrapper that runs both stages.
 *
 * Return codes: 0 on success, -1 on invalid input (NULL out, L > 8160,
 * NULL PRK in expand). All scratch buffers (PRK, HMAC state) are wiped
 * on every exit path including error returns.
 */
int crypt_hkdf_sha256_extract(const uint8_t *salt, size_t salt_len,
                              const uint8_t *ikm, size_t ikm_len,
                              uint8_t prk[SHA256_DIGEST_SIZE]);
int crypt_hkdf_sha256_expand(const uint8_t *prk, size_t prk_len,
                             const uint8_t *info, size_t info_len,
                             uint8_t *out, size_t out_len);
int crypt_hkdf_sha256(const uint8_t *salt, size_t salt_len,
                      const uint8_t *ikm, size_t ikm_len,
                      const uint8_t *info, size_t info_len,
                      uint8_t *out, size_t out_len);

#endif
