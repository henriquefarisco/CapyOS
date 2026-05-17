/*
 * src/security/internal/crypt_internal.h
 *
 * Internal helpers shared inside the `security` module across the
 * crypt translation units (`crypt.c`, `crypt_kdf.c`, `crypt_aes_xts.c`,
 * `crypt_hkdf.c`).
 *
 * NOT part of the public API: every consumer outside `src/security/`
 * must keep using the surface in `include/security/crypt.h`.
 *
 * The helpers exposed here used to be static inside the pre-split
 * monolith `src/security/crypt.c`. Exposing them as extern with the
 * `crypt_` prefix is the only behavioural change in the split — the
 * implementations are byte-for-byte identical to the originals so the
 * existing host vectors and the in-tree volume header still validate.
 */
#ifndef SECURITY_INTERNAL_CRYPT_INTERNAL_H
#define SECURITY_INTERNAL_CRYPT_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "security/sha256.h"

/* Volatile-safe zeroing helper used to wipe credential-bearing
 * scratch buffers (KDF derivations, HMAC pad arrays, AES round-key
 * intermediate state, HKDF T(i) chains). Marked volatile so the
 * optimiser cannot elide the stores when the destination leaves scope
 * or is freed right after — the classic dead-store hazard that
 * silently disables "secure wipe" code in C. */
void crypt_secure_clear(uint8_t *ptr, size_t len);

/* Internal one-shot HMAC-SHA256 used by `pbkdf2_hmac_sha256` and by
 * `crypt_hkdf_sha256_extract`. Distinct from the public
 * `crypt_hmac_sha256` because the internal flavour pre-builds both
 * pads (kipad + kopad) up front, which is the layout PBKDF2 and HKDF
 * happen to prefer. The output is identical for the same inputs. */
void crypt_hmac_sha256_internal(const uint8_t *key, size_t key_len,
                                const uint8_t *data, size_t data_len,
                                uint8_t out[SHA256_DIGEST_SIZE]);

#endif /* SECURITY_INTERNAL_CRYPT_INTERNAL_H */
