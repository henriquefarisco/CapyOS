#include "core/update_agent.h"

#if !defined(UNIT_TEST)
#include "fs/vfs.h"
#endif

#include <stddef.h>

#define UPDATE_AGENT_REPOSITORY_PATH "/system/update/repository.ini"
#define UPDATE_AGENT_DEFAULT_MANIFEST_PATH "/system/update/cache/latest.ini"
#define UPDATE_AGENT_DEFAULT_CHANNEL "stable"
#define UPDATE_AGENT_DEFAULT_SOURCE "github:henriquefarisco/CapyOS"

static struct system_update_status g_update_status;
static update_agent_read_file_fn g_update_reader = NULL;
static int g_update_ready = 0;

static void local_zero(void *ptr, size_t len) {
  uint8_t *dst = (uint8_t *)ptr;
  while (len--) {
    *dst++ = 0;
  }
}

static void local_copy(char *dst, size_t dst_size, const char *src) {
  size_t i = 0;
  if (!dst || dst_size == 0) {
    return;
  }
  if (src) {
    while (src[i] && i + 1 < dst_size) {
      dst[i] = src[i];
      ++i;
    }
  }
  dst[i] = '\0';
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

static update_agent_read_file_fn active_reader(void) {
  return g_update_reader ? g_update_reader : local_read_file;
}

static void update_agent_seed_defaults(const char *current_version) {
  local_zero(&g_update_status, sizeof(g_update_status));
  g_update_status.configured = 1u;
  g_update_status.catalog_present = 0u;
  g_update_status.update_available = 0u;
  g_update_status.last_result = 1;
  local_copy(g_update_status.channel, sizeof(g_update_status.channel),
             UPDATE_AGENT_DEFAULT_CHANNEL);
  local_copy(g_update_status.source, sizeof(g_update_status.source),
             UPDATE_AGENT_DEFAULT_SOURCE);
  local_copy(g_update_status.manifest_path, sizeof(g_update_status.manifest_path),
             UPDATE_AGENT_DEFAULT_MANIFEST_PATH);
  local_copy(g_update_status.current_version,
             sizeof(g_update_status.current_version),
             current_version ? current_version : "unknown");
  local_copy(g_update_status.summary, sizeof(g_update_status.summary),
             "catalog cache not checked");
}

void update_agent_reset(void) {
  local_zero(&g_update_status, sizeof(g_update_status));
  g_update_reader = NULL;
  g_update_ready = 0;
}

void update_agent_init(const char *current_version) {
  if (g_update_ready) {
    if (current_version && current_version[0]) {
      local_copy(g_update_status.current_version,
                 sizeof(g_update_status.current_version), current_version);
    }
    return;
  }
  update_agent_seed_defaults(current_version);
  g_update_ready = 1;
}

void update_agent_set_reader(update_agent_read_file_fn reader) {
  g_update_reader = reader;
}

static void parse_update_line(const char *line, size_t len, int is_manifest) {
  char key[24];
  char value[UPDATE_AGENT_SOURCE_MAX];
  size_t eq = 0;
  size_t i = 0;

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
  for (i = 0; i < eq; ++i) {
    key[i] = line[i];
  }
  key[i] = '\0';

  len -= (eq + 1u);
  if (len >= sizeof(value)) {
    len = sizeof(value) - 1u;
  }
  for (i = 0; i < len; ++i) {
    value[i] = line[eq + 1u + i];
  }
  value[i] = '\0';

  if (!is_manifest) {
    if (local_equal(key, "channel")) {
      local_copy(g_update_status.channel, sizeof(g_update_status.channel),
                 value);
    } else if (local_equal(key, "source")) {
      local_copy(g_update_status.source, sizeof(g_update_status.source), value);
    } else if (local_equal(key, "manifest")) {
      local_copy(g_update_status.manifest_path,
                 sizeof(g_update_status.manifest_path), value);
    }
    return;
  }

  if (local_equal(key, "available_version")) {
    local_copy(g_update_status.available_version,
               sizeof(g_update_status.available_version), value);
  } else if (local_equal(key, "channel")) {
    local_copy(g_update_status.channel, sizeof(g_update_status.channel), value);
  } else if (local_equal(key, "published_at")) {
    local_copy(g_update_status.published_at, sizeof(g_update_status.published_at),
               value);
  }
}

static void parse_update_buffer(const char *buffer, size_t len, int is_manifest) {
  size_t start = 0;
  if (!buffer || len == 0u) {
    return;
  }
  while (start < len) {
    size_t end = start;
    while (end < len && buffer[end] != '\n' && buffer[end] != '\r') {
      ++end;
    }
    if (end > start) {
      parse_update_line(&buffer[start], end - start, is_manifest);
    }
    start = end;
    while (start < len && (buffer[start] == '\n' || buffer[start] == '\r')) {
      ++start;
    }
  }
}

int update_agent_poll(void) {
  char buffer[768];
  size_t read_len = 0;
  int repo_rc = 0;
  int manifest_rc = 0;
  update_agent_read_file_fn reader = NULL;

  update_agent_init(NULL);
  reader = active_reader();
  g_update_status.catalog_present = 0u;
  g_update_status.update_available = 0u;
  g_update_status.last_result = 0;
  g_update_status.available_version[0] = '\0';
  g_update_status.published_at[0] = '\0';

  repo_rc = reader(UPDATE_AGENT_REPOSITORY_PATH, buffer, sizeof(buffer),
                   &read_len);
  if (repo_rc == 0 && read_len > 0u) {
    parse_update_buffer(buffer, read_len, 0);
  }

  manifest_rc = reader(g_update_status.manifest_path, buffer, sizeof(buffer),
                       &read_len);
  if (manifest_rc != 0 || read_len == 0u) {
    g_update_status.last_result = 1;
    local_copy(g_update_status.summary, sizeof(g_update_status.summary),
               "catalog cache missing");
    return 0;
  }

  parse_update_buffer(buffer, read_len, 1);
  if (!g_update_status.available_version[0]) {
    g_update_status.last_result = -2;
    local_copy(g_update_status.summary, sizeof(g_update_status.summary),
               "catalog cache invalid");
    return -2;
  }

  g_update_status.catalog_present = 1u;
  if (!local_equal(g_update_status.available_version,
                   g_update_status.current_version)) {
    g_update_status.update_available = 1u;
    local_copy(g_update_status.summary, sizeof(g_update_status.summary),
               "update available in local catalog");
  } else {
    local_copy(g_update_status.summary, sizeof(g_update_status.summary),
               "system already matches cached catalog");
  }
  g_update_status.last_result = 0;
  return 0;
}

void update_agent_status_get(struct system_update_status *out) {
  update_agent_init(NULL);
  if (!out) {
    return;
  }
  *out = g_update_status;
}
