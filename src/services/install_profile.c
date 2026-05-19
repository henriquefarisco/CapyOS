/*
 * src/services/install_profile.c
 *
 * Parser for /system/install/profile.ini. Pure logic, no I/O. Caller
 * reads the file through the VFS and hands the buffer to
 * install_profile_parse(). Designed to mirror the line-oriented
 * `key=value` discipline of update_agent_parse.c and capypkg_manifest.c.
 */

#include "services/install_profile.h"

#include <stddef.h>
#include <stdint.h>

/* ---- local helpers (no libc, mirrors capypkg / update_agent) ---- */

static void profile_zero(void *ptr, size_t len) {
    uint8_t *dst = (uint8_t *)ptr;
    while (len--) {
        *dst++ = 0u;
    }
}

static void profile_copy(char *dst, size_t dst_size, const char *src,
                         size_t src_len) {
    size_t copy_len = src_len;
    if (!dst || dst_size == 0u) {
        return;
    }
    if (copy_len + 1u > dst_size) {
        copy_len = dst_size - 1u;
    }
    for (size_t i = 0u; i < copy_len; ++i) {
        dst[i] = src[i];
    }
    dst[copy_len] = '\0';
}

static int profile_equal_cstr(const char *text, size_t len, const char *cstr) {
    size_t i = 0u;
    while (i < len && cstr[i]) {
        if (text[i] != cstr[i]) {
            return 0;
        }
        ++i;
    }
    return i == len && cstr[i] == '\0';
}

static int profile_starts_with(const char *text, const char *prefix) {
    size_t i = 0u;
    if (!text || !prefix) {
        return 0;
    }
    while (prefix[i]) {
        if (text[i] != prefix[i]) {
            return 0;
        }
        ++i;
    }
    return 1;
}

/* Reject any value carrying control bytes / ANSI escapes. Same gate
 * the capypkg manifest parser enforces, for the same reason: the
 * value will likely be echoed to the framebuffer and mirrored to
 * COM1 by `vga_write` via `shell_print`. */
static int profile_value_is_printable_ascii(const char *value, size_t len) {
    for (size_t i = 0u; i < len; ++i) {
        unsigned char c = (unsigned char)value[i];
        if (c < 0x20u || c > 0x7Eu) {
            return 0;
        }
    }
    return 1;
}

/* Allowed alphabet for repo name and install list entries:
 *   [a-zA-Z0-9._-]
 * Empty string rejected. Bare ".", ".." rejected even though every
 * char is in the alphabet (capypkg adapter would reject anyway, but
 * we mirror the rule here so profile.ini errors are surfaced first). */
