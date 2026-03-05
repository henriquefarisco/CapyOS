#include "core/user.h"
#include "security/csprng.h"

#include <stdint.h>

#include "security/crypt.h"
#include "memory/kmem.h"
#include "fs/vfs.h"

struct user_record;

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

static void memory_zero(void *dst, size_t len) {
    uint8_t *p = (uint8_t *)dst;
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

    user_record_clear(out);
    cstring_copy(out->username, USER_NAME_MAX, username);
    cstring_copy(out->role, USER_ROLE_MAX, role);
    cstring_copy(out->home, USER_HOME_MAX, home);
    out->uid = uid;
    out->gid = gid;

    generate_salt(out->salt, USER_SALT_SIZE);
    crypt_pbkdf2_sha256((const uint8_t *)password, cstring_length(password),
                        out->salt, USER_SALT_SIZE,
                        USER_ITERATIONS, out->hash, USER_HASH_SIZE);
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
    return vfs_create(path, VFS_MODE_FILE, NULL);
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
    int seen;
};

static int next_ids_callback(const struct user_record *rec, void *userdata) {
    struct next_ids_ctx *ctx = (struct next_ids_ctx *)userdata;
    ctx->seen = 1;
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
    ctx.max_uid = 0;
    ctx.max_gid = 0;
    ctx.seen = 0;
    int res = iterate_users(next_ids_callback, &ctx);
    if (res < 0) {
        return -1;
    }
    if (!ctx.seen) {
        if (out_uid) {
            *out_uid = 0;
        }
        if (out_gid) {
            *out_gid = 0;
        }
        return 0;
    }
    if (out_uid) {
        *out_uid = ctx.max_uid + 1;
    }
    if (out_gid) {
        *out_gid = ctx.max_gid + 1;
    }
    return 0;
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
    u32_to_string(user->uid, uid_buf, sizeof(uid_buf));
    u32_to_string(user->gid, gid_buf, sizeof(gid_buf));
    bytes_to_hex(user->salt, USER_SALT_SIZE, salt_hex);
    bytes_to_hex(user->hash, USER_HASH_SIZE, hash_hex);

    size_t idx = 0;
    const char *pieces[] = {
        user->username, ":", uid_buf, ":", gid_buf, ":", user->home, ":",
        salt_hex,       ":", hash_hex, ":", user->role, "\n"};
    for (size_t i = 0; i < sizeof(pieces) / sizeof(pieces[0]); ++i) {
        if (append_piece(line, line_cap, &idx, pieces[i]) != 0) {
            return -1;
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
    char line[USER_NAME_MAX + USER_HOME_MAX + USER_ROLE_MAX + USER_SALT_SIZE * 2 +
              USER_HASH_SIZE * 2 + 64];
    size_t line_len = 0;
    if (serialize_user_record_line(user, line, sizeof(line), &line_len) != 0) {
        vfs_close(f);
        return -1;
    }
    long written = vfs_write(f, line, line_len);
    vfs_close(f);
    return (written == (long)line_len) ? 0 : -1;
}

int userdb_authenticate(const char *username, const char *password, struct user_record *out) {
    if (!username || !password) {
        return -1;
    }
    struct user_record rec;
    if (userdb_find(username, &rec) != 0) {
        return -1;
    }
    uint8_t hash[USER_HASH_SIZE];
    crypt_pbkdf2_sha256((const uint8_t *)password, cstring_length(password),
                        rec.salt, USER_SALT_SIZE,
                        USER_ITERATIONS, hash, USER_HASH_SIZE);
    for (size_t i = 0; i < USER_HASH_SIZE; ++i) {
        if (hash[i] != rec.hash[i]) {
            return -1;
        }
    }
    if (out) {
        *out = rec;
    }
    return 0;
}

int userdb_set_password(const char *username, const char *new_password) {
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
                        generate_salt(rec.salt, USER_SALT_SIZE);
                        crypt_pbkdf2_sha256((const uint8_t *)new_password,
                                            cstring_length(new_password), rec.salt,
                                            USER_SALT_SIZE, USER_ITERATIONS,
                                            rec.hash, USER_HASH_SIZE);
                        updated = 1;
                    }
                    char line[USER_NAME_MAX + USER_HOME_MAX + USER_ROLE_MAX +
                              USER_SALT_SIZE * 2 + USER_HASH_SIZE * 2 + 64];
                    size_t len = 0;
                    if (serialize_user_record_line(&rec, line, sizeof(line), &len) != 0 ||
                        out_len + len >= out_cap) {
                        memory_zero(rec.salt, USER_SALT_SIZE);
                        memory_zero(rec.hash, USER_HASH_SIZE);
                        kfree(out);
                        kfree(source);
                        return -1;
                    }
                    for (size_t k = 0; k < len; ++k) {
                        out[out_len++] = line[k];
                    }
                    memory_zero(rec.salt, USER_SALT_SIZE);
                    memory_zero(rec.hash, USER_HASH_SIZE);
                }
            }
            line_start = i + 1;
        }
    }

    if (!updated) {
        memory_zero(out, out_cap);
        kfree(out);
        kfree(source);
        return -1;
    }

    int rc = userdb_write_blob(out, out_len);
    memory_zero(out, out_cap);
    kfree(out);
    kfree(source);
    return rc;
}
