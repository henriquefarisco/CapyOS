#ifndef CORE_UPDATE_AGENT_H
#define CORE_UPDATE_AGENT_H

#include <stddef.h>
#include <stdint.h>

#define UPDATE_AGENT_CHANNEL_MAX 16u
#define UPDATE_AGENT_BRANCH_MAX 16u
#define UPDATE_AGENT_SOURCE_MAX 96u
#define UPDATE_AGENT_VERSION_MAX 40u
#define UPDATE_AGENT_PATH_MAX 96u
#define UPDATE_AGENT_REMOTE_MAX 160u
#define UPDATE_AGENT_SUMMARY_MAX 80u
#define UPDATE_AGENT_PREPARE_GATE_MAX 32u
#define UPDATE_AGENT_PAYLOAD_MAX_BYTES (16u * 1024u * 1024u)
/* SHA-256 hex digest = 64 ASCII chars + NUL. */
#define UPDATE_AGENT_SHA256_HEX_LEN 64u
#define UPDATE_AGENT_SHA256_HEX_MAX (UPDATE_AGENT_SHA256_HEX_LEN + 1u)
#define UPDATE_AGENT_ED25519_SIGNATURE_HEX_LEN 128u
#define UPDATE_AGENT_ED25519_SIGNATURE_HEX_MAX \
  (UPDATE_AGENT_ED25519_SIGNATURE_HEX_LEN + 1u)

struct update_prepare_explain {
  uint8_t poll_ready;
  uint8_t catalog_ready;
  uint8_t repository_ready;
  uint8_t version_ready;
  uint8_t payload_sha256_ready;
  uint8_t payload_url_ready;
  uint8_t signature_ready;
  uint8_t cache_ready;
  uint8_t stage_safe;
  int32_t result;
  char failing_gate[UPDATE_AGENT_PREPARE_GATE_MAX];
  char summary[UPDATE_AGENT_SUMMARY_MAX];
};

struct system_update_status {
  uint8_t configured;
  uint8_t catalog_present;
  uint8_t update_available;
  uint8_t stage_ready;
  uint8_t pending_activation;
  int32_t last_result;
  char channel[UPDATE_AGENT_CHANNEL_MAX];
  char branch[UPDATE_AGENT_BRANCH_MAX];
  char source[UPDATE_AGENT_SOURCE_MAX];
  char manifest_path[UPDATE_AGENT_PATH_MAX];
  char remote_manifest_url[UPDATE_AGENT_REMOTE_MAX];
  char staged_manifest_path[UPDATE_AGENT_PATH_MAX];
  char current_version[UPDATE_AGENT_VERSION_MAX];
  char available_version[UPDATE_AGENT_VERSION_MAX];
  char staged_version[UPDATE_AGENT_VERSION_MAX];
  char payload_url[UPDATE_AGENT_REMOTE_MAX];
  char staged_payload_url[UPDATE_AGENT_REMOTE_MAX];
  char payload_cache_path[UPDATE_AGENT_PATH_MAX];
  char payload_cache_sha256[UPDATE_AGENT_SHA256_HEX_MAX];
  char published_at[24];
  char summary[UPDATE_AGENT_SUMMARY_MAX];
  /* Lower-case hex sha256 of the staged payload, taken verbatim from the
   * manifest (`payload_sha256=...`). Valid staged manifests must declare a
   * 64-character hex digest. Used by update_agent_apply_boot_slot_verified(). */
  char staged_payload_sha256[UPDATE_AGENT_SHA256_HEX_MAX];
};

typedef int (*update_agent_read_file_fn)(const char *path, char *buffer,
                                         size_t buffer_size,
                                         size_t *out_len);
typedef int (*update_agent_write_file_fn)(const char *path, const char *text);
typedef int (*update_agent_write_bytes_fn)(const char *path, const uint8_t *data,
                                           size_t len);
typedef int (*update_agent_remove_file_fn)(const char *path);
#if defined(UNIT_TEST)
typedef int (*update_agent_manifest_verify_fn)(const char *signed_text,
                                               size_t signed_len,
                                               const char *signature_hex);
typedef int (*update_agent_fetch_manifest_fn)(const char *url, char *buffer,
                                              size_t buffer_size,
                                              size_t *out_len);
typedef int (*update_agent_fetch_payload_fn)(const char *url, uint8_t *buffer,
                                             size_t buffer_size,
                                             size_t *out_len);
#endif

void update_agent_reset(void);
void update_agent_init(const char *current_version);
void update_agent_set_reader(update_agent_read_file_fn reader);
void update_agent_set_writer(update_agent_write_file_fn writer);
void update_agent_set_bytes_writer(update_agent_write_bytes_fn writer);
void update_agent_set_remover(update_agent_remove_file_fn remover);
#if defined(UNIT_TEST)
void update_agent_set_manifest_verifier(update_agent_manifest_verify_fn verifier);
void update_agent_set_manifest_fetcher(update_agent_fetch_manifest_fn fetcher);
void update_agent_set_payload_fetcher(update_agent_fetch_payload_fn fetcher);
#endif
int update_agent_poll(void);
int update_agent_fetch_remote_manifest(void);
int update_agent_import_manifest_path(const char *path);
int update_agent_download_payload(void);
int update_agent_prepare_dry_run(void);
int update_agent_prepare_explain(struct update_prepare_explain *out);
int update_agent_prepare_staged_update(void);
int update_agent_stage_latest(void);
int update_agent_clear_stage(void);
int update_agent_set_pending_activation(int enabled);
void update_agent_status_get(struct system_update_status *out);

/* Transactional update integration with the boot slot system. */
int update_agent_apply_boot_slot(void);
int update_agent_confirm_health(void);
int update_agent_check_rollback(void);

/* Apply the staged boot slot only if the supplied payload SHA-256 hex
 * digest matches the value declared by the staged manifest's
 * `payload_sha256=` field.
 *
 * Returns:
 *   0          on success (slot staged + activated)
 *  -30         staged digest missing, or caller passed NULL/empty
 *  -31         payload digest mismatch (refused; logged as [audit] [update])
 *  -32         payload digest declared but invalid length (must be 64 hex)
 *   <0         poll/apply error, including malformed staged manifest */
int update_agent_apply_boot_slot_verified(const char *actual_sha256_hex);
int update_agent_apply_cached_payload(void);

/* Returns 1 if the currently staged manifest declared a payload sha256
 * digest, 0 otherwise. */
int update_agent_staged_requires_payload_verification(void);

#endif /* CORE_UPDATE_AGENT_H */
