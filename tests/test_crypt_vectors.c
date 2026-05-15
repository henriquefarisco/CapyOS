#include <stdio.h>
#include <string.h>

#include "fs/block.h"
#include "memory/kmem.h"
#include "security/argon2.h"
#include "security/blake2b.h"
#include "security/chacha20_poly1305.h"
#include "security/crypt.h"
#include "security/ed25519.h"
#include "security/x25519.h"

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
  /* Buffer sized to fit the largest cryptographic vector tested in
   * this file. PBKDF2 emits 32 bytes; HKDF-SHA256 RFC 5869 Test Case 2
   * produces a 82-byte OKM (L = 82). 256 leaves headroom for future
   * vectors without per-call reallocations and stays well within the
   * test stack budget. */
  uint8_t expected[256];
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

static int test_constant_time_compare_semantics(void) {
  uint8_t a[32];
  uint8_t b[32];
  int fails = 0;
  for (size_t i = 0; i < sizeof(a); ++i) {
    a[i] = (uint8_t)((i * 7u + 11u) & 0xFFu);
    b[i] = a[i];
  }
  if (crypt_constant_time_compare(a, b, sizeof(a)) != 0) {
    printf("[crypt_vectors] constant-time compare must return 0 for equal buffers\n");
    fails++;
  }
  b[0] ^= 0x01u;
  if (crypt_constant_time_compare(a, b, sizeof(a)) == 0) {
    printf("[crypt_vectors] constant-time compare must detect first-byte mismatch\n");
    fails++;
  }
  b[0] = a[0];
  b[sizeof(b) - 1] ^= 0x80u;
  if (crypt_constant_time_compare(a, b, sizeof(a)) == 0) {
    printf("[crypt_vectors] constant-time compare must detect last-byte mismatch\n");
    fails++;
  }
  b[sizeof(b) - 1] = a[sizeof(a) - 1];
  if (crypt_constant_time_compare(a, b, 0u) != 0) {
    printf("[crypt_vectors] constant-time compare must accept zero-length input\n");
    fails++;
  }
  return fails;
}

static int test_sha256_clear_semantics(void) {
  struct sha256_ctx ctx;
  int fails = 0;
  uint8_t hash[SHA256_DIGEST_SIZE];
  const uint8_t input[] = "capyos-sha256-clear-test";

  /* Drive the context through update + final so it carries real state in
   * both `state[]` (the produced digest) and `data[]` (the last padded
   * block). After `sha256_clear`, the entire context must be zero. */
  sha256_init(&ctx);
  sha256_update(&ctx, input, sizeof(input) - 1u);
  sha256_final(&ctx, hash);
  sha256_clear(&ctx);

  const uint8_t *raw = (const uint8_t *)&ctx;
  for (size_t i = 0; i < sizeof(ctx); ++i) {
    if (raw[i] != 0u) {
      printf("[crypt_vectors] sha256_clear must zero every byte of the context (offset %zu = 0x%02x)\n",
             i, raw[i]);
      fails++;
      break;
    }
  }

  /* The function must be safe to call on a NULL context (no segfault, no
   * side effects). We can't observe "no side effects" directly, but we
   * can at least confirm the call returns without crashing the host. */
  sha256_clear(NULL);
  return fails;
}

/*
 * Ed25519 (RFC 8032 §7.1) test vectors.
 *
 * Em alpha.217 a implementacao real substituiu o esqueleto fail-closed
 * de alpha.210. Os testes contratuais antigos (que afirmavam que verify
 * sempre retorna -1 e sign/create_keypair sempre zeram outputs) foram
 * substituidos por testes baseados nos vetores oficiais RFC 8032 §7.1
 * + cobertura de fail-closed para inputs invalidos.
 *
 * Reutiliza `decode_hex`/`hex_nibble` declarados no topo deste arquivo.
 */

static int test_ed25519_failclosed_contract(void) {
  /*
   * Contrato em alpha.217+:
   *   - ed25519_verify retorna 0 para signature criptograficamente
   *     valida e -1 para tudo o resto.
   *   - ed25519_sign produz signature criptograficamente valida que
   *     ed25519_verify aceita.
   *   - ed25519_create_keypair produz (pk, sk) que assina/verifica
   *     end-to-end.
   *
   * Cobre:
   *   1. Tres vetores oficiais RFC 8032 §7.1.
   *   2. Round-trip create_keypair -> sign -> verify.
   *   3. Fail-closed em tampering (signature/message/pk).
   *   4. Fail-closed em S nao canonico.
   *   5. Fail-closed em NULL inputs.
   *   6. Determinismo: assinar a mesma mensagem com a mesma chave
   *      produz a mesma signature.
   */
  int fails = 0;

  struct {
    const char *seed_hex;
    const char *pk_hex;
    const char *sig_hex;
    const char *msg_hex;
    size_t msg_len;
  } vectors[] = {
      /* RFC 8032 §7.1 Test 1: empty message */
      {"9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
       "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
       "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555f"
       "b8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b",
       "", 0u},
      /* RFC 8032 §7.1 Test 2: 1-byte message 0x72 */
      {"4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
       "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c",
       "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da08"
       "5ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00",
       "72", 1u},
      /* RFC 8032 §7.1 Test 3: 2-byte message 0xaf82 */
      {"c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7",
       "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025",
       "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18"
       "ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a",
       "af82", 2u},
  };

  for (size_t v = 0; v < sizeof(vectors) / sizeof(vectors[0]); ++v) {
    uint8_t seed[32], pk_expected[32], sig_expected[64], msg[8];
    if (decode_hex(vectors[v].seed_hex, seed, 32) != 0 ||
        decode_hex(vectors[v].pk_hex, pk_expected, 32) != 0 ||
        decode_hex(vectors[v].sig_hex, sig_expected, 64) != 0) {
      printf("[tests] ed25519 vec %zu: hex decode\n", v);
      fails++;
      continue;
    }
    if (vectors[v].msg_len > 0u) {
      if (decode_hex(vectors[v].msg_hex, msg, vectors[v].msg_len) != 0) {
        printf("[tests] ed25519 vec %zu: msg hex\n", v);
        fails++;
        continue;
      }
    }
    /* create_keypair derives pk from seed. */
    uint8_t pk[32], sk[64];
    ed25519_create_keypair(pk, sk, seed);
    if (memcmp(pk, pk_expected, 32) != 0) {
      printf("[tests] ed25519 vec %zu: pk mismatch\n", v);
      fails++;
    }
    /* sign produces expected signature (deterministic). */
    uint8_t sig[64];
    ed25519_sign(sig, vectors[v].msg_len ? msg : NULL, vectors[v].msg_len,
                 pk, sk);
    if (memcmp(sig, sig_expected, 64) != 0) {
      printf("[tests] ed25519 vec %zu: sig mismatch\n", v);
      fails++;
    }
    /* verify accepts the expected signature. */
    if (ed25519_verify(sig_expected, vectors[v].msg_len ? msg : NULL,
                       vectors[v].msg_len, pk_expected) != 0) {
      printf("[tests] ed25519 vec %zu: verify rejected canonical sig\n", v);
      fails++;
    }
    /* Tampering rejection: flip a bit in signature. */
    uint8_t tampered[64];
    memcpy(tampered, sig_expected, 64);
    tampered[0] ^= 0x01;
    if (ed25519_verify(tampered, vectors[v].msg_len ? msg : NULL,
                       vectors[v].msg_len, pk_expected) == 0) {
      printf("[tests] ed25519 vec %zu: accepted tampered sig[0]\n", v);
      fails++;
    }
    memcpy(tampered, sig_expected, 64);
    tampered[32] ^= 0x01;
    if (ed25519_verify(tampered, vectors[v].msg_len ? msg : NULL,
                       vectors[v].msg_len, pk_expected) == 0) {
      printf("[tests] ed25519 vec %zu: accepted tampered sig[32]\n", v);
      fails++;
    }
    /* Wrong public key rejection. */
    uint8_t wrong_pk[32];
    memcpy(wrong_pk, pk_expected, 32);
    wrong_pk[0] ^= 0x01;
    if (ed25519_verify(sig_expected, vectors[v].msg_len ? msg : NULL,
                       vectors[v].msg_len, wrong_pk) == 0) {
      printf("[tests] ed25519 vec %zu: accepted wrong pk\n", v);
      fails++;
    }
  }

  /* S non-canonical (S >= L) rejection. Take a valid signature and
   * replace S with L itself: should be rejected. */
  uint8_t seed0[32], pk0[32], sk0[64], sig0[64], msg0 = 0x72;
  decode_hex(
      "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
      seed0, 32);
  ed25519_create_keypair(pk0, sk0, seed0);
  ed25519_sign(sig0, &msg0, 1u, pk0, sk0);
  uint8_t sig_bad_S[64];
  memcpy(sig_bad_S, sig0, 32);
  /* L bytes LE */
  const uint8_t L_le[32] = {
      0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
      0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
  };
  memcpy(sig_bad_S + 32, L_le, 32);
  if (ed25519_verify(sig_bad_S, &msg0, 1u, pk0) == 0) {
    printf("[tests] ed25519: accepted non-canonical S == L\n");
    fails++;
  }
  /* S > L (set top byte higher). */
  memcpy(sig_bad_S + 32, L_le, 32);
  sig_bad_S[63] = 0x20;
  if (ed25519_verify(sig_bad_S, &msg0, 1u, pk0) == 0) {
    printf("[tests] ed25519: accepted non-canonical S > L\n");
    fails++;
  }

  /* Fail-closed em NULL inputs. */
  if (ed25519_verify(NULL, &msg0, 1u, pk0) == 0) {
    printf("[tests] ed25519_verify accepted NULL signature\n");
    fails++;
  }
  if (ed25519_verify(sig0, &msg0, 1u, NULL) == 0) {
    printf("[tests] ed25519_verify accepted NULL public key\n");
    fails++;
  }

  /* ed25519_create_keypair/sign tolerates NULL outputs without crash. */
  ed25519_sign(NULL, &msg0, 1u, pk0, sk0);
  ed25519_create_keypair(NULL, NULL, seed0);

  /* Round-trip: chave fresca de seed arbitrario assina + verifica
   * mensagem de 64 bytes. */
  uint8_t seed_rt[32];
  for (int i = 0; i < 32; ++i) {
    seed_rt[i] = (uint8_t)(0xA0 + i);
  }
  uint8_t pk_rt[32], sk_rt[64], sig_rt[64];
  ed25519_create_keypair(pk_rt, sk_rt, seed_rt);
  uint8_t msg_rt[64];
  for (int i = 0; i < 64; ++i) {
    msg_rt[i] = (uint8_t)i;
  }
  ed25519_sign(sig_rt, msg_rt, 64u, pk_rt, sk_rt);
  if (ed25519_verify(sig_rt, msg_rt, 64u, pk_rt) != 0) {
    printf("[tests] ed25519 round-trip verify failed\n");
    fails++;
  }
  /* Tamper message: verify must reject. */
  msg_rt[10] ^= 0x01;
  if (ed25519_verify(sig_rt, msg_rt, 64u, pk_rt) == 0) {
    printf("[tests] ed25519 round-trip accepted tampered message\n");
    fails++;
  }
  msg_rt[10] ^= 0x01;
  /* Determinismo: re-assinar produz mesma sig. */
  uint8_t sig_rt2[64];
  ed25519_sign(sig_rt2, msg_rt, 64u, pk_rt, sk_rt);
  if (memcmp(sig_rt, sig_rt2, 64) != 0) {
    printf("[tests] ed25519 sign not deterministic\n");
    fails++;
  }

  return fails;
}

