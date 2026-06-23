#ifndef SERVICES_CAPYPKG_H
#define SERVICES_CAPYPKG_H

/*
 * services/capypkg.h
 *
 * CapyOS package adapter contract (Etapa 9 alpha).
 *
 * This is the runtime-facing boundary the CapyOS core exposes for the
 * external `CapyAgent` package manager. The core does not own package
 * format details, resolver heuristics, repository ranking or rollout
 * policies; those live in CapyAgent. The CapyOS adapter exposes only:
 *
 *   - a small fail-closed state machine for installed/available pkgs;
 *   - a verified install/remove/update path that reuses the kernel's
 *     network stack, `sha256_hash` digest and Ed25519 verifier;
 *   - a configurable list of remote repository sources persisted
 *     under `/system/capypkg/`;
 *   - shell-facing operations so installations and updates can be
 *     driven by `capysh` even when no graphical UI is mounted.
 *
 * Hard rules (Etapa 3 -> Etapa 9 boundary):
 *   - The kernel never executes arbitrary code from a package.
 *     Installations only stage files into the encrypted VFS under
 *     `/var/capypkg/<name>/...` and record metadata in
 *     `/system/capypkg/db.idx`. Runtime activation of binaries is
 *     deferred to a future stage with sandboxing.
 *   - Every fetched payload must declare a `sha256` of the bytes
 *     downloaded from `payload_url`; the adapter recomputes it on
 *     the wire and refuses the install on mismatch.
 *   - When the source repository is configured with
 *     `require_signature=1` (default, including the seeded `stable`
 *     repo), the manifest must also declare `signature_ed25519` over
 *     the canonical descriptor formed by concatenating, with literal
 *     `|` separators and a trailing `\n`:
 *       `name=<N>|version=<V>|payload_sha256=<H>|payload_url=<U>\n`
 *     where the four fields are taken verbatim from the manifest.
 *     The adapter refuses installs from a signed repo when the
 *     signature is missing, when no Ed25519 verifier has been
 *     plugged in via `capypkg_set_signature_verifier`, or when the
 *     verifier rejects the descriptor. CapyAgent owns the signing
 *     side and must produce signatures over exactly this byte string.
 *   - Network access goes through `net/http.h` only (HTTPS via
 *     BearSSL), never raw sockets. TLS trust anchors are the ones
 *     pinned in `security/tls_trust_anchors.c`.
 *   - `name` is restricted to `[a-zA-Z0-9._-]` and dot-only names
 *     (`.`, `..`, `...`) are refused; `install_root` must be either
 *     exactly `/var/capypkg`, a directory strictly under
 *     `/var/capypkg/`, or a directory under `/opt/`, and may not
 *     contain `..` path segments.
 *   - If no repository is configured or all repositories fail, the
 *     adapter returns a stable non-zero result code instead of
 *     panicking, so the shell can fall back cleanly.
 *
 * Lifecycle:
 *
 *   capypkg_init() -- idempotent; seeds defaults, opens the on-disk
 *     index if present. May be called from `kernel_services_boot`
 *     before login.
 *   capypkg_set_writer() / capypkg_set_remover() / capypkg_set_reader()
 *     -- inject the VFS adapters from the kernel runtime.
 *   capypkg_repo_add() / capypkg_repo_remove() / capypkg_repo_list()
 *     -- configure remote sources. Persisted in
 *     `/system/capypkg/repos.cfg` when a writer is bound.
 *   capypkg_fetch_index() -- (re)download the index from configured
 *     repositories, verify signature and write the local cache.
 *   capypkg_install(name) / capypkg_remove(name) /
 *   capypkg_update(name) / capypkg_update_all() -- shell-driven
 *     operations.
 *
 * Test seam:
 *   `capypkg_set_*_transport` hooks let UNIT_TEST builds replace the
 *   HTTP fetchers and signature verifier with deterministic fakes.
 */

#include <stddef.h>
#include <stdint.h>

#define CAPYPKG_NAME_MAX       64u
#define CAPYPKG_VERSION_MAX    32u
#define CAPYPKG_SUMMARY_MAX    96u
#define CAPYPKG_URL_MAX        160u
#define CAPYPKG_PATH_MAX       128u
#define CAPYPKG_SHA256_HEX_LEN 64u
#define CAPYPKG_SHA256_HEX_MAX (CAPYPKG_SHA256_HEX_LEN + 1u)
#define CAPYPKG_SIG_HEX_LEN    128u
#define CAPYPKG_SIG_HEX_MAX    (CAPYPKG_SIG_HEX_LEN + 1u)
#define CAPYPKG_MAX_DEPS       8u
#define CAPYPKG_MAX_INSTALLED  64u
#define CAPYPKG_MAX_AVAILABLE  128u
#define CAPYPKG_MAX_REPOS      4u
#define CAPYPKG_PAYLOAD_MAX    (8u * 1024u * 1024u)
#define CAPYPKG_REPO_NAME_MAX  32u

