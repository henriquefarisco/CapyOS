/*
 * src/services/capypkg/capypkg_state.c
 *
 * Singleton state, init/reset, accessors and shared low-level helpers
 * for the CapyOS package adapter. Manifest parsing, repository
 * persistence and install/remove logic live in sister TUs.
 */

#include "internal/capypkg_internal.h"

#include <stddef.h>
#include <stdint.h>

struct capypkg_runtime g_capypkg;

capypkg_read_file_fn   g_capypkg_reader = NULL;
capypkg_write_text_fn  g_capypkg_writer = NULL;
capypkg_write_bytes_fn g_capypkg_bytes_writer = NULL;
capypkg_remove_file_fn g_capypkg_remover = NULL;
capypkg_mkdir_fn       g_capypkg_mkdir = NULL;

capypkg_fetch_text_fn          g_capypkg_text_fetcher = NULL;
capypkg_fetch_bytes_fn         g_capypkg_bytes_fetcher = NULL;
capypkg_fetch_bytes_progress_fn g_capypkg_bytes_fetcher_progress = NULL;
capypkg_verify_signature_fn    g_capypkg_signature_verifier = NULL;

capypkg_install_progress_fn g_capypkg_install_observer = NULL;
void                       *g_capypkg_install_observer_ctx = NULL;

void capypkg_emit_install_phase(const char *name,
                                enum capypkg_install_phase phase,
                                uint64_t cur, uint64_t total) {
    if (g_capypkg_install_observer) {
        g_capypkg_install_observer(name ? name : "", phase, cur, total,
                                   g_capypkg_install_observer_ctx);
    }
}

const char *const CAPYPKG_DEFAULT_REPO_NAME = "stable";
const char *const CAPYPKG_DEFAULT_REPO_URL =
    "https://repo.capyos.org/capypkg/v1/index.cap";

void capypkg_local_zero(void *ptr, size_t len) {
    uint8_t *dst = (uint8_t *)ptr;
    while (len--) {
        *dst++ = 0u;
    }
}

void capypkg_local_copy(char *dst, size_t dst_size, const char *src) {
    size_t i = 0u;
    if (!dst || dst_size == 0u) {
        return;
    }
    if (src) {
        while (src[i] && i + 1u < dst_size) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = '\0';
}

void capypkg_local_append(char *dst, size_t dst_size, const char *src) {
    size_t len = 0u;
    size_t i = 0u;
    if (!dst || dst_size == 0u || !src) {
        return;
    }
    while (dst[len] && len + 1u < dst_size) {
        ++len;
    }
    while (src[i] && len + 1u < dst_size) {
        dst[len++] = src[i++];
    }
    dst[len] = '\0';
}

size_t capypkg_local_len(const char *text) {
    size_t n = 0u;
    if (!text) {
        return 0u;
    }
    while (text[n]) {
        ++n;
    }
    return n;
}

int capypkg_local_equal(const char *a, const char *b) {
    size_t i = 0u;
    if (!a || !b) {
        return 0;
    }
    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return 0;
        }
        ++i;
    }
    return a[i] == b[i];
}

