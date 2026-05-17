/*
 * tests/test_crypt_vectors_aead.c
 *
 * AEAD-tier vector coverage for the crypto module: ed25519
 * fail-closed contract (RFC 8032 §7.1 vectors + tampering/NULL
 * rejection), HKDF-SHA256 (RFC 5869 Appendix A.1/A.2/A.3), ChaCha20
 * block (RFC 8439 Appendix A.1), ChaCha20 encrypt round-trip and
 * counter-overflow rejection, Poly1305 (RFC 8439 Appendix A.3) and
 * ChaCha20-Poly1305 AEAD round-trip + tampering rejection.
 *
 * Carved out of `tests/test_crypt_vectors.c` at the 2026-05-15
 * monolith refactor so each host-test translation unit stays under
 * the 900-line layout limit. Shared hex helpers come from
 * `tests/test_crypt_vectors_internal.h`. The main entry
 * (`run_crypt_vector_tests`) lives in `tests/test_crypt_vectors.c`
 * and calls `test_crypt_vectors_aead_cases()` here.
 */
#include <stdio.h>
#include <string.h>

#include "security/chacha20_poly1305.h"
#include "security/crypt.h"
#include "security/ed25519.h"

#include "test_crypt_vectors_internal.h"

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
    uint8_t pk[32], sk[64];
    ed25519_create_keypair(pk, sk, seed);
    if (memcmp(pk, pk_expected, 32) != 0) {
      printf("[tests] ed25519 vec %zu: pk mismatch\n", v);
      fails++;
    }
    uint8_t sig[64];
    ed25519_sign(sig, vectors[v].msg_len ? msg : NULL, vectors[v].msg_len,
                 pk, sk);
    if (memcmp(sig, sig_expected, 64) != 0) {
      printf("[tests] ed25519 vec %zu: sig mismatch\n", v);
      fails++;
    }
    if (ed25519_verify(sig_expected, vectors[v].msg_len ? msg : NULL,
                       vectors[v].msg_len, pk_expected) != 0) {
      printf("[tests] ed25519 vec %zu: verify rejected canonical sig\n", v);
      fails++;
    }
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
  memcpy(sig_bad_S + 32, L_le, 32);
  sig_bad_S[63] = 0x20;
  if (ed25519_verify(sig_bad_S, &msg0, 1u, pk0) == 0) {
    printf("[tests] ed25519: accepted non-canonical S > L\n");
    fails++;
  }

  if (ed25519_verify(NULL, &msg0, 1u, pk0) == 0) {
    printf("[tests] ed25519_verify accepted NULL signature\n");
    fails++;
  }
  if (ed25519_verify(sig0, &msg0, 1u, NULL) == 0) {
    printf("[tests] ed25519_verify accepted NULL public key\n");
    fails++;
  }

  ed25519_sign(NULL, &msg0, 1u, pk0, sk0);
  ed25519_create_keypair(NULL, NULL, seed0);

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
  msg_rt[10] ^= 0x01;
  if (ed25519_verify(sig_rt, msg_rt, 64u, pk_rt) == 0) {
    printf("[tests] ed25519 round-trip accepted tampered message\n");
    fails++;
  }
  msg_rt[10] ^= 0x01;
  uint8_t sig_rt2[64];
  ed25519_sign(sig_rt2, msg_rt, 64u, pk_rt, sk_rt);
  if (memcmp(sig_rt, sig_rt2, 64) != 0) {
    printf("[tests] ed25519 sign not deterministic\n");
    fails++;
  }

  return fails;
}

static int test_hkdf_sha256_vectors(void) {
  /* RFC 5869 Appendix A vectors plus fail-closed contract. */
  int fails = 0;
  uint8_t prk[SHA256_DIGEST_SIZE];
  uint8_t okm[128];

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
  if (crypt_hkdf_sha256_expand(prk, SHA256_DIGEST_SIZE, NULL, 0u, okm, 0u) !=
      0) {
    printf("[tests] hkdf expand did not accept L = 0 as no-op\n");
    fails++;
  }

  return fails;
}

