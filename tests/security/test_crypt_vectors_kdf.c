/*
 * tests/test_crypt_vectors_kdf.c
 *
 * KDF-tier vector coverage for the crypto module: X25519 (RFC 7748
 * §5.2 scalar-mult, §6.1 ECDH, small-order rejection, NULL inputs,
 * high-bit masking, scalar clamping), BLAKE2b (RFC 7693 Appendix A
 * "abc" + empty/multiblock/streaming/variable-output/keyed + fail
 * closed) and Argon2id RFC 9106 smoke (determinism + parameter
 * sensitivity + fail-closed) plus the AES-XTS Argon2id-based key
 * derivation contract (`crypt_derive_xts_keys_argon2id`).
 *
 * Carved out of `tests/test_crypt_vectors.c` at the 2026-05-15
 * monolith refactor so each host-test translation unit stays under
 * the 900-line layout limit. Shared hex helpers come from
 * `tests/test_crypt_vectors_internal.h`. The main entry
 * (`run_crypt_vector_tests`) lives in `tests/test_crypt_vectors.c`
 * and calls `test_crypt_vectors_kdf_cases()` here.
 */
#include <stdio.h>
#include <string.h>

#include "security/argon2.h"
#include "security/blake2b.h"
#include "security/crypt.h"
#include "security/x25519.h"

#include "test_crypt_vectors_internal.h"

static int test_x25519_rfc7748_scalarmult(void) {
  /* RFC 7748 §5.2 test vectors, iteration 1 (single scalar-mult). */
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

  uint8_t alice_pk[32];
  if (x25519_base(alice_sk, alice_pk) != 0) {
    printf("[tests] x25519_base(alice_sk) returned non-zero\n");
    fails++;
  }
  if (memcmp(alice_pk, alice_pk_expected, 32) != 0) {
    printf("[tests] x25519 alice public key mismatch\n");
    fails++;
  }

  uint8_t bob_pk[32];
  if (x25519_base(bob_sk, bob_pk) != 0) {
    printf("[tests] x25519_base(bob_sk) returned non-zero\n");
    fails++;
  }
  if (memcmp(bob_pk, bob_pk_expected, 32) != 0) {
    printf("[tests] x25519 bob public key mismatch\n");
    fails++;
  }

  uint8_t shared_alice[32];
  if (x25519(alice_sk, bob_pk, shared_alice) != 0) {
    printf("[tests] x25519(alice_sk, bob_pk) returned non-zero\n");
    fails++;
  }
  if (memcmp(shared_alice, shared_expected, 32) != 0) {
    printf("[tests] x25519 alice shared key mismatch\n");
    fails++;
  }

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
  /* Small-order point rejection: u=0 (order 4) and u=1. */
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
  /* RFC 7748 §5: bit 255 of u-coord must be masked. */
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
  u_b[31] ^= 0x80u;
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
  /* Scalar clamping per RFC 7748 §5: bits 0,1,2,255 cleared, bit 254 set. */
  int fails = 0;
  uint8_t s_a[32], s_b[32];
  for (int i = 0; i < 32; ++i) {
    s_a[i] = (uint8_t)(0x12u + i);
    s_b[i] = s_a[i];
  }
  s_b[0] ^= 0x07u;
  s_b[31] ^= 0x80u;
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
  blake2b_update(&ctx, input, 50u);
  blake2b_update(&ctx, input + 50u, 78u);
  blake2b_update(&ctx, input + 128u, 1u);
  blake2b_update(&ctx, input + 129u, 127u);
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
  int fails = 0;
  uint8_t d64[64], d32[32], d16[16];
  if (blake2b(d64, 64u, NULL, 0u, (const uint8_t *)"abc", 3u) != 0 ||
      blake2b(d32, 32u, NULL, 0u, (const uint8_t *)"abc", 3u) != 0 ||
      blake2b(d16, 16u, NULL, 0u, (const uint8_t *)"abc", 3u) != 0) {
    printf("[tests] blake2b variable output failed\n");
    return 1;
  }
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
  if (blake2b(digest, 64u, NULL, 16u, NULL, 0u) == 0) {
    printf("[tests] blake2b NULL key with keylen>0 accepted\n");
    fails++;
  }
  return fails;
}

