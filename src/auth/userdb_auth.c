/*
 * src/auth/userdb_auth.c
 *
 * Credential verification and password mutation layer for the user
 * database. Composes the on-disk primitives owned by
 * `src/auth/userdb_io.c` with the rate-limiting policy in
 * `src/auth/auth_policy.c` and the KDF dispatch in
 * `src/auth/user_password_hash.c`.
 *
 * Carved out of `src/auth/user.c` when the monolith reached the
 * 900-line layout limit. The split keeps the storage layer
 * (parse/serialize/read/write) free of credential decisions, and
 * keeps this file free of disk-format trivia — both translation
 * units now fit well below the limit.
 */
#include "auth/user.h"
#include "auth/auth_policy.h"
#include "auth/user_password_hash.h"
#include "memory/kmem.h"

#include "auth/internal/user_helpers.h"
#include "auth/internal/userdb_io.h"

#include <stdint.h>

/*
 * Internal rewrite primitive shared by `userdb_set_password` (the
 * public, policy-gated entry point) and the implicit re-hash path
 * embedded in `userdb_authenticate`. Both callers need the same
 * read-modify-write of `/etc/users.db` with a fresh Argon2id
 * derivation. The split keeps the password policy check (which
 * `userdb_set_password` must enforce) outside the rewrite logic so
 * the implicit re-hash can run without re-validating a password the
 * caller already verified.
 */
static int userdb_replace_password_hash(const char *username,
                                        const char *new_password);

/* Fixed pseudo-random salt used to run a password derivation against
 * unknown usernames so that the wall-clock time spent inside
 * `userdb_authenticate` is the same whether the account exists or not.
 * The bytes are public ABI constants, not a secret: their only purpose
 * is to feed the KDF with a deterministic 16-byte buffer. Changing
 * these bytes does not affect correctness because the derived hash is
 * never compared against anything when the user is unknown — the
 * comparison branch is taken only when a real record was loaded from
 * the userdb. */
static const uint8_t k_userdb_dummy_salt[USER_SALT_SIZE] = {
    0x43u, 0x70u, 0x79u, 0x4fu, 0x53u, 0x2du, 0x6eu, 0x6fu,
    0x2du, 0x73u, 0x75u, 0x63u, 0x68u, 0x2du, 0x75u, 0x73u
};

int userdb_authenticate(const char *username, const char *password, struct user_record *out) {
    if (!username || !password) {
        return -1;
    }
    struct user_record rec;
    int user_found = (userdb_find(username, &rec) == 0) ? 1 : 0;
    /*
     * When the username does not match any record, run the KDF with
     * the dummy salt under the algorithm/parameters that
     * `user_record_init` emits for new records. That keeps the
     * dominant timing path (Argon2id with the default tuning) the
     * same as for a freshly-created account, so a remote attacker
     * probing for valid usernames cannot distinguish "unknown user"
     * from "valid user with a modern record" by measuring the wall
     * clock.
     *
     * Records still hashed under the legacy PBKDF2 path remain
     * faster (~50 ms vs ~200 ms for Argon2id) only until the next
     * successful authentication, at which point the implicit re-hash
     * block at the bottom of this function rewrites them to
     * Argon2id. The residual timing leak therefore exists only for
     * accounts that have never logged in since alpha.220 was
     * deployed, and it only reveals "this account predates the
     * alpha.220 rollout AND has not been used yet" — it never
     * reveals the password. The population of records that leak this
     * way is bounded by "accounts that have not logged in since the
     * upgrade" and shrinks monotonically as those accounts
     * authenticate.
     */
    const uint8_t *kdf_salt = user_found ? rec.salt : k_userdb_dummy_salt;
    uint8_t algo_id = user_found ? rec.algo_id
                                 : USER_PASSWORD_ALGO_ARGON2ID;
    uint32_t t_cost = user_found ? rec.algo_t_cost
                                 : USER_ARGON2ID_T_COST;
    uint32_t m_cost = user_found ? rec.algo_m_cost
                                 : USER_ARGON2ID_M_COST;
    int auth_ok = 0;
    if (user_found) {
        int verify_rc = user_password_hash_verify(
            (const uint8_t *)password, auth_cstring_length(password), kdf_salt,
            USER_SALT_SIZE, algo_id, t_cost, m_cost, rec.hash,
            USER_HASH_SIZE);
        auth_ok = (verify_rc == 0) ? 1 : 0;
    } else {
        /*
         * Run the KDF for its timing side-effect only; the derived
         * hash is discarded immediately because there is no stored
         * hash to compare against for an unknown user.
         */
        uint8_t derived[USER_HASH_SIZE];
        (void)user_password_hash_derive((const uint8_t *)password,
                                        auth_cstring_length(password), kdf_salt,
                                        USER_SALT_SIZE, algo_id, t_cost,
                                        m_cost, derived, USER_HASH_SIZE);
        auth_memory_zero(derived, sizeof(derived));
    }
    if (auth_ok && out) {
        *out = rec;
    }
    /*
     * Implicit re-hash on successful authentication of a legacy
     * record. When the verify call above succeeded and the record on
     * disk still uses the PBKDF2-SHA256 algorithm from before
     * alpha.219, rewrite the record with a fresh Argon2id derivation
     * using the just-verified password. This progressively migrates
     * every active account to the memory-hard primitive without ever
     * asking the user to change their password, and it self-heals
     * the timing-leak documented in alpha.219 — every successful
     * login of a legacy account converts it, so the population of
     * "still PBKDF2" records monotonically shrinks toward zero as
     * users authenticate. Failures here are intentionally silent:
     * the user has already been successfully authenticated and must
     * not be denied access just because the rewrite hit an
     * allocation or filesystem error. The record stays on PBKDF2
     * until the next successful login retries the migration.
     *
     * The rewrite runs AFTER the authoritative result has been
     * latched into `*out`, so a partial rewrite (followed by a
     * crash) cannot leave a caller with an inconsistent view of the
     * authenticated identity. It runs BEFORE the local `rec`
     * structure is wiped, but since the rewrite reads `/etc/users.db`
     * fresh and rebuilds the line from scratch, the wipe order does
     * not matter for correctness.
     */
    if (auth_ok && user_found &&
        rec.algo_id != USER_PASSWORD_ALGO_ARGON2ID) {
        (void)userdb_replace_password_hash(username, password);
    }
    auth_memory_zero(&rec, sizeof(rec));
    return auth_ok ? 0 : -1;
}

