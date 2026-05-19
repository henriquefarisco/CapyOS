/*
 * src/services/update_agent_parse.c
 *
 * Manifest parsing layer for the update_agent state machine: version
 * key comparison, manifest validators (sha256 / payload URL /
 * ed25519 signature / current-version compare), branch and URL
 * builders for the remote catalog, .ini-style line parsers for
 * repository / manifest / state files, the signed-text capture
 * routine used to verify ed25519 signatures, and the buffered
 * manifest / state readers + `prepare_repository_status`.
 *
 * Carved out of `src/services/update_agent.c` at the 2026-05-15
 * refactor so each translation unit stays under the 900-line layout
 * limit. Shares globals, view types and IO accessors with the other
 * `update_agent_*` files through
 * `src/services/internal/update_agent_internal.h`.
 */
#include "services/update_agent.h"
#include "security/ed25519.h"

#include "services/internal/update_agent_internal.h"

#include <stddef.h>
#include <stdint.h>

/* ── version key parsing + comparison ───────────────────────────────── */

struct update_version_key {
  uint32_t major;
  uint32_t minor;
  uint32_t patch;
  uint32_t prerelease_number;
  int prerelease_rank;
};

static int read_version_number(const char **cursor, uint32_t *out) {
  const char *p = cursor ? *cursor : NULL;
  uint32_t value = 0u;
  if (!p || !out || !update_agent_local_is_digit(*p)) {
    return -1;
  }
  while (update_agent_local_is_digit(*p)) {
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
    *out_cmp = update_agent_local_equal(candidate, current) ? 0 : 1;
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

/* ── manifest field validators ──────────────────────────────────────── */

static const uint8_t update_agent_release_public_key[ED25519_PUBLIC_KEY_SIZE] = {
    0x63, 0x61, 0x70, 0x79, 0x6f, 0x73, 0x2d, 0x72,
    0x65, 0x6c, 0x65, 0x61, 0x73, 0x65, 0x2d, 0x61,
    0x6c, 0x70, 0x68, 0x61, 0x2d, 0x32, 0x30, 0x32,
    0x36, 0x2d, 0x65, 0x64, 0x32, 0x35, 0x35, 0x31};

int update_agent_manifest_payload_sha256_valid(
    const struct update_manifest_view *view) {
  return view &&
         update_agent_local_hex_string_valid(view->payload_sha256,
                                             UPDATE_AGENT_SHA256_HEX_LEN);
}

int update_agent_manifest_payload_url_valid(
    const struct update_manifest_view *view) {
  const char *url = view ? view->payload_url : NULL;
  size_t i = 0u;

  if (!url || !url[0]) {
    return 0;
  }
  if (update_agent_local_starts_with(url, "https://")) {
    if (update_agent_local_equal(url, "https://")) {
      return 0;
    }
  } else if (update_agent_local_starts_with(url, "/system/update/")) {
    if (update_agent_local_equal(url, "/system/update/")) {
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

int update_agent_manifest_signature_ed25519_valid(
    const struct update_manifest_view *view) {
  uint8_t signature[ED25519_SIGNATURE_SIZE];
  if (!view || view->signature_line_count != 1u || view->signed_len == 0u ||
      !update_agent_local_hex_string_valid(
          view->signature_ed25519,
          UPDATE_AGENT_ED25519_SIGNATURE_HEX_LEN)) {
    return 0;
  }
  if (update_agent_local_hex_to_bytes(view->signature_ed25519, signature,
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

int update_agent_manifest_compare_current(
    const struct update_manifest_view *view, int *out_cmp) {
  if (!view || !view->version[0]) {
    return -1;
  }
  return compare_update_versions(view->version,
                                 update_agent_g_status.current_version,
                                 out_cmp);
}

/* ── branch + remote-URL builders ───────────────────────────────────── */

static const char *branch_for_channel(const char *channel) {
  return update_agent_local_equal(channel, "develop")
             ? "develop"
             : UPDATE_AGENT_DEFAULT_BRANCH;
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
  update_agent_local_append(dst, dst_size, "refs/tags/v");
  while (dst[written] && written + 1u < dst_size) {
    ++written;
  }
  while (*p && *p != '-' && *p != '+' && written + 1u < dst_size) {
    char next[2];
    if (!(update_agent_local_is_digit(*p) || *p == '.')) {
      break;
    }
    if (*p == '.') {
      ++dots;
    }
    next[0] = *p++;
    next[1] = '\0';
    update_agent_local_append(dst, dst_size, next);
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
  if (!source || !source[0] ||
      !update_agent_local_starts_with(source, UPDATE_AGENT_GITHUB_PREFIX)) {
    return;
  }
  update_agent_local_append(dst, dst_size, "https://raw.githubusercontent.com/");
  update_agent_local_append(dst, dst_size, source + 7u);
  update_agent_local_append(dst, dst_size, "/");
  if (update_agent_local_equal(channel, "develop")) {
    update_agent_local_append(dst, dst_size, "refs/heads/");
    update_agent_local_append(dst, dst_size,
                              (branch && branch[0]) ? branch : branch_for_channel(channel));
  } else {
    if (build_stable_tag_ref(current_version, remote_ref, sizeof(remote_ref)) !=
        0) {
      update_agent_local_copy(remote_ref, sizeof(remote_ref), "refs/heads/main");
    }
    update_agent_local_append(dst, dst_size, remote_ref);
  }
  update_agent_local_append(dst, dst_size, UPDATE_AGENT_REMOTE_MANIFEST_SUFFIX);
}

/* ── .ini line parsers + buffer iterator ────────────────────────────── */

/* Reject any byte outside printable ASCII (0x20-0x7E). Same threat
 * model and rationale as `value_is_printable_ascii` in
 * `src/services/capypkg/capypkg_manifest.c`: parsed update-agent
 * fields end up echoed by `cmd_update_status` through `shell_print`
 * to the framebuffer AND to the serial port (COM1). A terminal
 * emulator on the serial side would interpret ANSI escape bytes
 * smuggled inside `summary`, `version`, `branch`, `source`,
 * `payload_url`, `published_at` etc. before the signature gate ever
 * runs (signature is only validated when the staged update is
 * armed). Refusing at parse time keeps hostile bytes out of both
 * the in-memory status struct and the persisted state.ini /
 * repository.ini / payload_cache_sha256 stores. */
static int update_value_is_printable_ascii(const char *value, size_t value_len) {
  for (size_t i = 0u; i < value_len; ++i) {
    unsigned char c = (unsigned char)value[i];
    if (c < 0x20u || c > 0x7Eu) {
      return 0;
    }
  }
  return 1;
}

static void parse_repo_line(const char *key, const char *value) {
  if (update_agent_local_equal(key, "channel")) {
    update_agent_local_copy(update_agent_g_status.channel,
                            sizeof(update_agent_g_status.channel), value);
    update_agent_local_copy(update_agent_g_status.branch,
                            sizeof(update_agent_g_status.branch),
                            branch_for_channel(value));
  } else if (update_agent_local_equal(key, "branch")) {
    update_agent_local_copy(update_agent_g_status.branch,
                            sizeof(update_agent_g_status.branch), value);
  } else if (update_agent_local_equal(key, "source")) {
    update_agent_local_copy(update_agent_g_status.source,
                            sizeof(update_agent_g_status.source), value);
  } else if (update_agent_local_equal(key, "manifest")) {
    update_agent_local_copy(update_agent_g_status.manifest_path,
                            sizeof(update_agent_g_status.manifest_path), value);
  } else if (update_agent_local_equal(key, "remote_manifest")) {
    update_agent_local_copy(update_agent_g_status.remote_manifest_url,
                            sizeof(update_agent_g_status.remote_manifest_url),
                            value);
  }
}

static void parse_manifest_line(const char *key, const char *value,
                                struct update_manifest_view *view) {
  if (!view) {
    return;
  }
  if (update_agent_local_equal(key, "available_version")) {
    update_agent_local_copy(view->version, sizeof(view->version), value);
  } else if (update_agent_local_equal(key, "channel")) {
    update_agent_local_copy(view->channel, sizeof(view->channel), value);
  } else if (update_agent_local_equal(key, "branch")) {
    update_agent_local_copy(view->branch, sizeof(view->branch), value);
  } else if (update_agent_local_equal(key, "source")) {
    update_agent_local_copy(view->source, sizeof(view->source), value);
  } else if (update_agent_local_equal(key, "published_at")) {
    update_agent_local_copy(view->published_at, sizeof(view->published_at),
                            value);
  } else if (update_agent_local_equal(key, "payload_url")) {
    update_agent_local_copy(view->payload_url, sizeof(view->payload_url),
                            value);
  } else if (update_agent_local_equal(key, "payload_sha256")) {
    update_agent_local_copy(view->payload_sha256, sizeof(view->payload_sha256),
                            value);
  } else if (update_agent_local_equal(key, "signature_ed25519")) {
    update_agent_local_copy(view->signature_ed25519,
                            sizeof(view->signature_ed25519), value);
  }
}

static void parse_state_line(const char *key, const char *value,
                             struct update_state_view *view) {
  if (!view) {
    return;
  }
  if (update_agent_local_equal(key, "pending_activation")) {
    view->pending_activation =
        update_agent_parse_bool_value(value) ? 1u : 0u;
  } else if (update_agent_local_equal(key, "staged_manifest")) {
    update_agent_local_copy(view->staged_manifest_path,
                            sizeof(view->staged_manifest_path), value);
  } else if (update_agent_local_equal(key, "payload_cache")) {
    update_agent_local_copy(view->payload_cache_path,
                            sizeof(view->payload_cache_path), value);
  } else if (update_agent_local_equal(key, "payload_cache_sha256") &&
             update_agent_local_hex_string_valid(
                 value, UPDATE_AGENT_SHA256_HEX_LEN)) {
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

  /* Skip silently when the value carries non-printable bytes. This
   * is the analogue of `value_is_printable_ascii` in capypkg: hostile
   * manifests / tampered state.ini / repository.ini could otherwise
   * inject ANSI escapes into the in-memory status struct, which is
   * echoed by `cmd_update_status` to the serial port. Dropping the
   * line keeps every downstream consumer safe without faulting the
   * whole parse (other lines may still be valid). */
  if (!update_value_is_printable_ascii(value, len)) {
    return;
  }

  if (parse_mode == 0) {
    parse_repo_line(key, value);
  } else if (parse_mode == 1) {
    parse_manifest_line(key, value, (struct update_manifest_view *)target);
  } else if (parse_mode == 2) {
    parse_state_line(key, value, (struct update_state_view *)target);
  }
}

void update_agent_parse_buffer(const char *buffer, size_t len, int parse_mode,
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

int update_agent_manifest_capture_signed_text(
    const char *buffer, size_t len, struct update_manifest_view *view) {
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

/* ── manifest + state buffered readers ─────────────────────────────── */

int update_agent_read_manifest_view(const char *path,
                                    struct update_manifest_view *view) {
  char buffer[768];
  size_t read_len = 0u;
  int rc = 0;
  update_agent_read_file_fn reader = update_agent_active_reader();

  if (!path || !view) {
    return -1;
  }
  update_agent_manifest_view_reset(view);
  rc = reader(path, buffer, sizeof(buffer), &read_len);
  if (rc != 0 || read_len == 0u) {
    return -1;
  }
  if (update_agent_manifest_capture_signed_text(buffer, read_len, view) != 0) {
    return -2;
  }
  update_agent_parse_buffer(buffer, read_len, 1, view);
  return view->version[0] ? 0 : -2;
}

int update_agent_read_state_view(struct update_state_view *view) {
  char buffer[256];
  size_t read_len = 0u;
  update_agent_read_file_fn reader = update_agent_active_reader();

  if (!view) {
    return -1;
  }
  update_agent_state_view_reset(view);
  if (reader(UPDATE_AGENT_STATE_PATH, buffer, sizeof(buffer), &read_len) != 0 ||
      read_len == 0u) {
    return 1;
  }
  update_agent_parse_buffer(buffer, read_len, 2, view);
  return 0;
}

void update_agent_prepare_repository_status(void) {
  char buffer[768];
  char current_version[UPDATE_AGENT_VERSION_MAX];
  size_t read_len = 0u;
  update_agent_read_file_fn reader = update_agent_active_reader();

  update_agent_init(NULL);
  update_agent_local_copy(current_version, sizeof(current_version),
                          update_agent_g_status.current_version[0]
                              ? update_agent_g_status.current_version
                              : "unknown");
  update_agent_seed_defaults(current_version);

  if (reader(UPDATE_AGENT_REPOSITORY_PATH, buffer, sizeof(buffer), &read_len) ==
          0 &&
      read_len > 0u) {
    update_agent_parse_buffer(buffer, read_len, 0, NULL);
  }
  if (!update_agent_g_status.branch[0]) {
    update_agent_local_copy(update_agent_g_status.branch,
                            sizeof(update_agent_g_status.branch),
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
