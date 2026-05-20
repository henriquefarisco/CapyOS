/*
 * src/services/capypkg/capypkg_manifest.c
 *
 * Manifest parsing and payload verification for the CapyOS package
 * adapter. Manifest format is line-oriented `key=value` to mirror the
 * existing update_agent format and ease audit. Multiple package
 * descriptors in one index are separated by `---` on its own line.
 *
 * Recognized keys per descriptor:
 *
 *   name=<package-name>
 *   version=<semver>
 *   summary=<one-line description>
 *   payload_url=<https URL>
 *   payload_sha256=<64 hex>
 *   payload_size=<decimal bytes>
 *   signature_ed25519=<128 hex>
 *   install_root=<absolute path inside /var/capypkg or /opt>
 *   depends=<dep1,dep2,...>
 *   repo=<source repo name>
 *
 * Required keys: name, version, payload_url, payload_sha256.
 * Signature is required if the source repository was added with
 * require_signature == 1 (default).
 */

#include "internal/capypkg_internal.h"

#include "security/sha256.h"

#include <stddef.h>
#include <stdint.h>

static char g_capypkg_last_verify_error[192];

static void diag_clear(void) {
    g_capypkg_last_verify_error[0] = '\0';
}

static void diag_append(char *dst, size_t dst_size, const char *src) {
    size_t d = 0u;
    size_t s = 0u;
    if (!dst || dst_size == 0u || !src) return;
    while (d + 1u < dst_size && dst[d]) d++;
    while (d + 1u < dst_size && src[s]) {
        dst[d++] = src[s++];
    }
    dst[d] = '\0';
}

