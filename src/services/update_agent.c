#include "services/update_agent.h"
#include "boot/boot_slot.h"
#include "kernel/log/klog.h"
#include "security/ed25519.h"
#include "security/sha256.h"

#if !defined(UNIT_TEST)
#include "fs/vfs.h"
#include "memory/kmem.h"
#include "net/http.h"
#endif

#include <stddef.h>

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

struct system_update_status update_agent_g_status;
static update_agent_read_file_fn g_update_reader = NULL;
static update_agent_write_file_fn g_update_writer = NULL;
static update_agent_write_bytes_fn g_update_bytes_writer = NULL;
static update_agent_remove_file_fn g_update_remover = NULL;
#if defined(UNIT_TEST)
static update_agent_manifest_verify_fn g_update_manifest_verifier = NULL;
static update_agent_fetch_manifest_fn g_update_manifest_fetcher = NULL;
static update_agent_fetch_payload_fn g_update_payload_fetcher = NULL;
#endif
static int g_update_ready = 0;

static const uint8_t update_agent_release_public_key[ED25519_PUBLIC_KEY_SIZE] = {
    0x63, 0x61, 0x70, 0x79, 0x6f, 0x73, 0x2d, 0x72,
    0x65, 0x6c, 0x65, 0x61, 0x73, 0x65, 0x2d, 0x61,
    0x6c, 0x70, 0x68, 0x61, 0x2d, 0x32, 0x30, 0x32,
    0x36, 0x2d, 0x65, 0x64, 0x32, 0x35, 0x35, 0x31};