static int test_hkdf_sha256_vectors(void) {
  /* RFC 5869 Appendix A: HKDF-SHA256 test vectors. The three cases
   * exercise (1) the typical small-input path, (2) long inputs/outputs
   * that span multiple expand iterations, and (3) the salt-empty
   * info-empty path that must substitute HashLen zero octets for the
   * salt per §2.2. Drift in either extract or expand will fail at
   * least one of these vectors. */
  int fails = 0;
  uint8_t prk[SHA256_DIGEST_SIZE];
  uint8_t okm[128];

  /* Test Case 1: SHA-256, basic test case. */
  static const uint8_t ikm1[22] = {
      0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
      0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b};
  static const uint8_t salt1[13] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                                    0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c};
  static const uint8_t info1[10] = {0xf0, 0xf1, 0xf2, 0xf3, 0xf4,
                                    0xf5, 0xf6, 0xf7, 0xf8, 0xf9};
  static const char *k_prk1 =
      "077709362c2e32df0ddc3f0dc47bba63"
      "90b6c73bb50f9c3122ec844ad7c2b3e5";
  static const char *k_okm1 =
      "3cb25f25faacd57a90434f64d0362f2a"
      "2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
      "34007208d5b887185865";

  if (crypt_hkdf_sha256_extract(salt1, sizeof(salt1), ikm1, sizeof(ikm1),
                                prk) != 0) {
    printf("[tests] hkdf TC1 extract returned non-zero\n");
    fails++;
  }
  fails += expect_hex("hkdf-tc1-prk", prk, SHA256_DIGEST_SIZE, k_prk1);
  if (crypt_hkdf_sha256_expand(prk, SHA256_DIGEST_SIZE, info1, sizeof(info1),
                               okm, 42u) != 0) {
    printf("[tests] hkdf TC1 expand returned non-zero\n");
    fails++;
  }
  fails += expect_hex("hkdf-tc1-okm", okm, 42u, k_okm1);
  if (crypt_hkdf_sha256(salt1, sizeof(salt1), ikm1, sizeof(ikm1), info1,
                        sizeof(info1), okm, 42u) != 0) {
    printf("[tests] hkdf TC1 wrapper returned non-zero\n");
    fails++;
  }
  fails += expect_hex("hkdf-tc1-wrapper", okm, 42u, k_okm1);

  /* Test Case 2: SHA-256, longer inputs/outputs (L = 82 spans three
   * expand iterations). */
  static uint8_t ikm2[80];
  static uint8_t salt2[80];
  static uint8_t info2[80];
  for (size_t i = 0; i < 80u; ++i) {
    ikm2[i] = (uint8_t)i;
    salt2[i] = (uint8_t)(0x60u + i);
    info2[i] = (uint8_t)(0xb0u + i);
  }
  static const char *k_prk2 =
      "06a6b88c5853361a06104c9ceb35b45c"
      "ef760014904671014a193f40c15fc244";
  static const char *k_okm2 =
      "b11e398dc80327a1c8e7f78c596a4934"
      "4f012eda2d4efad8a050cc4c19afa97c"
      "59045a99cac7827271cb41c65e590e09"
      "da3275600c2f09b8367793a9aca3db71"
      "cc30c58179ec3e87c14c01d5c1f3434f"
      "1d87";

  if (crypt_hkdf_sha256_extract(salt2, sizeof(salt2), ikm2, sizeof(ikm2),
                                prk) != 0) {
    printf("[tests] hkdf TC2 extract returned non-zero\n");
    fails++;
  }
  fails += expect_hex("hkdf-tc2-prk", prk, SHA256_DIGEST_SIZE, k_prk2);
  if (crypt_hkdf_sha256_expand(prk, SHA256_DIGEST_SIZE, info2, sizeof(info2),
                               okm, 82u) != 0) {
    printf("[tests] hkdf TC2 expand returned non-zero\n");
    fails++;
  }
  fails += expect_hex("hkdf-tc2-okm", okm, 82u, k_okm2);

  /* Test Case 3: SHA-256, salt empty, info empty (covers the
   * substitution rule from RFC 5869 §2.2 — salt becomes HashLen
   * zero octets). */
  static const char *k_prk3 =
      "19ef24a32c717b167f33a91d6f648bdf"
      "96596776afdb6377ac434c1c293ccb04";
  static const char *k_okm3 =
      "8da4e775a563c18f715f802a063c5a31"
      "b8a11f5c5ee1879ec3454e5f3c738d2d"
      "9d201395faa4b61a96c8";

  if (crypt_hkdf_sha256_extract(NULL, 0u, ikm1, sizeof(ikm1), prk) != 0) {
    printf("[tests] hkdf TC3 extract (NULL salt) returned non-zero\n");
    fails++;
  }
  fails += expect_hex("hkdf-tc3-prk-nullsalt", prk, SHA256_DIGEST_SIZE, k_prk3);
  /* Zero-length salt with non-NULL pointer must also map to the same
   * zero-octet HashLen substitution. */
  static const uint8_t empty_salt[1] = {0u};
  if (crypt_hkdf_sha256_extract(empty_salt, 0u, ikm1, sizeof(ikm1), prk) != 0) {
    printf("[tests] hkdf TC3 extract (zero-len salt) returned non-zero\n");
    fails++;
  }
  fails += expect_hex("hkdf-tc3-prk-emptysalt", prk, SHA256_DIGEST_SIZE,
                      k_prk3);
  if (crypt_hkdf_sha256_expand(prk, SHA256_DIGEST_SIZE, NULL, 0u, okm, 42u) !=
      0) {
    printf("[tests] hkdf TC3 expand (NULL info) returned non-zero\n");
    fails++;
  }
  fails += expect_hex("hkdf-tc3-okm", okm, 42u, k_okm3);

  /* Contract: fail-closed on invalid inputs. */
  if (crypt_hkdf_sha256_extract(NULL, 0u, NULL, 0u, NULL) != -1) {
    printf("[tests] hkdf extract did not reject NULL prk output\n");
    fails++;
  }
  if (crypt_hkdf_sha256_expand(NULL, SHA256_DIGEST_SIZE, NULL, 0u, okm, 1u) !=
      -1) {
    printf("[tests] hkdf expand did not reject NULL prk\n");
    fails++;
  }
  if (crypt_hkdf_sha256_expand(prk, SHA256_DIGEST_SIZE, NULL, 0u, NULL, 1u) !=
      -1) {
    printf("[tests] hkdf expand did not reject NULL out\n");
    fails++;
  }
  if (crypt_hkdf_sha256_expand(prk, SHA256_DIGEST_SIZE, NULL, 0u, okm,
                               255u * SHA256_DIGEST_SIZE + 1u) != -1) {
    printf("[tests] hkdf expand did not reject L > 255 * HashLen\n");
    fails++;
  }
  if (crypt_hkdf_sha256_expand(prk, SHA256_DIGEST_SIZE - 1u, NULL, 0u, okm,
                               32u) != -1) {
    printf("[tests] hkdf expand did not reject short prk\n");
    fails++;
  }
  /* L = 0 is a no-op success per the implementation contract. */
  if (crypt_hkdf_sha256_expand(prk, SHA256_DIGEST_SIZE, NULL, 0u, okm, 0u) !=
      0) {
    printf("[tests] hkdf expand did not accept L = 0 as no-op\n");
    fails++;
  }

  return fails;
}

