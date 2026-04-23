#include "auth/session.h"

#include "fs/vfs.h"

#include <stdint.h>

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

static struct session_context *active_session = NULL;

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

void session_reset(struct session_context *ctx) {
    if (!ctx) {
        return;
    }
    memory_zero(ctx, sizeof(*ctx));
    user_preferences_set_defaults(&ctx->prefs);
    ctx->cwd[0] = '/';
    ctx->cwd[1] = '\0';
}

int session_begin(struct session_context *ctx, const struct user_record *user,
                  const char *default_language) {
    if (!ctx || !user) {
        return -1;
    }
    session_reset(ctx);
    ctx->user = *user;
    if (user_prefs_load(user, &ctx->prefs) <= 0 && default_language) {
        cstring_copy(ctx->prefs.language, sizeof(ctx->prefs.language),
                     default_language);
    }
    if (user->home[0]) {
        if (session_set_cwd(ctx, user->home) != 0) {
            session_set_cwd(ctx, "/");
        }
    } else {
        session_set_cwd(ctx, "/");
    }
    return 0;
}

const struct user_record *session_user(const struct session_context *ctx) {
    if (!ctx) {
        return NULL;
    }
    return &ctx->user;
}

const char *session_language(const struct session_context *ctx) {
    if (!ctx) {
        return "pt-BR";
    }
    return user_preferences_language(&ctx->prefs);
}

const char *session_cwd(const struct session_context *ctx) {
    if (!ctx) {
        return "/";
    }
    return ctx->cwd[0] ? ctx->cwd : "/";
}

static int push_component(char segments[][VFS_NAME_MAX], int *count, const char *component, size_t len) {
    if (len == 0) {
        return 0;
    }
    if (len >= VFS_NAME_MAX) {
        return -1;
    }
    if (*count >= 32) {
        return -1;
    }
    for (size_t i = 0; i < len; ++i) {
        segments[*count][i] = component[i];
    }
    segments[*count][len] = '\0';
    (*count)++;
    return 0;
}

static int normalize_path(const char *base, const char *input, char *out, size_t out_len) {
    char segments[32][VFS_NAME_MAX];
    int count = 0;

    if (input && input[0] == '/') {
        base = NULL;
    }

    if (base && base[0]) {
        const char *p = base;
        while (*p == '/') {
            ++p;
        }
        while (*p) {
            const char *start = p;
            size_t len = 0;
            while (p[len] && p[len] != '/') {
                ++len;
            }
            if (len > 0) {
                if (push_component(segments, &count, start, len) != 0) {
                    return -1;
                }
            }
            p += len;
            while (*p == '/') {
                ++p;
            }
        }
    }

    const char *src = input;
    if (!src) {
        src = "";
    }
    while (*src == '/') {
        ++src;
    }
    while (*src) {
        const char *start = src;
        size_t len = 0;
        while (src[len] && src[len] != '/') {
            ++len;
        }
        if (len == 1 && start[0] == '.') {
            // noop
        } else if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (count > 0) {
                count--;
            }
        } else if (len > 0) {
            if (push_component(segments, &count, start, len) != 0) {
                return -1;
            }
        }
        src += len;
        while (*src == '/') {
            ++src;
        }
    }

    size_t pos = 0;
    if (pos < out_len) {
        out[pos++] = '/';
    }
    for (int i = 0; i < count; ++i) {
        size_t clen = cstring_length(segments[i]);
        if (pos + clen + 1 >= out_len) {
            return -1;
        }
        for (size_t j = 0; j < clen; ++j) {
            out[pos++] = segments[i][j];
        }
        if (i != count - 1) {
            out[pos++] = '/';
        }
    }
    if (pos >= out_len) {
        return -1;
    }
    out[pos] = '\0';
    return 0;
}

int session_set_cwd(struct session_context *ctx, const char *path) {
    if (!ctx || !path) {
        return -1;
    }
    char normalized[SESSION_PATH_MAX];
    if (normalize_path(ctx->cwd, path, normalized, sizeof(normalized)) != 0) {
        return -1;
    }
    struct dentry *d = NULL;
    if (vfs_lookup(normalized, &d) != 0) {
        return -1;
    }
    int ok = 0;
    if (d->inode && (d->inode->mode & VFS_MODE_DIR)) {
        cstring_copy(ctx->cwd, SESSION_PATH_MAX, normalized);
        ok = 1;
    }
    if (d && d->refcount) {
        d->refcount--;
    }
    return ok ? 0 : -1;
}

int session_resolve_path(const struct session_context *ctx, const char *input, char *out, size_t out_len) {
    if (!ctx || !out || out_len == 0) {
        return -1;
    }
    const char *base = ctx->cwd;
    if (!base || !base[0]) {
        base = "/";
    }
    return normalize_path(base, input, out, out_len);
}

void session_set_active(struct session_context *ctx) {
    active_session = ctx;
}

struct session_context *session_active(void) {
    return active_session;
}
