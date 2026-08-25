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
    uint32_t digit = (uint32_t)(*p - '0');
    /* Reject a version component that would overflow uint32_t rather than
     * letting it wrap: a wrapped major/minor/patch/prerelease number would
     * misorder compare_update_versions and could flip an "update available?"
     * decision on a hostile manifest. Mirrors the guard in capypkg's
     * parse_uint32. Legitimate version numbers are far below this bound. */
    if (value > (0xFFFFFFFFu - digit) / 10u) {
      return -1;
    }
    value = value * 10u + digit;
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
    return -1;
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

static int parse_payload_size(const char *value, uint32_t *out) {
  uint32_t size = 0u;
  size_t i = 0u;
  if (!value || !out || value[0] < '1' || value[0] > '9') {
    return -1;
  }
  while (value[i]) {
    uint32_t digit = 0u;
    if (!update_agent_local_is_digit(value[i])) {
      return -1;
    }
    digit = (uint32_t)(value[i] - '0');
    if (size > (0xFFFFFFFFu - digit) / 10u) {
      return -1;
    }
    size = size * 10u + digit;
    ++i;
  }
  if (size == 0u || size > UPDATE_AGENT_PAYLOAD_MAX_BYTES) {
    return -1;
  }
  *out = size;
  return 0;
}

/* Raw Ed25519 public key for signed latest.ini catalogs. The corresponding
 * private key is offline-only and must never be present in the image or CI. */
static const uint8_t update_agent_release_public_key[ED25519_PUBLIC_KEY_SIZE] = {
    0x9a, 0x98, 0xd2, 0x01, 0x1b, 0xa9, 0x54, 0xa3,
    0x97, 0x5c, 0x9f, 0x62, 0x8e, 0x2f, 0x92, 0x55,
    0xdf, 0x87, 0xf1, 0xf4, 0x29, 0xc9, 0x66, 0x5d,
    0x51, 0xac, 0x6a, 0xaf, 0x91, 0xf4, 0x74, 0xe0};

#if defined(CAPYOS_UPDATE_LAB_TRUST_KEY_HEX)
/* Lab-only trust anchor (Etapa 8 signed A/B gate). The production private key
 * is offline and can never enter an automated gate, so a build gated by this
 * macro verifies manifests against a throwaway key generated per run. The macro
 * is never set by any official build: `iso-uefi` refuses a kernel carrying the
 * banner below unless the caller is a smoke target, and the macro simultaneously
 * relaxes `payload_url` to plain http:// so a hermetic host server can serve the
 * payload. Malformed hex fails closed: never falls back to the release key. */
const char *const update_agent_lab_trust_anchor_banner =
    "[lab] update trust anchor overridden; kernel not for production\n";

static const char update_agent_lab_trust_key_hex[] =
    CAPYOS_UPDATE_LAB_TRUST_KEY_HEX;

static int update_agent_lab_trust_key(uint8_t out[ED25519_PUBLIC_KEY_SIZE]) {
  if (!update_agent_local_hex_string_valid(update_agent_lab_trust_key_hex,
                                           ED25519_PUBLIC_KEY_SIZE * 2u)) {
    return -1;
  }
  return update_agent_local_hex_to_bytes(update_agent_lab_trust_key_hex, out,
                                         ED25519_PUBLIC_KEY_SIZE);
}
#endif

int update_agent_manifest_payload_sha256_valid(
    const struct update_manifest_view *view) {
  return view &&
         update_agent_local_hex_string_valid(view->payload_sha256,
                                             UPDATE_AGENT_SHA256_HEX_LEN);
}

