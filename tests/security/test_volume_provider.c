/*
 * Host-side tests for the alpha.222-alpha.224 volume provider.
 *
 * Strategy: back the block-device contract with an in-memory bytes
 * buffer so we can exercise the full write -> read roundtrip under
 * `volume_provider_install` and `volume_provider_open` without
 * touching real hardware. Tests use Argon2id minimums (t_cost=1,
 * m_cost=8 KiB) — production uses (t_cost=3, m_cost=8192 KiB) which
 * is bounded by the primitive's intrinsic cost rather than this test
 * suite.
 *
 * Coverage:
 *   - Install populates the header with the documented magic, version,
 *     algorithm, and offsets and zero-fills the rest of LBA 0.
 *   - Install + Open is a successful round-trip with matching keys.
 *   - Open with the wrong password fails CLEANLY (no key leak).
 *   - Open on a legacy volume (no header) succeeds via PBKDF2 fallback.
 *   - Once a header is on disk, Open NEVER falls back to legacy on
 *     authentication failure (downgrade protection).
 *   - NULL inputs, mismatched block sizes, and tiny devices are
 *     rejected fail-closed.
 *   - I/O failure on the header LBA fails the open path.
 *   - Round-tripping plaintext through the crypt layer recovers
 *     the original bytes after install.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs/block.h"
#include "fs/capyfs.h"
#include "security/crypt.h"
#include "security/csprng.h"
#include "security/volume_header.h"
#include "security/volume_provider.h"

#define TEST_BLOCK_SIZE 4096u
#define TEST_BLOCK_COUNT 32u
#define TEST_PASSWORD "alpha-222-passphrase"
#define TEST_BAD_PASSWORD "wrong-pass-on-purpose"

/* ---- in-memory block device backend ----------------------------- */

struct ram_dev {
  struct block_device dev;
  uint8_t *storage;
  int force_read_fail_lba; /* set to >= 0 to force a read failure on that LBA */
};

static int ram_read_block(void *ctx, uint32_t lba, void *buffer) {
  struct ram_dev *r = (struct ram_dev *)ctx;
  if (!r || !buffer) return -1;
  if (r->force_read_fail_lba >= 0 && (uint32_t)r->force_read_fail_lba == lba) {
    return -1;
  }
  if (lba >= r->dev.block_count) return -1;
  memcpy(buffer, r->storage + (size_t)lba * r->dev.block_size,
         r->dev.block_size);
  return 0;
}

static int ram_write_block(void *ctx, uint32_t lba, const void *buffer) {
  struct ram_dev *r = (struct ram_dev *)ctx;
  if (!r || !buffer) return -1;
  if (lba >= r->dev.block_count) return -1;
  memcpy(r->storage + (size_t)lba * r->dev.block_size, buffer,
         r->dev.block_size);
  return 0;
}

static struct block_device_ops g_ram_ops = {
    .read_block = ram_read_block,
    .write_block = ram_write_block,
};

static struct ram_dev *ram_alloc(uint32_t count) {
  struct ram_dev *r = (struct ram_dev *)calloc(1, sizeof(*r));
  if (!r) return NULL;
  r->storage = (uint8_t *)calloc((size_t)count, TEST_BLOCK_SIZE);
  if (!r->storage) {
    free(r);
    return NULL;
  }
  r->dev.name = "test-ram";
  r->dev.block_size = TEST_BLOCK_SIZE;
  r->dev.block_count = count;
  r->dev.ctx = r;
  r->dev.ops = &g_ram_ops;
  r->force_read_fail_lba = -1;
  return r;
}

static void ram_free(struct ram_dev *r) {
  if (!r) return;
  free(r->storage);
  free(r);
}

/* ---- assertion helpers ------------------------------------------ */

static int expect_int(int got, int want, const char *what) {
  if (got != want) {
    printf("[tests] volume_provider: %s expected %d, got %d\n", what, want,
           got);
    return 1;
  }
  return 0;
}

