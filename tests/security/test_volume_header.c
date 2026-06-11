/*
 * Host-side tests for the CapyOS volume header module (alpha.221).
 *
 * Tests are partitioned into focused functions so a regression
 * reports the SPECIFIC contract that broke. Every test wipes its
 * local key material before returning so a future audit reading
 * the test output cannot distinguish "the test passed" from "the
 * test passed AND its scratch is still on the stack".
 *
 * The host `kalloc` stub backs onto malloc, which lets Argon2id-
 * heavy paths run without a real kernel heap. To keep the suite
 * fast we use the smallest Argon2id parameters that the volume
 * header module accepts (`t_cost=1, m_cost=8` KiB); production uses
 * `t_cost=3, m_cost=8192` which is bounded by Argon2id intrinsic
 * cost rather than this test.
 */

#include <stdio.h>
#include <string.h>

#include "security/crypt.h"
#include "security/volume_header.h"

#define TEST_PASSWORD "capyos-volume-passphrase"

static const uint8_t kTestSalt16[16] = {
    'A', 'l', 'p', 'h', 'a', '2', '2', '1', '-', 'S', 'a', 'l', 't', '!', '!', '!'};
static const uint8_t kTestSalt32[32] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
    0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};

static int expect_int(int got, int want, const char *what) {
  if (got != want) {
    printf("[tests] volume_header: %s expected %d, got %d\n", what, want, got);
    return 1;
  }
  return 0;
}

static int expect_true(int cond, const char *msg) {
  if (!cond) {
    printf("[tests] volume_header: %s\n", msg);
    return 1;
  }
  return 0;
}

static int buffer_all_zero(const uint8_t *buf, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    if (buf[i] != 0) {
      return 0;
    }
  }
  return 1;
}

/* ---- CRC32 known-answer ----------------------------------------- */
static int test_crc32_known_vectors(void) {
  /* IEEE 802.3 / ISO-HDLC CRC32 standard vectors. These three are the
   * canonical test vectors quoted by every reference implementation
   * (RFC 3309, zlib, PNG spec). Match against any of them guarantees
   * the polynomial parameterisation (init 0xFFFFFFFF, reflected
   * 0xEDB88320, post-xor 0xFFFFFFFF) is wired correctly.
   *
   *   ""           -> 0x00000000
   *   "a"          -> 0xE8B7BE43
   *   "123456789"  -> 0xCBF43926
   *
   * We also verify that NULL input returns 0 (defensive contract) and
   * that running CRC over a long zero buffer doesn't get short-
   * circuited by any null-terminator-aware loop. */
  int fails = 0;
  uint8_t zero_block[64];
  for (size_t i = 0; i < sizeof(zero_block); ++i) {
    zero_block[i] = 0;
  }
  fails += expect_int(
      (int)capyos_volume_header_crc32((const uint8_t *)"", 0),
      0x00000000, "crc32 empty");
  fails += expect_int(
      (int)capyos_volume_header_crc32((const uint8_t *)"a", 1),
      (int)0xE8B7BE43u, "crc32 'a'");
  fails += expect_int(
      (int)capyos_volume_header_crc32((const uint8_t *)"123456789", 9),
      (int)0xCBF43926u, "crc32 '123456789'");
  fails +=
      expect_int((int)capyos_volume_header_crc32(NULL, 16), 0, "crc32 NULL");
  /* Zero-byte buffer of significant length: CRC MUST advance bit-by-
   * bit (not stop at the first zero). We don't pin a known value here
   * — the assertion is "non-zero output", which proves the loop ran. */
  fails += expect_true(
      capyos_volume_header_crc32(zero_block, sizeof(zero_block)) != 0u,
      "crc32 on zero buffer must run all bytes (non-zero output)");
  return fails;
}

/* ---- init happy path -------------------------------------------- */
static int test_init_pbkdf2_happy(void) {
  int fails = 0;
  struct capyos_volume_header hdr;
  int rc = capyos_volume_header_init(
      &hdr, CAPYOS_VOLUME_KDF_ALGO_PBKDF2_SHA256, /*t_cost*/ 16000u,
      /*m_cost*/ 0u, kTestSalt16, sizeof(kTestSalt16),
      /*data_offset_lba*/ 1u, /*reserved_lba_count*/ 1u,
      /*timestamp*/ 1747200000000000000ULL,
      "CapyOS-0.8.0-alpha.221");
  fails += expect_int(rc, CAPYOS_VOLUME_HEADER_OK, "init pbkdf2 rc");
  fails += expect_int((int)hdr.magic0, (int)CAPYOS_VOLUME_HEADER_MAGIC0,
                      "magic0");
  fails += expect_int((int)hdr.magic1, (int)CAPYOS_VOLUME_HEADER_MAGIC1,
                      "magic1");
  fails += expect_int((int)hdr.version, (int)CAPYOS_VOLUME_HEADER_VERSION,
                      "version");
  fails += expect_int((int)hdr.flags, 0, "flags");
  fails +=
      expect_int((int)hdr.kdf_algo_id,
                 (int)CAPYOS_VOLUME_KDF_ALGO_PBKDF2_SHA256, "algo_id pbkdf2");
  fails += expect_int((int)hdr.kdf_t_cost, 16000, "pbkdf2 t_cost");
  fails += expect_int((int)hdr.kdf_m_cost, 0, "pbkdf2 m_cost");
  fails +=
      expect_int((int)hdr.kdf_salt_len, (int)sizeof(kTestSalt16), "salt_len");
  fails += expect_true(memcmp(hdr.kdf_salt, kTestSalt16, sizeof(kTestSalt16)) ==
                           0,
                       "salt bytes mismatch");
  /* Salt area beyond salt_len MUST be zero (caller-supplied salt may be
   * shorter than the 64-byte slot). */
  fails += expect_true(buffer_all_zero(hdr.kdf_salt + sizeof(kTestSalt16),
                                       CAPYOS_VOLUME_KDF_SALT_MAX -
                                           sizeof(kTestSalt16)),
                       "salt tail not zero");
  fails += expect_int((int)hdr.data_offset_lba, 1, "data_offset_lba");
  fails += expect_int((int)hdr.reserved_lba_count, 1, "reserved_lba_count");
  fails += expect_true(buffer_all_zero(hdr.kdf_check_tag,
                                       CAPYOS_VOLUME_HEADER_CHECK_TAG_SIZE),
                       "check_tag must be zero before compute");
  fails += expect_true(hdr.creation_timestamp_ns == 1747200000000000000ULL,
                       "timestamp");
  fails += expect_true(memcmp(hdr.creator_version, "CapyOS-0.8.0-alpha.221",
                              22) == 0,
                       "creator_version bytes");
  fails +=
      expect_true(hdr.creator_version[22] == 0, "creator_version null pad");
  fails += expect_true(
      buffer_all_zero(hdr.reserved, CAPYOS_VOLUME_HEADER_RESERVED_SIZE),
      "reserved must be zero");
  return fails;
}

