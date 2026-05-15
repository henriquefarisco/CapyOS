#ifndef USER_H
#define USER_H

#include <stddef.h>
#include <stdint.h>

#define USER_NAME_MAX 32
#define USER_HOME_MAX 96
#define USER_ROLE_MAX 16
#define USER_SALT_SIZE 16
#define USER_HASH_SIZE 32
#define USER_ITERATIONS 64000
#define USER_UID_FIRST_REGULAR 1000u
#define USER_GID_FIRST_REGULAR 1000u

#define USER_DB_PATH "/etc/users.db"

/*
 * Password hashing algorithm identifiers stored in `user_record::algo_id`.
 *
 * `USER_PASSWORD_ALGO_PBKDF2_SHA256` is the legacy algorithm used before
 * alpha.219. Existing `/etc/users.db` records serialized as 7 fields
 * (username:uid:gid:home:salt_hex:hash_hex:role) deserialize with this id
 * for full backward compatibility — no DB migration is required.
 *
 * `USER_PASSWORD_ALGO_ARGON2ID` is the default since alpha.219 for all
 * newly-created records and re-hashed records (after a password change
 * via `userdb_set_password`). Records using this id serialize the
 * algorithm identifier explicitly with its t_cost/m_cost parameters
 * appended as three additional colon-separated fields:
 *
 *   username:uid:gid:home:salt_hex:hash_hex:role:argon2id:t_cost:m_cost
 *
 * The parser accepts either 7 fields (legacy PBKDF2) or 10 fields
 * (Argon2id). Mixed databases are supported during the transition.
 *
 * See `include/auth/user_password_hash.h` for the dispatch API used by
 * `userdb_authenticate`, `user_record_init` and `userdb_set_password`.
 */
#define USER_PASSWORD_ALGO_PBKDF2_SHA256  0u
#define USER_PASSWORD_ALGO_ARGON2ID       1u

/*
 * Default Argon2id parameters for `userdb` records.
 *
 * t_cost = 3, m_cost = 8192 KiB (= 8 MiB), parallelism = 1.
 *
 * RFC 9106 §4 lists this configuration as the high-security tier when
 * memory is constrained. OWASP 2024 recommends 19 MiB for server-class
 * deployments and >= 8 MiB as the absolute minimum for any device that
 * must hash passwords locally. CapyOS runs on systems with a 16 MiB
 * kernel heap, so 8 MiB per auth attempt is the largest defensible
 * value here.
 *
 * Out length = 32 bytes (matches USER_HASH_SIZE) so the schema does not
 * change between algorithms — only the way the bytes were derived.
 */
#define USER_ARGON2ID_T_COST  3u
#define USER_ARGON2ID_M_COST  8192u

struct user_record {
    char username[USER_NAME_MAX];
    uint32_t uid;
    uint32_t gid;
    char home[USER_HOME_MAX];
    uint8_t salt[USER_SALT_SIZE];
    uint8_t hash[USER_HASH_SIZE];
    char role[USER_ROLE_MAX];
    /*
     * Password hashing algorithm marker and its parameters.
     *
     * Appended at the end of the struct in alpha.219; existing consumers
     * that only touch username/uid/gid/home/role compile unchanged because
     * the leading layout is preserved. Records parsed from a legacy
     * 7-field line set `algo_id = USER_PASSWORD_ALGO_PBKDF2_SHA256` and
     * leave `algo_t_cost`/`algo_m_cost` at zero — the dispatch layer
     * substitutes the canonical PBKDF2 iteration count
     * (`USER_ITERATIONS`) at hash time.
     */
    uint8_t algo_id;
    uint32_t algo_t_cost;
    uint32_t algo_m_cost;
};

void user_record_clear(struct user_record *rec);
int user_record_init(const char *username,
                     const char *password,
                     const char *role,
                     uint32_t uid,
                     uint32_t gid,
                     const char *home,
                     struct user_record *out);

int userdb_ensure(void);
int userdb_find(const char *username, struct user_record *out);
int userdb_add(const struct user_record *user);
int userdb_authenticate(const char *username, const char *password, struct user_record *out);
int userdb_set_password(const char *username, const char *new_password);
int userdb_next_ids(uint32_t *out_uid, uint32_t *out_gid);
int userdb_has_any_user(void);

/* Result codes for `userdb_authenticate_with_policy`. The function composes
 * `auth_policy_check_allowed` (lockout enforcement) with
 * `userdb_authenticate` (timing-equalized credential check) and records the
 * outcome via `auth_policy_record_success` / `auth_policy_record_failure`,
 * giving callers a single safe entry point for interactive login flows. */
#define USERDB_AUTH_OK        0
#define USERDB_AUTH_FAILED   -1   /* bad credentials, unknown user, or invalid args */
#define USERDB_AUTH_LOCKED   -2   /* account locked by auth_policy */

int userdb_authenticate_with_policy(const char *username,
                                    const char *password,
                                    struct user_record *out);

#endif