int update_agent_manifest_payload_size_valid(
    const struct update_manifest_view *view) {
  return view &&
         (!view->payload_size_present ||
          (view->payload_size > 0u &&
           view->payload_size <= UPDATE_AGENT_PAYLOAD_MAX_BYTES));
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
#if defined(CAPYOS_UPDATE_LAB_TRUST_KEY_HEX)
  } else if (update_agent_local_starts_with(url, "http://")) {
    /* Lab gate only: a hermetic host server cannot present a publicly trusted
     * certificate to the kernel TLS stack, which always verifies the peer. */
    if (update_agent_local_equal(url, "http://")) {
      return 0;
    }
#endif
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
  {
    const uint8_t *trust_key = update_agent_release_public_key;
#if defined(CAPYOS_UPDATE_LAB_TRUST_KEY_HEX)
    uint8_t lab_key[ED25519_PUBLIC_KEY_SIZE];
    if (update_agent_lab_trust_key(lab_key) != 0) {
      return 0;
    }
    trust_key = lab_key;
#endif
    return ed25519_verify(signature, (const uint8_t *)view->signed_text,
                          view->signed_len, trust_key) == 0;
  }
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

static void build_remote_manifest_url(const char *source, const char *channel,
                                      const char *branch,
                                      char *dst, size_t dst_size) {
  if (!dst || dst_size == 0u) {
    return;
  }
  dst[0] = '\0';
  if (!source || !source[0] ||
      !update_agent_local_starts_with(source, UPDATE_AGENT_GITHUB_PREFIX)) {
    return;
  }
  if (update_agent_local_equal(channel, "develop")) {
    update_agent_local_append(dst, dst_size,
                              "https://raw.githubusercontent.com/");
    update_agent_local_append(dst, dst_size, source + 7u);
    update_agent_local_append(dst, dst_size, "/");
    update_agent_local_append(dst, dst_size, "refs/heads/");
    update_agent_local_append(dst, dst_size,
                              (branch && branch[0]) ? branch : branch_for_channel(channel));
    update_agent_local_append(dst, dst_size,
                              UPDATE_AGENT_REMOTE_MANIFEST_SUFFIX);
  } else {
    update_agent_local_append(dst, dst_size, "https://github.com/");
    update_agent_local_append(dst, dst_size, source + 7u);
    update_agent_local_append(dst, dst_size,
                              "/releases/latest/download/latest.ini");
  }
}

static int legacy_official_stable_url(const char *url) {
  static const char prefix[] =
      "https://raw.githubusercontent.com/henriquefarisco/CapyOS/";
  static const char tag_prefix[] = "refs/tags/v";
  static const char main_ref[] =
      "refs/heads/main/system/update/latest.ini";
  const char *p = NULL;
  size_t dots = 0u;

  if (!url || !update_agent_local_starts_with(url, prefix)) {
    return 0;
  }
  p = url + sizeof(prefix) - 1u;
  if (update_agent_local_equal(p, main_ref)) {
    return 1;
  }
  if (!update_agent_local_starts_with(p, tag_prefix)) {
    return 0;
  }
  p += sizeof(tag_prefix) - 1u;
  while (*p && *p != '/') {
    if (*p == '.') {
      ++dots;
    } else if (!update_agent_local_is_digit(*p)) {
      return 0;
    }
    ++p;
  }
  return dots == 2u && update_agent_local_equal(
                            p, UPDATE_AGENT_REMOTE_MANIFEST_SUFFIX);
}

static int replace_repository_remote(const char *buffer, size_t len,
                                     const char *old_url,
                                     const char *new_url, char *out,
                                     size_t out_size) {
  static const char key[] = "remote_manifest=";
  size_t start = 0u;
  size_t written = 0u;
  int replaced = 0;

  if (!buffer || !old_url || !new_url || !out || out_size == 0u) {
    return -1;
  }
  while (start < len) {
    size_t line_end = start;
    size_t end = start;
    size_t i = 0u;
    int exact_legacy_line = 1;
    while (line_end < len && buffer[line_end] != '\n' &&
           buffer[line_end] != '\r') {
      ++line_end;
    }
    end = line_end;
    while (end < len && (buffer[end] == '\n' || buffer[end] == '\r')) {
      ++end;
    }
    while (key[i] && start + i < line_end && buffer[start + i] == key[i]) {
      ++i;
    }
    if (key[i] || line_end - start - i == 0u) {
      exact_legacy_line = 0;
    } else {
      size_t j = 0u;
      while (old_url[j] && start + i + j < line_end &&
             buffer[start + i + j] == old_url[j]) {
        ++j;
      }
      if (old_url[j] || start + i + j != line_end) {
        exact_legacy_line = 0;
      }
    }
    if (exact_legacy_line) {
      for (i = 0u; key[i]; ++i) {
        if (written + 1u >= out_size) {
          return -1;
        }
        out[written++] = key[i];
      }
      for (i = 0u; new_url[i]; ++i) {
        if (written + 1u >= out_size) {
          return -1;
        }
        out[written++] = new_url[i];
      }
      for (i = line_end; i < end; ++i) {
        if (written + 1u >= out_size) {
          return -1;
        }
        out[written++] = buffer[i];
      }
      replaced = 1;
    } else {
      for (i = start; i < end; ++i) {
        if (written + 1u >= out_size) {
          return -1;
        }
        out[written++] = buffer[i];
      }
    }
    start = end;
  }
  out[written] = '\0';
  return replaced ? 0 : 1;
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
  } else if (update_agent_local_equal(key, "payload_size")) {
    view->payload_size_present = 1u;
    view->payload_size = 0u;
    (void)parse_payload_size(value, &view->payload_size);
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
  char migrated[768];
  char legacy_url[UPDATE_AGENT_REMOTE_MAX];
  char current_version[UPDATE_AGENT_VERSION_MAX];
  size_t read_len = 0u;
  update_agent_read_file_fn reader = update_agent_active_reader();
  update_agent_write_file_fn writer = update_agent_active_writer();

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
  if (update_agent_local_equal(update_agent_g_status.source,
                               UPDATE_AGENT_DEFAULT_SOURCE) &&
      !update_agent_local_equal(update_agent_g_status.channel, "develop") &&
      legacy_official_stable_url(
          update_agent_g_status.remote_manifest_url)) {
    update_agent_local_copy(legacy_url, sizeof(legacy_url),
                            update_agent_g_status.remote_manifest_url);
    build_remote_manifest_url(update_agent_g_status.source,
                              update_agent_g_status.channel,
                              update_agent_g_status.branch,
                              update_agent_g_status.remote_manifest_url,
                              sizeof(update_agent_g_status.remote_manifest_url));
    if (writer && replace_repository_remote(
                      buffer, read_len, legacy_url,
                      update_agent_g_status.remote_manifest_url, migrated,
                      sizeof(migrated)) == 0) {
      (void)writer(UPDATE_AGENT_REPOSITORY_PATH, migrated);
    }
  }
  if (!update_agent_g_status.remote_manifest_url[0]) {
    build_remote_manifest_url(update_agent_g_status.source,
                              update_agent_g_status.channel,
                              update_agent_g_status.branch,
                              update_agent_g_status.remote_manifest_url,
                              sizeof(update_agent_g_status.remote_manifest_url));
  }
}
