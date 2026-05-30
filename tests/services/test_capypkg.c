/*
 * tests/services/test_capypkg.c
 *
 * Host-side unit tests for the capypkg adapter. Covers:
 *   - init seeds the default repository
 *   - manifest parser accepts well-formed descriptors and rejects
 *     malformed/insecure ones (non-HTTPS URL, missing sha256)
 *   - repository add/remove respect HTTPS-only and pinned protection
 *   - install path verifies SHA-256 and rejects on mismatch
 *   - signature gate fails closed when no verifier is plugged and
 *     accepts when the injected verifier approves
 *   - install/update lifecycle drives state and persists DB
 *
 * Split note (2026-05-29): the test bodies were moved into sibling .inc
 * fragments to keep each source-layout translation-unit fragment under the
 * 900-line limit. This file is the orchestrator and remains the single TU,
 * so the deliberately-static harness in capypkg_test_harness.h stays shared
 * without leaking test symbols. The fragments are:
 *   - test_capypkg_vfs_stub.inc        VFS stub layer (bootstrap-gated)
 *   - test_capypkg_core.inc            init/repo/fetch/install/signature core
 *   - test_capypkg_bootstrap.inc       vfs-write + first-boot bootstrap tests
 *                                      (bootstrap-gated)
 *   - test_capypkg_install_manifest.inc install variants + manifest security
 * The bootstrap-gated fragments compile only under CAPYPKG_BOOTSTRAP_TESTS,
 * matching the original #ifdef structure. Makefile wiring is unchanged
 * because .inc fragments are #included by this TU, not compiled separately.
 */
#include "capypkg_test_harness.h"
#ifdef CAPYPKG_BOOTSTRAP_TESTS
#include "auth/session.h"
#include "fs/vfs.h"
#include "services/capypkg_bootstrap.h"
#include "test_capypkg_vfs_stub.inc"
#endif

#include "test_capypkg_core.inc"

#ifdef CAPYPKG_BOOTSTRAP_TESTS
#include "test_capypkg_bootstrap.inc"
#endif

#include "test_capypkg_install_manifest.inc"

/* ── runner entry ─────────────────────────────────────────────────── */

int run_capypkg_tests(void) {
    g_test_failures = 0;
    test_init_seeds_default_repo();
    test_repo_add_requires_https();
    test_repo_remove_protects_pinned();
    test_fetch_index_populates_catalog_signed_repo();
    test_install_rejects_non_https_payload();
    test_install_verifies_sha256_and_signature();
    test_install_rejects_sha256_mismatch();
    test_full_sweep_can_continue_after_one_payload_failure();
#ifdef CAPYPKG_BOOTSTRAP_TESTS
    test_vfs_write_requires_write_open_flag();
    test_vfs_write_appends_at_current_position();
    test_vfs_write_zero_bytes_is_noop();
    test_bootstrap_partial_failure_is_retryable_without_marker();
    test_bootstrap_marker_write_failure_is_retryable();
    test_bootstrap_marker_open_failure_is_retryable();
    test_bootstrap_marker_create_failure_is_retryable();
    test_bootstrap_basic_marker_write_failure_is_retryable();
    test_bootstrap_basic_marker_open_failure_is_retryable();
    test_bootstrap_basic_marker_create_failure_is_retryable();
    test_bootstrap_marker_create_already_fallback_completes();
    test_bootstrap_existing_marker_unlink_failure_is_retryable();
    test_bootstrap_existing_marker_rmdir_failure_is_retryable();
    test_bootstrap_marker_creates_missing_install_dirs();
    test_bootstrap_system_file_blocks_marker_dir_retryable();
    test_bootstrap_install_dir_file_blocks_marker_retryable();
    test_bootstrap_ignores_directory_named_done_marker();
    test_bootstrap_nonempty_marker_directory_is_retryable();
    test_bootstrap_marker_file_short_circuits_bad_profile();
    test_bootstrap_force_bypasses_marker_file();
    test_bootstrap_profile_directory_is_retryable_storage();
#endif
    test_install_accepts_payload_larger_than_one_mib();
    test_install_fail_closed_without_verifier();
    test_install_no_source_when_no_repo();
    test_repo_serialize_roundtrip();
    test_install_rejects_when_verifier_returns_failure();
    test_manifest_rejects_install_root_outside_allowed();
    test_manifest_rejects_malformed_sha256();
    test_install_resolves_dependency_chain();
    test_install_dependency_missing_in_catalog_fails();
    test_stats_reports_state_correctly();
    test_install_emits_audit_trail_on_success();
    test_install_emits_audit_trail_on_sha256_mismatch();
    test_repo_update_clears_any_repo_signed_when_last_signed();
    test_manifest_rejects_unsafe_package_name();
    test_manifest_rejects_install_root_prefix_bypass();
    test_manifest_rejects_install_root_dotdot();
    test_fetch_index_skips_malformed_entry_and_keeps_valid_one();
    test_repo_add_rejects_ansi_escape_in_arguments();
    test_manifest_rejects_ansi_escape_in_value();
    test_manifest_rejects_unsafe_dependency_name();
    test_manifest_rejects_payload_size_overflow();
    test_state_and_result_labels_are_well_formed();
    return g_test_failures;
}
