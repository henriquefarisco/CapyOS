/*
 * src/services/update_agent_prepare.c
 *
 * Apply + stage + arm phase of the update_agent state machine:
 *   - `write_state_file` (state.ini emitter, shared by this TU)
 *   - `update_agent_fetch_remote_manifest`
 *   - `update_agent_download_payload`
 *   - `update_agent_prepare_dry_run` / `_explain` /
 *     `_prepare_staged_update`
 *   - `update_agent_stage_latest`
 *   - `update_agent_clear_stage`
 *   - `update_agent_set_pending_activation`
 *
 * Carved out of `src/services/update_agent.c` at the 2026-05-15
 * refactor so each translation unit stays under the 900-line layout
 * limit. The state-machine evaluator (`update_agent_poll`) and the
 * offline manifest import gate
 * (`update_agent_import_manifest_path`) live in
 * `update_agent_apply.c`. All sister TUs share globals, view types,
 * validators and parsers through
 * `src/services/internal/update_agent_internal.h`.
 */
#include "services/update_agent.h"
#include "kernel/log/klog.h"
#include "security/sha256.h"

#if !defined(UNIT_TEST)
#include "memory/kmem.h"
#endif

#include "services/internal/update_agent_internal.h"

#include <stddef.h>
#include <stdint.h>

static int write_state_file(int pending_activation,
                            const char *staged_manifest_path) {
  char text[320];
  update_agent_write_file_fn writer = update_agent_active_writer();

  if (!writer || !staged_manifest_path || !staged_manifest_path[0]) {
    return -1;
  }

  text[0] = '\0';
  update_agent_local_append(text, sizeof(text), "pending_activation=");
  update_agent_local_append(text, sizeof(text), pending_activation ? "1" : "0");
  update_agent_local_append(text, sizeof(text), "\n");
  update_agent_local_append(text, sizeof(text), "staged_manifest=");
  update_agent_local_append(text, sizeof(text), staged_manifest_path);
  update_agent_local_append(text, sizeof(text), "\n");
  if (update_agent_g_status.payload_cache_path[0] &&
      update_agent_g_status.payload_cache_sha256[0]) {
    update_agent_local_append(text, sizeof(text), "payload_cache=");
    update_agent_local_append(text, sizeof(text),
                              update_agent_g_status.payload_cache_path);
    update_agent_local_append(text, sizeof(text), "\n");
    update_agent_local_append(text, sizeof(text), "payload_cache_sha256=");
    update_agent_local_append(text, sizeof(text),
                              update_agent_g_status.payload_cache_sha256);
    update_agent_local_append(text, sizeof(text), "\n");
  }
  return writer(UPDATE_AGENT_STATE_PATH, text);
}

int update_agent_fetch_remote_manifest(void) {
  char buffer[UPDATE_AGENT_MANIFEST_TEXT_MAX];
  size_t fetch_len = 0u;
  int rc = 0;
  update_agent_write_file_fn writer = NULL;
  update_agent_remove_file_fn remover = NULL;

  update_agent_prepare_repository_status();
  writer = update_agent_active_writer();
  remover = update_agent_active_remover();

  if (!update_agent_g_status.remote_manifest_url[0]) {
    update_agent_g_status.last_result = -34;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "remote manifest URL unavailable");
    return -34;
  }
  if (!writer) {
    update_agent_g_status.last_result = -35;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "remote manifest writer unavailable");
    return -35;
  }

  rc = update_agent_fetch_remote_manifest_text(
      update_agent_g_status.remote_manifest_url, buffer, sizeof(buffer),
      &fetch_len);
  if (rc != 0 || fetch_len == 0u || fetch_len >= sizeof(buffer)) {
    update_agent_g_status.last_result = -36;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "remote manifest fetch failed");
    klog(KLOG_WARN, "[audit] [update] remote manifest fetch failed");
    return -36;
  }
  buffer[fetch_len] = '\0';

  if (writer(UPDATE_AGENT_FETCHED_MANIFEST_PATH, buffer) != 0) {
    update_agent_g_status.last_result = -46;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "failed to persist fetched manifest");
    klog(KLOG_WARN, "[update] Failed to persist fetched manifest.");
    return -46;
  }

  rc = update_agent_import_manifest_path(UPDATE_AGENT_FETCHED_MANIFEST_PATH);
  if (remover) {
    (void)remover(UPDATE_AGENT_FETCHED_MANIFEST_PATH);
  }
  if (rc < 0) {
    klog(KLOG_WARN, "[audit] [update] fetched manifest rejected");
    return rc;
  }

  update_agent_local_copy(update_agent_g_status.summary,
                          sizeof(update_agent_g_status.summary),
                          "remote manifest fetched into local catalog");
  update_agent_g_status.last_result = 0;
  klog(KLOG_INFO, "[audit] [update] remote manifest fetched and accepted");
  return 0;
}

