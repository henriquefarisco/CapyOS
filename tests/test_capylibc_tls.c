#include "capylibc-tls/capy_tls.h"
#include "../userland/lib/capylibc-tls/capy_tls_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(label) do { tests_run++; printf("    %-68s", label); } while (0)
#define PASS() do { tests_passed++; printf(" OK\n"); } while (0)
#define FAIL(why) do { printf(" FAIL: %s\n", why); } while (0)

static struct capy_tls_context *fake_ctx(void) {
  return (struct capy_tls_context *)(uintptr_t)1;
}

static void test_tls_init_resets_state(void) {
  struct capy_tls_security_info info;
  memset(&info, 0xA5, sizeof(info));
  (void)capy_tls_init();
  TEST("capy_tls_init resets unsupported userland TLS state");
  if (capy_tls_is_supported() == 0 &&
      capy_tls_last_error() == CAPY_TLS_OK &&
      capy_tls_last_state() == CAPY_TLS_STATE_INIT &&
      capy_tls_get_last_security_info(&info) == 0 &&
      info.protocol_version == 0 && info.cipher_suite == 0 &&
      info.trust_anchor_count == 0 && info.peer_verified == 0 &&
      info.hostname_validated == 0 && info.custom_anchor_loaded == 0 &&
      info.alpn[0] == '\0') PASS();
  else FAIL("init did not reset state/info");
}

static void test_tls_config_resolve_defaults(void) {
  struct capy_tls_effective_config effective;
  struct capy_tls_config cfg_default_timeout = { 1, 0, 0, 0 };
  TEST("capy_tls_config_resolve normalizes safe defaults");
  if (capy_tls_config_resolve(0, &effective) &&
      effective.verify_peer == 1 && effective.ca_cert == 0 &&
      effective.ca_cert_len == 0 &&
      effective.timeout_ms == CAPY_TLS_TIMEOUT_DEFAULT_MS &&
      capy_tls_config_resolve(&cfg_default_timeout, &effective) &&
      effective.verify_peer == 1 && effective.ca_cert == 0 &&
      effective.ca_cert_len == 0 &&
      effective.timeout_ms == CAPY_TLS_TIMEOUT_DEFAULT_MS) PASS();
  else FAIL("safe defaults not normalized");
}

static void test_tls_config_resolve_custom_config(void) {
  uint8_t cert = 0;
  struct capy_tls_effective_config effective;
  struct capy_tls_config cfg = { 1, &cert, sizeof(cert), 1000 };
  TEST("capy_tls_config_resolve snapshots explicit config");
  if (capy_tls_config_resolve(&cfg, &effective) &&
      effective.verify_peer == 1 && effective.ca_cert == &cert &&
      effective.ca_cert_len == sizeof(cert) &&
      effective.timeout_ms == 1000) PASS();
  else FAIL("explicit config not normalized");
}

static void test_tls_config_resolve_rejects_invalid_output(void) {
  struct capy_tls_config cfg = { 1, 0, 0, 1000 };
  TEST("capy_tls_config_resolve rejects NULL output");
  if (!capy_tls_config_resolve(&cfg, 0)) PASS();
  else FAIL("NULL output accepted");
}

static void test_tls_context_prepare_snapshots_ready_context(void) {
  uint8_t cert = 0;
  struct capy_tls_context ctx;
  struct capy_tls_effective_config effective = {
      1, &cert, sizeof(cert), 1000
  };
  TEST("capy_tls_context_prepare snapshots ready context");
  if (capy_tls_context_prepare(&ctx, 7, "example.com", &effective) &&
      ctx.socket_fd == 7 && strcmp(ctx.hostname, "example.com") == 0 &&
      ctx.config.verify_peer == 1 && ctx.config.ca_cert == &cert &&
      ctx.config.ca_cert_len == sizeof(cert) &&
      ctx.config.timeout_ms == 1000) PASS();
  else FAIL("ready context not snapped");
}

static void test_tls_context_prepare_rejects_invalid_inputs(void) {
  struct capy_tls_context ctx;
  struct capy_tls_effective_config effective = {
      1, 0, 0, CAPY_TLS_TIMEOUT_DEFAULT_MS
  };
  TEST("capy_tls_context_prepare rejects invalid inputs");
  if (!capy_tls_context_prepare(0, 3, "example.com", &effective) &&
      !capy_tls_context_prepare(&ctx, -1, "example.com", &effective) &&
      !capy_tls_context_prepare(&ctx, 3, "bad host", &effective) &&
      !capy_tls_context_prepare(&ctx, 3, "example.com", 0)) PASS();
  else FAIL("invalid context input accepted");
}

static void test_tls_context_reset_and_clear_scrub_state(void) {
  uint8_t cert = 0;
  struct capy_tls_context ctx;
  struct capy_tls_effective_config effective = {
      1, &cert, sizeof(cert), 1000
  };
  TEST("capy_tls_context_clear scrubs prepared context");
  if (capy_tls_context_prepare(&ctx, 7, "example.com", &effective)) {
    capy_tls_context_clear(&ctx);
    if (ctx.socket_fd == -1 && ctx.hostname[0] == '\0' &&
        ctx.config.verify_peer == 1 && ctx.config.ca_cert == 0 &&
        ctx.config.ca_cert_len == 0 &&
        ctx.config.timeout_ms == CAPY_TLS_TIMEOUT_DEFAULT_MS &&
        ctx.backend.context_ready == 0 &&
        ctx.backend.trust_anchors_ready == 0 &&
        ctx.backend.custom_anchor_ready == 0 &&
        ctx.backend.trust_anchor_count == 0 &&
        ctx.backend.trust_anchor_rsa_count == 0 &&
        ctx.backend.trust_anchor_ec_count == 0 &&
        ctx.backend.trust_anchor_key_type_mask == 0 &&
        ctx.backend.trust_catalog_fingerprint == 0 &&
        ctx.backend.trust_anchor_slot_count == 0 &&
        ctx.backend.trust_slot_layout_fingerprint == 0 &&
        ctx.backend.trust_anchor_descriptor_count == 0 &&
        ctx.backend.trust_descriptor_fingerprint == 0 &&
        ctx.backend.trust_anchor_bundle_entry_count == 0 &&
        ctx.backend.trust_anchor_bundle_fingerprint == 0 &&
        ctx.backend.trust_material_summary_fingerprint == 0 &&
        ctx.backend.trust_subject_dn_total_bytes == 0 &&
        ctx.backend.trust_key_material_total_bytes == 0 &&
        ctx.backend.trust_subject_dn_max_bytes == 0 &&
        ctx.backend.trust_key_material_max_bytes == 0 &&
        ctx.backend.trust_manifest_schema_version == 0 &&
        ctx.backend.trust_manifest_source_id == 0 &&
        ctx.backend.trust_manifest_flags == 0 &&
        ctx.backend.trust_manifest_fingerprint == 0 &&
        ctx.backend.backend_plan_ready == 0 &&
        ctx.backend.handshake_allowed == 0 &&
        ctx.backend.backend_plan_schema_version == 0 &&
        ctx.backend.backend_plan_engine_id == 0 &&
        ctx.backend.backend_plan_flags == 0 &&
        ctx.backend.backend_plan_fingerprint == 0 &&
        ctx.backend.bearssl_state_ready == 0 &&
        ctx.backend.bearssl_engine_initialized == 0 &&
        ctx.backend.bearssl_state_schema_version == 0 &&
        ctx.backend.bearssl_state_engine_id == 0 &&
        ctx.backend.bearssl_state_flags == 0 &&
        ctx.backend.bearssl_state_fingerprint == 0 &&
        ctx.backend.bearssl_context_bytes == 0 &&
        ctx.backend.bearssl_io_buffer_bytes == 0 &&
        ctx.backend.bearssl_adapter_ready == 0 &&
        ctx.backend.bearssl_adapter_initialized == 0 &&
        ctx.backend.bearssl_adapter_schema_version == 0 &&
        ctx.backend.bearssl_adapter_engine_id == 0 &&
        ctx.backend.bearssl_adapter_flags == 0 &&
        ctx.backend.bearssl_adapter_fingerprint == 0 &&
        ctx.backend.custom_anchor_len == 0 &&
        ctx.backend.sni[0] == '\0') PASS();
    else FAIL("context was not scrubbed");
  } else FAIL("context setup failed");
}