static int test_chacha20_block_vectors(void) {
  /* RFC 8439 Appendix A.1 ChaCha20 block function vectors + fail-closed. */
  int fails = 0;
  uint8_t key[CHACHA20_KEY_SIZE];
  uint8_t nonce[CHACHA20_NONCE_SIZE];
  uint8_t out[CHACHA20_BLOCK_SIZE];

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
  /* Round-trip + in-place + counter overflow + fail-closed. */
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

  uint8_t small[1] = {0xAB};
  uint8_t small_out[1];
  if (chacha20_encrypt(key, 0xFFFFFFFFu, nonce, small, small_out, 65u) != -1) {
    printf("[tests] chacha20 did not reject counter overflow\n");
    fails++;
  }

  if (chacha20_encrypt(key, 0u, nonce, NULL, NULL, 0u) != 0) {
    printf("[tests] chacha20 did not accept len=0 as no-op\n");
    fails++;
  }
  if (chacha20_encrypt(NULL, 0u, nonce, NULL, NULL, 0u) != -1) {
    printf("[tests] chacha20 did not reject NULL key with len=0\n");
    fails++;
  }
  return fails;
}

static int test_poly1305_vectors(void) {
  /* RFC 8439 Appendix A.3 Poly1305 vectors + avalanche + fail-closed. */
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

  uint8_t msg2[64];
  memcpy(msg2, msg, sizeof(msg));
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

  uint8_t ct_bad[sizeof(k_plain) - 1u];
  memcpy(ct_bad, ct, sizeof(ct));
  ct_bad[50] ^= 0x01u;
  uint8_t pt_bad[sizeof(k_plain) - 1u];
  if (chacha20_poly1305_decrypt(key, nonce, k_aad, sizeof(k_aad), ct_bad,
                                sizeof(ct_bad), tag, pt_bad) != -1) {
    printf("[tests] aead decrypt accepted tampered ciphertext\n");
    fails++;
  }

  uint8_t aad_bad[20];
  memcpy(aad_bad, k_aad, sizeof(aad_bad));
  aad_bad[3] ^= 0x80u;
  if (chacha20_poly1305_decrypt(key, nonce, aad_bad, sizeof(aad_bad), ct,
                                sizeof(ct), tag, pt_bad) != -1) {
    printf("[tests] aead decrypt accepted tampered AAD\n");
    fails++;
  }

  uint8_t tag_bad[CHACHA20_POLY1305_TAG_SIZE];
  memcpy(tag_bad, tag, sizeof(tag_bad));
  tag_bad[7] ^= 0x10u;
  if (chacha20_poly1305_decrypt(key, nonce, k_aad, sizeof(k_aad), ct,
                                sizeof(ct), tag_bad, pt_bad) != -1) {
    printf("[tests] aead decrypt accepted tampered tag\n");
    fails++;
  }

  uint8_t key_bad[CHACHA20_KEY_SIZE];
  memcpy(key_bad, key, sizeof(key_bad));
  key_bad[0] ^= 0x01u;
  if (chacha20_poly1305_decrypt(key_bad, nonce, k_aad, sizeof(k_aad), ct,
                                sizeof(ct), tag, pt_bad) != -1) {
    printf("[tests] aead decrypt accepted wrong key\n");
    fails++;
  }

  uint8_t nonce_bad[CHACHA20_NONCE_SIZE];
  memcpy(nonce_bad, nonce, sizeof(nonce_bad));
  nonce_bad[5] ^= 0x01u;
  if (chacha20_poly1305_decrypt(key, nonce_bad, k_aad, sizeof(k_aad), ct,
                                sizeof(ct), tag, pt_bad) != -1) {
    printf("[tests] aead decrypt accepted wrong nonce\n");
    fails++;
  }

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

  uint8_t no_aad_tag[CHACHA20_POLY1305_TAG_SIZE];
  if (chacha20_poly1305_encrypt(key, nonce, NULL, 0u,
                                (const uint8_t *)k_plain,
                                sizeof(k_plain) - 1u, ct, no_aad_tag) != 0) {
    printf("[tests] aead encrypt rejected NULL AAD with aad_len=0\n");
    fails++;
  }
  if (memcmp(tag, no_aad_tag, sizeof(tag)) == 0) {
    printf("[tests] aead tag identical with and without AAD\n");
    fails++;
  }

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

int test_crypt_vectors_aead_cases(void) {
  int fails = 0;
  fails += test_ed25519_failclosed_contract();
  fails += test_hkdf_sha256_vectors();
  fails += test_chacha20_block_vectors();
  fails += test_chacha20_encrypt_round_trip();
  fails += test_poly1305_vectors();
  fails += test_chacha20_poly1305_aead();
  return fails;
}
