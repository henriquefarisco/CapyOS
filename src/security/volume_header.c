/*
 * CapyOS volume header implementation (alpha.221).
 *
 * Public API contract and threat model live in
 * `include/security/volume_header.h`. This translation unit holds:
 *
 *   - Endianness-explicit serialize/parse for the fixed 512-byte
 *     on-disk layout.
 *   - A small no-table CRC32 (IEEE 802.3 reflected) used as a fast
 *     accidental-corruption gate. CRC32 is NOT a security primitive;
 *     see `kdf_check_tag` for the cryptographic binding.
 *   - HMAC-SHA256 wrappers that compute/verify `kdf_check_tag` against
 *     a context label so different header versions cannot replay
 *     each other's tags.
 *   - A KDF dispatcher that picks PBKDF2-SHA256 or Argon2id based on
 *     `kdf_algo_id` and forwards to the existing primitives in
 *     `crypt.c`. The dispatcher always wipes its key outputs on
 *     failure so a caller that forgets the return code lands on a
 *     well-defined "no key here" sentinel rather than stack residue.
 *
 * No global state. No heap allocations except the transient 8 MiB
 * Argon2id work memory inside `crypt_derive_xts_keys_argon2id` (which
 * wipes + frees it before returning). All scratch buffers on the
 * stack are wiped via `secure_clear` (volatile-pointer loop) before
 * every exit path, including error paths.
 */

#include "security/volume_header.h"

#include <stddef.h>
#include <stdint.h>

#include "security/crypt.h"
#include "security/sha256.h"

/*
 * Tiny volatile-safe wipe so the compiler cannot eliminate the loop
 * even when the buffer is about to go out of scope. Mirrors the
 * `secure_clear` helper in `crypt.c` but kept local so this unit has
 * no link-time coupling to anything besides the header it implements.
 */
static void vh_wipe(void *ptr, size_t len) {
  volatile uint8_t *p = (volatile uint8_t *)ptr;
  while (len--) {
    *p++ = 0;
  }
}

/*
 * Constant-time byte comparison. Returns 0 if equal, -1 otherwise.
 * Used only for `kdf_check_tag` verification. We re-implement the
 * primitive here instead of calling `crypt_constant_time_compare`
 * just to keep this translation unit's external dependency surface
 * small (sha256 + crypt KDFs only); a future audit can grep this
 * file once and see every byte that touches secret material.
 */
static int vh_const_eq(const uint8_t *a, const uint8_t *b, size_t len) {
  volatile uint8_t diff = 0;
  for (size_t i = 0; i < len; ++i) {
    diff |= (uint8_t)(a[i] ^ b[i]);
  }
  return diff == 0 ? 0 : -1;
}

/*
 * Little-endian 32-bit store/load. We do byte-by-byte work so the
 * serializer produces the SAME on-disk layout on any host endianness
 * — the kernel only runs on x86_64 today but documentation/portability
 * tooling and host-side tests benefit from explicit conversion.
 */
static void vh_put_u32_le(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFFu);
  p[1] = (uint8_t)((v >> 8) & 0xFFu);
  p[2] = (uint8_t)((v >> 16) & 0xFFu);
  p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint32_t vh_get_u32_le(const uint8_t *p) {
  return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void vh_put_u64_le(uint8_t *p, uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    p[i] = (uint8_t)((v >> (8 * i)) & 0xFFu);
  }
}

static uint64_t vh_get_u64_le(const uint8_t *p) {
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i) {
    v |= ((uint64_t)p[i]) << (8 * i);
  }
  return v;
}

/*
 * Small no-table CRC32. Reflected polynomial 0xEDB88320 = bit-reverse
 * of IEEE 802.3's 0x04C11DB7. Initial value 0xFFFFFFFF, output xored
 * with 0xFFFFFFFF — the standard "CRC-32/ISO-HDLC" parameterisation
 * also used by Ethernet, gzip, PNG, zlib.
 *
 * Trade-off: no 256-entry lookup table keeps this trivially auditable
 * (~10 lines) and avoids a 1 KiB rodata footprint that we'd otherwise
 * carry into kernel text segment. Throughput is one CRC32 per 4096-
 * byte header per mount, so ~33000 cycles on a modern CPU — entirely
 * negligible next to the 8 MiB Argon2id derivation that follows.
 */