int update_agent_download_payload(void) {
#if defined(UNIT_TEST)
  static uint8_t payload_storage[UPDATE_AGENT_PAYLOAD_MAX_BYTES];
  uint8_t *payload_buffer = payload_storage;
#else
  uint8_t *payload_buffer = NULL;
#endif
  struct update_manifest_view manifest;
  uint8_t digest[SHA256_DIGEST_SIZE];
  char digest_hex[UPDATE_AGENT_SHA256_HEX_MAX];
  size_t payload_len = 0u;
  size_t payload_limit = UPDATE_AGENT_PAYLOAD_MAX_BYTES;
  int rc = 0;
  update_agent_write_bytes_fn writer = NULL;

  rc = update_agent_poll();
  if (rc < 0) {
    return rc;
  }
  if (!update_agent_g_status.update_available ||
      !update_agent_g_status.payload_url[0]) {
    update_agent_g_status.last_result = -40;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "no update payload available to download");
    return -40;
  }
  writer = update_agent_active_bytes_writer();
  if (!writer) {
    update_agent_g_status.last_result = -41;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "payload cache writer unavailable");
    return -41;
  }

  update_agent_manifest_view_reset(&manifest);
  if (update_agent_read_manifest_view(update_agent_g_status.manifest_path,
                                      &manifest) != 0 ||
      !update_agent_manifest_payload_sha256_valid(&manifest) ||
      !update_agent_manifest_payload_url_valid(&manifest) ||
      !update_agent_manifest_signature_ed25519_valid(&manifest) ||
      !update_agent_local_equal(manifest.payload_url,
                                update_agent_g_status.payload_url)) {
    update_agent_g_status.last_result = -43;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "payload manifest unavailable");
    return -43;
  }

#if !defined(UNIT_TEST)
  payload_buffer = (uint8_t *)kalloc(payload_limit);
  if (!payload_buffer) {
    update_agent_g_status.last_result = -48;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "payload download buffer unavailable");
    return -48;
  }
#endif

  rc = update_agent_fetch_payload_bytes(manifest.payload_url, payload_buffer,
                                        payload_limit, &payload_len);
  if (rc != 0 || payload_len == 0u || payload_len > payload_limit) {
    update_agent_g_status.last_result = -42;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "payload download failed");
    klog(KLOG_WARN, "[audit] [update] payload download failed");
#if !defined(UNIT_TEST)
    kfree(payload_buffer);
#endif
    return -42;
  }

  sha256_hash(payload_buffer, payload_len, digest);
  sha256_hex(digest, digest_hex);
  if (!update_agent_local_hex_equal_fixed(digest_hex, manifest.payload_sha256,
                                          UPDATE_AGENT_SHA256_HEX_LEN)) {
    update_agent_g_status.last_result = -44;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "payload sha256 mismatch; cache refused");
    klog(KLOG_ERROR, "[audit] [update] downloaded payload sha256 mismatch");
#if !defined(UNIT_TEST)
    kfree(payload_buffer);
#endif
    return -44;
  }

  if (writer(update_agent_g_status.payload_cache_path, payload_buffer,
             payload_len) != 0) {
    update_agent_g_status.last_result = -45;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "failed to persist payload cache");
    klog(KLOG_WARN, "[update] Failed to persist payload cache.");
#if !defined(UNIT_TEST)
    kfree(payload_buffer);
#endif
    return -45;
  }

#if !defined(UNIT_TEST)
  kfree(payload_buffer);
