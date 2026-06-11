/*
 * src/services/capypkg/capypkg_persist.c
 *
 * Persisted-state I/O for the CapyOS package adapter, split out of
 * capypkg_install.c (preventive split at the 900-line layout ceiling).
 * Owns serialization/restore of two on-disk surfaces, both written
 * through the bound VFS adapters and failing soft on writer errors:
 *
 *   - the installed DB (/system/capypkg/db.idx) via
 *     capypkg_db_serialize/parse/save/load;
 *   - the cached available catalog (/system/capypkg/cache/index.txt)
 *     via capypkg_catalog_persist/restore.
 *
 * Pure metadata I/O: it never fetches, verifies or stages payload
 * bytes (that is capypkg_install.c). Declarations live in the public
 * header (db_save/db_load) and the internal header (the rest).
 */

#include "internal/capypkg_internal.h"

#include <stddef.h>
#include <stdint.h>

static int has_writer(void) {
    return g_capypkg_writer != NULL;
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