static void test_tls_context_prepare_resets_on_reject(void) {
  uint8_t cert = 0;
  struct capy_tls_context ctx;
  struct capy_tls_effective_config effective = {
      1, &cert, sizeof(cert), 1000
  };
  TEST("capy_tls_context_prepare clears rejected context");
  if (capy_tls_context_prepare(&ctx, 7, "example.com", &effective) &&
      !capy_tls_context_prepare(&ctx, -1, "example.com", &effective) &&
      ctx.socket_fd == -1 && ctx.hostname[0] == '\0' &&
      ctx.config.verify_peer == 1 && ctx.config.ca_cert == 0 &&
      ctx.config.ca_cert_len == 0 &&
      ctx.config.timeout_ms == CAPY_TLS_TIMEOUT_DEFAULT_MS &&
      ctx.backend.context_ready == 0 &&
      ctx.backend.trust_anchors_ready == 0 &&
      ctx.backend.custom_anchor_ready == 0 &&
      ctx.backend.trust_anchor_count == 0 &&
      ctx.backend.trust_anchor_rsa_count == 0 &&
      ctx.backend.trust_anchor_ec_count == 0 &&
      ctx.backend.trust_anchor_key_type_mask == 0 &&
      ctx.backend.trust_catalog_fingerprint == 0 &&
      ctx.backend.trust_anchor_slot_count == 0 &&
      ctx.backend.trust_slot_layout_fingerprint == 0 &&
      ctx.backend.trust_anchor_descriptor_count == 0 &&
      ctx.backend.trust_descriptor_fingerprint == 0 &&
      ctx.backend.trust_anchor_bundle_entry_count == 0 &&
      ctx.backend.trust_anchor_bundle_fingerprint == 0 &&
      ctx.backend.trust_material_summary_fingerprint == 0 &&
      ctx.backend.trust_subject_dn_total_bytes == 0 &&
      ctx.backend.trust_key_material_total_bytes == 0 &&
      ctx.backend.trust_subject_dn_max_bytes == 0 &&
      ctx.backend.trust_key_material_max_bytes == 0 &&
      ctx.backend.trust_manifest_schema_version == 0 &&
      ctx.backend.trust_manifest_source_id == 0 &&
      ctx.backend.trust_manifest_flags == 0 &&
      ctx.backend.trust_manifest_fingerprint == 0 &&
      ctx.backend.backend_plan_ready == 0 &&
      ctx.backend.handshake_allowed == 0 &&
      ctx.backend.backend_plan_schema_version == 0 &&
      ctx.backend.backend_plan_engine_id == 0 &&
      ctx.backend.backend_plan_flags == 0 &&
      ctx.backend.backend_plan_fingerprint == 0 &&
      ctx.backend.bearssl_state_ready == 0 &&
      ctx.backend.bearssl_engine_initialized == 0 &&
      ctx.backend.bearssl_state_schema_version == 0 &&
      ctx.backend.bearssl_state_engine_id == 0 &&
      ctx.backend.bearssl_state_flags == 0 &&
      ctx.backend.bearssl_state_fingerprint == 0 &&
      ctx.backend.bearssl_context_bytes == 0 &&
      ctx.backend.bearssl_io_buffer_bytes == 0 &&
      ctx.backend.bearssl_adapter_ready == 0 &&
      ctx.backend.bearssl_adapter_initialized == 0 &&
      ctx.backend.bearssl_adapter_schema_version == 0 &&
      ctx.backend.bearssl_adapter_engine_id == 0 &&
      ctx.backend.bearssl_adapter_flags == 0 &&
      ctx.backend.bearssl_adapter_fingerprint == 0 &&
      ctx.backend.custom_anchor_len == 0 &&
      ctx.backend.sni[0] == '\0') PASS();
  else FAIL("rejected context retained stale data");
}

static void test_tls_context_slot_acquire_release(void) {
  struct capy_tls_context *ctx = capy_tls_context_acquire();
  struct capy_tls_context *busy = capy_tls_context_acquire();
  TEST("capy_tls_context_acquire exposes one managed slot");
  if (ctx && !busy && ctx->socket_fd == -1 &&
      ctx->hostname[0] == '\0' &&
      ctx->config.verify_peer == 1 &&
      ctx->config.timeout_ms == CAPY_TLS_TIMEOUT_DEFAULT_MS &&
      ctx->backend.context_ready == 0 &&
      ctx->backend.trust_anchors_ready == 0 &&
      ctx->backend.custom_anchor_ready == 0 &&
      ctx->backend.trust_anchor_count == 0 &&
      ctx->backend.trust_anchor_rsa_count == 0 &&
      ctx->backend.trust_anchor_ec_count == 0 &&
      ctx->backend.trust_anchor_key_type_mask == 0 &&
      ctx->backend.trust_catalog_fingerprint == 0 &&
      ctx->backend.trust_anchor_slot_count == 0 &&
      ctx->backend.trust_slot_layout_fingerprint == 0 &&
      ctx->backend.trust_anchor_descriptor_count == 0 &&
      ctx->backend.trust_descriptor_fingerprint == 0 &&
      ctx->backend.trust_anchor_bundle_entry_count == 0 &&
      ctx->backend.trust_anchor_bundle_fingerprint == 0 &&
      ctx->backend.trust_material_summary_fingerprint == 0 &&
      ctx->backend.trust_subject_dn_total_bytes == 0 &&
      ctx->backend.trust_key_material_total_bytes == 0 &&
      ctx->backend.trust_subject_dn_max_bytes == 0 &&
      ctx->backend.trust_key_material_max_bytes == 0 &&
      ctx->backend.trust_manifest_schema_version == 0 &&
      ctx->backend.trust_manifest_source_id == 0 &&
      ctx->backend.trust_manifest_flags == 0 &&
      ctx->backend.trust_manifest_fingerprint == 0 &&
      ctx->backend.backend_plan_ready == 0 &&
      ctx->backend.handshake_allowed == 0 &&
      ctx->backend.backend_plan_schema_version == 0 &&
      ctx->backend.backend_plan_engine_id == 0 &&
      ctx->backend.backend_plan_flags == 0 &&
      ctx->backend.backend_plan_fingerprint == 0 &&
      ctx->backend.bearssl_state_ready == 0 &&
      ctx->backend.bearssl_engine_initialized == 0 &&
      ctx->backend.bearssl_state_schema_version == 0 &&
      ctx->backend.bearssl_state_engine_id == 0 &&
      ctx->backend.bearssl_state_flags == 0 &&
      ctx->backend.bearssl_state_fingerprint == 0 &&
      ctx->backend.bearssl_context_bytes == 0 &&
      ctx->backend.bearssl_io_buffer_bytes == 0 &&
      ctx->backend.bearssl_adapter_ready == 0 &&
      ctx->backend.bearssl_adapter_initialized == 0 &&
      ctx->backend.bearssl_adapter_schema_version == 0 &&
      ctx->backend.bearssl_adapter_engine_id == 0 &&
      ctx->backend.bearssl_adapter_flags == 0 &&
      ctx->backend.bearssl_adapter_fingerprint == 0 &&
      ctx->backend.custom_anchor_len == 0 &&
      ctx->backend.sni[0] == '\0') {
    capy_tls_context_release(ctx);
    ctx = capy_tls_context_acquire();
    if (ctx && ctx->socket_fd == -1 && ctx->hostname[0] == '\0' &&
        ctx->backend.context_ready == 0 &&
        ctx->backend.trust_anchors_ready == 0 &&
        ctx->backend.custom_anchor_ready == 0 &&
        ctx->backend.trust_anchor_count == 0 &&
        ctx->backend.trust_anchor_rsa_count == 0 &&
        ctx->backend.trust_anchor_ec_count == 0 &&
        ctx->backend.trust_anchor_key_type_mask == 0 &&
        ctx->backend.trust_catalog_fingerprint == 0 &&
        ctx->backend.trust_anchor_slot_count == 0 &&
        ctx->backend.trust_slot_layout_fingerprint == 0 &&
        ctx->backend.trust_anchor_descriptor_count == 0 &&
        ctx->backend.trust_descriptor_fingerprint == 0 &&
        ctx->backend.trust_anchor_bundle_entry_count == 0 &&
        ctx->backend.trust_anchor_bundle_fingerprint == 0 &&
        ctx->backend.trust_material_summary_fingerprint == 0 &&
        ctx->backend.trust_subject_dn_total_bytes == 0 &&
        ctx->backend.trust_key_material_total_bytes == 0 &&
        ctx->backend.trust_subject_dn_max_bytes == 0 &&
        ctx->backend.trust_key_material_max_bytes == 0 &&
        ctx->backend.trust_manifest_schema_version == 0 &&
        ctx->backend.trust_manifest_source_id == 0 &&
        ctx->backend.trust_manifest_flags == 0 &&
        ctx->backend.trust_manifest_fingerprint == 0 &&
        ctx->backend.backend_plan_ready == 0 &&
        ctx->backend.handshake_allowed == 0 &&
        ctx->backend.backend_plan_schema_version == 0 &&
        ctx->backend.backend_plan_engine_id == 0 &&
        ctx->backend.backend_plan_flags == 0 &&
        ctx->backend.backend_plan_fingerprint == 0 &&
        ctx->backend.bearssl_state_ready == 0 &&
        ctx->backend.bearssl_engine_initialized == 0 &&
        ctx->backend.bearssl_state_schema_version == 0 &&
        ctx->backend.bearssl_state_engine_id == 0 &&
        ctx->backend.bearssl_state_flags == 0 &&
        ctx->backend.bearssl_state_fingerprint == 0 &&
        ctx->backend.bearssl_context_bytes == 0 &&
        ctx->backend.bearssl_io_buffer_bytes == 0 &&
        ctx->backend.bearssl_adapter_ready == 0 &&
        ctx->backend.bearssl_adapter_initialized == 0 &&
        ctx->backend.bearssl_adapter_schema_version == 0 &&
        ctx->backend.bearssl_adapter_engine_id == 0 &&
        ctx->backend.bearssl_adapter_flags == 0 &&
        ctx->backend.bearssl_adapter_fingerprint == 0 &&
        ctx->backend.custom_anchor_len == 0 &&
        ctx->backend.sni[0] == '\0') {
      capy_tls_context_release(ctx);
      PASS();
    } else FAIL("released slot was not reusable");
  } else FAIL("managed slot contract failed");
}