static int test_argon2id_smoke(void) {
  /* Argon2id (RFC 9106) smoke: determinism + sensitivity + fail-closed. */
  int fails = 0;

  static uint8_t memory[8u * 1024u];
  uint8_t out1[32], out2[32], out3[32];

  const uint8_t password[8] = {'p', 'a', 's', 's', 'w', 'o', 'r', 'd'};
  const uint8_t password_b[8] = {'p', 'a', 's', 's', 'w', 'o', 'r', 'e'};
  const uint8_t salt[8] = {'s', 'o', 'm', 'e', 's', 'a', 'l', 't'};
  const uint8_t salt_b[8] = {'s', 'o', 'm', 'e', 's', 'a', 'l', 'u'};

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

  if (argon2id_hash(password, 8u, salt, 8u, 1u, 8u, memory, sizeof(memory),
                    out2, 32u) != 0) {
    printf("[tests] argon2id second call failed\n");
    fails++;
  }
  if (memcmp(out1, out2, 32) != 0) {
    printf("[tests] argon2id is not deterministic\n");
    fails++;
  }

  if (argon2id_hash(password_b, 8u, salt, 8u, 1u, 8u, memory, sizeof(memory),
                    out3, 32u) != 0 ||
      memcmp(out1, out3, 32) == 0) {
    printf("[tests] argon2id NOT sensitive to password\n");
    fails++;
  }

  if (argon2id_hash(password, 8u, salt_b, 8u, 1u, 8u, memory, sizeof(memory),
                    out3, 32u) != 0 ||
      memcmp(out1, out3, 32) == 0) {
    printf("[tests] argon2id NOT sensitive to salt\n");
    fails++;
  }

  if (argon2id_hash(password, 8u, salt, 8u, 2u, 8u, memory, sizeof(memory),
                    out3, 32u) != 0 ||
      memcmp(out1, out3, 32) == 0) {
    printf("[tests] argon2id NOT sensitive to t_cost\n");
    fails++;
  }

  static uint8_t memory_big[16u * 1024u];
  if (argon2id_hash(password, 8u, salt, 8u, 1u, 16u, memory_big,
                    sizeof(memory_big), out3, 32u) != 0 ||
      memcmp(out1, out3, 32) == 0) {
    printf("[tests] argon2id NOT sensitive to m_cost\n");
    fails++;
  }

  uint8_t out_short[16];
  if (argon2id_hash(password, 8u, salt, 8u, 1u, 8u, memory, sizeof(memory),
                    out_short, 16u) != 0) {
    printf("[tests] argon2id out_len=16 failed\n");
    fails++;
  } else if (memcmp(out1, out_short, 16) == 0) {
    printf("[tests] argon2id out_len=16 == prefix de out_len=32\n");
    fails++;
  }

  if (argon2id_hash(NULL, 0u, salt, 8u, 1u, 8u, memory, sizeof(memory),
                    out3, 32u) != 0) {
    printf("[tests] argon2id empty password rejected\n");
    fails++;
  }

  if (argon2id_hash(password, 8u, NULL, 8u, 1u, 8u, memory, sizeof(memory),
                    out3, 32u) == 0) {
    printf("[tests] argon2id NULL salt accepted\n");
    fails++;
  }

  if (argon2id_hash(password, 8u, salt, 7u, 1u, 8u, memory, sizeof(memory),
                    out3, 32u) == 0) {
    printf("[tests] argon2id salt < 8 bytes accepted\n");
    fails++;
  }

  if (argon2id_hash(password, 8u, salt, 8u, 0u, 8u, memory, sizeof(memory),
                    out3, 32u) == 0) {
    printf("[tests] argon2id t_cost=0 accepted\n");
    fails++;
  }

  if (argon2id_hash(password, 8u, salt, 8u, 1u, 7u, memory, sizeof(memory),
                    out3, 32u) == 0) {
    printf("[tests] argon2id m_cost=7 accepted\n");
    fails++;
  }

  if (argon2id_hash(password, 8u, salt, 8u, 1u, 8u, memory, 1024u,
                    out3, 32u) == 0) {
    printf("[tests] argon2id memory_len=1024 accepted (precisa 8192)\n");
    fails++;
  }

  if (argon2id_hash(password, 8u, salt, 8u, 1u, 8u, memory, sizeof(memory),
                    out3, 3u) == 0) {
    printf("[tests] argon2id out_len=3 accepted\n");
    fails++;
  }

  if (argon2id_hash(password, 8u, salt, 8u, 1u, 8u, memory, sizeof(memory),
                    NULL, 32u) == 0) {
    printf("[tests] argon2id NULL out accepted\n");
    fails++;
  }

  if (argon2id_hash(password, 8u, salt, 8u, 1u, 8u, NULL, 0u, out3, 32u) ==
      0) {
    printf("[tests] argon2id NULL memory accepted\n");
    fails++;
  }

  return fails;
}

static int test_crypt_derive_xts_keys_argon2id(void) {
  /* Argon2id-based AES-XTS volume key derivation contract. */
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

  if (memcmp(k1_a, k2_a, CRYPT_KEY_SIZE) == 0) {
    printf("[tests] crypt_derive_xts_keys_argon2id key1 == key2 (split bug)\n");
    fails++;
  }

  if (crypt_derive_xts_keys_argon2id(password, salt_b, sizeof(salt_b), 1u,
                                     8u, k1_b, k2_b) != 0 ||
      memcmp(k1_a, k1_b, CRYPT_KEY_SIZE) == 0 ||
      memcmp(k2_a, k2_b, CRYPT_KEY_SIZE) == 0) {
    printf("[tests] crypt_derive_xts_keys_argon2id NOT sensitive to salt\n");
    fails++;
  }

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
  if (crypt_derive_xts_keys_argon2id(password, salt_a, sizeof(salt_a), 0u, 8u,
                                     k1_a, k2_a) == 0) {
    printf("[tests] crypt_derive_xts_keys_argon2id accepted t_cost=0\n");
    fails++;
  }
  if (crypt_derive_xts_keys_argon2id(password, salt_a, sizeof(salt_a), 1u, 7u,
                                     k1_a, k2_a) == 0) {
    printf("[tests] crypt_derive_xts_keys_argon2id accepted m_cost=7\n");
    fails++;
  }
  if (crypt_derive_xts_keys_argon2id(password, salt_a, 7u, 1u, 8u, k1_a,
                                     k2_a) == 0) {
    printf("[tests] crypt_derive_xts_keys_argon2id accepted salt_len=7\n");
    fails++;
  }

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

int test_crypt_vectors_kdf_cases(void) {
  int fails = 0;
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
  return fails;
}
