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
 *   6. Stage the bytes under <install_root>/<name>.bin, or split into
 *      payload.partNN files when the filesystem's current file-size
 *      ceiling is smaller than the verified artifact.
 *   7. Write <install_root>/installed, then append/replace the entry
 *      in the installed table and persist the DB.
 *
 * The kernel never executes the staged bytes from this path. Future
 * stages will introduce a sandboxed loader that consumes
 * /var/capypkg/<name>/ contents.
 */

#include "internal/capypkg_internal.h"
#include "kernel/log/klog.h"

#if !defined(UNIT_TEST)
#include "memory/kmem.h"
#endif

#include <stddef.h>
#include <stdint.h>

#define CAPYPKG_PAYLOAD_PART_BYTES (256u * 1024u)

static int has_text_fetcher(void) {
    return g_capypkg_text_fetcher != NULL;
}

static int has_bytes_fetcher(void) {
    return g_capypkg_bytes_fetcher != NULL ||
           g_capypkg_bytes_fetcher_progress != NULL;
}

/* Name of the package whose payload is currently streaming, so the
 * download-progress adapter can attribute byte counts to it. Installs
 * are serialized in the current runtime, so a single file-scope pointer
 * is sufficient; it is set immediately before the fetch and cleared
 * immediately after. */
static const char *g_capypkg_dl_name = NULL;

/* Adapter handed to the progress-aware bytes fetcher: forwards raw byte
 * counts up to the install observer as DOWNLOAD-phase progress. */
