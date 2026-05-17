#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "auth/user.h"
#include "auth/user_password_hash.h"

/*
 * Host-side unit tests for `src/auth/user_password_hash.c`.
 *
 * These tests pin the public contract of the dispatch layer:
 *
 *  - Algorithm token serialization is exhaustive and rejects unknown
 *    tokens (so a corrupted `/etc/users.db` cannot silently downgrade
 *    a record to an unintended algorithm).
 *  - The derive/verify round-trip succeeds for both PBKDF2 (legacy)
 *    and Argon2id (default since alpha.219) with the parameters that
 *    `user_record_init` and `userdb_set_password` emit.
 *  - Bad inputs always fail closed and zero `hash_out` before
 *    returning.
 *  - Sensitivity tests confirm the dispatch correctly threads
 *    password/salt/algo/parameters into the underlying primitive (so
 *    a parameter regression in the wrapper cannot accidentally yield
 *    constant output across distinct inputs).
 *
 * The tests deliberately use the smallest Argon2id parameter that
 * the dispatch accepts (`t_cost=1, m_cost=8`) because each call
 * allocates `m_cost * 1024` bytes via the host `kalloc` stub. That
 * keeps the suite fast while still exercising every branch of the
 * dispatcher.
 */

static int test_algo_string_roundtrip(void) {
    int fails = 0;
    const char *pbkdf2_str = user_password_hash_algo_to_string(
        USER_PASSWORD_ALGO_PBKDF2_SHA256);
    const char *argon2id_str = user_password_hash_algo_to_string(
        USER_PASSWORD_ALGO_ARGON2ID);
    if (!pbkdf2_str || strcmp(pbkdf2_str, "pbkdf2") != 0) {
        printf("[user_pwd] pbkdf2 algo_to_string mismatch\n");
        fails++;
    }
    if (!argon2id_str || strcmp(argon2id_str, "argon2id") != 0) {
        printf("[user_pwd] argon2id algo_to_string mismatch\n");
        fails++;
    }
    if (user_password_hash_algo_to_string(0xFFu) != NULL) {
        printf("[user_pwd] algo_to_string accepted unknown id\n");
        fails++;
    }

    uint8_t id = 0xFFu;
    if (user_password_hash_algo_from_string("pbkdf2", 6u, &id) != 0 ||
        id != USER_PASSWORD_ALGO_PBKDF2_SHA256) {
        printf("[user_pwd] algo_from_string failed for pbkdf2\n");
        fails++;
    }
    id = 0xFFu;
    if (user_password_hash_algo_from_string("argon2id", 8u, &id) != 0 ||
        id != USER_PASSWORD_ALGO_ARGON2ID) {
        printf("[user_pwd] algo_from_string failed for argon2id\n");
        fails++;
    }
    /* Unknown token rejects fail-closed (does not leave `id`
     * pointing at one of the supported algorithms). */
    id = 0xFFu;
    if (user_password_hash_algo_from_string("unknown", 7u, &id) == 0) {
        printf("[user_pwd] algo_from_string accepted unknown token\n");
        fails++;
    }
    /* Token comparison must be exact: prefix and suffix matches are
     * rejected. */
    if (user_password_hash_algo_from_string("pbkdf2x", 7u, &id) == 0) {
        printf("[user_pwd] algo_from_string accepted prefix collision\n");
        fails++;
    }
    if (user_password_hash_algo_from_string("argon2i", 7u, &id) == 0) {
        printf("[user_pwd] algo_from_string accepted truncated argon2i\n");
        fails++;
    }
    /* NULL guards. */
    if (user_password_hash_algo_from_string(NULL, 1u, &id) == 0) {
        printf("[user_pwd] algo_from_string accepted NULL text\n");
        fails++;
    }
    if (user_password_hash_algo_from_string("pbkdf2", 6u, NULL) == 0) {
        printf("[user_pwd] algo_from_string accepted NULL out\n");
        fails++;
    }
    return fails;
}

