#include "auth/user.h"
#include "auth/auth_policy.h"
#include "auth/user_password_hash.h"
#include "security/argon2.h"
#include "security/csprng.h"

#include <stdint.h>

#include "security/crypt.h"
#include "memory/kmem.h"
#include "fs/vfs.h"

struct user_record;

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

static size_t cstring_length(const char *s) {
    size_t len = 0;
    if (!s) {
        return 0;
    }
    while (s[len]) {
        ++len;
    }
    return len;
}

/* Defensive zeroing helper used for credential buffers (raw passwords,
 * salts, derived hashes, parsed user records, /etc/passwd snapshots).
 * The pointer is marked volatile so the compiler cannot eliminate the
 * stores as dead even when the destination buffer is freed or leaves
 * scope immediately after — that dead-store elimination is the classic
 * way "secure wipe" code gets silently optimised away in C. */
static void memory_zero(void *dst, size_t len) {
    volatile uint8_t *p = (volatile uint8_t *)dst;
    while (len--) {
        *p++ = 0;
    }
}

static void cstring_copy(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) {
        return;
    }
    size_t i = 0;
    if (src) {
        while (src[i] && i < dst_size - 1) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = '\0';
}

static void cstring_copy_n(char *dst, size_t dst_size, const char *src, size_t src_len) {
    if (!dst || dst_size == 0) {
        return;
    }
    size_t to_copy = 0;
    if (src && src_len > 0) {
        to_copy = (src_len < dst_size - 1) ? src_len : (dst_size - 1);
        for (size_t i = 0; i < to_copy; ++i) {
            dst[i] = src[i];
        }
    }
    dst[to_copy] = '\0';
}

static int strings_equal(const char *a, const char *b) {
    if (!a || !b) {
        return 0;
    }
    size_t ia = 0;
    while (a[ia] && b[ia]) {
        if (a[ia] != b[ia]) {
            return 0;
        }
        ++ia;
    }
    return a[ia] == b[ia];
}

static void bytes_to_hex(const uint8_t *src, size_t len, char *dst) {
    static const char hex_digits[] = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i) {
        uint8_t v = src[i];
        dst[i * 2] = hex_digits[(v >> 4) & 0x0F];
        dst[i * 2 + 1] = hex_digits[v & 0x0F];
    }
    dst[len * 2] = '\0';
}

static int hex_to_bytes(const char *hex, size_t hex_len, uint8_t *dst, size_t dst_len) {
    if (!hex || !dst || hex_len != dst_len * 2) {
        return -1;
    }
    for (size_t i = 0; i < dst_len; ++i) {
        char h = hex[i * 2];
        char l = hex[i * 2 + 1];
        uint8_t hv;
        uint8_t lv;
        if (h >= '0' && h <= '9') hv = h - '0';
        else if (h >= 'a' && h <= 'f') hv = (uint8_t)(10 + h - 'a');
        else if (h >= 'A' && h <= 'F') hv = (uint8_t)(10 + h - 'A');
        else return -1;

        if (l >= '0' && l <= '9') lv = l - '0';
        else if (l >= 'a' && l <= 'f') lv = (uint8_t)(10 + l - 'a');
        else if (l >= 'A' && l <= 'F') lv = (uint8_t)(10 + l - 'A');
        else return -1;

        dst[i] = (hv << 4) | lv;
    }
    return 0;
}

static uint32_t parse_u32(const char *str, size_t len) {
    uint32_t value = 0;
    for (size_t i = 0; i < len; ++i) {
        char c = str[i];
        if (c < '0' || c > '9') {
            return value;
        }
        value = value * 10u + (uint32_t)(c - '0');
    }
    return value;
}

static void u32_to_string(uint32_t value, char *buf, size_t buf_len) {
    if (!buf || buf_len == 0) {
        return;
    }
    char tmp[10];
    size_t len = 0;
    if (value == 0) {
        if (buf_len >= 2) {
            buf[0] = '0';
            buf[1] = '\0';
        } else {
            buf[0] = '\0';
        }
        return;
    }
    while (value && len < sizeof(tmp)) {
        tmp[len++] = (char)('0' + (value % 10));
        value /= 10;
    }
    size_t pos = 0;
    while (len && pos + 1 < buf_len) {
        buf[pos++] = tmp[--len];
    }
    buf[pos] = '\0';
}