static void test_tls_free_releases_managed_slot(void) {
  uint8_t cert = 0;
  struct capy_tls_context *ctx = capy_tls_context_acquire();
  struct capy_tls_context *again;
  struct capy_tls_effective_config effective = {
      1, &cert, sizeof(cert), 1000
  };
  TEST("capy_tls_free releases managed context slot");
  if (ctx && capy_tls_context_prepare(ctx, 7, "example.com", &effective)) {
    capy_tls_free(ctx);
    again = capy_tls_context_acquire();
    if (again == ctx && again->socket_fd == -1 &&
        again->hostname[0] == '\0' &&
        again->config.verify_peer == 1 &&
        again->config.ca_cert == 0 &&
        again->config.timeout_ms == CAPY_TLS_TIMEOUT_DEFAULT_MS &&
        again->backend.context_ready == 0 &&
        again->backend.trust_anchors_ready == 0 &&
        again->backend.custom_anchor_ready == 0 &&
        again->backend.trust_anchor_count == 0 &&
        again->backend.trust_anchor_rsa_count == 0 &&
        again->backend.trust_anchor_ec_count == 0 &&
        again->backend.trust_anchor_key_type_mask == 0 &&
        again->backend.trust_catalog_fingerprint == 0 &&
        again->backend.trust_anchor_slot_count == 0 &&
        again->backend.trust_slot_layout_fingerprint == 0 &&
        again->backend.trust_anchor_descriptor_count == 0 &&
        again->backend.trust_descriptor_fingerprint == 0 &&
        again->backend.trust_anchor_bundle_entry_count == 0 &&
        again->backend.trust_anchor_bundle_fingerprint == 0 &&
        again->backend.trust_material_summary_fingerprint == 0 &&
        again->backend.trust_subject_dn_total_bytes == 0 &&
        again->backend.trust_key_material_total_bytes == 0 &&
        again->backend.trust_subject_dn_max_bytes == 0 &&
        again->backend.trust_key_material_max_bytes == 0 &&
        again->backend.trust_manifest_schema_version == 0 &&
        again->backend.trust_manifest_source_id == 0 &&
        again->backend.trust_manifest_flags == 0 &&
        again->backend.trust_manifest_fingerprint == 0 &&
        again->backend.backend_plan_ready == 0 &&
        again->backend.handshake_allowed == 0 &&
        again->backend.backend_plan_schema_version == 0 &&
        again->backend.backend_plan_engine_id == 0 &&
        again->backend.backend_plan_flags == 0 &&
        again->backend.backend_plan_fingerprint == 0 &&
        again->backend.bearssl_state_ready == 0 &&
        again->backend.bearssl_engine_initialized == 0 &&
        again->backend.bearssl_state_schema_version == 0 &&
        again->backend.bearssl_state_engine_id == 0 &&
        again->backend.bearssl_state_flags == 0 &&
        again->backend.bearssl_state_fingerprint == 0 &&
        again->backend.bearssl_context_bytes == 0 &&
        again->backend.bearssl_io_buffer_bytes == 0 &&
        again->backend.bearssl_adapter_ready == 0 &&
        again->backend.bearssl_adapter_initialized == 0 &&
        again->backend.bearssl_adapter_schema_version == 0 &&
        again->backend.bearssl_adapter_engine_id == 0 &&
        again->backend.bearssl_adapter_flags == 0 &&
        again->backend.bearssl_adapter_fingerprint == 0 &&
        again->backend.custom_anchor_len == 0 &&
        again->backend.sni[0] == '\0') {
      capy_tls_context_release(again);
      PASS();
    } else FAIL("free did not release scrubbed slot");
  } else FAIL("managed slot setup failed");
}

static void test_tls_default_trust_source_reports_bundle(void) {
  const struct capy_tls_trust_anchor_catalog *catalog;
  const struct capy_tls_trust_anchor_bundle *bundle;
  const struct capy_tls_trust_store_manifest *manifest;
  catalog = capy_tls_default_trust_anchor_catalog();
  bundle = capy_tls_default_trust_anchor_bundle();
  manifest = capy_tls_default_trust_store_manifest();
  TEST("capy_tls default trust source exposes metadata");
  if (catalog && bundle && manifest && capy_tls_default_trust_anchors_available() &&
      capy_tls_default_trust_anchor_count() ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT &&
      catalog->anchor_count == CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT &&
      catalog->custom_anchor_slots == CAPY_TLS_CUSTOM_TRUST_ANCHOR_SLOT_COUNT &&
      catalog->key_type_mask ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_KEY_TYPE_MASK &&
      catalog->fingerprint ==
          CAPY_TLS_DEFAULT_TRUST_CATALOG_FINGERPRINT &&
      catalog->slot_layout_fingerprint ==
          CAPY_TLS_DEFAULT_TRUST_SLOT_LAYOUT_FINGERPRINT &&
      catalog->descriptor_fingerprint ==
          CAPY_TLS_DEFAULT_TRUST_DESCRIPTOR_FINGERPRINT &&
      catalog->anchor_bundle_fingerprint ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FINGERPRINT &&
      bundle->fingerprint ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FINGERPRINT &&
      manifest->anchor_bundle_fingerprint ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FINGERPRINT &&
      manifest->manifest_fingerprint ==
          CAPY_TLS_DEFAULT_TRUST_MANIFEST_FINGERPRINT &&
      catalog->metadata_only == 1 && bundle->metadata_only == 1 &&
      manifest->metadata_only == 1 &&
      CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT == 146u) PASS();
  else FAIL("default trust source metadata missing");
}

static void test_tls_default_trust_catalog_key_distribution(void) {
  const struct capy_tls_trust_anchor_catalog *catalog;
  catalog = capy_tls_default_trust_anchor_catalog();
  TEST("capy_tls default trust catalog records key distribution");
  if (catalog &&
      catalog->rsa_anchor_count == CAPY_TLS_DEFAULT_TRUST_ANCHOR_RSA_COUNT &&
      catalog->ec_anchor_count == CAPY_TLS_DEFAULT_TRUST_ANCHOR_EC_COUNT &&
      catalog->rsa_anchor_count + catalog->ec_anchor_count ==
          catalog->anchor_count &&
      capy_tls_default_trust_anchor_rsa_count() ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_RSA_COUNT &&
      capy_tls_default_trust_anchor_ec_count() ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_EC_COUNT &&
      capy_tls_default_trust_anchor_key_type_mask() ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_KEY_TYPE_MASK &&
      capy_tls_default_trust_catalog_fingerprint() ==
          CAPY_TLS_DEFAULT_TRUST_CATALOG_FINGERPRINT &&
      capy_tls_default_trust_anchor_slot_count() ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT &&
      capy_tls_default_trust_anchor_slot_layout_fingerprint() ==
          CAPY_TLS_DEFAULT_TRUST_SLOT_LAYOUT_FINGERPRINT &&
      capy_tls_default_trust_anchor_descriptor_count() ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT &&
      capy_tls_default_trust_anchor_descriptor_flags() ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_DESCRIPTOR_FLAGS &&
      capy_tls_default_trust_anchor_descriptor_fingerprint() ==
          CAPY_TLS_DEFAULT_TRUST_DESCRIPTOR_FINGERPRINT &&
      capy_tls_default_trust_anchor_bundle_entry_count() ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT &&
      capy_tls_default_trust_anchor_bundle_fingerprint() ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FINGERPRINT &&
      capy_tls_default_trust_anchor_bundle_flags() ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FLAGS &&
      capy_tls_default_trust_material_summary_fingerprint() ==
          CAPY_TLS_DEFAULT_TRUST_MATERIAL_SUMMARY_FINGERPRINT &&
      capy_tls_default_trust_subject_dn_total_bytes() ==
          CAPY_TLS_DEFAULT_TRUST_SUBJECT_DN_TOTAL_BYTES &&
      capy_tls_default_trust_key_material_total_bytes() ==
          CAPY_TLS_DEFAULT_TRUST_KEY_MATERIAL_TOTAL_BYTES &&
      capy_tls_default_trust_manifest_schema_version() ==
          CAPY_TLS_DEFAULT_TRUST_MANIFEST_SCHEMA_VERSION &&
      capy_tls_default_trust_manifest_source_id() ==
          CAPY_TLS_DEFAULT_TRUST_MANIFEST_SOURCE_KERNEL_BUNDLE &&
      capy_tls_default_trust_manifest_flags() ==
          CAPY_TLS_DEFAULT_TRUST_MANIFEST_FLAGS &&
      capy_tls_default_trust_manifest_fingerprint() ==
          CAPY_TLS_DEFAULT_TRUST_MANIFEST_FINGERPRINT &&
      capy_tls_default_trust_catalog_consistent() &&
      capy_tls_default_trust_anchor_slots_consistent() &&
      capy_tls_default_trust_anchor_descriptors_consistent() &&
      capy_tls_default_trust_anchor_bundle_consistent() &&
      capy_tls_default_trust_material_summary_consistent() &&
      capy_tls_default_trust_store_manifest_consistent() &&
      CAPY_TLS_DEFAULT_TRUST_ANCHOR_RSA_COUNT == 106u &&
      CAPY_TLS_DEFAULT_TRUST_ANCHOR_EC_COUNT == 40u) PASS();
  else FAIL("default trust catalog key distribution changed");
}


static void test_tls_default_trust_catalog_identity_is_stable(void) {
  const struct capy_tls_trust_anchor_catalog *catalog;
  catalog = capy_tls_default_trust_anchor_catalog();
  TEST("capy_tls default trust catalog identity is stable");
  if (catalog &&
      catalog->key_type_mask ==
          (CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK |
           CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK) &&
      catalog->fingerprint == 0xDB22D94Au &&
      CAPY_TLS_DEFAULT_TRUST_CATALOG_FINGERPRINT == 0xDB22D94Au) PASS();
  else FAIL("default trust catalog identity changed");
}


static void test_tls_default_trust_slot_table_reports_layout(void) {
  const struct capy_tls_trust_anchor_slot *slots;
  slots = capy_tls_default_trust_anchor_slots();
  TEST("capy_tls default trust slot table reports layout");
  if (slots &&
      capy_tls_default_trust_anchor_slot_count() ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT &&
      slots[0].index == 0 &&
      slots[0].key_type == CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK &&
      slots[0].metadata_only == 1 &&
      slots[1].index == 1 &&
      slots[1].key_type == CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK &&
      slots[2].index == 2 &&
      slots[2].key_type == CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK &&
      capy_tls_default_trust_anchor_slot_key_type(0) ==
          CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK &&
      capy_tls_default_trust_anchor_slot_key_type(2) ==
          CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK &&
      capy_tls_default_trust_anchor_slot_key_type(
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT) == 0 &&
      capy_tls_default_trust_anchor_slot_layout_fingerprint() ==
          CAPY_TLS_DEFAULT_TRUST_SLOT_LAYOUT_FINGERPRINT &&
      CAPY_TLS_DEFAULT_TRUST_SLOT_LAYOUT_FINGERPRINT == 0x07A622ABu) PASS();
  else FAIL("default trust slot layout changed");
}