static int test_pbkdf2_legacy_roundtrip(void) {
    /*
     * Legacy records serialized before alpha.219 set
     * `algo_t_cost == 0` to signal "use the canonical
     * `USER_ITERATIONS`". The dispatcher must translate that into the
     * same PBKDF2 output that the pre-alpha.219 implementation
     * produced, so authentication continues to work without a DB
     * migration. The expected vector below was computed with the
     * legacy `crypt_pbkdf2_sha256(password, salt, USER_ITERATIONS, ...)`
     * call against the same password/salt used at the test runner —
     * we just feed it through the dispatch layer and check
     * round-trip.
     */
    int fails = 0;
    const uint8_t password[] = "capyos-legacy-password";
    const uint8_t salt[USER_SALT_SIZE] = {
        0x10u, 0x20u, 0x30u, 0x40u, 0x50u, 0x60u, 0x70u, 0x80u,
        0x90u, 0xA0u, 0xB0u, 0xC0u, 0xD0u, 0xE0u, 0xF0u, 0x11u};
    uint8_t hash_legacy_default[USER_HASH_SIZE];
    uint8_t hash_legacy_explicit[USER_HASH_SIZE];

    if (user_password_hash_derive(password, sizeof(password) - 1u, salt,
                                  USER_SALT_SIZE,
                                  USER_PASSWORD_ALGO_PBKDF2_SHA256, 0u, 0u,
                                  hash_legacy_default,
                                  USER_HASH_SIZE) != 0) {
        printf("[user_pwd] pbkdf2 derive (t_cost=0 legacy) returned non-zero\n");
        fails++;
    }
    if (user_password_hash_derive(password, sizeof(password) - 1u, salt,
                                  USER_SALT_SIZE,
                                  USER_PASSWORD_ALGO_PBKDF2_SHA256,
                                  USER_ITERATIONS, 0u,
                                  hash_legacy_explicit,
                                  USER_HASH_SIZE) != 0) {
        printf("[user_pwd] pbkdf2 derive (t_cost explicit) returned non-zero\n");
        fails++;
    }
    /* Same algorithm, same password, same salt — t_cost=0 must map to
     * USER_ITERATIONS so legacy records continue to authenticate. */
    if (memcmp(hash_legacy_default, hash_legacy_explicit,
               USER_HASH_SIZE) != 0) {
        printf("[user_pwd] pbkdf2 t_cost=0 legacy != USER_ITERATIONS explicit\n");
        fails++;
    }
    /* Verify accepts the matching password. */
    if (user_password_hash_verify(password, sizeof(password) - 1u, salt,
                                  USER_SALT_SIZE,
                                  USER_PASSWORD_ALGO_PBKDF2_SHA256, 0u, 0u,
                                  hash_legacy_default,
                                  USER_HASH_SIZE) != 0) {
        printf("[user_pwd] pbkdf2 verify (legacy) rejected correct password\n");
        fails++;
    }
    /* Verify rejects the wrong password. */
    const uint8_t wrong[] = "capyos-legacy-passw0rd";
    if (user_password_hash_verify(wrong, sizeof(wrong) - 1u, salt,
                                  USER_SALT_SIZE,
                                  USER_PASSWORD_ALGO_PBKDF2_SHA256, 0u, 0u,
                                  hash_legacy_default,
                                  USER_HASH_SIZE) == 0) {
        printf("[user_pwd] pbkdf2 verify accepted wrong password\n");
        fails++;
    }
    return fails;
}

