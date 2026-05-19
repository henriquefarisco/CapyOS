/*
 * src/services/capypkg/capypkg_install.c
 *
 * Index fetch, install, remove and update operations for the CapyOS
 * package adapter.
 *
 * High-level flow for `capypkg_install(name)`:
 *
 *   1. Resolve <name> against the available catalog. If the catalog
 *      is stale, refuse with CAPYPKG_ERR_NOT_READY (caller decides
 *      whether to fetch_index first).
 *   2. Walk dependencies recursively; install missing deps first.
 *   3. Stream the payload through the bound bytes fetcher
 *      (HTTPS only).
 *   4. Verify SHA-256 against the manifest.
 *   5. Verify Ed25519 signature when the source repository requires
 *      it (default).
 *   6. Stage the bytes under <install_root>/<name>.bin.
 *   7. Append/replace the entry in the installed table and persist
 *      the DB.
 *
 * The kernel never executes the staged bytes from this path. Future
 * stages will introduce a sandboxed loader that consumes
 * /var/capypkg/<name>/ contents.
 */

#include "internal/capypkg_internal.h"
#include "kernel/log/klog.h"

#include <stddef.h>
#include <stdint.h>

#define CAPYPKG_INDEX_BUFFER_BYTES 16384u
#define CAPYPKG_DB_BUFFER_BYTES    8192u

static int has_text_fetcher(void) {
    return g_capypkg_text_fetcher != NULL;
}

static int has_bytes_fetcher(void) {
    return g_capypkg_bytes_fetcher != NULL;
}

static int has_writer(void) {
    return g_capypkg_writer != NULL;
}

static int has_bytes_writer(void) {
    return g_capypkg_bytes_writer != NULL;
}

static int has_remover(void) {
    return g_capypkg_remover != NULL;
}

static void rebuild_signed_flag(void) {
    g_capypkg.any_repo_signed = 0u;
    for (uint32_t i = 0u; i < g_capypkg.repo_count; ++i) {
        if (g_capypkg.repos[i].require_signature) {
            g_capypkg.any_repo_signed = 1u;
            return;
        }
    }
}

static int signature_required(const struct capypkg_entry *entry) {
    if (!entry) return 0;
    if (entry->source_repo[0]) {
        struct capypkg_repo *repo = capypkg_find_repo(entry->source_repo);
        if (repo) return repo->require_signature ? 1 : 0;
    }
    rebuild_signed_flag();
    return g_capypkg.any_repo_signed ? 1 : 0;
}

/* Canonical signed descriptor: deterministic concatenation of the
 * package identity fields. The repo signs this string so the
 * signature simultaneously binds the package name, version, payload
 * URL and payload SHA-256. Because SHA-256 in turn binds the payload
 * bytes, a valid signature attests "this exact package identity ->
 * exactly these bytes" without trusting the transport. Format:
 *
 *   name=<n>|version=<v>|payload_sha256=<h>|payload_url=<u>\n
 */
static int build_signed_descriptor(const struct capypkg_entry *entry,
                                   char *out, size_t out_size,
                                   size_t *out_len) {
    if (!entry || !out || out_size == 0u) {
        return -1;
    }
    out[0] = '\0';
    capypkg_local_append(out, out_size, "name=");
    capypkg_local_append(out, out_size, entry->name);
    capypkg_local_append(out, out_size, "|version=");
    capypkg_local_append(out, out_size, entry->version);
    capypkg_local_append(out, out_size, "|payload_sha256=");
    capypkg_local_append(out, out_size, entry->payload_sha256);
    capypkg_local_append(out, out_size, "|payload_url=");
    capypkg_local_append(out, out_size, entry->payload_url);
    capypkg_local_append(out, out_size, "\n");
    if (out_len) {
        *out_len = capypkg_local_len(out);
    }
    return 0;
}

static int verify_signature_if_required(const struct capypkg_entry *entry) {
    if (!signature_required(entry)) {
        return CAPYPKG_OK;
    }
    if (!entry->signature_hex[0]) {
        return CAPYPKG_ERR_SIGNATURE;
    }
    if (!g_capypkg_signature_verifier) {
        /* Without a verifier installed we must fail closed. */
        return CAPYPKG_ERR_SIGNATURE;
    }
    char descriptor[CAPYPKG_NAME_MAX + CAPYPKG_VERSION_MAX +
                    CAPYPKG_SHA256_HEX_MAX + CAPYPKG_URL_MAX + 64u];
    size_t descriptor_len = 0u;
    if (build_signed_descriptor(entry, descriptor, sizeof(descriptor),
                                &descriptor_len) != 0) {
        return CAPYPKG_ERR_SIGNATURE;
    }
    int rc = g_capypkg_signature_verifier(descriptor, descriptor_len,
                                          entry->signature_hex);
    return rc == 0 ? CAPYPKG_OK : CAPYPKG_ERR_SIGNATURE;
}

