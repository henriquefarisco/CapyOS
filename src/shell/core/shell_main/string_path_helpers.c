#include "internal/shell_main_internal.h"

int shell_string_equal(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b) {
        if (*a++ != *b++) {
            return 0;
        }
    }
    return *a == *b;
}

size_t shell_cstring_length(const char *s)
{
    size_t len = 0;
    if (!s) {
        return 0;
    }
    while (s[len]) {
        ++len;
    }
    return len;
}

void shell_copy(char *dst, size_t dst_len, const char *src)
{
    if (!dst || !dst_len) {
        return;
    }
    size_t i = 0;
    if (src) {
        while (src[i] && i < dst_len - 1) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = '\0';
}

int shell_string_contains(const char *haystack, const char *needle)
{
    if (!needle || !needle[0]) {
        return 1;
    }
    if (!haystack) {
        return 0;
    }
    size_t hlen = shell_cstring_length(haystack);
    size_t nlen = shell_cstring_length(needle);
    if (nlen > hlen) {
        return 0;
    }
    for (size_t i = 0; i + nlen <= hlen; ++i) {
        size_t j = 0;
        while (j < nlen && haystack[i + j] == needle[j]) {
            ++j;
        }
        if (j == nlen) {
            return 1;
        }
    }
    return 0;
}

int shell_help_requested(int argc, char **argv)
{
    if (argc != 2 || !argv || !argv[1]) {
        return 0;
    }
    return shell_string_equal(argv[1], "-help") || shell_string_equal(argv[1], "--help");
}

int shell_handle_help(int argc, char **argv, const char *usage, const char *details)
{
    const char *language = shell_current_language();
    if (!shell_help_requested(argc, argv)) {
        return 0;
    }
    if (usage) {
        shell_print(localization_select(language, "Uso: ", "Usage: ", "Uso: "));
        shell_print(usage);
        shell_newline();
    }
    if (details) {
        shell_print(details);
        shell_newline();
    }
    return 1;
}

void shell_suggest_help(const char *cmd)
{
    const char *language = shell_current_language();
    shell_print(localization_select(language, "Use ", "Use ", "Usa "));
    shell_print(cmd);
    shell_print(localization_select(language, " -help para detalhes.\n",
                                    " -help for details.\n",
                                    " -help para ver detalles.\n"));
}

static int shell_get_stat(const char *path, struct vfs_stat *st)
{
    return vfs_stat_path(path, st);
}

void shell_fill_metadata(struct shell_context *ctx, uint16_t mode, struct vfs_metadata *meta)
{
    const struct user_record *user = ctx && ctx->session ? session_user(ctx->session) : NULL;
    if (user) {
        meta->uid = user->uid;
        meta->gid = user->gid;
    } else {
        meta->uid = 0;
        meta->gid = 0;
    }
    meta->perm = (mode & VFS_MODE_DIR) ? 0755 : 0644;
}

int shell_path_is_dir(const char *path)
{
    struct vfs_stat st;
    if (shell_get_stat(path, &st) != 0) {
        return 0;
    }
    return (st.mode & VFS_MODE_DIR) != 0;
}

int shell_path_is_file(const char *path)
{
    struct vfs_stat st;
    if (shell_get_stat(path, &st) != 0) {
        return 0;
    }
    return (st.mode & VFS_MODE_FILE) != 0;
}

void shell_trim_trailing_slash(char *path)
{
    size_t len = shell_cstring_length(path);
    while (len > 1 && path[len - 1] == '/') {
        path[--len] = '\0';
    }
}

static void shell_copy_slice(char *dst, size_t dst_size, const char *src, size_t len)
{
    size_t out = 0;
    if (!dst || dst_size == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    while (out + 1 < dst_size && out < len) {
        dst[out] = src[out];
        ++out;
    }
    dst[out] = '\0';
}

void shell_append_text(char *dst, size_t dst_size, const char *src)
{
    size_t pos = shell_cstring_length(dst);
    size_t idx = 0;
    if (!dst || dst_size == 0 || !src) {
        return;
    }
    while (src[idx] && pos + 1 < dst_size) {
        dst[pos++] = src[idx++];
    }
    dst[pos] = '\0';
}

int shell_resolve_path(struct shell_context *ctx, const char *input, char *out, size_t out_len)
{
    if (!ctx || !ctx->session) {
        return -1;
    }
    if (!input || !input[0]) {
        return session_resolve_path(ctx->session, ".", out, out_len);
    }
    return session_resolve_path(ctx->session, input, out, out_len);
}

int shell_join_path(const char *dir, const char *name, char *out, size_t out_len)
{
    if (!out || !out_len) {
        return -1;
    }
    size_t pos = 0;
    const char *base_dir = (dir && dir[0]) ? dir : "/";
    size_t dir_len = shell_cstring_length(base_dir);
    if (dir_len >= out_len) {
        return -1;
    }
    for (size_t i = 0; i < dir_len; ++i) {
        out[pos++] = base_dir[i];
    }
    if (pos > 1 && out[pos - 1] != '/') {
        if (pos >= out_len) {
            return -1;
        }
        out[pos++] = '/';
    }
    const char *leaf = name ? name : "";
    size_t name_len = shell_cstring_length(leaf);
    if (pos + name_len >= out_len) {
        return -1;
    }
    for (size_t i = 0; i < name_len; ++i) {
        out[pos++] = leaf[i];
    }
    if (pos == 0) {
        out[pos++] = '/';
    }
    out[pos] = '\0';
    return 0;
}

const char *shell_basename(const char *path)
{
    if (!path) {
        return "";
    }
    size_t len = shell_cstring_length(path);
    while (len > 0 && path[len - 1] == '/') {
        --len;
    }
    const char *last = path;
    for (size_t i = 0; i < len; ++i) {
        if (path[i] == '/' && i + 1 < len) {
            last = &path[i + 1];
        }
    }
    return last;
}

void shell_format_prompt_path(const struct user_record *user, const char *cwd,
                              char *out, size_t out_len)
{
    const char *path = (cwd && cwd[0]) ? cwd : "/";
    const char *home = (user && user->home[0]) ? user->home : NULL;
    const char *display = path;
    char segments[16][VFS_NAME_MAX];
    size_t seg_count = 0;

    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';

    if (home) {
        size_t home_len = shell_cstring_length(home);
        size_t path_len = shell_cstring_length(path);
        if (path[0] == '/' && home_len > 0) {
            if (path_len == home_len && path[home_len] == '\0') {
                shell_append_text(out, out_len, "~");
                return;
            }
            if (home_len < path_len && path[home_len] == '/') {
                shell_append_text(out, out_len, "~/");
                display = path + home_len + 1;
            }
        }
    }

    if (display[0] == '\0') {
        shell_append_text(out, out_len, out[0] ? "~" : "/");
        return;
    }

    if (display[0] == '/' && display[1] == '\0') {
        shell_append_text(out, out_len, "/");
        return;
    }

    while (*display == '/') {
        ++display;
    }

    while (*display && seg_count < (sizeof(segments) / sizeof(segments[0]))) {
        const char *start = display;
        size_t len = 0;
        while (display[len] && display[len] != '/') {
            ++len;
        }
        shell_copy_slice(segments[seg_count], sizeof(segments[seg_count]), start, len);
        ++seg_count;
        display += len;
        while (*display == '/') {
            ++display;
        }
    }

    if (seg_count == 0) {
        shell_append_text(out, out_len, out[0] ? "~" : "/");
        return;
    }

    if (out[0] == '\0' && path[0] == '/') {
        shell_append_text(out, out_len, "/");
    }

    if (seg_count > 2) {
        shell_append_text(out, out_len, ".../");
    }

    {
        size_t start = (seg_count > 2) ? (seg_count - 2) : 0;
        for (size_t i = start; i < seg_count; ++i) {
            shell_append_text(out, out_len, segments[i]);
            if (i + 1 < seg_count) {
                shell_append_text(out, out_len, "/");
            }
        }
    }
}

void shell_build_prompt(const struct user_record *user,
                        const struct system_settings *settings,
                        const char *cwd, char *out, size_t out_len)
{
    const char *username = (user && user->username[0]) ? user->username : "user";
    const char *hostname =
        (settings && settings->hostname[0]) ? settings->hostname : "capyos";
    char prompt_path[96];

    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';

    shell_format_prompt_path(user, cwd, prompt_path, sizeof(prompt_path));
    shell_append_text(out, out_len, username);
    shell_append_text(out, out_len, "@");
    shell_append_text(out, out_len, hostname);
    shell_append_text(out, out_len, ">");
    shell_append_text(out, out_len, prompt_path[0] ? prompt_path : "/");
    shell_append_text(out, out_len, "> ");
}

void shell_format_perm(uint16_t perm, char out[5])
{
    out[0] = (char)('0' + ((perm >> 6) & 0x7));
    out[1] = (char)('0' + ((perm >> 3) & 0x7));
    out[2] = (char)('0' + (perm & 0x7));
    out[3] = '\0';
    out[4] = '\0';
}

