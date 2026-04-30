#include "services/update_agent.h"
#include "boot/boot_slot.h"
#include "kernel/log/klog.h"

#if !defined(UNIT_TEST)
#include "fs/vfs.h"
#endif

#include <stddef.h>

#define UPDATE_AGENT_REPOSITORY_PATH "/system/update/repository.ini"
#define UPDATE_AGENT_STATE_PATH "/system/update/state.ini"
#define UPDATE_AGENT_DEFAULT_MANIFEST_PATH "/system/update/latest.ini"
#define UPDATE_AGENT_DEFAULT_STAGED_MANIFEST_PATH "/system/update/staged.ini"
#define UPDATE_AGENT_DEFAULT_CHANNEL "stable"
#define UPDATE_AGENT_DEFAULT_BRANCH "main"
#define UPDATE_AGENT_DEFAULT_SOURCE "github:henriquefarisco/CapyOS"
#define UPDATE_AGENT_GITHUB_PREFIX "github:"
#define UPDATE_AGENT_REMOTE_MANIFEST_SUFFIX "/system/update/latest.ini"

struct update_manifest_view {
  char version[UPDATE_AGENT_VERSION_MAX];
  char channel[UPDATE_AGENT_CHANNEL_MAX];
  char branch[UPDATE_AGENT_BRANCH_MAX];
  char source[UPDATE_AGENT_SOURCE_MAX];
  char published_at[24];
  char payload_sha256[UPDATE_AGENT_SHA256_HEX_MAX];
};

struct update_state_view {
  uint8_t pending_activation;
  char staged_manifest_path[UPDATE_AGENT_PATH_MAX];
};

struct system_update_status update_agent_g_status;
static update_agent_read_file_fn g_update_reader = NULL;
static update_agent_write_file_fn g_update_writer = NULL;
static update_agent_remove_file_fn g_update_remover = NULL;
static int g_update_ready = 0;

static void local_zero(void *ptr, size_t len) {
  uint8_t *dst = (uint8_t *)ptr;
  while (len--) {
    *dst++ = 0;
  }
}

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