static void test_tls_default_trust_slot_table_matches_catalog(void) {
  const struct capy_tls_trust_anchor_slot *slots;
  uint32_t rsa_count = 0;
  uint32_t ec_count = 0;
  uint32_t key_type_mask = 0;
  uint32_t i;
  int ok = 1;
  slots = capy_tls_default_trust_anchor_slots();
  TEST("capy_tls default trust slot table matches catalog");
  if (!slots) ok = 0;
  for (i = 0; ok && i < CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT; i++) {
    if (slots[i].index != i || slots[i].metadata_only != 1) ok = 0;
    else if (slots[i].key_type == CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK) {
      rsa_count++;
      key_type_mask |= slots[i].key_type;
    } else if (slots[i].key_type == CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK) {
      ec_count++;
      key_type_mask |= slots[i].key_type;
    } else ok = 0;
  }
  if (ok && rsa_count == CAPY_TLS_DEFAULT_TRUST_ANCHOR_RSA_COUNT &&
      ec_count == CAPY_TLS_DEFAULT_TRUST_ANCHOR_EC_COUNT &&
      key_type_mask == CAPY_TLS_DEFAULT_TRUST_ANCHOR_KEY_TYPE_MASK &&
      capy_tls_default_trust_anchor_slots_consistent()) PASS();
  else FAIL("default trust slot table did not match catalog");
}


static void test_tls_default_trust_descriptor_reports_metadata(void) {
  struct capy_tls_trust_anchor_descriptor descriptor;
  TEST("capy_tls default trust descriptor reports metadata");
  memset(&descriptor, 0xA5, sizeof(descriptor));
  if (capy_tls_default_trust_anchor_descriptor(0, &descriptor) &&
      descriptor.index == 0 &&
      descriptor.key_type == CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK &&
      descriptor.metadata_only == 1 &&
      descriptor.flags == CAPY_TLS_DEFAULT_TRUST_ANCHOR_DESCRIPTOR_FLAGS &&
      (descriptor.flags &
       CAPY_TLS_TRUST_ANCHOR_DESCRIPTOR_FLAG_METADATA_ONLY) &&
      (descriptor.flags &
       CAPY_TLS_TRUST_ANCHOR_DESCRIPTOR_FLAG_SLOT_BACKED) &&
      (descriptor.flags &
       CAPY_TLS_TRUST_ANCHOR_DESCRIPTOR_FLAG_KEY_TYPE_KNOWN) &&
      (descriptor.flags &
       CAPY_TLS_TRUST_ANCHOR_DESCRIPTOR_FLAG_CERT_BYTES_ABSENT) &&
      capy_tls_default_trust_anchor_descriptor_count() ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT &&
      capy_tls_default_trust_anchor_descriptor_fingerprint() ==
          CAPY_TLS_DEFAULT_TRUST_DESCRIPTOR_FINGERPRINT &&
      CAPY_TLS_DEFAULT_TRUST_DESCRIPTOR_FINGERPRINT == 0xE1A18A70u) PASS();
  else FAIL("default trust descriptor metadata changed");
}

static void test_tls_default_trust_descriptors_match_slots(void) {
  struct capy_tls_trust_anchor_descriptor descriptor;
  const struct capy_tls_trust_anchor_slot *slots;
  uint32_t i;
  int ok = 1;
  slots = capy_tls_default_trust_anchor_slots();
  TEST("capy_tls default trust descriptors match slots");
  if (!slots) ok = 0;
  for (i = 0; ok && i < CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT; i++) {
    if (!capy_tls_default_trust_anchor_descriptor(i, &descriptor)) ok = 0;
    else if (descriptor.index != slots[i].index ||
             descriptor.key_type != slots[i].key_type ||
             descriptor.metadata_only != slots[i].metadata_only ||
             descriptor.flags !=
                 CAPY_TLS_DEFAULT_TRUST_ANCHOR_DESCRIPTOR_FLAGS) ok = 0;
  }
  memset(&descriptor, 0xA5, sizeof(descriptor));
  if (ok && !capy_tls_default_trust_anchor_descriptor(
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT, &descriptor) &&
      descriptor.index == 0 && descriptor.key_type == 0 &&
      descriptor.metadata_only == 0 && descriptor.flags == 0 &&
      capy_tls_default_trust_anchor_descriptors_consistent()) PASS();
  else FAIL("default trust descriptors diverged from slots");
}



static void test_tls_default_trust_anchor_bundle_reports_material(void) {
  const struct capy_tls_trust_anchor_bundle_entry *entries;
  const struct capy_tls_trust_anchor_bundle *bundle;
  struct capy_tls_trust_anchor_bundle_entry entry;
  int ok;
  entries = capy_tls_default_trust_anchor_bundle_entries();
  bundle = capy_tls_default_trust_anchor_bundle();
  memset(&entry, 0xA5, sizeof(entry));
  TEST("capy_tls default trust bundle reports material metadata");
  ok = entries && bundle &&
       capy_tls_default_trust_anchor_bundle_entry(0, &entry) &&
       entry.index == 0 &&
       entry.key_type == CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK &&
       entry.metadata_only == 1 &&
       entry.subject_dn_bytes == 68u &&
       entry.key_material_bytes == 515u &&
       entry.flags == CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_ENTRY_FLAGS &&
       entries[120].subject_dn_bytes ==
           CAPY_TLS_DEFAULT_TRUST_SUBJECT_DN_MAX_BYTES &&
       entries[0].key_material_bytes ==
           CAPY_TLS_DEFAULT_TRUST_KEY_MATERIAL_MAX_BYTES &&
       bundle->entry_count == CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT &&
       bundle->subject_dn_total_bytes ==
           CAPY_TLS_DEFAULT_TRUST_SUBJECT_DN_TOTAL_BYTES &&
       bundle->key_material_total_bytes ==
           CAPY_TLS_DEFAULT_TRUST_KEY_MATERIAL_TOTAL_BYTES &&
       bundle->subject_dn_max_bytes ==
           CAPY_TLS_DEFAULT_TRUST_SUBJECT_DN_MAX_BYTES &&
       bundle->key_material_max_bytes ==
           CAPY_TLS_DEFAULT_TRUST_KEY_MATERIAL_MAX_BYTES &&
       bundle->fingerprint ==
           CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FINGERPRINT &&
       bundle->flags == CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FLAGS &&
       (bundle->flags & CAPY_TLS_TRUST_ANCHOR_BUNDLE_FLAG_METADATA_ONLY) &&
       (bundle->flags &
        CAPY_TLS_TRUST_ANCHOR_BUNDLE_FLAG_KERNEL_BUNDLE_DERIVED) &&
       (bundle->flags & CAPY_TLS_TRUST_ANCHOR_BUNDLE_FLAG_CERT_BYTES_ABSENT) &&
       (bundle->flags & CAPY_TLS_TRUST_ANCHOR_BUNDLE_FLAG_FAIL_CLOSED_ONLY) &&
       capy_tls_default_trust_anchor_bundle_fingerprint() ==
           CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FINGERPRINT &&
       capy_tls_default_trust_anchor_bundle_flags() ==
           CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FLAGS &&
       capy_tls_default_trust_anchor_bundle_consistent() &&
       CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FINGERPRINT == 0x0ED4E969u;
  memset(&entry, 0xA5, sizeof(entry));
  ok = ok && !capy_tls_default_trust_anchor_bundle_entry(
      CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT, &entry) &&
      entry.index == 0 && entry.key_type == 0 && entry.metadata_only == 0 &&
      entry.subject_dn_bytes == 0 && entry.key_material_bytes == 0 &&
      entry.flags == 0;
  if (ok) PASS();
  else FAIL("default trust bundle material metadata changed");
}

static void test_tls_default_trust_material_summary_reports_aggregate(void) {
  const struct capy_tls_trust_material_summary *summary;
  summary = capy_tls_default_trust_material_summary();
  TEST("capy_tls default trust material summary reports aggregate sizes");
  if (summary && summary->anchor_count == CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT &&
      summary->subject_dn_total_bytes == CAPY_TLS_DEFAULT_TRUST_SUBJECT_DN_TOTAL_BYTES &&
      summary->key_material_total_bytes == CAPY_TLS_DEFAULT_TRUST_KEY_MATERIAL_TOTAL_BYTES &&
      summary->subject_dn_max_bytes == CAPY_TLS_DEFAULT_TRUST_SUBJECT_DN_MAX_BYTES &&
      summary->key_material_max_bytes == CAPY_TLS_DEFAULT_TRUST_KEY_MATERIAL_MAX_BYTES &&
      summary->fingerprint == CAPY_TLS_DEFAULT_TRUST_MATERIAL_SUMMARY_FINGERPRINT &&
      summary->flags == CAPY_TLS_DEFAULT_TRUST_MATERIAL_SUMMARY_FLAGS &&
      summary->metadata_only == 1 &&
      capy_tls_default_trust_material_summary_flags() ==
          CAPY_TLS_DEFAULT_TRUST_MATERIAL_SUMMARY_FLAGS &&
      capy_tls_default_trust_material_summary_consistent() &&
      CAPY_TLS_DEFAULT_TRUST_MATERIAL_SUMMARY_FINGERPRINT == 0x8DFC9FAFu) PASS();
  else FAIL("default trust material summary changed");
}