static int test_init_argon2id_happy(void) {
  int fails = 0;
  struct capyos_volume_header hdr;
  int rc = capyos_volume_header_init(
      &hdr, CAPYOS_VOLUME_KDF_ALGO_ARGON2ID, /*t_cost*/ 1u, /*m_cost*/ 8u,
      kTestSalt32, sizeof(kTestSalt32), 1u, 1u, 0ULL, NULL);
  fails += expect_int(rc, CAPYOS_VOLUME_HEADER_OK, "init argon2id rc");
  fails += expect_int((int)hdr.kdf_algo_id,
                      (int)CAPYOS_VOLUME_KDF_ALGO_ARGON2ID, "algo_id");
  fails += expect_int((int)hdr.kdf_t_cost, 1, "argon2id t_cost");
  fails += expect_int((int)hdr.kdf_m_cost, 8, "argon2id m_cost");
  fails +=
      expect_int((int)hdr.kdf_salt_len, (int)sizeof(kTestSalt32), "salt_len");
  fails += expect_true(memcmp(hdr.kdf_salt, kTestSalt32, sizeof(kTestSalt32)) ==
                           0,
                       "salt bytes mismatch");
  /* NULL creator_version is allowed (results in 32 zero bytes). */
  fails += expect_true(buffer_all_zero(
                          hdr.creator_version,
                          CAPYOS_VOLUME_HEADER_CREATOR_VERSION_SIZE),
                       "creator_version must be zero when NULL passed");
  return fails;
}