static void local_append(char *dst, size_t dst_size, const char *src) {
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

static int local_equal(const char *a, const char *b) {
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

static int local_starts_with(const char *text, const char *prefix) {
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

static const char *branch_for_channel(const char *channel) {
  return local_equal(channel, "develop") ? "develop" : UPDATE_AGENT_DEFAULT_BRANCH;
}

static void build_remote_manifest_url(const char *source, const char *branch,
                                      char *dst, size_t dst_size) {
  if (!dst || dst_size == 0u) {
    return;
  }
  dst[0] = '\0';
  if (!source || !source[0] || !local_starts_with(source, UPDATE_AGENT_GITHUB_PREFIX)) {
    return;
  }
  local_append(dst, dst_size, "https://raw.githubusercontent.com/");
  local_append(dst, dst_size, source + 7u);
  local_append(dst, dst_size, "/");
  local_append(dst, dst_size,
               (branch && branch[0]) ? branch : UPDATE_AGENT_DEFAULT_BRANCH);
  local_append(dst, dst_size, UPDATE_AGENT_REMOTE_MANIFEST_SUFFIX);
}

static int parse_bool_value(const char *value) {
  if (!value) {
    return 0;
  }
  return local_equal(value, "1") || local_equal(value, "yes") ||
         local_equal(value, "true") || local_equal(value, "enabled") ||
         local_equal(value, "on");
}

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
  while (text[len]) {
    ++len;
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
  if (len > 0u && vfs_write(file, text, len) < 0) {
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

static update_agent_read_file_fn active_reader(void) {
  return g_update_reader ? g_update_reader : local_read_file;
}

static update_agent_write_file_fn active_writer(void) {
#if defined(UNIT_TEST)
  return g_update_writer;
#else
  return g_update_writer ? g_update_writer : local_write_file;
#endif
}

static update_agent_remove_file_fn active_remover(void) {
#if defined(UNIT_TEST)
  return g_update_remover;
#else
  return g_update_remover ? g_update_remover : local_remove_file;
#endif
}

static void update_agent_seed_defaults(const char *current_version) {
  local_zero(&update_agent_g_status, sizeof(update_agent_g_status));
  update_agent_g_status.configured = 1u;
  update_agent_g_status.catalog_present = 0u;
  update_agent_g_status.update_available = 0u;
  update_agent_g_status.stage_ready = 0u;
  update_agent_g_status.pending_activation = 0u;
  update_agent_g_status.last_result = 1;
  update_agent_local_copy(update_agent_g_status.channel, sizeof(update_agent_g_status.channel),
             UPDATE_AGENT_DEFAULT_CHANNEL);
  update_agent_local_copy(update_agent_g_status.branch, sizeof(update_agent_g_status.branch),
             UPDATE_AGENT_DEFAULT_BRANCH);
  update_agent_local_copy(update_agent_g_status.source, sizeof(update_agent_g_status.source),
             UPDATE_AGENT_DEFAULT_SOURCE);
  update_agent_local_copy(update_agent_g_status.manifest_path, sizeof(update_agent_g_status.manifest_path),
             UPDATE_AGENT_DEFAULT_MANIFEST_PATH);
  update_agent_g_status.remote_manifest_url[0] = '\0';
  update_agent_local_copy(update_agent_g_status.staged_manifest_path,
             sizeof(update_agent_g_status.staged_manifest_path),
             UPDATE_AGENT_DEFAULT_STAGED_MANIFEST_PATH);
  update_agent_local_copy(update_agent_g_status.current_version,
             sizeof(update_agent_g_status.current_version),
             current_version ? current_version : "unknown");
  update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
             "catalog cache not checked");
}

static void manifest_view_reset(struct update_manifest_view *view) {
  if (!view) {
    return;
  }
  local_zero(view, sizeof(*view));
}

static void state_view_reset(struct update_state_view *view) {
  if (!view) {
    return;
  }
  local_zero(view, sizeof(*view));
  update_agent_local_copy(view->staged_manifest_path, sizeof(view->staged_manifest_path),
             UPDATE_AGENT_DEFAULT_STAGED_MANIFEST_PATH);
}

void update_agent_reset(void) {
  local_zero(&update_agent_g_status, sizeof(update_agent_g_status));
  g_update_reader = NULL;
  g_update_writer = NULL;
  g_update_remover = NULL;
  g_update_ready = 0;
}

void update_agent_init(const char *current_version) {
  if (g_update_ready) {
    if (current_version && current_version[0]) {
      update_agent_local_copy(update_agent_g_status.current_version,
                 sizeof(update_agent_g_status.current_version), current_version);
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

void update_agent_set_remover(update_agent_remove_file_fn remover) {
  g_update_remover = remover;
}

static void parse_repo_line(const char *key, const char *value) {
  if (local_equal(key, "channel")) {
    update_agent_local_copy(update_agent_g_status.channel, sizeof(update_agent_g_status.channel), value);
    update_agent_local_copy(update_agent_g_status.branch, sizeof(update_agent_g_status.branch),
               branch_for_channel(value));
  } else if (local_equal(key, "branch")) {
    update_agent_local_copy(update_agent_g_status.branch, sizeof(update_agent_g_status.branch), value);
  } else if (local_equal(key, "source")) {
    update_agent_local_copy(update_agent_g_status.source, sizeof(update_agent_g_status.source), value);
  } else if (local_equal(key, "manifest")) {
    update_agent_local_copy(update_agent_g_status.manifest_path, sizeof(update_agent_g_status.manifest_path),
               value);
  } else if (local_equal(key, "remote_manifest")) {
    update_agent_local_copy(update_agent_g_status.remote_manifest_url,
               sizeof(update_agent_g_status.remote_manifest_url), value);
  }
}

static void parse_manifest_line(const char *key, const char *value,
                                struct update_manifest_view *view) {
  if (!view) {
    return;
  }
  if (local_equal(key, "available_version")) {
    update_agent_local_copy(view->version, sizeof(view->version), value);
  } else if (local_equal(key, "channel")) {
    update_agent_local_copy(view->channel, sizeof(view->channel), value);
  } else if (local_equal(key, "branch")) {
    update_agent_local_copy(view->branch, sizeof(view->branch), value);
  } else if (local_equal(key, "source")) {
    update_agent_local_copy(view->source, sizeof(view->source), value);
  } else if (local_equal(key, "published_at")) {
    update_agent_local_copy(view->published_at, sizeof(view->published_at), value);
  } else if (local_equal(key, "payload_sha256")) {
    update_agent_local_copy(view->payload_sha256, sizeof(view->payload_sha256), value);
  }
}

static void parse_state_line(const char *key, const char *value,
                             struct update_state_view *view) {
  if (!view) {
    return;
  }
  if (local_equal(key, "pending_activation")) {
    view->pending_activation = parse_bool_value(value) ? 1u : 0u;
  } else if (local_equal(key, "staged_manifest")) {
    update_agent_local_copy(view->staged_manifest_path, sizeof(view->staged_manifest_path),
               value);
  }
}

static void parse_buffer_line(const char *line, size_t len, int parse_mode,
                              void *target) {
  char key[24];
  char value[UPDATE_AGENT_REMOTE_MAX];
  size_t eq = 0u;
  size_t i = 0u;

  if (!line || len == 0u) {
    return;
  }
  while (eq < len && line[eq] != '=') {
    ++eq;
  }
  if (eq == 0u || eq >= len) {
    return;
  }
  if (eq >= sizeof(key)) {
    eq = sizeof(key) - 1u;
  }
  for (i = 0u; i < eq; ++i) {
    key[i] = line[i];
  }
  key[i] = '\0';

  len -= (eq + 1u);
  if (len >= sizeof(value)) {
    len = sizeof(value) - 1u;
  }
  for (i = 0u; i < len; ++i) {
    value[i] = line[eq + 1u + i];
  }
  value[i] = '\0';

  if (parse_mode == 0) {
    parse_repo_line(key, value);
  } else if (parse_mode == 1) {
    parse_manifest_line(key, value, (struct update_manifest_view *)target);
  } else if (parse_mode == 2) {
    parse_state_line(key, value, (struct update_state_view *)target);
  }
}

static void parse_buffer(const char *buffer, size_t len, int parse_mode,
                         void *target);

static void prepare_repository_status(void) {
  char buffer[768];
  char current_version[UPDATE_AGENT_VERSION_MAX];
  size_t read_len = 0u;
  update_agent_read_file_fn reader = active_reader();

  update_agent_init(NULL);
  update_agent_local_copy(current_version, sizeof(current_version),
             update_agent_g_status.current_version[0] ? update_agent_g_status.current_version
                                                : "unknown");
  update_agent_seed_defaults(current_version);

  if (reader(UPDATE_AGENT_REPOSITORY_PATH, buffer, sizeof(buffer), &read_len) == 0 &&
      read_len > 0u) {
    parse_buffer(buffer, read_len, 0, NULL);
  }
  if (!update_agent_g_status.branch[0]) {
    update_agent_local_copy(update_agent_g_status.branch, sizeof(update_agent_g_status.branch),
               branch_for_channel(update_agent_g_status.channel));
  }
  if (!update_agent_g_status.remote_manifest_url[0]) {
    build_remote_manifest_url(update_agent_g_status.source, update_agent_g_status.branch,
                              update_agent_g_status.remote_manifest_url,
                              sizeof(update_agent_g_status.remote_manifest_url));
  }
}

static void parse_buffer(const char *buffer, size_t len, int parse_mode,
                         void *target) {
  size_t start = 0u;
  if (!buffer || len == 0u) {
    return;
  }
  while (start < len) {
    size_t end = start;
    while (end < len && buffer[end] != '\n' && buffer[end] != '\r') {
      ++end;
    }
    if (end > start) {
      parse_buffer_line(&buffer[start], end - start, parse_mode, target);
    }
    start = end;
    while (start < len && (buffer[start] == '\n' || buffer[start] == '\r')) {
      ++start;
    }
  }
}

static int read_manifest_view(const char *path, struct update_manifest_view *view) {
  char buffer[768];
  size_t read_len = 0u;
  int rc = 0;
  update_agent_read_file_fn reader = active_reader();

  if (!path || !view) {
    return -1;
  }
  manifest_view_reset(view);
  rc = reader(path, buffer, sizeof(buffer), &read_len);
  if (rc != 0 || read_len == 0u) {
    return -1;
  }
  parse_buffer(buffer, read_len, 1, view);
  return view->version[0] ? 0 : -2;
}

static int read_state_view(struct update_state_view *view) {
  char buffer[256];
  size_t read_len = 0u;
  update_agent_read_file_fn reader = active_reader();

  if (!view) {
    return -1;
  }
  state_view_reset(view);
  if (reader(UPDATE_AGENT_STATE_PATH, buffer, sizeof(buffer), &read_len) != 0 ||
      read_len == 0u) {
    return 1;
  }
  parse_buffer(buffer, read_len, 2, view);
  return 0;
}

static int write_state_file(int pending_activation, const char *staged_manifest_path) {
  char text[192];
  update_agent_write_file_fn writer = active_writer();

  if (!writer || !staged_manifest_path || !staged_manifest_path[0]) {
    return -1;
  }

  text[0] = '\0';
  local_append(text, sizeof(text), "pending_activation=");
  local_append(text, sizeof(text), pending_activation ? "1" : "0");
  local_append(text, sizeof(text), "\n");
  local_append(text, sizeof(text), "staged_manifest=");
  local_append(text, sizeof(text), staged_manifest_path);
  local_append(text, sizeof(text), "\n");
  return writer(UPDATE_AGENT_STATE_PATH, text);
}

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
  struct update_manifest_view available_manifest;
  struct update_manifest_view staged_manifest;
  struct update_state_view state_view;

  prepare_repository_status();
  manifest_view_reset(&available_manifest);
  manifest_view_reset(&staged_manifest);
  state_view_reset(&state_view);

  update_agent_g_status.catalog_present = 0u;
  update_agent_g_status.update_available = 0u;
  update_agent_g_status.stage_ready = 0u;
  update_agent_g_status.pending_activation = 0u;
  update_agent_g_status.last_result = 0;
  update_agent_g_status.available_version[0] = '\0';
  update_agent_g_status.staged_version[0] = '\0';
  update_agent_g_status.staged_payload_sha256[0] = '\0';
  update_agent_g_status.published_at[0] = '\0';
  update_agent_local_copy(update_agent_g_status.staged_manifest_path,
             sizeof(update_agent_g_status.staged_manifest_path),
             UPDATE_AGENT_DEFAULT_STAGED_MANIFEST_PATH);

  state_rc = read_state_view(&state_view);
  if (state_rc == 0) {
    update_agent_g_status.pending_activation = state_view.pending_activation;
    update_agent_local_copy(update_agent_g_status.staged_manifest_path,
               sizeof(update_agent_g_status.staged_manifest_path),
               state_view.staged_manifest_path);
  }

  manifest_rc =
      read_manifest_view(update_agent_g_status.manifest_path, &available_manifest);
  if (manifest_rc == 0) {
    update_agent_g_status.catalog_present = 1u;
    update_agent_local_copy(update_agent_g_status.available_version,
               sizeof(update_agent_g_status.available_version),
               available_manifest.version);
    update_agent_local_copy(update_agent_g_status.published_at, sizeof(update_agent_g_status.published_at),
               available_manifest.published_at);
    manifest_channel_mismatch =
        available_manifest.channel[0] &&
        !local_equal(available_manifest.channel, update_agent_g_status.channel);
    manifest_branch_mismatch =
        available_manifest.branch[0] &&
        !local_equal(available_manifest.branch, update_agent_g_status.branch);
    manifest_source_mismatch =
        available_manifest.source[0] &&
        !local_equal(available_manifest.source, update_agent_g_status.source);
    if (!local_equal(update_agent_g_status.available_version,
                     update_agent_g_status.current_version)) {
      update_agent_g_status.update_available = 1u;
    }
  }

  staged_rc = read_manifest_view(update_agent_g_status.staged_manifest_path, &staged_manifest);
  if (staged_rc == 0) {
    update_agent_g_status.stage_ready = 1u;
    update_agent_local_copy(update_agent_g_status.staged_version,
               sizeof(update_agent_g_status.staged_version), staged_manifest.version);
    update_agent_local_copy(update_agent_g_status.staged_payload_sha256,
               sizeof(update_agent_g_status.staged_payload_sha256),
               staged_manifest.payload_sha256);
    staged_channel_mismatch =
        staged_manifest.channel[0] &&
        !local_equal(staged_manifest.channel, update_agent_g_status.channel);
    staged_branch_mismatch =
        staged_manifest.branch[0] &&
        !local_equal(staged_manifest.branch, update_agent_g_status.branch);
    staged_source_mismatch =
        staged_manifest.source[0] &&
        !local_equal(staged_manifest.source, update_agent_g_status.source);
  }

  if (manifest_channel_mismatch || manifest_branch_mismatch ||
      manifest_source_mismatch) {
    rc = -13;
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "catalog cache does not match selected update repository");
  } else if (staged_channel_mismatch || staged_branch_mismatch ||
             staged_source_mismatch) {
    rc = -14;
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "staged update does not match selected update repository");
  } else if (manifest_rc == -2) {
    rc = -2;
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "catalog cache invalid");
  } else if (staged_rc == -2) {
    rc = -3;
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "staged update invalid");
  } else if (update_agent_g_status.pending_activation && !update_agent_g_status.stage_ready) {
    rc = -4;
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "activation pending without staged update");
  } else if (update_agent_g_status.pending_activation && update_agent_g_status.stage_ready) {
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "staged update armed for activation");
  } else if (update_agent_g_status.stage_ready) {
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "staged update ready");
  } else if (!update_agent_g_status.catalog_present) {
    update_agent_g_status.last_result = 1;
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "catalog cache missing");
    return 0;
  } else if (update_agent_g_status.update_available) {
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "update available in local catalog");
  } else {
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "system already matches cached catalog");
  }

  update_agent_g_status.last_result = rc;
  return rc;
}