static void test_tls_default_trust_manifest_reports_provenance(void) {
  const struct capy_tls_trust_store_manifest *manifest;
  manifest = capy_tls_default_trust_store_manifest();
  TEST("capy_tls default trust manifest reports provenance");
  if (manifest &&
      manifest->schema_version ==
          CAPY_TLS_DEFAULT_TRUST_MANIFEST_SCHEMA_VERSION &&
      manifest->source_id ==
          CAPY_TLS_DEFAULT_TRUST_MANIFEST_SOURCE_KERNEL_BUNDLE &&
      manifest->flags == CAPY_TLS_DEFAULT_TRUST_MANIFEST_FLAGS &&
      (manifest->flags & CAPY_TLS_TRUST_MANIFEST_FLAG_METADATA_ONLY) &&
      (manifest->flags &
       CAPY_TLS_TRUST_MANIFEST_FLAG_KERNEL_BUNDLE_DERIVED) &&
      (manifest->flags & CAPY_TLS_TRUST_MANIFEST_FLAG_CERT_BYTES_ABSENT) &&
      (manifest->flags & CAPY_TLS_TRUST_MANIFEST_FLAG_FAIL_CLOSED_ONLY) &&
      manifest->manifest_fingerprint ==
          CAPY_TLS_DEFAULT_TRUST_MANIFEST_FINGERPRINT &&
      capy_tls_default_trust_manifest_fingerprint() ==
          CAPY_TLS_DEFAULT_TRUST_MANIFEST_FINGERPRINT &&
      CAPY_TLS_DEFAULT_TRUST_MANIFEST_FINGERPRINT == 0xBD2653A4u) PASS();
  else FAIL("default trust manifest provenance changed");
}

static void test_tls_default_trust_manifest_matches_catalog(void) {
  const struct capy_tls_trust_anchor_catalog *catalog;
  const struct capy_tls_trust_store_manifest *manifest;
  catalog = capy_tls_default_trust_anchor_catalog();
  manifest = capy_tls_default_trust_store_manifest();
  TEST("capy_tls default trust manifest matches catalog");
  if (catalog && manifest &&
      manifest->anchor_count == catalog->anchor_count &&
      manifest->rsa_anchor_count == catalog->rsa_anchor_count &&
      manifest->ec_anchor_count == catalog->ec_anchor_count &&
      manifest->custom_anchor_slots == catalog->custom_anchor_slots &&
      manifest->key_type_mask == catalog->key_type_mask &&
      manifest->catalog_fingerprint == catalog->fingerprint &&
      manifest->slot_layout_fingerprint ==
          catalog->slot_layout_fingerprint &&
      manifest->descriptor_fingerprint == catalog->descriptor_fingerprint &&
      manifest->anchor_bundle_fingerprint ==
          catalog->anchor_bundle_fingerprint &&
      manifest->anchor_bundle_fingerprint ==
          capy_tls_default_trust_anchor_bundle_fingerprint() &&
      manifest->material_summary_fingerprint ==
          catalog->material_summary_fingerprint &&
      manifest->subject_dn_total_bytes ==
          capy_tls_default_trust_subject_dn_total_bytes() &&
      manifest->key_material_total_bytes ==
          capy_tls_default_trust_key_material_total_bytes() &&
      manifest->subject_dn_max_bytes ==
          capy_tls_default_trust_subject_dn_max_bytes() &&
      manifest->key_material_max_bytes ==
          capy_tls_default_trust_key_material_max_bytes() &&
      capy_tls_default_trust_store_manifest_consistent()) PASS();
  else FAIL("default trust manifest diverged from catalog");
}

static void test_tls_backend_plan_reports_fail_closed_contract(void) {
  const struct capy_tls_backend_plan *plan;
  plan = capy_tls_default_backend_plan();
  TEST("capy_tls backend plan reports fail-closed BearSSL contract");
  if (plan &&
      plan->schema_version == CAPY_TLS_BACKEND_PLAN_SCHEMA_VERSION &&
      plan->engine_id == CAPY_TLS_BACKEND_PLAN_ENGINE_BEARSSL_USERLAND &&
      plan->flags == CAPY_TLS_DEFAULT_BACKEND_PLAN_FLAGS &&
      plan->trust_manifest_schema_version ==
          CAPY_TLS_DEFAULT_TRUST_MANIFEST_SCHEMA_VERSION &&
      plan->trust_manifest_fingerprint ==
          CAPY_TLS_DEFAULT_TRUST_MANIFEST_FINGERPRINT &&
      plan->trust_anchor_bundle_fingerprint ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FINGERPRINT &&
      plan->fingerprint == CAPY_TLS_DEFAULT_BACKEND_PLAN_FINGERPRINT &&
      plan->handshake_allowed == 0 &&
      (plan->flags & CAPY_TLS_BACKEND_PLAN_FLAG_FAIL_CLOSED_ONLY) &&
      (plan->flags & CAPY_TLS_BACKEND_PLAN_FLAG_HANDSHAKE_DISABLED) &&
      (plan->flags & CAPY_TLS_BACKEND_PLAN_FLAG_TRUST_METADATA_GATED) &&
      (plan->flags & CAPY_TLS_BACKEND_PLAN_FLAG_BEARSSL_STATE_ABSENT) &&
      capy_tls_default_backend_plan_schema_version() ==
          CAPY_TLS_BACKEND_PLAN_SCHEMA_VERSION &&
      capy_tls_default_backend_plan_engine_id() ==
          CAPY_TLS_BACKEND_PLAN_ENGINE_BEARSSL_USERLAND &&
      capy_tls_default_backend_plan_flags() ==
          CAPY_TLS_DEFAULT_BACKEND_PLAN_FLAGS &&
      capy_tls_default_backend_plan_fingerprint() ==
          CAPY_TLS_DEFAULT_BACKEND_PLAN_FINGERPRINT &&
      capy_tls_default_backend_plan_consistent() &&
      CAPY_TLS_DEFAULT_BACKEND_PLAN_FINGERPRINT == 0x4F809D54u) PASS();
  else FAIL("backend plan contract changed");
}

static void test_tls_bearssl_reserved_state_reports_absent_engine(void) {
  const struct capy_tls_bearssl_reserved_state *state;
  state = capy_tls_default_bearssl_reserved_state();
  TEST("capy_tls BearSSL reserved state reports absent engine");
  if (state &&
      state->schema_version == CAPY_TLS_BEARSSL_STATE_SCHEMA_VERSION &&
      state->engine_id == CAPY_TLS_BEARSSL_STATE_ENGINE_BEARSSL_USERLAND &&
      state->flags == CAPY_TLS_DEFAULT_BEARSSL_STATE_FLAGS &&
      state->backend_plan_fingerprint ==
          CAPY_TLS_DEFAULT_BACKEND_PLAN_FINGERPRINT &&
      state->trust_manifest_fingerprint ==
          CAPY_TLS_DEFAULT_TRUST_MANIFEST_FINGERPRINT &&
      state->trust_anchor_bundle_fingerprint ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FINGERPRINT &&
      state->reserved_context_bytes == CAPY_TLS_DEFAULT_BEARSSL_CONTEXT_BYTES &&
      state->reserved_io_bytes == CAPY_TLS_DEFAULT_BEARSSL_IO_BUFFER_BYTES &&
      state->fingerprint == CAPY_TLS_DEFAULT_BEARSSL_STATE_FINGERPRINT &&
      state->metadata_only == 1 &&
      state->engine_initialized == 0 &&
      state->handshake_allowed == 0 &&
      (state->flags & CAPY_TLS_BEARSSL_STATE_FLAG_METADATA_ONLY) &&
      (state->flags & CAPY_TLS_BEARSSL_STATE_FLAG_ENGINE_ABSENT) &&
      (state->flags & CAPY_TLS_BEARSSL_STATE_FLAG_CONTEXT_BYTES_ABSENT) &&
      (state->flags & CAPY_TLS_BEARSSL_STATE_FLAG_IO_BYTES_ABSENT) &&
      (state->flags & CAPY_TLS_BEARSSL_STATE_FLAG_HANDSHAKE_DISABLED) &&
      capy_tls_default_bearssl_state_schema_version() ==
          CAPY_TLS_BEARSSL_STATE_SCHEMA_VERSION &&
      capy_tls_default_bearssl_state_engine_id() ==
          CAPY_TLS_BEARSSL_STATE_ENGINE_BEARSSL_USERLAND &&
      capy_tls_default_bearssl_state_flags() ==
          CAPY_TLS_DEFAULT_BEARSSL_STATE_FLAGS &&
      capy_tls_default_bearssl_state_fingerprint() ==
          CAPY_TLS_DEFAULT_BEARSSL_STATE_FINGERPRINT &&
      capy_tls_default_bearssl_reserved_state_consistent() &&
      CAPY_TLS_DEFAULT_BEARSSL_STATE_FINGERPRINT == 0x7D1732D0u) PASS();
  else FAIL("BearSSL reserved state contract changed");
}

static void test_tls_bearssl_adapter_reports_metadata_only_contract(void) {
  const struct capy_tls_bearssl_adapter_contract *adapter;
  adapter = capy_tls_default_bearssl_adapter_contract();
  TEST("capy_tls BearSSL adapter reports metadata-only contract");
  if (adapter &&
      adapter->schema_version == CAPY_TLS_BEARSSL_ADAPTER_SCHEMA_VERSION &&
      adapter->engine_id == CAPY_TLS_BEARSSL_ADAPTER_ENGINE_BEARSSL_USERLAND &&
      adapter->flags == CAPY_TLS_DEFAULT_BEARSSL_ADAPTER_FLAGS &&
      adapter->backend_plan_fingerprint ==
          CAPY_TLS_DEFAULT_BACKEND_PLAN_FINGERPRINT &&
      adapter->reserved_state_fingerprint ==
          CAPY_TLS_DEFAULT_BEARSSL_STATE_FINGERPRINT &&
      adapter->trust_manifest_fingerprint ==
          CAPY_TLS_DEFAULT_TRUST_MANIFEST_FINGERPRINT &&
      adapter->trust_anchor_bundle_fingerprint ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FINGERPRINT &&
      adapter->fingerprint == CAPY_TLS_DEFAULT_BEARSSL_ADAPTER_FINGERPRINT &&
      adapter->metadata_only == 1 &&
      adapter->adapter_initialized == 0 &&
      adapter->handshake_allowed == 0 &&
      (adapter->flags & CAPY_TLS_BEARSSL_ADAPTER_FLAG_METADATA_ONLY) &&
      (adapter->flags & CAPY_TLS_BEARSSL_ADAPTER_FLAG_BACKEND_PLAN_GATED) &&
      (adapter->flags & CAPY_TLS_BEARSSL_ADAPTER_FLAG_RESERVED_STATE_GATED) &&
      (adapter->flags & CAPY_TLS_BEARSSL_ADAPTER_FLAG_ENGINE_INIT_DISABLED) &&
      (adapter->flags & CAPY_TLS_BEARSSL_ADAPTER_FLAG_HANDSHAKE_DISABLED) &&
      capy_tls_default_bearssl_adapter_schema_version() ==
          CAPY_TLS_BEARSSL_ADAPTER_SCHEMA_VERSION &&
      capy_tls_default_bearssl_adapter_engine_id() ==
          CAPY_TLS_BEARSSL_ADAPTER_ENGINE_BEARSSL_USERLAND &&
      capy_tls_default_bearssl_adapter_flags() ==
          CAPY_TLS_DEFAULT_BEARSSL_ADAPTER_FLAGS &&
      capy_tls_default_bearssl_adapter_fingerprint() ==
          CAPY_TLS_DEFAULT_BEARSSL_ADAPTER_FINGERPRINT &&
      capy_tls_default_bearssl_adapter_consistent() &&
      CAPY_TLS_DEFAULT_BEARSSL_ADAPTER_FINGERPRINT == 0xE73E3E65u) PASS();
  else FAIL("BearSSL adapter contract changed");
}