uint32_t capyos_volume_header_crc32(const uint8_t *data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  if (!data) {
    return 0;
  }
  for (size_t i = 0; i < len; ++i) {
    crc ^= (uint32_t)data[i];
    for (int bit = 0; bit < 8; ++bit) {
      uint32_t mask = (uint32_t) - (int32_t)(crc & 1u);
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return crc ^ 0xFFFFFFFFu;
}

/*
 * Serialize header bytes [0..CHECK_TAG_OFFSET) into a stack buffer.
 * Used both by the disk serializer and by the check-tag computation
 * (which authenticates exactly these bytes). Keeping the layout in
 * ONE place avoids drift between the on-disk format and what the
 * HMAC covers — any future change to the head of the layout MUST
 * be reflected here, by extending the offsets in the header struct
 * and updating this function in lock-step.
 */
static void vh_serialize_prefix(const struct capyos_volume_header *hdr,
                                uint8_t prefix[CAPYOS_VOLUME_HEADER_CHECK_TAG_OFFSET]) {
  uint8_t *p = prefix;
  vh_put_u32_le(p + 0, hdr->magic0);
  vh_put_u32_le(p + 4, hdr->magic1);
  vh_put_u32_le(p + 8, hdr->version);
  vh_put_u32_le(p + 12, hdr->flags);
  vh_put_u32_le(p + 16, hdr->kdf_algo_id);
  vh_put_u32_le(p + 20, hdr->kdf_t_cost);
  vh_put_u32_le(p + 24, hdr->kdf_m_cost);
  vh_put_u32_le(p + 28, hdr->kdf_salt_len);
  for (size_t i = 0; i < CAPYOS_VOLUME_KDF_SALT_MAX; ++i) {
    p[32 + i] = hdr->kdf_salt[i];
  }
  vh_put_u32_le(p + 96, hdr->data_offset_lba);
  vh_put_u32_le(p + 100, hdr->reserved_lba_count);
}

/*
 * Full 512-byte serializer. Builds the prefix via `vh_serialize_prefix`
 * (keeps layout authority in one place), then appends check tag,
 * timestamp, creator string, reserved padding, and CRC.
 *
 * `header_crc32` field in `hdr` is NOT consulted — we always recompute
 * here so the on-disk image is internally consistent with whatever
 * the caller built. This protects against accidentally writing a stale
 * CRC if `hdr` was mutated post-finalize.
 */
int capyos_volume_header_serialize(const struct capyos_volume_header *hdr,
                                   uint8_t out[CAPYOS_VOLUME_HEADER_SIZE]) {
  if (!hdr || !out) {
    return CAPYOS_VOLUME_HEADER_ERR_NULL;
  }
  uint8_t *p = out;
  for (size_t i = 0; i < CAPYOS_VOLUME_HEADER_SIZE; ++i) {
    p[i] = 0;
  }
  vh_serialize_prefix(hdr, p);
  for (size_t i = 0; i < CAPYOS_VOLUME_HEADER_CHECK_TAG_SIZE; ++i) {
    p[CAPYOS_VOLUME_HEADER_CHECK_TAG_OFFSET + i] = hdr->kdf_check_tag[i];
  }
  vh_put_u64_le(p + 136, hdr->creation_timestamp_ns);
  for (size_t i = 0; i < CAPYOS_VOLUME_HEADER_CREATOR_VERSION_SIZE; ++i) {
    p[144 + i] = hdr->creator_version[i];
  }
  for (size_t i = 0; i < CAPYOS_VOLUME_HEADER_RESERVED_SIZE; ++i) {
    p[176 + i] = hdr->reserved[i];
  }
  uint32_t crc = capyos_volume_header_crc32(p, CAPYOS_VOLUME_HEADER_CRC_OFFSET);
  vh_put_u32_le(p + CAPYOS_VOLUME_HEADER_CRC_OFFSET, crc);
  return CAPYOS_VOLUME_HEADER_OK;
}

/*
 * Parameter range checks. Centralised so `_init` (write-side) and
 * `_parse` (read-side) agree on what's accepted. The boot path MUST
 * reject parameters outside these ranges so an attacker who tampers
 * the header with degenerate values (t_cost=0, m_cost=1, salt_len=2)
 * cannot trick the KDF into producing trivially crackable keys.
 *
 * Returns CAPYOS_VOLUME_HEADER_OK if valid, the relevant ERR code if
 * not. Does not access global state.
 */
static int vh_validate_params(uint32_t algo_id, uint32_t t_cost,
                              uint32_t m_cost, uint32_t salt_len,
                              uint32_t data_offset_lba,
                              uint32_t reserved_lba_count) {
  if (algo_id != CAPYOS_VOLUME_KDF_ALGO_PBKDF2_SHA256 &&
      algo_id != CAPYOS_VOLUME_KDF_ALGO_ARGON2ID) {
    return CAPYOS_VOLUME_HEADER_ERR_ALGO;
  }
  if (algo_id == CAPYOS_VOLUME_KDF_ALGO_PBKDF2_SHA256) {
    /* PBKDF2 sanity floor: 1000 iterations matches the alpha.220
     * legacy g_kdf_iterations floor of 16000 with comfortable margin
     * for parameter tuning. We reject lower so a tampered header
     * cannot collapse PBKDF2 to a trivial cost. m_cost MUST be 0 for
     * PBKDF2 (any non-zero value is taken as a probe to confuse the
     * dispatcher). */
    if (t_cost < 1000u || t_cost > CAPYOS_VOLUME_KDF_PBKDF2_ITERS_MAX ||
        m_cost != 0u) {
      return CAPYOS_VOLUME_HEADER_ERR_PARAMS;
    }
  } else {
    /* Argon2id bounds per RFC 9106 §3.1: t_cost >= 1, m_cost >= 8 (KiB).
     * CapyOS uses t=3, m=8192 as the production tuning. The lower bounds
     * reject degenerate-weak values; the upper bounds reject absurd ones
     * that would force an unbounded mount-time derivation/allocation before
     * the header is authenticated (see the ceiling rationale in the
     * header). */
    if (t_cost < 1u || t_cost > CAPYOS_VOLUME_KDF_ARGON2_T_COST_MAX ||
        m_cost < 8u || m_cost > CAPYOS_VOLUME_KDF_ARGON2_M_COST_MAX) {
      return CAPYOS_VOLUME_HEADER_ERR_PARAMS;
    }
  }
  if (salt_len < CAPYOS_VOLUME_KDF_SALT_MIN ||
      salt_len > CAPYOS_VOLUME_KDF_SALT_MAX) {
    return CAPYOS_VOLUME_HEADER_ERR_SALT_LEN;
  }
  if (data_offset_lba < 1u) {
    return CAPYOS_VOLUME_HEADER_ERR_DATA_OFFSET;
  }
  if (reserved_lba_count < 1u || reserved_lba_count > data_offset_lba) {
    return CAPYOS_VOLUME_HEADER_ERR_RESERVED;
  }
  return CAPYOS_VOLUME_HEADER_OK;
}

int capyos_volume_header_init(struct capyos_volume_header *out,
                              uint32_t algo_id, uint32_t t_cost,
                              uint32_t m_cost, const uint8_t *salt,
                              uint32_t salt_len, uint32_t data_offset_lba,
                              uint32_t reserved_lba_count,
                              uint64_t creation_timestamp_ns,
                              const char *creator_version) {
  if (!out || !salt) {
    return CAPYOS_VOLUME_HEADER_ERR_NULL;
  }
  int rc = vh_validate_params(algo_id, t_cost, m_cost, salt_len,
                              data_offset_lba, reserved_lba_count);
  if (rc != CAPYOS_VOLUME_HEADER_OK) {
    vh_wipe(out, sizeof(*out));
    return rc;
  }
  /* Zero the struct first so trailing/reserved fields never carry
   * stack residue into the on-disk image. */
  vh_wipe(out, sizeof(*out));
  out->magic0 = CAPYOS_VOLUME_HEADER_MAGIC0;
  out->magic1 = CAPYOS_VOLUME_HEADER_MAGIC1;
  out->version = CAPYOS_VOLUME_HEADER_VERSION;
  out->flags = 0u;
  out->kdf_algo_id = algo_id;
  out->kdf_t_cost = t_cost;
  out->kdf_m_cost = m_cost;
  out->kdf_salt_len = salt_len;
  for (uint32_t i = 0; i < salt_len; ++i) {
    out->kdf_salt[i] = salt[i];
  }
  out->data_offset_lba = data_offset_lba;
  out->reserved_lba_count = reserved_lba_count;
  out->creation_timestamp_ns = creation_timestamp_ns;
  /* Copy creator_version with NUL-padding. Truncate silently if
   * longer than the fixed slot — versions are short ASCII strings
   * like "CapyOS-0.8.0-alpha.221" (22 bytes) so truncation only kicks
   * in for misuse. */
  if (creator_version) {
    size_t i = 0;
    while (i < CAPYOS_VOLUME_HEADER_CREATOR_VERSION_SIZE &&
           creator_version[i] != '\0') {
      out->creator_version[i] = (uint8_t)creator_version[i];
      ++i;
    }
  }
  /* check_tag stays zero — caller must fill after key derivation. */
  return CAPYOS_VOLUME_HEADER_OK;
}

/*
 * HMAC-SHA256 wrapper that consumes the context label + serialized
 * prefix in a single streaming pass. We build the input on the stack
 * (~127 bytes) so no heap allocation is needed and the buffer can be
 * wiped on every exit path. Keying material is the concatenation of
 * the two AES-XTS keys (64 bytes total) — same construction that
 * `crypt_hmac_sha256` already accepts.
 */
static void vh_compute_tag_internal(
    const struct capyos_volume_header *hdr,
    const uint8_t key1[CRYPT_KEY_SIZE], const uint8_t key2[CRYPT_KEY_SIZE],
    uint8_t out_tag[CAPYOS_VOLUME_HEADER_CHECK_TAG_SIZE]) {
  uint8_t prefix[CAPYOS_VOLUME_HEADER_CHECK_TAG_OFFSET];
  uint8_t input[CAPYOS_VOLUME_HEADER_CHECK_CONTEXT_LEN +
                CAPYOS_VOLUME_HEADER_CHECK_TAG_OFFSET];
  uint8_t key_concat[CRYPT_KEY_SIZE * 2];

  vh_serialize_prefix(hdr, prefix);
  const char *ctx = CAPYOS_VOLUME_HEADER_CHECK_CONTEXT;
  for (size_t i = 0; i < CAPYOS_VOLUME_HEADER_CHECK_CONTEXT_LEN; ++i) {
    input[i] = (uint8_t)ctx[i];
  }
  for (size_t i = 0; i < CAPYOS_VOLUME_HEADER_CHECK_TAG_OFFSET; ++i) {
    input[CAPYOS_VOLUME_HEADER_CHECK_CONTEXT_LEN + i] = prefix[i];
  }
  for (size_t i = 0; i < CRYPT_KEY_SIZE; ++i) {
    key_concat[i] = key1[i];
    key_concat[CRYPT_KEY_SIZE + i] = key2[i];
  }
  crypt_hmac_sha256(key_concat, sizeof(key_concat), input, sizeof(input),
                    out_tag);
  vh_wipe(prefix, sizeof(prefix));
  vh_wipe(input, sizeof(input));
  vh_wipe(key_concat, sizeof(key_concat));
}

int capyos_volume_header_compute_check_tag(
    struct capyos_volume_header *hdr,
    const uint8_t key1[CRYPT_KEY_SIZE],
    const uint8_t key2[CRYPT_KEY_SIZE],
    uint8_t out_tag[CAPYOS_VOLUME_HEADER_CHECK_TAG_SIZE]) {
  if (!hdr || !key1 || !key2 || !out_tag) {
    return CAPYOS_VOLUME_HEADER_ERR_NULL;
  }
  vh_compute_tag_internal(hdr, key1, key2, out_tag);
  for (size_t i = 0; i < CAPYOS_VOLUME_HEADER_CHECK_TAG_SIZE; ++i) {
    hdr->kdf_check_tag[i] = out_tag[i];
  }
  return CAPYOS_VOLUME_HEADER_OK;
}

int capyos_volume_header_verify_check_tag(
    const struct capyos_volume_header *hdr,
    const uint8_t key1[CRYPT_KEY_SIZE],
    const uint8_t key2[CRYPT_KEY_SIZE]) {
  if (!hdr || !key1 || !key2) {
    return CAPYOS_VOLUME_HEADER_ERR_NULL;
  }
  uint8_t expected[CAPYOS_VOLUME_HEADER_CHECK_TAG_SIZE];
  vh_compute_tag_internal(hdr, key1, key2, expected);
  int rc =
      vh_const_eq(expected, hdr->kdf_check_tag,
                  CAPYOS_VOLUME_HEADER_CHECK_TAG_SIZE) == 0
          ? CAPYOS_VOLUME_HEADER_OK
          : CAPYOS_VOLUME_HEADER_ERR_CHECK_TAG;
  vh_wipe(expected, sizeof(expected));
  return rc;
}

int capyos_volume_header_finalize_crc(struct capyos_volume_header *hdr) {
  if (!hdr) {
    return CAPYOS_VOLUME_HEADER_ERR_NULL;
  }
  /* Build the same 508-byte image that goes on disk minus the CRC
   * field itself, then CRC32 that prefix and stash the result. */
  uint8_t buf[CAPYOS_VOLUME_HEADER_SIZE];
  int rc = capyos_volume_header_serialize(hdr, buf);
  if (rc != CAPYOS_VOLUME_HEADER_OK) {
    vh_wipe(buf, sizeof(buf));
    return rc;
  }
  /* `_serialize` already places the CRC into `buf` — read it back so
   * the in-memory struct's `header_crc32` matches the on-disk image
   * if the caller happens to inspect it. */
  hdr->header_crc32 = vh_get_u32_le(buf + CAPYOS_VOLUME_HEADER_CRC_OFFSET);
  vh_wipe(buf, sizeof(buf));
  return CAPYOS_VOLUME_HEADER_OK;
}

/*
 * Cheap structural check used by `_looks_valid`. Verifies magic +
 * version + CRC. Does NOT validate parameter ranges (an attacker who
 * crafted a header with valid CRC but degenerate t_cost still gets
 * past this; the full `_parse` catches that). Boot path uses this to
 * decide "modern vs legacy" before paying for Argon2id.
 */
static int vh_quick_validate_buf(const uint8_t buf[CAPYOS_VOLUME_HEADER_SIZE]) {
  uint32_t m0 = vh_get_u32_le(buf + 0);
  uint32_t m1 = vh_get_u32_le(buf + 4);
  uint32_t ver = vh_get_u32_le(buf + 8);
  if (m0 != CAPYOS_VOLUME_HEADER_MAGIC0 ||
      m1 != CAPYOS_VOLUME_HEADER_MAGIC1) {
    return CAPYOS_VOLUME_HEADER_ERR_MAGIC;
  }
  if (ver != CAPYOS_VOLUME_HEADER_VERSION) {
    return CAPYOS_VOLUME_HEADER_ERR_VERSION;
  }
  uint32_t expected_crc =
      capyos_volume_header_crc32(buf, CAPYOS_VOLUME_HEADER_CRC_OFFSET);
  uint32_t stored_crc = vh_get_u32_le(buf + CAPYOS_VOLUME_HEADER_CRC_OFFSET);
  if (expected_crc != stored_crc) {
    return CAPYOS_VOLUME_HEADER_ERR_CRC;
  }
  return CAPYOS_VOLUME_HEADER_OK;
}

int capyos_volume_header_looks_valid(
    const uint8_t buf[CAPYOS_VOLUME_HEADER_SIZE]) {
  if (!buf) {
    return 0;
  }
  return vh_quick_validate_buf(buf) == CAPYOS_VOLUME_HEADER_OK ? 1 : 0;
}

int capyos_volume_header_parse(const uint8_t buf[CAPYOS_VOLUME_HEADER_SIZE],
                               struct capyos_volume_header *out) {
  if (!buf || !out) {
    return CAPYOS_VOLUME_HEADER_ERR_NULL;
  }
  /* Always wipe the output first. Then either we fail and the caller
   * sees zeros (safe), or we succeed and overwrite with parsed data. */
  vh_wipe(out, sizeof(*out));

  int rc = vh_quick_validate_buf(buf);
  if (rc != CAPYOS_VOLUME_HEADER_OK) {
    return rc;
  }

  uint32_t algo_id = vh_get_u32_le(buf + 16);
  uint32_t t_cost = vh_get_u32_le(buf + 20);
  uint32_t m_cost = vh_get_u32_le(buf + 24);
  uint32_t salt_len = vh_get_u32_le(buf + 28);
  uint32_t data_offset = vh_get_u32_le(buf + 96);
  uint32_t reserved_lba = vh_get_u32_le(buf + 100);
  uint32_t flags = vh_get_u32_le(buf + 12);
  if (flags != 0u) {
    return CAPYOS_VOLUME_HEADER_ERR_FLAGS;
  }
  rc = vh_validate_params(algo_id, t_cost, m_cost, salt_len, data_offset,
                          reserved_lba);
  if (rc != CAPYOS_VOLUME_HEADER_OK) {
    return rc;
  }
  /* Reserved region MUST be all zero in v1. Any non-zero byte means
   * a future writer used flags we don't understand — refuse rather
   * than mount with potentially incompatible semantics. */
  for (size_t i = 0; i < CAPYOS_VOLUME_HEADER_RESERVED_SIZE; ++i) {
    if (buf[176 + i] != 0u) {
      return CAPYOS_VOLUME_HEADER_ERR_RESERVED;
    }
  }
  out->magic0 = vh_get_u32_le(buf + 0);
  out->magic1 = vh_get_u32_le(buf + 4);
  out->version = vh_get_u32_le(buf + 8);
  out->flags = flags;
  out->kdf_algo_id = algo_id;
  out->kdf_t_cost = t_cost;
  out->kdf_m_cost = m_cost;
  out->kdf_salt_len = salt_len;
  for (size_t i = 0; i < CAPYOS_VOLUME_KDF_SALT_MAX; ++i) {
    out->kdf_salt[i] = buf[32 + i];
  }
  out->data_offset_lba = data_offset;
  out->reserved_lba_count = reserved_lba;
  for (size_t i = 0; i < CAPYOS_VOLUME_HEADER_CHECK_TAG_SIZE; ++i) {
    out->kdf_check_tag[i] =
        buf[CAPYOS_VOLUME_HEADER_CHECK_TAG_OFFSET + i];
  }
  out->creation_timestamp_ns = vh_get_u64_le(buf + 136);
  for (size_t i = 0; i < CAPYOS_VOLUME_HEADER_CREATOR_VERSION_SIZE; ++i) {
    out->creator_version[i] = buf[144 + i];
  }
  /* `reserved` is left zero (already wiped above) — we verified it was
   * zero on disk; copying preserves that invariant in memory. */
  out->header_crc32 = vh_get_u32_le(buf + CAPYOS_VOLUME_HEADER_CRC_OFFSET);
  return CAPYOS_VOLUME_HEADER_OK;
}

int capyos_volume_header_derive_keys(const struct capyos_volume_header *hdr,
                                     const char *password,
                                     uint8_t key1[CRYPT_KEY_SIZE],
                                     uint8_t key2[CRYPT_KEY_SIZE]) {
  /* Fail-closed first: zero both keys before any parameter check so
   * a caller that forgets the return code lands on the "no key here"
   * sentinel rather than uninitialised stack. Mirrors the discipline
   * of `crypt_derive_xts_keys_argon2id`. */
  if (key1) {
    vh_wipe(key1, CRYPT_KEY_SIZE);
  }
  if (key2) {
    vh_wipe(key2, CRYPT_KEY_SIZE);
  }
  if (!hdr || !password || !key1 || !key2) {
    return CAPYOS_VOLUME_HEADER_ERR_NULL;
  }
  /* The header may have been constructed in-memory (e.g. by tests)
   * without passing through `_parse` — revalidate parameters so we
   * cannot end up calling the KDF with out-of-range inputs. */
  int rc = vh_validate_params(hdr->kdf_algo_id, hdr->kdf_t_cost,
                              hdr->kdf_m_cost, hdr->kdf_salt_len,
                              hdr->data_offset_lba,
                              hdr->reserved_lba_count);
  if (rc != CAPYOS_VOLUME_HEADER_OK) {
    return rc;
  }
  if (hdr->kdf_algo_id == CAPYOS_VOLUME_KDF_ALGO_PBKDF2_SHA256) {
    crypt_derive_xts_keys(password, hdr->kdf_salt, hdr->kdf_salt_len,
                          hdr->kdf_t_cost, key1, key2);
    /* `crypt_derive_xts_keys` returns void; treat as success and let
     * the check-tag verification catch any silent corruption. */
  } else {
    int kdf_rc = crypt_derive_xts_keys_argon2id(
        password, hdr->kdf_salt, hdr->kdf_salt_len, hdr->kdf_t_cost,
        hdr->kdf_m_cost, key1, key2);
    if (kdf_rc != 0) {
      /* `crypt_derive_xts_keys_argon2id` wipes key1/key2 on failure
       * already, but be explicit so this file remains auditable on
       * its own without cross-reading crypt.c. */
      vh_wipe(key1, CRYPT_KEY_SIZE);
      vh_wipe(key2, CRYPT_KEY_SIZE);
      return CAPYOS_VOLUME_HEADER_ERR_KDF;
    }
  }
  int verify_rc = capyos_volume_header_verify_check_tag(hdr, key1, key2);
  if (verify_rc != CAPYOS_VOLUME_HEADER_OK) {
    /* Wipe so the caller cannot misuse keys that didn't authenticate
     * the header. The boot path treats this as "wrong password"
     * (same UX as legacy bad-password) to avoid leaking a tampered-
     * header oracle. */
    vh_wipe(key1, CRYPT_KEY_SIZE);
    vh_wipe(key2, CRYPT_KEY_SIZE);
    return verify_rc;
  }
  return CAPYOS_VOLUME_HEADER_OK;
}
