#ifndef CAPYOS_SECURITY_VOLUME_HEADER_H
#define CAPYOS_SECURITY_VOLUME_HEADER_H

#include <stddef.h>
#include <stdint.h>

#include "security/crypt.h"

/*
 * CapyOS on-disk volume header (alpha.221).
 *
 * Goal
 * ----
 *   Replace the implicit "PBKDF2-SHA256 with hardcoded `g_disk_salt`
 *   and `g_kdf_iterations`" convention used by every CapyOS install
 *   through alpha.220 with an explicit, auditable, on-disk descriptor
 *   that:
 *
 *     1. Records WHICH KDF was used (PBKDF2-SHA256 or Argon2id).
 *     2. Records the KDF parameters (iterations / t_cost / m_cost).
 *     3. Carries a CSPRNG-generated per-install random salt instead
 *        of the global `g_disk_salt` constant baked into the kernel.
 *     4. Carries a `kdf_check_tag` (HMAC-SHA256 over a context label
 *        + header bytes) that lets the boot path detect (a) accidental
 *        bit-flip corruption of the header (via a separate CRC32) and
 *        (b) malicious tampering with KDF parameters intended to
 *        downgrade the algorithm or weaken the cost — the tampered
 *        header derives a different key, the HMAC mismatches, mount
 *        refuses with the same "wrong password" UX that legacy volumes
 *        already exhibit on bad input.
 *
 * Wire layout
 * -----------
 *   The header lives at LBA 0 of the raw data partition (the same
 *   block that previously held the first block of the encrypted
 *   filesystem). Header is 512 bytes (legacy IBM sector size); the
 *   surrounding 4096-byte LBA is zero-padded reserved space available
 *   to future header versions.
 *
 *   Total fixed layout (offsets in bytes):
 *
 *     0   magic0                u32  "CAPY" little-endian
 *     4   magic1                u32  "VHDR" little-endian
 *     8   version               u32  CAPYOS_VOLUME_HEADER_VERSION
 *    12   flags                 u32  reserved, MUST be 0 in v1
 *    16   kdf_algo_id           u32  CAPYOS_VOLUME_KDF_ALGO_*
 *    20   kdf_t_cost            u32  PBKDF2 iter or Argon2id t_cost
 *    24   kdf_m_cost            u32  Argon2id m_cost in KiB (0 if PBKDF2)
 *    28   kdf_salt_len          u32  CAPYOS_VOLUME_KDF_SALT_MIN..MAX
 *    32   kdf_salt[64]          u8[] padded with zeros beyond salt_len
 *    96   data_offset_lba       u32  where the encrypted FS starts
 *   100   reserved_lba_count    u32  how many LBAs reserved (>=1)
 *   104   kdf_check_tag[32]     u8[] HMAC-SHA256(key1||key2, context||hdr[0..104])
 *   136   creation_timestamp_ns u64  CSPRNG-friendly forensic stamp
 *   144   creator_version[32]   char "CapyOS-0.8.0-alphaNNN" null-padded
 *   176   reserved[332]         u8[] MUST be all zero in v1
 *   508   header_crc32          u32  IEEE 802.3 CRC32 of bytes [0..508)
 *
 *   The struct is naturally aligned on uint32_t boundaries (the u64
 *   `creation_timestamp_ns` lands at offset 136 = 8 * 17 so its 8-byte
 *   alignment is satisfied too). Endianness is fixed little-endian on
 *   disk — CapyOS only targets x86_64 today, but the (de)serializers
 *   convert explicitly so a future big-endian port reads the same
 *   layout.
 *
 * Threat model and invariants
 * ---------------------------
 *   The header itself is NOT encrypted — it MUST be readable before
 *   key derivation since it carries the KDF parameters. That makes it
 *   a tampering target. Two-tier integrity:
 *
 *     - `header_crc32` catches bit rot / silent disk corruption. NOT a
 *       security primitive — an attacker who recomputes CRC32 trivially
 *       defeats it. Its job is to abort early on a corrupted header
 *       before we burn an 8 MiB Argon2id derivation on a 50% certain
 *       failure.
 *
 *     - `kdf_check_tag` catches malicious parameter tampering. It is
 *       HMAC-SHA256 of `context || hdr[0..104]` keyed by `key1||key2`.
 *       The HMAC binds the KDF parameters and salt to the derived key.
 *       If an attacker rewrites any byte before offset 104 (algo_id,
 *       t_cost, m_cost, salt_len, salt, data_offset, reserved_lba),
 *       the user's password derives a DIFFERENT key, the recomputed
 *       HMAC differs from the stored tag, and mount refuses. The
 *       user-visible outcome is identical to a wrong password — we
 *       intentionally do NOT distinguish "tampered header" from "bad
 *       password" in the error message to avoid handing the attacker
 *       an oracle that distinguishes presence of correct-password-but-
 *       tampered-header from absence of the password.
 *
 *   `kdf_check_tag` and `header_crc32` themselves are NOT covered by
 *   `kdf_check_tag` (we'd need them to compute it). They ARE covered
 *   by `header_crc32`. Order of validation at boot:
 *
 *     1. CRC32 over [0..508) matches `header_crc32`.
 *     2. `magic0` and `magic1` match.
 *     3. `version` is supported.
 *     4. `kdf_algo_id` and parameters are within accepted ranges.
 *     5. Derive keys with the declared KDF and salt.
 *     6. Recompute HMAC and constant-time compare against
 *        `kdf_check_tag`.
 *     7. If all pass: keys are good, proceed to mount with offset
 *        `data_offset_lba`.
 *     8. Any failure between 1 and 6: wipe keys and return "wrong
 *        password" (same UX as legacy bad-password path).
 *
 *   We accept (1)-(4) being attacker-controlled: the worst they can
 *   do is make the boot path waste an Argon2id derivation. Steps (5)-
 *   (6) bind everything together cryptographically.
 *
 * Backward compatibility
 * ----------------------
 *   Existing volumes installed pre-alpha.222 have raw LBA 0 holding
 *   encrypted filesystem content, not header magic. The probability
 *   that random encrypted bytes match both magic words is 2^-64, so
 *   the legacy fallback in the boot path safely activates when
 *   `volume_header_parse` returns -1.
 *
 *   Per-version policy:
 *
 *     - alpha.221: header module landed; installer/boot still on
 *       legacy PBKDF2 + g_disk_salt; primitive available for testing.
 *     - alpha.222 (planned): installer writes header on fresh
 *       installs; boot path tries header first, falls back to legacy
 *       PBKDF2 + g_disk_salt for existing volumes.
 *     - alpha.223 (planned): re-keying tool to convert legacy volumes
 *       to header-managed ones in place.
 */