static void test_tls_backend_stub_reports_unsupported_for_ready_context(void) {
  uint8_t cert = 0;
  struct capy_tls_context ctx;
  struct capy_tls_effective_config effective = {
      1, &cert, sizeof(cert), 1000
  };
  TEST("capy_tls_backend_connect consumes ready context as unsupported");
  if (capy_tls_context_prepare(&ctx, 7, "example.com", &effective) &&
      capy_tls_backend_connect(&ctx) == CAPY_TLS_EUNSUPPORTED &&
      ctx.backend.context_ready == 1 && ctx.backend.sni_ready == 1 &&
      ctx.backend.timeout_ready == 1 &&
      ctx.backend.trust_anchors_ready == 1 &&
      ctx.backend.custom_anchor_ready == 1 &&
      ctx.backend.handshake_started == 0 &&
      ctx.backend.timeout_ms == 1000 &&
      ctx.backend.trust_anchor_count == 1 &&
      ctx.backend.trust_anchor_rsa_count == 0 &&
      ctx.backend.trust_anchor_ec_count == 0 &&
      ctx.backend.trust_anchor_key_type_mask == 0 &&
      ctx.backend.trust_catalog_fingerprint == 0 &&
      ctx.backend.trust_anchor_slot_count == 0 &&
      ctx.backend.trust_slot_layout_fingerprint == 0 &&
      ctx.backend.trust_anchor_descriptor_count == 0 &&
      ctx.backend.trust_descriptor_fingerprint == 0 &&
      ctx.backend.trust_anchor_bundle_entry_count == 0 &&
      ctx.backend.trust_anchor_bundle_fingerprint == 0 &&
      ctx.backend.trust_material_summary_fingerprint == 0 &&
      ctx.backend.trust_subject_dn_total_bytes == 0 &&
      ctx.backend.trust_key_material_total_bytes == 0 &&
      ctx.backend.trust_subject_dn_max_bytes == 0 &&
      ctx.backend.trust_key_material_max_bytes == 0 &&
      ctx.backend.trust_manifest_schema_version == 0 &&
      ctx.backend.trust_manifest_source_id == 0 &&
      ctx.backend.trust_manifest_flags == 0 &&
      ctx.backend.trust_manifest_fingerprint == 0 &&
      ctx.backend.backend_plan_ready == 0 &&
      ctx.backend.handshake_allowed == 0 &&
      ctx.backend.backend_plan_schema_version == 0 &&
      ctx.backend.backend_plan_engine_id == 0 &&
      ctx.backend.backend_plan_flags == 0 &&
      ctx.backend.backend_plan_fingerprint == 0 &&
      ctx.backend.bearssl_state_ready == 0 &&
      ctx.backend.bearssl_engine_initialized == 0 &&
      ctx.backend.bearssl_state_schema_version == 0 &&
      ctx.backend.bearssl_state_engine_id == 0 &&
      ctx.backend.bearssl_state_flags == 0 &&
      ctx.backend.bearssl_state_fingerprint == 0 &&
      ctx.backend.bearssl_context_bytes == 0 &&
      ctx.backend.bearssl_io_buffer_bytes == 0 &&
      ctx.backend.bearssl_adapter_ready == 0 &&
      ctx.backend.bearssl_adapter_initialized == 0 &&
      ctx.backend.bearssl_adapter_schema_version == 0 &&
      ctx.backend.bearssl_adapter_engine_id == 0 &&
      ctx.backend.bearssl_adapter_flags == 0 &&
      ctx.backend.bearssl_adapter_fingerprint == 0 &&
      ctx.backend.custom_anchor_len == sizeof(cert) &&
      strcmp(ctx.backend.sni, "example.com") == 0) PASS();
  else FAIL("ready backend context was not unsupported");
}

static void test_tls_backend_stub_prepares_default_trust_metadata(void) {
  struct capy_tls_context ctx;
  struct capy_tls_effective_config effective = {
      1, 0, 0, 1000
  };
  TEST("capy_tls_backend_connect records default trust metadata");
  if (capy_tls_context_prepare(&ctx, 7, "example.com", &effective) &&
      capy_tls_backend_connect(&ctx) == CAPY_TLS_EUNSUPPORTED &&
      ctx.backend.trust_anchors_ready == 1 &&
      ctx.backend.custom_anchor_ready == 0 &&
      ctx.backend.trust_anchor_count ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT &&
      ctx.backend.trust_anchor_rsa_count ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_RSA_COUNT &&
      ctx.backend.trust_anchor_ec_count ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_EC_COUNT &&
      ctx.backend.trust_anchor_key_type_mask ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_KEY_TYPE_MASK &&
      ctx.backend.trust_catalog_fingerprint ==
          CAPY_TLS_DEFAULT_TRUST_CATALOG_FINGERPRINT &&
      ctx.backend.trust_anchor_slot_count ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT &&
      ctx.backend.trust_slot_layout_fingerprint ==
          CAPY_TLS_DEFAULT_TRUST_SLOT_LAYOUT_FINGERPRINT &&
      ctx.backend.trust_anchor_descriptor_count ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT &&
      ctx.backend.trust_descriptor_fingerprint ==
          CAPY_TLS_DEFAULT_TRUST_DESCRIPTOR_FINGERPRINT &&
      ctx.backend.trust_anchor_bundle_entry_count ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT &&
      ctx.backend.trust_anchor_bundle_fingerprint ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FINGERPRINT &&
      ctx.backend.trust_material_summary_fingerprint ==
          CAPY_TLS_DEFAULT_TRUST_MATERIAL_SUMMARY_FINGERPRINT &&
      ctx.backend.trust_subject_dn_total_bytes ==
          CAPY_TLS_DEFAULT_TRUST_SUBJECT_DN_TOTAL_BYTES &&
      ctx.backend.trust_key_material_total_bytes ==
          CAPY_TLS_DEFAULT_TRUST_KEY_MATERIAL_TOTAL_BYTES &&
      ctx.backend.trust_subject_dn_max_bytes ==
          CAPY_TLS_DEFAULT_TRUST_SUBJECT_DN_MAX_BYTES &&
      ctx.backend.trust_key_material_max_bytes ==
          CAPY_TLS_DEFAULT_TRUST_KEY_MATERIAL_MAX_BYTES &&
      ctx.backend.trust_manifest_schema_version ==
          CAPY_TLS_DEFAULT_TRUST_MANIFEST_SCHEMA_VERSION &&
      ctx.backend.trust_manifest_source_id ==
          CAPY_TLS_DEFAULT_TRUST_MANIFEST_SOURCE_KERNEL_BUNDLE &&
      ctx.backend.trust_manifest_flags ==
          CAPY_TLS_DEFAULT_TRUST_MANIFEST_FLAGS &&
      ctx.backend.trust_manifest_fingerprint ==
          CAPY_TLS_DEFAULT_TRUST_MANIFEST_FINGERPRINT &&
      ctx.backend.backend_plan_ready == 1 &&
      ctx.backend.handshake_allowed == 0 &&
      ctx.backend.backend_plan_schema_version ==
          CAPY_TLS_BACKEND_PLAN_SCHEMA_VERSION &&
      ctx.backend.backend_plan_engine_id ==
          CAPY_TLS_BACKEND_PLAN_ENGINE_BEARSSL_USERLAND &&
      ctx.backend.backend_plan_flags ==
          CAPY_TLS_DEFAULT_BACKEND_PLAN_FLAGS &&
      ctx.backend.backend_plan_fingerprint ==
          CAPY_TLS_DEFAULT_BACKEND_PLAN_FINGERPRINT &&
      ctx.backend.bearssl_state_ready == 1 &&
      ctx.backend.bearssl_engine_initialized == 0 &&
      ctx.backend.bearssl_state_schema_version ==
          CAPY_TLS_BEARSSL_STATE_SCHEMA_VERSION &&
      ctx.backend.bearssl_state_engine_id ==
          CAPY_TLS_BEARSSL_STATE_ENGINE_BEARSSL_USERLAND &&
      ctx.backend.bearssl_state_flags ==
          CAPY_TLS_DEFAULT_BEARSSL_STATE_FLAGS &&
      ctx.backend.bearssl_state_fingerprint ==
          CAPY_TLS_DEFAULT_BEARSSL_STATE_FINGERPRINT &&
      ctx.backend.bearssl_context_bytes ==
          CAPY_TLS_DEFAULT_BEARSSL_CONTEXT_BYTES &&
      ctx.backend.bearssl_io_buffer_bytes ==
          CAPY_TLS_DEFAULT_BEARSSL_IO_BUFFER_BYTES &&
      ctx.backend.bearssl_adapter_ready == 1 &&
      ctx.backend.bearssl_adapter_initialized == 0 &&
      ctx.backend.bearssl_adapter_schema_version ==
          CAPY_TLS_BEARSSL_ADAPTER_SCHEMA_VERSION &&
      ctx.backend.bearssl_adapter_engine_id ==
          CAPY_TLS_BEARSSL_ADAPTER_ENGINE_BEARSSL_USERLAND &&
      ctx.backend.bearssl_adapter_flags ==
          CAPY_TLS_DEFAULT_BEARSSL_ADAPTER_FLAGS &&
      ctx.backend.bearssl_adapter_fingerprint ==
          CAPY_TLS_DEFAULT_BEARSSL_ADAPTER_FINGERPRINT &&
      ctx.backend.custom_anchor_len == 0 &&
      ctx.backend.handshake_started == 0) PASS();
  else FAIL("default trust metadata was not prepared");
}

