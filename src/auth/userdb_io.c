/*
 * src/auth/userdb_io.c
 *
 * Storage and query layer for /etc/users.db. Owns the on-disk format
 * (legacy 7-field PBKDF2 + 10-field Argon2id), the read/write
 * primitives that map it to and from the VFS, and the public query
 * helpers (`userdb_find`, `userdb_next_ids`, `userdb_has_any_user`)
 * that walk the file without touching credentials.
 *
 * The translation unit was carved out of `src/auth/user.c` when the
 * monolith reached the 900-line limit. Authentication and password
 * mutation logic now lives in `src/auth/userdb_auth.c`; the helpers
 * shared between the two translation units sit in
 * `src/auth/user_helpers.c`.
 */
#include "auth/user.h"
#include "auth/user_password_hash.h"
#include "fs/vfs.h"
#include "memory/kmem.h"
#include "security/argon2.h"

#include "auth/internal/user_helpers.h"
#include "auth/internal/userdb_io.h"

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

char *userdb_read_all(size_t *out_len) {
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

int userdb_parse_line(const char *line, size_t len, struct user_record *out) {
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
                    auth_cstring_copy_n(rec.username, USER_NAME_MAX, field, field_len);
                    break;
                case 1:
                    rec.uid = auth_parse_u32(field, field_len);
                    break;
                case 2:
                    rec.gid = auth_parse_u32(field, field_len);
                    break;
                case 3:
                    if (field_len >= USER_HOME_MAX) {
                        return -1;
                    }
                    auth_cstring_copy_n(rec.home, USER_HOME_MAX, field, field_len);
                    break;
                case 4:
                    if (field_len != USER_SALT_SIZE * 2) {
                        return -1;
                    }
                    if (auth_hex_to_bytes(field, field_len, rec.salt, USER_SALT_SIZE) != 0) {
                        return -1;
                    }
                    break;
                case 5:
                    if (field_len != USER_HASH_SIZE * 2) {
                        return -1;
                    }
                    if (auth_hex_to_bytes(field, field_len, rec.hash, USER_HASH_SIZE) != 0) {
                        return -1;
                    }
                    break;
                case 6:
                    if (field_len >= USER_ROLE_MAX) {
                        return -1;
                    }
                    auth_cstring_copy_n(rec.role, USER_ROLE_MAX, field, field_len);
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
                    rec.algo_t_cost = auth_parse_u32(field, field_len);
                    break;
                }
                case 9: {
                    if (field_len == 0) {
                        return -1;
                    }
                    rec.algo_m_cost = auth_parse_u32(field, field_len);
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

int userdb_iterate(int (*callback)(const struct user_record *, void *),
                   void *userdata) {
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
                if (userdb_parse_line(&data[line_start], line_len, &rec) == 0) {
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
    if (auth_strings_equal(rec->username, ctx->username)) {
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
    int res = userdb_iterate(find_callback, &ctx);
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
    int res = userdb_iterate(next_ids_callback, &ctx);
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
    int res = userdb_iterate(has_any_callback, &found);
    if (res < 0) {
        return -1;
    }
    return found;
}

int userdb_serialize_line(const struct user_record *user, char *line,
                          size_t line_cap, size_t *out_len) {
    if (!user || !line || line_cap == 0) {
        return -1;
    }
    char uid_buf[12];
    char gid_buf[12];
    char salt_hex[USER_SALT_SIZE * 2 + 1];
    char hash_hex[USER_HASH_SIZE * 2 + 1];
    char t_cost_buf[12];
    char m_cost_buf[12];
    auth_u32_to_string(user->uid, uid_buf, sizeof(uid_buf));
    auth_u32_to_string(user->gid, gid_buf, sizeof(gid_buf));
    auth_bytes_to_hex(user->salt, USER_SALT_SIZE, salt_hex);
    auth_bytes_to_hex(user->hash, USER_HASH_SIZE, hash_hex);

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
            if (auth_append_piece(line, line_cap, &idx, pieces[i]) != 0) {
                return -1;
            }
        }
    } else {
        const char *algo_name = user_password_hash_algo_to_string(user->algo_id);
        if (!algo_name) {
            return -1;
        }
        auth_u32_to_string(user->algo_t_cost, t_cost_buf, sizeof(t_cost_buf));
        auth_u32_to_string(user->algo_m_cost, m_cost_buf, sizeof(m_cost_buf));
        const char *pieces[] = {
            user->username, ":", uid_buf, ":", gid_buf, ":", user->home, ":",
            salt_hex,       ":", hash_hex, ":", user->role, ":",
            algo_name,      ":", t_cost_buf, ":", m_cost_buf, "\n"};
        for (size_t i = 0; i < sizeof(pieces) / sizeof(pieces[0]); ++i) {
            if (auth_append_piece(line, line_cap, &idx, pieces[i]) != 0) {
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

int userdb_write_blob(const char *data, size_t len) {
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
    if (userdb_serialize_line(user, line, sizeof(line), &line_len) != 0) {
        vfs_close(f);
        return -1;
    }
    long written = vfs_write(f, line, line_len);
    vfs_close(f);
    return (written == (long)line_len) ? 0 : -1;
}