enum capypkg_state {
    CAPYPKG_STATE_AVAILABLE = 0,
    CAPYPKG_STATE_FETCHING,
    CAPYPKG_STATE_VERIFYING,
    CAPYPKG_STATE_STAGED,
    CAPYPKG_STATE_INSTALLED,
    CAPYPKG_STATE_REMOVING,
    CAPYPKG_STATE_BROKEN,
};

enum capypkg_result {
    CAPYPKG_OK = 0,
    CAPYPKG_ERR_INVALID_ARG = -1,
    CAPYPKG_ERR_NOT_READY = -2,
    CAPYPKG_ERR_NOT_FOUND = -3,
    CAPYPKG_ERR_ALREADY = -4,
    CAPYPKG_ERR_NO_SOURCE = -5,
    CAPYPKG_ERR_FETCH = -6,
    CAPYPKG_ERR_PARSE = -7,
    CAPYPKG_ERR_DIGEST = -8,
    CAPYPKG_ERR_SIGNATURE = -9,
    CAPYPKG_ERR_STORAGE = -10,
    CAPYPKG_ERR_DEPENDENCY = -11,
    CAPYPKG_ERR_QUOTA = -12,
    CAPYPKG_ERR_DENIED = -13,
};

struct capypkg_entry {
    char name[CAPYPKG_NAME_MAX];
    char version[CAPYPKG_VERSION_MAX];
    char summary[CAPYPKG_SUMMARY_MAX];
    char source_repo[CAPYPKG_REPO_NAME_MAX];
    char payload_url[CAPYPKG_URL_MAX];
    char payload_sha256[CAPYPKG_SHA256_HEX_MAX];
    char signature_hex[CAPYPKG_SIG_HEX_MAX];
    char install_root[CAPYPKG_PATH_MAX];
    char deps[CAPYPKG_MAX_DEPS][CAPYPKG_NAME_MAX];
    uint32_t dep_count;
    uint32_t size_bytes;
    uint8_t state;
};

struct capypkg_repo {
    char name[CAPYPKG_REPO_NAME_MAX];
    char index_url[CAPYPKG_URL_MAX];
    uint8_t pinned;
    uint8_t require_signature;
};

struct capypkg_stats {
    uint32_t installed_count;
    uint32_t available_count;
    uint32_t updates_pending;
    uint32_t repo_count;
    uint8_t any_repo_signed;
    uint8_t catalog_fresh;
    uint8_t initialized;
};

/* Pluggable VFS access. The runtime supplies real implementations from
 * `shell/commands/system_control/jobs_updates.c` patterns. Test builds
 * supply stub versions. */
typedef int (*capypkg_read_file_fn)(const char *path, char *buffer,
                                    size_t buffer_size, size_t *out_len);
typedef int (*capypkg_write_text_fn)(const char *path, const char *text);
typedef int (*capypkg_write_bytes_fn)(const char *path, const uint8_t *data,
                                      size_t len);
typedef int (*capypkg_remove_file_fn)(const char *path);
typedef int (*capypkg_mkdir_fn)(const char *path);

/* Pluggable transport. Production binds these to HTTPS via
 * `net/http.h`. Tests bind deterministic fakes. */
typedef int (*capypkg_fetch_text_fn)(const char *url, char *buffer,
                                     size_t buffer_size, size_t *out_len);
typedef int (*capypkg_fetch_bytes_fn)(const char *url, uint8_t *buffer,
                                      size_t buffer_size, size_t *out_len);
typedef int (*capypkg_verify_signature_fn)(const char *signed_text,
                                           size_t signed_len,
                                           const char *signature_hex);

/* Optional progress-aware payload fetcher. When bound via
 * capypkg_set_bytes_fetcher_progress() the install path prefers it over
 * the plain capypkg_fetch_bytes_fn so byte-level download progress can
 * be surfaced to the install observer. `cb` (when non-NULL) is invoked
 * repeatedly as the payload streams in; `received`/`total` are byte
 * counts and `total` is 0 when the length is not known in advance.
 * Production binds this to net/http's http_download_progress; host
 * tests may leave it NULL and rely on the plain fetcher fallback. */
typedef void (*capypkg_download_progress_fn)(uint64_t received,
                                             uint64_t total, void *ctx);
typedef int (*capypkg_fetch_bytes_progress_fn)(const char *url,
                                               uint8_t *buffer,
                                               size_t buffer_size,
                                               size_t *out_len,
                                               capypkg_download_progress_fn cb,
                                               void *cb_ctx);

/* Per-package install lifecycle phases reported to the install
 * observer. Stable enum: append only, never renumber (UI consumers and
 * the first-boot wizard switch on these). */
enum capypkg_install_phase {
    CAPYPKG_INSTALL_PHASE_RESOLVE  = 0, /* resolving dependencies        */
    CAPYPKG_INSTALL_PHASE_DOWNLOAD = 1, /* streaming payload (cur/total) */
    CAPYPKG_INSTALL_PHASE_VERIFY   = 2, /* sha-256 + signature           */
    CAPYPKG_INSTALL_PHASE_STAGE    = 3, /* committing to install_root    */
    CAPYPKG_INSTALL_PHASE_DONE     = 4  /* installed + db persisted      */
};