static void test_tls_backend_stub_rejects_incomplete_context(void) {
  struct capy_tls_context ctx;
  struct capy_tls_effective_config effective = {
      1, 0, 0, CAPY_TLS_TIMEOUT_DEFAULT_MS
  };
  TEST("capy_tls_backend_connect rejects incomplete context");
  capy_tls_context_reset(&ctx);
  if (capy_tls_backend_connect(0) == CAPY_TLS_EINVAL &&
      capy_tls_backend_connect(&ctx) == CAPY_TLS_EINVAL &&
      capy_tls_context_prepare(&ctx, 7, "example.com", &effective)) {
    ctx.config.verify_peer = 0;
    if (capy_tls_backend_connect(&ctx) == CAPY_TLS_EINVAL &&
        ctx.backend.context_ready == 0 &&
        ctx.backend.trust_anchors_ready == 0 &&
        ctx.backend.custom_anchor_ready == 0 &&
        ctx.backend.trust_anchor_count == 0 &&
        ctx.backend.trust_anchor_rsa_count == 0 &&
        ctx.backend.trust_anchor_ec_count == 0 &&
        ctx.backend.trust_anchor_key_type_mask == 0 &&
        ctx.backend.trust_catalog_fingerprint == 0 &&
        ctx.backend.trust_anchor_slot_count == 0 &&
        ctx.backend.trust_slot_layout_fingerprint == 0 &&
        ctx.backend.trust_anchor_descriptor_count == 0 &&
        ctx.backend.trust_descriptor_fingerprint == 0 &&
        ctx.backend.trust_anchor_bundle_entry_count == 0 &&
        ctx.backend.trust_anchor_bundle_fingerprint == 0 &&
        ctx.backend.trust_material_summary_fingerprint == 0 &&
        ctx.backend.trust_subject_dn_total_bytes == 0 &&
        ctx.backend.trust_key_material_total_bytes == 0 &&
        ctx.backend.trust_subject_dn_max_bytes == 0 &&
        ctx.backend.trust_key_material_max_bytes == 0 &&
        ctx.backend.trust_manifest_schema_version == 0 &&
        ctx.backend.trust_manifest_source_id == 0 &&
        ctx.backend.trust_manifest_flags == 0 &&
        ctx.backend.trust_manifest_fingerprint == 0 &&
        ctx.backend.backend_plan_ready == 0 &&
        ctx.backend.handshake_allowed == 0 &&
        ctx.backend.backend_plan_schema_version == 0 &&
        ctx.backend.backend_plan_engine_id == 0 &&
        ctx.backend.backend_plan_flags == 0 &&
        ctx.backend.backend_plan_fingerprint == 0 &&
        ctx.backend.bearssl_state_ready == 0 &&
        ctx.backend.bearssl_engine_initialized == 0 &&
        ctx.backend.bearssl_state_schema_version == 0 &&
        ctx.backend.bearssl_state_engine_id == 0 &&
        ctx.backend.bearssl_state_flags == 0 &&
        ctx.backend.bearssl_state_fingerprint == 0 &&
        ctx.backend.bearssl_context_bytes == 0 &&
        ctx.backend.bearssl_io_buffer_bytes == 0 &&
        ctx.backend.bearssl_adapter_ready == 0 &&
        ctx.backend.bearssl_adapter_initialized == 0 &&
        ctx.backend.bearssl_adapter_schema_version == 0 &&
        ctx.backend.bearssl_adapter_engine_id == 0 &&
        ctx.backend.bearssl_adapter_flags == 0 &&
        ctx.backend.bearssl_adapter_fingerprint == 0 &&
        ctx.backend.custom_anchor_len == 0 &&
        ctx.backend.sni[0] == '\0') PASS();
    else FAIL("corrupt backend context accepted");
  } else FAIL("incomplete backend context accepted");
}

static void test_tls_backend_stub_resets_state_on_reject(void) {
  struct capy_tls_context ctx;
  struct capy_tls_effective_config effective = {
      1, 0, 0, 1000
  };
  TEST("capy_tls_backend_connect clears state on rejection");
  if (capy_tls_context_prepare(&ctx, 7, "example.com", &effective) &&
      capy_tls_backend_connect(&ctx) == CAPY_TLS_EUNSUPPORTED) {
    ctx.socket_fd = -1;
    if (capy_tls_backend_connect(&ctx) == CAPY_TLS_EINVAL &&
        ctx.backend.context_ready == 0 &&
        ctx.backend.sni_ready == 0 &&
        ctx.backend.timeout_ready == 0 &&
        ctx.backend.trust_anchors_ready == 0 &&
        ctx.backend.custom_anchor_ready == 0 &&
        ctx.backend.timeout_ms == 0 &&
        ctx.backend.trust_anchor_count == 0 &&
        ctx.backend.trust_anchor_rsa_count == 0 &&
        ctx.backend.trust_anchor_ec_count == 0 &&
        ctx.backend.trust_anchor_key_type_mask == 0 &&
        ctx.backend.trust_catalog_fingerprint == 0 &&
        ctx.backend.trust_anchor_slot_count == 0 &&
        ctx.backend.trust_slot_layout_fingerprint == 0 &&
        ctx.backend.trust_anchor_descriptor_count == 0 &&
        ctx.backend.trust_descriptor_fingerprint == 0 &&
        ctx.backend.trust_anchor_bundle_entry_count == 0 &&
        ctx.backend.trust_anchor_bundle_fingerprint == 0 &&
        ctx.backend.trust_material_summary_fingerprint == 0 &&
        ctx.backend.trust_subject_dn_total_bytes == 0 &&
        ctx.backend.trust_key_material_total_bytes == 0 &&
        ctx.backend.trust_subject_dn_max_bytes == 0 &&
        ctx.backend.trust_key_material_max_bytes == 0 &&
        ctx.backend.trust_manifest_schema_version == 0 &&
        ctx.backend.trust_manifest_source_id == 0 &&
        ctx.backend.trust_manifest_flags == 0 &&
        ctx.backend.trust_manifest_fingerprint == 0 &&
        ctx.backend.backend_plan_ready == 0 &&
        ctx.backend.handshake_allowed == 0 &&
        ctx.backend.backend_plan_schema_version == 0 &&
        ctx.backend.backend_plan_engine_id == 0 &&
        ctx.backend.backend_plan_flags == 0 &&
        ctx.backend.backend_plan_fingerprint == 0 &&
        ctx.backend.bearssl_state_ready == 0 &&
        ctx.backend.bearssl_engine_initialized == 0 &&
        ctx.backend.bearssl_state_schema_version == 0 &&
        ctx.backend.bearssl_state_engine_id == 0 &&
        ctx.backend.bearssl_state_flags == 0 &&
        ctx.backend.bearssl_state_fingerprint == 0 &&
        ctx.backend.bearssl_context_bytes == 0 &&
        ctx.backend.bearssl_io_buffer_bytes == 0 &&
        ctx.backend.bearssl_adapter_ready == 0 &&
        ctx.backend.bearssl_adapter_initialized == 0 &&
        ctx.backend.bearssl_adapter_schema_version == 0 &&
        ctx.backend.bearssl_adapter_engine_id == 0 &&
        ctx.backend.bearssl_adapter_flags == 0 &&
        ctx.backend.bearssl_adapter_fingerprint == 0 &&
        ctx.backend.custom_anchor_len == 0 &&
        ctx.backend.sni[0] == '\0') PASS();
    else FAIL("backend state survived rejection");
  } else FAIL("backend state setup failed");
}

static void test_tls_connect_rejects_invalid_args(void) {
  TEST("capy_tls_connect_tcp rejects invalid arguments");
  if (capy_tls_connect_tcp(-1, "example.com", 0) == 0 &&
      capy_tls_last_error() == CAPY_TLS_EINVAL &&
      capy_tls_connect_tcp(3, 0, 0) == 0 &&
      capy_tls_last_error() == CAPY_TLS_EINVAL &&
      capy_tls_connect_tcp(3, "", 0) == 0 &&
      capy_tls_last_error() == CAPY_TLS_EINVAL) PASS();
  else FAIL("invalid connect args not rejected");
}

static void test_tls_connect_rejects_malformed_hostnames(void) {
  char long_label[65];
  memset(long_label, 'a', sizeof(long_label));
  long_label[64] = '\0';
  TEST("capy_tls_connect_tcp rejects malformed hostnames");
  if (capy_tls_connect_tcp(3, "bad host", 0) == 0 &&
      capy_tls_last_error() == CAPY_TLS_EINVAL &&
      capy_tls_connect_tcp(3, "-example.com", 0) == 0 &&
      capy_tls_last_error() == CAPY_TLS_EINVAL &&
      capy_tls_connect_tcp(3, "example-.com", 0) == 0 &&
      capy_tls_last_error() == CAPY_TLS_EINVAL &&
      capy_tls_connect_tcp(3, "example..com", 0) == 0 &&
      capy_tls_last_error() == CAPY_TLS_EINVAL &&
      capy_tls_connect_tcp(3, "example.com.", 0) == 0 &&
      capy_tls_last_error() == CAPY_TLS_EINVAL &&
      capy_tls_connect_tcp(3, "exa_mple.com", 0) == 0 &&
      capy_tls_last_error() == CAPY_TLS_EINVAL &&
      capy_tls_connect_tcp(3, "exa\\mple.com", 0) == 0 &&
      capy_tls_last_error() == CAPY_TLS_EINVAL &&
      capy_tls_connect_tcp(3, "exa%mple.com", 0) == 0 &&
      capy_tls_last_error() == CAPY_TLS_EINVAL &&
      capy_tls_connect_tcp(3, "[::1]", 0) == 0 &&
      capy_tls_last_error() == CAPY_TLS_EINVAL &&
      capy_tls_connect_tcp(3, long_label, 0) == 0 &&
      capy_tls_last_error() == CAPY_TLS_EINVAL) PASS();
  else FAIL("malformed hostname accepted");
}

