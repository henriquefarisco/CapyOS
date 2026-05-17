#ifndef SERVICES_UPDATE_AGENT_INTERNAL_H
#define SERVICES_UPDATE_AGENT_INTERNAL_H

/* Internal boundary between the update_agent translation units.
 *
 * The agent is split across four .c files to keep each one under the
 * monolith threshold while preserving a single coherent state machine:
 *
 *   - src/services/update_agent.c
 *       Definitions, globals, string/hex helpers, IO helpers, remote
 *       fetchers, status seeding, reset/init/setters/status_get.
 *   - src/services/update_agent_parse.c
 *       Version parsing, manifest validators (sha256/url/signature),
 *       branch/URL builders, .ini line parsers, manifest_capture_signed_text,
 *       read_manifest_view, read_state_view, prepare_repository_status.
 *   - src/services/update_agent_apply.c
 *       Catalog state machine: write_state_file, update_agent_poll,
 *       import_manifest, fetch_remote_manifest, download_payload,
 *       prepare_dry_run/explain/staged_update, stage_latest,
 *       clear_stage, set_pending_activation.
 *   - src/services/update_agent_transact.c
 *       Boot-slot integration: apply_boot_slot, confirm_health,
 *       check_rollback and the M6.4 payload sha256 verification path.
 *
 * All four TUs share the singleton runtime status struct, the helper
 * data views (manifest/state) and a small set of helpers used to write
 * char fields, parse bytes, validate manifests and reach the active
 * filesystem hooks. The shared symbols live here with the
 * `update_agent_` prefix so this header acts as a private API
 * boundary; nothing outside src/services/ should ever include it. */

#include "services/update_agent.h"

#include <stddef.h>
#include <stdint.h>

/* ── repository / manifest path defaults (update_agent.c) ──────────── */

#define UPDATE_AGENT_REPOSITORY_PATH "/system/update/repository.ini"
#define UPDATE_AGENT_STATE_PATH "/system/update/state.ini"
#define UPDATE_AGENT_DEFAULT_MANIFEST_PATH "/system/update/latest.ini"
#define UPDATE_AGENT_DEFAULT_STAGED_MANIFEST_PATH "/system/update/staged.ini"
#define UPDATE_AGENT_FETCHED_MANIFEST_PATH "/system/update/fetched.ini"
#define UPDATE_AGENT_PAYLOAD_CACHE_PATH "/system/update/payload.bin"
#define UPDATE_AGENT_DEFAULT_CHANNEL "stable"
#define UPDATE_AGENT_DEFAULT_BRANCH "main"
#define UPDATE_AGENT_DEFAULT_SOURCE "github:henriquefarisco/CapyOS"
#define UPDATE_AGENT_GITHUB_PREFIX "github:"
#define UPDATE_AGENT_REMOTE_MANIFEST_SUFFIX "/system/update/latest.ini"
#define UPDATE_AGENT_MANIFEST_TEXT_MAX 768u

/* ── shared view types (defined by update_agent.c) ──────────────────── */

struct update_manifest_view {
  char version[UPDATE_AGENT_VERSION_MAX];
  char channel[UPDATE_AGENT_CHANNEL_MAX];
  char branch[UPDATE_AGENT_BRANCH_MAX];
  char source[UPDATE_AGENT_SOURCE_MAX];
  char published_at[24];
  char payload_url[UPDATE_AGENT_REMOTE_MAX];
  char payload_sha256[UPDATE_AGENT_SHA256_HEX_MAX];
  char signature_ed25519[UPDATE_AGENT_ED25519_SIGNATURE_HEX_MAX];
  char signed_text[UPDATE_AGENT_MANIFEST_TEXT_MAX];
  size_t signed_len;
  uint8_t signature_line_count;
};

struct update_state_view {
  uint8_t pending_activation;
  char staged_manifest_path[UPDATE_AGENT_PATH_MAX];
  char payload_cache_path[UPDATE_AGENT_PATH_MAX];
  char payload_cache_sha256[UPDATE_AGENT_SHA256_HEX_MAX];
};

/* ── singleton runtime status + test-only fetcher hooks ─────────────── */

extern struct system_update_status update_agent_g_status;

#if defined(UNIT_TEST)
extern update_agent_manifest_verify_fn g_update_manifest_verifier;
extern update_agent_fetch_manifest_fn g_update_manifest_fetcher;
extern update_agent_fetch_payload_fn g_update_payload_fetcher;
#endif

/* ── string + hex helpers (update_agent.c) ───────────────────────────── */

void update_agent_local_copy(char *dst, size_t dst_size, const char *src);
void update_agent_local_zero(void *ptr, size_t len);
void update_agent_local_append(char *dst, size_t dst_size, const char *src);
int update_agent_local_equal(const char *a, const char *b);
int update_agent_local_starts_with(const char *text, const char *prefix);
int update_agent_local_is_digit(char c);
int update_agent_local_is_hex_digit(char c);
int update_agent_local_hex_value(char c);
int update_agent_local_hex_string_valid(const char *text, size_t hex_len);
int update_agent_local_hex_to_bytes(const char *hex, uint8_t *out,
                                    size_t out_len);
int update_agent_local_hex_equal_fixed(const char *a, const char *b,
                                       size_t hex_len);
int update_agent_parse_bool_value(const char *value);

/* ── IO accessors + remote fetchers (update_agent.c) ────────────────── */

update_agent_read_file_fn update_agent_active_reader(void);
update_agent_write_file_fn update_agent_active_writer(void);
update_agent_write_bytes_fn update_agent_active_bytes_writer(void);
update_agent_remove_file_fn update_agent_active_remover(void);

int update_agent_fetch_remote_manifest_text(const char *url, char *buffer,
                                            size_t buffer_size,
                                            size_t *out_len);
int update_agent_fetch_payload_bytes(const char *url, uint8_t *buffer,
                                     size_t buffer_size, size_t *out_len);

/* ── view reset + status seeding (update_agent.c) ───────────────────── */

void update_agent_seed_defaults(const char *current_version);
void update_agent_manifest_view_reset(struct update_manifest_view *view);
void update_agent_state_view_reset(struct update_state_view *view);

/* ── validators + parsers (update_agent_parse.c) ────────────────────── */

int update_agent_manifest_payload_sha256_valid(
    const struct update_manifest_view *view);
int update_agent_manifest_payload_url_valid(
    const struct update_manifest_view *view);
int update_agent_manifest_signature_ed25519_valid(
    const struct update_manifest_view *view);
int update_agent_manifest_compare_current(
    const struct update_manifest_view *view, int *out_cmp);

void update_agent_parse_buffer(const char *buffer, size_t len, int parse_mode,
                               void *target);
int update_agent_manifest_capture_signed_text(
    const char *buffer, size_t len, struct update_manifest_view *view);
int update_agent_read_manifest_view(const char *path,
                                    struct update_manifest_view *view);
int update_agent_read_state_view(struct update_state_view *view);
void update_agent_prepare_repository_status(void);

#endif /* SERVICES_UPDATE_AGENT_INTERNAL_H */
