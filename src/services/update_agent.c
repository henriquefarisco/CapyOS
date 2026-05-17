/*
 * src/services/update_agent.c
 *
 * Core/anchor TU for the update_agent state machine. After the
 * 2026-05-15 refactor this file owns:
 *
 *   - the canonical runtime status singleton (`update_agent_g_status`)
 *     and the test-only fetcher/verifier function pointers;
 *   - low-level string and hex helpers used pervasively across the
 *     four update_agent sister TUs;
 *   - the production VFS read/write/remove helpers and the
 *     `active_*` accessors that gate them through the
 *     replaceable function pointers used in UNIT_TEST;
 *   - production HTTP fetchers `fetch_remote_manifest_text` and
 *     `fetch_payload_bytes` (they need the test-only hooks, so they
 *     stay with the globals);
 *   - status seeding (`update_agent_seed_defaults`,
 *     `manifest_view_reset`, `state_view_reset`),
 *     `update_agent_reset`, `update_agent_init`, the public setters
 *     and `update_agent_status_get`.
 *
 * Manifest parsing/validators/branch-URL builders live in
 * `update_agent_parse.c`. The catalog state machine
 * (`update_agent_poll` + apply/stage/clear/arm operations) lives in
 * `update_agent_apply.c`. Boot-slot integration lives in
 * `update_agent_transact.c`. All four TUs share globals and view
 * types through `internal/update_agent_internal.h`.
 */
#include "services/update_agent.h"

#if !defined(UNIT_TEST)
#include "fs/vfs.h"
#include "net/http.h"
#endif

#include "services/internal/update_agent_internal.h"

#include <stddef.h>
#include <stdint.h>

/* ── singleton runtime status + fetcher hooks ───────────────────────── */

struct system_update_status update_agent_g_status;

static update_agent_read_file_fn g_update_reader = NULL;
static update_agent_write_file_fn g_update_writer = NULL;
static update_agent_write_bytes_fn g_update_bytes_writer = NULL;
static update_agent_remove_file_fn g_update_remover = NULL;
#if defined(UNIT_TEST)
update_agent_manifest_verify_fn g_update_manifest_verifier = NULL;
update_agent_fetch_manifest_fn g_update_manifest_fetcher = NULL;
update_agent_fetch_payload_fn g_update_payload_fetcher = NULL;
#endif
static int g_update_ready = 0;

/* ── byte + string helpers ──────────────────────────────────────────── */

void update_agent_local_zero(void *ptr, size_t len) {
  uint8_t *dst = (uint8_t *)ptr;
  while (len--) {
    *dst++ = 0;
  }
}

#if !defined(UNIT_TEST)
static void local_copy_bytes(uint8_t *dst, const uint8_t *src, size_t len) {
  size_t i = 0u;
  if (!dst || !src) {
    return;
  }
  while (i < len) {
    dst[i] = src[i];
    ++i;
  }
}
#endif

void update_agent_local_copy(char *dst, size_t dst_size, const char *src) {
  size_t i = 0;
  if (!dst || dst_size == 0u) {
    return;
  }
  if (src) {
    while (src[i] && i + 1u < dst_size) {
      dst[i] = src[i];
      ++i;
    }
  }
  dst[i] = '\0';
}

void update_agent_local_append(char *dst, size_t dst_size, const char *src) {
  size_t i = 0;
  size_t len = 0;
  if (!dst || dst_size == 0u || !src) {
    return;
  }
  while (dst[len] && len + 1u < dst_size) {
    ++len;
  }
  while (src[i] && len + 1u < dst_size) {
    dst[len++] = src[i++];
  }
  dst[len] = '\0';
}

#if !defined(UNIT_TEST)
static size_t local_len(const char *text) {
  size_t len = 0u;
  if (!text) {
    return 0u;
  }
  while (text[len]) {
    ++len;
  }
  return len;
}
#endif

int update_agent_local_equal(const char *a, const char *b) {
  size_t i = 0;
  if (!a || !b) {
    return 0;
  }
  while (a[i] && b[i]) {
    if (a[i] != b[i]) {
      return 0;
    }
    ++i;
  }
  return a[i] == b[i];
}

int update_agent_local_starts_with(const char *text, const char *prefix) {
  size_t i = 0;
  if (!text || !prefix) {
    return 0;
  }
  while (prefix[i]) {
    if (text[i] != prefix[i]) {
      return 0;
    }
    ++i;
  }
  return 1;
}

int update_agent_local_is_digit(char c) {
  return c >= '0' && c <= '9';
}

