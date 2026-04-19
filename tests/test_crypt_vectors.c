#include <stdio.h>
#include <string.h>

#include "fs/block.h"
#include "memory/kmem.h"
#include "security/crypt.h"

#define TEST_BLOCK_SIZE 4096u
#define TEST_BLOCK_COUNT 8u

struct mem_block_device {
  struct block_device dev;
  uint8_t data[TEST_BLOCK_COUNT][TEST_BLOCK_SIZE];
};

static int mem_read_block(void *ctx, uint32_t block_no, void *buffer) {
  struct mem_block_device *mem = (struct mem_block_device *)ctx;
  if (!mem || !buffer || block_no >= TEST_BLOCK_COUNT) {
    return -1;
  }
  memcpy(buffer, mem->data[block_no], TEST_BLOCK_SIZE);
  return 0;
}

static int mem_write_block(void *ctx, uint32_t block_no, const void *buffer) {
  struct mem_block_device *mem = (struct mem_block_device *)ctx;
  if (!mem || !buffer || block_no >= TEST_BLOCK_COUNT) {
    return -1;
  }
  memcpy(mem->data[block_no], buffer, TEST_BLOCK_SIZE);
  return 0;
}

static const struct block_device_ops k_mem_block_ops = {
    .read_block = mem_read_block,
    .write_block = mem_write_block,
};

struct mem_lba_device {
  struct block_device dev;
  uint8_t data[TEST_BLOCK_COUNT * 8u * 512u];
};

static int mem_lba_read_block(void *ctx, uint32_t block_no, void *buffer) {
  struct mem_lba_device *mem = (struct mem_lba_device *)ctx;
  if (!mem || !buffer || block_no >= mem->dev.block_count) {
    return -1;
  }
  memcpy(buffer, mem->data + (size_t)block_no * mem->dev.block_size,
         mem->dev.block_size);
  return 0;
}

static int mem_lba_write_block(void *ctx, uint32_t block_no,
                               const void *buffer) {
  struct mem_lba_device *mem = (struct mem_lba_device *)ctx;
  if (!mem || !buffer || block_no >= mem->dev.block_count) {
    return -1;
  }
  memcpy(mem->data + (size_t)block_no * mem->dev.block_size, buffer,
         mem->dev.block_size);
  return 0;
}

static const struct block_device_ops k_mem_lba_ops = {
    .read_block = mem_lba_read_block,
    .write_block = mem_lba_write_block,
};

static uint8_t hex_nibble(char ch) {
  if (ch >= '0' && ch <= '9') {
    return (uint8_t)(ch - '0');
  }
  if (ch >= 'a' && ch <= 'f') {
    return (uint8_t)(10 + (ch - 'a'));
  }
  if (ch >= 'A' && ch <= 'F') {
    return (uint8_t)(10 + (ch - 'A'));
  }
  return 0xFFu;
}

static int decode_hex(const char *hex, uint8_t *out, size_t out_len) {
  size_t i = 0u;
  if (!hex || !out) {
    return -1;
  }
  for (; i < out_len; ++i) {
    uint8_t hi = hex_nibble(hex[i * 2u]);
    uint8_t lo = hex_nibble(hex[i * 2u + 1u]);
    if (hi > 0x0Fu || lo > 0x0Fu) {
      return -1;
    }
    out[i] = (uint8_t)((hi << 4) | lo);
  }
  return 0;
}

static int expect_hex(const char *name, const uint8_t *actual, size_t len,
                      const char *expected_hex) {
  uint8_t expected[64];
  if (!actual || !expected_hex || len > sizeof(expected) ||
      decode_hex(expected_hex, expected, len) != 0) {
    printf("[crypt_vectors] %s invalid test vector setup\n", name);
    return 1;
  }
  if (memcmp(actual, expected, len) != 0) {
    printf("[crypt_vectors] %s mismatch\n", name);
    return 1;
  }
  return 0;
}

static int test_sha256_vectors(void) {
  static const char *k_expected_abc =
      "ba7816bf8f01cfea414140de5dae2223"
      "b00361a396177a9cb410ff61f20015ad";
  struct sha256_ctx ctx;
  uint8_t digest[SHA256_DIGEST_SIZE];

  sha256_init(&ctx);
  sha256_update(&ctx, (const uint8_t *)"abc", 3u);
  sha256_final(&ctx, digest);
  return expect_hex("sha256(abc)", digest, sizeof(digest), k_expected_abc);
}