static int expect_true(int cond, const char *msg) {
  if (!cond) {
    printf("[tests] volume_provider: %s\n", msg);
    return 1;
  }
  return 0;
}

static void test_put_u32_le(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFFu);
  p[1] = (uint8_t)((v >> 8) & 0xFFu);
  p[2] = (uint8_t)((v >> 16) & 0xFFu);
  p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static int write_legacy_capyfs_super(struct ram_dev *r,
                                     const uint8_t *legacy_salt,
                                     size_t legacy_salt_len,
                                     uint32_t legacy_iter,
                                     uint32_t capyfs_blocks) {
  uint8_t k1[CRYPT_KEY_SIZE], k2[CRYPT_KEY_SIZE];
  uint8_t super[TEST_BLOCK_SIZE];
  struct block_device *legacy_crypt = NULL;
  uint32_t bits_per_block = CAPYFS_BLOCK_SIZE * 8u;
  uint32_t inode_count = 128u;
  uint32_t block_bitmap_blocks =
      (capyfs_blocks + bits_per_block - 1u) / bits_per_block;
  uint32_t inode_bitmap_blocks =
      (inode_count + bits_per_block - 1u) / bits_per_block;
  uint32_t inode_table_blocks =
      (inode_count * (uint32_t)sizeof(struct capy_inode_disk) +
       CAPYFS_BLOCK_SIZE - 1u) /
      CAPYFS_BLOCK_SIZE;
  uint32_t bmap_start = 1u;
  uint32_t imap_start = bmap_start + block_bitmap_blocks;
  uint32_t inode_start = imap_start + inode_bitmap_blocks;
  uint32_t data_start = inode_start + inode_table_blocks;
  int rc = -1;
  if (data_start >= capyfs_blocks) return -1;
  for (size_t i = 0; i < sizeof(super); ++i) super[i] = 0;
  test_put_u32_le(super + 0, CAPYFS_MAGIC);
  test_put_u32_le(super + 4, CAPYFS_VERSION);
  test_put_u32_le(super + 8, CAPYFS_BLOCK_SIZE);
  test_put_u32_le(super + 12, capyfs_blocks);
  test_put_u32_le(super + 16, inode_count);
  test_put_u32_le(super + 20, bmap_start);
  test_put_u32_le(super + 24, imap_start);
  test_put_u32_le(super + 28, inode_start);
  test_put_u32_le(super + 32, data_start);
  crypt_derive_xts_keys(TEST_PASSWORD, legacy_salt, legacy_salt_len,
                        legacy_iter, k1, k2);
  legacy_crypt = crypt_init(&r->dev, k1, k2);
  if (legacy_crypt && legacy_crypt != &r->dev &&
      block_device_write(legacy_crypt, 0u, super) == 0) {
    rc = 0;
  }
  if (legacy_crypt && legacy_crypt != &r->dev) crypt_free(legacy_crypt);
  for (size_t i = 0; i < CRYPT_KEY_SIZE; ++i) {
    k1[i] = 0;
    k2[i] = 0;
  }
  for (size_t i = 0; i < sizeof(super); ++i) super[i] = 0;
  return rc;
}

/* ---- install populates header ---------------------------------- */