/* Magic = ASCII "CAPYVHDR" little-endian as two u32. */
#define CAPYOS_VOLUME_HEADER_MAGIC0 0x59504143u  /* "CAPY" */
#define CAPYOS_VOLUME_HEADER_MAGIC1 0x52444856u  /* "VHDR" */

#define CAPYOS_VOLUME_HEADER_VERSION 1u
#define CAPYOS_VOLUME_HEADER_SIZE 512u
#define CAPYOS_VOLUME_HEADER_CHECK_TAG_SIZE 32u
#define CAPYOS_VOLUME_HEADER_CREATOR_VERSION_SIZE 32u
#define CAPYOS_VOLUME_HEADER_RESERVED_SIZE 332u

#define CAPYOS_VOLUME_KDF_ALGO_PBKDF2_SHA256 0u
#define CAPYOS_VOLUME_KDF_ALGO_ARGON2ID 1u

#define CAPYOS_VOLUME_KDF_SALT_MIN 8u
#define CAPYOS_VOLUME_KDF_SALT_MAX 64u

/*
 * Default data offset for new headers. The header itself reserves
 * LBA 0; the encrypted filesystem starts at LBA 1 (= raw byte offset
 * 4096 with 4 KiB sectors). Older bootloaders that read raw LBA 0
 * expecting filesystem content WILL break — but those bootloaders
 * are pre-CapyOS, so this only applies to fresh CapyOS installs that
 * opt into the header.
 */