#endif
  update_agent_local_copy(update_agent_g_status.payload_cache_sha256,
                          sizeof(update_agent_g_status.payload_cache_sha256),
                          digest_hex);
  if (write_state_file(update_agent_g_status.pending_activation,
                       update_agent_g_status.staged_manifest_path[0]
                           ? update_agent_g_status.staged_manifest_path
                           : UPDATE_AGENT_DEFAULT_STAGED_MANIFEST_PATH) != 0) {
    update_agent_g_status.last_result = -47;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "failed to persist payload cache state");
    klog(KLOG_WARN, "[update] Failed to persist payload cache state.");
    return -47;
  }
  update_agent_g_status.last_result = 0;
  update_agent_local_copy(update_agent_g_status.summary,
                          sizeof(update_agent_g_status.summary),
                          "payload downloaded and verified");
  klog(KLOG_INFO, "[audit] [update] payload downloaded and verified");
  return 0;
}

static void prepare_explain_reset(struct update_prepare_explain *explain) {
  if (!explain) {
    return;
  }
  update_agent_local_zero(explain, sizeof(*explain));
  explain->result = 1;
  update_agent_local_copy(explain->failing_gate,
                          sizeof(explain->failing_gate), "not-checked");
  update_agent_local_copy(explain->summary, sizeof(explain->summary),
                          "prepare explain not checked");
}

static int prepare_explain_finish(struct update_prepare_explain *explain,
                                  int rc, const char *gate,
                                  const char *summary) {
  if (explain) {
    explain->result = rc;
    update_agent_local_copy(explain->failing_gate,
                            sizeof(explain->failing_gate), gate);
    update_agent_local_copy(explain->summary, sizeof(explain->summary),
                            summary);
  }
  update_agent_g_status.last_result = rc;
  update_agent_local_copy(update_agent_g_status.summary,
                          sizeof(update_agent_g_status.summary), summary);
  return rc;
}

int update_agent_prepare_dry_run(void) {
  struct update_manifest_view manifest;
  int rc = update_agent_poll();

  if (rc < 0) {
    return rc;
  }
  if (!update_agent_g_status.catalog_present ||
      !update_agent_g_status.update_available) {
    update_agent_g_status.last_result = -51;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "no cached update available for prepare dry-run");
    return -51;
  }
  update_agent_manifest_view_reset(&manifest);
  if (update_agent_read_manifest_view(update_agent_g_status.manifest_path,
                                      &manifest) != 0 ||
      !update_agent_manifest_payload_sha256_valid(&manifest) ||
      !update_agent_manifest_payload_url_valid(&manifest) ||
      !update_agent_manifest_signature_ed25519_valid(&manifest) ||
      !update_agent_local_equal(manifest.payload_url,
                                update_agent_g_status.payload_url)) {
    update_agent_g_status.last_result = -52;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "prepare dry-run catalog invalid");
    return -52;
  }
  if (!update_agent_g_status.payload_cache_sha256[0] ||
      !update_agent_local_hex_equal_fixed(
          update_agent_g_status.payload_cache_sha256, manifest.payload_sha256,
          UPDATE_AGENT_SHA256_HEX_LEN)) {
    update_agent_g_status.last_result = -53;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "prepare dry-run requires verified payload cache");
    return -53;
  }
  update_agent_g_status.last_result = 0;
  update_agent_local_copy(update_agent_g_status.summary,
                          sizeof(update_agent_g_status.summary),
                          "prepare dry-run passed; local catalog is ready");
  klog(KLOG_INFO, "[audit] [update] prepare dry-run passed");
  return 0;
}

