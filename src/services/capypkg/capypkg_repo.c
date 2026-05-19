/*
 * src/services/capypkg/capypkg_repo.c
 *
 * Repository configuration management and persistence for the CapyOS
 * package adapter. Repos are persisted in /system/capypkg/repos.cfg
 * as line-oriented `name|index_url|require_signature|pinned` entries.
 *
 * The default repository is seeded by capypkg_init() on a clean
 * system. capypkg_repo_load() reads the persisted config when a
 * reader is bound; capypkg_repo_save() writes it through the bound
 * writer.
 */

#include "internal/capypkg_internal.h"
#include "kernel/log/klog.h"

#include <stddef.h>
#include <stdint.h>

#define REPO_BUFFER_BYTES 1024u

static int append_uint(char *buf, size_t buf_size, size_t *pos, uint32_t v) {
    char digits[12];
    size_t n = 0u;
    if (!buf || !pos) return -1;
    if (v == 0u) {
        if (*pos + 1u >= buf_size) return -1;
        buf[(*pos)++] = '0';
        buf[*pos] = '\0';
        return 0;
    }
    while (v > 0u && n < sizeof(digits)) {
        digits[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (n--) {
        if (*pos + 1u >= buf_size) return -1;
        buf[(*pos)++] = digits[n];
    }
    buf[*pos] = '\0';
    return 0;
}

static int append_str(char *buf, size_t buf_size, size_t *pos,
                      const char *src) {
    size_t i = 0u;
    if (!buf || !pos || !src) return -1;
    while (src[i]) {
        if (*pos + 1u >= buf_size) return -1;
        buf[(*pos)++] = src[i++];
    }
    buf[*pos] = '\0';
    return 0;
}

int capypkg_repo_serialize(char *buffer, size_t buffer_size, size_t *out_len) {
    size_t pos = 0u;
    if (!buffer || buffer_size == 0u) {
        return CAPYPKG_ERR_INVALID_ARG;
    }
    buffer[0] = '\0';
    if (append_str(buffer, buffer_size, &pos,
                   "# capypkg repos.cfg v1\n") != 0) {
        return CAPYPKG_ERR_STORAGE;
    }
    for (uint32_t i = 0u; i < g_capypkg.repo_count; ++i) {
        const struct capypkg_repo *repo = &g_capypkg.repos[i];
        if (!repo->name[0]) continue;
        if (append_str(buffer, buffer_size, &pos, repo->name) != 0)
            return CAPYPKG_ERR_STORAGE;
        if (append_str(buffer, buffer_size, &pos, "|") != 0)
            return CAPYPKG_ERR_STORAGE;
        if (append_str(buffer, buffer_size, &pos, repo->index_url) != 0)
            return CAPYPKG_ERR_STORAGE;
        if (append_str(buffer, buffer_size, &pos, "|") != 0)
            return CAPYPKG_ERR_STORAGE;
        if (append_uint(buffer, buffer_size, &pos,
                        repo->require_signature) != 0)
            return CAPYPKG_ERR_STORAGE;
        if (append_str(buffer, buffer_size, &pos, "|") != 0)
            return CAPYPKG_ERR_STORAGE;
        if (append_uint(buffer, buffer_size, &pos, repo->pinned) != 0)
            return CAPYPKG_ERR_STORAGE;
        if (append_str(buffer, buffer_size, &pos, "\n") != 0)
            return CAPYPKG_ERR_STORAGE;
    }
    if (out_len) {
        *out_len = pos;
    }
    return CAPYPKG_OK;
}

static int read_field(const char *text, size_t len, size_t *cursor,
                      char *out, size_t out_size, char delim) {
    size_t start = *cursor;
    while (*cursor < len && text[*cursor] != delim) {
        ++(*cursor);
    }
    size_t span = *cursor - start;
    if (span + 1u > out_size) {
        span = out_size - 1u;
    }
    for (size_t i = 0u; i < span; ++i) {
        out[i] = text[start + i];
    }
    out[span] = '\0';
    if (*cursor < len && text[*cursor] == delim) {
        ++(*cursor);
    }
    return 0;
}

/* True if every byte of the null-terminated string `s` is printable
 * ASCII (0x20-0x7E). Used to reject `repos.cfg` entries whose `name`
 * or `index_url` field carries ANSI escapes or other control bytes;
 * the same threat model that motivated the value_is_printable_ascii
 * gate in capypkg_manifest.c applies here, except the input source
 * is the on-disk config rather than a remote index. */
static int repo_string_printable(const char *s) {
    if (!s) return 0;
    for (size_t i = 0u; s[i]; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20u || c > 0x7Eu) {
            return 0;
        }
    }
    return 1;
}

int capypkg_repo_parse(const char *text, size_t len) {
    size_t cursor = 0u;
    if (!text) {
        return CAPYPKG_ERR_INVALID_ARG;
    }
    g_capypkg.repo_count = 0u;
    g_capypkg.any_repo_signed = 0u;
    while (cursor < len && g_capypkg.repo_count < CAPYPKG_MAX_REPOS) {
        size_t line_start = cursor;
        while (cursor < len && text[cursor] != '\n') {
            ++cursor;
        }
        size_t line_end = cursor;
        if (cursor < len) ++cursor;
        if (line_end == line_start || text[line_start] == '#') {
            continue;
        }
        size_t field_cursor = line_start;
        struct capypkg_repo repo;
        capypkg_local_zero(&repo, sizeof(repo));
        char require_buf[8];
        char pinned_buf[8];
        read_field(text, line_end, &field_cursor, repo.name,
                   sizeof(repo.name), '|');
        read_field(text, line_end, &field_cursor, repo.index_url,
                   sizeof(repo.index_url), '|');
        read_field(text, line_end, &field_cursor, require_buf,
                   sizeof(require_buf), '|');
        read_field(text, line_end, &field_cursor, pinned_buf,
                   sizeof(pinned_buf), '\n');
        if (!repo.name[0] || !repo.index_url[0]) {
            continue;
        }
        /* On-disk repos.cfg is trusted in steady state but may be
         * tampered with by an attacker who has filesystem access. A
         * non-printable byte in name or index_url would later be
         * echoed by pkg-source-list to the serial port, where a
         * terminal emulator could interpret it as an ANSI escape. */
        if (!repo_string_printable(repo.name) ||
            !repo_string_printable(repo.index_url)) {
            continue;
        }
        if (!capypkg_local_starts_with(repo.index_url, "https://")) {
            continue;
        }
        repo.require_signature = (require_buf[0] == '1') ? 1u : 0u;
        repo.pinned = (pinned_buf[0] == '1') ? 1u : 0u;
        g_capypkg.repos[g_capypkg.repo_count++] = repo;
        if (repo.require_signature) {
            g_capypkg.any_repo_signed = 1u;
        }
    }
    return CAPYPKG_OK;
}

int capypkg_repo_save(void) {
    char buffer[REPO_BUFFER_BYTES];
    size_t len = 0u;
    if (!g_capypkg_writer) {
        return CAPYPKG_ERR_NOT_READY;
    }
    int rc = capypkg_repo_serialize(buffer, sizeof(buffer), &len);
    if (rc != CAPYPKG_OK) {
        return rc;
    }
    if (g_capypkg_mkdir) {
        (void)g_capypkg_mkdir(CAPYPKG_DIR_SYSTEM);
    }
    if (g_capypkg_writer(CAPYPKG_REPOS_FILE, buffer) != 0) {
        return CAPYPKG_ERR_STORAGE;
    }
    return CAPYPKG_OK;
}

int capypkg_repo_load(void) {
    char buffer[REPO_BUFFER_BYTES];
    size_t len = 0u;
    if (!g_capypkg_reader) {
        return CAPYPKG_ERR_NOT_READY;
    }
    if (g_capypkg_reader(CAPYPKG_REPOS_FILE, buffer, sizeof(buffer),
                         &len) != 0) {
        return CAPYPKG_ERR_NOT_FOUND;
    }
    int rc = capypkg_repo_parse(buffer, len);
    if (rc == CAPYPKG_OK && g_capypkg.repo_count == 0u) {
        /* On a freshly loaded but empty config keep the seeded
         * default so the user never sees a system without any
         * repository. */
        struct capypkg_repo *repo = &g_capypkg.repos[0];
        capypkg_local_zero(repo, sizeof(*repo));
        capypkg_local_copy(repo->name, sizeof(repo->name),
                           CAPYPKG_DEFAULT_REPO_NAME);
        capypkg_local_copy(repo->index_url, sizeof(repo->index_url),
                           CAPYPKG_DEFAULT_REPO_URL);
        repo->pinned = 1u;
        repo->require_signature = 1u;
        g_capypkg.repo_count = 1u;
        g_capypkg.any_repo_signed = 1u;
    }
    return rc;
}

int capypkg_repo_add(const char *name, const char *index_url,
                     int require_signature) {
    if (!name || !name[0] || !index_url || !index_url[0]) {
        return CAPYPKG_ERR_INVALID_ARG;
    }
    if (!capypkg_local_starts_with(index_url, "https://")) {
        return CAPYPKG_ERR_DENIED;
    }
    if (capypkg_local_len(name) >= CAPYPKG_REPO_NAME_MAX ||
        capypkg_local_len(index_url) >= CAPYPKG_URL_MAX) {
        return CAPYPKG_ERR_INVALID_ARG;
    }
    /* Defense in depth against ANSI escape injection: even though
     * the shell normally only forwards printable bytes through argv,
     * a piped or scripted caller could feed control bytes. Refusing
     * here keeps the on-disk repos.cfg clean even before the
     * load-side check fires on the next boot. */
    if (!repo_string_printable(name) || !repo_string_printable(index_url)) {
        return CAPYPKG_ERR_DENIED;
    }
    struct capypkg_repo *existing = capypkg_find_repo(name);
    if (existing) {
        capypkg_local_copy(existing->index_url, sizeof(existing->index_url),
                           index_url);
        existing->require_signature = require_signature ? 1u : 0u;
        /* Recompute any_repo_signed from scratch: the existing repo
         * may have transitioned from signed -> unsigned, in which
         * case the incremental update path used to leave the flag
         * stale. capypkg_stats_get and signature_required (fallback
         * path) read this directly. */
        g_capypkg.any_repo_signed = 0u;
        for (uint32_t i = 0u; i < g_capypkg.repo_count; ++i) {
            if (g_capypkg.repos[i].require_signature) {
                g_capypkg.any_repo_signed = 1u;
                break;
            }
        }
        int save_rc = capypkg_repo_save();
        if (save_rc == CAPYPKG_ERR_NOT_READY) {
            save_rc = CAPYPKG_OK;
        }
        if (save_rc != CAPYPKG_OK) {
            klog(KLOG_WARN,
                 "[audit] [capypkg] repository updated but db persistence failed");
            return save_rc;
        }
        klog(KLOG_INFO, "[audit] [capypkg] repository updated");
        return save_rc;
    }
    if (g_capypkg.repo_count >= CAPYPKG_MAX_REPOS) {
        return CAPYPKG_ERR_QUOTA;
    }
    struct capypkg_repo *repo = &g_capypkg.repos[g_capypkg.repo_count];
    capypkg_local_zero(repo, sizeof(*repo));
    capypkg_local_copy(repo->name, sizeof(repo->name), name);
    capypkg_local_copy(repo->index_url, sizeof(repo->index_url), index_url);
    repo->require_signature = require_signature ? 1u : 0u;
    repo->pinned = 0u;
    if (repo->require_signature) {
        g_capypkg.any_repo_signed = 1u;
    }
    ++g_capypkg.repo_count;
    int save_rc = capypkg_repo_save();
    if (save_rc == CAPYPKG_ERR_NOT_READY) {
        save_rc = CAPYPKG_OK;
    }
    if (save_rc != CAPYPKG_OK) {
        klog(KLOG_WARN,
             "[audit] [capypkg] repository added but db persistence failed");
        return save_rc;
    }
    klog(KLOG_INFO, "[audit] [capypkg] repository added");
    return save_rc;
}

int capypkg_repo_remove(const char *name) {
    if (!name || !name[0]) {
        return CAPYPKG_ERR_INVALID_ARG;
    }
    int found = -1;
    for (uint32_t i = 0u; i < g_capypkg.repo_count; ++i) {
        if (capypkg_local_equal(name, g_capypkg.repos[i].name)) {
            if (g_capypkg.repos[i].pinned) {
                return CAPYPKG_ERR_DENIED;
            }
            found = (int)i;
            break;
        }
    }
    if (found < 0) {
        return CAPYPKG_ERR_NOT_FOUND;
    }
    for (uint32_t i = (uint32_t)found; i + 1u < g_capypkg.repo_count; ++i) {
        g_capypkg.repos[i] = g_capypkg.repos[i + 1u];
    }
    --g_capypkg.repo_count;
    capypkg_local_zero(&g_capypkg.repos[g_capypkg.repo_count],
                       sizeof(g_capypkg.repos[g_capypkg.repo_count]));
    g_capypkg.any_repo_signed = 0u;
    for (uint32_t i = 0u; i < g_capypkg.repo_count; ++i) {
        if (g_capypkg.repos[i].require_signature) {
            g_capypkg.any_repo_signed = 1u;
            break;
        }
    }
    int save_rc = capypkg_repo_save();
    if (save_rc == CAPYPKG_ERR_NOT_READY) {
        save_rc = CAPYPKG_OK;
    }
    if (save_rc != CAPYPKG_OK) {
        klog(KLOG_WARN,
             "[audit] [capypkg] repository removed but db persistence failed");
        return save_rc;
    }
    klog(KLOG_INFO, "[audit] [capypkg] repository removed");
    return save_rc;
}