static int test_chacha20_block_vectors(void) {
  /* RFC 8439 Appendix A.1: ChaCha20 block function test vectors.
   * Os dois vetores #1 (counter=0) e #2 (counter=1) compartilham
   * key e nonce zero — validam que o counter incrementa o keystream
   * conforme RFC. */
  int fails = 0;
  uint8_t key[CHACHA20_KEY_SIZE];
  uint8_t nonce[CHACHA20_NONCE_SIZE];
  uint8_t out[CHACHA20_BLOCK_SIZE];

  /* Test Vector #1: key=0, nonce=0, counter=0. */
  memset(key, 0, sizeof(key));
  memset(nonce, 0, sizeof(nonce));
  static const char *const k_tc1 =
      "76b8e0ada0f13d90405d6ae55386bd28"
      "bdd219b8a08ded1aa836efcc8b770dc7"
      "da41597c5157488d7724e03fb8d84a37"
      "6a43b8f41518a11cc387b669b2ee6586";
  if (chacha20_block(key, 0u, nonce, out) != 0) {
    printf("[tests] chacha20 block TC1 returned non-zero\n");
    fails++;
  }
  fails += expect_hex("chacha20-tc1-block", out, sizeof(out), k_tc1);

  /* Test Vector #2: key=0, nonce=0, counter=1. RFC 8439 §A.1.2. */
  static const char *const k_tc2 =
      "9f07e7be5551387a98ba977c732d080d"
      "cb0f29a048e3656912c6533e32ee7aed"
      "29b721769ce64e43d57133b074d839d5"
      "31ed1f28510afb45ace10a1f4b794d6f";
  if (chacha20_block(key, 1u, nonce, out) != 0) {
    printf("[tests] chacha20 block TC2 returned non-zero\n");
    fails++;
  }
  fails += expect_hex("chacha20-tc2-block", out, sizeof(out), k_tc2);
  /* Fail-closed: NULL inputs. */
  if (chacha20_block(NULL, 0u, nonce, out) != -1) {
    printf("[tests] chacha20 block did not reject NULL key\n");
    fails++;
  }
  if (chacha20_block(key, 0u, NULL, out) != -1) {
    printf("[tests] chacha20 block did not reject NULL nonce\n");
    fails++;
  }
  if (chacha20_block(key, 0u, nonce, NULL) != -1) {
    printf("[tests] chacha20 block did not reject NULL out\n");
    fails++;
  }
  return fails;
}

static int test_chacha20_encrypt_round_trip(void) {
  /* Round-trip: encrypt(plaintext) entao encrypt(ciphertext) (com
   * mesmas key/counter/nonce) deve recuperar o plaintext, porque
   * ChaCha20 e um stream cipher XOR-based. */
  int fails = 0;
  uint8_t key[CHACHA20_KEY_SIZE] = {0};
  uint8_t nonce[CHACHA20_NONCE_SIZE] = {0};
  for (size_t i = 0u; i < sizeof(key); ++i) {
    key[i] = (uint8_t)(0xAAu ^ (i * 17u));
  }
  for (size_t i = 0u; i < sizeof(nonce); ++i) {
    nonce[i] = (uint8_t)(0x55u ^ (i * 31u));
  }
  static const uint8_t k_plain[200] = {
      0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE,
      0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0,
      0x13, 0x37, 0xC0, 0xDE, 0xFE, 0xED, 0xFA, 0xCE};
  uint8_t ct[200];
  uint8_t pt2[200];
  if (chacha20_encrypt(key, 7u, nonce, k_plain, ct, sizeof(k_plain)) != 0) {
    printf("[tests] chacha20 encrypt returned non-zero\n");
    fails++;
  }
  /* Ciphertext nao pode ser identico ao plaintext (zero seria caso
   * degenerado de all-zero keystream). */
  if (memcmp(k_plain, ct, sizeof(k_plain)) == 0) {
    printf("[tests] chacha20 encrypt did not change plaintext\n");
    fails++;
  }
  if (chacha20_encrypt(key, 7u, nonce, ct, pt2, sizeof(k_plain)) != 0) {
    printf("[tests] chacha20 decrypt (re-encrypt) returned non-zero\n");
    fails++;
  }
  if (memcmp(k_plain, pt2, sizeof(k_plain)) != 0) {
    printf("[tests] chacha20 round-trip did not recover plaintext\n");
    fails++;
  }

  /* In-place: encrypt(in == out). */
  uint8_t inplace[200];
  memcpy(inplace, k_plain, sizeof(k_plain));
  if (chacha20_encrypt(key, 7u, nonce, inplace, inplace, sizeof(inplace)) !=
      0) {
    printf("[tests] chacha20 in-place encrypt returned non-zero\n");
    fails++;
  }
  if (memcmp(inplace, ct, sizeof(inplace)) != 0) {
    printf("[tests] chacha20 in-place encrypt diverged from out-of-place\n");
    fails++;
  }

  /* Counter overflow: initial_counter perto de 2^32 com len que estoura. */
  uint8_t small[1] = {0xAB};
  uint8_t small_out[1];
  if (chacha20_encrypt(key, 0xFFFFFFFFu, nonce, small, small_out, 65u) != -1) {
    /* 65 bytes = 2 blocos a partir do counter 2^32-1 = blocos
     * 2^32-1 e 2^32 (overflow). Deve falhar. Nota: usar small[1]
     * com len=65 e desalinhado de proposito — a funcao retorna -1
     * antes de tocar o buffer. */
    printf("[tests] chacha20 did not reject counter overflow\n");
    fails++;
  }

  /* Len = 0 e sucesso vacuo. */
  if (chacha20_encrypt(key, 0u, nonce, NULL, NULL, 0u) != 0) {
    printf("[tests] chacha20 did not accept len=0 as no-op\n");
    fails++;
  }
  /* Len = 0 com key NULL e fail-closed. */
  if (chacha20_encrypt(NULL, 0u, nonce, NULL, NULL, 0u) != -1) {
    printf("[tests] chacha20 did not reject NULL key with len=0\n");
    fails++;
  }
  return fails;
}