#define CAPYOS_VOLUME_HEADER_DEFAULT_DATA_OFFSET_LBA 1u
#define CAPYOS_VOLUME_HEADER_DEFAULT_RESERVED_LBA_COUNT 1u

/*
 * HMAC-SHA256 context label for `kdf_check_tag`. The version suffix
 * lets a future header version use a different label so an attacker
 * cannot replay a v1 check tag on a v2 header (or vice versa) even
 * if the rest of the byte layout happens to coincide.
 */
#define CAPYOS_VOLUME_HEADER_CHECK_CONTEXT "CAPYOS-VOL-HDR-CHECK-v1"
#define CAPYOS_VOLUME_HEADER_CHECK_CONTEXT_LEN 23u

/*
 * Offset within the header at which `kdf_check_tag` starts. The HMAC
 * input is `context || header_bytes[0..CHECK_TAG_OFFSET)` — i.e. we
 * authenticate every byte up to but not including the tag itself.
 */
#define CAPYOS_VOLUME_HEADER_CHECK_TAG_OFFSET 104u

/*
 * Offset at which `header_crc32` lives (last 4 bytes of the header).
 * CRC is computed over header bytes [0..CRC_OFFSET).
 */
#define CAPYOS_VOLUME_HEADER_CRC_OFFSET 508u

/*
 * In-memory representation of the header. Disk layout is little-
 * endian as documented above; (de)serializers convert explicitly so
 * this struct can use natural host endianness for the C API.
 */
struct capyos_volume_header {
  uint32_t magic0;
  uint32_t magic1;
  uint32_t version;
  uint32_t flags;
  uint32_t kdf_algo_id;
  uint32_t kdf_t_cost;
  uint32_t kdf_m_cost;
  uint32_t kdf_salt_len;
  uint8_t kdf_salt[CAPYOS_VOLUME_KDF_SALT_MAX];
  uint32_t data_offset_lba;
  uint32_t reserved_lba_count;
  uint8_t kdf_check_tag[CAPYOS_VOLUME_HEADER_CHECK_TAG_SIZE];
  uint64_t creation_timestamp_ns;
  uint8_t creator_version[CAPYOS_VOLUME_HEADER_CREATOR_VERSION_SIZE];
  uint8_t reserved[CAPYOS_VOLUME_HEADER_RESERVED_SIZE];
  uint32_t header_crc32;
};

/*
 * Validation results returned by `capyos_volume_header_validate` and
 * exposed for unit tests that need to assert WHICH validation step
 * caught a tampered or corrupted header. The boot path treats all
 * non-OK codes equivalently (= "fall back to legacy" before keys are
 * derived, "wrong password" after) — distinguishing them in the UX
 * would leak an oracle.
 */
typedef enum capyos_volume_header_status {
  CAPYOS_VOLUME_HEADER_OK = 0,
  CAPYOS_VOLUME_HEADER_ERR_NULL = -1,
  CAPYOS_VOLUME_HEADER_ERR_MAGIC = -2,
  CAPYOS_VOLUME_HEADER_ERR_VERSION = -3,
  CAPYOS_VOLUME_HEADER_ERR_FLAGS = -4,
  CAPYOS_VOLUME_HEADER_ERR_ALGO = -5,
  CAPYOS_VOLUME_HEADER_ERR_PARAMS = -6,
  CAPYOS_VOLUME_HEADER_ERR_SALT_LEN = -7,
  CAPYOS_VOLUME_HEADER_ERR_DATA_OFFSET = -8,
  CAPYOS_VOLUME_HEADER_ERR_RESERVED = -9,
  CAPYOS_VOLUME_HEADER_ERR_CRC = -10,
  CAPYOS_VOLUME_HEADER_ERR_CHECK_TAG = -11,
  CAPYOS_VOLUME_HEADER_ERR_KDF = -12
} capyos_volume_header_status_t;

