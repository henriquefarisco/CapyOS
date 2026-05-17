/*
 * tests/userland/test_capylibc_tls.c
 *
 * Host-side regression test for libcapy-tls (the userland TLS façade
 * in `userland/lib/capylibc-tls/`). The tests run on the host
 * (arm64 / x86_64 macOS / Linux) and exercise the metadata-only,
 * fail-closed contract that libcapy-tls offers until a real BearSSL
 * engine lands.
 *
 * This file owns the entry point `test_capylibc_tls_run` and the
 * lifecycle-tier coverage:
 *
 *   - `capy_tls_init` state reset;
 *   - `capy_tls_config_resolve` defaults and explicit-config snapshot;
 *   - `capy_tls_context_prepare`/`reset`/`clear` scrub semantics and
 *     fail-closed handling of bad inputs;
 *   - the single managed context slot acquire/release contract and
 *     `capy_tls_free` slot recycling;
 *   - `capy_tls_connect_tcp` input validation (fd, hostname,
 *     config), busy-slot reporting, fail-closed-as-unsupported
 *     contract, and managed-slot release on unsupported;
 *   - `capy_tls_send`/`recv`/`close` fail-closed contract and NULL
 *     rejection;
 *   - `capy_tls_get_last_security_info` emptiness contract;
 *   - `capy_tls_error_name` / `capy_tls_state_name` stability.
 *
 * Trust-store metadata coverage lives in
 * `tests/userland/test_capylibc_tls_trust.c`. Backend plan, BearSSL
 * reserved state, BearSSL adapter contract and the
 * `capy_tls_backend_connect` state machine live in
 * `tests/userland/test_capylibc_tls_backend.c`. Shared TEST/PASS/FAIL
 * macros, run/pass counter externs and the `fake_ctx()` helper come
 * from `tests/userland/test_capylibc_tls_internal.h`.
 *
 * Carved out of the historical single-file `tests/test_capylibc_tls.c`
 * (1324 LOC) at the 2026-05-15 monolith refactor so each host-test
 * translation unit stays under the 900-line layout limit.
 */
#include <stdint.h>
#include <string.h>

#include "test_capylibc_tls_internal.h"

/* Shared run/pass counter storage. */
int test_capylibc_tls_runs = 0;
int test_capylibc_tls_passes = 0;

struct capy_tls_context *test_capylibc_tls_fake_ctx(void) {
  return (struct capy_tls_context *)(uintptr_t)1;
}

/* === Lifecycle: init + config_resolve =========================== */

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

/* === Context: prepare / reset / clear =========================== */

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

/* === Managed context slot acquire/release/free ================== */

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

/* === capy_tls_connect_tcp / IO / names ========================== */

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

/* === Entry point ================================================ */

int test_capylibc_tls_run(void) {
  printf("[test_capylibc_tls]\n");
  test_capylibc_tls_runs = 0;
  test_capylibc_tls_passes = 0;

  /* Lifecycle / config / context / slot management. */
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

  /* Trust-store metadata coverage (companion file). */
  test_capylibc_tls_trust_cases();

  /* Backend plan / BearSSL state / adapter / backend_connect
   * state machine (companion file). */
  test_capylibc_tls_backend_cases();

  /* Connect / I/O / security info / name maps. */
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

  printf("  -> %d/%d passed\n",
         test_capylibc_tls_passes, test_capylibc_tls_runs);
  return test_capylibc_tls_runs - test_capylibc_tls_passes;
}
