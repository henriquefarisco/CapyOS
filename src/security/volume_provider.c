/*
 * CapyOS volume provider implementation (alpha.222-alpha.226).
 *
 * Contract and threat model live in `include/security/volume_provider.h`.
 * This translation unit owns the steady-state volume primitives:
 *
 *   - `volume_provider_install`: write the header on a fresh device,
 *     derive Argon2id keys via `crypt_derive_xts_keys_argon2id` and
 *     hand back a header-managed crypt device ready to mount.
 *   - `volume_provider_open`: read the header on subsequent boots,
 *     authoritatively pick header-managed vs legacy PBKDF2 path and
 *     return the crypt device for `mount_capyfs`.
 *
 * The rekey orchestration (preflight, plan, dry-run execute and
 * persistent checkpoint init/serialize/parse) lives in the sibling
 * TU `src/security/volume_provider_rekey.c`. Write-enabled rekey
 * steps (checkpoint scratch, stage header, copy reverse, commit
 * header, verify open, rollback, abort, cleanup, orchestrate) live
 * in their own siblings (`volume_provider_rekey_execute.c`,
 * `_rekey_commit.c`, `_rekey_recovery.c`, `_rekey_orchestrator.c`).
 *
 * This translation unit ties together:
 *
 *   - alpha.221 on-disk header module (`security/volume_header.h`).
 *   - alpha.220 Argon2id volume-key derivation primitive
 *     (`crypt_derive_xts_keys_argon2id` in `security/crypt.h`).
 *   - alpha.214 hardened CSPRNG (`security/csprng.h`) for the
 *     per-install salt.
 *   - existing block_device wrapper stack (`fs/block.h`,
 *     `block_offset_wrap`, `crypt_init`) for the in-memory layering.
 *
 * Wipe hygiene: every exit path zeros local key material via the
 * volatile-safe `vp_wipe` helper before returning, matching the rest
 * of the security primitives in the project. The on-disk write itself
 * does NOT leak password material — the header carries only the salt
 * and KDF parameters, never the password or the derived keys.
 */

#include "security/volume_provider.h"

#include <stddef.h>
#include <stdint.h>

#include "fs/block.h"
#include "security/crypt.h"
#include "security/csprng.h"
#include "security/volume_header.h"

/*
 * 4 KiB block size that the chunked wrapper produces (see
 * `block_chunked_wrap` callers in installer and boot path). Hard-
 * coded here to avoid coupling this module to the CAPYFS macro and
 * to make it self-evident in code review.
 */
#define VP_BLOCK_4K_SIZE 4096u

/*
 * Length of the per-install random salt. Argon2id minimum is 8 bytes
 * (RFC 9106 §3.1); the legacy g_disk_salt is also 16 bytes; CAPYOS
 * volume header supports up to 64. 16 keeps Argon2id pre-hash time
 * minimal while staying well above the floor and matching the legacy
 * footprint for consistency.
 */
#define VP_DEFAULT_SALT_LEN 16u

/*
 * Volatile-safe wipe. Mirrors the helper in `volume_header.c` so this
 * translation unit has no link-time coupling to other security
 * primitives beyond what's strictly necessary.
 */
static void vp_wipe(void *ptr, size_t len) {
  volatile uint8_t *p = (volatile uint8_t *)ptr;
  while (len--) {
    *p++ = 0;
  }
}