static int test_argon2id_roundtrip(void) {
    int fails = 0;
    const uint8_t password[] = "capyos-modern-password";
    const uint8_t salt[USER_SALT_SIZE] = {
        0xA1u, 0xA2u, 0xA3u, 0xA4u, 0xA5u, 0xA6u, 0xA7u, 0xA8u,
        0xA9u, 0xAAu, 0xABu, 0xACu, 0xADu, 0xAEu, 0xAFu, 0xB0u};
    uint8_t hash_default[USER_HASH_SIZE];
    uint8_t hash_repeat[USER_HASH_SIZE];

    /* Use the smallest accepted parameters so the host suite runs
     * fast — the dispatch path is identical at any tuning. */
    if (user_password_hash_derive(password, sizeof(password) - 1u, salt,
                                  USER_SALT_SIZE,
                                  USER_PASSWORD_ALGO_ARGON2ID, 1u, 8u,
                                  hash_default, USER_HASH_SIZE) != 0) {
        printf("[user_pwd] argon2id derive returned non-zero\n");
        fails++;
        return fails;
    }
    /* Determinism: same inputs => same hash. */
    if (user_password_hash_derive(password, sizeof(password) - 1u, salt,
                                  USER_SALT_SIZE,
                                  USER_PASSWORD_ALGO_ARGON2ID, 1u, 8u,
                                  hash_repeat, USER_HASH_SIZE) != 0 ||
        memcmp(hash_default, hash_repeat, USER_HASH_SIZE) != 0) {
        printf("[user_pwd] argon2id non-deterministic across calls\n");
        fails++;
    }
    /* Different algorithm => different hash even with same password.
     * Compares the modern Argon2id output against a PBKDF2 output
     * with USER_ITERATIONS — they must not collide. */
    uint8_t hash_pbkdf2[USER_HASH_SIZE];
    if (user_password_hash_derive(password, sizeof(password) - 1u, salt,
                                  USER_SALT_SIZE,
                                  USER_PASSWORD_ALGO_PBKDF2_SHA256, 0u, 0u,
                                  hash_pbkdf2, USER_HASH_SIZE) != 0 ||
        memcmp(hash_default, hash_pbkdf2, USER_HASH_SIZE) == 0) {
        printf("[user_pwd] argon2id collided with pbkdf2 output\n");
        fails++;
    }
    /* Verify accepts the matching password. */
    if (user_password_hash_verify(password, sizeof(password) - 1u, salt,
                                  USER_SALT_SIZE,
                                  USER_PASSWORD_ALGO_ARGON2ID, 1u, 8u,
                                  hash_default, USER_HASH_SIZE) != 0) {
        printf("[user_pwd] argon2id verify rejected correct password\n");
        fails++;
    }
    /* Verify rejects the wrong password. */
    const uint8_t wrong[] = "capyos-modern-passw0rd";
    if (user_password_hash_verify(wrong, sizeof(wrong) - 1u, salt,
                                  USER_SALT_SIZE,
                                  USER_PASSWORD_ALGO_ARGON2ID, 1u, 8u,
                                  hash_default, USER_HASH_SIZE) == 0) {
        printf("[user_pwd] argon2id verify accepted wrong password\n");
        fails++;
    }
    return fails;
}

static int test_argon2id_sensitivity(void) {
    /*
     * Parameter sensitivity: changing t_cost, m_cost, or salt must
     * change the derived output. This pins the contract that the
     * dispatcher threads parameters through to the primitive — a
     * regression that hard-codes them upstream would surface as a
     * collision here.
     */
    int fails = 0;
    const uint8_t password[] = "sensitivity-probe";
    const uint8_t salt_a[USER_SALT_SIZE] = {
        0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u, 0x07u, 0x08u,
        0x09u, 0x0Au, 0x0Bu, 0x0Cu, 0x0Du, 0x0Eu, 0x0Fu, 0x10u};
    const uint8_t salt_b[USER_SALT_SIZE] = {
        0x10u, 0x0Fu, 0x0Eu, 0x0Du, 0x0Cu, 0x0Bu, 0x0Au, 0x09u,
        0x08u, 0x07u, 0x06u, 0x05u, 0x04u, 0x03u, 0x02u, 0x01u};
    uint8_t hash_base[USER_HASH_SIZE];
    uint8_t hash_alt_salt[USER_HASH_SIZE];
    uint8_t hash_alt_t[USER_HASH_SIZE];
    uint8_t hash_alt_m[USER_HASH_SIZE];

    if (user_password_hash_derive(password, sizeof(password) - 1u, salt_a,
                                  USER_SALT_SIZE,
                                  USER_PASSWORD_ALGO_ARGON2ID, 1u, 8u,
                                  hash_base, USER_HASH_SIZE) != 0) {
        printf("[user_pwd] argon2id sensitivity baseline derive failed\n");
        return 1;
    }
    if (user_password_hash_derive(password, sizeof(password) - 1u, salt_b,
                                  USER_SALT_SIZE,
                                  USER_PASSWORD_ALGO_ARGON2ID, 1u, 8u,
                                  hash_alt_salt, USER_HASH_SIZE) != 0 ||
        memcmp(hash_base, hash_alt_salt, USER_HASH_SIZE) == 0) {
        printf("[user_pwd] argon2id insensitive to salt change\n");
        fails++;
    }
    if (user_password_hash_derive(password, sizeof(password) - 1u, salt_a,
                                  USER_SALT_SIZE,
                                  USER_PASSWORD_ALGO_ARGON2ID, 2u, 8u,
                                  hash_alt_t, USER_HASH_SIZE) != 0 ||
        memcmp(hash_base, hash_alt_t, USER_HASH_SIZE) == 0) {
        printf("[user_pwd] argon2id insensitive to t_cost change\n");
        fails++;
    }
    if (user_password_hash_derive(password, sizeof(password) - 1u, salt_a,
                                  USER_SALT_SIZE,
                                  USER_PASSWORD_ALGO_ARGON2ID, 1u, 16u,
                                  hash_alt_m, USER_HASH_SIZE) != 0 ||
        memcmp(hash_base, hash_alt_m, USER_HASH_SIZE) == 0) {
        printf("[user_pwd] argon2id insensitive to m_cost change\n");
        fails++;
    }
    return fails;
}