static void capypkg_install_download_progress(uint64_t received,
                                              uint64_t total, void *ctx) {
    (void)ctx;
    capypkg_emit_install_phase(g_capypkg_dl_name,
                               CAPYPKG_INSTALL_PHASE_DOWNLOAD, received,
                               total);
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

static int build_payload_part_target(const struct capypkg_entry *entry,
                                     uint32_t part_index,
                                     char *out, size_t out_size) {
    if (!entry || !out || out_size == 0u || part_index > 99u) return -1;
    capypkg_local_copy(out, out_size, entry->install_root);
    capypkg_local_append(out, out_size, "/payload.part");
    char suffix[3];
    suffix[0] = (char)('0' + (part_index / 10u));
    suffix[1] = (char)('0' + (part_index % 10u));
    suffix[2] = '\0';
    capypkg_local_append(out, out_size, suffix);
    return 0;
}

static int build_installed_marker_target(const struct capypkg_entry *entry,
                                         char *out, size_t out_size) {
    if (!entry || !out || out_size == 0u) return -1;
    capypkg_local_copy(out, out_size, entry->install_root);
    capypkg_local_append(out, out_size, "/installed");
    return 0;
}

static void append_decimal(char *out, size_t out_size, uint32_t value) {
    char digits[10];
    size_t count = 0u;
    if (value == 0u) {
        capypkg_local_append(out, out_size, "0");
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
        capypkg_local_append(out, out_size, ch);
    }
}

static int write_payload_parts(const struct capypkg_entry *entry,
                               const uint8_t *payload,
                               size_t payload_len) {
    size_t offset = 0u;
    uint32_t part = 0u;
    if (!entry || !payload || !g_capypkg_bytes_writer) {
        return CAPYPKG_ERR_INVALID_ARG;
    }
    while (offset < payload_len) {
        char part_path[CAPYPKG_PATH_MAX];
        size_t chunk = payload_len - offset;
        if (chunk > CAPYPKG_PAYLOAD_PART_BYTES) {
            chunk = CAPYPKG_PAYLOAD_PART_BYTES;
        }
        if (build_payload_part_target(entry, part, part_path,
                                      sizeof(part_path)) != 0) {
            return CAPYPKG_ERR_STORAGE;
        }
        if (g_capypkg_bytes_writer(part_path, payload + offset, chunk) != 0) {
            return CAPYPKG_ERR_STORAGE;
        }
        offset += chunk;
        ++part;
    }
    return CAPYPKG_OK;
}

static int write_installed_marker(const struct capypkg_entry *entry,
                                  size_t payload_len) {
    char marker_path[CAPYPKG_PATH_MAX];
    char marker[256];
    if (!entry || !g_capypkg_bytes_writer) {
        return CAPYPKG_ERR_INVALID_ARG;
    }
    if (build_installed_marker_target(entry, marker_path,
                                      sizeof(marker_path)) != 0) {
        return CAPYPKG_ERR_STORAGE;
    }
    marker[0] = '\0';
    capypkg_local_append(marker, sizeof(marker), "state=installed\nname=");
    capypkg_local_append(marker, sizeof(marker), entry->name);
    capypkg_local_append(marker, sizeof(marker), "\nversion=");
    capypkg_local_append(marker, sizeof(marker), entry->version);
    capypkg_local_append(marker, sizeof(marker), "\npayload_sha256=");
    capypkg_local_append(marker, sizeof(marker), entry->payload_sha256);
    capypkg_local_append(marker, sizeof(marker), "\npayload_size=");
    append_decimal(marker, sizeof(marker), (uint32_t)payload_len);
    capypkg_local_append(marker, sizeof(marker), "\n");
    return g_capypkg_bytes_writer(marker_path, (const uint8_t *)marker,
                                  capypkg_local_len(marker)) == 0
               ? CAPYPKG_OK
               : CAPYPKG_ERR_STORAGE;
}

/* Build the staging path under CAPYPKG_DIR_UPDATES for the freshly
 * fetched payload. The directory is bounded by capypkg_manifest's
 * `path_is_under()` whitelist (CAPYPKG_DIR_VAR), so this path
 * inherits the same sandbox as `install_root`. */
static int build_payload_staging(const struct capypkg_entry *entry,
                                 char *out, size_t out_size) {
    if (!entry || !out || out_size == 0u) return -1;
    capypkg_local_copy(out, out_size, CAPYPKG_DIR_UPDATES);
    capypkg_local_append(out, out_size, "/");
    capypkg_local_append(out, out_size, entry->name);
    capypkg_local_append(out, out_size, ".bin");
    return 0;
}

static int ensure_install_dirs(const struct capypkg_entry *entry) {
    if (!g_capypkg_mkdir) return CAPYPKG_OK;
    (void)g_capypkg_mkdir(CAPYPKG_DIR_SYSTEM);
    (void)g_capypkg_mkdir(CAPYPKG_DIR_VAR);
    /* Staging area for fetched payloads. Created up-front (not just
     * before the staging write) so failed installs leave the
     * directory visible to the operator for forensics, and so the
     * cleanup pass at the start of capypkg_install can always find
     * the directory to scan. */
    (void)g_capypkg_mkdir(CAPYPKG_DIR_UPDATES);
    (void)g_capypkg_mkdir(entry->install_root);
    return CAPYPKG_OK;
}

/* Best-effort cleanup of the staging payload. Called both on the
 * happy path (after the file has been committed to install_root)
 * and from the error paths after the staging write has happened.
 * The remover is allowed to be NULL or to fail silently: a leftover
 * .bin in CAPYPKG_DIR_UPDATES is harmless (it is not on the load
 * path) and the next install of the same package will overwrite it. */
static void cleanup_payload_staging(const struct capypkg_entry *entry) {
    if (!entry || !g_capypkg_remover) return;
    char staging[CAPYPKG_PATH_MAX];
    if (build_payload_staging(entry, staging, sizeof(staging)) != 0) {
        return;
    }
    (void)g_capypkg_remover(staging);
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
    capypkg_emit_install_phase(avail->name, CAPYPKG_INSTALL_PHASE_RESOLVE,
                               0, 0);
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

    /* Make sure the staging directory exists before we even hit the
     * network: an mkdir failure now is easier to surface (we can
     * still try the legacy direct-write path) than after several
     * megabytes of payload have been pulled. */
    (void)ensure_install_dirs(avail);

    /* Keep host tests deterministic, but allocate in-kernel so the
     * 8 MiB package contract does not permanently live in .bss. */
#if defined(UNIT_TEST)
    static uint8_t payload_buffer[CAPYPKG_PAYLOAD_MAX];
    size_t payload_buffer_cap = sizeof(payload_buffer);
#else
    /* Size the download buffer to the package's declared payload size
     * (capped at the 8 MiB contract) instead of always reserving the full
     * 8 MiB. kalloc draws from the 16 MiB kernel heap, and a fixed 8 MiB
     * request -- half the heap in one contiguous block -- routinely fails
     * once the first-boot heap is partly used/fragmented, which aborted
     * every module install with CAPYPKG_ERR_QUOTA (the local-bundle modules
     * are ~1.2 MiB each). size_bytes is only a hint, so the fetcher still
     * enforces this cap and capypkg_verify_payload re-checks the SHA-256: a
     * wrong hint can only shrink the buffer and trip a clean verify failure,
     * never smuggle unverified bytes through. A missing/oversized hint falls
     * back to the full contract size (prior behaviour). */
    size_t payload_buffer_cap = CAPYPKG_PAYLOAD_MAX;
    if (avail->size_bytes > 0u &&
        (size_t)avail->size_bytes <= CAPYPKG_PAYLOAD_MAX) {
        payload_buffer_cap = (size_t)avail->size_bytes;
    }
    uint8_t *payload_buffer = (uint8_t *)kalloc(payload_buffer_cap);
    if (!payload_buffer) {
        avail->state = CAPYPKG_STATE_BROKEN;
        klog(KLOG_WARN,
             "[audit] [capypkg] payload buffer unavailable; install aborted");
        return CAPYPKG_ERR_QUOTA;
    }
#endif
    size_t payload_len = 0u;
    int rc;
    /* Announce the download phase. `size_bytes` is the manifest's hint
     * for the total; when it is 0 (absent) the streaming callback fills
     * in the real Content-Length as bytes arrive. Prefer the
     * progress-aware fetcher so the wizard can render a live byte-level
     * bar; fall back to the plain fetcher (e.g. host tests) otherwise. */
    capypkg_emit_install_phase(avail->name, CAPYPKG_INSTALL_PHASE_DOWNLOAD,
                               0, (uint64_t)avail->size_bytes);
    if (g_capypkg_bytes_fetcher_progress) {
        g_capypkg_dl_name = avail->name;
        rc = g_capypkg_bytes_fetcher_progress(
            avail->payload_url, payload_buffer, payload_buffer_cap,
            &payload_len, capypkg_install_download_progress, NULL);
        g_capypkg_dl_name = NULL;
    } else {
        rc = g_capypkg_bytes_fetcher(avail->payload_url, payload_buffer,
                                     payload_buffer_cap, &payload_len);
    }
    if (rc != 0 || payload_len == 0u) {
        avail->state = CAPYPKG_STATE_BROKEN;
        klog(KLOG_WARN,
             "[audit] [capypkg] payload fetch failed; install aborted");
#if !defined(UNIT_TEST)
        kfree(payload_buffer);
#endif
        return CAPYPKG_ERR_FETCH;
    }
    /* Download complete: report 100% of the phase for both fetch paths
     * (the plain fetcher emits no intermediate progress). */
    capypkg_emit_install_phase(avail->name, CAPYPKG_INSTALL_PHASE_DOWNLOAD,
                               (uint64_t)payload_len, (uint64_t)payload_len);

    /* Land the fresh payload in /var/capypkg/updates/<name>.bin BEFORE
     * verifying it: this lets an operator inspect the bytes that were
     * actually rejected (e.g. an Azure SAS error XML disguised as a
     * 200 response, or a truncated download) instead of seeing an
     * opaque "sha256 mismatch" line. The file is cleared again on the
     * happy path right after the commit to install_root, so the
     * staging area stays empty on a steady-state system. */
    char staging[CAPYPKG_PATH_MAX];
    int staged = 0;
    if (build_payload_staging(avail, staging, sizeof(staging)) == 0 &&
        g_capypkg_bytes_writer) {
        if (g_capypkg_bytes_writer(staging, payload_buffer, payload_len) == 0) {
            staged = 1;
        } else {
            klog(KLOG_WARN,
                 "[audit] [capypkg] staging write failed; install continues");
        }
    }

    avail->state = CAPYPKG_STATE_VERIFYING;
    capypkg_emit_install_phase(avail->name, CAPYPKG_INSTALL_PHASE_VERIFY,
                               0, 0);

    rc = capypkg_verify_payload(avail, payload_buffer, payload_len);
    if (rc != CAPYPKG_OK) {
        avail->state = CAPYPKG_STATE_BROKEN;
        klog(KLOG_WARN,
             "[audit] [capypkg] payload-sha256 mismatch; install aborted");
        /* Keep the staging file on verify failure: it is the only
         * artifact the operator has to debug the upstream payload. */
#if !defined(UNIT_TEST)
        kfree(payload_buffer);
#endif
        return rc;
    }

    rc = verify_signature_if_required(avail);
    if (rc != CAPYPKG_OK) {
        avail->state = CAPYPKG_STATE_BROKEN;
        klog(KLOG_WARN,
             "[audit] [capypkg] signature verification failed; install aborted");
        /* Same rationale as above: keep the staging file for audit. */
#if !defined(UNIT_TEST)
        kfree(payload_buffer);
#endif
        return rc;
    }

    capypkg_emit_install_phase(avail->name, CAPYPKG_INSTALL_PHASE_STAGE, 0, 0);
    char target[CAPYPKG_PATH_MAX];
    if (build_payload_target(avail, target, sizeof(target)) != 0) {
        if (staged) cleanup_payload_staging(avail);
#if !defined(UNIT_TEST)
        kfree(payload_buffer);
#endif
        return CAPYPKG_ERR_INVALID_ARG;
    }
    if (g_capypkg_bytes_writer(target, payload_buffer, payload_len) != 0) {
        if (g_capypkg_remover) {
            (void)g_capypkg_remover(target);
        }
        rc = write_payload_parts(avail, payload_buffer, payload_len);
        if (rc != CAPYPKG_OK) {
            avail->state = CAPYPKG_STATE_BROKEN;
            klog(KLOG_WARN,
                 "[audit] [capypkg] payload write failed; install aborted");
            /* Keep the staging file: the commit failed, so the operator
             * may want to manually copy the verified bytes into place. */
#if !defined(UNIT_TEST)
            kfree(payload_buffer);
#endif
            return rc;
        }
        klog(KLOG_INFO,
             "[audit] [capypkg] payload stored in split parts");
    }

    /* Happy path: the bytes are committed in install_root and the
     * staging copy is now redundant. Hygienise the staging directory
     * so a steady-state system never accumulates leftover payloads. */
    if (staged) {
        cleanup_payload_staging(avail);
    }

    avail->state = CAPYPKG_STATE_STAGED;
    rc = write_installed_marker(avail, payload_len);
    if (rc != CAPYPKG_OK) {
        avail->state = CAPYPKG_STATE_BROKEN;
        klog(KLOG_WARN,
             "[audit] [capypkg] installed marker write failed; install aborted");
#if !defined(UNIT_TEST)
        kfree(payload_buffer);
#endif
        return rc;
    }

    int append_rc = append_or_replace_installed(avail);
    if (append_rc != CAPYPKG_OK) {
        /* Payload is on disk but the installed table is full. The
         * file is harmless without a db entry pointing at it; a
         * future transactional path will roll back the write. */
        avail->state = CAPYPKG_STATE_BROKEN;
        klog(KLOG_WARN,
             "[audit] [capypkg] installed-table quota exhausted; install aborted");
#if !defined(UNIT_TEST)
        kfree(payload_buffer);
#endif
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
#if !defined(UNIT_TEST)
        kfree(payload_buffer);
#endif
        return save_rc;
    }
    klog(KLOG_INFO,
         "[audit] [capypkg] payload-sha256 verified; package installed");
    capypkg_emit_install_phase(avail->name, CAPYPKG_INSTALL_PHASE_DONE, 0, 0);
#if !defined(UNIT_TEST)
    kfree(payload_buffer);
#endif
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