/* ---- init fail-closed ------------------------------------------- */
static int test_init_fail_closed(void) {
  int fails = 0;
  struct capyos_volume_header hdr;
  /* NULL output. */
  fails += expect_int(
      capyos_volume_header_init(NULL, CAPYOS_VOLUME_KDF_ALGO_PBKDF2_SHA256,
                                16000u, 0u, kTestSalt16, 16u, 1u, 1u, 0ULL,
                                NULL),
      CAPYOS_VOLUME_HEADER_ERR_NULL, "init NULL out");
  /* NULL salt. */
  fails += expect_int(
      capyos_volume_header_init(&hdr, CAPYOS_VOLUME_KDF_ALGO_PBKDF2_SHA256,
                                16000u, 0u, NULL, 16u, 1u, 1u, 0ULL, NULL),
      CAPYOS_VOLUME_HEADER_ERR_NULL, "init NULL salt");
  /* Unknown algo. */
  fails += expect_int(
      capyos_volume_header_init(&hdr, 999u, 16000u, 0u, kTestSalt16, 16u, 1u,
                                1u, 0ULL, NULL),
      CAPYOS_VOLUME_HEADER_ERR_ALGO, "init unknown algo");
  /* PBKDF2 with weak t_cost. */
  fails += expect_int(
      capyos_volume_header_init(&hdr, CAPYOS_VOLUME_KDF_ALGO_PBKDF2_SHA256,
                                999u, 0u, kTestSalt16, 16u, 1u, 1u, 0ULL, NULL),
      CAPYOS_VOLUME_HEADER_ERR_PARAMS, "init pbkdf2 t_cost<1000");
  /* PBKDF2 with non-zero m_cost. */
  fails += expect_int(
      capyos_volume_header_init(&hdr, CAPYOS_VOLUME_KDF_ALGO_PBKDF2_SHA256,
                                16000u, 1u, kTestSalt16, 16u, 1u, 1u, 0ULL,
                                NULL),
      CAPYOS_VOLUME_HEADER_ERR_PARAMS, "init pbkdf2 m_cost!=0");
  /* Argon2id with t_cost=0. */
  fails += expect_int(
      capyos_volume_header_init(&hdr, CAPYOS_VOLUME_KDF_ALGO_ARGON2ID, 0u, 8u,
                                kTestSalt16, 16u, 1u, 1u, 0ULL, NULL),
      CAPYOS_VOLUME_HEADER_ERR_PARAMS, "init argon2id t_cost=0");
  /* Argon2id with m_cost<8. */
  fails += expect_int(
      capyos_volume_header_init(&hdr, CAPYOS_VOLUME_KDF_ALGO_ARGON2ID, 1u, 7u,
                                kTestSalt16, 16u, 1u, 1u, 0ULL, NULL),
      CAPYOS_VOLUME_HEADER_ERR_PARAMS, "init argon2id m_cost<8");
  /* salt_len below minimum. */
  fails += expect_int(
      capyos_volume_header_init(&hdr, CAPYOS_VOLUME_KDF_ALGO_ARGON2ID, 1u, 8u,
                                kTestSalt16, 7u, 1u, 1u, 0ULL, NULL),
      CAPYOS_VOLUME_HEADER_ERR_SALT_LEN, "init salt_len<8");
  /* salt_len above maximum. */
  fails += expect_int(
      capyos_volume_header_init(&hdr, CAPYOS_VOLUME_KDF_ALGO_ARGON2ID, 1u, 8u,
                                kTestSalt16,
                                CAPYOS_VOLUME_KDF_SALT_MAX + 1u, 1u, 1u, 0ULL,
                                NULL),
      CAPYOS_VOLUME_HEADER_ERR_SALT_LEN, "init salt_len>max");
  /* data_offset_lba==0. */
  fails += expect_int(
      capyos_volume_header_init(&hdr, CAPYOS_VOLUME_KDF_ALGO_ARGON2ID, 1u, 8u,
                                kTestSalt16, 16u, 0u, 1u, 0ULL, NULL),
      CAPYOS_VOLUME_HEADER_ERR_DATA_OFFSET, "init data_offset_lba=0");
  /* reserved_lba_count==0. */
  fails += expect_int(
      capyos_volume_header_init(&hdr, CAPYOS_VOLUME_KDF_ALGO_ARGON2ID, 1u, 8u,
                                kTestSalt16, 16u, 1u, 0u, 0ULL, NULL),
      CAPYOS_VOLUME_HEADER_ERR_RESERVED, "init reserved_lba_count=0");
  /* reserved_lba_count>data_offset_lba. */
  fails += expect_int(
      capyos_volume_header_init(&hdr, CAPYOS_VOLUME_KDF_ALGO_ARGON2ID, 1u, 8u,
                                kTestSalt16, 16u, 1u, 2u, 0ULL, NULL),
      CAPYOS_VOLUME_HEADER_ERR_RESERVED, "init reserved>data_offset");
  /* Argon2id t_cost above the sanity ceiling (pre-auth mount-time DoS guard). */
  fails += expect_int(
      capyos_volume_header_init(&hdr, CAPYOS_VOLUME_KDF_ALGO_ARGON2ID,
                                CAPYOS_VOLUME_KDF_ARGON2_T_COST_MAX + 1u, 8u,
                                kTestSalt16, 16u, 1u, 1u, 0ULL, NULL),
      CAPYOS_VOLUME_HEADER_ERR_PARAMS, "init argon2id t_cost>max");
  /* Argon2id m_cost above the sanity ceiling (would force a huge mount-time
   * allocation before the header is authenticated). */
  fails += expect_int(
      capyos_volume_header_init(&hdr, CAPYOS_VOLUME_KDF_ALGO_ARGON2ID, 3u,
                                CAPYOS_VOLUME_KDF_ARGON2_M_COST_MAX + 1u,
                                kTestSalt16, 16u, 1u, 1u, 0ULL, NULL),
      CAPYOS_VOLUME_HEADER_ERR_PARAMS, "init argon2id m_cost>max");
  /* PBKDF2 iteration count above the sanity ceiling. */
  fails += expect_int(
      capyos_volume_header_init(&hdr, CAPYOS_VOLUME_KDF_ALGO_PBKDF2_SHA256,
                                CAPYOS_VOLUME_KDF_PBKDF2_ITERS_MAX + 1u, 0u,
                                kTestSalt16, 16u, 1u, 1u, 0ULL, NULL),
      CAPYOS_VOLUME_HEADER_ERR_PARAMS, "init pbkdf2 iters>max");
  return fails;
}