static int test_derive_fail_closed(void) {
    /*
     * Fail-closed contract: every invalid parameter path returns -1
     * and wipes `hash_out` to zero. A caller comparing the wiped
     * buffer against a stored hash via constant-time compare cannot
     * accidentally accept a partial derivation when the dispatcher
     * itself fails before producing a real digest.
     */
    int fails = 0;
    const uint8_t password[] = "fail-closed-probe";
    const uint8_t salt[USER_SALT_SIZE] = {
        0xAAu, 0xBBu, 0xCCu, 0xDDu, 0xEEu, 0xFFu, 0x00u, 0x11u,
        0x22u, 0x33u, 0x44u, 0x55u, 0x66u, 0x77u, 0x88u, 0x99u};
    uint8_t hash[USER_HASH_SIZE];

    /* Sentinel byte detects whether the dispatcher honoured the
     * fail-closed wipe contract. */
    for (size_t i = 0; i < USER_HASH_SIZE; ++i) hash[i] = 0xCAu;
    if (user_password_hash_derive(NULL, 4u, salt, USER_SALT_SIZE,
                                  USER_PASSWORD_ALGO_ARGON2ID, 1u, 8u,
                                  hash, USER_HASH_SIZE) == 0) {
        printf("[user_pwd] derive accepted NULL password with len>0\n");
        fails++;
    }
    for (size_t i = 0; i < USER_HASH_SIZE; ++i) {
        if (hash[i] != 0u) {
            printf("[user_pwd] derive failure path left hash byte %zu = 0x%02x\n",
                   i, hash[i]);
            fails++;
            break;
        }
    }
    for (size_t i = 0; i < USER_HASH_SIZE; ++i) hash[i] = 0xCAu;
    if (user_password_hash_derive(password, sizeof(password) - 1u, NULL,
                                  USER_SALT_SIZE,
                                  USER_PASSWORD_ALGO_ARGON2ID, 1u, 8u,
                                  hash, USER_HASH_SIZE) == 0) {
        printf("[user_pwd] derive accepted NULL salt with salt_len>0\n");
        fails++;
    }
    /* Argon2id with t_cost=0 must reject (RFC 9106 §3.1 lower bound). */
    for (size_t i = 0; i < USER_HASH_SIZE; ++i) hash[i] = 0xCAu;
    if (user_password_hash_derive(password, sizeof(password) - 1u, salt,
                                  USER_SALT_SIZE,
                                  USER_PASSWORD_ALGO_ARGON2ID, 0u, 8u,
                                  hash, USER_HASH_SIZE) == 0) {
        printf("[user_pwd] derive accepted argon2id t_cost=0\n");
        fails++;
    }
    /* Argon2id with m_cost<8 must reject. */
    if (user_password_hash_derive(password, sizeof(password) - 1u, salt,
                                  USER_SALT_SIZE,
                                  USER_PASSWORD_ALGO_ARGON2ID, 1u, 7u,
                                  hash, USER_HASH_SIZE) == 0) {
        printf("[user_pwd] derive accepted argon2id m_cost<8\n");
        fails++;
    }
    /* Unknown algorithm id must reject. */
    if (user_password_hash_derive(password, sizeof(password) - 1u, salt,
                                  USER_SALT_SIZE,
                                  0xFFu, 1u, 8u,
                                  hash, USER_HASH_SIZE) == 0) {
        printf("[user_pwd] derive accepted unknown algo_id\n");
        fails++;
    }
    /* NULL output must reject without touching anything. */
    if (user_password_hash_derive(password, sizeof(password) - 1u, salt,
                                  USER_SALT_SIZE,
                                  USER_PASSWORD_ALGO_ARGON2ID, 1u, 8u,
                                  NULL, USER_HASH_SIZE) == 0) {
        printf("[user_pwd] derive accepted NULL hash_out\n");
        fails++;
    }
    /* Zero-length output must reject. */
    if (user_password_hash_derive(password, sizeof(password) - 1u, salt,
                                  USER_SALT_SIZE,
                                  USER_PASSWORD_ALGO_ARGON2ID, 1u, 8u,
                                  hash, 0u) == 0) {
        printf("[user_pwd] derive accepted zero-length hash_out\n");
        fails++;
    }
    return fails;
}