/* Fine-grained, per-package install progress observer. `name` is the
 * package being installed. `cur`/`total` carry byte counts during
 * DOWNLOAD (total may be 0 when unknown) and are 0 for the other
 * phases. Single global slot: installs are serialized in the current
 * runtime. Set NULL to disable (the default). */
typedef void (*capypkg_install_progress_fn)(const char *name,
                                             enum capypkg_install_phase phase,
                                             uint64_t cur, uint64_t total,
                                             void *ctx);

void capypkg_reset(void);
int  capypkg_init(void);
int  capypkg_initialized(void);

void capypkg_set_reader(capypkg_read_file_fn fn);
void capypkg_set_writer(capypkg_write_text_fn fn);
void capypkg_set_bytes_writer(capypkg_write_bytes_fn fn);
void capypkg_set_remover(capypkg_remove_file_fn fn);
void capypkg_set_mkdir(capypkg_mkdir_fn fn);

void capypkg_set_text_fetcher(capypkg_fetch_text_fn fn);
void capypkg_set_bytes_fetcher(capypkg_fetch_bytes_fn fn);
void capypkg_set_bytes_fetcher_progress(capypkg_fetch_bytes_progress_fn fn);
void capypkg_set_signature_verifier(capypkg_verify_signature_fn fn);

/* CapyOS-side Ed25519 descriptor verifier (src/services/capypkg/
 * capypkg_signature.c), wired over the kernel's audited ed25519_verify and
 * registered as the signature verifier in the kernel binder. Matches
 * capypkg_verify_signature_fn: returns 0 iff `signature_hex` (exactly 128 hex
 * digits) is a valid Ed25519 signature, by the pinned trusted publisher key,
 * over the `signed_len` bytes at `signed_text`. Fail-closed (non-zero) when no
 * key is pinned, on NULL input, or on malformed hex. */
int capypkg_ed25519_verify_signature(const char *signed_text, size_t signed_len,
                                     const char *signature_hex);

/* Pin / clear the trusted publisher Ed25519 public key (32 bytes) used by
 * capypkg_ed25519_verify_signature. Unset by default, so signed installs stay
 * fail-closed until an operator pins the official offline-generated release key
 * (the publicly-known KAT test key must never be pinned in production). Passing
 * NULL clears the key. */
void capypkg_set_trusted_publisher_key(const uint8_t *key);
void capypkg_clear_trusted_publisher_key(void);

/* Install a fine-grained per-package install progress observer. NULL
 * disables it. The wizard installs this to drive its live status bar. */
void capypkg_set_install_observer(capypkg_install_progress_fn fn, void *ctx);

/* Repository management. */
int  capypkg_repo_add(const char *name, const char *index_url,
                      int require_signature);
int  capypkg_repo_remove(const char *name);
size_t capypkg_repo_count(void);
int  capypkg_repo_get_at(size_t idx, struct capypkg_repo *out);
int  capypkg_repo_save(void);
int  capypkg_repo_load(void);

/* Catalog (available packages) operations. */
int  capypkg_fetch_index(void);
size_t capypkg_available_count(void);
int  capypkg_available_get_at(size_t idx, struct capypkg_entry *out);
int  capypkg_available_get(const char *name, struct capypkg_entry *out);

/* Installation lifecycle. */
size_t capypkg_installed_count(void);
int  capypkg_installed_get_at(size_t idx, struct capypkg_entry *out);
int  capypkg_installed_get(const char *name, struct capypkg_entry *out);

int  capypkg_install(const char *name);
int  capypkg_remove(const char *name);
int  capypkg_update(const char *name);
int  capypkg_update_all(void);
int  capypkg_db_save(void);
int  capypkg_db_load(void);

void capypkg_stats_get(struct capypkg_stats *out);
const char *capypkg_state_label(uint8_t state);
const char *capypkg_result_label(int rc);
const char *capypkg_last_verify_error(void);

/* Path constants the runtime uses for persistence. */
#define CAPYPKG_DIR_SYSTEM    "/system/capypkg"
#define CAPYPKG_DIR_VAR       "/var/capypkg"
#define CAPYPKG_REPOS_FILE    "/system/capypkg/repos.cfg"
#define CAPYPKG_DB_FILE       "/system/capypkg/db.idx"
#define CAPYPKG_CACHE_DIR     "/system/capypkg/cache"
/* Staging area where the install pipeline lands the freshly fetched
 * payload before it is verified (SHA-256 + signature) and committed
 * to its final install_root. The directory is opt-in: it only exists
 * if `ensure_install_dirs()` has been called at least once for a
 * non-basic profile. Successful installs delete their staging file
 * immediately after the commit; failed installs keep the file so the
 * operator can audit it with `cat /var/capypkg/updates/<name>.bin`.
 *
 * The directory is also explicitly bounded by capypkg_manifest's
 * `path_is_under()` check: install_root must stay under
 * CAPYPKG_DIR_VAR or /opt, and CAPYPKG_DIR_UPDATES is a child of
 * CAPYPKG_DIR_VAR so the staging path inherits the same sandbox. */
#define CAPYPKG_DIR_UPDATES   "/var/capypkg/updates"

#endif /* SERVICES_CAPYPKG_H */