static int test_poly1305_vectors(void) {
  /* RFC 8439 Appendix A.3 Test Vector #1: key=0, msg=0 (64 bytes) ->
   * tag=0 (16 bytes). Resultado degenerado mas valido: r=0 zera o
   * polinomio, s=0 nao adiciona deslocamento. */
  int fails = 0;
  uint8_t key[POLY1305_KEY_SIZE] = {0};
  uint8_t msg[64] = {0};
  uint8_t tag[POLY1305_TAG_SIZE];
  if (poly1305_mac(key, msg, sizeof(msg), tag) != 0) {
    printf("[tests] poly1305 TC1 returned non-zero\n");
    fails++;
  }
  uint8_t zero_tag[POLY1305_TAG_SIZE] = {0};
  if (memcmp(tag, zero_tag, sizeof(tag)) != 0) {
    printf("[tests] poly1305 TC1 tag not all-zero\n");
    fails++;
  }

  /* TC2: key=0 (low 16 zeros) || s (high 16 bytes nao-zero), msg=0.
   * Tag deve ser exatamente s (porque r=0 anula o polinomio,
   * accumulator=0, tag = 0 + s = s). */
  for (size_t i = 0u; i < 16u; ++i) {
    key[i] = 0u;
  }
  for (size_t i = 16u; i < 32u; ++i) {
    key[i] = (uint8_t)(0x10u + i);
  }
  if (poly1305_mac(key, msg, sizeof(msg), tag) != 0) {
    printf("[tests] poly1305 TC2 returned non-zero\n");
    fails++;
  }
  if (memcmp(tag, key + 16, 16u) != 0) {
    printf("[tests] poly1305 TC2 tag != s (high 16 bytes of key)\n");
    fails++;
  }

  /* TC3: tag varia com a mensagem. Mudar 1 byte do msg deve dar tag
   * completamente diferente (avalanche da MAC). */
  uint8_t msg2[64];
  memcpy(msg2, msg, sizeof(msg));
  /* Now use a real Poly1305 key with non-zero r (apos clamping). */
  for (size_t i = 0u; i < 32u; ++i) {
    key[i] = (uint8_t)(0x80u + i);
  }
  uint8_t tag_a[POLY1305_TAG_SIZE];
  uint8_t tag_b[POLY1305_TAG_SIZE];
  if (poly1305_mac(key, msg, sizeof(msg), tag_a) != 0 ||
      poly1305_mac(key, msg2, sizeof(msg2), tag_b) != 0) {
    printf("[tests] poly1305 TC3 returned non-zero\n");
    fails++;
  }
  if (memcmp(tag_a, tag_b, sizeof(tag_a)) != 0) {
    printf("[tests] poly1305 TC3 identical msgs produced different tags\n");
    fails++;
  }
  msg2[0] ^= 0x01u;
  if (poly1305_mac(key, msg2, sizeof(msg2), tag_b) != 0) {
    printf("[tests] poly1305 TC3 second mac returned non-zero\n");
    fails++;
  }
  if (memcmp(tag_a, tag_b, sizeof(tag_a)) == 0) {
    printf("[tests] poly1305 TC3 different msgs produced identical tag\n");
    fails++;
  }

  /* Fail-closed. */
  if (poly1305_mac(NULL, msg, sizeof(msg), tag) != -1) {
    printf("[tests] poly1305 did not reject NULL key\n");
    fails++;
  }
  if (poly1305_mac(key, msg, sizeof(msg), NULL) != -1) {
    printf("[tests] poly1305 did not reject NULL tag\n");
    fails++;
  }
  if (poly1305_mac(key, NULL, 16u, tag) != -1) {
    printf("[tests] poly1305 did not reject NULL msg with non-zero len\n");
    fails++;
  }
  /* Empty msg is valid. */
  if (poly1305_mac(key, NULL, 0u, tag) != 0) {
    printf("[tests] poly1305 did not accept empty msg\n");
    fails++;
  }
  return fails;
}

static int test_chacha20_poly1305_aead(void) {
  /* AEAD round-trip + tampering rejection + fail-closed. */
  int fails = 0;
  uint8_t key[CHACHA20_KEY_SIZE];
  uint8_t nonce[CHACHA20_NONCE_SIZE];
  for (size_t i = 0u; i < sizeof(key); ++i) {
    key[i] = (uint8_t)(0x10u + i);
  }
  for (size_t i = 0u; i < sizeof(nonce); ++i) {
    nonce[i] = (uint8_t)(0xA0u + i);
  }
  static const uint8_t k_aad[20] = {
      0x50, 0x51, 0x52, 0x53, 0xC0, 0xC1, 0xC2, 0xC3,
      0xC4, 0xC5, 0xC6, 0xC7, 0x55, 0x66, 0x77, 0x88,
      0x99, 0xAA, 0xBB, 0xCC};
  static const char k_plain[] =
      "Ladies and Gentlemen of the class of '99: If I could offer you "
      "only one tip for the future, sunscreen would be it.";
  uint8_t ct[sizeof(k_plain) - 1u];
  uint8_t tag[CHACHA20_POLY1305_TAG_SIZE];

  /* Encrypt. */
  if (chacha20_poly1305_encrypt(key, nonce, k_aad, sizeof(k_aad),
                                (const uint8_t *)k_plain,
                                sizeof(k_plain) - 1u, ct, tag) != 0) {
    printf("[tests] aead encrypt returned non-zero\n");
    fails++;
  }
  if (memcmp(k_plain, ct, sizeof(k_plain) - 1u) == 0) {
    printf("[tests] aead encrypt did not change plaintext\n");
    fails++;
  }

  /* Decrypt — valid tag. */
  uint8_t pt[sizeof(k_plain) - 1u];
  if (chacha20_poly1305_decrypt(key, nonce, k_aad, sizeof(k_aad), ct,
                                sizeof(ct), tag, pt) != 0) {
    printf("[tests] aead decrypt rejected valid tag\n");
    fails++;
  }
  if (memcmp(k_plain, pt, sizeof(k_plain) - 1u) != 0) {
    printf("[tests] aead decrypt did not recover plaintext\n");
    fails++;
  }

  /* Tampering: flip 1 byte do ciphertext deve rejeitar. */
  uint8_t ct_bad[sizeof(k_plain) - 1u];
  memcpy(ct_bad, ct, sizeof(ct));
  ct_bad[50] ^= 0x01u;
  uint8_t pt_bad[sizeof(k_plain) - 1u];
  if (chacha20_poly1305_decrypt(key, nonce, k_aad, sizeof(k_aad), ct_bad,
                                sizeof(ct_bad), tag, pt_bad) != -1) {
    printf("[tests] aead decrypt accepted tampered ciphertext\n");
    fails++;
  }

  /* Tampering: flip 1 byte do AAD deve rejeitar. */
  uint8_t aad_bad[20];
  memcpy(aad_bad, k_aad, sizeof(aad_bad));
  aad_bad[3] ^= 0x80u;
  if (chacha20_poly1305_decrypt(key, nonce, aad_bad, sizeof(aad_bad), ct,
                                sizeof(ct), tag, pt_bad) != -1) {
    printf("[tests] aead decrypt accepted tampered AAD\n");
    fails++;
  }

  /* Tampering: flip 1 byte do tag deve rejeitar. */
  uint8_t tag_bad[CHACHA20_POLY1305_TAG_SIZE];
  memcpy(tag_bad, tag, sizeof(tag_bad));
  tag_bad[7] ^= 0x10u;
  if (chacha20_poly1305_decrypt(key, nonce, k_aad, sizeof(k_aad), ct,
                                sizeof(ct), tag_bad, pt_bad) != -1) {
    printf("[tests] aead decrypt accepted tampered tag\n");
    fails++;
  }

  /* Wrong key rejects. */
  uint8_t key_bad[CHACHA20_KEY_SIZE];
  memcpy(key_bad, key, sizeof(key_bad));
  key_bad[0] ^= 0x01u;
  if (chacha20_poly1305_decrypt(key_bad, nonce, k_aad, sizeof(k_aad), ct,
                                sizeof(ct), tag, pt_bad) != -1) {
    printf("[tests] aead decrypt accepted wrong key\n");
    fails++;
  }

  /* Wrong nonce rejects. */
  uint8_t nonce_bad[CHACHA20_NONCE_SIZE];
  memcpy(nonce_bad, nonce, sizeof(nonce_bad));
  nonce_bad[5] ^= 0x01u;
  if (chacha20_poly1305_decrypt(key, nonce_bad, k_aad, sizeof(k_aad), ct,
                                sizeof(ct), tag, pt_bad) != -1) {
    printf("[tests] aead decrypt accepted wrong nonce\n");
    fails++;
  }

  /* Empty plaintext (AEAD over AAD only) is supported. */
  uint8_t empty_tag[CHACHA20_POLY1305_TAG_SIZE];
  if (chacha20_poly1305_encrypt(key, nonce, k_aad, sizeof(k_aad), NULL, 0u,
                                NULL, empty_tag) != 0) {
    printf("[tests] aead encrypt rejected empty plaintext\n");
    fails++;
  }
  if (chacha20_poly1305_decrypt(key, nonce, k_aad, sizeof(k_aad), NULL, 0u,
                                empty_tag, NULL) != 0) {
    printf("[tests] aead decrypt rejected empty plaintext round-trip\n");
    fails++;
  }

  /* Empty AAD is supported. */
  uint8_t no_aad_tag[CHACHA20_POLY1305_TAG_SIZE];
  if (chacha20_poly1305_encrypt(key, nonce, NULL, 0u,
                                (const uint8_t *)k_plain,
                                sizeof(k_plain) - 1u, ct, no_aad_tag) != 0) {
    printf("[tests] aead encrypt rejected NULL AAD with aad_len=0\n");
    fails++;
  }
  /* No-AAD tag deve diferir do tag com AAD (mesma key/nonce/pt). */
  if (memcmp(tag, no_aad_tag, sizeof(tag)) == 0) {
    printf("[tests] aead tag identical with and without AAD\n");
    fails++;
  }

  /* Fail-closed: NULL key. */
  if (chacha20_poly1305_encrypt(NULL, nonce, k_aad, sizeof(k_aad),
                                (const uint8_t *)k_plain,
                                sizeof(k_plain) - 1u, ct, tag) != -1) {
    printf("[tests] aead encrypt did not reject NULL key\n");
    fails++;
  }
  if (chacha20_poly1305_decrypt(NULL, nonce, k_aad, sizeof(k_aad), ct,
                                sizeof(ct), tag, pt) != -1) {
    printf("[tests] aead decrypt did not reject NULL key\n");
    fails++;
  }

  return fails;
}