static int test_pbkdf2_vectors(void) {
  static const char *k_expected_iter1 =
      "120fb6cffcf8b32c43e7225256c4f837"
      "a86548c92ccc35480805987cb70be17b";
  static const char *k_expected_iter2 =
      "ae4d0c95af6b46d32d0adff928f06dd0"
      "2a303f8ef3c251dfd6e2d85a95474c43";
  static const char *k_expected_iter4096 =
      "c5e478d59288c841aa530db6845c4c8d"
      "962893a001ce4e11a4963873aa98134a";
  uint8_t derived[32];
  int fails = 0;

  crypt_pbkdf2_sha256((const uint8_t *)"password", 8u,
                      (const uint8_t *)"salt", 4u, 1u, derived,
                      sizeof(derived));
  fails += expect_hex("pbkdf2-iter1", derived, sizeof(derived),
                      k_expected_iter1);

  crypt_pbkdf2_sha256((const uint8_t *)"password", 8u,
                      (const uint8_t *)"salt", 4u, 2u, derived,
                      sizeof(derived));
  fails += expect_hex("pbkdf2-iter2", derived, sizeof(derived),
                      k_expected_iter2);

  crypt_pbkdf2_sha256((const uint8_t *)"password", 8u,
                      (const uint8_t *)"salt", 4u, 4096u, derived,
                      sizeof(derived));
  fails += expect_hex("pbkdf2-iter4096", derived, sizeof(derived),
                      k_expected_iter4096);
  return fails;
}

static int test_aes_xts_vector(void) {
  static const char *k_expected_prefix =
      "b2d9289b998ebd6bcc8a6d434711b8af"
      "6f974e732a6253c9ed6c74de21b56629"
      "bc87fef9fde088fd9009a8357801561c"
      "03be40ad3367f539c9846dd3b5bb4185";
  static const char *k_expected_suffix =
      "6f9f3611b93e0586a4c028e0fd580e78"
      "3b2c255d2212d4b4dc8be7a28f72f4b8"
      "2c58a94c93ac4af2347dd11dcfb519b4"
      "6b53ff40ed0f2b75d4ec3f3418f64d50";
  static const char *k_expected_cipher_hash =
      "909a6b23e4bc86883003399fee8b8096"
      "3f30998474b510f3dcadb79e539bd2d7";
  struct mem_block_device mem;
  struct block_device *crypt_dev = NULL;
  uint8_t key1[CRYPT_KEY_SIZE];
  uint8_t key2[CRYPT_KEY_SIZE];
  uint8_t plain[TEST_BLOCK_SIZE];
  uint8_t roundtrip[TEST_BLOCK_SIZE];
  uint8_t digest[SHA256_DIGEST_SIZE];
  struct sha256_ctx sha;
  int fails = 0;

  memset(&mem, 0, sizeof(mem));
  mem.dev.name = "mem";
  mem.dev.block_size = TEST_BLOCK_SIZE;
  mem.dev.block_count = TEST_BLOCK_COUNT;
  mem.dev.ctx = &mem;
  mem.dev.ops = &k_mem_block_ops;

  for (uint32_t i = 0; i < CRYPT_KEY_SIZE; ++i) {
    key1[i] = (uint8_t)i;
    key2[i] = (uint8_t)(i + CRYPT_KEY_SIZE);
  }
  for (uint32_t i = 0; i < TEST_BLOCK_SIZE; ++i) {
    plain[i] = (uint8_t)(i & 0xFFu);
  }

  crypt_dev = crypt_init(&mem.dev, key1, key2);
  if (!crypt_dev) {
    printf("[crypt_vectors] crypt_init failed\n");
    return 1;
  }
  if (block_device_write(crypt_dev, 7u, plain) != 0) {
    printf("[crypt_vectors] encrypted block write failed\n");
    crypt_free(crypt_dev);
    return 1;
  }
  if (block_device_read(crypt_dev, 7u, roundtrip) != 0) {
    printf("[crypt_vectors] encrypted block read failed\n");
    crypt_free(crypt_dev);
    return 1;
  }
  if (memcmp(plain, roundtrip, sizeof(plain)) != 0) {
    printf("[crypt_vectors] XTS roundtrip mismatch\n");
    fails++;
  }

  fails += expect_hex("xts-prefix", mem.data[7], 64u, k_expected_prefix);
  fails += expect_hex("xts-suffix", &mem.data[7][TEST_BLOCK_SIZE - 64u], 64u,
                      k_expected_suffix);

  sha256_init(&sha);
  sha256_update(&sha, mem.data[7], TEST_BLOCK_SIZE);
  sha256_final(&sha, digest);
  fails += expect_hex("xts-sha256", digest, sizeof(digest),
                      k_expected_cipher_hash);

  crypt_free(crypt_dev);
  return fails;
}