static void test_tls_connect_rejects_malformed_config(void) {
  uint8_t cert = 0;
  struct capy_tls_config cfg_a = { 1, &cert, 0, 1000 };
  struct capy_tls_config cfg_b = { 1, 0, 1, 1000 };
  struct capy_tls_config cfg_c = { 0, 0, 0, 1000 };
  struct capy_tls_config cfg_d = { -1, 0, 0, 1000 };
  struct capy_tls_config cfg_e = {
      1, 0, 0, CAPY_TLS_TIMEOUT_MIN_MS - 1u
  };
  struct capy_tls_config cfg_f = {
      1, 0, 0, CAPY_TLS_TIMEOUT_MAX_MS + 1u
  };
  TEST("capy_tls_connect_tcp rejects malformed TLS config");
  if (capy_tls_connect_tcp(3, "example.com", &cfg_a) == 0 &&
      capy_tls_last_error() == CAPY_TLS_EINVAL &&
      capy_tls_connect_tcp(3, "example.com", &cfg_b) == 0 &&
      capy_tls_last_error() == CAPY_TLS_EINVAL &&
      capy_tls_connect_tcp(3, "example.com", &cfg_c) == 0 &&
      capy_tls_last_error() == CAPY_TLS_EINVAL &&
      capy_tls_connect_tcp(3, "example.com", &cfg_d) == 0 &&
      capy_tls_last_error() == CAPY_TLS_EINVAL &&
      capy_tls_connect_tcp(3, "example.com", &cfg_e) == 0 &&
      capy_tls_last_error() == CAPY_TLS_EINVAL &&
      capy_tls_connect_tcp(3, "example.com", &cfg_f) == 0 &&
      capy_tls_last_error() == CAPY_TLS_EINVAL) PASS();
  else FAIL("malformed config accepted");
}

static void test_tls_connect_releases_managed_slot_on_unsupported(void) {
  struct capy_tls_context *ctx;
  struct capy_tls_config cfg = { 1, 0, 0, 1000 };
  TEST("capy_tls_connect_tcp releases managed slot on unsupported");
  if (capy_tls_connect_tcp(3, "example.com", &cfg) == 0 &&
      capy_tls_last_error() == CAPY_TLS_EUNSUPPORTED &&
      capy_tls_last_state() == CAPY_TLS_STATE_UNSUPPORTED) {
    ctx = capy_tls_context_acquire();
    if (ctx && ctx->socket_fd == -1 && ctx->hostname[0] == '\0' &&
        ctx->config.verify_peer == 1 && ctx->config.ca_cert == 0 &&
        ctx->config.timeout_ms == CAPY_TLS_TIMEOUT_DEFAULT_MS) {
      capy_tls_context_release(ctx);
      PASS();
    } else {
      capy_tls_context_release(ctx);
      FAIL("managed slot was not released");
    }
  } else FAIL("valid connect did not reach unsupported");
}

static void test_tls_connect_reports_busy_context_slot(void) {
  struct capy_tls_context *ctx = capy_tls_context_acquire();
  int ok;
  TEST("capy_tls_connect_tcp reports busy managed slot");
  ok = ctx && capy_tls_connect_tcp(3, "example.com", 0) == 0 &&
       capy_tls_last_error() == CAPY_TLS_ESTATE &&
       capy_tls_last_state() == CAPY_TLS_STATE_ERROR;
  capy_tls_context_release(ctx);
  if (ok) PASS();
  else FAIL("busy slot was not reported as bad state");
}

static void test_tls_connect_fails_closed_until_backend_lands(void) {
  struct capy_tls_config cfg = { 1, 0, 0, 1000 };
  struct capy_tls_config cfg_default_timeout = { 1, 0, 0, 0 };
  TEST("valid capy_tls_connect_tcp fails closed as unsupported");
  if (capy_tls_connect_tcp(3, "example.com", &cfg) == 0 &&
      capy_tls_last_error() == CAPY_TLS_EUNSUPPORTED &&
      capy_tls_last_state() == CAPY_TLS_STATE_UNSUPPORTED &&
      capy_tls_connect_tcp(3, "example.com", &cfg_default_timeout) == 0 &&
      capy_tls_last_error() == CAPY_TLS_EUNSUPPORTED &&
      capy_tls_last_state() == CAPY_TLS_STATE_UNSUPPORTED) PASS();
  else FAIL("valid connect did not fail closed");
}

static void test_tls_io_fails_closed_without_backend(void) {
  uint8_t buf[8] = {0};
  struct capy_tls_context *ctx = fake_ctx();
  TEST("send/recv/close fail closed without TLS backend");
  if (capy_tls_send(ctx, buf, sizeof(buf)) == -1 &&
      capy_tls_last_error() == CAPY_TLS_EUNSUPPORTED &&
      capy_tls_recv(ctx, buf, sizeof(buf)) == -1 &&
      capy_tls_last_error() == CAPY_TLS_EUNSUPPORTED &&
      capy_tls_close(ctx) == -1 &&
      capy_tls_last_error() == CAPY_TLS_EUNSUPPORTED) PASS();
  else FAIL("I/O path did not fail closed");
}

static void test_tls_null_io_rejected(void) {
  uint8_t buf[1] = {0};
  TEST("send/recv/close reject NULL context or buffer");
  if (capy_tls_send(0, buf, sizeof(buf)) == -1 &&
      capy_tls_last_error() == CAPY_TLS_EINVAL &&
      capy_tls_recv(fake_ctx(), 0, sizeof(buf)) == -1 &&
      capy_tls_last_error() == CAPY_TLS_EINVAL &&
      capy_tls_close(0) == -1 &&
      capy_tls_last_error() == CAPY_TLS_EINVAL) PASS();
  else FAIL("NULL args were not rejected");
}

static void test_tls_security_info_contract(void) {
  struct capy_tls_security_info info;
  memset(&info, 0xA5, sizeof(info));
  (void)capy_tls_connect_tcp(3, "example.com", 0);
  TEST("unsupported TLS leaves last security info empty");
  if (capy_tls_get_last_security_info(&info) == 0 &&
      info.protocol_version == 0 && info.cipher_suite == 0 &&
      info.trust_anchor_count == 0 && info.peer_verified == 0 &&
      info.hostname_validated == 0 && info.custom_anchor_loaded == 0 &&
      info.alpn[0] == '\0') PASS();
  else FAIL("last security info not empty");
}

static void test_tls_names_are_stable(void) {
  TEST("TLS error/state names are stable strings");
  if (strcmp(capy_tls_error_name(CAPY_TLS_EUNSUPPORTED), "unsupported") == 0 &&
      strcmp(capy_tls_error_name(CAPY_TLS_EINVAL), "invalid-argument") == 0 &&
      strcmp(capy_tls_state_name(CAPY_TLS_STATE_INIT), "init") == 0 &&
      strcmp(capy_tls_state_name(CAPY_TLS_STATE_UNSUPPORTED), "unsupported") == 0)
    PASS();
  else FAIL("name mapping changed");
}

int test_capylibc_tls_run(void) {
  printf("[test_capylibc_tls]\n");
  tests_run = 0;
  tests_passed = 0;
  test_tls_init_resets_state();
  test_tls_config_resolve_defaults();
  test_tls_config_resolve_custom_config();
  test_tls_config_resolve_rejects_invalid_output();
  test_tls_context_prepare_snapshots_ready_context();
  test_tls_context_prepare_rejects_invalid_inputs();
  test_tls_context_reset_and_clear_scrub_state();
  test_tls_context_prepare_resets_on_reject();
  test_tls_context_slot_acquire_release();
  test_tls_free_releases_managed_slot();
  test_tls_default_trust_source_reports_bundle();
  test_tls_default_trust_catalog_key_distribution();
  test_tls_default_trust_catalog_identity_is_stable();
  test_tls_default_trust_slot_table_reports_layout();
  test_tls_default_trust_slot_table_matches_catalog();
  test_tls_default_trust_descriptor_reports_metadata();
  test_tls_default_trust_descriptors_match_slots();
  test_tls_default_trust_anchor_bundle_reports_material();
  test_tls_default_trust_material_summary_reports_aggregate();
  test_tls_default_trust_manifest_reports_provenance();
  test_tls_default_trust_manifest_matches_catalog();
  test_tls_backend_plan_reports_fail_closed_contract();
  test_tls_bearssl_reserved_state_reports_absent_engine();
  test_tls_bearssl_adapter_reports_metadata_only_contract();
  test_tls_backend_stub_reports_unsupported_for_ready_context();
  test_tls_backend_stub_prepares_default_trust_metadata();
  test_tls_backend_stub_rejects_incomplete_context();
  test_tls_backend_stub_resets_state_on_reject();
  test_tls_connect_rejects_invalid_args();
  test_tls_connect_rejects_malformed_hostnames();
  test_tls_connect_rejects_malformed_config();
  test_tls_connect_releases_managed_slot_on_unsupported();
  test_tls_connect_reports_busy_context_slot();
  test_tls_connect_fails_closed_until_backend_lands();
  test_tls_io_fails_closed_without_backend();
  test_tls_null_io_rejected();
  test_tls_security_info_contract();
  test_tls_names_are_stable();
  printf("  -> %d/%d passed\n", tests_passed, tests_run);
  return tests_run - tests_passed;
}