static int test_x25519_rfc7748_scalarmult(void) {
  /* RFC 7748 §5.2 test vectors para iteracoes 1 e ... (apenas a
   * primeira iteracao aqui, sem milhao de iteracoes que seria
   * caro). */
  int fails = 0;

  static const char *const k_scalar_hex =
      "a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4";
  static const char *const k_u_hex =
      "e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c";
  static const char *const k_expected_hex =
      "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552";

  uint8_t scalar[32];
  uint8_t u[32];
  uint8_t expected[32];
  if (decode_hex(k_scalar_hex, scalar, 32u) != 0 ||
      decode_hex(k_u_hex, u, 32u) != 0 ||
      decode_hex(k_expected_hex, expected, 32u) != 0) {
    printf("[tests] x25519 RFC 7748 §5.2 hex decode failed\n");
    return 1;
  }
  uint8_t out[32];
  if (x25519(scalar, u, out) != 0) {
    printf("[tests] x25519 RFC 7748 §5.2 returned non-zero\n");
    fails++;
  }
  if (memcmp(out, expected, 32) != 0) {
    printf("[tests] x25519 RFC 7748 §5.2 mismatch\n");
    fails++;
  }

  /* Segundo vetor: scalar = 4b66e9d4d1b4673c5ad22691957d6af5c11b6421e0ea01d42ca4169e7918ba0d,
   * u = e5210f12786811d3f4b7959d0538ae2c31dbe7106fc03c3efc4cd549c715a493,
   * expected = 95cbde9476e8907d7aade45cb4b873f88b595a68799fa152e6f8f7647aac7957. */
  static const char *const k_scalar2_hex =
      "4b66e9d4d1b4673c5ad22691957d6af5c11b6421e0ea01d42ca4169e7918ba0d";
  static const char *const k_u2_hex =
      "e5210f12786811d3f4b7959d0538ae2c31dbe7106fc03c3efc4cd549c715a493";
  static const char *const k_expected2_hex =
      "95cbde9476e8907d7aade45cb4b873f88b595a68799fa152e6f8f7647aac7957";
  if (decode_hex(k_scalar2_hex, scalar, 32u) != 0 ||
      decode_hex(k_u2_hex, u, 32u) != 0 ||
      decode_hex(k_expected2_hex, expected, 32u) != 0) {
    printf("[tests] x25519 RFC 7748 §5.2 (2) hex decode failed\n");
    return fails + 1;
  }
  if (x25519(scalar, u, out) != 0) {
    printf("[tests] x25519 RFC 7748 §5.2 (2) returned non-zero\n");
    fails++;
  }
  if (memcmp(out, expected, 32) != 0) {
    printf("[tests] x25519 RFC 7748 §5.2 (2) mismatch\n");
    fails++;
  }

  return fails;
}

static int test_x25519_rfc7748_dh(void) {
  /* RFC 7748 §6.1 ECDH test vector. */
  int fails = 0;

  static const char *const k_alice_sk_hex =
      "77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a";
  static const char *const k_alice_pk_hex =
      "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a";
  static const char *const k_bob_sk_hex =
      "5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb";
  static const char *const k_bob_pk_hex =
      "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f";
  static const char *const k_shared_hex =
      "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742";

  uint8_t alice_sk[32], alice_pk_expected[32];
  uint8_t bob_sk[32], bob_pk_expected[32];
  uint8_t shared_expected[32];
  if (decode_hex(k_alice_sk_hex, alice_sk, 32u) != 0 ||
      decode_hex(k_alice_pk_hex, alice_pk_expected, 32u) != 0 ||
      decode_hex(k_bob_sk_hex, bob_sk, 32u) != 0 ||
      decode_hex(k_bob_pk_hex, bob_pk_expected, 32u) != 0 ||
      decode_hex(k_shared_hex, shared_expected, 32u) != 0) {
    printf("[tests] x25519 RFC 7748 §6.1 hex decode failed\n");
    return 1;
  }

  /* Alice computa sua public key. */
  uint8_t alice_pk[32];
  if (x25519_base(alice_sk, alice_pk) != 0) {
    printf("[tests] x25519_base(alice_sk) returned non-zero\n");
    fails++;
  }
  if (memcmp(alice_pk, alice_pk_expected, 32) != 0) {
    printf("[tests] x25519 alice public key mismatch\n");
    fails++;
  }

  /* Bob computa sua public key. */
  uint8_t bob_pk[32];
  if (x25519_base(bob_sk, bob_pk) != 0) {
    printf("[tests] x25519_base(bob_sk) returned non-zero\n");
    fails++;
  }
  if (memcmp(bob_pk, bob_pk_expected, 32) != 0) {
    printf("[tests] x25519 bob public key mismatch\n");
    fails++;
  }

  /* Alice deriva shared(alice_sk, bob_pk). */
  uint8_t shared_alice[32];
  if (x25519(alice_sk, bob_pk, shared_alice) != 0) {
    printf("[tests] x25519(alice_sk, bob_pk) returned non-zero\n");
    fails++;
  }
  if (memcmp(shared_alice, shared_expected, 32) != 0) {
    printf("[tests] x25519 alice shared key mismatch\n");
    fails++;
  }

  /* Bob deriva shared(bob_sk, alice_pk). Deve bater. */
  uint8_t shared_bob[32];
  if (x25519(bob_sk, alice_pk, shared_bob) != 0) {
    printf("[tests] x25519(bob_sk, alice_pk) returned non-zero\n");
    fails++;
  }
  if (memcmp(shared_bob, shared_expected, 32) != 0) {
    printf("[tests] x25519 bob shared key mismatch\n");
    fails++;
  }
  if (memcmp(shared_alice, shared_bob, 32) != 0) {
    printf("[tests] x25519 ECDH did not converge\n");
    fails++;
  }

  return fails;
}

static int test_x25519_small_order_rejection(void) {
  /* Small-order point rejection (RFC 7748 §6.1, §7). O ponto u=0
   * tem ordem 4 (subgroup pequeno); ECDH com ele retorna shared=0
   * que esta implementacao rejeita fail-closed. */
  int fails = 0;
  uint8_t scalar[32];
  for (int i = 0; i < 32; ++i) {
    scalar[i] = (uint8_t)(0x42u + i);
  }
  static const uint8_t k_u_zero[32] = {0};
  uint8_t shared[32];
  int rc = x25519(scalar, k_u_zero, shared);
  if (rc != -1) {
    printf("[tests] x25519 did not reject u=0 (small order)\n");
    fails++;
  }
  /* Outros small-order points conhecidos. RFC 7748 lista alguns:
   *   u=1 (order 4) — produz shared=0 apos clamping no scalar.
   *   2^255-19 mod p = 0 (mesma classe que u=0).
   *   Pontos com ordem 2 ou 4. */
  static const uint8_t k_u_one[32] = {
      1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  };
  rc = x25519(scalar, k_u_one, shared);
  if (rc != -1) {
    printf("[tests] x25519 did not reject u=1 (small order)\n");
    fails++;
  }

  return fails;
}

static int test_x25519_fail_closed(void) {
  int fails = 0;
  uint8_t scalar[32] = {0};
  uint8_t u[32] = {1};
  uint8_t out[32];
  if (x25519(NULL, u, out) != -1) {
    printf("[tests] x25519 did not reject NULL scalar\n");
    fails++;
  }
  if (x25519(scalar, NULL, out) != -1) {
    printf("[tests] x25519 did not reject NULL u_coord\n");
    fails++;
  }
  if (x25519(scalar, u, NULL) != -1) {
    printf("[tests] x25519 did not reject NULL shared\n");
    fails++;
  }
  if (x25519_base(NULL, out) != -1) {
    printf("[tests] x25519_base did not reject NULL scalar\n");
    fails++;
  }
  if (x25519_base(scalar, NULL) != -1) {
    printf("[tests] x25519_base did not reject NULL public_key\n");
    fails++;
  }
  return fails;
}

static int test_x25519_high_bit_masked(void) {
  /* RFC 7748 §5: o bit 255 do u-coord deve ser mascarado pela
   * implementacao. Verificamos passando dois inputs identicos
   * exceto pelo bit 255 e esperando mesmo output. */
  int fails = 0;
  uint8_t scalar[32];
  for (int i = 0; i < 32; ++i) {
    scalar[i] = (uint8_t)(0x55u + i);
  }
  uint8_t u_a[32], u_b[32];
  for (int i = 0; i < 32; ++i) {
    u_a[i] = (uint8_t)(0xA0u + i);
    u_b[i] = u_a[i];
  }
  u_b[31] ^= 0x80u; /* Flip do bit 255. */
  uint8_t out_a[32], out_b[32];
  if (x25519(scalar, u_a, out_a) != 0) {
    printf("[tests] x25519 (high-bit a) returned non-zero\n");
    fails++;
  }
  if (x25519(scalar, u_b, out_b) != 0) {
    printf("[tests] x25519 (high-bit b) returned non-zero\n");
    fails++;
  }
  if (memcmp(out_a, out_b, 32) != 0) {
    printf("[tests] x25519 did not mask bit 255 of u_coord\n");
    fails++;
  }
  return fails;
}