int volume_provider_install(struct block_device *chunked_4096,
                            const char *password,
                            struct block_device **out_crypt) {
  if (out_crypt) {
    *out_crypt = NULL;
  }
  if (!chunked_4096 || !password || !out_crypt) {
    return -1;
  }
  if (chunked_4096->block_size != VP_BLOCK_4K_SIZE) {
    return -1;
  }
  /* Need at least 1 block for the header AND 1 for the FS data area. */
  if (chunked_4096->block_count < 2u) {
    return -1;
  }

  uint8_t salt[VP_DEFAULT_SALT_LEN];
  uint8_t key1[CRYPT_KEY_SIZE];
  uint8_t key2[CRYPT_KEY_SIZE];
  uint8_t tag[CAPYOS_VOLUME_HEADER_CHECK_TAG_SIZE];
  uint8_t header_buf[VP_BLOCK_4K_SIZE];
  struct capyos_volume_header hdr;
  struct block_device *fs_dev = NULL;
  struct block_device *crypt_dev = NULL;
  int rc = -1;

  /*
   * Pre-zero EVERY local buffer up front. The header is going to be
   * written as a 4 KiB block where only the first 512 bytes carry
   * struct data — the remaining 3584 bytes MUST be zero (reserved
   * region the parser enforces all-zero). Pre-zeroing also gives all
   * the key/tag scratch a "no secret here" sentinel before we
   * generate any.
   */
  vp_wipe(salt, sizeof(salt));
  vp_wipe(key1, sizeof(key1));
  vp_wipe(key2, sizeof(key2));
  vp_wipe(tag, sizeof(tag));
  vp_wipe(header_buf, sizeof(header_buf));
  vp_wipe(&hdr, sizeof(hdr));

  /*
   * Per-install random salt. csprng_get_bytes performs lazy init +
   * hardware reseed transparently; the call is safe from any boot
   * stage that has the CSPRNG module linked.
   */
  csprng_get_bytes(salt, sizeof(salt));

  if (capyos_volume_header_init(&hdr, CAPYOS_VOLUME_KDF_ALGO_ARGON2ID,
                                CRYPT_VOLUME_ARGON2ID_T_COST,
                                CRYPT_VOLUME_ARGON2ID_M_COST, salt,
                                sizeof(salt),
                                CAPYOS_VOLUME_HEADER_DEFAULT_DATA_OFFSET_LBA,
                                CAPYOS_VOLUME_HEADER_DEFAULT_RESERVED_LBA_COUNT,
                                /*timestamp_ns=*/0u,
                                "CapyOS-0.8.0-alpha.222") !=
      CAPYOS_VOLUME_HEADER_OK) {
    goto out;
  }

  if (crypt_derive_xts_keys_argon2id(password, hdr.kdf_salt, hdr.kdf_salt_len,
                                     hdr.kdf_t_cost, hdr.kdf_m_cost, key1,
                                     key2) != 0) {
    goto out;
  }

  if (capyos_volume_header_compute_check_tag(&hdr, key1, key2, tag) !=
      CAPYOS_VOLUME_HEADER_OK) {
    goto out;
  }
  if (capyos_volume_header_finalize_crc(&hdr) != CAPYOS_VOLUME_HEADER_OK) {
    goto out;
  }
  /* `_serialize` writes the first 512 bytes; the rest of `header_buf`
   * stays at the zero we initialised above (reserved region must be
   * all zero — invariant the parser enforces on read). */
  if (capyos_volume_header_serialize(&hdr, header_buf) !=
      CAPYOS_VOLUME_HEADER_OK) {
    goto out;
  }

  if (block_device_write(chunked_4096, 0u, header_buf) != 0) {
    goto out;
  }

  fs_dev = block_offset_wrap(chunked_4096, hdr.data_offset_lba,
                             chunked_4096->block_count - hdr.data_offset_lba);
  if (!fs_dev) {
    goto out;
  }

  crypt_dev = crypt_init(fs_dev, key1, key2);
  if (!crypt_dev || crypt_dev == fs_dev) {
    /* crypt_init failure does NOT free the offset wrapper — the API
     * has no public free helper for it. On install failure the
     * installer reboots; the leak is bounded and acceptable. */
    crypt_dev = NULL;
    goto out;
  }

  *out_crypt = crypt_dev;
  rc = 0;

out:
  vp_wipe(salt, sizeof(salt));
  vp_wipe(key1, sizeof(key1));
  vp_wipe(key2, sizeof(key2));
  vp_wipe(tag, sizeof(tag));
  vp_wipe(header_buf, sizeof(header_buf));
  vp_wipe(&hdr, sizeof(hdr));
  return rc;
}