static void local_zero(void *ptr, size_t len) {
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

struct update_version_key {
  uint32_t major;
  uint32_t minor;
  uint32_t patch;
  uint32_t prerelease_number;
  int prerelease_rank;
};

static int local_is_digit(char c) {
  return c >= '0' && c <= '9';
}

static int local_is_hex_digit(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

static int local_hex_value(char c) {
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

static int local_hex_string_valid(const char *text, size_t hex_len) {
  size_t i = 0u;
  if (!text || !text[0]) {
    return 0;
  }
  while (i < hex_len) {
    if (!local_is_hex_digit(text[i])) {
      return 0;
    }
    ++i;
  }
  return text[i] == '\0';
}

static int local_hex_to_bytes(const char *hex, uint8_t *out, size_t out_len) {
  size_t i = 0u;
  if (!hex || !out) {
    return -1;
  }
  while (i < out_len) {
    int hi = local_hex_value(hex[i * 2u]);
    int lo = local_hex_value(hex[i * 2u + 1u]);
    if (hi < 0 || lo < 0) {
      return -1;
    }
    out[i] = (uint8_t)(((uint8_t)hi << 4) | (uint8_t)lo);
    ++i;
  }
  return hex[out_len * 2u] == '\0' ? 0 : -1;
}

static int local_hex_equal_fixed(const char *a, const char *b, size_t hex_len) {
  size_t i = 0u;
  if (!a || !b) {
    return 0;
  }
  while (i < hex_len) {
    if (local_hex_value(a[i]) != local_hex_value(b[i])) {
      return 0;
    }
    ++i;
  }
  return a[i] == '\0' && b[i] == '\0';
}

static int read_version_number(const char **cursor, uint32_t *out) {
  const char *p = cursor ? *cursor : NULL;
  uint32_t value = 0u;
  if (!p || !out || !local_is_digit(*p)) {
    return -1;
  }
  while (local_is_digit(*p)) {
    value = value * 10u + (uint32_t)(*p - '0');
    ++p;
  }
  *cursor = p;
  *out = value;
  return 0;
}

static int prerelease_rank(const char *start, size_t len) {
  if (len == 5u && start[0] == 'a' && start[1] == 'l' && start[2] == 'p' &&
      start[3] == 'h' && start[4] == 'a') {
    return 1;
  }
  if (len == 4u && start[0] == 'b' && start[1] == 'e' && start[2] == 't' &&
      start[3] == 'a') {
    return 2;
  }
  if (len == 2u && start[0] == 'r' && start[1] == 'c') {
    return 3;
  }
  return 0;
}

static int parse_update_version_key(const char *version,
                                    struct update_version_key *out) {
  const char *p = version;
  if (!version || !out) {
    return -1;
  }
  if (*p == 'v' || *p == 'V') {
    ++p;
  }
  out->major = 0u;
  out->minor = 0u;
  out->patch = 0u;
  out->prerelease_number = 0u;
  out->prerelease_rank = 4;
  if (read_version_number(&p, &out->major) != 0 || *p++ != '.' ||
      read_version_number(&p, &out->minor) != 0 || *p++ != '.' ||
      read_version_number(&p, &out->patch) != 0) {
    return -1;
  }
  if (*p == '-') {
    const char *label = ++p;
    size_t label_len = 0u;
    while (p[label_len] && p[label_len] != '.' && p[label_len] != '+') {
      ++label_len;
    }
    out->prerelease_rank = prerelease_rank(label, label_len);
    if (out->prerelease_rank == 0) {
      return -1;
    }
    p += label_len;
    if (*p == '.') {
      ++p;
      if (read_version_number(&p, &out->prerelease_number) != 0) {
        return -1;
      }
    }
  }
  if (*p == '+') {
    return 0;
  }
  return *p == '\0' ? 0 : -1;
}

static int compare_u32(uint32_t a, uint32_t b) {
  if (a < b) {
    return -1;
  }
  if (a > b) {
    return 1;
  }
  return 0;
}

static int compare_update_versions(const char *candidate, const char *current,
                                   int *out_cmp) {
  struct update_version_key a;
  struct update_version_key b;
  int cmp = 0;
  if (!out_cmp || parse_update_version_key(candidate, &a) != 0) {
    return -1;
  }
  if (parse_update_version_key(current, &b) != 0) {
    *out_cmp = local_equal(candidate, current) ? 0 : 1;
    return 0;
  }
  cmp = compare_u32(a.major, b.major);
  if (cmp == 0) cmp = compare_u32(a.minor, b.minor);
  if (cmp == 0) cmp = compare_u32(a.patch, b.patch);
  if (cmp == 0) cmp = compare_u32((uint32_t)a.prerelease_rank,
                                  (uint32_t)b.prerelease_rank);
  if (cmp == 0) cmp = compare_u32(a.prerelease_number, b.prerelease_number);
  *out_cmp = cmp;
  return 0;
}

static int manifest_payload_sha256_valid(const struct update_manifest_view *view) {
  return view &&
         local_hex_string_valid(view->payload_sha256,
                                UPDATE_AGENT_SHA256_HEX_LEN);
}

static int manifest_payload_url_valid(const struct update_manifest_view *view) {
  const char *url = view ? view->payload_url : NULL;
  size_t i = 0u;

  if (!url || !url[0]) {
    return 0;
  }
  if (local_starts_with(url, "https://")) {
    if (local_equal(url, "https://")) {
      return 0;
    }
  } else if (local_starts_with(url, "/system/update/")) {
    if (local_equal(url, "/system/update/")) {
      return 0;
    }
  } else {
    return 0;
  }
  while (url[i]) {
    if (url[i] == ' ' || url[i] == '\t' || url[i] == '\r' ||
        url[i] == '\n') {
      return 0;
    }
    if (url[i] == '.' && url[i + 1u] == '.') {
      return 0;
    }
    ++i;
  }
  return i > 0u;
}

static int manifest_signature_ed25519_valid(
    const struct update_manifest_view *view) {
  uint8_t signature[ED25519_SIGNATURE_SIZE];
  if (!view || view->signature_line_count != 1u || view->signed_len == 0u ||
      !local_hex_string_valid(view->signature_ed25519,
                              UPDATE_AGENT_ED25519_SIGNATURE_HEX_LEN)) {
    return 0;
  }
  if (local_hex_to_bytes(view->signature_ed25519, signature,
                         sizeof(signature)) != 0) {
    return 0;
  }
#if defined(UNIT_TEST)
  if (g_update_manifest_verifier) {
    return g_update_manifest_verifier(view->signed_text, view->signed_len,
                                      view->signature_ed25519)
               ? 1
               : 0;
  }
#endif
  /*
   * Production gate. Em alpha.217 `ed25519_verify` virou a
   * implementacao real RFC 8032 (src/security/ed25519.c) substituindo
   * o esqueleto fail-closed que vinha de alpha.210. Manifests
   * assinados com a chave canonica `update_agent_release_public_key`
   * sao agora aceitos quando a assinatura e criptograficamente valida.
   * Manifests com assinatura forjada / corrompida / com S >= L /
   * ponto R invalido continuam sendo rejeitados fail-closed.
   *
   * Tests UNIT_TEST continuam bypassando via
   * `g_update_manifest_verifier` para fixture-based testing sem
   * precisar gerar assinaturas reais.
   */
  return ed25519_verify(signature, (const uint8_t *)view->signed_text,
                        view->signed_len,
                        update_agent_release_public_key) == 0;
}

static int manifest_compare_current(const struct update_manifest_view *view,
                                    int *out_cmp) {
  if (!view || !view->version[0]) {
    return -1;
  }
  return compare_update_versions(view->version,
                                 update_agent_g_status.current_version,
                                 out_cmp);
}

static const char *branch_for_channel(const char *channel) {
  return local_equal(channel, "develop") ? "develop" : UPDATE_AGENT_DEFAULT_BRANCH;
}

static int build_stable_tag_ref(const char *version, char *dst,
                                size_t dst_size) {
  const char *p = version;
  size_t dots = 0u;
  size_t written = 0u;

  if (!dst || dst_size == 0u) {
    return -1;
  }
  dst[0] = '\0';
  if (!p || !p[0]) {
    return -1;
  }
  if (*p == 'v' || *p == 'V') {
    ++p;
  }
  local_append(dst, dst_size, "refs/tags/v");
  while (dst[written] && written + 1u < dst_size) {
    ++written;
  }
  while (*p && *p != '-' && *p != '+' && written + 1u < dst_size) {
    char next[2];
    if (!(local_is_digit(*p) || *p == '.')) {
      break;
    }
    if (*p == '.') {
      ++dots;
    }
    next[0] = *p++;
    next[1] = '\0';
    local_append(dst, dst_size, next);
    while (dst[written] && written + 1u < dst_size) {
      ++written;
    }
  }
  return dots == 2u ? 0 : -1;
}

static void build_remote_manifest_url(const char *source, const char *channel,
                                      const char *branch,
                                      const char *current_version,
                                      char *dst, size_t dst_size) {
  char remote_ref[32];

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
  if (local_equal(channel, "develop")) {
    local_append(dst, dst_size, "refs/heads/");
    local_append(dst, dst_size,
                 (branch && branch[0]) ? branch : branch_for_channel(channel));
  } else {
    if (build_stable_tag_ref(current_version, remote_ref, sizeof(remote_ref)) !=
        0) {
      update_agent_local_copy(remote_ref, sizeof(remote_ref), "refs/heads/main");
    }
    local_append(dst, dst_size, remote_ref);
  }
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

static update_agent_write_bytes_fn active_bytes_writer(void) {
#if defined(UNIT_TEST)
  return g_update_bytes_writer;
#else
  return g_update_bytes_writer ? g_update_bytes_writer : local_write_bytes;
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
  update_agent_local_copy(update_agent_g_status.payload_cache_path,
             sizeof(update_agent_g_status.payload_cache_path),
             UPDATE_AGENT_PAYLOAD_CACHE_PATH);
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
  update_agent_local_copy(view->payload_cache_path,
             sizeof(view->payload_cache_path),
             UPDATE_AGENT_PAYLOAD_CACHE_PATH);
}

void update_agent_reset(void) {
  local_zero(&update_agent_g_status, sizeof(update_agent_g_status));
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
  } else if (local_equal(key, "payload_url")) {
    update_agent_local_copy(view->payload_url, sizeof(view->payload_url), value);
  } else if (local_equal(key, "payload_sha256")) {
    update_agent_local_copy(view->payload_sha256, sizeof(view->payload_sha256), value);
  } else if (local_equal(key, "signature_ed25519")) {
    update_agent_local_copy(view->signature_ed25519,
                            sizeof(view->signature_ed25519), value);
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
  } else if (local_equal(key, "payload_cache")) {
    update_agent_local_copy(view->payload_cache_path,
               sizeof(view->payload_cache_path), value);
  } else if (local_equal(key, "payload_cache_sha256") &&
             local_hex_string_valid(value, UPDATE_AGENT_SHA256_HEX_LEN)) {
    update_agent_local_copy(view->payload_cache_sha256,
               sizeof(view->payload_cache_sha256), value);
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

static int manifest_line_matches_key(const char *line, size_t len,
                                     const char *key) {
  size_t i = 0u;
  if (!line || !key) {
    return 0;
  }
  while (key[i]) {
    if (i >= len || line[i] != key[i]) {
      return 0;
    }
    ++i;
  }
  return i < len && line[i] == '=';
}

static int manifest_capture_signed_text(const char *buffer, size_t len,
                                        struct update_manifest_view *view) {
  size_t start = 0u;
  if (!buffer || !view) {
    return -1;
  }
  view->signed_len = 0u;
  view->signed_text[0] = '\0';
  view->signature_line_count = 0u;
  while (start < len) {
    size_t end = start;
    size_t line_end = start;
    size_t copy_len = 0u;
    while (end < len && buffer[end] != '\n' && buffer[end] != '\r') {
      ++end;
    }
    line_end = end;
    while (end < len && (buffer[end] == '\n' || buffer[end] == '\r')) {
      ++end;
    }
    if (line_end > start &&
        manifest_line_matches_key(&buffer[start], line_end - start,
                                  "signature_ed25519")) {
      if (view->signature_line_count != 255u) {
        ++view->signature_line_count;
      }
    } else {
      copy_len = end - start;
      if (view->signed_len + copy_len >= sizeof(view->signed_text)) {
        return -1;
      }
      while (copy_len) {
        view->signed_text[view->signed_len++] = buffer[start++];
        --copy_len;
      }
      start = end;
      continue;
    }
    start = end;
  }
  view->signed_text[view->signed_len] = '\0';
  return 0;
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
    build_remote_manifest_url(update_agent_g_status.source,
                              update_agent_g_status.channel,
                              update_agent_g_status.branch,
                              update_agent_g_status.current_version,
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
  if (manifest_capture_signed_text(buffer, read_len, view) != 0) {
    return -2;
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

static int fetch_remote_manifest_text(const char *url, char *buffer,
                                      size_t buffer_size, size_t *out_len) {
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
  local_zero(&response, sizeof(response));
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

static int fetch_payload_bytes(const char *url, uint8_t *buffer,
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
  if (local_starts_with(url, "/system/update/")) {
    return read_local_payload_bytes(url, buffer, buffer_size, out_len);
  }
  local_zero(&response, sizeof(response));
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

static int write_state_file(int pending_activation, const char *staged_manifest_path) {
  char text[320];
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
  if (update_agent_g_status.payload_cache_path[0] &&
      update_agent_g_status.payload_cache_sha256[0]) {
    local_append(text, sizeof(text), "payload_cache=");
    local_append(text, sizeof(text), update_agent_g_status.payload_cache_path);
    local_append(text, sizeof(text), "\n");
    local_append(text, sizeof(text), "payload_cache_sha256=");
    local_append(text, sizeof(text), update_agent_g_status.payload_cache_sha256);
    local_append(text, sizeof(text), "\n");
  }
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

  state_rc = read_state_view(&state_view);
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

  manifest_rc =
      read_manifest_view(update_agent_g_status.manifest_path, &available_manifest);
  if (manifest_rc == 0) {
    update_agent_g_status.catalog_present = 1u;
    update_agent_local_copy(update_agent_g_status.available_version,
               sizeof(update_agent_g_status.available_version),
               available_manifest.version);
    update_agent_local_copy(update_agent_g_status.published_at, sizeof(update_agent_g_status.published_at),
               available_manifest.published_at);
    update_agent_local_copy(update_agent_g_status.payload_url,
               sizeof(update_agent_g_status.payload_url),
               available_manifest.payload_url);
    manifest_channel_mismatch =
        available_manifest.channel[0] &&
        !local_equal(available_manifest.channel, update_agent_g_status.channel);
    manifest_branch_mismatch =
        available_manifest.branch[0] &&
        !local_equal(available_manifest.branch, update_agent_g_status.branch);
    manifest_source_mismatch =
        available_manifest.source[0] &&
        !local_equal(available_manifest.source, update_agent_g_status.source);
    if (manifest_compare_current(&available_manifest, &manifest_version_cmp) != 0) {
      manifest_version_invalid = 1;
    } else if (manifest_version_cmp < 0) {
      manifest_downgrade = 1;
    } else if (manifest_version_cmp > 0 &&
               !manifest_payload_sha256_valid(&available_manifest)) {
      manifest_payload_invalid = 1;
    } else if (manifest_version_cmp > 0 &&
               !manifest_payload_url_valid(&available_manifest)) {
      manifest_payload_url_invalid = 1;
    } else if (manifest_version_cmp > 0 &&
               !manifest_signature_ed25519_valid(&available_manifest)) {
      manifest_signature_invalid = 1;
    } else if (manifest_version_cmp > 0) {
      update_agent_g_status.update_available = 1u;
    }
  }

  staged_rc = read_manifest_view(update_agent_g_status.staged_manifest_path, &staged_manifest);
  if (staged_rc == 0) {
    update_agent_local_copy(update_agent_g_status.staged_version,
               sizeof(update_agent_g_status.staged_version), staged_manifest.version);
    update_agent_local_copy(update_agent_g_status.staged_payload_sha256,
               sizeof(update_agent_g_status.staged_payload_sha256),
               staged_manifest.payload_sha256);
    update_agent_local_copy(update_agent_g_status.staged_payload_url,
               sizeof(update_agent_g_status.staged_payload_url),
               staged_manifest.payload_url);
    staged_channel_mismatch =
        staged_manifest.channel[0] &&
        !local_equal(staged_manifest.channel, update_agent_g_status.channel);
    staged_branch_mismatch =
        staged_manifest.branch[0] &&
        !local_equal(staged_manifest.branch, update_agent_g_status.branch);
    staged_source_mismatch =
        staged_manifest.source[0] &&
        !local_equal(staged_manifest.source, update_agent_g_status.source);
    if (manifest_compare_current(&staged_manifest, &staged_version_cmp) != 0) {
      staged_version_invalid = 1;
    } else if (staged_version_cmp < 0) {
      staged_downgrade = 1;
    } else if (!manifest_payload_sha256_valid(&staged_manifest)) {
      staged_payload_invalid = 1;
    } else if (!manifest_payload_url_valid(&staged_manifest)) {
      staged_payload_url_invalid = 1;
    } else if (!manifest_signature_ed25519_valid(&staged_manifest)) {
      staged_signature_invalid = 1;
    } else {
      update_agent_g_status.stage_ready = 1u;
    }
  }

  if (update_agent_g_status.payload_cache_sha256[0]) {
    int cache_matches_available =
        manifest_rc == 0 && manifest_payload_sha256_valid(&available_manifest) &&
        local_hex_equal_fixed(update_agent_g_status.payload_cache_sha256,
                              available_manifest.payload_sha256,
                              UPDATE_AGENT_SHA256_HEX_LEN);
    int cache_matches_staged =
        staged_rc == 0 && manifest_payload_sha256_valid(&staged_manifest) &&
        local_hex_equal_fixed(update_agent_g_status.payload_cache_sha256,
                              staged_manifest.payload_sha256,
                              UPDATE_AGENT_SHA256_HEX_LEN);
    if (!cache_matches_available && !cache_matches_staged) {
      update_agent_g_status.payload_cache_sha256[0] = '\0';
    }
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
  } else if (manifest_version_invalid) {
    rc = -22;
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "catalog cache version invalid");
  } else if (staged_version_invalid) {
    rc = -23;
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "staged update version invalid");
  } else if (manifest_downgrade) {
    rc = -24;
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "catalog cache version is older than current system");
  } else if (staged_downgrade) {
    rc = -25;
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "staged update version is older than current system");
  } else if (manifest_payload_invalid) {
    rc = -26;
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "catalog cache missing or malformed payload sha256");
  } else if (staged_payload_invalid) {
    rc = -27;
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "staged update missing or malformed payload sha256");
  } else if (manifest_payload_url_invalid) {
    rc = -37;
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "catalog cache missing or malformed payload url");
  } else if (staged_payload_url_invalid) {
    rc = -38;
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "staged update missing or malformed payload url");
  } else if (manifest_signature_invalid) {
    rc = -28;
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "catalog cache missing or invalid ed25519 signature");
  } else if (staged_signature_invalid) {
    rc = -29;
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "staged update missing or invalid ed25519 signature");
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
  int import_version_cmp = 0;
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
  if (manifest_capture_signed_text(buffer, read_len, &import_manifest) != 0) {
    update_agent_g_status.last_result = -18;
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "imported manifest invalid");
    return -18;
  }
  parse_buffer(buffer, read_len, 1, &import_manifest);
  if (!import_manifest.version[0]) {
    update_agent_g_status.last_result = -18;
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "imported manifest invalid");
    return -18;
  }
  if (manifest_compare_current(&import_manifest, &import_version_cmp) != 0) {
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
  if (import_version_cmp <= 0) {
    update_agent_g_status.last_result = -20;
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "imported manifest not newer than current system");
    klog(KLOG_WARN, "[update] Manifest import rejected: version is not newer.");
    return -20;
  }
  if (!manifest_payload_sha256_valid(&import_manifest)) {
    update_agent_g_status.last_result = -22;
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "imported manifest missing or malformed payload sha256");
    klog(KLOG_WARN, "[update] Manifest import rejected: payload sha256 invalid.");
    return -22;
  }
  if (!manifest_payload_url_valid(&import_manifest)) {
    update_agent_g_status.last_result = -39;
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "imported manifest missing or malformed payload url");
    klog(KLOG_WARN, "[update] Manifest import rejected: payload url invalid.");
    return -39;
  }
  if (!manifest_signature_ed25519_valid(&import_manifest)) {
    update_agent_g_status.last_result = -23;
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "imported manifest missing or invalid ed25519 signature");
    klog(KLOG_WARN, "[audit] [update] manifest ed25519 signature invalid -> refused");
    return -23;
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