static int test_block0_with_wrappers(void) {
  struct mem_lba_device mem512;
  struct block_device base512;
  struct block_device *slice = NULL;
  struct block_device *chunked = NULL;
  struct block_device *crypt_dev = NULL;
  uint8_t key1[CRYPT_KEY_SIZE];
  uint8_t key2[CRYPT_KEY_SIZE];
  uint8_t *plain = NULL;
  uint8_t *roundtrip = NULL;
  int fails = 0;

  memset(&mem512, 0, sizeof(mem512));
  memset(&base512, 0, sizeof(base512));
  mem512.dev.name = "mem512";
  mem512.dev.block_size = 512u;
  mem512.dev.block_count = TEST_BLOCK_COUNT * 8u;
  mem512.dev.ctx = &mem512;
  mem512.dev.ops = &k_mem_lba_ops;

  base512 = mem512.dev;
  slice = block_offset_wrap(&base512, 8u, TEST_BLOCK_COUNT * 8u - 8u);
  chunked = slice ? block_chunked_wrap(slice, TEST_BLOCK_SIZE) : NULL;
  if (!slice || !chunked) {
    printf("[crypt_vectors] wrapper chain creation failed\n");
    fails = 1;
    goto cleanup;
  }

  for (uint32_t i = 0; i < CRYPT_KEY_SIZE; ++i) {
    key1[i] = (uint8_t)(0xA0u + i);
    key2[i] = (uint8_t)(0xC0u + i);
  }

  crypt_dev = crypt_init(chunked, key1, key2);
  if (!crypt_dev) {
    printf("[crypt_vectors] crypt_init(wrapper chain) failed\n");
    fails = 1;
    goto cleanup;
  }

  plain = (uint8_t *)kalloc(TEST_BLOCK_SIZE);
  roundtrip = (uint8_t *)kalloc(TEST_BLOCK_SIZE);
  if (!plain || !roundtrip) {
    printf("[crypt_vectors] allocation failed\n");
    fails = 1;
    goto cleanup;
  }

  memset(plain, 0, TEST_BLOCK_SIZE);
  plain[0] = 0x53u;
  plain[1] = 0x46u;
  plain[2] = 0x52u;
  plain[3] = 0x4Eu;
  plain[4] = 0x02u;
  plain[8] = 0x00u;
  plain[9] = 0x10u;
  plain[10] = 0x00u;
  plain[11] = 0x00u;
  for (uint32_t i = 32u; i < TEST_BLOCK_SIZE; ++i) {
    plain[i] = (uint8_t)((i * 13u + 7u) & 0xFFu);
  }

  if (block_device_write(crypt_dev, 0u, plain) != 0) {
    printf("[crypt_vectors] wrapper-chain block0 write failed\n");
    fails = 1;
    goto cleanup;
  }
  memset(roundtrip, 0, TEST_BLOCK_SIZE);
  if (block_device_read(crypt_dev, 0u, roundtrip) != 0) {
    printf("[crypt_vectors] wrapper-chain block0 read failed\n");
    fails = 1;
    goto cleanup;
  }
  if (memcmp(plain, roundtrip, TEST_BLOCK_SIZE) != 0) {
    printf("[crypt_vectors] wrapper-chain block0 roundtrip mismatch: "
           "got %02X%02X%02X%02X expected %02X%02X%02X%02X\n",
           roundtrip[0], roundtrip[1], roundtrip[2], roundtrip[3],
           plain[0], plain[1], plain[2], plain[3]);
    fails = 1;
  }

cleanup:
  if (plain) {
    kfree(plain);
  }
  if (roundtrip) {
    kfree(roundtrip);
  }
  if (crypt_dev) {
    crypt_free(crypt_dev);
  }
  if (chunked) {
    kfree(chunked->ctx);
    kfree(chunked);
  }
  if (slice) {
    kfree(slice->ctx);
    kfree(slice);
  }
  return fails;
}

int run_crypt_vector_tests(void) {
  int fails = 0;
  fails += test_sha256_vectors();
  fails += test_pbkdf2_vectors();
  fails += test_aes_xts_vector();
  fails += test_block0_with_wrappers();
  if (fails == 0) {
    printf("[tests] crypt_vectors OK\n");
  }
  return fails;
}