static int test_install_writes_valid_header(void) {
  int fails = 0;
  struct ram_dev *r = ram_alloc(TEST_BLOCK_COUNT);
  fails += expect_true(r != NULL, "ram alloc");
  if (!r) return fails;

  struct block_device *crypt_dev = NULL;
  int rc = volume_provider_install(&r->dev, TEST_PASSWORD, &crypt_dev);
  fails += expect_int(rc, 0, "install rc");
  fails += expect_true(crypt_dev != NULL, "install crypt_dev non-null");

  /* The header lives in the first 512 bytes of LBA 0. The remainder
   * of that block (3584 bytes) MUST be zero so the parser's reserved-
   * all-zero invariant holds. */
  uint8_t lba0[TEST_BLOCK_SIZE];
  fails += expect_int(block_device_read(&r->dev, 0u, lba0), 0, "read lba0");
  fails += expect_true(capyos_volume_header_looks_valid(lba0) == 1,
                       "looks_valid after install");
  int padding_zero = 1;
  for (size_t i = 512; i < sizeof(lba0); ++i) {
    if (lba0[i] != 0) {
      padding_zero = 0;
      break;
    }
  }
  fails += expect_true(padding_zero, "lba0 padding after 512 must be zero");

  /* Parse the header and confirm the documented defaults landed. */
  struct capyos_volume_header hdr;
  fails += expect_int(capyos_volume_header_parse(lba0, &hdr),
                      CAPYOS_VOLUME_HEADER_OK, "parse header");
  fails += expect_int((int)hdr.kdf_algo_id,
                      (int)CAPYOS_VOLUME_KDF_ALGO_ARGON2ID,
                      "kdf is Argon2id");
  fails += expect_int((int)hdr.kdf_t_cost, (int)CRYPT_VOLUME_ARGON2ID_T_COST,
                      "t_cost matches default");
  fails += expect_int((int)hdr.kdf_m_cost, (int)CRYPT_VOLUME_ARGON2ID_M_COST,
                      "m_cost matches default");
  fails += expect_int(
      (int)hdr.data_offset_lba,
      (int)CAPYOS_VOLUME_HEADER_DEFAULT_DATA_OFFSET_LBA,
      "data_offset_lba matches default");

  /* crypt_dev should be wired through an offset wrapper that starts
   * at LBA 1, so its visible block_count is one less than the device. */
  fails += expect_int((int)crypt_dev->block_count, (int)(TEST_BLOCK_COUNT - 1u),
                      "crypt_dev->block_count = raw - 1");
  fails += expect_int((int)crypt_dev->block_size, (int)TEST_BLOCK_SIZE,
                      "crypt_dev->block_size = 4096");

  ram_free(r);
  return fails;
}

/* ---- open after install succeeds with same password ------------ */

static int test_open_after_install_roundtrip(void) {
  int fails = 0;
  struct ram_dev *r = ram_alloc(TEST_BLOCK_COUNT);
  if (!r) return 1;

  struct block_device *first_crypt = NULL;
  fails += expect_int(volume_provider_install(&r->dev, TEST_PASSWORD,
                                              &first_crypt),
                      0, "install rc");

  /* Write a deterministic pattern through the crypt layer to LBA 0
   * of the FS area, then re-open with the same password and verify
   * the plaintext survives the encryption + decryption round-trip. */
  uint8_t plain_in[TEST_BLOCK_SIZE];
  for (size_t i = 0; i < sizeof(plain_in); ++i) {
    plain_in[i] = (uint8_t)((i * 131u + 7u) & 0xFFu);
  }
  fails += expect_int(block_device_write(first_crypt, 0u, plain_in), 0,
                      "crypt write fs[0]");
  /* The on-disk LBA 1 (raw, AFTER the header) MUST now hold the
   * encrypted version of `plain_in` — it must NOT equal the
   * plaintext (otherwise we would not actually be encrypting). */
  fails += expect_true(memcmp(r->storage + TEST_BLOCK_SIZE, plain_in,
                              TEST_BLOCK_SIZE) != 0,
                       "raw LBA 1 must differ from plaintext");

  /* Re-open with the same password and read back. */
  struct block_device *second_crypt = NULL;
  fails += expect_int(volume_provider_open(&r->dev, TEST_PASSWORD, NULL, 0,
                                            0u, &second_crypt),
                      0, "open after install rc");
  fails += expect_true(second_crypt != NULL, "open returns crypt_dev");
  uint8_t plain_out[TEST_BLOCK_SIZE];
  fails += expect_int(block_device_read(second_crypt, 0u, plain_out), 0,
                      "crypt read after reopen");
  fails += expect_true(memcmp(plain_in, plain_out, sizeof(plain_in)) == 0,
                       "plaintext survives encryption round-trip");

  ram_free(r);
  return fails;
}

/* ---- open with wrong password fails cleanly -------------------- */