int update_agent_import_manifest_path(const char *path) {
  char buffer[768];
  size_t read_len = 0u;
  int rc = 0;
  update_agent_read_file_fn reader = active_reader();
  update_agent_write_file_fn writer = active_writer();
  struct update_manifest_view import_manifest;

  if (!path || !path[0]) {
    update_agent_init(NULL);
    update_agent_g_status.last_result = -15;
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "manifest path not provided");
    return -15;
  }

  prepare_repository_status();
  manifest_view_reset(&import_manifest);

  if (!writer) {
    update_agent_g_status.last_result = -16;
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "update cache writer unavailable");
    return -16;
  }
  if (reader(path, buffer, sizeof(buffer), &read_len) != 0 || read_len == 0u) {
    update_agent_g_status.last_result = -17;
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "failed to read imported manifest");
    return -17;
  }
  parse_buffer(buffer, read_len, 1, &import_manifest);
  if (!import_manifest.version[0]) {
    update_agent_g_status.last_result = -18;
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "imported manifest invalid");
    return -18;
  }
  if ((import_manifest.channel[0] &&
       !local_equal(import_manifest.channel, update_agent_g_status.channel)) ||
      (import_manifest.branch[0] &&
       !local_equal(import_manifest.branch, update_agent_g_status.branch)) ||
      (import_manifest.source[0] &&
       !local_equal(import_manifest.source, update_agent_g_status.source))) {
    update_agent_g_status.last_result = -19;
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "imported manifest does not match selected update repository");
    klog(KLOG_WARN, "[update] Manifest import rejected: repository mismatch.");
    return -19;
  }
  if (writer(update_agent_g_status.manifest_path, buffer) != 0) {
    update_agent_g_status.last_result = -21;
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "failed to persist imported manifest");
    klog(KLOG_WARN, "[update] Failed to persist imported manifest.");
    return -21;
  }

  rc = update_agent_poll();
  if (rc < 0) {
    return rc;
  }
  update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
             "manifest imported into local catalog");
  update_agent_g_status.last_result = 0;
  klog(KLOG_INFO, "[update] Manifest imported into local catalog.");
  return 0;
}