/*
 * Initialise an in-memory header with the given KDF parameters and a
 * caller-supplied random salt. Caller is responsible for generating
 * `salt` from a CSPRNG (`csprng_get_bytes` in production); the
 * function does NOT call into the CSPRNG itself so it remains testable
 * with deterministic vectors.
 *
 * Side effects:
 *   - Populates `magic0/1`, `version`, `kdf_*`, `data_offset_lba`,
 *     `reserved_lba_count`, `creation_timestamp_ns`, and
 *     `creator_version` (truncated to fit, null-padded).
 *   - Zeros `kdf_check_tag` (caller must compute via
 *     `capyos_volume_header_compute_check_tag` after key derivation).
 *   - Recomputes `header_crc32` — but the tag is still zero at this
 *     point, so this CRC is provisional; caller must call
 *     `capyos_volume_header_finalize_crc` after the tag is filled in.
 *   - Zero-fills `reserved`.
 *
 * Returns CAPYOS_VOLUME_HEADER_OK on success, or one of the ERR_*
 * codes on parameter validation failure. On failure the header is
 * zeroed.
 *
 * Constraints:
 *   - `algo_id` in {PBKDF2_SHA256, ARGON2ID}.
 *   - For PBKDF2: t_cost >= 1000 (sanity floor), m_cost == 0.
 *   - For Argon2id: t_cost >= 1, m_cost >= 8 (RFC 9106 §3.1).
 *   - salt_len in [SALT_MIN, SALT_MAX].
 *   - data_offset_lba >= 1 (header occupies LBA 0).
 *   - reserved_lba_count >= 1 and <= data_offset_lba.
 *   - `creator_version` truncated/padded to 32 bytes including
 *     terminating NUL when shorter.
 */
int capyos_volume_header_init(struct capyos_volume_header *out,
                              uint32_t algo_id, uint32_t t_cost,
                              uint32_t m_cost, const uint8_t *salt,
                              uint32_t salt_len, uint32_t data_offset_lba,
                              uint32_t reserved_lba_count,
                              uint64_t creation_timestamp_ns,
                              const char *creator_version);

/*
 * Compute the HMAC-SHA256 `kdf_check_tag` for a header given the
 * already-derived volume keys. The HMAC input is
 *   context || hdr_serialized[0..CHECK_TAG_OFFSET)
 * keyed by `key1 || key2`.
 *
 * Writes the 32-byte tag to `out_tag` AND mirrors it into
 * `hdr->kdf_check_tag` for convenience (the in-memory header is the
 * usual production target). Wipes all scratch buffers before
 * returning.
 *
 * The serialization is internal — callers do not need to serialize
 * the header first. We serialize the fixed-layout bytes [0..104) into
 * a stack buffer, prepend the context label, run HMAC, and wipe.
 *
 * Returns CAPYOS_VOLUME_HEADER_OK on success, ERR_NULL if any required
 * pointer is NULL. Never fails on otherwise-valid input — HMAC is
 * deterministic and total.
 */
int capyos_volume_header_compute_check_tag(
    struct capyos_volume_header *hdr,
    const uint8_t key1[CRYPT_KEY_SIZE],
    const uint8_t key2[CRYPT_KEY_SIZE],
    uint8_t out_tag[CAPYOS_VOLUME_HEADER_CHECK_TAG_SIZE]);

/*
 * Verify the `kdf_check_tag` in a header against keys derived from a
 * candidate password. Computes the expected tag and constant-time
 * compares against `hdr->kdf_check_tag`. Returns
 * CAPYOS_VOLUME_HEADER_OK on match, CAPYOS_VOLUME_HEADER_ERR_CHECK_TAG
 * on mismatch (constant-time path).
 */
int capyos_volume_header_verify_check_tag(
    const struct capyos_volume_header *hdr,
    const uint8_t key1[CRYPT_KEY_SIZE],
    const uint8_t key2[CRYPT_KEY_SIZE]);

/*
 * Recompute and store `header_crc32` based on the current contents
 * of all preceding fields. MUST be called AFTER
 * `capyos_volume_header_compute_check_tag` since the tag is covered
 * by the CRC.
 */
int capyos_volume_header_finalize_crc(struct capyos_volume_header *hdr);