/* ---- serialize / parse roundtrip + endianness ------------------- */
static int test_serialize_parse_roundtrip(void) {
  int fails = 0;
  struct capyos_volume_header src, dst;
  uint8_t buf[CAPYOS_VOLUME_HEADER_SIZE];
  /* Use a fully-populated header (Argon2id path, big salt, real keys
   * available for tag). The check tag must roundtrip too. */
  int rc = capyos_volume_header_init(
      &src, CAPYOS_VOLUME_KDF_ALGO_ARGON2ID, 1u, 8u, kTestSalt32,
      sizeof(kTestSalt32), 1u, 1u, 1234567890ULL,
      "CapyOS-0.8.0-alpha.221");
  fails += expect_int(rc, CAPYOS_VOLUME_HEADER_OK, "init for serialize");

  /* Derive real keys with this header so we can populate a real
   * check_tag — serializing a header with a zero tag would still
   * roundtrip but would be a weaker assertion. */
  uint8_t k1[CRYPT_KEY_SIZE], k2[CRYPT_KEY_SIZE];
  fails += expect_int(
      capyos_volume_header_derive_keys(&src, TEST_PASSWORD, k1, k2),
      CAPYOS_VOLUME_HEADER_ERR_CHECK_TAG,
      "derive on freshly-initialized header must fail check_tag (tag still zero)");
  /* For the roundtrip test we first compute the check_tag, then derive
   * should succeed. */
  /* We need to re-derive keys directly (the header's tag is zero so
   * derive_keys wiped k1/k2). Use the lower-level KDF directly. */
  int kdf_rc = crypt_derive_xts_keys_argon2id(
      TEST_PASSWORD, src.kdf_salt, src.kdf_salt_len, src.kdf_t_cost,
      src.kdf_m_cost, k1, k2);
  fails += expect_int(kdf_rc, 0, "argon2id direct derive");

  uint8_t out_tag[CAPYOS_VOLUME_HEADER_CHECK_TAG_SIZE];
  fails += expect_int(
      capyos_volume_header_compute_check_tag(&src, k1, k2, out_tag),
      CAPYOS_VOLUME_HEADER_OK, "compute tag");
  fails += expect_int(
      capyos_volume_header_finalize_crc(&src), CAPYOS_VOLUME_HEADER_OK,
      "finalize crc");

  rc = capyos_volume_header_serialize(&src, buf);
  fails += expect_int(rc, CAPYOS_VOLUME_HEADER_OK, "serialize rc");

  /* Endianness sanity: magic0/1 are little-endian "CAPY" and "VHDR".
   * Bytes 0..7 must read as ASCII "CAPYVHDR" on every host. */
  fails += expect_true(buf[0] == 'C' && buf[1] == 'A' && buf[2] == 'P' &&
                           buf[3] == 'Y',
                       "magic0 LE bytes != 'CAPY'");
  fails += expect_true(buf[4] == 'V' && buf[5] == 'H' && buf[6] == 'D' &&
                           buf[7] == 'R',
                       "magic1 LE bytes != 'VHDR'");

  rc = capyos_volume_header_parse(buf, &dst);
  fails += expect_int(rc, CAPYOS_VOLUME_HEADER_OK, "parse rc");
  fails += expect_int((int)dst.magic0, (int)src.magic0, "magic0 roundtrip");
  fails += expect_int((int)dst.magic1, (int)src.magic1, "magic1 roundtrip");
  fails += expect_int((int)dst.version, (int)src.version, "version rt");
  fails += expect_int((int)dst.kdf_algo_id, (int)src.kdf_algo_id, "algo rt");
  fails += expect_int((int)dst.kdf_t_cost, (int)src.kdf_t_cost, "t_cost rt");
  fails += expect_int((int)dst.kdf_m_cost, (int)src.kdf_m_cost, "m_cost rt");
  fails += expect_int((int)dst.kdf_salt_len, (int)src.kdf_salt_len,
                      "salt_len rt");
  fails += expect_true(memcmp(dst.kdf_salt, src.kdf_salt,
                              CAPYOS_VOLUME_KDF_SALT_MAX) == 0,
                       "salt rt");
  fails += expect_int((int)dst.data_offset_lba, (int)src.data_offset_lba,
                      "data_offset rt");
  fails += expect_int((int)dst.reserved_lba_count, (int)src.reserved_lba_count,
                      "reserved_lba rt");
  fails += expect_true(memcmp(dst.kdf_check_tag, src.kdf_check_tag,
                              CAPYOS_VOLUME_HEADER_CHECK_TAG_SIZE) == 0,
                      "check_tag rt");
  fails += expect_true(dst.creation_timestamp_ns == src.creation_timestamp_ns,
                       "timestamp rt");
  fails += expect_true(memcmp(dst.creator_version, src.creator_version,
                              CAPYOS_VOLUME_HEADER_CREATOR_VERSION_SIZE) == 0,
                       "creator_version rt");
  fails += expect_int((int)dst.header_crc32, (int)src.header_crc32, "crc rt");

  /* Wipe key material. */
  for (size_t i = 0; i < CRYPT_KEY_SIZE; ++i) {
    k1[i] = 0;
    k2[i] = 0;
  }
  for (size_t i = 0; i < sizeof(out_tag); ++i) {
    out_tag[i] = 0;
  }
  return fails;
}