int userdb_authenticate_with_policy(const char *username, const char *password,
                                    struct user_record *out) {
    if (!username || !password) {
        return USERDB_AUTH_FAILED;
    }
    /*
     * SECURITY: timing-equalize the locked path with the not-locked path.
     *
     * Before this slice, `auth_policy_check_allowed` was queried first and
     * the function returned `USERDB_AUTH_LOCKED` IMMEDIATELY if the
     * account was in lockout — no PBKDF2 was executed, no FS access was
     * performed. That made the locked path complete in microseconds
     * while the unlocked path always paid the ~50 ms PBKDF2 cost (the
     * dummy-salt fallback from `userdb_authenticate` keeps the unlocked
     * cost the same for existent and non-existent users since
     * alpha.206). A remote attacker observing response latency could
     * therefore distinguish locked accounts from not-locked accounts
     * with a single login probe per username, leaking exactly the
     * information that alpha.211 removed from `auth_policy_status`
     * (which accounts are currently in lockout) and the information
     * that alpha.206 removed from `userdb_authenticate` (account
     * existence). Worse, the leak survived the privacy hardening of
     * `auth_policy_status` because it does not rely on any log or
     * privileged command — purely on the wall-clock response of the
     * standard login API.
     *
     * Mitigation: always run `userdb_authenticate` regardless of the
     * lockout state. `userdb_authenticate` itself already equalises
     * timing between existent and non-existent users via
     * `k_userdb_dummy_salt`. When the account is locked, we still pay
     * the PBKDF2 cost, discard the auth result, wipe `out` (so a
     * coincident correct password cannot leak the record while locked),
     * and return `USERDB_AUTH_LOCKED`. The locked path now costs the
     * same as the unlocked path, eliminating the side-channel.
     *
     * As a side effect, this also rate-limits credential-stuffing
     * attempts against locked accounts at the same ~50 ms per probe as
     * unlocked accounts — the attacker can no longer enumerate locked
     * targets at microsecond speed to plan a wait-for-unlock attack.
     *
     * `auth_policy_record_failure` is intentionally NOT called when the
     * account is already locked: incrementing `failed_count` further
     * does not extend the lockout window (which is anchored to the
     * tick when the threshold was first crossed) but would pollute the
     * counter with attacker noise that obscures legitimate retry
     * patterns.
     */
    int allowed = auth_policy_check_allowed(username);
    int rc = userdb_authenticate(username, password, out);
    if (!allowed) {
        if (out) {
            user_record_clear(out);
        }
        return USERDB_AUTH_LOCKED;
    }
    if (rc == 0) {
        auth_policy_record_success(username);
        return USERDB_AUTH_OK;
    }
    auth_policy_record_failure(username);
    return USERDB_AUTH_FAILED;
}