int update_agent_prepare_explain(struct update_prepare_explain *out) {
  struct update_manifest_view manifest;
  int poll_rc = 0;
  int manifest_rc = 0;
  int version_cmp = 0;
  int repository_ready = 0;

  if (!out) {
    update_agent_g_status.last_result = -54;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "prepare explain output unavailable");
    return -54;
  }

  prepare_explain_reset(out);
  update_agent_manifest_view_reset(&manifest);
  poll_rc = update_agent_poll();
  out->poll_ready = poll_rc >= 0 ? 1u : 0u;

  manifest_rc = update_agent_read_manifest_view(
      update_agent_g_status.manifest_path, &manifest);
  if (manifest_rc != 0) {
    return prepare_explain_finish(out, -51, "catalog",
                                  "prepare explain: catalog missing");
  }

  out->catalog_ready = 1u;
  repository_ready =
      (!manifest.channel[0] ||
       update_agent_local_equal(manifest.channel,
                                update_agent_g_status.channel)) &&
      (!manifest.branch[0] ||
       update_agent_local_equal(manifest.branch,
                                update_agent_g_status.branch)) &&
      (!manifest.source[0] ||
       update_agent_local_equal(manifest.source,
                                update_agent_g_status.source));
  out->repository_ready = repository_ready ? 1u : 0u;
  out->payload_sha256_ready =
      update_agent_manifest_payload_sha256_valid(&manifest) ? 1u : 0u;
  out->payload_url_ready =
      (update_agent_manifest_payload_url_valid(&manifest) &&
       update_agent_local_equal(manifest.payload_url,
                                update_agent_g_status.payload_url))
          ? 1u
          : 0u;
  out->signature_ready =
      update_agent_manifest_signature_ed25519_valid(&manifest) ? 1u : 0u;

  if (update_agent_manifest_compare_current(&manifest, &version_cmp) == 0 &&
      version_cmp > 0) {
    out->version_ready = 1u;
  }
  if (out->payload_sha256_ready &&
      update_agent_g_status.payload_cache_sha256[0] &&
      update_agent_local_hex_equal_fixed(
          update_agent_g_status.payload_cache_sha256, manifest.payload_sha256,
          UPDATE_AGENT_SHA256_HEX_LEN)) {
    out->cache_ready = 1u;
  }

  out->stage_safe = out->poll_ready && out->catalog_ready &&
                    out->repository_ready && out->version_ready &&
                    out->payload_sha256_ready && out->payload_url_ready &&
                    out->signature_ready && out->cache_ready
                        ? 1u
                        : 0u;

  if (!out->repository_ready) {
    return prepare_explain_finish(out, -52, "repository",
                                  "prepare explain: repository mismatch");
  }
  if (!out->version_ready) {
    return prepare_explain_finish(out, -51, "version",
                                  "prepare explain: no newer catalog update");
  }
  if (!out->payload_sha256_ready) {
    return prepare_explain_finish(out, -52, "payload_sha256",
                                  "prepare explain: payload sha256 invalid");
  }
  if (!out->payload_url_ready) {
    return prepare_explain_finish(out, -52, "payload_url",
                                  "prepare explain: payload url invalid");
  }
  if (!out->signature_ready) {
    return prepare_explain_finish(out, -52, "signature",
                                  "prepare explain: signature invalid");
  }
  if (!out->cache_ready) {
    return prepare_explain_finish(out, -53, "cache",
                                  "prepare explain: verified payload cache missing");
  }
  if (!out->poll_ready) {
    return prepare_explain_finish(out, poll_rc, "poll",
                                  update_agent_g_status.summary);
  }

  return prepare_explain_finish(out, 0, "-",
                                "prepare explain: all prepare gates passed");
}

int update_agent_prepare_staged_update(void) {
  int rc = update_agent_fetch_remote_manifest();
  if (rc < 0) {
    return rc;
  }
  rc = update_agent_download_payload();
  if (rc < 0) {
    return rc;
  }
  rc = update_agent_stage_latest();
  if (rc < 0) {
    return rc;
  }
  rc = update_agent_set_pending_activation(1);
  if (rc < 0) {
    return rc;
  }
  update_agent_g_status.last_result = 0;
  update_agent_local_copy(update_agent_g_status.summary,
                          sizeof(update_agent_g_status.summary),
                          "update prepared and armed for activation");
  klog(KLOG_INFO, "[audit] [update] update prepared and armed");
  return 0;
}