static void generate_salt(uint8_t *salt, size_t len) {
    csprng_get_bytes(salt, len);
}

void user_record_clear(struct user_record *rec) {
    if (!rec) {
        return;
    }
    memory_zero(rec, sizeof(*rec));
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
    if (cstring_length(username) >= USER_NAME_MAX) {
        return -1;
    }
    if (cstring_length(role) >= USER_ROLE_MAX) {
        return -1;
    }
    if (cstring_length(home) >= USER_HOME_MAX) {
        return -1;
    }
    if (auth_policy_validate_password(password, NULL) != 0) {
        return -1;
    }

    user_record_clear(out);
    cstring_copy(out->username, USER_NAME_MAX, username);
    cstring_copy(out->role, USER_ROLE_MAX, role);
    cstring_copy(out->home, USER_HOME_MAX, home);
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
    generate_salt(out->salt, USER_SALT_SIZE);
    if (user_password_hash_derive((const uint8_t *)password,
                                  cstring_length(password), out->salt,
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

static int ensure_file(const char *path) {
    struct dentry *d = NULL;
    if (vfs_lookup(path, &d) == 0) {
        if (d && d->refcount) {
            d->refcount--;
        }
        return 0;
    }
    if (vfs_create(path, VFS_MODE_FILE, NULL) != 0) {
        if (vfs_lookup(path, &d) != 0) {
            return -1;
        }
        if (d && d->refcount) {
            d->refcount--;
        }
        return 0;
    }
    return 0;
}

int userdb_ensure(void) {
    return ensure_file(USER_DB_PATH);
}

static char *userdb_read_all(size_t *out_len) {
    struct file *f = vfs_open(USER_DB_PATH, VFS_OPEN_READ);
    if (!f) {
        return NULL;
    }
    size_t size = 0;
    if (f->dentry && f->dentry->inode) {
        size = f->dentry->inode->size;
    }
    char *buffer = (char *)kalloc(size + 1);
    if (!buffer) {
        vfs_close(f);
        return NULL;
    }
    long read = 0;
    if (size > 0) {
        read = vfs_read(f, buffer, size);
        if (read < 0) {
            kfree(buffer);
            vfs_close(f);
            return NULL;
        }
    }
    buffer[read] = '\0';
    if (out_len) {
        *out_len = (size_t)read;
    }
    vfs_close(f);
    return buffer;
}

static int parse_user_line(const char *line, size_t len, struct user_record *out) {
    size_t field_start = 0;
    int field_index = 0;
    struct user_record rec;
    user_record_clear(&rec);
    /*
     * Defaults applied before parsing so that legacy 7-field lines
     * (without the algorithm trailer) deserialize as a fully-formed
     * PBKDF2 record. The dispatch layer in
     * `src/auth/user_password_hash.c` interprets `algo_t_cost == 0`
     * for the PBKDF2 algorithm as "use the canonical
     * `USER_ITERATIONS` baked into the historical schema", so the
     * zero values here express the legacy intent explicitly.
     */
    rec.algo_id = USER_PASSWORD_ALGO_PBKDF2_SHA256;
    rec.algo_t_cost = 0u;
    rec.algo_m_cost = 0u;
    int saw_algo = 0;

    for (size_t i = 0; i <= len; ++i) {
        if (i == len || line[i] == ':') {
            size_t field_len = i - field_start;
            const char *field = &line[field_start];
            switch (field_index) {
                case 0:
                    if (field_len == 0 || field_len >= USER_NAME_MAX) {
                        return -1;
                    }
                    cstring_copy_n(rec.username, USER_NAME_MAX, field, field_len);
                    break;
                case 1:
                    rec.uid = parse_u32(field, field_len);
                    break;
                case 2:
                    rec.gid = parse_u32(field, field_len);
                    break;
                case 3:
                    if (field_len >= USER_HOME_MAX) {
                        return -1;
                    }
                    cstring_copy_n(rec.home, USER_HOME_MAX, field, field_len);
                    break;
                case 4:
                    if (field_len != USER_SALT_SIZE * 2) {
                        return -1;
                    }
                    if (hex_to_bytes(field, field_len, rec.salt, USER_SALT_SIZE) != 0) {
                        return -1;
                    }
                    break;
                case 5:
                    if (field_len != USER_HASH_SIZE * 2) {
                        return -1;
                    }
                    if (hex_to_bytes(field, field_len, rec.hash, USER_HASH_SIZE) != 0) {
                        return -1;
                    }
                    break;
                case 6:
                    if (field_len >= USER_ROLE_MAX) {
                        return -1;
                    }
                    cstring_copy_n(rec.role, USER_ROLE_MAX, field, field_len);
                    break;
                case 7: {
                    /*
                     * Optional algorithm identifier. An empty field or
                     * an unknown token rejects the entire line
                     * fail-closed — accepting an unknown algorithm
                     * could otherwise silently downgrade the record to
                     * the default PBKDF2 path and accept a hash that
                     * was never produced by PBKDF2 at all.
                     */
                    uint8_t parsed_algo = USER_PASSWORD_ALGO_PBKDF2_SHA256;
                    if (field_len == 0) {
                        return -1;
                    }
                    if (user_password_hash_algo_from_string(field, field_len,
                                                            &parsed_algo) != 0) {
                        return -1;
                    }
                    rec.algo_id = parsed_algo;
                    saw_algo = 1;
                    break;
                }
                case 8: {
                    if (field_len == 0) {
                        return -1;
                    }
                    rec.algo_t_cost = parse_u32(field, field_len);
                    break;
                }
                case 9: {
                    if (field_len == 0) {
                        return -1;
                    }
                    rec.algo_m_cost = parse_u32(field, field_len);
                    break;
                }
                default:
                    break;
            }
            field_start = i + 1;
            field_index++;
        }
    }

    if (field_index < 7) {
        return -1;
    }
    if (saw_algo) {
        /*
         * When the algorithm trailer is present, all three optional
         * fields are mandatory and must be valid for the selected
         * algorithm. Argon2id rejects t_cost == 0 / m_cost < 8 below.
         */
        if (field_index < 10) {
            return -1;
        }
        if (rec.algo_id == USER_PASSWORD_ALGO_ARGON2ID) {
            if (rec.algo_t_cost < ARGON2_MIN_T_COST ||
                rec.algo_m_cost < ARGON2_MIN_M_COST) {
                return -1;
            }
        }
    }
    if (out) {
        *out = rec;
    }
    return 0;
}

static int iterate_users(int (*callback)(const struct user_record *, void *), void *userdata) {
    size_t len = 0;
    char *data = userdb_read_all(&len);
    if (!data) {
        return -1;
    }
    size_t line_start = 0;
    for (size_t i = 0; i <= len; ++i) {
        if (i == len || data[i] == '\n') {
            size_t line_len = i - line_start;
            if (line_len > 0) {
                struct user_record rec;
                if (parse_user_line(&data[line_start], line_len, &rec) == 0) {
                    if (callback(&rec, userdata) != 0) {
                        kfree(data);
                        return 1;
                    }
                }
            }
            line_start = i + 1;
        }
    }
    kfree(data);
    return 0;
}

struct find_ctx {
    const char *username;
    struct user_record *out;
    int found;
};

static int find_callback(const struct user_record *rec, void *userdata) {
    struct find_ctx *ctx = (struct find_ctx *)userdata;
    if (strings_equal(rec->username, ctx->username)) {
        if (ctx->out) {
            *(ctx->out) = *rec;
        }
        ctx->found = 1;
        return 1;
    }
    return 0;
}

int userdb_find(const char *username, struct user_record *out) {
    if (!username) {
        return -1;
    }
    struct find_ctx ctx;
    ctx.username = username;
    ctx.out = out;
    ctx.found = 0;
    int res = iterate_users(find_callback, &ctx);
    if (res < 0) {
        return -1;
    }
    return ctx.found ? 0 : -1;
}

struct next_ids_ctx {
    uint32_t max_uid;
    uint32_t max_gid;
};

static int next_ids_callback(const struct user_record *rec, void *userdata) {
    struct next_ids_ctx *ctx = (struct next_ids_ctx *)userdata;
    if (rec->uid > ctx->max_uid) {
        ctx->max_uid = rec->uid;
    }
    if (rec->gid > ctx->max_gid) {
        ctx->max_gid = rec->gid;
    }
    return 0;
}

int userdb_next_ids(uint32_t *out_uid, uint32_t *out_gid) {
    struct next_ids_ctx ctx;
    if (userdb_ensure() != 0) {
        return -1;
    }
    ctx.max_uid = USER_UID_FIRST_REGULAR - 1u;
    ctx.max_gid = USER_GID_FIRST_REGULAR - 1u;
    int res = iterate_users(next_ids_callback, &ctx);
    if (res < 0) {
        return -1;
    }
    if (out_uid) {
        *out_uid = ctx.max_uid + 1;
    }
    if (out_gid) {
        *out_gid = ctx.max_gid + 1;
    }
    return 0;
}

static int has_any_callback(const struct user_record *rec, void *userdata) {
    int *found = (int *)userdata;
    (void)rec;
    *found = 1;
    return 1;
}

int userdb_has_any_user(void) {
    int found = 0;
    int res = iterate_users(has_any_callback, &found);
    if (res < 0) {
        return -1;
    }
    return found;
}

static int append_piece(char *dst, size_t cap, size_t *idx, const char *src) {
    if (!dst || !idx || !src || *idx >= cap) {
        return -1;
    }
    size_t plen = cstring_length(src);
    if (*idx + plen >= cap) {
        return -1;
    }
    for (size_t i = 0; i < plen; ++i) {
        dst[(*idx)++] = src[i];
    }
    return 0;
}

static int serialize_user_record_line(const struct user_record *user,
                                      char *line,
                                      size_t line_cap,
                                      size_t *out_len) {
    if (!user || !line || line_cap == 0) {
        return -1;
    }
    char uid_buf[12];
    char gid_buf[12];
    char salt_hex[USER_SALT_SIZE * 2 + 1];
    char hash_hex[USER_HASH_SIZE * 2 + 1];
    char t_cost_buf[12];
    char m_cost_buf[12];
    u32_to_string(user->uid, uid_buf, sizeof(uid_buf));
    u32_to_string(user->gid, gid_buf, sizeof(gid_buf));
    bytes_to_hex(user->salt, USER_SALT_SIZE, salt_hex);
    bytes_to_hex(user->hash, USER_HASH_SIZE, hash_hex);

    size_t idx = 0;
    /*
     * Legacy PBKDF2 records keep the 7-field on-disk format so that
     * downgrading a CapyOS deployment to a pre-alpha.219 binary does
     * not need a DB migration in the opposite direction either. Only
     * Argon2id records emit the 10-field schema with the algorithm
     * trailer.
     */
    if (user->algo_id == USER_PASSWORD_ALGO_PBKDF2_SHA256) {
        const char *pieces[] = {
            user->username, ":", uid_buf, ":", gid_buf, ":", user->home, ":",
            salt_hex,       ":", hash_hex, ":", user->role, "\n"};
        for (size_t i = 0; i < sizeof(pieces) / sizeof(pieces[0]); ++i) {
            if (append_piece(line, line_cap, &idx, pieces[i]) != 0) {
                return -1;
            }
        }
    } else {
        const char *algo_name = user_password_hash_algo_to_string(user->algo_id);
        if (!algo_name) {
            return -1;
        }
        u32_to_string(user->algo_t_cost, t_cost_buf, sizeof(t_cost_buf));
        u32_to_string(user->algo_m_cost, m_cost_buf, sizeof(m_cost_buf));
        const char *pieces[] = {
            user->username, ":", uid_buf, ":", gid_buf, ":", user->home, ":",
            salt_hex,       ":", hash_hex, ":", user->role, ":",
            algo_name,      ":", t_cost_buf, ":", m_cost_buf, "\n"};
        for (size_t i = 0; i < sizeof(pieces) / sizeof(pieces[0]); ++i) {
            if (append_piece(line, line_cap, &idx, pieces[i]) != 0) {
                return -1;
            }
        }
    }
    line[idx] = '\0';
    if (out_len) {
        *out_len = idx;
    }
    return 0;
}

static int userdb_write_blob(const char *data, size_t len) {
    if (!data) {
        return -1;
    }
    (void)vfs_unlink(USER_DB_PATH);
    if (userdb_ensure() != 0) {
        return -1;
    }
    struct file *f = vfs_open(USER_DB_PATH, VFS_OPEN_WRITE);
    if (!f) {
        return -1;
    }
    if (f->dentry && f->dentry->inode) {
        f->position = 0;
    }
    long written = 0;
    if (len > 0) {
        written = vfs_write(f, data, len);
    }
    vfs_close(f);
    return (len == 0 || written == (long)len) ? 0 : -1;
}

int userdb_add(const struct user_record *user) {
    if (!user) {
        return -1;
    }
    if (userdb_ensure() != 0) {
        return -1;
    }
    if (userdb_find(user->username, NULL) == 0) {
        return -1;
    }
    struct file *f = vfs_open(USER_DB_PATH, VFS_OPEN_WRITE);
    if (!f) {
        return -1;
    }
    if (f->dentry && f->dentry->inode) {
        f->position = f->dentry->inode->size;
    }
    /* Reserve enough trailing space for the Argon2id schema bump:
     * `:algo_name:t_cost:m_cost`. The Argon2id token alone is 8 bytes,
     * t_cost/m_cost decimal expansions are up to 10 bytes each, plus 3
     * separators — 64 extra bytes covers the worst case and leaves a
     * safety margin against future schema additions. */
    char line[USER_NAME_MAX + USER_HOME_MAX + USER_ROLE_MAX + USER_SALT_SIZE * 2 +
              USER_HASH_SIZE * 2 + 128];
    size_t line_len = 0;
    if (serialize_user_record_line(user, line, sizeof(line), &line_len) != 0) {
        vfs_close(f);
        return -1;
    }
    long written = vfs_write(f, line, line_len);
    vfs_close(f);
    return (written == (long)line_len) ? 0 : -1;
}

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
            (const uint8_t *)password, cstring_length(password), kdf_salt,
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
                                        cstring_length(password), kdf_salt,
                                        USER_SALT_SIZE, algo_id, t_cost,
                                        m_cost, derived, USER_HASH_SIZE);
        memory_zero(derived, sizeof(derived));
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
    memory_zero(&rec, sizeof(rec));
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
        memory_zero(source, source_len);
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
                if (parse_user_line(&source[line_start], line_len, &rec) == 0) {
                    if (strings_equal(rec.username, username)) {
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
                        generate_salt(rec.salt, USER_SALT_SIZE);
                        if (user_password_hash_derive(
                                (const uint8_t *)new_password,
                                cstring_length(new_password), rec.salt,
                                USER_SALT_SIZE, rec.algo_id, rec.algo_t_cost,
                                rec.algo_m_cost, rec.hash,
                                USER_HASH_SIZE) != 0) {
                            /* Allocation failure mid-flight: wipe every
                             * buffer that carries credential material
                             * before aborting the transaction. */
                            memory_zero(&rec, sizeof(rec));
                            memory_zero(out, out_cap);
                            memory_zero(source, source_len);
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
                    if (serialize_user_record_line(&rec, line, sizeof(line), &len) != 0 ||
                        out_len + len >= out_cap) {
                        /* Failure path: wipe every buffer that may carry
                         * credential material — the partially built `out`
                         * already has previous users' records, `line` has
                         * the current record's hex, `rec` has the
                         * derived hash, and `source` has the full DB. */
                        memory_zero(line, sizeof(line));
                        memory_zero(&rec, sizeof(rec));
                        memory_zero(out, out_cap);
                        memory_zero(source, source_len);
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
                    memory_zero(line, sizeof(line));
                    memory_zero(&rec, sizeof(rec));
                }
            }
            line_start = i + 1;
        }
    }

    if (!updated) {
        memory_zero(out, out_cap);
        memory_zero(source, source_len);
        kfree(out);
        kfree(source);
        return -1;
    }

    int rc = userdb_write_blob(out, out_len);
    /* Both `out` (canonical serialized DB just written to disk) and
     * `source` (previous serialized DB) carry every user's hash/salt;
     * wipe both before releasing so the heap region cannot leak. */
    memory_zero(out, out_cap);
    memory_zero(source, source_len);
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