static int test_open_wrong_password_fails(void) {
  int fails = 0;
  struct ram_dev *r = ram_alloc(TEST_BLOCK_COUNT);
  if (!r) return 1;

  struct block_device *crypt_dev = NULL;
  fails += expect_int(
      volume_provider_install(&r->dev, TEST_PASSWORD, &crypt_dev), 0,
      "install rc");

  /* Wrong password MUST fail and MUST NOT leak a crypt_dev. The
   * provider does not distinguish wrong-password from tampered-
   * header in the return value, which is the documented contract. */
  struct block_device *opened = (struct block_device *)0xdeadbeefULL;
  fails += expect_int(volume_provider_open(&r->dev, TEST_BAD_PASSWORD, NULL,
                                            0, 0u, &opened),
                      -1, "open wrong pwd rc");
  fails += expect_true(opened == NULL, "open wrong pwd must null crypt_dev");

  ram_free(r);
  return fails;
}

/* ---- legacy volume mounts via PBKDF2 fallback ------------------ */

static int test_open_legacy_volume_succeeds(void) {
  int fails = 0;
  struct ram_dev *r = ram_alloc(TEST_BLOCK_COUNT);
  if (!r) return 1;

  /* Legacy salt + iter (same construction the pre-alpha.222 kernel
   * used). The device has NO header on disk — all blocks are zero. */
  static const uint8_t legacy_salt[16] = {
      0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
      0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00};
  const uint32_t legacy_iter = 1000u;

  /* Pre-write a recognizable plaintext block at LBA 0 of the FS via
   * a temporary crypt layer derived with the legacy parameters
   * exactly as the old kernel would have. */
  uint8_t k1[CRYPT_KEY_SIZE], k2[CRYPT_KEY_SIZE];
  crypt_derive_xts_keys(TEST_PASSWORD, legacy_salt, sizeof(legacy_salt),
                        legacy_iter, k1, k2);
  struct block_device *legacy_crypt = crypt_init(&r->dev, k1, k2);
  fails += expect_true(legacy_crypt != NULL && legacy_crypt != &r->dev,
                       "legacy crypt_init");
  uint8_t plain[TEST_BLOCK_SIZE];
  for (size_t i = 0; i < sizeof(plain); ++i) plain[i] = (uint8_t)(i & 0xFFu);
  fails += expect_int(block_device_write(legacy_crypt, 0u, plain), 0,
                      "legacy write");
  crypt_free(legacy_crypt);

  /* Confirm there is no valid header on disk — the legacy ciphertext
   * at LBA 0 has random-looking bytes that almost certainly do not
   * satisfy magic + CRC. */
  uint8_t lba0[TEST_BLOCK_SIZE];
  fails += expect_int(block_device_read(&r->dev, 0u, lba0), 0,
                      "raw read lba0");
  fails += expect_int(capyos_volume_header_looks_valid(lba0), 0,
                      "legacy disk must NOT look valid");

  /* Now open via the provider with the legacy parameters and verify
   * the plaintext round-trips. */
  struct block_device *opened = NULL;
  fails += expect_int(volume_provider_open(&r->dev, TEST_PASSWORD, legacy_salt,
                                            sizeof(legacy_salt), legacy_iter,
                                            &opened),
                      0, "legacy open rc");
  fails += expect_true(opened != NULL, "legacy open returns crypt_dev");
  uint8_t plain_out[TEST_BLOCK_SIZE];
  fails += expect_int(block_device_read(opened, 0u, plain_out), 0,
                      "legacy crypt read");
  fails += expect_true(memcmp(plain, plain_out, sizeof(plain)) == 0,
                       "legacy plaintext round-trip");
  /* Legacy crypt_dev wraps the whole device: block_count unchanged. */
  fails += expect_int((int)opened->block_count, (int)TEST_BLOCK_COUNT,
                      "legacy crypt covers full device");

  for (size_t i = 0; i < CRYPT_KEY_SIZE; ++i) {
    k1[i] = 0;
    k2[i] = 0;
  }
  ram_free(r);
  return fails;
}