static int test_x25519_scalar_clamping(void) {
  /* Scalar clamping per RFC 7748 §5: a implementacao deve mascarar
   * os bits 0,1,2,255 e setar bit 254 do scalar antes de usa-lo.
   * Validacao: dois scalars que diferem apenas nos bits que devem
   * ser zerados/setados pelo clamping produzem o MESMO output. */
  int fails = 0;
  uint8_t s_a[32], s_b[32];
  for (int i = 0; i < 32; ++i) {
    s_a[i] = (uint8_t)(0x12u + i);
    s_b[i] = s_a[i];
  }
  /* Flip dos bits que devem ser mascarados:
   *   byte 0: bits 0,1,2 (mask 248 = ~7)
   *   byte 31: bit 7 (mask 127 = ~128)
   * E setar o bit 6 do byte 31 (que o clamping força a 1). */
  s_b[0] ^= 0x07u;
  s_b[31] ^= 0x80u;
  /* O bit 6 do byte 31 nao podemos flipar livremente; o clamping
   * SETA-o. Se ambos s_a[31] e s_b[31] ja tem bit 6 setado, OK; se
   * nao, garantimos que o clamping faz a igualdade. */
  uint8_t out_a[32], out_b[32];
  if (x25519_base(s_a, out_a) != 0) {
    printf("[tests] x25519_base(s_a) returned non-zero\n");
    fails++;
  }
  if (x25519_base(s_b, out_b) != 0) {
    printf("[tests] x25519_base(s_b) returned non-zero\n");
    fails++;
  }
  if (memcmp(out_a, out_b, 32) != 0) {
    printf("[tests] x25519 did not clamp scalar correctly\n");
    fails++;
  }
  return fails;
}

/* ============================================================
 * BLAKE2b (RFC 7693) and Argon2id (RFC 9106) tests — alpha.218.
 *
 * BLAKE2b: validacao com o vetor canonico de RFC 7693 Appendix A
 *   (input "abc") + cobertura de empty input, multi-block input,
 *   parametros invalidos.
 *
 * Argon2id: validacao com vetor RFC 9106 Appendix A (Argon2id v1.3,
 *   p=1 path simplificado: salt/password/m/t bem definidos, sem
 *   secret/AD) + cobertura de determinismo, sensibilidade a
 *   password/salt/t_cost/m_cost, e fail-closed.
 *
 * Nota: O vetor canonico RFC 9106 §A.3 usa p=4 com secret + AD; nao
 * compatibilidade direta com esta implementacao (p=1, sem secret/AD).
 * O smoke test cobre as propriedades estruturais do KDF.
 * ============================================================ */

static int test_blake2b_rfc7693_abc(void) {
  /* RFC 7693 Appendix A: BLAKE2b("abc") = 64-byte digest below. */
  static const char *k_expected_abc =
      "ba80a53f981c4d0d6a2797b69f12f6e9"
      "4c212f14685ac4b74b12bb6fdbffa2d1"
      "7d87c5392aab792dc252d5de4533cc95"
      "18d38aa8dbf1925ab92386edd4009923";
  uint8_t digest[64];
  int rc = blake2b(digest, 64u, NULL, 0u, (const uint8_t *)"abc", 3u);
  if (rc != 0) {
    printf("[tests] blake2b(abc) returned non-zero\n");
    return 1;
  }
  return expect_hex("blake2b(abc)", digest, 64, k_expected_abc);
}

static int test_blake2b_empty(void) {
  /*
   * BLAKE2b("") (empty input, no key, 64-byte output).
   * Valor verificado contra Python hashlib.blake2b(b"").digest().
   */
  static const char *k_expected_empty =
      "786a02f742015903c6c6fd852552d272"
      "912f4740e15847618a86e217f71f5419"
      "d25e1031afee585313896444934eb04b"
      "903a685b1448b755d56f701afe9be2ce";
  uint8_t digest[64];
  int rc = blake2b(digest, 64u, NULL, 0u, NULL, 0u);
  if (rc != 0) {
    printf("[tests] blake2b(empty) returned non-zero\n");
    return 1;
  }
  return expect_hex("blake2b(empty)", digest, 64, k_expected_empty);
}

static int test_blake2b_multiblock(void) {
  /*
   * BLAKE2b("The quick brown fox jumps over the lazy dog") — 43 bytes,
   * fits in one block, but exercises non-empty padding.
   * Verificado contra Python hashlib.blake2b().
   */
  static const char *k_expected =
      "a8add4bdddfd93e4877d2746e62817b1"
      "16364a1fa7bc148d95090bc7333b3673"
      "f82401cf7aa2e4cb1ecd90296e3f14cb"
      "5413f8ed77be73045b13914cdcd6a918";
  static const char *k_input =
      "The quick brown fox jumps over the lazy dog";
  uint8_t digest[64];
  int rc = blake2b(digest, 64u, NULL, 0u, (const uint8_t *)k_input, 43u);
  if (rc != 0) {
    printf("[tests] blake2b(fox) returned non-zero\n");
    return 1;
  }
  return expect_hex("blake2b(fox)", digest, 64, k_expected);
}

static int test_blake2b_streaming_equals_oneshot(void) {
  /*
   * Verifica que init+update+update+final produz o mesmo digest que
   * blake2b() one-shot, garantindo correto lazy-compression no buffer.
   */
  int fails = 0;
  uint8_t input[300];
  for (size_t i = 0; i < sizeof(input); ++i) {
    input[i] = (uint8_t)(i & 0xFFu);
  }
  uint8_t out_oneshot[64], out_streaming[64];
  if (blake2b(out_oneshot, 64u, NULL, 0u, input, sizeof(input)) != 0) {
    printf("[tests] blake2b oneshot failed\n");
    return 1;
  }
  struct blake2b_ctx ctx;
  if (blake2b_init(&ctx, 64u, NULL, 0u) != 0) {
    printf("[tests] blake2b_init failed\n");
    return 1;
  }
  /* Update em chunks ate cruzar varios block boundaries */
  blake2b_update(&ctx, input, 50u);
  blake2b_update(&ctx, input + 50u, 78u);   /* boundary 128 */
  blake2b_update(&ctx, input + 128u, 1u);
  blake2b_update(&ctx, input + 129u, 127u); /* boundary 256 */
  blake2b_update(&ctx, input + 256u, 44u);
  blake2b_final(&ctx, out_streaming);
  blake2b_wipe(&ctx);
  if (memcmp(out_oneshot, out_streaming, 64) != 0) {
    printf("[tests] blake2b streaming != oneshot\n");
    fails++;
  }
  return fails;
}

static int test_blake2b_variable_output(void) {
  /*
   * Verifica que outlen 32 / 16 / 1 produzem digest com o prefixo
   * esperado (prefix de outlen=64 nao matchea — outlen e parte do
   * param block, alterando h[0] inicial).
   *
   * Apenas garante que diferentes outlen produzem outputs distintos.
   */
  int fails = 0;
  uint8_t d64[64], d32[32], d16[16];
  if (blake2b(d64, 64u, NULL, 0u, (const uint8_t *)"abc", 3u) != 0 ||
      blake2b(d32, 32u, NULL, 0u, (const uint8_t *)"abc", 3u) != 0 ||
      blake2b(d16, 16u, NULL, 0u, (const uint8_t *)"abc", 3u) != 0) {
    printf("[tests] blake2b variable output failed\n");
    return 1;
  }
  /* Outlen 32 deve ser diferente do prefixo de outlen 64 (param block
   * difere) */
  if (memcmp(d64, d32, 16) == 0) {
    printf("[tests] blake2b outlen=32 nao difere de outlen=64\n");
    fails++;
  }
  if (memcmp(d32, d16, 16) == 0) {
    printf("[tests] blake2b outlen=16 nao difere de outlen=32\n");
    fails++;
  }
  return fails;
}

static int test_blake2b_keyed(void) {
  /*
   * BLAKE2b keyed mode (HMAC-like). Verifica que adicionar key altera
   * o digest, e que key+empty != key2+empty.
   */
  int fails = 0;
  uint8_t key1[16], key2[16], d_nokey[64], d_key1[64], d_key2[64];
  for (int i = 0; i < 16; ++i) {
    key1[i] = (uint8_t)i;
    key2[i] = (uint8_t)(0xFF - i);
  }
  if (blake2b(d_nokey, 64u, NULL, 0u, NULL, 0u) != 0 ||
      blake2b(d_key1, 64u, key1, sizeof(key1), NULL, 0u) != 0 ||
      blake2b(d_key2, 64u, key2, sizeof(key2), NULL, 0u) != 0) {
    printf("[tests] blake2b keyed failed\n");
    return 1;
  }
  if (memcmp(d_nokey, d_key1, 64) == 0) {
    printf("[tests] blake2b key1 == no-key\n");
    fails++;
  }
  if (memcmp(d_key1, d_key2, 64) == 0) {
    printf("[tests] blake2b key1 == key2\n");
    fails++;
  }
  return fails;
}

