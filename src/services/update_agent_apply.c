/*
 * src/services/update_agent_apply.c
 *
 * Catalog + staged state machine and the offline manifest import
 * gate for the update agent:
 *   - `update_agent_poll` (catalog + staged + state evaluator)
 *   - `update_agent_import_manifest_path` (offline import gate)
 *
 * Carved out of `src/services/update_agent.c` at the 2026-05-15
 * refactor so each translation unit stays under the 900-line layout
 * limit. The companion file `update_agent_prepare.c` owns the apply
 * + stage + arm operations (fetch_remote_manifest, download_payload,
 * prepare_* family, stage_latest, clear_stage,
 * set_pending_activation). Both files share globals, view types,
 * validators and parsers with `update_agent.c` /
 * `update_agent_parse.c` / `update_agent_transact.c` through
 * `src/services/internal/update_agent_internal.h`.
 */
#include "services/update_agent.h"
#include "kernel/log/klog.h"

#include "services/internal/update_agent_internal.h"

#include <stddef.h>
#include <stdint.h>

int update_agent_poll(void) {
  int manifest_rc = 0;
  int state_rc = 0;
  int staged_rc = 0;
  int rc = 0;
  int manifest_channel_mismatch = 0;
  int manifest_branch_mismatch = 0;
  int manifest_source_mismatch = 0;
  int staged_channel_mismatch = 0;
  int staged_branch_mismatch = 0;
  int staged_source_mismatch = 0;
  int manifest_version_cmp = 0;
  int staged_version_cmp = 0;
  int manifest_version_invalid = 0;
  int staged_version_invalid = 0;
  int manifest_downgrade = 0;
  int staged_downgrade = 0;
  int manifest_payload_invalid = 0;
  int staged_payload_invalid = 0;
  int manifest_payload_url_invalid = 0;
  int staged_payload_url_invalid = 0;
  int manifest_signature_invalid = 0;
  int staged_signature_invalid = 0;
  struct update_manifest_view available_manifest;
  struct update_manifest_view staged_manifest;
  struct update_state_view state_view;

  update_agent_prepare_repository_status();
  update_agent_manifest_view_reset(&available_manifest);
  update_agent_manifest_view_reset(&staged_manifest);
  update_agent_state_view_reset(&state_view);

  update_agent_g_status.catalog_present = 0u;
  update_agent_g_status.update_available = 0u;
  update_agent_g_status.stage_ready = 0u;
  update_agent_g_status.pending_activation = 0u;
  update_agent_g_status.last_result = 0;
  update_agent_g_status.available_version[0] = '\0';
  update_agent_g_status.staged_version[0] = '\0';
  update_agent_g_status.payload_url[0] = '\0';
  update_agent_g_status.staged_payload_url[0] = '\0';
  update_agent_local_copy(update_agent_g_status.payload_cache_path,
                          sizeof(update_agent_g_status.payload_cache_path),
                          UPDATE_AGENT_PAYLOAD_CACHE_PATH);
  update_agent_g_status.payload_cache_sha256[0] = '\0';
  update_agent_g_status.staged_payload_sha256[0] = '\0';
  update_agent_g_status.published_at[0] = '\0';
  update_agent_local_copy(update_agent_g_status.staged_manifest_path,
                          sizeof(update_agent_g_status.staged_manifest_path),
                          UPDATE_AGENT_DEFAULT_STAGED_MANIFEST_PATH);

  state_rc = update_agent_read_state_view(&state_view);
  if (state_rc == 0) {
    update_agent_g_status.pending_activation = state_view.pending_activation;
    update_agent_local_copy(update_agent_g_status.staged_manifest_path,
                            sizeof(update_agent_g_status.staged_manifest_path),
                            state_view.staged_manifest_path);
    update_agent_local_copy(update_agent_g_status.payload_cache_path,
                            sizeof(update_agent_g_status.payload_cache_path),
                            state_view.payload_cache_path);
    update_agent_local_copy(update_agent_g_status.payload_cache_sha256,
                            sizeof(update_agent_g_status.payload_cache_sha256),
                            state_view.payload_cache_sha256);
  }

  manifest_rc = update_agent_read_manifest_view(
      update_agent_g_status.manifest_path, &available_manifest);
  if (manifest_rc == 0) {
    update_agent_g_status.catalog_present = 1u;
    update_agent_local_copy(update_agent_g_status.available_version,
                            sizeof(update_agent_g_status.available_version),
                            available_manifest.version);
    update_agent_local_copy(update_agent_g_status.published_at,
                            sizeof(update_agent_g_status.published_at),
                            available_manifest.published_at);
    update_agent_local_copy(update_agent_g_status.payload_url,
                            sizeof(update_agent_g_status.payload_url),
                            available_manifest.payload_url);
    manifest_channel_mismatch =
        available_manifest.channel[0] &&
        !update_agent_local_equal(available_manifest.channel,
                                  update_agent_g_status.channel);
    manifest_branch_mismatch =
        available_manifest.branch[0] &&
        !update_agent_local_equal(available_manifest.branch,
                                  update_agent_g_status.branch);
    manifest_source_mismatch =
        available_manifest.source[0] &&
        !update_agent_local_equal(available_manifest.source,
                                  update_agent_g_status.source);
    if (update_agent_manifest_compare_current(&available_manifest,
                                              &manifest_version_cmp) != 0) {
      manifest_version_invalid = 1;
    } else if (manifest_version_cmp < 0) {
      manifest_downgrade = 1;
    } else if (manifest_version_cmp > 0 &&
               !update_agent_manifest_payload_sha256_valid(
                   &available_manifest)) {
      manifest_payload_invalid = 1;
    } else if (manifest_version_cmp > 0 &&
               !update_agent_manifest_payload_url_valid(&available_manifest)) {
      manifest_payload_url_invalid = 1;
    } else if (manifest_version_cmp > 0 &&
               !update_agent_manifest_signature_ed25519_valid(
                   &available_manifest)) {
      manifest_signature_invalid = 1;
    } else if (manifest_version_cmp > 0) {
      update_agent_g_status.update_available = 1u;
    }
  }

  staged_rc = update_agent_read_manifest_view(
      update_agent_g_status.staged_manifest_path, &staged_manifest);
  if (staged_rc == 0) {
    update_agent_local_copy(update_agent_g_status.staged_version,
                            sizeof(update_agent_g_status.staged_version),
                            staged_manifest.version);
    update_agent_local_copy(update_agent_g_status.staged_payload_sha256,
                            sizeof(update_agent_g_status.staged_payload_sha256),
                            staged_manifest.payload_sha256);
    update_agent_local_copy(update_agent_g_status.staged_payload_url,
                            sizeof(update_agent_g_status.staged_payload_url),
                            staged_manifest.payload_url);
    staged_channel_mismatch =
        staged_manifest.channel[0] &&
        !update_agent_local_equal(staged_manifest.channel,
                                  update_agent_g_status.channel);
    staged_branch_mismatch =
        staged_manifest.branch[0] &&
        !update_agent_local_equal(staged_manifest.branch,
                                  update_agent_g_status.branch);
    staged_source_mismatch =
        staged_manifest.source[0] &&
        !update_agent_local_equal(staged_manifest.source,
                                  update_agent_g_status.source);
    if (update_agent_manifest_compare_current(&staged_manifest,
                                              &staged_version_cmp) != 0) {
      staged_version_invalid = 1;
    } else if (staged_version_cmp < 0) {
      staged_downgrade = 1;
    } else if (!update_agent_manifest_payload_sha256_valid(&staged_manifest)) {
      staged_payload_invalid = 1;
    } else if (!update_agent_manifest_payload_url_valid(&staged_manifest)) {
      staged_payload_url_invalid = 1;
    } else if (!update_agent_manifest_signature_ed25519_valid(
                   &staged_manifest)) {
      staged_signature_invalid = 1;
    } else {
      update_agent_g_status.stage_ready = 1u;
    }
  }

  if (update_agent_g_status.payload_cache_sha256[0]) {
    int cache_matches_available =
        manifest_rc == 0 &&
        update_agent_manifest_payload_sha256_valid(&available_manifest) &&
        update_agent_local_hex_equal_fixed(
            update_agent_g_status.payload_cache_sha256,
            available_manifest.payload_sha256,
            UPDATE_AGENT_SHA256_HEX_LEN);
    int cache_matches_staged =
        staged_rc == 0 &&
        update_agent_manifest_payload_sha256_valid(&staged_manifest) &&
        update_agent_local_hex_equal_fixed(
            update_agent_g_status.payload_cache_sha256,
            staged_manifest.payload_sha256, UPDATE_AGENT_SHA256_HEX_LEN);
    if (!cache_matches_available && !cache_matches_staged) {
      update_agent_g_status.payload_cache_sha256[0] = '\0';
    }
  }

  if (manifest_channel_mismatch || manifest_branch_mismatch ||
      manifest_source_mismatch) {
    rc = -13;
    update_agent_local_copy(
        update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
        "catalog cache does not match selected update repository");
  } else if (staged_channel_mismatch || staged_branch_mismatch ||
             staged_source_mismatch) {
    rc = -14;
    update_agent_local_copy(
        update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
        "staged update does not match selected update repository");
  } else if (manifest_rc == -2) {
    rc = -2;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "catalog cache invalid");
  } else if (staged_rc == -2) {
    rc = -3;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "staged update invalid");
  } else if (manifest_version_invalid) {
    rc = -22;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "catalog cache version invalid");
  } else if (staged_version_invalid) {
    rc = -23;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "staged update version invalid");
  } else if (manifest_downgrade) {
    rc = -24;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "catalog cache version is older than current system");
  } else if (staged_downgrade) {
    rc = -25;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "staged update version is older than current system");
  } else if (manifest_payload_invalid) {
    rc = -26;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "catalog cache missing or malformed payload sha256");
  } else if (staged_payload_invalid) {
    rc = -27;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "staged update missing or malformed payload sha256");
  } else if (manifest_payload_url_invalid) {
    rc = -37;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "catalog cache missing or malformed payload url");
  } else if (staged_payload_url_invalid) {
    rc = -38;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "staged update missing or malformed payload url");
  } else if (manifest_signature_invalid) {
    rc = -28;
    update_agent_local_copy(
        update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
        "catalog cache missing or invalid ed25519 signature");
  } else if (staged_signature_invalid) {
    rc = -29;
    update_agent_local_copy(
        update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
        "staged update missing or invalid ed25519 signature");
  } else if (update_agent_g_status.pending_activation &&
             !update_agent_g_status.stage_ready) {
    rc = -4;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "activation pending without staged update");
  } else if (update_agent_g_status.pending_activation &&
             update_agent_g_status.stage_ready) {
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "staged update armed for activation");
  } else if (update_agent_g_status.stage_ready) {
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "staged update ready");
  } else if (!update_agent_g_status.catalog_present) {
    update_agent_g_status.last_result = 1;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "catalog cache missing");
    return 0;
  } else if (update_agent_g_status.update_available) {
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "update available in local catalog");
  } else {
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "system already matches cached catalog");
  }

  update_agent_g_status.last_result = rc;
  return rc;
}

