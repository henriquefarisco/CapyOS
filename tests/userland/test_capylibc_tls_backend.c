/*
 * tests/userland/test_capylibc_tls_backend.c
 *
 * Backend-tier metadata + state-machine coverage for libcapy-tls
 * (the userland TLS façade in `userland/lib/capylibc-tls/`). This
 * translation unit pins:
 *
 *   - the default backend plan contract (BearSSL userland engine,
 *     fail-closed-only, handshake-disabled, trust-metadata-gated);
 *   - the reserved-state contract for the BearSSL backend (engine
 *     absent, context/io bytes absent, handshake disabled);
 *   - the BearSSL adapter contract (metadata-only, backend-plan
 *     gated, reserved-state gated, engine-init/handshake disabled);
 *   - the `capy_tls_backend_connect` state machine:
 *     - consuming a ready prepared context as unsupported while
 *       still recording trust readiness flags;
 *     - recording default trust metadata + plan/state/adapter
 *       fingerprints when no custom anchor is set;
 *     - rejecting incomplete or corrupt contexts and scrubbing them
 *       fail-closed.
 *
 * Carved out of the historical single-file
 * `tests/test_capylibc_tls.c` (1324 LOC) at the 2026-05-15 monolith
 * refactor. The TEST/PASS/FAIL macros, `tests_run`/`tests_passed`
 * counters and companion entry contract come from
 * `tests/userland/test_capylibc_tls_internal.h`. The owning entry is
 * `test_capylibc_tls_backend_cases()`, invoked by
 * `test_capylibc_tls_run` in `tests/userland/test_capylibc_tls.c`.
 */
#include <stdint.h>
#include <string.h>

#include "test_capylibc_tls_internal.h"

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

void test_capylibc_tls_backend_cases(void) {
  test_tls_backend_plan_reports_fail_closed_contract();
  test_tls_bearssl_reserved_state_reports_absent_engine();
  test_tls_bearssl_adapter_reports_metadata_only_contract();
  test_tls_backend_stub_reports_unsupported_for_ready_context();
  test_tls_backend_stub_prepares_default_trust_metadata();
  test_tls_backend_stub_rejects_incomplete_context();
  test_tls_backend_stub_resets_state_on_reject();
}
