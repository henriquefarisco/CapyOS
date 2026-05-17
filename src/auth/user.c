/*
 * src/auth/user.c
 *
 * Owner of the `struct user_record` lifecycle: zeroing a record and
 * initialising a freshly-created one from raw inputs. Every other
 * concern that used to live here in the pre-split monolith now sits
 * in dedicated sister translation units:
 *
 *   - reusable helpers (cstring, hex, memory-zero, salt, parse_u32):
 *       src/auth/user_helpers.c
 *       src/auth/internal/user_helpers.h
 *
 *   - userdb storage layer (read/write/iterate/serialize/parse,
 *     public queries, userdb_add):
 *       src/auth/userdb_io.c
 *       src/auth/internal/userdb_io.h
 *
 *   - userdb credential layer (authenticate, lockout policy,
 *     password rewrite, implicit re-hash):
 *       src/auth/userdb_auth.c
 *
 * The split was driven by the 900-line layout limit documented in
 * `docs/architecture/source-layout.md`. Functional behaviour is
 * preserved; the helpers used to be `static` and are now `extern`
 * with an `auth_` prefix declared only inside the internal headers.
 */
#include "auth/user.h"
#include "auth/auth_policy.h"
#include "auth/user_password_hash.h"

#include "auth/internal/user_helpers.h"

void user_record_clear(struct user_record *rec) {
    if (!rec) {
        return;
    }
    auth_memory_zero(rec, sizeof(*rec));
}

int user_record_init(const char *username,
                     const char *password,
                     const char *role,
                     uint32_t uid,
                     uint32_t gid,
                     const char *home,
                     struct user_record *out) {
    if (!username || !password || !role || !home || !out) {
        return -1;
    }
    if (auth_cstring_length(username) >= USER_NAME_MAX) {
        return -1;
    }
    if (auth_cstring_length(role) >= USER_ROLE_MAX) {
        return -1;
    }
    if (auth_cstring_length(home) >= USER_HOME_MAX) {
        return -1;
    }
    if (auth_policy_validate_password(password, NULL) != 0) {
        return -1;
    }

    user_record_clear(out);
    auth_cstring_copy(out->username, USER_NAME_MAX, username);
    auth_cstring_copy(out->role, USER_ROLE_MAX, role);
    auth_cstring_copy(out->home, USER_HOME_MAX, home);
    out->uid = uid;
    out->gid = gid;

    /*
     * Newly-created records always use Argon2id with the default
     * `USER_ARGON2ID_T_COST`/`USER_ARGON2ID_M_COST` parameters from
     * `include/auth/user.h`. Existing PBKDF2 records on disk are still
     * accepted by `userdb_authenticate` for backward compatibility, but
     * `user_record_init` itself never produces a PBKDF2 record any
     * longer — the system administrator who runs `add-user` after
     * alpha.219 always lands on the modern memory-hard primitive.
     */
    out->algo_id = USER_PASSWORD_ALGO_ARGON2ID;
    out->algo_t_cost = USER_ARGON2ID_T_COST;
    out->algo_m_cost = USER_ARGON2ID_M_COST;
    auth_generate_salt(out->salt, USER_SALT_SIZE);
    if (user_password_hash_derive((const uint8_t *)password,
                                  auth_cstring_length(password), out->salt,
                                  USER_SALT_SIZE, out->algo_id,
                                  out->algo_t_cost, out->algo_m_cost,
                                  out->hash, USER_HASH_SIZE) != 0) {
        /*
         * Fail-closed: an allocation failure or invalid parameter
         * leaves no half-initialised record on the caller's stack.
         */
        user_record_clear(out);
        return -1;
    }
    return 0;
}