static int append_available(const struct capypkg_entry *entry) {
    if (g_capypkg.available_count >= CAPYPKG_MAX_AVAILABLE) {
        return CAPYPKG_ERR_QUOTA;
    }
    g_capypkg.available[g_capypkg.available_count++] = *entry;
    return CAPYPKG_OK;
}

static int append_or_replace_installed(const struct capypkg_entry *entry) {
    struct capypkg_entry *existing = capypkg_find_installed(entry->name);
    if (existing) {
        *existing = *entry;
        existing->state = CAPYPKG_STATE_INSTALLED;
        return CAPYPKG_OK;
    }
    if (g_capypkg.installed_count >= CAPYPKG_MAX_INSTALLED) {
        return CAPYPKG_ERR_QUOTA;
    }
    g_capypkg.installed[g_capypkg.installed_count] = *entry;
    g_capypkg.installed[g_capypkg.installed_count].state =
        CAPYPKG_STATE_INSTALLED;
    ++g_capypkg.installed_count;
    return CAPYPKG_OK;
}

static int remove_installed_at(uint32_t idx) {
    if (idx >= g_capypkg.installed_count) {
        return CAPYPKG_ERR_NOT_FOUND;
    }
    for (uint32_t i = idx; i + 1u < g_capypkg.installed_count; ++i) {
        g_capypkg.installed[i] = g_capypkg.installed[i + 1u];
    }
    --g_capypkg.installed_count;
    capypkg_local_zero(&g_capypkg.installed[g_capypkg.installed_count],
                       sizeof(g_capypkg.installed[g_capypkg.installed_count]));
    return CAPYPKG_OK;
}

int capypkg_db_serialize(char *buffer, size_t buffer_size, size_t *out_len) {
    size_t pos = 0u;
    if (!buffer || buffer_size == 0u) {
        return CAPYPKG_ERR_INVALID_ARG;
    }
    buffer[0] = '\0';
    capypkg_local_append(buffer, buffer_size, "# capypkg db.idx v1\n");
    pos = capypkg_local_len(buffer);
    for (uint32_t i = 0u; i < g_capypkg.installed_count; ++i) {
        const struct capypkg_entry *e = &g_capypkg.installed[i];
        capypkg_local_append(buffer, buffer_size, "name=");
        capypkg_local_append(buffer, buffer_size, e->name);
        capypkg_local_append(buffer, buffer_size, "\nversion=");
        capypkg_local_append(buffer, buffer_size, e->version);
        capypkg_local_append(buffer, buffer_size, "\npayload_url=");
        capypkg_local_append(buffer, buffer_size, e->payload_url);
        capypkg_local_append(buffer, buffer_size, "\npayload_sha256=");
        capypkg_local_append(buffer, buffer_size, e->payload_sha256);
        capypkg_local_append(buffer, buffer_size, "\ninstall_root=");
        capypkg_local_append(buffer, buffer_size, e->install_root);
        if (e->source_repo[0]) {
            capypkg_local_append(buffer, buffer_size, "\nrepo=");
            capypkg_local_append(buffer, buffer_size, e->source_repo);
        }
        if (e->summary[0]) {
            capypkg_local_append(buffer, buffer_size, "\nsummary=");
            capypkg_local_append(buffer, buffer_size, e->summary);
        }
        capypkg_local_append(buffer, buffer_size, "\n---\n");
    }
    pos = capypkg_local_len(buffer);
    if (out_len) *out_len = pos;
    return CAPYPKG_OK;
}

int capypkg_db_parse(const char *text, size_t len) {
    size_t cursor = 0u;
    if (!text) {
        return CAPYPKG_ERR_INVALID_ARG;
    }
    g_capypkg.installed_count = 0u;
    while (cursor < len &&
           g_capypkg.installed_count < CAPYPKG_MAX_INSTALLED) {
        struct capypkg_entry entry;
        size_t consumed = 0u;
        int rc = capypkg_manifest_parse_entry(&text[cursor], len - cursor,
                                              &entry, &consumed);
        if (rc != CAPYPKG_OK) {
            /* Skip a malformed descriptor: advance to the next
             * separator if present, otherwise abort. */
            if (consumed == 0u) break;
            cursor += consumed;
            continue;
        }
        entry.state = CAPYPKG_STATE_INSTALLED;
        g_capypkg.installed[g_capypkg.installed_count++] = entry;
        cursor += consumed;
    }
    return CAPYPKG_OK;
}