int update_agent_fetch_remote_manifest(void) {
  char buffer[UPDATE_AGENT_MANIFEST_TEXT_MAX];
  size_t fetch_len = 0u;
  int rc = 0;
  update_agent_write_file_fn writer = NULL;
  update_agent_remove_file_fn remover = NULL;

  prepare_repository_status();
  writer = active_writer();
  remover = active_remover();

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

  rc = fetch_remote_manifest_text(update_agent_g_status.remote_manifest_url,
                                  buffer, sizeof(buffer), &fetch_len);
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
  writer = active_bytes_writer();
  if (!writer) {
    update_agent_g_status.last_result = -41;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "payload cache writer unavailable");
    return -41;
  }

  manifest_view_reset(&manifest);
  if (read_manifest_view(update_agent_g_status.manifest_path, &manifest) != 0 ||
      !manifest_payload_sha256_valid(&manifest) ||
      !manifest_payload_url_valid(&manifest) ||
      !manifest_signature_ed25519_valid(&manifest) ||
      !local_equal(manifest.payload_url, update_agent_g_status.payload_url)) {
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

  rc = fetch_payload_bytes(manifest.payload_url, payload_buffer,
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
  if (!local_hex_equal_fixed(digest_hex, manifest.payload_sha256,
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
  local_zero(explain, sizeof(*explain));
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
  manifest_view_reset(&manifest);
  if (read_manifest_view(update_agent_g_status.manifest_path, &manifest) != 0 ||
      !manifest_payload_sha256_valid(&manifest) ||
      !manifest_payload_url_valid(&manifest) ||
      !manifest_signature_ed25519_valid(&manifest) ||
      !local_equal(manifest.payload_url, update_agent_g_status.payload_url)) {
    update_agent_g_status.last_result = -52;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "prepare dry-run catalog invalid");
    return -52;
  }
  if (!update_agent_g_status.payload_cache_sha256[0] ||
      !local_hex_equal_fixed(update_agent_g_status.payload_cache_sha256,
                             manifest.payload_sha256,
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
  manifest_view_reset(&manifest);
  poll_rc = update_agent_poll();
  out->poll_ready = poll_rc >= 0 ? 1u : 0u;

  manifest_rc = read_manifest_view(update_agent_g_status.manifest_path,
                                   &manifest);
  if (manifest_rc != 0) {
    return prepare_explain_finish(out, -51, "catalog",
                                  "prepare explain: catalog missing");
  }

  out->catalog_ready = 1u;
  repository_ready =
      (!manifest.channel[0] ||
       local_equal(manifest.channel, update_agent_g_status.channel)) &&
      (!manifest.branch[0] ||
       local_equal(manifest.branch, update_agent_g_status.branch)) &&
      (!manifest.source[0] ||
       local_equal(manifest.source, update_agent_g_status.source));
  out->repository_ready = repository_ready ? 1u : 0u;
  out->payload_sha256_ready = manifest_payload_sha256_valid(&manifest) ? 1u : 0u;
  out->payload_url_ready =
      (manifest_payload_url_valid(&manifest) &&
       local_equal(manifest.payload_url, update_agent_g_status.payload_url))
          ? 1u
          : 0u;
  out->signature_ready = manifest_signature_ed25519_valid(&manifest) ? 1u : 0u;

  if (manifest_compare_current(&manifest, &version_cmp) == 0 &&
      version_cmp > 0) {
    out->version_ready = 1u;
  }
  if (out->payload_sha256_ready && update_agent_g_status.payload_cache_sha256[0] &&
      local_hex_equal_fixed(update_agent_g_status.payload_cache_sha256,
                            manifest.payload_sha256,
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
  manifest_view_reset(&manifest);
  if (manifest_capture_signed_text(buffer, read_len, &manifest) != 0) {
    update_agent_g_status.last_result = -7;
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "failed to read cached manifest for staging");
    return -7;
  }
  parse_buffer(buffer, read_len, 1, &manifest);
  if (!update_agent_g_status.payload_cache_sha256[0] ||
      !manifest_payload_sha256_valid(&manifest) ||
      !manifest_signature_ed25519_valid(&manifest) ||
      !local_hex_equal_fixed(update_agent_g_status.payload_cache_sha256,
                             manifest.payload_sha256,
                             UPDATE_AGENT_SHA256_HEX_LEN)) {
    update_agent_g_status.last_result = -49;
    update_agent_local_copy(update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
               "payload cache missing or unverified for staging");
    return -49;
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