/* ---- modern volume rejects downgrade attack -------------------- */

static int test_open_no_legacy_fallback_when_header_present(void) {
  int fails = 0;
  struct ram_dev *r = ram_alloc(TEST_BLOCK_COUNT);
  if (!r) return 1;

  struct block_device *crypt_dev = NULL;
  fails += expect_int(
      volume_provider_install(&r->dev, TEST_PASSWORD, &crypt_dev), 0,
      "install rc");

  /* Legacy salt unused on header-managed volumes. Even if the caller
   * supplies legacy parameters, the open MUST fail with the wrong
   * password rather than silently fall back. Failure to enforce this
   * would let an attacker who can write the disk replace the header
   * with garbage and induce a downgrade. */
  static const uint8_t legacy_salt[16] = {0};
  struct block_device *opened = (struct block_device *)0xdeadbeefULL;
  fails += expect_int(volume_provider_open(&r->dev, TEST_BAD_PASSWORD,
                                            legacy_salt, sizeof(legacy_salt),
                                            1000u, &opened),
                      -1, "wrong pwd with legacy fallback supplied");
  fails += expect_true(opened == NULL,
                       "wrong pwd must NOT leak a crypt_dev via legacy path");

  ram_free(r);
  return fails;
}

/* ---- fail-closed contract -------------------------------------- */

static int test_install_fail_closed(void) {
  int fails = 0;
  struct ram_dev *r = ram_alloc(TEST_BLOCK_COUNT);
  if (!r) return 1;
  struct block_device *out = (struct block_device *)0xdeadbeefULL;

  fails += expect_int(volume_provider_install(NULL, TEST_PASSWORD, &out), -1,
                      "install NULL device");
  fails += expect_true(out == NULL, "out cleared on NULL device");

  out = (struct block_device *)0xdeadbeefULL;
  fails += expect_int(volume_provider_install(&r->dev, NULL, &out), -1,
                      "install NULL password");
  fails += expect_true(out == NULL, "out cleared on NULL password");

  fails += expect_int(volume_provider_install(&r->dev, TEST_PASSWORD, NULL),
                      -1, "install NULL out");

  /* Wrong block size. */
  struct ram_dev *small_block = ram_alloc(TEST_BLOCK_COUNT);
  if (small_block) {
    small_block->dev.block_size = 512u;
    out = (struct block_device *)0xdeadbeefULL;
    fails += expect_int(
        volume_provider_install(&small_block->dev, TEST_PASSWORD, &out), -1,
        "install 512B device");
    fails += expect_true(out == NULL, "out cleared on bad block size");
    ram_free(small_block);
  }

  /* Tiny device (only header block). */
  struct ram_dev *tiny = ram_alloc(1u);
  if (tiny) {
    out = (struct block_device *)0xdeadbeefULL;
    fails += expect_int(volume_provider_install(&tiny->dev, TEST_PASSWORD,
                                                &out),
                        -1, "install 1-block device");
    fails += expect_true(out == NULL, "out cleared on tiny device");
    ram_free(tiny);
  }

  ram_free(r);
  return fails;
}

static int test_open_fail_closed(void) {
  int fails = 0;
  struct ram_dev *r = ram_alloc(TEST_BLOCK_COUNT);
  if (!r) return 1;
  struct block_device *out = (struct block_device *)0xdeadbeefULL;

  fails += expect_int(volume_provider_open(NULL, TEST_PASSWORD, NULL, 0, 0u,
                                            &out),
                      -1, "open NULL device");
  fails += expect_true(out == NULL, "out cleared on NULL device");

  out = (struct block_device *)0xdeadbeefULL;
  fails += expect_int(volume_provider_open(&r->dev, NULL, NULL, 0, 0u, &out),
                      -1, "open NULL password");
  fails += expect_true(out == NULL, "out cleared on NULL password");

  fails += expect_int(volume_provider_open(&r->dev, TEST_PASSWORD, NULL, 0,
                                            0u, NULL),
                      -1, "open NULL out");

  /* Legacy parameters required when no header is present and the
   * caller supplied no legacy material — provider returns -1. */
  out = (struct block_device *)0xdeadbeefULL;
  fails += expect_int(volume_provider_open(&r->dev, TEST_PASSWORD, NULL, 0,
                                            0u, &out),
                      -1, "open no header & no legacy");
  fails += expect_true(out == NULL,
                       "out cleared on missing legacy parameters");

  ram_free(r);
  return fails;
}