/* ---- parse fail-closed ------------------------------------------ */
static int test_parse_fail_closed(void) {
  int fails = 0;
  struct capyos_volume_header hdr;
  uint8_t buf[CAPYOS_VOLUME_HEADER_SIZE];
  /* Build a valid baseline header. */
  int rc = capyos_volume_header_init(
      &hdr, CAPYOS_VOLUME_KDF_ALGO_PBKDF2_SHA256, 16000u, 0u, kTestSalt16,
      sizeof(kTestSalt16), 1u, 1u, 0ULL, NULL);
  fails += expect_int(rc, CAPYOS_VOLUME_HEADER_OK, "baseline init");
  rc = capyos_volume_header_finalize_crc(&hdr);
  fails += expect_int(rc, CAPYOS_VOLUME_HEADER_OK, "baseline crc");
  rc = capyos_volume_header_serialize(&hdr, buf);
  fails += expect_int(rc, CAPYOS_VOLUME_HEADER_OK, "baseline serialize");

  struct capyos_volume_header parsed;
  /* NULL inputs. */
  fails += expect_int(capyos_volume_header_parse(NULL, &parsed),
                      CAPYOS_VOLUME_HEADER_ERR_NULL, "parse NULL buf");
  fails += expect_int(capyos_volume_header_parse(buf, NULL),
                      CAPYOS_VOLUME_HEADER_ERR_NULL, "parse NULL out");

  /* Bad magic. */
  uint8_t tampered[CAPYOS_VOLUME_HEADER_SIZE];
  memcpy(tampered, buf, sizeof(tampered));
  tampered[0] ^= 0x01;
  /* CRC will also be wrong but we want to catch magic first. The
   * implementation checks CRC first, so this case returns ERR_CRC.
   * That is acceptable — both report "not a valid header" to the
   * boot path. We verify magic detection by ALSO fixing the CRC. */
  uint32_t fixed_crc = capyos_volume_header_crc32(
      tampered, CAPYOS_VOLUME_HEADER_CRC_OFFSET);
  tampered[CAPYOS_VOLUME_HEADER_CRC_OFFSET + 0] = (uint8_t)(fixed_crc & 0xFFu);
  tampered[CAPYOS_VOLUME_HEADER_CRC_OFFSET + 1] =
      (uint8_t)((fixed_crc >> 8) & 0xFFu);
  tampered[CAPYOS_VOLUME_HEADER_CRC_OFFSET + 2] =
      (uint8_t)((fixed_crc >> 16) & 0xFFu);
  tampered[CAPYOS_VOLUME_HEADER_CRC_OFFSET + 3] =
      (uint8_t)((fixed_crc >> 24) & 0xFFu);
  fails += expect_int(capyos_volume_header_parse(tampered, &parsed),
                      CAPYOS_VOLUME_HEADER_ERR_MAGIC,
                      "parse bad magic w/ fixed crc");
  fails += expect_true(buffer_all_zero((const uint8_t *)&parsed, sizeof(parsed)),
                       "parsed must be wiped on magic fail");

  /* Bad version. */
  memcpy(tampered, buf, sizeof(tampered));
  tampered[8] = 99u; /* version */
  fixed_crc = capyos_volume_header_crc32(tampered,
                                         CAPYOS_VOLUME_HEADER_CRC_OFFSET);
  for (int i = 0; i < 4; ++i) {
    tampered[CAPYOS_VOLUME_HEADER_CRC_OFFSET + i] =
        (uint8_t)((fixed_crc >> (8 * i)) & 0xFFu);
  }
  fails += expect_int(capyos_volume_header_parse(tampered, &parsed),
                      CAPYOS_VOLUME_HEADER_ERR_VERSION, "parse bad version");

  /* Bad CRC (single bit flip in the body, no CRC repair). */
  memcpy(tampered, buf, sizeof(tampered));
  tampered[20] ^= 0x01; /* tamper t_cost */
  fails += expect_int(capyos_volume_header_parse(tampered, &parsed),
                      CAPYOS_VOLUME_HEADER_ERR_CRC, "parse bad crc");

  /* Bad algo with fixed CRC. */
  memcpy(tampered, buf, sizeof(tampered));
  tampered[16] = 0x05; /* unknown algo */
  fixed_crc = capyos_volume_header_crc32(tampered,
                                         CAPYOS_VOLUME_HEADER_CRC_OFFSET);
  for (int i = 0; i < 4; ++i) {
    tampered[CAPYOS_VOLUME_HEADER_CRC_OFFSET + i] =
        (uint8_t)((fixed_crc >> (8 * i)) & 0xFFu);
  }
  fails += expect_int(capyos_volume_header_parse(tampered, &parsed),
                      CAPYOS_VOLUME_HEADER_ERR_ALGO, "parse bad algo");

  /* Non-zero flags. */
  memcpy(tampered, buf, sizeof(tampered));
  tampered[12] = 0x01;
  fixed_crc = capyos_volume_header_crc32(tampered,
                                         CAPYOS_VOLUME_HEADER_CRC_OFFSET);
  for (int i = 0; i < 4; ++i) {
    tampered[CAPYOS_VOLUME_HEADER_CRC_OFFSET + i] =
        (uint8_t)((fixed_crc >> (8 * i)) & 0xFFu);
  }
  fails += expect_int(capyos_volume_header_parse(tampered, &parsed),
                      CAPYOS_VOLUME_HEADER_ERR_FLAGS, "parse non-zero flags");

  /* Non-zero reserved byte. */
  memcpy(tampered, buf, sizeof(tampered));
  tampered[176] = 0xFF;
  fixed_crc = capyos_volume_header_crc32(tampered,
                                         CAPYOS_VOLUME_HEADER_CRC_OFFSET);
  for (int i = 0; i < 4; ++i) {
    tampered[CAPYOS_VOLUME_HEADER_CRC_OFFSET + i] =
        (uint8_t)((fixed_crc >> (8 * i)) & 0xFFu);
  }
  fails += expect_int(capyos_volume_header_parse(tampered, &parsed),
                      CAPYOS_VOLUME_HEADER_ERR_RESERVED,
                      "parse non-zero reserved");

  return fails;
}

/* ---- looks_valid quick gate ------------------------------------- */
static int test_looks_valid(void) {
  int fails = 0;
  struct capyos_volume_header hdr;
  uint8_t buf[CAPYOS_VOLUME_HEADER_SIZE];
  int rc = capyos_volume_header_init(
      &hdr, CAPYOS_VOLUME_KDF_ALGO_PBKDF2_SHA256, 16000u, 0u, kTestSalt16,
      sizeof(kTestSalt16), 1u, 1u, 0ULL, NULL);
  fails += expect_int(rc, CAPYOS_VOLUME_HEADER_OK, "looks_valid init");
  fails +=
      expect_int(capyos_volume_header_finalize_crc(&hdr),
                 CAPYOS_VOLUME_HEADER_OK, "looks_valid finalize");
  fails += expect_int(capyos_volume_header_serialize(&hdr, buf),
                      CAPYOS_VOLUME_HEADER_OK, "looks_valid serialize");
  fails += expect_int(capyos_volume_header_looks_valid(buf), 1,
                      "looks_valid valid");
  /* Bit-flip somewhere — must report not valid. */
  buf[100] ^= 0x01;
  fails += expect_int(capyos_volume_header_looks_valid(buf), 0,
                      "looks_valid corrupt");
  fails += expect_int(capyos_volume_header_looks_valid(NULL), 0,
                      "looks_valid NULL");
  /* All-zero buffer (= legacy unencrypted FS block boundary): rejected. */
  for (size_t i = 0; i < sizeof(buf); ++i) {
    buf[i] = 0;
  }
  fails += expect_int(capyos_volume_header_looks_valid(buf), 0,
                      "looks_valid all-zero");
  return fails;
}