static int profile_name_alphabet_ok(const char *value, size_t len) {
    if (len == 0u) {
        return 0;
    }
    int only_dots = 1;
    for (size_t i = 0u; i < len; ++i) {
        char c = value[i];
        int alnum = (c >= 'a' && c <= 'z') ||
                    (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9');
        int allowed_punct = (c == '.' || c == '_' || c == '-');
        if (!alnum && !allowed_punct) {
            return 0;
        }
        if (c != '.') {
            only_dots = 0;
        }
    }
    return only_dots ? 0 : 1;
}

static int profile_parse_bool(const char *value, size_t len, uint8_t *out) {
    if (len == 1u && value[0] == '0') {
        *out = 0u;
        return 0;
    }
    if (len == 1u && value[0] == '1') {
        *out = 1u;
        return 0;
    }
    return -1;
}

/* ---- public API ---- */

void install_profile_reset(struct install_profile *profile) {
    if (!profile) {
        return;
    }
    profile_zero(profile, sizeof(*profile));
    profile->kind = INSTALL_PROFILE_BASIC;
    profile->valid = 1u;
}

const char *install_profile_kind_label(enum install_profile_kind kind) {
    switch (kind) {
    case INSTALL_PROFILE_BASIC: return "basic";
    case INSTALL_PROFILE_FULL: return "full";
    case INSTALL_PROFILE_CUSTOM: return "custom";
    default: return "unknown";
    }
}

const char *install_profile_result_label(int rc) {
    switch (rc) {
    case INSTALL_PROFILE_OK: return "ok";
    case INSTALL_PROFILE_ERR_INVALID_ARG: return "invalid argument";
    case INSTALL_PROFILE_ERR_PARSE: return "parse error";
    case INSTALL_PROFILE_ERR_DENIED: return "denied";
    case INSTALL_PROFILE_ERR_MISSING_FIELD: return "missing field";
    case INSTALL_PROFILE_ERR_NOT_READY: return "not ready";
    case INSTALL_PROFILE_ERR_STORAGE: return "storage error";
    default: return "error";
    }
}

int install_profile_install_list_next(const char *list, size_t *cursor,
                                      char *out, size_t out_size) {
    if (!list || !cursor || !out || out_size == 0u) {
        return -1;
    }
    size_t i = *cursor;
    /* skip leading separators / whitespace */
    while (list[i] == ',' || list[i] == ' ' || list[i] == '\t') {
        ++i;
    }
    if (list[i] == '\0') {
        *cursor = i;
        return -1;
    }
    size_t start = i;
    while (list[i] != '\0' && list[i] != ',') {
        ++i;
    }
    size_t span = i - start;
    /* trim trailing whitespace */
    while (span > 0u && (list[start + span - 1u] == ' ' ||
                         list[start + span - 1u] == '\t')) {
        --span;
    }
    profile_copy(out, out_size, &list[start], span);
    *cursor = i;
    return 0;
}

/* Internal: apply a single key=value pair after the printable-ASCII
 * gate has cleared. Returns INSTALL_PROFILE_OK on accepted or
 * tolerated keys, negative on hard rejections. Unknown keys are
 * tolerated. */
static int profile_apply_kv(struct install_profile *profile,
                            const char *key, size_t key_len,
                            const char *value, size_t value_len) {
    if (profile_equal_cstr(key, key_len, "profile")) {
        if (profile_equal_cstr(value, value_len, "basic")) {
            profile->kind = INSTALL_PROFILE_BASIC;
        } else if (profile_equal_cstr(value, value_len, "full")) {
            profile->kind = INSTALL_PROFILE_FULL;
        } else if (profile_equal_cstr(value, value_len, "custom")) {
            profile->kind = INSTALL_PROFILE_CUSTOM;
        } else {
            return INSTALL_PROFILE_ERR_PARSE;
        }
        return INSTALL_PROFILE_OK;
    }
    if (profile_equal_cstr(key, key_len, "bootstrap_repo_name")) {
        if (!profile_name_alphabet_ok(value, value_len)) {
            return INSTALL_PROFILE_ERR_DENIED;
        }
        profile_copy(profile->repo_name, sizeof(profile->repo_name),
                     value, value_len);
        return INSTALL_PROFILE_OK;
    }
    if (profile_equal_cstr(key, key_len, "bootstrap_repo_url")) {
        if (value_len == 0u) {
            return INSTALL_PROFILE_ERR_PARSE;
        }
        /* Persist the raw value first so error reporting works, then
         * sanity-check the prefix. */
        profile_copy(profile->repo_url, sizeof(profile->repo_url),
                     value, value_len);
        if (!profile_starts_with(profile->repo_url, "https://")) {
            return INSTALL_PROFILE_ERR_DENIED;
        }
        return INSTALL_PROFILE_OK;
    }
    if (profile_equal_cstr(key, key_len, "bootstrap_repo_signed")) {
        uint8_t flag = 0u;
        if (profile_parse_bool(value, value_len, &flag) != 0) {
            return INSTALL_PROFILE_ERR_PARSE;
        }
        profile->repo_signed = flag;
        return INSTALL_PROFILE_OK;
    }
    if (profile_equal_cstr(key, key_len, "bootstrap_install")) {
        /* Persist verbatim; iterate at consume time. Each token will
         * still be checked against the name alphabet by the capypkg
         * adapter when capypkg_install runs. We do a light syntactic
         * check here so a clearly broken list (with control bytes or
         * out-of-alphabet chars) is rejected before bootstrap. */
        profile_copy(profile->install_list, sizeof(profile->install_list),
                     value, value_len);
        size_t cursor = 0u;
        char token[INSTALL_PROFILE_NAME_MAX];
        int saw_any = 0;
        while (install_profile_install_list_next(profile->install_list,
                                                 &cursor, token,
                                                 sizeof(token)) == 0) {
            size_t tlen = 0u;
            while (token[tlen] != '\0') {
                ++tlen;
            }
            if (!profile_name_alphabet_ok(token, tlen)) {
                return INSTALL_PROFILE_ERR_DENIED;
            }
            saw_any = 1;
        }
        if (!saw_any) {
            /* Empty list value is OK only when profile != custom; we
             * defer the cross-field check to the validation step below. */
            profile->install_list[0] = '\0';
        }
        return INSTALL_PROFILE_OK;
    }
    /* Unknown key: tolerate forward-compat (mirrors capypkg manifest). */
    (void)profile;
    (void)key;
    (void)value;
    (void)key_len;
    (void)value_len;
    return INSTALL_PROFILE_OK;
}

/* Cross-field validation after all key=value pairs are applied. */
static int profile_validate(struct install_profile *profile) {
    if (profile->kind == INSTALL_PROFILE_BASIC) {
        return INSTALL_PROFILE_OK;
    }
    /* full and custom both need the bootstrap repo configured. */
    if (profile->repo_name[0] == '\0' || profile->repo_url[0] == '\0') {
        return INSTALL_PROFILE_ERR_MISSING_FIELD;
    }
    /* custom additionally needs at least one install entry. */
    if (profile->kind == INSTALL_PROFILE_CUSTOM) {
        size_t cursor = 0u;
        char token[INSTALL_PROFILE_NAME_MAX];
        int saw_any = 0;
        while (install_profile_install_list_next(profile->install_list,
                                                 &cursor, token,
                                                 sizeof(token)) == 0) {
            saw_any = 1;
            break;
        }
        if (!saw_any) {
            return INSTALL_PROFILE_ERR_MISSING_FIELD;
        }
    }
    return INSTALL_PROFILE_OK;
}

int install_profile_parse(const char *text, size_t len,
                          struct install_profile *out) {
    if (!out) {
        return INSTALL_PROFILE_ERR_INVALID_ARG;
    }
    install_profile_reset(out);

    if (!text || len == 0u) {
        /* Empty / missing config => BASIC default. */
        return INSTALL_PROFILE_OK;
    }

    size_t cursor = 0u;
    while (cursor < len) {
        size_t line_start = cursor;
        size_t line_end = cursor;
        while (line_end < len &&
               text[line_end] != '\n' && text[line_end] != '\r') {
            ++line_end;
        }
        size_t line_len = line_end - line_start;
        size_t advance = line_end;
        while (advance < len &&
               (text[advance] == '\n' || text[advance] == '\r')) {
            ++advance;
        }

        if (line_len == 0u) {
            cursor = advance;
            continue;
        }
        /* Skip leading whitespace then full-line comment marker. */
        size_t key_start = line_start;
        while (key_start < line_end &&
               (text[key_start] == ' ' || text[key_start] == '\t')) {
            ++key_start;
        }
        if (key_start >= line_end || text[key_start] == '#') {
            cursor = advance;
            continue;
        }

        size_t eq = key_start;
        while (eq < line_end && text[eq] != '=') {
            ++eq;
        }
        if (eq >= line_end) {
            /* Line without `=` is malformed; reject as a whole so a
             * truncated/garbled profile.ini cannot silently boot as
             * BASIC when the operator expected FULL. */
            install_profile_reset(out);
            return INSTALL_PROFILE_ERR_PARSE;
        }
        const char *key = &text[key_start];
        size_t key_len = eq - key_start;
        const char *value = &text[eq + 1u];
        size_t value_len = line_end - (eq + 1u);

        /* Trim trailing whitespace from value. */
        while (value_len > 0u &&
               (value[value_len - 1u] == ' ' ||
                value[value_len - 1u] == '\t')) {
            --value_len;
        }

        if (!profile_value_is_printable_ascii(value, value_len)) {
            install_profile_reset(out);
            return INSTALL_PROFILE_ERR_DENIED;
        }

        int rc = profile_apply_kv(out, key, key_len, value, value_len);
        if (rc != INSTALL_PROFILE_OK) {
            install_profile_reset(out);
            return rc;
        }

        cursor = advance;
    }

    int vrc = profile_validate(out);
    if (vrc != INSTALL_PROFILE_OK) {
        install_profile_reset(out);
        return vrc;
    }
    out->valid = 1u;
    return INSTALL_PROFILE_OK;
}

int install_profile_should_bootstrap(const struct install_profile *profile) {
    if (!profile || !profile->valid) {
        return 0;
    }
    if (profile->kind == INSTALL_PROFILE_BASIC) {
        return 0;
    }
    if (profile->repo_name[0] == '\0' || profile->repo_url[0] == '\0') {
        return 0;
    }
    if (!profile_starts_with(profile->repo_url, "https://")) {
        return 0;
    }
    return 1;
}
