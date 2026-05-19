#ifndef SERVICES_CAPYPKG_INTERNAL_H
#define SERVICES_CAPYPKG_INTERNAL_H

/*
 * src/services/capypkg/internal/capypkg_internal.h
 *
 * Shared state and helpers between the four capypkg TUs.
 * Strictly internal: callers must use the public API in
 * include/services/capypkg.h.
 */

#include "services/capypkg.h"

#include <stddef.h>
#include <stdint.h>

struct capypkg_runtime {
    uint8_t initialized;
    uint8_t catalog_fresh;
    uint8_t any_repo_signed;
    uint32_t repo_count;
    uint32_t installed_count;
    uint32_t available_count;
    struct capypkg_repo  repos[CAPYPKG_MAX_REPOS];
    struct capypkg_entry installed[CAPYPKG_MAX_INSTALLED];
    struct capypkg_entry available[CAPYPKG_MAX_AVAILABLE];
};

extern struct capypkg_runtime g_capypkg;

extern capypkg_read_file_fn   g_capypkg_reader;
extern capypkg_write_text_fn  g_capypkg_writer;
extern capypkg_write_bytes_fn g_capypkg_bytes_writer;
extern capypkg_remove_file_fn g_capypkg_remover;
extern capypkg_mkdir_fn       g_capypkg_mkdir;

extern capypkg_fetch_text_fn       g_capypkg_text_fetcher;
extern capypkg_fetch_bytes_fn      g_capypkg_bytes_fetcher;
extern capypkg_verify_signature_fn g_capypkg_signature_verifier;

/* Generic byte/string helpers (no libc, kernel freestanding). */
void capypkg_local_zero(void *ptr, size_t len);
void capypkg_local_copy(char *dst, size_t dst_size, const char *src);
void capypkg_local_append(char *dst, size_t dst_size, const char *src);
size_t capypkg_local_len(const char *text);
int capypkg_local_equal(const char *a, const char *b);
int capypkg_local_starts_with(const char *text, const char *prefix);
int capypkg_local_is_hex_digit(char c);
int capypkg_local_hex_string_valid(const char *text, size_t hex_len);

/* Catalog accessors used by install operations. */
struct capypkg_entry *capypkg_find_installed(const char *name);
struct capypkg_entry *capypkg_find_available(const char *name);
struct capypkg_repo  *capypkg_find_repo(const char *name);

/* Manifest parsing for one package descriptor. Stops at end-of-buffer
 * or at the first descriptor break ("\n---\n"). On success populates
 * `entry` with payload metadata. Returns CAPYPKG_OK or a negative
 * `capypkg_result`. */
int capypkg_manifest_parse_entry(const char *text, size_t len,
                                 struct capypkg_entry *entry,
                                 size_t *consumed);

/* Verify a freshly downloaded package payload against the entry. */
int capypkg_verify_payload(const struct capypkg_entry *entry,
                           const uint8_t *payload, size_t payload_len);

/* Repository persistence helpers. */
int capypkg_repo_serialize(char *buffer, size_t buffer_size, size_t *out_len);
int capypkg_repo_parse(const char *text, size_t len);

/* Available catalog persistence (cache of last-fetched index). */
int capypkg_catalog_persist(void);
int capypkg_catalog_restore(void);

/* DB persistence (installed entries). */
int capypkg_db_serialize(char *buffer, size_t buffer_size, size_t *out_len);
int capypkg_db_parse(const char *text, size_t len);

/* Default repository entry seeded on first init. */
extern const char *const CAPYPKG_DEFAULT_REPO_NAME;
extern const char *const CAPYPKG_DEFAULT_REPO_URL;

#endif /* SERVICES_CAPYPKG_INTERNAL_H */