/* ---- derive_keys success path (PBKDF2) -------------------------- */
static int test_derive_keys_pbkdf2_success(void) {
  int fails = 0;
  struct capyos_volume_header hdr;
  uint8_t k1[CRYPT_KEY_SIZE], k2[CRYPT_KEY_SIZE];
  int rc = capyos_volume_header_init(
      &hdr, CAPYOS_VOLUME_KDF_ALGO_PBKDF2_SHA256, /*t_cost*/ 4096u,
      /*m_cost*/ 0u, kTestSalt16, sizeof(kTestSalt16), 1u, 1u, 0ULL,
      "test-pbkdf2");
  fails += expect_int(rc, CAPYOS_VOLUME_HEADER_OK, "pbkdf2 init");
  /* Derive direct, then compute tag, then derive via header should
   * succeed and return matching keys. */
  uint8_t pre_k1[CRYPT_KEY_SIZE], pre_k2[CRYPT_KEY_SIZE];
  crypt_derive_xts_keys(TEST_PASSWORD, hdr.kdf_salt, hdr.kdf_salt_len,
                        hdr.kdf_t_cost, pre_k1, pre_k2);
  uint8_t tag[CAPYOS_VOLUME_HEADER_CHECK_TAG_SIZE];
  fails += expect_int(
      capyos_volume_header_compute_check_tag(&hdr, pre_k1, pre_k2, tag),
      CAPYOS_VOLUME_HEADER_OK, "pbkdf2 compute tag");
  fails += expect_int(
      capyos_volume_header_finalize_crc(&hdr), CAPYOS_VOLUME_HEADER_OK,
      "pbkdf2 finalize crc");
  fails += expect_int(
      capyos_volume_header_derive_keys(&hdr, TEST_PASSWORD, k1, k2),
      CAPYOS_VOLUME_HEADER_OK, "pbkdf2 derive");
  fails += expect_true(memcmp(k1, pre_k1, CRYPT_KEY_SIZE) == 0,
                       "pbkdf2 derive k1 matches direct");
  fails += expect_true(memcmp(k2, pre_k2, CRYPT_KEY_SIZE) == 0,
                       "pbkdf2 derive k2 matches direct");
  fails += expect_true(memcmp(k1, k2, CRYPT_KEY_SIZE) != 0,
                       "pbkdf2 k1 != k2");
  /* Wipe. */
  for (size_t i = 0; i < CRYPT_KEY_SIZE; ++i) {
    k1[i] = 0;
    k2[i] = 0;
    pre_k1[i] = 0;
    pre_k2[i] = 0;
  }
  for (size_t i = 0; i < sizeof(tag); ++i) tag[i] = 0;
  return fails;
}

/* ---- derive_keys success path (Argon2id) ------------------------ */
static int test_derive_keys_argon2id_success(void) {
  int fails = 0;
  struct capyos_volume_header hdr;
  uint8_t k1[CRYPT_KEY_SIZE], k2[CRYPT_KEY_SIZE];
  int rc = capyos_volume_header_init(
      &hdr, CAPYOS_VOLUME_KDF_ALGO_ARGON2ID, /*t_cost*/ 1u, /*m_cost*/ 8u,
      kTestSalt32, sizeof(kTestSalt32), 1u, 1u, 0ULL, "test-argon2id");
  fails += expect_int(rc, CAPYOS_VOLUME_HEADER_OK, "argon2id init");
  uint8_t pre_k1[CRYPT_KEY_SIZE], pre_k2[CRYPT_KEY_SIZE];
  int kdf_rc = crypt_derive_xts_keys_argon2id(
      TEST_PASSWORD, hdr.kdf_salt, hdr.kdf_salt_len, hdr.kdf_t_cost,
      hdr.kdf_m_cost, pre_k1, pre_k2);
  fails += expect_int(kdf_rc, 0, "argon2id direct kdf");
  uint8_t tag[CAPYOS_VOLUME_HEADER_CHECK_TAG_SIZE];
  fails += expect_int(
      capyos_volume_header_compute_check_tag(&hdr, pre_k1, pre_k2, tag),
      CAPYOS_VOLUME_HEADER_OK, "argon2id compute tag");
  fails += expect_int(
      capyos_volume_header_finalize_crc(&hdr), CAPYOS_VOLUME_HEADER_OK,
      "argon2id finalize");
  fails += expect_int(
      capyos_volume_header_derive_keys(&hdr, TEST_PASSWORD, k1, k2),
      CAPYOS_VOLUME_HEADER_OK, "argon2id derive");
  fails += expect_true(memcmp(k1, pre_k1, CRYPT_KEY_SIZE) == 0,
                       "argon2id derive k1 matches direct");
  fails += expect_true(memcmp(k2, pre_k2, CRYPT_KEY_SIZE) == 0,
                       "argon2id derive k2 matches direct");
  fails += expect_true(memcmp(k1, k2, CRYPT_KEY_SIZE) != 0,
                       "argon2id k1 != k2");
  /* Wipe. */
  for (size_t i = 0; i < CRYPT_KEY_SIZE; ++i) {
    k1[i] = 0;
    k2[i] = 0;
    pre_k1[i] = 0;
    pre_k2[i] = 0;
  }
  for (size_t i = 0; i < sizeof(tag); ++i) tag[i] = 0;
  return fails;
}

