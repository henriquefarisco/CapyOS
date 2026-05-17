/*
 * tests/test_crypt_vectors_internal.h
 *
 * Shared hex helpers (`hex_nibble`, `decode_hex`, `expect_hex`) and
 * companion-entry declarations for the crypto vector host tests.
 * Created at the 2026-05-15 monolith refactor when the single file
 * `tests/test_crypt_vectors.c` (1910 LOC) was split into three
 * translation units by algorithm family:
 *   - tests/test_crypt_vectors.c        : helpers + entry + SHA-256,
 *                                          PBKDF2, AES-XTS, block0
 *                                          wrappers, constant-time
 *                                          compare, SHA-256 clear.
 *   - tests/test_crypt_vectors_aead.c   : ed25519 fail-closed
 *                                          contract, HKDF-SHA256,
 *                                          ChaCha20 block + encrypt,
 *                                          Poly1305, ChaCha20-Poly1305
 *                                          AEAD.
 *   - tests/test_crypt_vectors_kdf.c    : X25519 RFC 7748, BLAKE2b
 *                                          RFC 7693, Argon2id, AES-XTS
 *                                          key derivation.
 *
 * Not a public test interface — only the three `test_crypt_vectors*.c`
 * files include this header.
 */
#ifndef TEST_CRYPT_VECTORS_INTERNAL_H
#define TEST_CRYPT_VECTORS_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

uint8_t test_crypt_vectors_hex_nibble(char ch);
int test_crypt_vectors_decode_hex(const char *hex, uint8_t *out, size_t out_len);
int test_crypt_vectors_expect_hex(const char *name, const uint8_t *actual,
                                  size_t len, const char *expected_hex);

/* Companion entries — the main `run_crypt_vector_tests` defined in
 * `tests/test_crypt_vectors.c` calls these after running its own
 * cases. Each companion entry returns the number of failing cases
 * (0 = all passing). */
int test_crypt_vectors_aead_cases(void);
int test_crypt_vectors_kdf_cases(void);

/* Short aliases so existing test bodies stay verbatim after the
 * split. Only the three `test_crypt_vectors*.c` files see these
 * macros. */
#define hex_nibble  test_crypt_vectors_hex_nibble
#define decode_hex  test_crypt_vectors_decode_hex
#define expect_hex  test_crypt_vectors_expect_hex

#endif /* TEST_CRYPT_VECTORS_INTERNAL_H */