int capypkg_db_save(void) {
    char buffer[CAPYPKG_DB_BUFFER_BYTES];
    size_t len = 0u;
    if (!has_writer()) {
        return CAPYPKG_ERR_NOT_READY;
    }
    int rc = capypkg_db_serialize(buffer, sizeof(buffer), &len);
    if (rc != CAPYPKG_OK) {
        return rc;
    }
    if (g_capypkg_mkdir) {
        (void)g_capypkg_mkdir(CAPYPKG_DIR_SYSTEM);
    }
    if (g_capypkg_writer(CAPYPKG_DB_FILE, buffer) != 0) {
        return CAPYPKG_ERR_STORAGE;
    }
    return CAPYPKG_OK;
}

int capypkg_db_load(void) {
    char buffer[CAPYPKG_DB_BUFFER_BYTES];
    size_t len = 0u;
    if (!g_capypkg_reader) {
        return CAPYPKG_ERR_NOT_READY;
    }
    if (g_capypkg_reader(CAPYPKG_DB_FILE, buffer, sizeof(buffer),
                         &len) != 0) {
        return CAPYPKG_ERR_NOT_FOUND;
    }
    return capypkg_db_parse(buffer, len);
}

int capypkg_catalog_persist(void) {
    char buffer[CAPYPKG_INDEX_BUFFER_BYTES];
    if (!has_writer()) {
        return CAPYPKG_ERR_NOT_READY;
    }
    buffer[0] = '\0';
    capypkg_local_append(buffer, sizeof(buffer),
                         "# capypkg cached index v1\n");
    for (uint32_t i = 0u; i < g_capypkg.available_count; ++i) {
        const struct capypkg_entry *e = &g_capypkg.available[i];
        capypkg_local_append(buffer, sizeof(buffer), "name=");
        capypkg_local_append(buffer, sizeof(buffer), e->name);
        capypkg_local_append(buffer, sizeof(buffer), "\nversion=");
        capypkg_local_append(buffer, sizeof(buffer), e->version);
        capypkg_local_append(buffer, sizeof(buffer), "\npayload_url=");
        capypkg_local_append(buffer, sizeof(buffer), e->payload_url);
        capypkg_local_append(buffer, sizeof(buffer), "\npayload_sha256=");
        capypkg_local_append(buffer, sizeof(buffer), e->payload_sha256);
        if (e->signature_hex[0]) {
            capypkg_local_append(buffer, sizeof(buffer),
                                 "\nsignature_ed25519=");
            capypkg_local_append(buffer, sizeof(buffer), e->signature_hex);
        }
        if (e->install_root[0]) {
            capypkg_local_append(buffer, sizeof(buffer), "\ninstall_root=");
            capypkg_local_append(buffer, sizeof(buffer), e->install_root);
        }
        if (e->source_repo[0]) {
            capypkg_local_append(buffer, sizeof(buffer), "\nrepo=");
            capypkg_local_append(buffer, sizeof(buffer), e->source_repo);
        }
        if (e->summary[0]) {
            capypkg_local_append(buffer, sizeof(buffer), "\nsummary=");
            capypkg_local_append(buffer, sizeof(buffer), e->summary);
        }
        capypkg_local_append(buffer, sizeof(buffer), "\n---\n");
    }
    if (g_capypkg_mkdir) {
        (void)g_capypkg_mkdir(CAPYPKG_DIR_SYSTEM);
        (void)g_capypkg_mkdir(CAPYPKG_CACHE_DIR);
    }
    if (g_capypkg_writer("/system/capypkg/cache/index.txt", buffer) != 0) {
        return CAPYPKG_ERR_STORAGE;
    }
    return CAPYPKG_OK;
}