int capypkg_local_starts_with(const char *text, const char *prefix) {
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

int capypkg_local_is_hex_digit(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

int capypkg_local_hex_string_valid(const char *text, size_t hex_len) {
    size_t i = 0u;
    if (!text || !text[0]) {
        return 0;
    }
    while (i < hex_len) {
        if (!capypkg_local_is_hex_digit(text[i])) {
            return 0;
        }
        ++i;
    }
    return text[i] == '\0';
}

void capypkg_reset(void) {
    capypkg_local_zero(&g_capypkg, sizeof(g_capypkg));
    g_capypkg_reader = NULL;
    g_capypkg_writer = NULL;
    g_capypkg_bytes_writer = NULL;
    g_capypkg_remover = NULL;
    g_capypkg_mkdir = NULL;
    g_capypkg_text_fetcher = NULL;
    g_capypkg_bytes_fetcher = NULL;
    g_capypkg_bytes_fetcher_progress = NULL;
    g_capypkg_signature_verifier = NULL;
    g_capypkg_install_observer = NULL;
    g_capypkg_install_observer_ctx = NULL;
}

static void seed_default_repository(void) {
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

int capypkg_init(void) {
    if (g_capypkg.initialized) {
        return CAPYPKG_OK;
    }
    capypkg_local_zero(&g_capypkg, sizeof(g_capypkg));
    seed_default_repository();
    /* Best-effort: persisted state is restored only when readers
     * exist (kernel runtime). Failures are non-fatal: the system
     * still boots into the shell with the seeded default repo. */
    (void)capypkg_repo_load();
    (void)capypkg_db_load();
    (void)capypkg_catalog_restore();
    g_capypkg.initialized = 1u;
    return CAPYPKG_OK;
}

int capypkg_initialized(void) {
    return g_capypkg.initialized ? 1 : 0;
}

void capypkg_set_reader(capypkg_read_file_fn fn) { g_capypkg_reader = fn; }
void capypkg_set_writer(capypkg_write_text_fn fn) { g_capypkg_writer = fn; }
void capypkg_set_bytes_writer(capypkg_write_bytes_fn fn) {
    g_capypkg_bytes_writer = fn;
}
void capypkg_set_remover(capypkg_remove_file_fn fn) { g_capypkg_remover = fn; }
void capypkg_set_mkdir(capypkg_mkdir_fn fn) { g_capypkg_mkdir = fn; }

void capypkg_set_text_fetcher(capypkg_fetch_text_fn fn) {
    g_capypkg_text_fetcher = fn;
}
void capypkg_set_bytes_fetcher(capypkg_fetch_bytes_fn fn) {
    g_capypkg_bytes_fetcher = fn;
}
void capypkg_set_bytes_fetcher_progress(capypkg_fetch_bytes_progress_fn fn) {
    g_capypkg_bytes_fetcher_progress = fn;
}
void capypkg_set_signature_verifier(capypkg_verify_signature_fn fn) {
    g_capypkg_signature_verifier = fn;
}
void capypkg_set_install_observer(capypkg_install_progress_fn fn, void *ctx) {
    g_capypkg_install_observer = fn;
    g_capypkg_install_observer_ctx = ctx;
}

struct capypkg_entry *capypkg_find_installed(const char *name) {
    if (!name || !name[0]) {
        return NULL;
    }
    for (uint32_t i = 0u; i < g_capypkg.installed_count; ++i) {
        if (capypkg_local_equal(name, g_capypkg.installed[i].name)) {
            return &g_capypkg.installed[i];
        }
    }
    return NULL;
}

struct capypkg_entry *capypkg_find_available(const char *name) {
    if (!name || !name[0]) {
        return NULL;
    }
    for (uint32_t i = 0u; i < g_capypkg.available_count; ++i) {
        if (capypkg_local_equal(name, g_capypkg.available[i].name)) {
            return &g_capypkg.available[i];
        }
    }
    return NULL;
}

struct capypkg_repo *capypkg_find_repo(const char *name) {
    if (!name || !name[0]) {
        return NULL;
    }
    for (uint32_t i = 0u; i < g_capypkg.repo_count; ++i) {
        if (capypkg_local_equal(name, g_capypkg.repos[i].name)) {
            return &g_capypkg.repos[i];
        }
    }
    return NULL;
}

size_t capypkg_repo_count(void) {
    return (size_t)g_capypkg.repo_count;
}

int capypkg_repo_get_at(size_t idx, struct capypkg_repo *out) {
    if (!out || idx >= g_capypkg.repo_count) {
        return CAPYPKG_ERR_INVALID_ARG;
    }
    *out = g_capypkg.repos[idx];
    return CAPYPKG_OK;
}

size_t capypkg_available_count(void) {
    return (size_t)g_capypkg.available_count;
}

int capypkg_available_get_at(size_t idx, struct capypkg_entry *out) {
    if (!out || idx >= g_capypkg.available_count) {
        return CAPYPKG_ERR_INVALID_ARG;
    }
    *out = g_capypkg.available[idx];
    return CAPYPKG_OK;
}

int capypkg_available_get(const char *name, struct capypkg_entry *out) {
    struct capypkg_entry *found = capypkg_find_available(name);
    if (!out) {
        return CAPYPKG_ERR_INVALID_ARG;
    }
    if (!found) {
        return CAPYPKG_ERR_NOT_FOUND;
    }
    *out = *found;
    return CAPYPKG_OK;
}

size_t capypkg_installed_count(void) {
    return (size_t)g_capypkg.installed_count;
}

int capypkg_installed_get_at(size_t idx, struct capypkg_entry *out) {
    if (!out || idx >= g_capypkg.installed_count) {
        return CAPYPKG_ERR_INVALID_ARG;
    }
    *out = g_capypkg.installed[idx];
    return CAPYPKG_OK;
}

int capypkg_installed_get(const char *name, struct capypkg_entry *out) {
    struct capypkg_entry *found = capypkg_find_installed(name);
    if (!out) {
        return CAPYPKG_ERR_INVALID_ARG;
    }
    if (!found) {
        return CAPYPKG_ERR_NOT_FOUND;
    }
    *out = *found;
    return CAPYPKG_OK;
}

void capypkg_stats_get(struct capypkg_stats *out) {
    if (!out) {
        return;
    }
    out->installed_count = g_capypkg.installed_count;
    out->available_count = g_capypkg.available_count;
    out->repo_count = g_capypkg.repo_count;
    out->any_repo_signed = g_capypkg.any_repo_signed;
    out->catalog_fresh = g_capypkg.catalog_fresh;
    out->initialized = g_capypkg.initialized;
    /* updates_pending = available[name].version > installed[name].version. */
    uint32_t pending = 0u;
    for (uint32_t i = 0u; i < g_capypkg.installed_count; ++i) {
        const struct capypkg_entry *inst = &g_capypkg.installed[i];
        const struct capypkg_entry *avail = capypkg_find_available(inst->name);
        if (avail && !capypkg_local_equal(avail->version, inst->version)) {
            ++pending;
        }
    }
    out->updates_pending = pending;
}

const char *capypkg_state_label(uint8_t state) {
    switch (state) {
    case CAPYPKG_STATE_AVAILABLE: return "available";
    case CAPYPKG_STATE_FETCHING:  return "fetching";
    case CAPYPKG_STATE_VERIFYING: return "verifying";
    case CAPYPKG_STATE_STAGED:    return "staged";
    case CAPYPKG_STATE_INSTALLED: return "installed";
    case CAPYPKG_STATE_REMOVING:  return "removing";
    case CAPYPKG_STATE_BROKEN:    return "broken";
    default:                      return "unknown";
    }
}

const char *capypkg_result_label(int rc) {
    switch (rc) {
    case CAPYPKG_OK:              return "ok";
    case CAPYPKG_ERR_INVALID_ARG: return "invalid-argument";
    case CAPYPKG_ERR_NOT_READY:   return "not-ready";
    case CAPYPKG_ERR_NOT_FOUND:   return "not-found";
    case CAPYPKG_ERR_ALREADY:     return "already-installed";
    case CAPYPKG_ERR_NO_SOURCE:   return "no-repository-configured";
    case CAPYPKG_ERR_FETCH:       return "fetch-failed";
    case CAPYPKG_ERR_PARSE:       return "manifest-parse-error";
    case CAPYPKG_ERR_DIGEST:      return "payload-digest-mismatch";
    case CAPYPKG_ERR_SIGNATURE:   return "signature-mismatch";
    case CAPYPKG_ERR_STORAGE:     return "storage-write-failed";
    case CAPYPKG_ERR_DEPENDENCY:  return "dependency-unresolved";
    case CAPYPKG_ERR_QUOTA:       return "quota-exceeded";
    case CAPYPKG_ERR_DENIED:      return "policy-denied";
    default:                      return "error";
    }
}