/* ---- I/O error path -------------------------------------------- */

static int test_open_io_failure(void) {
  int fails = 0;
  struct ram_dev *r = ram_alloc(TEST_BLOCK_COUNT);
  if (!r) return 1;
  struct block_device *crypt_dev = NULL;
  fails += expect_int(
      volume_provider_install(&r->dev, TEST_PASSWORD, &crypt_dev), 0,
      "install rc");

  /* Force the header read to fail and confirm the provider refuses
   * to mount rather than mounting garbage or falling through to the
   * legacy path. */
  r->force_read_fail_lba = 0;
  struct block_device *opened = (struct block_device *)0xdeadbeefULL;
  fails += expect_int(volume_provider_open(&r->dev, TEST_PASSWORD, NULL, 0,
                                            0u, &opened),
                      -1, "open rc with forced I/O error");
  fails += expect_true(opened == NULL, "I/O error must not leak crypt_dev");
  r->force_read_fail_lba = -1;

  ram_free(r);
  return fails;
}

/* ---- two installs in the same buffer produce different salts --- */

static int test_install_uses_fresh_salt_each_time(void) {
  int fails = 0;
  struct ram_dev *a = ram_alloc(TEST_BLOCK_COUNT);
  struct ram_dev *b = ram_alloc(TEST_BLOCK_COUNT);
  if (!a || !b) {
    ram_free(a);
    ram_free(b);
    return 1;
  }
  struct block_device *ca = NULL;
  struct block_device *cb = NULL;
  fails += expect_int(volume_provider_install(&a->dev, TEST_PASSWORD, &ca), 0,
                      "install a");
  fails += expect_int(volume_provider_install(&b->dev, TEST_PASSWORD, &cb), 0,
                      "install b");
  uint8_t hdr_a[TEST_BLOCK_SIZE];
  uint8_t hdr_b[TEST_BLOCK_SIZE];
  fails += expect_int(block_device_read(&a->dev, 0u, hdr_a), 0, "read hdr_a");
  fails += expect_int(block_device_read(&b->dev, 0u, hdr_b), 0, "read hdr_b");
  struct capyos_volume_header pa, pb;
  fails += expect_int(capyos_volume_header_parse(hdr_a, &pa),
                      CAPYOS_VOLUME_HEADER_OK, "parse a");
  fails += expect_int(capyos_volume_header_parse(hdr_b, &pb),
                      CAPYOS_VOLUME_HEADER_OK, "parse b");
  /* Two CSPRNG-generated salts MUST differ. Probability of
   * collision is 2^-128, indistinguishable from a regression in
   * the entropy path. */
  fails += expect_true(memcmp(pa.kdf_salt, pb.kdf_salt, pa.kdf_salt_len) != 0,
                       "two installs must produce different salts");

  ram_free(a);
  ram_free(b);
  return fails;
}

int run_volume_provider_tests(void) {
  int fails = 0;
  fails += test_install_writes_valid_header();
  fails += test_open_after_install_roundtrip();
  fails += test_open_wrong_password_fails();
  fails += test_open_legacy_volume_succeeds();
  fails += test_open_no_legacy_fallback_when_header_present();
  fails += test_install_fail_closed();
  fails += test_open_fail_closed();
  fails += test_open_io_failure();
  fails += test_install_uses_fresh_salt_each_time();
  if (fails == 0) {
    printf("[tests] volume_provider OK\n");
  } else {
    printf("[tests] volume_provider FAILED %d\n", fails);
  }
  return fails;
}