static void diag_append_u32(char *dst, size_t dst_size, uint32_t value) {
    char digits[10];
    size_t count = 0u;
    if (value == 0u) {
        diag_append(dst, dst_size, "0");
        return;
    }
    while (value > 0u && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (count > 0u) {
        char ch[2];
        ch[0] = digits[--count];
        ch[1] = '\0';
        diag_append(dst, dst_size, ch);
    }
}

static void diag_set_size_mismatch(size_t actual, uint32_t expected) {
    diag_clear();
    diag_append(g_capypkg_last_verify_error, sizeof(g_capypkg_last_verify_error),
                "size actual=");
    diag_append_u32(g_capypkg_last_verify_error,
                    sizeof(g_capypkg_last_verify_error), (uint32_t)actual);
    diag_append(g_capypkg_last_verify_error, sizeof(g_capypkg_last_verify_error),
                " expected=");
    diag_append_u32(g_capypkg_last_verify_error,
                    sizeof(g_capypkg_last_verify_error), expected);
}

static void diag_set_sha_mismatch(const char *actual, const char *expected) {
    diag_clear();
    diag_append(g_capypkg_last_verify_error, sizeof(g_capypkg_last_verify_error),
                "sha actual=");
    diag_append(g_capypkg_last_verify_error, sizeof(g_capypkg_last_verify_error),
                actual ? actual : "-");
    diag_append(g_capypkg_last_verify_error, sizeof(g_capypkg_last_verify_error),
                " expected=");
    diag_append(g_capypkg_last_verify_error, sizeof(g_capypkg_last_verify_error),
                expected ? expected : "-");
}

const char *capypkg_last_verify_error(void) {
    return g_capypkg_last_verify_error;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'a' && c <= 'f') return 10 + (int)(c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (int)(c - 'A');
    return -1;
}

static int decode_hex64(const char *hex, uint8_t out[32]) {
    if (!hex) return -1;
    for (size_t i = 0u; i < 32u; ++i) {
        int hi = hex_value(hex[2u * i]);
        int lo = hex_value(hex[2u * i + 1u]);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return 0;
}

static int parse_uint32(const char *text, size_t len, uint32_t *out) {
    uint32_t v = 0u;
    int seen = 0;
    if (!text || !out || len == 0u) return -1;
    for (size_t i = 0u; i < len; ++i) {
        char c = text[i];
        if (c < '0' || c > '9') return -1;
        uint32_t digit = (uint32_t)(c - '0');
        /* Detect overflow before it happens: v*10 + digit must still
         * fit in uint32_t. UINT32_MAX is 0xFFFFFFFFu = 4294967295.
         * Without this check, a manifest with payload_size set to
         * UINT32_MAX+k could wrap to a small value that bypasses the
         * CAPYPKG_PAYLOAD_MAX quota check downstream. */
        if (v > (0xFFFFFFFFu - digit) / 10u) {
            return -1;
        }
        v = v * 10u + digit;
        seen = 1;
    }
    if (!seen) return -1;
    *out = v;
    return 0;
}

static int line_is_separator(const char *line, size_t len) {
    return len == 3u && line[0] == '-' && line[1] == '-' && line[2] == '-';
}

/* Reject any value that contains a byte outside printable ASCII
 * (0x20-0x7E). The motivating threat is a manifest from a hostile
 * repository inserting ANSI escape sequences (e.g. ESC=0x1B) into
 * `summary`, `version` or `payload_url`. Those fields are echoed
 * verbatim to the framebuffer AND to the serial port by `vga_write`;
 * a terminal emulator attached to the serial port (xterm, tmux,
 * PuTTY) would interpret the escapes and could be used to clear the
 * screen, move the cursor and forge a fake shell prompt above
 * malicious commands. Rejecting at parse time means hostile bytes
 * never enter the in-memory catalog or the persisted db. */
static int value_is_printable_ascii(const char *value, size_t value_len) {
    for (size_t i = 0u; i < value_len; ++i) {
        unsigned char c = (unsigned char)value[i];
        if (c < 0x20u || c > 0x7Eu) {
            return 0;
        }
    }
    return 1;
}

/* Forward declaration: name_is_safe is defined alongside the other
 * field validators below, but append_dependency needs to reject
 * unsafe dep names eagerly so the catalog never holds entries with
 * a slot that can never resolve. */
static int name_is_safe(const char *name);

static void copy_field(char *dst, size_t dst_size,
                       const char *value, size_t value_len) {
    size_t copy_len = value_len;
    if (dst_size == 0u) return;
    if (copy_len + 1u > dst_size) {
        copy_len = dst_size - 1u;
    }
    for (size_t i = 0u; i < copy_len; ++i) {
        dst[i] = value[i];
    }
    dst[copy_len] = '\0';
}

static int append_dependency(struct capypkg_entry *entry,
                             const char *value, size_t value_len) {
    size_t cursor = 0u;
    while (cursor < value_len) {
        size_t end = cursor;
        while (end < value_len && value[end] != ',') {
            ++end;
        }
        size_t span = end - cursor;
        while (span > 0u &&
               (value[cursor] == ' ' || value[cursor] == '\t')) {
            ++cursor;
            --span;
        }
        while (span > 0u &&
               (value[cursor + span - 1u] == ' ' ||
                value[cursor + span - 1u] == '\t')) {
            --span;
        }
        if (span > 0u) {
            if (entry->dep_count >= CAPYPKG_MAX_DEPS) {
                return CAPYPKG_ERR_PARSE;
            }
            copy_field(entry->deps[entry->dep_count],
                       sizeof(entry->deps[entry->dep_count]),
                       &value[cursor], span);
            /* Apply the same alphabet rule as `name`: a dep entry
             * is concatenated unchanged into shell output and into
             * recursive install_dependencies lookups; an unsafe
             * dep would either fail to resolve or surface noise to
             * the audit trail. Reject at parse time so a malformed
             * manifest is rejected immediately rather than only
             * surfacing when install runs. */
            if (!name_is_safe(entry->deps[entry->dep_count])) {
                return CAPYPKG_ERR_DENIED;
            }
            ++entry->dep_count;
        }
        cursor = end + 1u;
    }
    return CAPYPKG_OK;
}

static int key_is(const char *key, size_t key_len, const char *expected) {
    size_t i = 0u;
    while (i < key_len && expected[i]) {
        if (key[i] != expected[i]) {
            return 0;
        }
        ++i;
    }
    return i == key_len && expected[i] == '\0';
}

static int apply_kv(struct capypkg_entry *entry,
                    const char *key, size_t key_len,
                    const char *value, size_t value_len) {
    /* Every value reaching this point is later either echoed to the
     * shell (and therefore to the serial port that a terminal
     * emulator may interpret) or written verbatim into the persisted
     * db/cache files. Reject any non-printable byte up front so a
     * hostile manifest cannot inject ANSI escapes or NULs into our
     * state. */
    if (!value_is_printable_ascii(value, value_len)) {
        return CAPYPKG_ERR_DENIED;
    }
    if (key_is(key, key_len, "name")) {
        copy_field(entry->name, sizeof(entry->name), value, value_len);
    } else if (key_is(key, key_len, "version")) {
        copy_field(entry->version, sizeof(entry->version), value, value_len);
    } else if (key_is(key, key_len, "summary")) {
        copy_field(entry->summary, sizeof(entry->summary), value, value_len);
    } else if (key_is(key, key_len, "payload_url")) {
        copy_field(entry->payload_url, sizeof(entry->payload_url),
                   value, value_len);
    } else if (key_is(key, key_len, "payload_sha256")) {
        copy_field(entry->payload_sha256, sizeof(entry->payload_sha256),
                   value, value_len);
    } else if (key_is(key, key_len, "payload_size")) {
        uint32_t size_value = 0u;
        if (parse_uint32(value, value_len, &size_value) != 0) {
            return CAPYPKG_ERR_PARSE;
        }
        if (size_value > CAPYPKG_PAYLOAD_MAX) {
            return CAPYPKG_ERR_QUOTA;
        }
        entry->size_bytes = size_value;
    } else if (key_is(key, key_len, "signature_ed25519")) {
        copy_field(entry->signature_hex, sizeof(entry->signature_hex),
                   value, value_len);
    } else if (key_is(key, key_len, "install_root")) {
        copy_field(entry->install_root, sizeof(entry->install_root),
                   value, value_len);
    } else if (key_is(key, key_len, "depends")) {
        return append_dependency(entry, value, value_len);
    } else if (key_is(key, key_len, "repo")) {
        copy_field(entry->source_repo, sizeof(entry->source_repo),
                   value, value_len);
    }
    /* Unknown keys are tolerated forward-compat; CapyAgent may add
     * fields the core does not consume yet. */
    return CAPYPKG_OK;
}

/* Package names are concatenated into install paths and on-disk
 * filenames (<install_root>/<name>.bin), so we restrict them to a
 * portable, traversal-safe alphabet. Allowed characters: a-z, A-Z,
 * 0-9, '.', '_', '-'. The bare string ".." is explicitly rejected
 * even though every char in it is in the alphabet, otherwise a
 * package called ".." would let an attacker write next to the
 * install_root parent. Empty name is rejected by the caller. */
static int name_is_safe(const char *name) {
    if (!name || !name[0]) return 0;
    /* Reject the dot-only names that have meaning on POSIX-like
     * filesystems: ".", "..", or any all-dots string. */
    int only_dots = 1;
    for (size_t i = 0u; name[i]; ++i) {
        char c = name[i];
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
    if (only_dots) return 0;
    return 1;
}

/* True if `path` contains a `..` path segment. A segment is a slash-
 * delimited piece, plus optional leading/trailing components. We
 * reject `..` at the very start, between any two slashes, and at the
 * very end. */
static int path_has_dotdot_segment(const char *path) {
    if (!path) return 0;
    size_t i = 0u;
    while (path[i]) {
        int at_segment_start = (i == 0u) || (path[i - 1u] == '/');
        if (at_segment_start && path[i] == '.' && path[i + 1u] == '.' &&
            (path[i + 2u] == '\0' || path[i + 2u] == '/')) {
            return 1;
        }
        ++i;
    }
    return 0;
}

/* True if `path` lives strictly under `prefix`. Either path == prefix
 * exactly, or path == prefix + "/...". A bare prefix match without a
 * directory boundary (e.g. prefix="/var/capypkg" matching
 * "/var/capypkgsneak") is rejected. */
static int path_is_under(const char *path, const char *prefix) {
    if (!capypkg_local_starts_with(path, prefix)) return 0;
    size_t prefix_len = capypkg_local_len(prefix);
    if (prefix_len == 0u) return 1;
    char boundary = path[prefix_len];
    if (boundary == '\0') return 1;
    if (boundary == '/') return 1;
    /* If the prefix already ends in '/', starts_with() guarantees the
     * boundary is correct. CAPYPKG_DIR_VAR is "/var/capypkg" (no
     * trailing slash), so this branch protects that case. */
    if (prefix[prefix_len - 1u] == '/') return 1;
    return 0;
}

static int validate_required_fields(struct capypkg_entry *entry) {
    if (!entry->name[0] || !entry->version[0] ||
        !entry->payload_url[0] || !entry->payload_sha256[0]) {
        return CAPYPKG_ERR_PARSE;
    }
    /* Name is concatenated into the on-disk install path; restrict it
     * to a portable, traversal-safe alphabet before anything else
     * consumes it (notably the default install_root builder below). */
    if (!name_is_safe(entry->name)) {
        return CAPYPKG_ERR_DENIED;
    }
    if (!capypkg_local_hex_string_valid(entry->payload_sha256,
                                        CAPYPKG_SHA256_HEX_LEN)) {
        return CAPYPKG_ERR_PARSE;
    }
    if (entry->signature_hex[0] &&
        !capypkg_local_hex_string_valid(entry->signature_hex,
                                        CAPYPKG_SIG_HEX_LEN)) {
        return CAPYPKG_ERR_PARSE;
    }
    /* Reject payload URLs that are not HTTPS. */
    if (!capypkg_local_starts_with(entry->payload_url, "https://")) {
        return CAPYPKG_ERR_DENIED;
    }
    /* install_root must be absolute and live under /var/capypkg or /opt. */
    if (!entry->install_root[0]) {
        capypkg_local_copy(entry->install_root,
                           CAPYPKG_PATH_MAX, CAPYPKG_DIR_VAR);
        capypkg_local_append(entry->install_root,
                             CAPYPKG_PATH_MAX, "/");
        capypkg_local_append(entry->install_root,
                             CAPYPKG_PATH_MAX, entry->name);
    }
    if (entry->install_root[0] != '/') {
        return CAPYPKG_ERR_DENIED;
    }
    /* Reject any path that escapes the allowed roots, either by
     * prefix-bypass (/var/capypkgsneak) or by `..` traversal. */
    if (path_has_dotdot_segment(entry->install_root)) {
        return CAPYPKG_ERR_DENIED;
    }
    if (!(path_is_under(entry->install_root, CAPYPKG_DIR_VAR) ||
          path_is_under(entry->install_root, "/opt/"))) {
        return CAPYPKG_ERR_DENIED;
    }
    return CAPYPKG_OK;
}

/* Advance `cursor` past every remaining line of the current entry,
 * stopping just past the next `---\n` separator (or at EOF). Used to
 * skip a malformed descriptor without halting the surrounding parser
 * loop in capypkg_db_parse / capypkg_fetch_index. */
static size_t skip_to_next_entry(const char *text, size_t len,
                                 size_t cursor) {
    while (cursor < len) {
        size_t line_start = cursor;
        size_t line_end = cursor;
        while (line_end < len && text[line_end] != '\n') {
            ++line_end;
        }
        size_t line_len = line_end - line_start;
        size_t advance = line_end < len ? line_end + 1u : len;
        if (line_is_separator(&text[line_start], line_len)) {
            return advance;
        }
        cursor = advance;
    }
    return cursor;
}

int capypkg_manifest_parse_entry(const char *text, size_t len,
                                 struct capypkg_entry *entry,
                                 size_t *consumed) {
    size_t cursor = 0u;
    if (!text || !entry) {
        if (consumed) *consumed = 0u;
        return CAPYPKG_ERR_INVALID_ARG;
    }
    capypkg_local_zero(entry, sizeof(*entry));
    entry->state = CAPYPKG_STATE_AVAILABLE;

    while (cursor < len) {
        size_t line_start = cursor;
        size_t line_end = cursor;
        while (line_end < len && text[line_end] != '\n') {
            ++line_end;
        }
        size_t line_len = line_end - line_start;
        size_t advance = line_end < len ? line_end + 1u : len;

        if (line_is_separator(&text[line_start], line_len)) {
            cursor = advance;
            break;
        }

        /* Skip blank lines and full-line comments (#). */
        if (line_len == 0u || text[line_start] == '#') {
            cursor = advance;
            continue;
        }

        size_t eq = line_start;
        while (eq < line_end && text[eq] != '=') {
            ++eq;
        }
        if (eq >= line_end) {
            cursor = advance;
            continue;
        }
        const char *key = &text[line_start];
        size_t key_len = eq - line_start;
        const char *value = &text[eq + 1u];
        size_t value_len = line_end - (eq + 1u);

        int rc = apply_kv(entry, key, key_len, value, value_len);
        if (rc < 0) {
            /* Report progress past the rest of the malformed entry so
             * the surrounding loop can skip it and continue with the
             * next descriptor instead of breaking out entirely. */
            if (consumed) *consumed = skip_to_next_entry(text, len, advance);
            return rc;
        }
        cursor = advance;
    }

    int rc = validate_required_fields(entry);
    if (rc != CAPYPKG_OK) {
        if (consumed) *consumed = cursor;
        return rc;
    }

    if (consumed) {
        *consumed = cursor;
    }
    return CAPYPKG_OK;
}

int capypkg_verify_payload(const struct capypkg_entry *entry,
                           const uint8_t *payload, size_t payload_len) {
    uint8_t digest[SHA256_DIGEST_SIZE];
    char digest_hex[CAPYPKG_SHA256_HEX_MAX];
    uint8_t expected[32];

    if (!entry || !payload || payload_len == 0u) {
        return CAPYPKG_ERR_INVALID_ARG;
    }
    diag_clear();
    if (entry->size_bytes && (uint32_t)payload_len != entry->size_bytes) {
        diag_set_size_mismatch(payload_len, entry->size_bytes);
        return CAPYPKG_ERR_DIGEST;
    }
    if (decode_hex64(entry->payload_sha256, expected) != 0) {
        return CAPYPKG_ERR_DIGEST;
    }
    sha256_hash(payload, payload_len, digest);
    sha256_hex(digest, digest_hex);
    if (!capypkg_local_equal(digest_hex, entry->payload_sha256)) {
        diag_set_sha_mismatch(digest_hex, entry->payload_sha256);
        /* Constant-style compare not strictly necessary here (hash is
         * not a secret), but we go through capypkg_local_equal for
         * uniformity with the rest of the adapter. */
        for (size_t i = 0u; i < 32u; ++i) {
            if (digest[i] != expected[i]) {
                return CAPYPKG_ERR_DIGEST;
            }
        }
    }
    return CAPYPKG_OK;
}