int update_agent_stage_latest(void) {
  char buffer[768];
  size_t read_len = 0u;
  update_agent_read_file_fn reader = active_reader();
  update_agent_write_file_fn writer = active_writer();
  int rc = update_agent_poll();

  if (rc < 0) {
    return rc;
  }
  if (!update_agent_g_status.catalog_present || !update_agent_g_status.update_available) {
    update_agent_g_status.last_result = -5;
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "no cached update available to stage");
    return -5;
  }
  if (!writer) {
    update_agent_g_status.last_result = -6;
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "update staging writer unavailable");
    return -6;
  }
  if (reader(update_agent_g_status.manifest_path, buffer, sizeof(buffer), &read_len) != 0 ||
      read_len == 0u) {
    update_agent_g_status.last_result = -7;
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "failed to read cached manifest for staging");
    return -7;
  }
  if (writer(update_agent_g_status.staged_manifest_path, buffer) != 0 ||
      write_state_file(0, update_agent_g_status.staged_manifest_path) != 0) {
    update_agent_g_status.last_result = -9;
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "failed to persist staged update");
    klog(KLOG_WARN, "[update] Failed to persist staged update.");
    return -9;
  }
  klog(KLOG_INFO, "[update] Update staged.");
  return update_agent_poll();
}

int update_agent_clear_stage(void) {
  update_agent_remove_file_fn remover = active_remover();

  update_agent_init(NULL);
  if (remover) {
    (void)remover(update_agent_g_status.staged_manifest_path[0]
                      ? update_agent_g_status.staged_manifest_path
                      : UPDATE_AGENT_DEFAULT_STAGED_MANIFEST_PATH);
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
      update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
                 "no staged update available to arm");
      return -10;
    }
    if (write_state_file(1, update_agent_g_status.staged_manifest_path) != 0) {
      update_agent_g_status.last_result = -11;
      update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
                 "failed to arm staged update");
      klog(KLOG_WARN, "[update] Failed to arm staged update.");
      return -11;
    }
    klog(KLOG_INFO, "[update] Update armed for activation.");
  } else if (update_agent_g_status.stage_ready) {
    if (write_state_file(0, update_agent_g_status.staged_manifest_path) != 0) {
      update_agent_g_status.last_result = -12;
      update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
                 "failed to disarm staged update");
      klog(KLOG_WARN, "[update] Failed to disarm staged update.");
      return -12;
    }
    klog(KLOG_INFO, "[update] Update activation disarmed.");
  } else {
    update_agent_remove_file_fn remover = active_remover();
    if (remover) {
      (void)remover(UPDATE_AGENT_STATE_PATH);
    }
  }
  return update_agent_poll();
}

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