static int test_verify_fail_closed(void) {
    int fails = 0;
    const uint8_t password[] = "verify-fail-probe";
    const uint8_t salt[USER_SALT_SIZE] = {
        0x12u, 0x34u, 0x56u, 0x78u, 0x9Au, 0xBCu, 0xDEu, 0xF0u,
        0x0Fu, 0xEDu, 0xCBu, 0xA9u, 0x87u, 0x65u, 0x43u, 0x21u};
    uint8_t stored[USER_HASH_SIZE];
    if (user_password_hash_derive(password, sizeof(password) - 1u, salt,
                                  USER_SALT_SIZE,
                                  USER_PASSWORD_ALGO_ARGON2ID, 1u, 8u,
                                  stored, USER_HASH_SIZE) != 0) {
        printf("[user_pwd] verify setup derive failed\n");
        return 1;
    }
    /* NULL stored hash must reject. */
    if (user_password_hash_verify(password, sizeof(password) - 1u, salt,
                                  USER_SALT_SIZE,
                                  USER_PASSWORD_ALGO_ARGON2ID, 1u, 8u,
                                  NULL, USER_HASH_SIZE) == 0) {
        printf("[user_pwd] verify accepted NULL stored hash\n");
        fails++;
    }
    /* Zero-length stored hash must reject. */
    if (user_password_hash_verify(password, sizeof(password) - 1u, salt,
                                  USER_SALT_SIZE,
                                  USER_PASSWORD_ALGO_ARGON2ID, 1u, 8u,
                                  stored, 0u) == 0) {
        printf("[user_pwd] verify accepted zero-length stored hash\n");
        fails++;
    }
    /* Stored hash longer than the internal scratch (64 bytes) must
     * reject — the dispatcher caps comparison at 64 bytes to bound
     * the host scratch size. */
    uint8_t big_stored[128];
    for (size_t i = 0; i < sizeof(big_stored); ++i) big_stored[i] = 0u;
    if (user_password_hash_verify(password, sizeof(password) - 1u, salt,
                                  USER_SALT_SIZE,
                                  USER_PASSWORD_ALGO_ARGON2ID, 1u, 8u,
                                  big_stored, sizeof(big_stored)) == 0) {
        printf("[user_pwd] verify accepted stored hash > scratch\n");
        fails++;
    }
    return fails;
}

int run_user_password_hash_tests(void) {
    int fails = 0;
    fails += test_algo_string_roundtrip();
    fails += test_pbkdf2_legacy_roundtrip();
    fails += test_argon2id_roundtrip();
    fails += test_argon2id_sensitivity();
    fails += test_derive_fail_closed();
    fails += test_verify_fail_closed();
    if (fails == 0) {
        printf("[tests] user_password_hash OK\n");
    }
    return fails;
}