static int userdb_replace_password_hash(const char *username,
                                        const char *new_password) {
    /*
     * Internal rewrite primitive. Both the public, policy-gated
     * `userdb_set_password` and the implicit re-hash path in
     * `userdb_authenticate` call here after they have decided that
     * the rewrite is appropriate. The function itself does not
     * validate the password policy: the policy check is the only
     * meaningful difference between the two call sites, and inlining
     * it here would either (a) re-validate a just-authenticated
     * password during implicit re-hash (which could lock the user
     * out of their own account if the policy tightened) or (b)
     * silently skip the policy check on `userdb_set_password` (which
     * would weaken the public API contract). Both inputs are
     * assumed non-NULL non-empty by callers and re-checked here as a
     * defensive belt-and-braces.
     */
    if (!username || !new_password || username[0] == '\0' || new_password[0] == '\0') {
        return -1;
    }
    size_t source_len = 0;
    char *source = userdb_read_all(&source_len);
    if (!source) {
        return -1;
    }

    size_t out_cap = source_len + 512;
    char *out = (char *)kalloc(out_cap);
    if (!out) {
        /* `source` carries every user's salt_hex/hash_hex in plain text;
         * wipe before returning so the freed heap region does not retain
         * credential material until the next allocation reuses it. */
        auth_memory_zero(source, source_len);
        kfree(source);
        return -1;
    }
    size_t out_len = 0;
    int updated = 0;

    size_t line_start = 0;
    for (size_t i = 0; i <= source_len; ++i) {
        if (i == source_len || source[i] == '\n') {
            size_t line_len = i - line_start;
            if (line_len > 0) {
                struct user_record rec;
                if (userdb_parse_line(&source[line_start], line_len, &rec) == 0) {
                    if (auth_strings_equal(rec.username, username)) {
                        /*
                         * Password change (or implicit re-hash) is
                         * the natural upgrade point for legacy
                         * PBKDF2 records: every new salt gets paired
                         * with a new Argon2id derivation, so once the
                         * user authenticates or changes their password
                         * the record graduates to the memory-hard
                         * primitive with no extra migration step.
                         * Existing PBKDF2 records that are never
                         * accessed keep working thanks to the
                         * dispatch layer in `user_password_hash.c`.
                         */
                        rec.algo_id = USER_PASSWORD_ALGO_ARGON2ID;
                        rec.algo_t_cost = USER_ARGON2ID_T_COST;
                        rec.algo_m_cost = USER_ARGON2ID_M_COST;
                        auth_generate_salt(rec.salt, USER_SALT_SIZE);
                        if (user_password_hash_derive(
                                (const uint8_t *)new_password,
                                auth_cstring_length(new_password), rec.salt,
                                USER_SALT_SIZE, rec.algo_id, rec.algo_t_cost,
                                rec.algo_m_cost, rec.hash,
                                USER_HASH_SIZE) != 0) {
                            /* Allocation failure mid-flight: wipe every
                             * buffer that carries credential material
                             * before aborting the transaction. */
                            auth_memory_zero(&rec, sizeof(rec));
                            auth_memory_zero(out, out_cap);
                            auth_memory_zero(source, source_len);
                            kfree(out);
                            kfree(source);
                            return -1;
                        }
                        updated = 1;
                    }
                    /* Match the headroom calculation in `userdb_add`. */
                    char line[USER_NAME_MAX + USER_HOME_MAX + USER_ROLE_MAX +
                              USER_SALT_SIZE * 2 + USER_HASH_SIZE * 2 + 128];
                    size_t len = 0;
                    if (userdb_serialize_line(&rec, line, sizeof(line), &len) != 0 ||
                        out_len + len >= out_cap) {
                        /* Failure path: wipe every buffer that may carry
                         * credential material — the partially built `out`
                         * already has previous users' records, `line` has
                         * the current record's hex, `rec` has the
                         * derived hash, and `source` has the full DB. */
                        auth_memory_zero(line, sizeof(line));
                        auth_memory_zero(&rec, sizeof(rec));
                        auth_memory_zero(out, out_cap);
                        auth_memory_zero(source, source_len);
                        kfree(out);
                        kfree(source);
                        return -1;
                    }
                    for (size_t k = 0; k < len; ++k) {
                        out[out_len++] = line[k];
                    }
                    /* Wipe scratch state once the line is copied into
                     * `out`. `line` has the serialized salt/hash hex and
                     * `rec` has the raw derived hash for this iteration. */
                    auth_memory_zero(line, sizeof(line));
                    auth_memory_zero(&rec, sizeof(rec));
                }
            }
            line_start = i + 1;
        }
    }

    if (!updated) {
        auth_memory_zero(out, out_cap);
        auth_memory_zero(source, source_len);
        kfree(out);
        kfree(source);
        return -1;
    }

    int rc = userdb_write_blob(out, out_len);
    /* Both `out` (canonical serialized DB just written to disk) and
     * `source` (previous serialized DB) carry every user's hash/salt;
     * wipe both before releasing so the heap region cannot leak. */
    auth_memory_zero(out, out_cap);
    auth_memory_zero(source, source_len);
    kfree(out);
    kfree(source);
    return rc;
}

int userdb_set_password(const char *username, const char *new_password) {
    if (!username || !new_password || username[0] == '\0' || new_password[0] == '\0') {
        return -1;
    }
    if (auth_policy_validate_password(new_password, NULL) != 0) {
        return -1;
    }
    return userdb_replace_password_hash(username, new_password);
}
