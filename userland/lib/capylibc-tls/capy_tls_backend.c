#include "capy_tls_internal.h"
#include "security/tls_hostname_policy.h"

#ifdef CAPYOS_TLS_USERLAND_HANDSHAKE
#include "capylibc/capylibc.h"               /* capy_send/recv/getrandom/clock */
#include "capylibc-tls/capy_tls_handshake.h" /* capy_tls_unix_to_x509_time */

/* The userland TLS client validates against the SAME in-tree anchor bundle
 * the kernel uses (capyos_tls_trust_anchors), compiled into the userland
 * build under this flag (see Makefile). */
extern const br_x509_trust_anchor *capyos_tls_trust_anchors(void);
extern size_t capyos_tls_trust_anchor_count(void);

static const char *const g_capy_tls_alpn_protocols[] = { "http/1.1" };

/* BearSSL's ssl_engine references br_prng_seeder_system (sysrng.c is not
 * linked); CapyOS seeds the DRBG explicitly via SYS_GETRANDOM, so this
 * reports "no system seeder", exactly like the kernel stub. */
br_prng_seeder br_prng_seeder_system(const char **name) {
  if (name) *name = "none";
  return 0;
}

/* BearSSL's x509_minimal validator references the libc clock time() as a
 * fallback for the validation "now". CapyOS always sets the validation time
 * explicitly via br_x509_minimal_set_time() (capy_tls_engine_set_time below),
 * so this is normally dead code — but x509_minimal.o still references the
 * symbol, so the freestanding ring-3 link (tls_smoke, capybrowse, any binary
 * embedding the BearSSL path) must define it. Same role as
 * br_prng_seeder_system above; signature matches the kernel stub in
 * src/arch/x86_64/stubs.c. Returns the real calendar time (SYS_CLOCK_REALTIME)
 * so that, even on a path that skipped set_time, validity windows stay
 * correct rather than defaulting to the 1970 epoch.
 *
 * Excluded from the host unit-test build (UNIT_TEST): there `time` is the real
 * C library symbol, and the host libc provides it — defining our own would
 * collide. Only the freestanding ring-3 build (no libc) needs the stub. */
#ifndef UNIT_TEST
long time(long *out) {
  long now = capy_clock_realtime();
  if (now < 0) {
    now = 0;
  }
  if (out) {
    *out = now;
  }
  return now;
}
#endif /* !UNIT_TEST */

static int capy_tls_engine_socket_read(void *read_context,
                                       unsigned char *data, size_t len) {
  struct capy_tls_context *ctx = (struct capy_tls_context *)read_context;
  long r;
  if (!ctx || !data || len == 0) return -1;
  r = capy_recv(ctx->socket_fd, data, len, 0);
  return r > 0 ? (int)r : -1;
}

static int capy_tls_engine_socket_write(void *write_context,
                                        const unsigned char *data, size_t len) {
  struct capy_tls_context *ctx = (struct capy_tls_context *)write_context;
  long r;
  if (!ctx || !data || len == 0) return -1;
  r = capy_send(ctx->socket_fd, data, len, 0);
  return r > 0 ? (int)r : -1;
}

/* Seed BearSSL's DRBG from the kernel CSPRNG (SYS_GETRANDOM). Fail-closed
 * if the full seed cannot be obtained. Mirrors kernel tls_seed_engine. */
static int capy_tls_engine_seed(struct capy_tls_context *ctx) {
  unsigned char seed[48];
  volatile unsigned char *wipe;
  size_t i;
  long n = capy_getrandom(seed, sizeof seed, 0);
  int ok = (n == (long)sizeof seed);
  if (ok) {
    br_ssl_engine_inject_entropy(&ctx->bearssl_client.eng, seed, sizeof seed);
  }
  wipe = seed;
  for (i = 0; i < sizeof seed; i++) wipe[i] = 0;
  return ok;
}

/* Set X.509 validation time from the kernel RTC (SYS_CLOCK_REALTIME).
 * Mirrors kernel tls_set_validation_time. */
