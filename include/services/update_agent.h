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
/* SHA-256 hex digest = 64 ASCII chars + NUL. */
#define UPDATE_AGENT_SHA256_HEX_LEN 64u
#define UPDATE_AGENT_SHA256_HEX_MAX (UPDATE_AGENT_SHA256_HEX_LEN + 1u)

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
  char published_at[24];
  char summary[UPDATE_AGENT_SUMMARY_MAX];
  /* Lower-case hex sha256 of the staged payload, taken verbatim from the
   * manifest (`payload_sha256=...`). Empty string when the manifest does
   * not declare a hash. Used by update_agent_apply_boot_slot_verified(). */
  char staged_payload_sha256[UPDATE_AGENT_SHA256_HEX_MAX];
};

typedef int (*update_agent_read_file_fn)(const char *path, char *buffer,
                                         size_t buffer_size,
                                         size_t *out_len);
typedef int (*update_agent_write_file_fn)(const char *path, const char *text);
typedef int (*update_agent_remove_file_fn)(const char *path);

void update_agent_reset(void);
void update_agent_init(const char *current_version);
void update_agent_set_reader(update_agent_read_file_fn reader);
void update_agent_set_writer(update_agent_write_file_fn writer);
void update_agent_set_remover(update_agent_remove_file_fn remover);
int update_agent_poll(void);
int update_agent_import_manifest_path(const char *path);
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
 *  -30         payload digest declared but caller passed NULL or empty
 *  -31         payload digest mismatch (refused; logged as [audit] [update])
 *  -32         payload digest declared but invalid length (must be 64 hex)
 *   <0         underlying update_agent_apply_boot_slot() error
 *
 * If the staged manifest does NOT declare `payload_sha256` (legacy path),
 * the call falls back to update_agent_apply_boot_slot() unchanged so that
 * existing tests and call sites keep working. */
int update_agent_apply_boot_slot_verified(const char *actual_sha256_hex);

/* Returns 1 if the currently staged manifest declared a payload sha256
 * digest, 0 otherwise. Useful for callers that want to refuse to call
 * update_agent_apply_boot_slot() without verification when a digest is
 * present. */
int update_agent_staged_requires_payload_verification(void);

#endif /* CORE_UPDATE_AGENT_H */