static int test_blake2b_fail_closed(void) {
  int fails = 0;
  uint8_t digest[64];
  if (blake2b(NULL, 64u, NULL, 0u, NULL, 0u) == 0) {
    printf("[tests] blake2b NULL out accepted\n");
    fails++;
  }
  if (blake2b(digest, 0u, NULL, 0u, NULL, 0u) == 0) {
    printf("[tests] blake2b outlen=0 accepted\n");
    fails++;
  }
  if (blake2b(digest, 65u, NULL, 0u, NULL, 0u) == 0) {
    printf("[tests] blake2b outlen=65 accepted\n");
    fails++;
  }
  if (blake2b(digest, 64u, NULL, 65u, NULL, 0u) == 0) {
    printf("[tests] blake2b keylen=65 accepted\n");
    fails++;
  }
  /* keylen > 0 mas key NULL deve rejeitar */
  if (blake2b(digest, 64u, NULL, 16u, NULL, 0u) == 0) {
    printf("[tests] blake2b NULL key with keylen>0 accepted\n");
    fails++;
  }
  return fails;
}

static int test_argon2id_smoke(void) {
  /*
   * Smoke test estrutural para Argon2id (RFC 9106) em parallelism=1.
   *
   * Os vetores canonicos do reference impl (kats/argon2id) usam p=4
   * com secret + AD, fora do escopo desta implementacao. Cobrimos as
   * propriedades estruturais do KDF que validam o pipeline G + H' +
   * indexing:
   *
   *   1. Determinismo: mesmo input -> mesmo output.
   *   2. Sensibilidade a password (avalanche).
   *   3. Sensibilidade a salt.
   *   4. Sensibilidade a t_cost.
   *   5. Sensibilidade a m_cost.
   *   6. Fail-closed em todos os parametros invalidos.
   *
   * Para validacao externa contra reference impl auditavel, executar:
   *
   *   echo -n password | ./argon2 somesalt -t 1 -m 3 -p 1 -l 32 -id -v 13
   *
   * (m=3 KiB para validacao rapida; resultado deve casar com hash
   * computado por argon2id_hash(password=8, salt=8, t=1, m_cost=8,
   * out_len=32) ... mas m_cost minimo aqui e 8 KiB per RFC 9106 §3.1
   * com p=1.) — KAT pode ser adicionado em slice futuro com vector
   * gerado por reference impl auditavel.
   */
  int fails = 0;

  /* m_cost = 8 KiB -> 8 * 1024 = 8192 bytes memory buffer */
  static uint8_t memory[8u * 1024u];
  uint8_t out1[32], out2[32], out3[32];

  const uint8_t password[8] = {'p', 'a', 's', 's', 'w', 'o', 'r', 'd'};
  const uint8_t password_b[8] = {'p', 'a', 's', 's', 'w', 'o', 'r', 'e'};
  const uint8_t salt[8] = {'s', 'o', 'm', 'e', 's', 'a', 'l', 't'};
  const uint8_t salt_b[8] = {'s', 'o', 'm', 'e', 's', 'a', 'l', 'u'};

  /* Baseline - KAT validated against argon2-cffi (Python reference impl):
   *   argon2id(password='password', salt='somesalt', t=1, m=8, p=1,
   *            hash_len=32, type=Type.ID, version=0x13)
   *   == f137f8e186a403a679ccd0606e5ab5dcdafe43c1640855ac8c6e33e9bd63eeb3 */
  static const char *k_baseline_kat =
      "f137f8e186a403a679ccd0606e5ab5dc"
      "dafe43c1640855ac8c6e33e9bd63eeb3";
  if (argon2id_hash(password, 8u, salt, 8u, 1u, 8u, memory, sizeof(memory),
                    out1, 32u) != 0) {
    printf("[tests] argon2id baseline failed\n");
    return 1;
  }
  fails += expect_hex("argon2id(p=password,s=somesalt,t=1,m=8,p=1,len=32)",
                      out1, 32, k_baseline_kat);

  /* Determinismo */
  if (argon2id_hash(password, 8u, salt, 8u, 1u, 8u, memory, sizeof(memory),
                    out2, 32u) != 0) {
    printf("[tests] argon2id second call failed\n");
    fails++;
  }
  if (memcmp(out1, out2, 32) != 0) {
    printf("[tests] argon2id is not deterministic\n");
    fails++;
  }

  /* Sensibilidade a password */
  if (argon2id_hash(password_b, 8u, salt, 8u, 1u, 8u, memory, sizeof(memory),
                    out3, 32u) != 0 ||
      memcmp(out1, out3, 32) == 0) {
    printf("[tests] argon2id NOT sensitive to password\n");
    fails++;
  }

  /* Sensibilidade a salt */
  if (argon2id_hash(password, 8u, salt_b, 8u, 1u, 8u, memory, sizeof(memory),
                    out3, 32u) != 0 ||
      memcmp(out1, out3, 32) == 0) {
    printf("[tests] argon2id NOT sensitive to salt\n");
    fails++;
  }

  /* Sensibilidade a t_cost */
  if (argon2id_hash(password, 8u, salt, 8u, 2u, 8u, memory, sizeof(memory),
                    out3, 32u) != 0 ||
      memcmp(out1, out3, 32) == 0) {
    printf("[tests] argon2id NOT sensitive to t_cost\n");
    fails++;
  }

  /* Sensibilidade a m_cost (precisa de memory buffer maior) */
  static uint8_t memory_big[16u * 1024u];
  if (argon2id_hash(password, 8u, salt, 8u, 1u, 16u, memory_big,
                    sizeof(memory_big), out3, 32u) != 0 ||
      memcmp(out1, out3, 32) == 0) {
    printf("[tests] argon2id NOT sensitive to m_cost\n");
    fails++;
  }

  /* Sensibilidade a out_len: out_len=16 deve diferir do prefixo de
   * out_len=32 (H' inclui T_le no input). */
  uint8_t out_short[16];
  if (argon2id_hash(password, 8u, salt, 8u, 1u, 8u, memory, sizeof(memory),
                    out_short, 16u) != 0) {
    printf("[tests] argon2id out_len=16 failed\n");
    fails++;
  } else if (memcmp(out1, out_short, 16) == 0) {
    printf("[tests] argon2id out_len=16 == prefix de out_len=32\n");
    fails++;
  }

  /* Empty password (valido per RFC) */
  if (argon2id_hash(NULL, 0u, salt, 8u, 1u, 8u, memory, sizeof(memory),
                    out3, 32u) != 0) {
    printf("[tests] argon2id empty password rejected\n");
    fails++;
  }

  /* Fail-closed: NULL salt */
  if (argon2id_hash(password, 8u, NULL, 8u, 1u, 8u, memory, sizeof(memory),
                    out3, 32u) == 0) {
    printf("[tests] argon2id NULL salt accepted\n");
    fails++;
  }

  /* Fail-closed: salt curto */
  if (argon2id_hash(password, 8u, salt, 7u, 1u, 8u, memory, sizeof(memory),
                    out3, 32u) == 0) {
    printf("[tests] argon2id salt < 8 bytes accepted\n");
    fails++;
  }

  /* Fail-closed: t_cost = 0 */
  if (argon2id_hash(password, 8u, salt, 8u, 0u, 8u, memory, sizeof(memory),
                    out3, 32u) == 0) {
    printf("[tests] argon2id t_cost=0 accepted\n");
    fails++;
  }

  /* Fail-closed: m_cost < 8 */
  if (argon2id_hash(password, 8u, salt, 8u, 1u, 7u, memory, sizeof(memory),
                    out3, 32u) == 0) {
    printf("[tests] argon2id m_cost=7 accepted\n");
    fails++;
  }

  /* Fail-closed: memory insuficiente */
  if (argon2id_hash(password, 8u, salt, 8u, 1u, 8u, memory, 1024u,
                    out3, 32u) == 0) {
    printf("[tests] argon2id memory_len=1024 accepted (precisa 8192)\n");
    fails++;
  }

  /* Fail-closed: out_len < 4 */
  if (argon2id_hash(password, 8u, salt, 8u, 1u, 8u, memory, sizeof(memory),
                    out3, 3u) == 0) {
    printf("[tests] argon2id out_len=3 accepted\n");
    fails++;
  }

  /* Fail-closed: NULL out */
  if (argon2id_hash(password, 8u, salt, 8u, 1u, 8u, memory, sizeof(memory),
                    NULL, 32u) == 0) {
    printf("[tests] argon2id NULL out accepted\n");
    fails++;
  }

  /* Fail-closed: NULL memory */
  if (argon2id_hash(password, 8u, salt, 8u, 1u, 8u, NULL, 0u, out3, 32u) ==
      0) {
    printf("[tests] argon2id NULL memory accepted\n");
    fails++;
  }

  return fails;
}