static void capy_tls_engine_set_time(struct capy_tls_context *ctx) {
  long unix_time = capy_clock_realtime();
  uint32_t days = 0u, seconds = 0u;
  if (unix_time < 0) unix_time = 0;
  capy_tls_unix_to_x509_time((uint64_t)unix_time, &days, &seconds);
  br_x509_minimal_set_time(&ctx->bearssl_x509, days, seconds);
}
#endif /* CAPYOS_TLS_USERLAND_HANDSHAKE */

static void capy_tls_backend_state_zero(
    struct capy_tls_backend_state *state) {
  uint8_t *p = (uint8_t *)state;
  size_t i;
  if (!state) return;
  for (i = 0; i < sizeof(*state); i++) p[i] = 0;
}

static int capy_tls_backend_config_ready(
    const struct capy_tls_effective_config *config) {
  if (!config) return 0;
  if (config->verify_peer != 1) return 0;
  if (config->ca_cert && config->ca_cert_len == 0) return 0;
  if (!config->ca_cert && config->ca_cert_len > 0) return 0;
  if (config->timeout_ms < CAPY_TLS_TIMEOUT_MIN_MS ||
      config->timeout_ms > CAPY_TLS_TIMEOUT_MAX_MS) return 0;
  return 1;
}

static int capy_tls_backend_copy_sni(char *dst, const char *src) {
  size_t i = 0;
  if (!dst || !tls_hostname_policy_valid(src)) return 0;
  while (src[i]) {
    if (i >= CAPY_TLS_HOSTNAME_MAX_LEN) return 0;
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
  return 1;
}

static int capy_tls_backend_prepare_trust_anchors(
    struct capy_tls_backend_state *state,
    const struct capy_tls_effective_config *config) {
  const struct capy_tls_trust_anchor_catalog *catalog = 0;
  if (!state || !config) return 0;
  if (config->ca_cert && config->ca_cert_len == 0) return 0;
  if (!config->ca_cert && config->ca_cert_len > 0) return 0;
  if (!config->ca_cert) {
    catalog = capy_tls_default_trust_anchor_catalog();
    if (!capy_tls_default_trust_anchors_available() || !catalog) return 0;
  }
  state->trust_anchors_ready = 1;
  if (config->ca_cert) {
    state->custom_anchor_ready = 1;
    state->trust_anchor_count = 1u;
    state->custom_anchor_len = config->ca_cert_len;
  } else {
    state->trust_anchor_count = catalog->anchor_count;
    state->trust_anchor_rsa_count = catalog->rsa_anchor_count;
    state->trust_anchor_ec_count = catalog->ec_anchor_count;
    state->trust_anchor_key_type_mask = catalog->key_type_mask;
    state->trust_catalog_fingerprint = catalog->fingerprint;
    state->trust_anchor_slot_count =
        capy_tls_default_trust_anchor_slot_count();
    state->trust_slot_layout_fingerprint =
        capy_tls_default_trust_anchor_slot_layout_fingerprint();
    state->trust_anchor_descriptor_count =
        capy_tls_default_trust_anchor_descriptor_count();
    state->trust_descriptor_fingerprint =
        capy_tls_default_trust_anchor_descriptor_fingerprint();
    state->trust_anchor_bundle_entry_count =
        capy_tls_default_trust_anchor_bundle_entry_count();
    state->trust_anchor_bundle_fingerprint =
        capy_tls_default_trust_anchor_bundle_fingerprint();
    state->trust_material_summary_fingerprint =
        capy_tls_default_trust_material_summary_fingerprint();
    state->trust_subject_dn_total_bytes =
        capy_tls_default_trust_subject_dn_total_bytes();
    state->trust_key_material_total_bytes =
        capy_tls_default_trust_key_material_total_bytes();
    state->trust_subject_dn_max_bytes =
        capy_tls_default_trust_subject_dn_max_bytes();
    state->trust_key_material_max_bytes =
        capy_tls_default_trust_key_material_max_bytes();
    state->trust_manifest_schema_version =
        capy_tls_default_trust_manifest_schema_version();
    state->trust_manifest_source_id =
        capy_tls_default_trust_manifest_source_id();
    state->trust_manifest_flags =
        capy_tls_default_trust_manifest_flags();
    state->trust_manifest_fingerprint =
        capy_tls_default_trust_manifest_fingerprint();
  }
  return 1;
}

static int capy_tls_backend_prepare_plan(
    struct capy_tls_backend_state *state) {
  const struct capy_tls_backend_plan *plan;
  if (!state) return 0;
  if (state->custom_anchor_ready) return 1;
  if (state->trust_anchor_bundle_fingerprint == 0) return 0;
  if (!capy_tls_default_backend_plan_consistent()) return 0;
  plan = capy_tls_default_backend_plan();
  if (!plan || plan->handshake_allowed != 0) return 0;
  state->backend_plan_ready = 1;
  state->handshake_allowed = plan->handshake_allowed;
  state->backend_plan_schema_version = plan->schema_version;
  state->backend_plan_engine_id = plan->engine_id;
  state->backend_plan_flags = plan->flags;
  state->backend_plan_fingerprint = plan->fingerprint;
  return 1;
}

static int capy_tls_backend_prepare_bearssl_state(
    struct capy_tls_backend_state *state) {
  const struct capy_tls_bearssl_reserved_state *reserved;
  if (!state) return 0;
  if (state->custom_anchor_ready) return 1;
  if (!state->backend_plan_ready) return 0;
  if (state->handshake_allowed != 0) return 0;
  if (!capy_tls_default_bearssl_reserved_state_consistent()) return 0;
  reserved = capy_tls_default_bearssl_reserved_state();
  if (!reserved || reserved->engine_initialized != 0) return 0;
  if (reserved->handshake_allowed != 0) return 0;
  if (reserved->backend_plan_fingerprint != state->backend_plan_fingerprint)
    return 0;
  state->bearssl_state_ready = 1;
  state->bearssl_engine_initialized = reserved->engine_initialized;
  state->bearssl_state_schema_version = reserved->schema_version;
  state->bearssl_state_engine_id = reserved->engine_id;
  state->bearssl_state_flags = reserved->flags;
  state->bearssl_state_fingerprint = reserved->fingerprint;
  state->bearssl_context_bytes = reserved->reserved_context_bytes;
  state->bearssl_io_buffer_bytes = reserved->reserved_io_bytes;
  return 1;
}

static int capy_tls_backend_prepare_bearssl_adapter(
    struct capy_tls_backend_state *state) {
  const struct capy_tls_bearssl_adapter_contract *adapter;
  if (!state) return 0;
  if (state->custom_anchor_ready) return 1;
  if (!state->backend_plan_ready || !state->bearssl_state_ready) return 0;
  if (state->handshake_allowed != 0) return 0;
  if (state->bearssl_engine_initialized != 0) return 0;
  if (!capy_tls_default_bearssl_adapter_consistent()) return 0;
  adapter = capy_tls_default_bearssl_adapter_contract();
  if (!adapter || adapter->adapter_initialized != 0) return 0;
  if (adapter->handshake_allowed != 0) return 0;
  if (adapter->backend_plan_fingerprint != state->backend_plan_fingerprint)
    return 0;
  if (adapter->reserved_state_fingerprint != state->bearssl_state_fingerprint)
    return 0;
  state->bearssl_adapter_ready = 1;
  state->bearssl_adapter_initialized = adapter->adapter_initialized;
  state->bearssl_adapter_schema_version = adapter->schema_version;
  state->bearssl_adapter_engine_id = adapter->engine_id;
  state->bearssl_adapter_flags = adapter->flags;
  state->bearssl_adapter_fingerprint = adapter->fingerprint;
  return 1;
}

static int capy_tls_backend_prepare_state(struct capy_tls_context *ctx) {
  if (!ctx) return 0;
  capy_tls_backend_state_zero(&ctx->backend);
  if (!capy_tls_backend_copy_sni(ctx->backend.sni, ctx->hostname)) return 0;
  if (!capy_tls_backend_prepare_trust_anchors(&ctx->backend,
                                             &ctx->config)) return 0;
  if (!capy_tls_backend_prepare_plan(&ctx->backend)) return 0;
  if (!capy_tls_backend_prepare_bearssl_state(&ctx->backend)) return 0;
  if (!capy_tls_backend_prepare_bearssl_adapter(&ctx->backend)) return 0;
  ctx->backend.context_ready = 1;
  ctx->backend.sni_ready = 1;
  ctx->backend.timeout_ready = 1;
  ctx->backend.handshake_started = 0;
  ctx->backend.timeout_ms = ctx->config.timeout_ms;
  return 1;
}

capy_tls_err_t capy_tls_backend_connect(struct capy_tls_context *ctx) {
#ifdef CAPYOS_TLS_USERLAND_HANDSHAKE
  /* Slice 5.4: real BearSSL TLS-1.2 client handshake, mirroring the kernel
   * tls_connect path (src/security/tls.c). Every error path is fail-closed
   * — an untrusted/unreachable/expired peer never yields CAPY_TLS_OK. */
  const br_x509_trust_anchor *anchors;
  size_t anchor_count;
  unsigned state;

  if (!ctx) return CAPY_TLS_EINVAL;
  ctx->bearssl_connected = 0;
  if (ctx->socket_fd < 0) return CAPY_TLS_EINVAL;
  if (!tls_hostname_policy_valid(ctx->hostname)) return CAPY_TLS_EINVAL;
  if (!capy_tls_backend_config_ready(&ctx->config)) return CAPY_TLS_EINVAL;
  /* Custom CA pinning is not wired into the userland engine yet. */
  if (ctx->config.ca_cert || ctx->config.ca_cert_len) return CAPY_TLS_EUNSUPPORTED;

  anchors = capyos_tls_trust_anchors();
  anchor_count = capyos_tls_trust_anchor_count();
  if (!anchors || anchor_count == 0u) return CAPY_TLS_ESTATE;

  br_ssl_client_init_full(&ctx->bearssl_client, &ctx->bearssl_x509,
                          anchors, anchor_count);
  capy_tls_engine_set_time(ctx);
  br_ssl_engine_set_buffer(&ctx->bearssl_client.eng, ctx->bearssl_iobuf,
                           sizeof ctx->bearssl_iobuf, 1);
  br_ssl_engine_set_versions(&ctx->bearssl_client.eng, BR_TLS12, BR_TLS12);
  br_ssl_engine_set_protocol_names(&ctx->bearssl_client.eng,
                                   g_capy_tls_alpn_protocols, 1u);
  if (!capy_tls_engine_seed(ctx)) return CAPY_TLS_ESTATE;
  if (!br_ssl_client_reset(&ctx->bearssl_client, ctx->hostname, 0))
    return CAPY_TLS_ESTATE;

  br_sslio_init(&ctx->bearssl_io, &ctx->bearssl_client.eng,
                capy_tls_engine_socket_read, ctx,
                capy_tls_engine_socket_write, ctx);
  /* Drive the handshake to completion (sends ClientHello, processes the
   * server flight + validates the cert chain). br_sslio_flush returns < 0
   * on any handshake/certificate error. */
  if (br_sslio_flush(&ctx->bearssl_io) < 0) return CAPY_TLS_ESTATE;
  state = br_ssl_engine_current_state(&ctx->bearssl_client.eng);
  if ((state & (BR_SSL_SENDAPP | BR_SSL_RECVAPP)) == 0) return CAPY_TLS_ESTATE;
  ctx->bearssl_connected = 1;
  return CAPY_TLS_OK;
#else
  if (!ctx) return CAPY_TLS_EINVAL;
  capy_tls_backend_state_zero(&ctx->backend);
  if (ctx->socket_fd < 0) return CAPY_TLS_EINVAL;
  if (!tls_hostname_policy_valid(ctx->hostname)) return CAPY_TLS_EINVAL;
  if (!capy_tls_backend_config_ready(&ctx->config)) return CAPY_TLS_EINVAL;
  if (!capy_tls_backend_prepare_state(ctx)) {
    capy_tls_backend_state_zero(&ctx->backend);
    return CAPY_TLS_EINVAL;
  }
  return CAPY_TLS_EUNSUPPORTED;
#endif
}