int capypkg_catalog_restore(void) {
    char buffer[CAPYPKG_INDEX_BUFFER_BYTES];
    size_t len = 0u;
    if (!g_capypkg_reader) {
        return CAPYPKG_ERR_NOT_READY;
    }
    if (g_capypkg_reader("/system/capypkg/cache/index.txt", buffer,
                         sizeof(buffer), &len) != 0) {
        return CAPYPKG_ERR_NOT_FOUND;
    }
    g_capypkg.available_count = 0u;
    size_t cursor = 0u;
    while (cursor < len &&
           g_capypkg.available_count < CAPYPKG_MAX_AVAILABLE) {
        struct capypkg_entry entry;
        size_t consumed = 0u;
        int rc = capypkg_manifest_parse_entry(&buffer[cursor], len - cursor,
                                              &entry, &consumed);
        if (rc != CAPYPKG_OK) {
            if (consumed == 0u) break;
            cursor += consumed;
            continue;
        }
        g_capypkg.available[g_capypkg.available_count++] = entry;
        cursor += consumed;
    }
    if (g_capypkg.available_count > 0u) {
        g_capypkg.catalog_fresh = 1u;
    }
    return CAPYPKG_OK;
}

int capypkg_fetch_index(void) {
    if (!g_capypkg.initialized) {
        return CAPYPKG_ERR_NOT_READY;
    }
    if (g_capypkg.repo_count == 0u) {
        return CAPYPKG_ERR_NO_SOURCE;
    }
    if (!has_text_fetcher()) {
        return CAPYPKG_ERR_NOT_READY;
    }
    g_capypkg.available_count = 0u;
    g_capypkg.catalog_fresh = 0u;
    int last_rc = CAPYPKG_ERR_FETCH;
    for (uint32_t r = 0u; r < g_capypkg.repo_count; ++r) {
        const struct capypkg_repo *repo = &g_capypkg.repos[r];
        char buffer[CAPYPKG_INDEX_BUFFER_BYTES];
        size_t len = 0u;
        int rc = g_capypkg_text_fetcher(repo->index_url, buffer,
                                        sizeof(buffer), &len);
        if (rc != 0 || len == 0u) {
            last_rc = CAPYPKG_ERR_FETCH;
            continue;
        }
        size_t cursor = 0u;
        while (cursor < len &&
               g_capypkg.available_count < CAPYPKG_MAX_AVAILABLE) {
            struct capypkg_entry entry;
            size_t consumed = 0u;
            int parse_rc = capypkg_manifest_parse_entry(
                &buffer[cursor], len - cursor, &entry, &consumed);
            if (parse_rc != CAPYPKG_OK) {
                if (consumed == 0u) break;
                cursor += consumed;
                continue;
            }
            if (!entry.source_repo[0]) {
                capypkg_local_copy(entry.source_repo,
                                   sizeof(entry.source_repo), repo->name);
            }
            int append_rc = append_available(&entry);
            if (append_rc != CAPYPKG_OK) {
                last_rc = append_rc;
                break;
            }
            cursor += consumed;
            last_rc = CAPYPKG_OK;
        }
    }
    if (g_capypkg.available_count > 0u) {
        g_capypkg.catalog_fresh = 1u;
        (void)capypkg_catalog_persist();
        return CAPYPKG_OK;
    }
    return last_rc;
}

static int install_dependencies(const struct capypkg_entry *entry,
                                int recursion_budget) {
    if (recursion_budget <= 0) {
        return CAPYPKG_ERR_DEPENDENCY;
    }
    for (uint32_t d = 0u; d < entry->dep_count; ++d) {
        const char *dep = entry->deps[d];
        if (!dep[0]) continue;
        if (capypkg_find_installed(dep)) continue;
        struct capypkg_entry *avail = capypkg_find_available(dep);
        if (!avail) {
            return CAPYPKG_ERR_DEPENDENCY;
        }
        int rc = install_dependencies(avail, recursion_budget - 1);
        if (rc != CAPYPKG_OK) return rc;
        rc = capypkg_install(dep);
        if (rc != CAPYPKG_OK && rc != CAPYPKG_ERR_ALREADY) {
            return rc;
        }
    }
    return CAPYPKG_OK;
}

static int build_payload_target(const struct capypkg_entry *entry,
                                char *out, size_t out_size) {
    if (!entry || !out || out_size == 0u) return -1;
    capypkg_local_copy(out, out_size, entry->install_root);
    capypkg_local_append(out, out_size, "/");
    capypkg_local_append(out, out_size, entry->name);
    capypkg_local_append(out, out_size, ".bin");
    return 0;
}

static int ensure_install_dirs(const struct capypkg_entry *entry) {
    if (!g_capypkg_mkdir) return CAPYPKG_OK;
    (void)g_capypkg_mkdir(CAPYPKG_DIR_SYSTEM);
    (void)g_capypkg_mkdir(CAPYPKG_DIR_VAR);
    (void)g_capypkg_mkdir(entry->install_root);
    return CAPYPKG_OK;
}