/* ---- derive_keys wrong password --------------------------------- */
static int test_derive_keys_wrong_password(void) {
  int fails = 0;
  struct capyos_volume_header hdr;
  uint8_t k1[CRYPT_KEY_SIZE], k2[CRYPT_KEY_SIZE];
  int rc = capyos_volume_header_init(
      &hdr, CAPYOS_VOLUME_KDF_ALGO_PBKDF2_SHA256, 4096u, 0u, kTestSalt16,
      sizeof(kTestSalt16), 1u, 1u, 0ULL, NULL);
  fails += expect_int(rc, CAPYOS_VOLUME_HEADER_OK, "wrong-pwd init");
  uint8_t correct_k1[CRYPT_KEY_SIZE], correct_k2[CRYPT_KEY_SIZE];
  crypt_derive_xts_keys(TEST_PASSWORD, hdr.kdf_salt, hdr.kdf_salt_len,
                        hdr.kdf_t_cost, correct_k1, correct_k2);
  uint8_t tag[CAPYOS_VOLUME_HEADER_CHECK_TAG_SIZE];
  fails += expect_int(
      capyos_volume_header_compute_check_tag(&hdr, correct_k1, correct_k2, tag),
      CAPYOS_VOLUME_HEADER_OK, "wrong-pwd tag");
  /* Mark with sentinel so we can confirm wipe. */
  for (size_t i = 0; i < CRYPT_KEY_SIZE; ++i) {
    k1[i] = 0xA5;
    k2[i] = 0xA5;
  }
  rc = capyos_volume_header_derive_keys(&hdr, "WRONG-PASSPHRASE", k1, k2);
  fails += expect_int(rc, CAPYOS_VOLUME_HEADER_ERR_CHECK_TAG,
                      "wrong-pwd rc must be ERR_CHECK_TAG");
  fails += expect_true(buffer_all_zero(k1, CRYPT_KEY_SIZE),
                       "wrong-pwd k1 must be wiped to zero");
  fails += expect_true(buffer_all_zero(k2, CRYPT_KEY_SIZE),
                       "wrong-pwd k2 must be wiped to zero");
  for (size_t i = 0; i < CRYPT_KEY_SIZE; ++i) {
    correct_k1[i] = 0;
    correct_k2[i] = 0;
  }
  for (size_t i = 0; i < sizeof(tag); ++i) tag[i] = 0;
  return fails;
}

/* ---- tampered salt yields check-tag failure --------------------- */
static int test_tampered_salt_rejected(void) {
  int fails = 0;
  struct capyos_volume_header hdr;
  uint8_t k1[CRYPT_KEY_SIZE], k2[CRYPT_KEY_SIZE];
  int rc = capyos_volume_header_init(
      &hdr, CAPYOS_VOLUME_KDF_ALGO_PBKDF2_SHA256, 4096u, 0u, kTestSalt16,
      sizeof(kTestSalt16), 1u, 1u, 0ULL, NULL);
  fails += expect_int(rc, CAPYOS_VOLUME_HEADER_OK, "tampered-salt init");
  uint8_t real_k1[CRYPT_KEY_SIZE], real_k2[CRYPT_KEY_SIZE];
  crypt_derive_xts_keys(TEST_PASSWORD, hdr.kdf_salt, hdr.kdf_salt_len,
                        hdr.kdf_t_cost, real_k1, real_k2);
  uint8_t tag[CAPYOS_VOLUME_HEADER_CHECK_TAG_SIZE];
  fails += expect_int(
      capyos_volume_header_compute_check_tag(&hdr, real_k1, real_k2, tag),
      CAPYOS_VOLUME_HEADER_OK, "tampered-salt tag");
  /* Now flip one byte of the salt (attacker swaps in a known weak
   * salt). Even with the correct password, the derived keys will be
   * different and the check-tag verification must fail. */
  hdr.kdf_salt[0] ^= 0x01;
  rc = capyos_volume_header_derive_keys(&hdr, TEST_PASSWORD, k1, k2);
  fails += expect_int(rc, CAPYOS_VOLUME_HEADER_ERR_CHECK_TAG,
                      "tampered salt must fail check_tag");
  fails += expect_true(buffer_all_zero(k1, CRYPT_KEY_SIZE),
                       "tampered-salt k1 wiped");
  fails += expect_true(buffer_all_zero(k2, CRYPT_KEY_SIZE),
                       "tampered-salt k2 wiped");
  for (size_t i = 0; i < CRYPT_KEY_SIZE; ++i) {
    real_k1[i] = 0;
    real_k2[i] = 0;
  }
  for (size_t i = 0; i < sizeof(tag); ++i) tag[i] = 0;
  return fails;
}