int volume_provider_open(struct block_device *chunked_4096,
                         const char *password, const uint8_t *legacy_salt,
                         size_t legacy_salt_len, uint32_t legacy_iter,
                         struct block_device **out_crypt) {
  if (out_crypt) {
    *out_crypt = NULL;
  }
  if (!chunked_4096 || !password || !out_crypt) {
    return -1;
  }
  if (chunked_4096->block_size != VP_BLOCK_4K_SIZE) {
    return -1;
  }
  if (chunked_4096->block_count < 1u) {
    return -1;
  }

  uint8_t header_buf[VP_BLOCK_4K_SIZE];
  uint8_t key1[CRYPT_KEY_SIZE];
  uint8_t key2[CRYPT_KEY_SIZE];
  struct capyos_volume_header hdr;
  struct block_device *fs_dev = NULL;
  struct block_device *crypt_dev = NULL;
  int rc = -1;

  vp_wipe(header_buf, sizeof(header_buf));
  vp_wipe(key1, sizeof(key1));
  vp_wipe(key2, sizeof(key2));
  vp_wipe(&hdr, sizeof(hdr));

  if (block_device_read(chunked_4096, 0u, header_buf) != 0) {
    /* I/O failure on block 0 read. Refuse to mount — neither path
     * is reachable from here, and silently falling through to the
     * legacy path would mount whatever the lower layer eventually
     * returns when it recovers. */
    goto out;
  }

  if (capyos_volume_header_looks_valid(header_buf)) {
    /*
     * Header-managed path. Authoritative once `looks_valid` passes:
     * we deliberately do NOT fall back to legacy on a downstream
     * failure (parse / derive / tag-mismatch). Legacy fall-back
     * would derive keys via PBKDF2 and wrap the WHOLE chunked
     * device, including the header block — which is unrelated
     * ciphertext from the FS's perspective, so mount_capyfs would
     * fail anyway, but the attempt itself could be observable as a
     * latency signal. Tight gate.
     */
    if (capyos_volume_header_parse(header_buf, &hdr) !=
        CAPYOS_VOLUME_HEADER_OK) {
      goto out;
    }
    if (capyos_volume_header_derive_keys(&hdr, password, key1, key2) !=
        CAPYOS_VOLUME_HEADER_OK) {
      goto out;
    }
    /* Defence in depth: the header parser already requires
     * data_offset_lba >= 1, but check again against the runtime
     * device size to make sure we don't ask the offset wrapper for
     * an out-of-range start. */
    if (hdr.data_offset_lba >= chunked_4096->block_count) {
      goto out;
    }
    fs_dev = block_offset_wrap(chunked_4096, hdr.data_offset_lba,
                               chunked_4096->block_count - hdr.data_offset_lba);
    if (!fs_dev) {
      goto out;
    }
    crypt_dev = crypt_init(fs_dev, key1, key2);
    if (!crypt_dev || crypt_dev == fs_dev) {
      crypt_dev = NULL;
      goto out;
    }
    *out_crypt = crypt_dev;
    rc = 0;
    goto out;
  }

  /*
   * Legacy path: no header on disk. Derive directly with PBKDF2 and
   * caller-supplied legacy parameters. This is the same construction
   * pre-alpha.222 volumes were created with, and remains in place so
   * existing installations keep mounting after the kernel upgrade.
   */
  if (!legacy_salt || legacy_salt_len == 0u || legacy_iter == 0u) {
    goto out;
  }
  crypt_derive_xts_keys(password, legacy_salt, legacy_salt_len, legacy_iter,
                        key1, key2);
  crypt_dev = crypt_init(chunked_4096, key1, key2);
  if (!crypt_dev || crypt_dev == chunked_4096) {
    crypt_dev = NULL;
    goto out;
  }
  *out_crypt = crypt_dev;
  rc = 0;

out:
  vp_wipe(header_buf, sizeof(header_buf));
  vp_wipe(key1, sizeof(key1));
  vp_wipe(key2, sizeof(key2));
  vp_wipe(&hdr, sizeof(hdr));
  return rc;
}