int capypkg_install(const char *name) {
    if (!g_capypkg.initialized) {
        return CAPYPKG_ERR_NOT_READY;
    }
    if (!name || !name[0]) {
        return CAPYPKG_ERR_INVALID_ARG;
    }
    struct capypkg_entry *existing_installed = capypkg_find_installed(name);
    struct capypkg_entry *avail = capypkg_find_available(name);
    if (existing_installed && avail &&
        capypkg_local_equal(existing_installed->version, avail->version)) {
        return CAPYPKG_ERR_ALREADY;
    }
    if (!avail) {
        return CAPYPKG_ERR_NOT_FOUND;
    }
    if (!has_bytes_fetcher() || !has_bytes_writer()) {
        return CAPYPKG_ERR_NOT_READY;
    }

    /* Resolve dependencies first; they must already be in the
     * catalog. Recursion budget caps cyclic descriptors. */
    int dep_rc = install_dependencies(avail, 8);
    if (dep_rc != CAPYPKG_OK) {
        avail->state = CAPYPKG_STATE_BROKEN;
        /* Distinguish "dep not in catalog / cycle detected" from
         * "dep was in catalog but its own install failed". The
         * recursive failure already emitted its own audit entry. */
        if (dep_rc == CAPYPKG_ERR_DEPENDENCY) {
            klog(KLOG_WARN,
                 "[audit] [capypkg] dependency missing or cycle; install aborted");
        } else {
            klog(KLOG_WARN,
                 "[audit] [capypkg] dependency install failed; install aborted");
        }
        return dep_rc;
    }

    avail->state = CAPYPKG_STATE_FETCHING;

    /* Allocate a stack-bound payload buffer. CAPYPKG_PAYLOAD_MAX is
     * 8 MiB and would blow up the stack; we cap per-call to a small
     * buffer for the alpha runtime. Larger payloads should ride
     * through the kernel heap once a streaming writer lands. */
    static uint8_t payload_buffer[1u * 1024u * 1024u];
    size_t payload_len = 0u;
    int rc = g_capypkg_bytes_fetcher(avail->payload_url, payload_buffer,
                                     sizeof(payload_buffer), &payload_len);
    if (rc != 0 || payload_len == 0u) {
        avail->state = CAPYPKG_STATE_BROKEN;
        klog(KLOG_WARN,
             "[audit] [capypkg] payload fetch failed; install aborted");
        return CAPYPKG_ERR_FETCH;
    }

    avail->state = CAPYPKG_STATE_VERIFYING;

    rc = capypkg_verify_payload(avail, payload_buffer, payload_len);
    if (rc != CAPYPKG_OK) {
        avail->state = CAPYPKG_STATE_BROKEN;
        klog(KLOG_WARN,
             "[audit] [capypkg] payload-sha256 mismatch; install aborted");
        return rc;
    }

    rc = verify_signature_if_required(avail);
    if (rc != CAPYPKG_OK) {
        avail->state = CAPYPKG_STATE_BROKEN;
        klog(KLOG_WARN,
             "[audit] [capypkg] signature verification failed; install aborted");
        return rc;
    }

    (void)ensure_install_dirs(avail);

    char target[CAPYPKG_PATH_MAX];
    if (build_payload_target(avail, target, sizeof(target)) != 0) {
        return CAPYPKG_ERR_INVALID_ARG;
    }
    if (g_capypkg_bytes_writer(target, payload_buffer, payload_len) != 0) {
        avail->state = CAPYPKG_STATE_BROKEN;
        klog(KLOG_WARN,
             "[audit] [capypkg] payload write failed; install aborted");
        return CAPYPKG_ERR_STORAGE;
    }

    avail->state = CAPYPKG_STATE_STAGED;
    int append_rc = append_or_replace_installed(avail);
    if (append_rc != CAPYPKG_OK) {
        /* Payload is on disk but the installed table is full. The
         * file is harmless without a db entry pointing at it; a
         * future transactional path will roll back the write. */
        avail->state = CAPYPKG_STATE_BROKEN;
        klog(KLOG_WARN,
             "[audit] [capypkg] installed-table quota exhausted; install aborted");
        return append_rc;
    }
    avail->state = CAPYPKG_STATE_INSTALLED;
    int save_rc = capypkg_db_save();
    if (save_rc == CAPYPKG_ERR_NOT_READY) {
        save_rc = CAPYPKG_OK;
    }
    if (save_rc != CAPYPKG_OK) {
        /* Memory-resident install succeeded (file written, table
         * updated), but the db file did not persist. On next boot
         * the install is lost. The success klog is intentionally
         * NOT emitted in this branch so an audit reader can tell
         * the difference between persisted and transient installs. */
        klog(KLOG_WARN,
             "[audit] [capypkg] package installed but db persistence failed");
        return save_rc;
    }
    klog(KLOG_INFO,
         "[audit] [capypkg] payload-sha256 verified; package installed");
    return save_rc;
}