static int test_crypt_derive_xts_keys_argon2id(void) {
  /*
   * Contract tests for the Argon2id-based AES-XTS volume key
   * derivation primitive introduced in alpha.220. The host test
   * stub kalloc backs onto malloc, so the 8 MiB work memory the
   * primitive allocates internally is satisfied transparently. We
   * keep the tests at the smallest accepted parameters
   * (`t_cost=1, m_cost=8`) to keep the suite quick; the kernel
   * itself uses `CRYPT_VOLUME_ARGON2ID_T_COST=3, M_COST=8192` so
   * timing in production is bounded by Argon2id's intrinsic cost
   * rather than by this test.
   */
  int fails = 0;
  const char *password = "volume-key-probe";
  const uint8_t salt_a[16] = {
      'N', 'o', 'i', 'r', 'O', 'S', '-', 'F', 'S', '-',
      'S', 'a', 'l', 't', '!', 0x00};
  const uint8_t salt_b[16] = {
      'C', 'a', 'p', 'y', 'O', 'S', '-', 'V', 'o', 'l',
      '-', 'S', 'a', 'l', 't', '!'};
  uint8_t k1_a[CRYPT_KEY_SIZE], k2_a[CRYPT_KEY_SIZE];
  uint8_t k1_b[CRYPT_KEY_SIZE], k2_b[CRYPT_KEY_SIZE];
  uint8_t k1_repeat[CRYPT_KEY_SIZE], k2_repeat[CRYPT_KEY_SIZE];

  /* Determinism: identical input must yield identical key material
   * across calls (every byte of `key1` and `key2`). */
  if (crypt_derive_xts_keys_argon2id(password, salt_a, sizeof(salt_a), 1u,
                                     8u, k1_a, k2_a) != 0) {
    printf("[tests] crypt_derive_xts_keys_argon2id baseline derive failed\n");
    return 1;
  }
  if (crypt_derive_xts_keys_argon2id(password, salt_a, sizeof(salt_a), 1u,
                                     8u, k1_repeat, k2_repeat) != 0) {
    printf("[tests] crypt_derive_xts_keys_argon2id repeat derive failed\n");
    return 1;
  }
  if (memcmp(k1_a, k1_repeat, CRYPT_KEY_SIZE) != 0 ||
      memcmp(k2_a, k2_repeat, CRYPT_KEY_SIZE) != 0) {
    printf("[tests] crypt_derive_xts_keys_argon2id NOT deterministic\n");
    fails++;
  }

  /* key1 and key2 are distinct halves of the 64-byte Argon2id
   * output. Different inputs must yield different keys. */
  if (memcmp(k1_a, k2_a, CRYPT_KEY_SIZE) == 0) {
    printf("[tests] crypt_derive_xts_keys_argon2id key1 == key2 (split bug)\n");
    fails++;
  }

  /* Salt sensitivity: changing the salt must change BOTH keys
   * (because Argon2id mixes the salt into H0 which feeds the
   * entire matrix). */
  if (crypt_derive_xts_keys_argon2id(password, salt_b, sizeof(salt_b), 1u,
                                     8u, k1_b, k2_b) != 0 ||
      memcmp(k1_a, k1_b, CRYPT_KEY_SIZE) == 0 ||
      memcmp(k2_a, k2_b, CRYPT_KEY_SIZE) == 0) {
    printf("[tests] crypt_derive_xts_keys_argon2id NOT sensitive to salt\n");
    fails++;
  }

  /* Fail-closed: NULL password, NULL salt, NULL key1, NULL key2.
   * In every failure case the output buffers must be wiped to
   * zero so a caller forgetting to check the return value cannot
   * accidentally seed AES-XTS with stack residue. */
  uint8_t sentinel1[CRYPT_KEY_SIZE];
  uint8_t sentinel2[CRYPT_KEY_SIZE];
  for (size_t i = 0; i < CRYPT_KEY_SIZE; ++i) {
    sentinel1[i] = 0xA5u;
    sentinel2[i] = 0xA5u;
  }
  if (crypt_derive_xts_keys_argon2id(NULL, salt_a, sizeof(salt_a), 1u, 8u,
                                     sentinel1, sentinel2) == 0) {
    printf("[tests] crypt_derive_xts_keys_argon2id accepted NULL password\n");
    fails++;
  }
  for (size_t i = 0; i < CRYPT_KEY_SIZE; ++i) {
    if (sentinel1[i] != 0u || sentinel2[i] != 0u) {
      printf(
          "[tests] crypt_derive_xts_keys_argon2id failure path left key "
          "byte %zu unwiped\n",
          i);
      fails++;
      break;
    }
  }
  if (crypt_derive_xts_keys_argon2id(password, NULL, sizeof(salt_a), 1u, 8u,
                                     k1_a, k2_a) == 0) {
    printf("[tests] crypt_derive_xts_keys_argon2id accepted NULL salt\n");
    fails++;
  }
  if (crypt_derive_xts_keys_argon2id(password, salt_a, sizeof(salt_a), 1u, 8u,
                                     NULL, k2_a) == 0) {
    printf("[tests] crypt_derive_xts_keys_argon2id accepted NULL key1\n");
    fails++;
  }
  if (crypt_derive_xts_keys_argon2id(password, salt_a, sizeof(salt_a), 1u, 8u,
                                     k1_a, NULL) == 0) {
    printf("[tests] crypt_derive_xts_keys_argon2id accepted NULL key2\n");
    fails++;
  }
  /* t_cost = 0 violates RFC 9106 §3.1 lower bound. */
  if (crypt_derive_xts_keys_argon2id(password, salt_a, sizeof(salt_a), 0u, 8u,
                                     k1_a, k2_a) == 0) {
    printf("[tests] crypt_derive_xts_keys_argon2id accepted t_cost=0\n");
    fails++;
  }
  /* m_cost < 8 violates RFC 9106 §3.1 lower bound (p=1). */
  if (crypt_derive_xts_keys_argon2id(password, salt_a, sizeof(salt_a), 1u, 7u,
                                     k1_a, k2_a) == 0) {
    printf("[tests] crypt_derive_xts_keys_argon2id accepted m_cost=7\n");
    fails++;
  }
  /* salt_len < 8 violates the bound enforced by this dispatcher to
   * keep future callers from weakening the construction. */
  if (crypt_derive_xts_keys_argon2id(password, salt_a, 7u, 1u, 8u, k1_a,
                                     k2_a) == 0) {
    printf("[tests] crypt_derive_xts_keys_argon2id accepted salt_len=7\n");
    fails++;
  }

  /* PBKDF2 vs Argon2id non-collision: the two derivations must
   * produce different keys for the same password+salt. */
  uint8_t pbkdf2_k1[CRYPT_KEY_SIZE], pbkdf2_k2[CRYPT_KEY_SIZE];
  crypt_derive_xts_keys(password, salt_a, sizeof(salt_a), 4u, pbkdf2_k1,
                        pbkdf2_k2);
  if (memcmp(pbkdf2_k1, k1_a, CRYPT_KEY_SIZE) == 0 &&
      memcmp(pbkdf2_k2, k2_a, CRYPT_KEY_SIZE) == 0) {
    printf("[tests] crypt_derive_xts_keys_argon2id COLLIDED with PBKDF2\n");
    fails++;
  }

  return fails;
}

int run_crypt_vector_tests(void) {
  int fails = 0;
  fails += test_sha256_vectors();
  fails += test_pbkdf2_vectors();
  fails += test_aes_xts_vector();
  fails += test_block0_with_wrappers();
  fails += test_constant_time_compare_semantics();
  fails += test_sha256_clear_semantics();
  fails += test_ed25519_failclosed_contract();
  fails += test_hkdf_sha256_vectors();
  fails += test_chacha20_block_vectors();
  fails += test_chacha20_encrypt_round_trip();
  fails += test_poly1305_vectors();
  fails += test_chacha20_poly1305_aead();
  fails += test_x25519_rfc7748_scalarmult();
  fails += test_x25519_rfc7748_dh();
  fails += test_x25519_small_order_rejection();
  fails += test_x25519_fail_closed();
  fails += test_x25519_high_bit_masked();
  fails += test_x25519_scalar_clamping();
  fails += test_blake2b_rfc7693_abc();
  fails += test_blake2b_empty();
  fails += test_blake2b_multiblock();
  fails += test_blake2b_streaming_equals_oneshot();
  fails += test_blake2b_variable_output();
  fails += test_blake2b_keyed();
  fails += test_blake2b_fail_closed();
  fails += test_argon2id_smoke();
  fails += test_crypt_derive_xts_keys_argon2id();
  if (fails == 0) {
    printf("[tests] crypt_vectors OK\n");
  }
  return fails;
}