int update_agent_import_manifest_path(const char *path) {
  char buffer[768];
  size_t read_len = 0u;
  int rc = 0;
  int import_version_cmp = 0;
  update_agent_read_file_fn reader = update_agent_active_reader();
  update_agent_write_file_fn writer = update_agent_active_writer();
  struct update_manifest_view import_manifest;

  if (!path || !path[0]) {
    update_agent_init(NULL);
    update_agent_g_status.last_result = -15;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "manifest path not provided");
    return -15;
  }

  update_agent_prepare_repository_status();
  update_agent_manifest_view_reset(&import_manifest);

  if (!writer) {
    update_agent_g_status.last_result = -16;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "update cache writer unavailable");
    return -16;
  }
  if (reader(path, buffer, sizeof(buffer), &read_len) != 0 || read_len == 0u) {
    update_agent_g_status.last_result = -17;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "failed to read imported manifest");
    return -17;
  }
  if (update_agent_manifest_capture_signed_text(buffer, read_len,
                                                &import_manifest) != 0) {
    update_agent_g_status.last_result = -18;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "imported manifest invalid");
    return -18;
  }
  update_agent_parse_buffer(buffer, read_len, 1, &import_manifest);
  if (!import_manifest.version[0]) {
    update_agent_g_status.last_result = -18;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "imported manifest invalid");
    return -18;
  }
  if (update_agent_manifest_compare_current(&import_manifest,
                                            &import_version_cmp) != 0) {
    update_agent_g_status.last_result = -18;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "imported manifest invalid");
    return -18;
  }
  if ((import_manifest.channel[0] &&
       !update_agent_local_equal(import_manifest.channel,
                                 update_agent_g_status.channel)) ||
      (import_manifest.branch[0] &&
       !update_agent_local_equal(import_manifest.branch,
                                 update_agent_g_status.branch)) ||
      (import_manifest.source[0] &&
       !update_agent_local_equal(import_manifest.source,
                                 update_agent_g_status.source))) {
    update_agent_g_status.last_result = -19;
    update_agent_local_copy(
        update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
        "imported manifest does not match selected update repository");
    klog(KLOG_WARN, "[update] Manifest import rejected: repository mismatch.");
    return -19;
  }
  if (import_version_cmp <= 0) {
    update_agent_g_status.last_result = -20;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "imported manifest not newer than current system");
    klog(KLOG_WARN, "[update] Manifest import rejected: version is not newer.");
    return -20;
  }
  if (!update_agent_manifest_payload_sha256_valid(&import_manifest)) {
    update_agent_g_status.last_result = -22;
    update_agent_local_copy(
        update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
        "imported manifest missing or malformed payload sha256");
    klog(KLOG_WARN, "[update] Manifest import rejected: payload sha256 invalid.");
    return -22;
  }
  if (!update_agent_manifest_payload_url_valid(&import_manifest)) {
    update_agent_g_status.last_result = -39;
    update_agent_local_copy(
        update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
        "imported manifest missing or malformed payload url");
    klog(KLOG_WARN, "[update] Manifest import rejected: payload url invalid.");
    return -39;
  }
  if (!update_agent_manifest_signature_ed25519_valid(&import_manifest)) {
    update_agent_g_status.last_result = -23;
    update_agent_local_copy(
        update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
        "imported manifest missing or invalid ed25519 signature");
    klog(KLOG_WARN, "[audit] [update] manifest ed25519 signature invalid -> refused");
    return -23;
  }
  if (writer(update_agent_g_status.manifest_path, buffer) != 0) {
    update_agent_g_status.last_result = -21;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "failed to persist imported manifest");
    klog(KLOG_WARN, "[update] Failed to persist imported manifest.");
    return -21;
  }

  rc = update_agent_poll();
  if (rc < 0) {
    return rc;
  }
  update_agent_local_copy(update_agent_g_status.summary,
                          sizeof(update_agent_g_status.summary),
                          "manifest imported into local catalog");
  update_agent_g_status.last_result = 0;
  klog(KLOG_INFO, "[update] Manifest imported into local catalog.");
  return 0;
}

