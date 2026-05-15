#include "capy_tls_internal.h"
#include "security/tls_hostname_policy.h"

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
}