int update_agent_local_is_hex_digit(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

int update_agent_local_hex_value(char c) {
  if (c >= '0' && c <= '9') {
    return (int)(c - '0');
  }
  if (c >= 'a' && c <= 'f') {
    return 10 + (int)(c - 'a');
  }
  if (c >= 'A' && c <= 'F') {
    return 10 + (int)(c - 'A');
  }
  return -1;
}

int update_agent_local_hex_string_valid(const char *text, size_t hex_len) {
  size_t i = 0u;
  if (!text || !text[0]) {
    return 0;
  }
  while (i < hex_len) {
    if (!update_agent_local_is_hex_digit(text[i])) {
      return 0;
    }
    ++i;
  }
  return text[i] == '\0';
}

int update_agent_local_hex_to_bytes(const char *hex, uint8_t *out,
                                    size_t out_len) {
  size_t i = 0u;
  if (!hex || !out) {
    return -1;
  }
  while (i < out_len) {
    int hi = update_agent_local_hex_value(hex[i * 2u]);
    int lo = update_agent_local_hex_value(hex[i * 2u + 1u]);
    if (hi < 0 || lo < 0) {
      return -1;
    }
    out[i] = (uint8_t)(((uint8_t)hi << 4) | (uint8_t)lo);
    ++i;
  }
  return hex[out_len * 2u] == '\0' ? 0 : -1;
}

int update_agent_local_hex_equal_fixed(const char *a, const char *b,
                                       size_t hex_len) {
  size_t i = 0u;
  if (!a || !b) {
    return 0;
  }
  while (i < hex_len) {
    if (update_agent_local_hex_value(a[i]) !=
        update_agent_local_hex_value(b[i])) {
      return 0;
    }
    ++i;
  }
  return a[i] == '\0' && b[i] == '\0';
}

int update_agent_parse_bool_value(const char *value) {
  if (!value) {
    return 0;
  }
  return update_agent_local_equal(value, "1") ||
         update_agent_local_equal(value, "yes") ||
         update_agent_local_equal(value, "true") ||
         update_agent_local_equal(value, "enabled") ||
         update_agent_local_equal(value, "on");
}

/* ── filesystem helpers + active_* accessors ────────────────────────── */

static int local_read_file(const char *path, char *buffer, size_t buffer_size,
                           size_t *out_len) {
#if defined(UNIT_TEST)
  (void)path;
  (void)buffer;
  (void)buffer_size;
  (void)out_len;
  return -1;
#else
  struct file *file = NULL;
  long read = 0;

  if (!path || !buffer || buffer_size < 2u) {
    return -1;
  }
  file = vfs_open(path, VFS_OPEN_READ);
  if (!file) {
    return -1;
  }
  read = vfs_read(file, buffer, buffer_size - 1u);
  vfs_close(file);
  if (read < 0) {
    return -1;
  }
  buffer[(size_t)read] = '\0';
  if (out_len) {
    *out_len = (size_t)read;
  }
  return 0;
#endif
}

#if !defined(UNIT_TEST)
static int local_write_file(const char *path, const char *text) {
  struct file *file = NULL;
  size_t len = 0u;
  struct dentry *d = NULL;

  if (!path || !text) {
    return -1;
  }
  len = local_len(text);

  if (vfs_lookup(path, &d) == 0) {
    (void)vfs_unlink(path);
  }
  if (vfs_create(path, VFS_MODE_FILE, NULL) != 0 &&
      vfs_lookup(path, &d) != 0) {
    return -1;
  }
  file = vfs_open(path, VFS_OPEN_WRITE);
  if (!file) {
    return -1;
  }
  if (len > 0u && vfs_write(file, text, len) < 0) {
    vfs_close(file);
    return -1;
  }
  vfs_close(file);
  return 0;
}

static int local_write_bytes(const char *path, const uint8_t *data, size_t len) {
  struct file *file = NULL;
  struct dentry *d = NULL;

  if (!path || (!data && len > 0u)) {
    return -1;
  }
  if (vfs_lookup(path, &d) == 0) {
    (void)vfs_unlink(path);
  }
  if (vfs_create(path, VFS_MODE_FILE, NULL) != 0 &&
      vfs_lookup(path, &d) != 0) {
    return -1;
  }
  file = vfs_open(path, VFS_OPEN_WRITE);
  if (!file) {
    return -1;
  }
  if (len > 0u && vfs_write(file, data, len) != (long)len) {
    vfs_close(file);
    return -1;
  }
  vfs_close(file);
  return 0;
}

static int local_remove_file(const char *path) {
  struct dentry *d = NULL;
  if (!path) {
    return -1;
  }
  if (vfs_lookup(path, &d) != 0) {
    return 0;
  }
  return vfs_unlink(path);
}
#endif

update_agent_read_file_fn update_agent_active_reader(void) {
  return g_update_reader ? g_update_reader : local_read_file;
}

update_agent_write_file_fn update_agent_active_writer(void) {
#if defined(UNIT_TEST)
  return g_update_writer;
#else
  return g_update_writer ? g_update_writer : local_write_file;
#endif
}

update_agent_write_bytes_fn update_agent_active_bytes_writer(void) {
#if defined(UNIT_TEST)
  return g_update_bytes_writer;
#else
  return g_update_bytes_writer ? g_update_bytes_writer : local_write_bytes;
#endif
}

update_agent_remove_file_fn update_agent_active_remover(void) {
#if defined(UNIT_TEST)
  return g_update_remover;
#else
  return g_update_remover ? g_update_remover : local_remove_file;
#endif
}

/* ── remote fetchers (kept with globals due to UNIT_TEST hooks) ─────── */

int update_agent_fetch_remote_manifest_text(const char *url, char *buffer,
                                            size_t buffer_size,
                                            size_t *out_len) {
#if defined(UNIT_TEST)
  if (!g_update_manifest_fetcher) {
    return -1;
  }
  return g_update_manifest_fetcher(url, buffer, buffer_size, out_len);
#else
  struct http_response response;
  size_t i = 0u;
  if (!url || !url[0] || !buffer || buffer_size < 2u) {
    return -1;
  }
  update_agent_local_zero(&response, sizeof(response));
  if (http_get(url, &response) != 0) {
    http_response_free(&response);
    return -1;
  }
  if (response.status_code != 200 || !response.body || response.body_len == 0u) {
    http_response_free(&response);
    return -2;
  }
  if (response.body_len + 1u > buffer_size) {
    http_response_free(&response);
    return -3;
  }
  while (i < response.body_len) {
    buffer[i] = (char)response.body[i];
    ++i;
  }
  buffer[i] = '\0';
  if (out_len) {
    *out_len = i;
  }
  http_response_free(&response);
  return 0;
#endif
}

#if !defined(UNIT_TEST)
static int read_local_payload_bytes(const char *path, uint8_t *buffer,
                                    size_t buffer_size, size_t *out_len) {
  struct vfs_stat st;
  struct file *file = NULL;
  long read = 0;

  if (!path || !buffer || buffer_size == 0u) {
    return -1;
  }
  if (vfs_stat_path(path, &st) != 0 || st.size == 0u ||
      st.size > buffer_size) {
    return -1;
  }
  file = vfs_open(path, VFS_OPEN_READ);
  if (!file) {
    return -1;
  }
  read = vfs_read(file, buffer, (size_t)st.size);
  vfs_close(file);
  if (read < 0 || (size_t)read != (size_t)st.size) {
    return -1;
  }
  if (out_len) {
    *out_len = (size_t)read;
  }
  return 0;
}
#endif

int update_agent_fetch_payload_bytes(const char *url, uint8_t *buffer,
                                     size_t buffer_size, size_t *out_len) {
#if defined(UNIT_TEST)
  if (!g_update_payload_fetcher) {
    return -1;
  }
  return g_update_payload_fetcher(url, buffer, buffer_size, out_len);
#else
  struct http_response response;
  if (!url || !url[0] || !buffer || buffer_size == 0u) {
    return -1;
  }
  if (update_agent_local_starts_with(url, "/system/update/")) {
    return read_local_payload_bytes(url, buffer, buffer_size, out_len);
  }
  update_agent_local_zero(&response, sizeof(response));
  if (http_get(url, &response) != 0) {
    http_response_free(&response);
    return -1;
  }
  if (response.status_code != 200 || !response.body || response.body_len == 0u) {
    http_response_free(&response);
    return -2;
  }
  if (response.body_len > buffer_size) {
    http_response_free(&response);
    return -3;
  }
  local_copy_bytes(buffer, response.body, response.body_len);
  if (out_len) {
    *out_len = response.body_len;
  }
  http_response_free(&response);
  return 0;
#endif
}

/* ── status seeding + view resets ───────────────────────────────────── */

void update_agent_seed_defaults(const char *current_version) {
  update_agent_local_zero(&update_agent_g_status,
                          sizeof(update_agent_g_status));
  update_agent_g_status.configured = 1u;
  update_agent_g_status.catalog_present = 0u;
  update_agent_g_status.update_available = 0u;
  update_agent_g_status.stage_ready = 0u;
  update_agent_g_status.pending_activation = 0u;
  update_agent_g_status.last_result = 1;
  update_agent_local_copy(update_agent_g_status.channel,
                          sizeof(update_agent_g_status.channel),
                          UPDATE_AGENT_DEFAULT_CHANNEL);
  update_agent_local_copy(update_agent_g_status.branch,
                          sizeof(update_agent_g_status.branch),
                          UPDATE_AGENT_DEFAULT_BRANCH);
  update_agent_local_copy(update_agent_g_status.source,
                          sizeof(update_agent_g_status.source),
                          UPDATE_AGENT_DEFAULT_SOURCE);
  update_agent_local_copy(update_agent_g_status.manifest_path,
                          sizeof(update_agent_g_status.manifest_path),
                          UPDATE_AGENT_DEFAULT_MANIFEST_PATH);
  update_agent_g_status.remote_manifest_url[0] = '\0';
  update_agent_local_copy(update_agent_g_status.staged_manifest_path,
                          sizeof(update_agent_g_status.staged_manifest_path),
                          UPDATE_AGENT_DEFAULT_STAGED_MANIFEST_PATH);
  update_agent_local_copy(update_agent_g_status.payload_cache_path,
                          sizeof(update_agent_g_status.payload_cache_path),
                          UPDATE_AGENT_PAYLOAD_CACHE_PATH);
  update_agent_local_copy(update_agent_g_status.current_version,
                          sizeof(update_agent_g_status.current_version),
                          current_version ? current_version : "unknown");
  update_agent_local_copy(update_agent_g_status.summary,
                          sizeof(update_agent_g_status.summary),
                          "catalog cache not checked");
}

void update_agent_manifest_view_reset(struct update_manifest_view *view) {
  if (!view) {
    return;
  }
  update_agent_local_zero(view, sizeof(*view));
}

void update_agent_state_view_reset(struct update_state_view *view) {
  if (!view) {
    return;
  }
  update_agent_local_zero(view, sizeof(*view));
  update_agent_local_copy(view->staged_manifest_path,
                          sizeof(view->staged_manifest_path),
                          UPDATE_AGENT_DEFAULT_STAGED_MANIFEST_PATH);
  update_agent_local_copy(view->payload_cache_path,
                          sizeof(view->payload_cache_path),
                          UPDATE_AGENT_PAYLOAD_CACHE_PATH);
}

/* ── public lifecycle + setters ─────────────────────────────────────── */

void update_agent_reset(void) {
  update_agent_local_zero(&update_agent_g_status,
                          sizeof(update_agent_g_status));
  g_update_reader = NULL;
  g_update_writer = NULL;
  g_update_bytes_writer = NULL;
  g_update_remover = NULL;
#if defined(UNIT_TEST)
  g_update_manifest_verifier = NULL;
  g_update_manifest_fetcher = NULL;
  g_update_payload_fetcher = NULL;
#endif
  g_update_ready = 0;
}

void update_agent_init(const char *current_version) {
  if (g_update_ready) {
    if (current_version && current_version[0]) {
      update_agent_local_copy(update_agent_g_status.current_version,
                              sizeof(update_agent_g_status.current_version),
                              current_version);
    }
    return;
  }
  update_agent_seed_defaults(current_version);
  g_update_ready = 1;
}

void update_agent_set_reader(update_agent_read_file_fn reader) {
  g_update_reader = reader;
}

void update_agent_set_writer(update_agent_write_file_fn writer) {
  g_update_writer = writer;
}

void update_agent_set_bytes_writer(update_agent_write_bytes_fn writer) {
  g_update_bytes_writer = writer;
}

void update_agent_set_remover(update_agent_remove_file_fn remover) {
  g_update_remover = remover;
}

#if defined(UNIT_TEST)
void update_agent_set_manifest_verifier(
    update_agent_manifest_verify_fn verifier) {
  g_update_manifest_verifier = verifier;
}

void update_agent_set_manifest_fetcher(update_agent_fetch_manifest_fn fetcher) {
  g_update_manifest_fetcher = fetcher;
}

void update_agent_set_payload_fetcher(update_agent_fetch_payload_fn fetcher) {
  g_update_payload_fetcher = fetcher;
}
#endif

void update_agent_status_get(struct system_update_status *out) {
  update_agent_init(NULL);
  if (!out) {
    return;
  }
  *out = update_agent_g_status;
}

/* The boot-slot integration (apply, confirm health, rollback) and the
 * M6.4 payload sha256 verification path live in
 * src/services/update_agent_transact.c. They share the runtime status
 * and string helper through src/services/internal/update_agent_internal.h
 * so this file remains under the project monolith threshold while still
 * presenting a single coherent state machine to callers. */