int capypkg_remove(const char *name) {
    if (!g_capypkg.initialized) {
        return CAPYPKG_ERR_NOT_READY;
    }
    if (!name || !name[0]) {
        return CAPYPKG_ERR_INVALID_ARG;
    }
    struct capypkg_entry *existing = capypkg_find_installed(name);
    if (!existing) {
        return CAPYPKG_ERR_NOT_FOUND;
    }
    char target[CAPYPKG_PATH_MAX];
    if (build_payload_target(existing, target, sizeof(target)) != 0) {
        return CAPYPKG_ERR_INVALID_ARG;
    }
    if (has_remover()) {
        if (g_capypkg_remover(target) != 0) {
            /* File removal failed (locked, missing, permission). We
             * still drop the db entry below because the user asked
             * for the package to be gone — but the audit trail must
             * record that the on-disk payload may linger. */
            klog(KLOG_WARN,
                 "[audit] [capypkg] payload removal failed; db entry still dropped");
        }
    }
    /* Find the array index of `existing` and drop it. */
    uint32_t idx = (uint32_t)(existing - g_capypkg.installed);
    int rc = remove_installed_at(idx);
    if (rc != CAPYPKG_OK) {
        return rc;
    }
    int save_rc = capypkg_db_save();
    if (save_rc == CAPYPKG_ERR_NOT_READY) {
        save_rc = CAPYPKG_OK;
    }
    if (save_rc != CAPYPKG_OK) {
        /* In-memory removal succeeded but the db file did not
         * persist. On next boot the entry will reappear. Distinct
         * audit entry so a forensics reader can tell. */
        klog(KLOG_WARN,
             "[audit] [capypkg] package removed but db persistence failed");
        return save_rc;
    }
    klog(KLOG_INFO, "[audit] [capypkg] package removed");
    return save_rc;
}

int capypkg_update(const char *name) {
    if (!g_capypkg.initialized) {
        return CAPYPKG_ERR_NOT_READY;
    }
    if (!name || !name[0]) {
        return CAPYPKG_ERR_INVALID_ARG;
    }
    struct capypkg_entry *installed = capypkg_find_installed(name);
    struct capypkg_entry *avail = capypkg_find_available(name);
    if (!installed) {
        /* No-op fast path: behave as install for not-yet-installed
         * packages so `pkg-update <name>` is idempotent. */
        if (!avail) return CAPYPKG_ERR_NOT_FOUND;
        return capypkg_install(name);
    }
    if (!avail) {
        return CAPYPKG_ERR_NOT_FOUND;
    }
    if (capypkg_local_equal(installed->version, avail->version)) {
        return CAPYPKG_ERR_ALREADY;
    }
    /* Re-installing replaces the entry in installed[] with the new
     * descriptor and persists. */
    return capypkg_install(name);
}

int capypkg_update_all(void) {
    if (!g_capypkg.initialized) {
        return CAPYPKG_ERR_NOT_READY;
    }
    if (g_capypkg.installed_count == 0u) {
        return CAPYPKG_OK;
    }
    int last_rc = CAPYPKG_OK;
    /* Iterate over a snapshot of installed names; capypkg_install
     * may re-order the table. */
    char names[CAPYPKG_MAX_INSTALLED][CAPYPKG_NAME_MAX];
    uint32_t count = g_capypkg.installed_count;
    if (count > CAPYPKG_MAX_INSTALLED) count = CAPYPKG_MAX_INSTALLED;
    for (uint32_t i = 0u; i < count; ++i) {
        capypkg_local_copy(names[i], CAPYPKG_NAME_MAX,
                           g_capypkg.installed[i].name);
    }
    for (uint32_t i = 0u; i < count; ++i) {
        if (!names[i][0]) continue;
        int rc = capypkg_update(names[i]);
        if (rc != CAPYPKG_OK && rc != CAPYPKG_ERR_ALREADY &&
            rc != CAPYPKG_ERR_NOT_FOUND) {
            last_rc = rc;
        }
    }
    return last_rc;
}