int update_agent_stage_latest(void) {
  char buffer[768];
  size_t read_len = 0u;
  struct update_manifest_view manifest;
  update_agent_read_file_fn reader = update_agent_active_reader();
  update_agent_write_file_fn writer = update_agent_active_writer();
  int rc = update_agent_poll();

  if (rc < 0) {
    return rc;
  }
  if (!update_agent_g_status.catalog_present ||
      !update_agent_g_status.update_available) {
    update_agent_g_status.last_result = -5;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "no cached update available to stage");
    return -5;
  }
  if (!writer) {
    update_agent_g_status.last_result = -6;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "update staging writer unavailable");
    return -6;
  }
  if (reader(update_agent_g_status.manifest_path, buffer, sizeof(buffer),
             &read_len) != 0 ||
      read_len == 0u) {
    update_agent_g_status.last_result = -7;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "failed to read cached manifest for staging");
    return -7;
  }
  update_agent_manifest_view_reset(&manifest);
  if (update_agent_manifest_capture_signed_text(buffer, read_len, &manifest) !=
      0) {
    update_agent_g_status.last_result = -7;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "failed to read cached manifest for staging");
    return -7;
  }
  update_agent_parse_buffer(buffer, read_len, 1, &manifest);
  if (!update_agent_g_status.payload_cache_sha256[0] ||
      !update_agent_manifest_payload_sha256_valid(&manifest) ||
      !update_agent_manifest_signature_ed25519_valid(&manifest) ||
      !update_agent_local_hex_equal_fixed(
          update_agent_g_status.payload_cache_sha256, manifest.payload_sha256,
          UPDATE_AGENT_SHA256_HEX_LEN)) {
    update_agent_g_status.last_result = -49;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "payload cache missing or unverified for staging");
    return -49;
  }
  if (writer(update_agent_g_status.staged_manifest_path, buffer) != 0 ||
      write_state_file(0, update_agent_g_status.staged_manifest_path) != 0) {
    update_agent_g_status.last_result = -9;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "failed to persist staged update");
    klog(KLOG_WARN, "[update] Failed to persist staged update.");
    return -9;
  }
  klog(KLOG_INFO, "[update] Update staged.");
  return update_agent_poll();
}

int update_agent_clear_stage(void) {
  update_agent_remove_file_fn remover = update_agent_active_remover();

  update_agent_init(NULL);
  if (remover) {
    (void)remover(update_agent_g_status.staged_manifest_path[0]
                      ? update_agent_g_status.staged_manifest_path
                      : UPDATE_AGENT_DEFAULT_STAGED_MANIFEST_PATH);
    (void)remover(update_agent_g_status.payload_cache_path[0]
                      ? update_agent_g_status.payload_cache_path
                      : UPDATE_AGENT_PAYLOAD_CACHE_PATH);
    (void)remover(UPDATE_AGENT_STATE_PATH);
  }
  klog(KLOG_INFO, "[update] Staged update cleared.");
  return update_agent_poll();
}

int update_agent_set_pending_activation(int enabled) {
  int rc = update_agent_poll();

  if (rc < 0) {
    return rc;
  }
  if (enabled) {
    if (!update_agent_g_status.stage_ready) {
      update_agent_g_status.last_result = -10;
      update_agent_local_copy(update_agent_g_status.summary,
                              sizeof(update_agent_g_status.summary),
                              "no staged update available to arm");
      return -10;
    }
    if (write_state_file(1, update_agent_g_status.staged_manifest_path) != 0) {
      update_agent_g_status.last_result = -11;
      update_agent_local_copy(update_agent_g_status.summary,
                              sizeof(update_agent_g_status.summary),
                              "failed to arm staged update");
      klog(KLOG_WARN, "[update] Failed to arm staged update.");
      return -11;
    }
    klog(KLOG_INFO, "[update] Update armed for activation.");
  } else if (update_agent_g_status.stage_ready) {
    if (write_state_file(0, update_agent_g_status.staged_manifest_path) != 0) {
      update_agent_g_status.last_result = -12;
      update_agent_local_copy(update_agent_g_status.summary,
                              sizeof(update_agent_g_status.summary),
                              "failed to disarm staged update");
      klog(KLOG_WARN, "[update] Failed to disarm staged update.");
      return -12;
    }
    klog(KLOG_INFO, "[update] Update activation disarmed.");
  } else {
    update_agent_remove_file_fn remover = update_agent_active_remover();
    if (remover) {
      (void)remover(UPDATE_AGENT_STATE_PATH);
    }
  }
  return update_agent_poll();
}
