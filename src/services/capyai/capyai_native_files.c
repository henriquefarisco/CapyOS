/* Typed CapyAI file adapters for the real CapyOS VFS.
 *
 * No generated text is interpreted as a shell command. The shared executor
 * validates the exact action/tool/risk tuple and this adapter validates paths,
 * session permissions and postconditions again at the VFS boundary.
 */
#include "services/capyai.h"

#include "fs/vfs.h"
#include "shell/core.h"

#define CAPYAI_NATIVE_TEXT_LIMIT (1024u * 1024u)
#define CAPYAI_NATIVE_COPY_CHUNK 256u

static volatile uint32_t g_native_path_nonce;

static int native_streq(const char *a, const char *b) {
    size_t i = 0u;
    if (!a || !b) return 0;
    while (a[i] && b[i] && a[i] == b[i]) ++i;
    return a[i] == '\0' && b[i] == '\0';
}

static void native_copy(char *dst, size_t size, const char *src) {
    size_t i = 0u;
    if (!dst || size == 0u) return;
    while (src && src[i] && i + 1u < size) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

static void native_detail(char *detail, size_t size, const char *prefix,
                          const char *path) {
    size_t n = 0u, i = 0u;
    if (!detail || size == 0u) return;
    while (prefix && prefix[i] && n + 1u < size) detail[n++] = prefix[i++];
    i = 0u;
    while (path && path[i] && n + 1u < size) detail[n++] = path[i++];
    detail[n] = '\0';
}

static int native_suffix_ci(const char *path, const char *suffix) {
    size_t pn = 0u, sn = 0u, i;
    while (path && path[pn]) ++pn;
    while (suffix && suffix[sn]) ++sn;
    if (pn < sn || sn == 0u) return 0;
    for (i = 0u; i < sn; ++i) {
        char a = path[pn - sn + i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
        if (a != b) return 0;
    }
    return 1;
}

static int native_path_in_home(const struct shell_context *ctx,
                               const char *path) {
    const char *home;
    size_t i = 0u;
    if (!ctx || !ctx->session || !path) return 0;
    home = ctx->session->user.home;
    if (!home[0] || home[0] != '/' || (home[0] == '/' && home[1] == '\0'))
        return 0;
    while (home[i] && path[i] == home[i]) ++i;
    if (home[i] != '\0') return 0;
    return path[i] == '\0' || path[i] == '/';
}

static int native_text_path(const char *path) {
    static const char *const suffixes[] = {
        ".txt", ".md", ".log", ".csv", ".json", ".ini", ".conf", NULL};
    size_t i;
    for (i = 0u; suffixes[i]; ++i) {
        if (native_suffix_ci(path, suffixes[i])) return 1;
    }
    return 0;
}

static int native_write_all(struct file *file, const char *data, size_t size) {
    size_t offset = 0u;
    while (offset < size) {
        long written = vfs_write(file, data + offset, size - offset);
        if (written <= 0) return -1;
        offset += (size_t)written;
    }
    return 0;
}

static int native_stat_equal(const struct vfs_stat *a,
                             const struct vfs_stat *b) {
    return a && b && a->ino == b->ino && a->size == b->size &&
           a->uid == b->uid && a->gid == b->gid &&
           a->mode == b->mode && a->perm == b->perm;
}

static void native_hex8(char out[9], uint32_t value) {
    static const char hex[] = "0123456789abcdef";
    size_t i;
    for (i = 0u; i < 8u; ++i) {
        out[7u - i] = hex[value & 0x0fu];
        value >>= 4u;
    }
    out[8] = '\0';
}

/* Build a short hidden sibling name so it remains under VFS_NAME_MAX even
 * when the user filename itself is at the limit. */
static int native_unique_sibling(const char *path, const char *tag,
                                 char *out, size_t out_size) {
    size_t slash = 0u, i, n;
    char serial[9];
    struct vfs_stat ignored;
    if (!path || !tag || !out || out_size == 0u) return -1;
    for (i = 0u; path[i]; ++i) if (path[i] == '/') slash = i;
    for (i = 0u; i < 16u; ++i) {
        uint32_t nonce = __sync_add_and_fetch(&g_native_path_nonce, 1u);
        native_hex8(serial, nonce);
        n = 0u;
        if (slash == 0u) {
            if (n + 1u >= out_size) return -1;
            out[n++] = '/';
        } else {
            size_t p;
            for (p = 0u; p <= slash && n + 1u < out_size; ++p)
                out[n++] = path[p];
            if (p <= slash) return -1;
        }
        {
            static const char prefix[] = ".capyai-";
            size_t p;
            for (p = 0u; prefix[p] && n + 1u < out_size; ++p)
                out[n++] = prefix[p];
            if (prefix[p]) return -1;
            for (p = 0u; tag[p] && n + 1u < out_size; ++p) out[n++] = tag[p];
            if (tag[p] || n + 1u >= out_size) return -1;
            out[n++] = '-';
            for (p = 0u; serial[p] && n + 1u < out_size; ++p)
                out[n++] = serial[p];
            if (serial[p]) return -1;
        }
        out[n] = '\0';
        if (vfs_stat_path(out, &ignored) != 0) {
            if (vfs_last_error() == VFS_ERR_NOT_FOUND) return 0;
            return -1;
        }
    }
    return -1;
}

static int native_create_temp(struct shell_context *ctx, const char *path,
                              const char *tag,
                              const struct vfs_metadata *metadata,
                              char *out, size_t out_size) {
    size_t attempt;
    (void)ctx;
    for (attempt = 0u; attempt < 16u; ++attempt) {
        if (native_unique_sibling(path, tag, out, out_size) != 0) return -1;
        if (vfs_create(out, VFS_MODE_FILE, metadata) == 0) return 0;
    }
    return -1;
}

static int native_copy_exact(const char *source, const char *destination,
                             size_t expected) {
    struct file *reader = shell_open_file_read(source);
    struct file *writer = shell_open_file_write(destination);
    char buffer[CAPYAI_NATIVE_COPY_CHUNK];
    size_t copied = 0u;
    int rc = -1;
    if (!reader || !writer) goto done;
    while (copied < expected) {
        size_t wanted = expected - copied;
        long got;
        if (wanted > sizeof(buffer)) wanted = sizeof(buffer);
        got = vfs_read(reader, buffer, wanted);
        if (got <= 0 || (size_t)got > wanted ||
            native_write_all(writer, buffer, (size_t)got) != 0) goto done;
        copied += (size_t)got;
    }
    rc = copied == expected ? 0 : -1;
done:
    if (reader) vfs_close(reader);
    if (writer) vfs_close(writer);
    return rc;
}

static void native_failure_detail(char *detail, size_t detail_size,
                                  const char *operation) {
    const char *reason = vfs_error_string(vfs_last_error());
    native_detail(detail, detail_size, operation,
                  reason && reason[0] ? reason : "validation failed");
}

static int native_create(struct shell_context *ctx,
                         const struct capy_ai_output *call,
                         char *detail, size_t detail_size) {
    char path[SHELL_PATH_BUFFER];
    char temporary[SHELL_PATH_BUFFER];
    struct vfs_metadata meta;
    struct vfs_stat stat;
    struct file *file;
    size_t content_size = shell_cstring_length(call->content);
    if (shell_resolve_path(ctx, call->path, path, sizeof(path)) != 0 ||
        !native_path_in_home(ctx, path) || !native_text_path(path) ||
        shell_path_is_file(path) ||
        shell_path_is_dir(path)) return -1;
    shell_trim_trailing_slash(path);
    shell_fill_metadata(ctx, VFS_MODE_FILE, &meta);
    if (native_create_temp(ctx, path, "new", &meta, temporary,
                           sizeof(temporary)) != 0) return -1;
    if (content_size > 0u) {
        file = shell_open_file_write(temporary);
        if (!file || native_write_all(file, call->content, content_size) != 0) {
            if (file) vfs_close(file);
            (void)vfs_unlink(temporary);
            return -1;
        }
        vfs_close(file);
    }
    if (vfs_stat_path(temporary, &stat) != 0 || stat.size != content_size ||
        shell_path_is_file(path) || shell_path_is_dir(path) ||
        vfs_rename(temporary, path) != 0) {
        (void)vfs_unlink(temporary);
        return -1;
    }
    if (vfs_stat_path(path, &stat) != 0 || stat.size != content_size) {
        (void)vfs_unlink(path);
        return -1;
    }
    native_detail(detail, detail_size, "arquivo criado: ", path);
    return 0;
}

static int native_edit(struct shell_context *ctx,
                       const struct capy_ai_output *call,
                       char *detail, size_t detail_size) {
    char path[SHELL_PATH_BUFFER];
    char temporary[SHELL_PATH_BUFFER];
    char backup[SHELL_PATH_BUFFER];
    struct vfs_stat before, unchanged, after;
    struct vfs_metadata metadata;
    struct file *file;
    char last = '\0';
    size_t content_size = shell_cstring_length(call->content);
    size_t separator_size = 0u;
    if (content_size == 0u ||
        shell_resolve_path(ctx, call->path, path, sizeof(path)) != 0 ||
        !native_path_in_home(ctx, path) || !native_text_path(path) ||
        !shell_path_is_file(path) ||
        vfs_stat_path(path, &before) != 0 ||
        before.size > CAPYAI_NATIVE_TEXT_LIMIT) return -1;

    if (before.size > 0u) {
        struct file *reader = shell_open_file_read(path);
        if (!reader) return -1;
        reader->position = before.size - 1u;
        if (vfs_read(reader, &last, 1u) != 1) {
            vfs_close(reader);
            return -1;
        }
        vfs_close(reader);
        separator_size = last == '\n' ? 0u : 1u;
    }

    if (content_size > CAPYAI_NATIVE_TEXT_LIMIT ||
        separator_size + 1u > CAPYAI_NATIVE_TEXT_LIMIT - before.size ||
        content_size > CAPYAI_NATIVE_TEXT_LIMIT - before.size -
                           separator_size - 1u) return -1;

    metadata.uid = before.uid;
    metadata.gid = before.gid;
    metadata.perm = before.perm;
    if (native_create_temp(ctx, path, "edit", &metadata, temporary,
                           sizeof(temporary)) != 0 ||
        native_copy_exact(path, temporary, before.size) != 0) {
        (void)vfs_unlink(temporary);
        return -1;
    }
    file = shell_open_file_write(temporary);
    if (!file) {
        (void)vfs_unlink(temporary);
        return -1;
    }
    file->position = before.size;
    if ((separator_size && native_write_all(file, "\n", 1u) != 0) ||
        native_write_all(file, call->content, content_size) != 0 ||
        native_write_all(file, "\n", 1u) != 0) {
        vfs_close(file);
        (void)vfs_unlink(temporary);
        return -1;
    }
    vfs_close(file);
    if (vfs_stat_path(temporary, &after) != 0 ||
        after.size != before.size + separator_size + content_size + 1u ||
        vfs_stat_path(path, &unchanged) != 0 ||
        !native_stat_equal(&before, &unchanged) ||
        native_unique_sibling(path, "old", backup, sizeof(backup)) != 0 ||
        vfs_rename(path, backup) != 0) {
        (void)vfs_unlink(temporary);
        return -1;
    }
    if (vfs_rename(temporary, path) != 0) {
        (void)vfs_rename(backup, path);
        (void)vfs_unlink(temporary);
        return -1;
    }
    if (vfs_stat_path(path, &after) != 0 ||
        after.size != before.size + separator_size + content_size + 1u) {
        (void)vfs_rename(path, temporary);
        (void)vfs_rename(backup, path);
        (void)vfs_unlink(temporary);
        return -1;
    }
    if (vfs_unlink(backup) != 0) {
        (void)vfs_rename(path, temporary);
        (void)vfs_rename(backup, path);
        (void)vfs_unlink(temporary);
        return -1;
    }
    native_detail(detail, detail_size, "arquivo atualizado: ", path);
    return 0;
}

static int native_move(struct shell_context *ctx,
                       const struct capy_ai_output *call,
                       char *detail, size_t detail_size) {
    char source[SHELL_PATH_BUFFER];
    char destination[SHELL_PATH_BUFFER];
    char combined[SHELL_PATH_BUFFER];
    struct vfs_stat before, stat;
    int explicit_directory = 0;
    if (shell_resolve_path(ctx, call->path, source, sizeof(source)) != 0 ||
        shell_resolve_path(ctx, call->content, destination,
                           sizeof(destination)) != 0) return -1;
    {
        size_t raw_size = shell_cstring_length(call->content);
        explicit_directory = raw_size > 0u &&
                             call->content[raw_size - 1u] == '/';
    }
    shell_trim_trailing_slash(source);
    shell_trim_trailing_slash(destination);
    if (!native_path_in_home(ctx, source) ||
        !native_path_in_home(ctx, destination) ||
        !shell_path_is_file(source) || vfs_stat_path(source, &before) != 0)
        return -1;
    if (shell_path_is_dir(destination)) {
        size_t destination_size = shell_cstring_length(destination);
        size_t basename_size = shell_cstring_length(shell_basename(source));
        if (destination_size + 1u + basename_size >= CAPYAI_PATH_MAX) return -1;
        if (shell_join_path(destination, shell_basename(source), combined,
                            sizeof(combined)) != 0) return -1;
        native_copy(destination, sizeof(destination), combined);
    } else if (explicit_directory) {
        /* The planner marked this as a folder, but it does not exist. */
        return -1;
    }
    if (shell_path_is_file(destination) || shell_path_is_dir(destination) ||
        native_streq(source, destination)) return -1;
    if (vfs_rename(source, destination) != 0) return -1;
    if (vfs_stat_path(source, &stat) == 0 ||
        vfs_stat_path(destination, &stat) != 0 ||
        (stat.mode & VFS_MODE_FILE) == 0u || stat.ino != before.ino) {
        (void)vfs_rename(destination, source);
        return -1;
    }
    native_detail(detail, detail_size, "arquivo movido: ", destination);
    return 0;
}

int capyai_native_file_dispatch(void *opaque,
                                const struct capy_ai_output *tool_call,
                                char *detail, size_t detail_size) {
    struct shell_context *ctx = (struct shell_context *)opaque;
    if (!ctx || !tool_call || !capyai_typed_dispatch_valid(tool_call)) return -1;
    {
        int rc = -1;
        if (detail && detail_size > 0u) detail[0] = '\0';
        if (native_streq(tool_call->action, "file_create"))
            rc = native_create(ctx, tool_call, detail, detail_size);
        else if (native_streq(tool_call->action, "file_edit_text"))
            rc = native_edit(ctx, tool_call, detail, detail_size);
        else if (native_streq(tool_call->action, "file_move"))
            rc = native_move(ctx, tool_call, detail, detail_size);
        if (rc != 0 && detail && detail_size > 0u && detail[0] == '\0')
            native_failure_detail(detail, detail_size, "falha de arquivo: ");
        return rc;
    }
}