/*
 * Serialize a header into a 512-byte little-endian on-disk buffer.
 * `out` MUST point to at least CAPYOS_VOLUME_HEADER_SIZE bytes. The
 * full buffer is overwritten — caller does not need to pre-zero it.
 *
 * Returns CAPYOS_VOLUME_HEADER_OK on success, ERR_NULL on bad
 * pointers.
 */
int capyos_volume_header_serialize(const struct capyos_volume_header *hdr,
                                   uint8_t out[CAPYOS_VOLUME_HEADER_SIZE]);

/*
 * Parse a 512-byte little-endian on-disk buffer into a header struct
 * AND validate magic, version, flags, algorithm marker, parameter
 * ranges, salt length, data offset, reserved fields, and CRC32.
 *
 * Does NOT validate the `kdf_check_tag` — that requires the derived
 * keys and is handled separately by
 * `capyos_volume_header_verify_check_tag` so the parse step stays
 * cheap and predictable.
 *
 * Returns CAPYOS_VOLUME_HEADER_OK on full validation success. On any
 * failure, returns the corresponding ERR_* code AND zeros `out` so
 * partial state cannot leak to the caller's frame.
 */
int capyos_volume_header_parse(const uint8_t buf[CAPYOS_VOLUME_HEADER_SIZE],
                               struct capyos_volume_header *out);

/*
 * Detect whether a 512-byte buffer plausibly holds a CapyOS volume
 * header by checking magic + version + CRC ONLY. Useful for the boot
 * path "is this a modern volume?" decision without paying for full
 * field validation. Returns 1 if the buffer looks like a v1 header,
 * 0 otherwise (including all error cases). Constant time over the
 * 512-byte buffer for CRC purposes.
 *
 * NOTE: A positive result is necessary but NOT sufficient to mount
 * the volume — full validation via `capyos_volume_header_parse` and
 * `capyos_volume_header_verify_check_tag` is still required. This
 * function exists so the boot path can decide "try modern path" vs
 * "fall back to legacy" without leaking why each path failed.
 */
int capyos_volume_header_looks_valid(
    const uint8_t buf[CAPYOS_VOLUME_HEADER_SIZE]);

/*
 * High-level dispatcher: derive AES-XTS volume keys from a password
 * using the KDF declared in the header (PBKDF2-SHA256 or Argon2id),
 * then verify the resulting keys match the header's `kdf_check_tag`.
 *
 * On success, `key1` and `key2` carry the derived AES-XTS data/tweak
 * keys, and the caller can immediately `crypt_init(data_dev, key1,
 * key2)` to spin up the encrypted layer (over a `block_offset_wrap`
 * shifted by `hdr->data_offset_lba`).
 *
 * On any failure path (KDF error, check-tag mismatch, NULL pointers,
 * unsupported algorithm), `key1` and `key2` are wiped to zero so a
 * caller that forgets to check the return value cannot accidentally
 * use a partially-initialised key. The function does NOT distinguish
 * "wrong password" from "tampered header" in the return code — both
 * surface as CAPYOS_VOLUME_HEADER_ERR_CHECK_TAG so the boot path
 * cannot accidentally turn that distinction into an attacker-visible
 * oracle.
 *
 * Allocates 8 MiB of work memory transiently for Argon2id paths via
 * the same `kalloc`/wipe/`kfree` discipline as
 * `crypt_derive_xts_keys_argon2id` (which is what this function calls
 * internally). For PBKDF2 paths there is no heap allocation.
 */
int capyos_volume_header_derive_keys(const struct capyos_volume_header *hdr,
                                     const char *password,
                                     uint8_t key1[CRYPT_KEY_SIZE],
                                     uint8_t key2[CRYPT_KEY_SIZE]);

/*
 * Internal CRC32 helper exposed for testing. IEEE 802.3 polynomial
 * (0xEDB88320 reflected) with no lookup table (small + readable).
 * NOT a security primitive — see threat model in the file header.
 */
uint32_t capyos_volume_header_crc32(const uint8_t *data, size_t len);

#endif /* CAPYOS_SECURITY_VOLUME_HEADER_H */