/* ---- algorithm downgrade attempt -------------------------------- */
static int test_algo_downgrade_rejected(void) {
  int fails = 0;
  /* Build a header where the legitimate creator chose Argon2id. An
   * attacker rewrites the header to declare PBKDF2 with a low t_cost
   * in hopes of forcing the boot path to derive a weaker key. The
   * check tag was computed under Argon2id params, so PBKDF2 with the
   * same salt yields a different key and the verification fails. */
  struct capyos_volume_header hdr;
  uint8_t k1[CRYPT_KEY_SIZE], k2[CRYPT_KEY_SIZE];
  int rc = capyos_volume_header_init(
      &hdr, CAPYOS_VOLUME_KDF_ALGO_ARGON2ID, 1u, 8u, kTestSalt32,
      sizeof(kTestSalt32), 1u, 1u, 0ULL, "argon2id-creator");
  fails += expect_int(rc, CAPYOS_VOLUME_HEADER_OK, "downgrade init");
  uint8_t real_k1[CRYPT_KEY_SIZE], real_k2[CRYPT_KEY_SIZE];
  int kdf_rc = crypt_derive_xts_keys_argon2id(
      TEST_PASSWORD, hdr.kdf_salt, hdr.kdf_salt_len, hdr.kdf_t_cost,
      hdr.kdf_m_cost, real_k1, real_k2);
  fails += expect_int(kdf_rc, 0, "downgrade real kdf");
  uint8_t tag[CAPYOS_VOLUME_HEADER_CHECK_TAG_SIZE];
  fails += expect_int(
      capyos_volume_header_compute_check_tag(&hdr, real_k1, real_k2, tag),
      CAPYOS_VOLUME_HEADER_OK, "downgrade tag");
  /* Attacker switches algo and params; tag is now bound to the
   * original Argon2id-derived keys. Note we must also normalise
   * m_cost to 0 for PBKDF2 so the param validator accepts the
   * tampered header (the validator does its job before derive). */
  hdr.kdf_algo_id = CAPYOS_VOLUME_KDF_ALGO_PBKDF2_SHA256;
  hdr.kdf_t_cost = 1000u; /* minimum accepted PBKDF2 cost */
  hdr.kdf_m_cost = 0u;
  rc = capyos_volume_header_derive_keys(&hdr, TEST_PASSWORD, k1, k2);
  fails += expect_int(rc, CAPYOS_VOLUME_HEADER_ERR_CHECK_TAG,
                      "algo downgrade must fail check_tag");
  fails += expect_true(buffer_all_zero(k1, CRYPT_KEY_SIZE),
                       "downgrade k1 wiped");
  fails += expect_true(buffer_all_zero(k2, CRYPT_KEY_SIZE),
                       "downgrade k2 wiped");
  for (size_t i = 0; i < CRYPT_KEY_SIZE; ++i) {
    real_k1[i] = 0;
    real_k2[i] = 0;
  }
  for (size_t i = 0; i < sizeof(tag); ++i) tag[i] = 0;
  return fails;
}

/* ---- derive_keys fail-closed on NULL ---------------------------- */
static int test_derive_keys_fail_closed(void) {
  int fails = 0;
  struct capyos_volume_header hdr;
  uint8_t k1[CRYPT_KEY_SIZE], k2[CRYPT_KEY_SIZE];
  int rc = capyos_volume_header_init(
      &hdr, CAPYOS_VOLUME_KDF_ALGO_PBKDF2_SHA256, 4096u, 0u, kTestSalt16,
      sizeof(kTestSalt16), 1u, 1u, 0ULL, NULL);
  fails += expect_int(rc, CAPYOS_VOLUME_HEADER_OK, "fc init");

  for (size_t i = 0; i < CRYPT_KEY_SIZE; ++i) {
    k1[i] = 0xA5;
    k2[i] = 0xA5;
  }
  rc = capyos_volume_header_derive_keys(NULL, TEST_PASSWORD, k1, k2);
  fails += expect_int(rc, CAPYOS_VOLUME_HEADER_ERR_NULL, "fc NULL hdr");
  fails += expect_true(buffer_all_zero(k1, CRYPT_KEY_SIZE),
                       "fc NULL hdr wipes k1");
  fails += expect_true(buffer_all_zero(k2, CRYPT_KEY_SIZE),
                       "fc NULL hdr wipes k2");

  for (size_t i = 0; i < CRYPT_KEY_SIZE; ++i) {
    k1[i] = 0xA5;
    k2[i] = 0xA5;
  }
  rc = capyos_volume_header_derive_keys(&hdr, NULL, k1, k2);
  fails += expect_int(rc, CAPYOS_VOLUME_HEADER_ERR_NULL, "fc NULL pwd");
  fails += expect_true(buffer_all_zero(k1, CRYPT_KEY_SIZE),
                       "fc NULL pwd wipes k1");
  fails += expect_true(buffer_all_zero(k2, CRYPT_KEY_SIZE),
                       "fc NULL pwd wipes k2");

  rc = capyos_volume_header_derive_keys(&hdr, TEST_PASSWORD, NULL, k2);
  fails += expect_int(rc, CAPYOS_VOLUME_HEADER_ERR_NULL, "fc NULL k1");
  rc = capyos_volume_header_derive_keys(&hdr, TEST_PASSWORD, k1, NULL);
  fails += expect_int(rc, CAPYOS_VOLUME_HEADER_ERR_NULL, "fc NULL k2");

  return fails;
}

int run_volume_header_tests(void) {
  int fails = 0;
  fails += test_crc32_known_vectors();
  fails += test_init_pbkdf2_happy();
  fails += test_init_argon2id_happy();
  fails += test_init_fail_closed();
  fails += test_serialize_parse_roundtrip();
  fails += test_parse_fail_closed();
  fails += test_looks_valid();
  fails += test_derive_keys_pbkdf2_success();
  fails += test_derive_keys_argon2id_success();
  fails += test_derive_keys_wrong_password();
  fails += test_tampered_salt_rejected();
  fails += test_algo_downgrade_rejected();
  fails += test_derive_keys_fail_closed();
  if (fails == 0) {
    printf("[tests] volume_header OK\n");
  } else {
    printf("[tests] volume_header FAILED %d\n", fails);
  }
  return fails;
}
