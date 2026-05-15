#include "auth/user_password_hash.h"

#include <stddef.h>
#include <stdint.h>

#include "memory/kmem.h"
#include "security/argon2.h"
#include "security/crypt.h"

/*
 * Defensive zeroing helper used for derived-hash buffers and Argon2id
 * work memory. The pointer is marked `volatile` so the compiler cannot
 * eliminate the stores as dead even when the destination buffer is
 * freed or leaves scope immediately after — that dead-store elimination
 * is the classic way "secure wipe" code gets silently optimised away
 * in C.
 */
static void wipe_bytes(void *dst, size_t len) {
    if (!dst || len == 0) {
        return;
    }
    volatile uint8_t *p = (volatile uint8_t *)dst;
    while (len--) {
        *p++ = 0;
    }
}

static int bytes_equal(const char *a, size_t a_len, const char *b) {
    if (!a || !b) {
        return 0;
    }
    size_t i = 0;
    while (i < a_len && b[i] != '\0') {
        if (a[i] != b[i]) {
            return 0;
        }
        ++i;
    }
    return (i == a_len) && (b[i] == '\0');
}

const char *user_password_hash_algo_to_string(uint8_t algo_id) {
    switch (algo_id) {
        case USER_PASSWORD_ALGO_PBKDF2_SHA256:
            return "pbkdf2";
        case USER_PASSWORD_ALGO_ARGON2ID:
            return "argon2id";
        default:
            return (const char *)0;
    }
}

int user_password_hash_algo_from_string(const char *text, size_t len,
                                        uint8_t *out_algo_id) {
    if (!text || !out_algo_id) {
        return -1;
    }
    if (bytes_equal(text, len, "pbkdf2")) {
        *out_algo_id = USER_PASSWORD_ALGO_PBKDF2_SHA256;
        return 0;
    }
    if (bytes_equal(text, len, "argon2id")) {
        *out_algo_id = USER_PASSWORD_ALGO_ARGON2ID;
        return 0;
    }
    return -1;
}

static int derive_pbkdf2(const uint8_t *password, size_t password_len,
                         const uint8_t *salt, size_t salt_len,
                         uint32_t t_cost,
                         uint8_t *hash_out, size_t hash_out_len) {
    /*
     * For backward compatibility with legacy records serialized before
     * alpha.219, `t_cost == 0` is interpreted as "use the canonical
     * iteration count baked into the existing schema". This keeps
     * `/etc/users.db` entries from before alpha.219 hashing with the
     * exact same PBKDF2 parameters they were originally written with,
     * so authentication continues to work without any DB migration.
     */
    uint32_t iterations = (t_cost == 0u) ? USER_PBKDF2_DEFAULT_ITERATIONS
                                         : t_cost;
    crypt_pbkdf2_sha256(password, password_len, salt, salt_len, iterations,
                        hash_out, hash_out_len);
    return 0;
}

static int derive_argon2id(const uint8_t *password, size_t password_len,
                           const uint8_t *salt, size_t salt_len,
                           uint32_t t_cost, uint32_t m_cost,
                           uint8_t *hash_out, size_t hash_out_len) {
    if (t_cost < ARGON2_MIN_T_COST || m_cost < ARGON2_MIN_M_COST) {
        return -1;
    }
    /*
     * Argon2id requires `m_cost * 1024` bytes of working memory per
     * RFC 9106 §3.3 (`p=1`). The implementation in
     * `src/security/argon2.c` is caller-allocates by design so the
     * library never reaches for `kalloc` itself — this module bridges
     * the two: allocate, run, wipe, free. The work memory carries the
     * full Argon2id matrix (including data derived from the password
     * through the data-dependent passes) so it MUST be wiped before
     * `kfree` to keep the freed heap region from leaking material
     * that could be replayed against subsequent allocations.
     */
    size_t memory_bytes = (size_t)m_cost * 1024u;
    uint8_t *memory = (uint8_t *)kalloc(memory_bytes);
    if (!memory) {
        return -1;
    }
    int rc = argon2id_hash(password, password_len, salt, salt_len, t_cost,
                           m_cost, memory, memory_bytes, hash_out,
                           hash_out_len);
    wipe_bytes(memory, memory_bytes);
    kfree(memory);
    return rc;
}

int user_password_hash_derive(const uint8_t *password, size_t password_len,
                              const uint8_t *salt, size_t salt_len,
                              uint8_t algo_id,
                              uint32_t t_cost, uint32_t m_cost,
                              uint8_t *hash_out, size_t hash_out_len) {
    if (!hash_out || hash_out_len == 0) {
        return -1;
    }
    if (!salt && salt_len > 0) {
        return -1;
    }
    if (!password && password_len > 0) {
        wipe_bytes(hash_out, hash_out_len);
        return -1;
    }
    int rc = -1;
    switch (algo_id) {
        case USER_PASSWORD_ALGO_PBKDF2_SHA256:
            rc = derive_pbkdf2(password, password_len, salt, salt_len, t_cost,
                               hash_out, hash_out_len);
            break;
        case USER_PASSWORD_ALGO_ARGON2ID:
            rc = derive_argon2id(password, password_len, salt, salt_len, t_cost,
                                 m_cost, hash_out, hash_out_len);
            break;
        default:
            rc = -1;
            break;
    }
    if (rc != 0) {
        /*
         * Fail-closed: callers must not observe a partially derived
         * hash. Wiping `hash_out` removes any keystream/iteration
         * residue that may have leaked from the failing primitive (for
         * example a partial PBKDF2 block or the H' pre-hash of
         * Argon2id) before the caller compares it against a stored
         * value in constant time.
         */
        wipe_bytes(hash_out, hash_out_len);
    }
    return rc;
}

int user_password_hash_verify(const uint8_t *candidate_password,
                              size_t candidate_password_len,
                              const uint8_t *salt, size_t salt_len,
                              uint8_t algo_id,
                              uint32_t t_cost, uint32_t m_cost,
                              const uint8_t *stored_hash,
                              size_t stored_hash_len) {
    if (!stored_hash || stored_hash_len == 0) {
        return -1;
    }
    /*
     * Cap the derived-hash scratch buffer at 64 bytes (`USER_HASH_SIZE`
     * is 32 today; 64 leaves headroom for one future schema bump
     * without reworking the dispatcher). Stored hashes larger than the
     * buffer are rejected fail-closed because the dispatcher cannot
     * produce a comparison value of matching length.
     */
    uint8_t derived[64];
    if (stored_hash_len > sizeof(derived)) {
        return -1;
    }
    int rc = user_password_hash_derive(candidate_password,
                                       candidate_password_len, salt, salt_len,
                                       algo_id, t_cost, m_cost, derived,
                                       stored_hash_len);
    int verdict = -1;
    if (rc == 0) {
        int cmp = crypt_constant_time_compare(derived, stored_hash,
                                              stored_hash_len);
        verdict = (cmp == 0) ? 0 : -1;
    }
    wipe_bytes(derived, sizeof(derived));
    return verdict;
}
