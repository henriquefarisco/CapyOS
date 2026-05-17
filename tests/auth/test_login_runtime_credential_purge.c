/*
 * tests/auth/test_login_runtime_credential_purge.c
 *
 * Credential screen purge plan coverage for the `login_runtime`
 * host test. Carved out of `tests/auth/test_login_runtime.c` at the
 * 2026-05-16 monolith refactor (PR D.29 of the Estagio D dedicated
 * plan) so each host-test translation unit stays under the 900-line
 * layout limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_purge_plan_build`: 4 tests
 *     covering the credential widgets purge + the text-route purge
 *     (recovery + resume) + the submit/unknown fallback purge + the
 *     missing-or-unsafe expiry plan fail-closed default.
 *
 * Also exposes shared helper
 * `build_loginwindow_credential_screen_purge_plan_for_action`, used
 * by later companion files that chain on top of the purge stage
 * (tombstone, ...).
 *
 * Split independently from `tombstone` (PR D.30) because the
 * combined block exceeded the 900-line layout limit.
 *
 * The companion entry `test_login_runtime_credential_purge_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_purge_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_purge_plan *purge_plan) {
  struct login_window_credential_screen_expiry_plan expiry_plan;

  if (build_loginwindow_credential_screen_expiry_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &expiry_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_purge_plan_build(&expiry_plan,
                                                         purge_plan);
}

static int test_loginwindow_credential_screen_purge_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_purge_plan purge_plan;

  fails += expect_true(build_loginwindow_credential_screen_purge_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'a', 0, 0, 0,
                           1, &purge_plan) == 0,
                       "credential purge plan edit should build");
  fails += expect_true(purge_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_PURGE_PLAN_VERSION,
                       "credential purge plan should expose stable version");
  fails += expect_true(purge_plan.expiry_plan_available == 1 &&
                           purge_plan.expiry_plan_safe == 1 &&
                           purge_plan.purge_plan_safe == 1,
                       "credential purge plan should require safe expiry plan");
  fails += expect_true(purge_plan.purge_required == 1 &&
                           purge_plan.purge_allowed == 1 &&
                           purge_plan.purge_submitted == 0 &&
                           purge_plan.purge_ticket_selected == 1 &&
                           purge_plan.purge_target_selected == 1 &&
                           purge_plan.purge_persist_allowed == 0 &&
                           purge_plan.purge_persisted == 0 &&
                           purge_plan.purge_cpu_gpu_sync_allowed == 0 &&
                           purge_plan.purge_cpu_gpu_sync_submitted == 0 &&
                           purge_plan.purge_delete_allowed == 0 &&
                           purge_plan.purge_deleted == 0,
                       "credential purge plan should remain declarative");
  fails += expect_true(purge_plan.expiry_submitted == 0 &&
                           purge_plan.expiry_persisted == 0 &&
                           purge_plan.retention_submitted == 0 &&
                           purge_plan.retention_persisted == 0 &&
                           purge_plan.archive_submitted == 0 &&
                           purge_plan.archive_persisted == 0 &&
                           purge_plan.journal_submitted == 0 &&
                           purge_plan.journal_persisted == 0 &&
                           purge_plan.ledger_submitted == 0 &&
                           purge_plan.ledger_persisted == 0 &&
                           purge_plan.receipt_submitted == 0 &&
                           purge_plan.receipt_persisted == 0 &&
                           purge_plan.record_submitted == 0 &&
                           purge_plan.record_persisted == 0 &&
                           purge_plan.audit_submitted == 0 &&
                           purge_plan.audit_log_appended == 0 &&
                           purge_plan.seal_submitted == 0 &&
                           purge_plan.cleanup_submitted == 0 &&
                           purge_plan.retire_submitted == 0 &&
                           purge_plan.ack_submitted == 0 &&
                           purge_plan.completion_reported == 0 &&
                           purge_plan.deadline_armed == 0 &&
                           purge_plan.sync_submitted == 0 &&
                           purge_plan.timeline_submitted == 0 &&
                           purge_plan.fence_submitted == 0 &&
                           purge_plan.barrier_submitted == 0 &&
                           purge_plan.flush_submitted == 0 &&
                           purge_plan.framebuffer_written == 0 &&
                           purge_plan.blit_pixels_copied == 0 &&
                           purge_plan.output_submitted == 0 &&
                           purge_plan.display_mode_committed == 0 &&
                           purge_plan.page_flip_submitted == 0,
                       "credential purge plan must not execute GUI work");
  fails += expect_true(purge_plan.purge_credential_panel == 1 &&
                           purge_plan.purge_credential_input == 1 &&
                           purge_plan.purge_credential_focus == 1,
                       "credential purge plan should mark credential widgets");
  fails += expect_true(purge_plan.submit_callback_bound == 0 &&
                           purge_plan.auth_callback_bound == 0 &&
                           purge_plan.submit_enabled == 0 &&
                           purge_plan.auth_attempt_allowed == 0 &&
                           purge_plan.raw_secret_exposed == 0 &&
                           purge_plan.masked_text_exposed == 0 &&
                           purge_plan.length_redacted == 1,
                       "credential purge plan must stay redacted and auth-disabled");
  fails += expect_true(strings_equal(purge_plan.purge_ticket,
                                     "credential-screen-purge-ticket") &&
                           strings_equal(purge_plan.expiry_ticket,
                                         "credential-screen-expiry-ticket") &&
                           strings_equal(purge_plan.purge_policy,
                                         "declarative-purge-no-delete") &&
                           strings_equal(purge_plan.state,
                                         "purge-credential-ready"),
                       "credential purge plan should report purge ticket");
  return fails;
}

static int test_loginwindow_credential_screen_purge_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_purge_plan purge_plan;

  fails += expect_true(build_loginwindow_credential_screen_purge_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &purge_plan) == 0,
                       "credential purge plan recovery should build");
  fails += expect_true(purge_plan.purge_plan_safe == 1 &&
                           purge_plan.purge_allowed == 1 &&
                           purge_plan.purge_submitted == 0 &&
                           purge_plan.purge_persisted == 0 &&
                           purge_plan.purge_deleted == 0 &&
                           purge_plan.purge_text_recovery == 1 &&
                           purge_plan.purge_text_login == 1 &&
                           purge_plan.purge_credential_focus == 0,
                       "credential purge plan recovery should mark text recovery");
  fails += expect_true(purge_plan.expiry_submitted == 0 &&
                           purge_plan.retention_submitted == 0 &&
                           purge_plan.archive_submitted == 0 &&
                           purge_plan.journal_submitted == 0 &&
                           purge_plan.ledger_submitted == 0 &&
                           purge_plan.receipt_submitted == 0 &&
                           purge_plan.record_submitted == 0 &&
                           purge_plan.audit_submitted == 0 &&
                           purge_plan.audit_log_appended == 0 &&
                           purge_plan.seal_submitted == 0 &&
                           purge_plan.cleanup_submitted == 0 &&
                           purge_plan.retire_submitted == 0 &&
                           purge_plan.ack_submitted == 0 &&
                           purge_plan.completion_reported == 0 &&
                           purge_plan.deadline_armed == 0 &&
                           purge_plan.sync_submitted == 0 &&
                           purge_plan.timeline_submitted == 0 &&
                           purge_plan.fence_submitted == 0 &&
                           purge_plan.barrier_submitted == 0 &&
                           purge_plan.flush_submitted == 0 &&
                           purge_plan.framebuffer_written == 0 &&
                           purge_plan.output_submitted == 0 &&
                           purge_plan.display_mode_committed == 0 &&
                           purge_plan.page_flip_submitted == 0 &&
                           purge_plan.submit_enabled == 0 &&
                           purge_plan.auth_attempt_allowed == 0,
                       "credential purge plan recovery must not persist or output");
  fails += expect_true(strings_equal(purge_plan.purge_ticket,
                                     "text-recovery-purge-ticket") &&
                           strings_equal(purge_plan.compositor_target,
                                         "text-recovery-purge") &&
                           strings_equal(purge_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential purge plan recovery should report recovery ticket");

  fails += expect_true(build_loginwindow_credential_screen_purge_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &purge_plan) == 0,
                       "credential purge plan resume should build");
  fails += expect_true(purge_plan.purge_plan_safe == 1 &&
                           purge_plan.purge_text_login_resume == 1 &&
                           purge_plan.session_reset_required == 1 &&
                           purge_plan.login_screen_rerender_required == 1 &&
                           purge_plan.purge_submitted == 0 &&
                           purge_plan.purge_persisted == 0 &&
                           purge_plan.purge_deleted == 0 &&
                           purge_plan.expiry_submitted == 0 &&
                           purge_plan.submit_enabled == 0 &&
                           purge_plan.auth_attempt_allowed == 0,
                       "credential purge plan resume should keep GUI auth disabled");
  fails += expect_true(strings_equal(purge_plan.purge_ticket,
                                     "text-login-resume-purge-ticket") &&
                           strings_equal(purge_plan.purge_policy,
                                         "full-purge-declarative") &&
                           strings_equal(purge_plan.state,
                                         "purge-resume-ready"),
                       "credential purge plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_purge_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_purge_plan purge_plan;

  fails += expect_true(build_loginwindow_credential_screen_purge_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &purge_plan) == 0,
                       "credential purge plan submit should build");
  fails += expect_true(purge_plan.purge_plan_safe == 1 &&
                           purge_plan.submit_requested == 1 &&
                           purge_plan.purge_text_login_fallback == 1 &&
                           purge_plan.action_allowed == 0 &&
                           purge_plan.action_blocked == 1 &&
                           purge_plan.input_focus_allowed == 0,
                       "credential purge plan submit should force text login fallback");
  fails += expect_true(purge_plan.purge_submitted == 0 &&
                           purge_plan.purge_persist_allowed == 0 &&
                           purge_plan.purge_persisted == 0 &&
                           purge_plan.purge_cpu_gpu_sync_allowed == 0 &&
                           purge_plan.purge_cpu_gpu_sync_submitted == 0 &&
                           purge_plan.purge_delete_allowed == 0 &&
                           purge_plan.purge_deleted == 0 &&
                           purge_plan.expiry_submitted == 0 &&
                           purge_plan.expiry_persisted == 0 &&
                           purge_plan.retention_submitted == 0 &&
                           purge_plan.retention_persisted == 0 &&
                           purge_plan.archive_submitted == 0 &&
                           purge_plan.archive_persisted == 0 &&
                           purge_plan.journal_submitted == 0 &&
                           purge_plan.journal_persisted == 0 &&
                           purge_plan.ledger_submitted == 0 &&
                           purge_plan.ledger_persisted == 0 &&
                           purge_plan.receipt_submitted == 0 &&
                           purge_plan.receipt_persisted == 0 &&
                           purge_plan.record_submitted == 0 &&
                           purge_plan.audit_submitted == 0 &&
                           purge_plan.audit_log_appended == 0 &&
                           purge_plan.seal_submitted == 0 &&
                           purge_plan.cleanup_submitted == 0 &&
                           purge_plan.retire_submitted == 0 &&
                           purge_plan.ack_submitted == 0 &&
                           purge_plan.completion_reported == 0 &&
                           purge_plan.deadline_armed == 0 &&
                           purge_plan.sync_submitted == 0 &&
                           purge_plan.timeline_submitted == 0 &&
                           purge_plan.fence_submitted == 0 &&
                           purge_plan.barrier_submitted == 0 &&
                           purge_plan.flush_submitted == 0 &&
                           purge_plan.framebuffer_written == 0 &&
                           purge_plan.blit_pixels_copied == 0 &&
                           purge_plan.output_submitted == 0 &&
                           purge_plan.display_submitted == 0 &&
                           purge_plan.page_flip_submitted == 0 &&
                           purge_plan.submit_callback_bound == 0 &&
                           purge_plan.auth_callback_bound == 0 &&
                           purge_plan.submit_enabled == 0 &&
                           purge_plan.auth_attempt_allowed == 0,
                       "credential purge plan submit must stay declarative");
  fails += expect_true(strings_equal(purge_plan.purge_ticket,
                                     "text-login-fallback-purge-ticket") &&
                           strings_equal(purge_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(purge_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential purge plan submit should explain fallback");

  fails += expect_true(build_loginwindow_credential_screen_purge_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &purge_plan) == 0,
                       "credential purge plan unknown should build");
  fails += expect_true(purge_plan.purge_plan_safe == 1 &&
                           purge_plan.purge_text_login_fallback == 1 &&
                           purge_plan.action_allowed == 0 &&
                           purge_plan.action_blocked == 1,
                       "credential purge plan unknown should force text login fallback");
  fails += expect_true(strings_equal(purge_plan.purge_ticket,
                                     "text-login-fallback-purge-ticket") &&
                           strings_equal(purge_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential purge plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_purge_plan_fails_closed_for_unsafe_or_missing_expiry_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_expiry_plan expiry_plan;
  struct login_window_credential_screen_purge_plan purge_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_purge_plan_build(
                           NULL, &purge_plan) == 0,
                       "credential purge plan missing expiry plan should build fail-closed state");
  fails += expect_true(purge_plan.expiry_plan_available == 0 &&
                           purge_plan.expiry_plan_safe == 0 &&
                           purge_plan.purge_plan_safe == 0 &&
                           purge_plan.route_selected == 0 &&
                           purge_plan.route_blocked == 1,
                       "credential purge plan missing expiry plan should block purge plan");
  fails += expect_true(purge_plan.purge_allowed == 0 &&
                           purge_plan.purge_submitted == 0 &&
                           purge_plan.purge_persisted == 0 &&
                           purge_plan.purge_deleted == 0 &&
                           purge_plan.expiry_submitted == 0 &&
                           purge_plan.purge_text_login_fallback == 1 &&
                           purge_plan.submit_enabled == 0 &&
                           purge_plan.auth_attempt_allowed == 0,
                       "credential purge plan missing expiry plan must stay redacted");
  fails += expect_true(strings_equal(purge_plan.purge_ticket,
                                     "text-login-fallback-purge-ticket") &&
                           strings_equal(purge_plan.event_type,
                                         "credential-screen-purge-plan-unavailable") &&
                           strings_equal(purge_plan.blocked_reason,
                                         "expiry-plan-unavailable"),
                       "credential purge plan missing expiry plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_expiry_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &expiry_plan) == 0,
                       "credential purge plan unsafe expiry source should build");
  fails += expect_true(login_window_credential_screen_purge_plan_build(
                           &expiry_plan, &purge_plan) == 0,
                       "credential purge plan unsafe expiry plan should build blocked state");
  fails += expect_true(purge_plan.expiry_plan_available == 1 &&
                           purge_plan.expiry_plan_safe == 0 &&
                           purge_plan.purge_plan_safe == 0 &&
                           purge_plan.route_selected == 0 &&
                           purge_plan.route_blocked == 1,
                       "credential purge plan unsafe expiry plan should block purge plan");
  fails += expect_true(purge_plan.purge_allowed == 0 &&
                           purge_plan.purge_submitted == 0 &&
                           purge_plan.purge_text_login_fallback == 1 &&
                           purge_plan.submit_enabled == 0 &&
                           purge_plan.auth_attempt_allowed == 0,
                       "credential purge plan unsafe expiry plan must force text login fallback");
  fails += expect_true(strings_equal(purge_plan.purge_ticket,
                                     "text-login-fallback-purge-ticket") &&
                           strings_equal(purge_plan.event_type,
                                         "credential-screen-purge-plan-unsafe") &&
                           strings_equal(purge_plan.blocked_reason,
                                         "credential-purge-plan-unsafe"),
                       "credential purge plan unsafe expiry plan should force text login");

  fails += expect_true(build_loginwindow_credential_screen_expiry_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'y', 0, 0, 0,
                           1, &expiry_plan) == 0,
                       "credential purge plan submitted expiry source should build");
  expiry_plan.expiry_submitted = 1;
  expiry_plan.expiry_persist_allowed = 1;
  expiry_plan.expiry_persisted = 1;
  expiry_plan.expiry_cpu_gpu_sync_allowed = 1;
  expiry_plan.expiry_cpu_gpu_sync_submitted = 1;
  expiry_plan.expiry_delete_allowed = 1;
  expiry_plan.expiry_deleted = 1;
  expiry_plan.retention_submitted = 1;
  expiry_plan.retention_persist_allowed = 1;
  expiry_plan.retention_persisted = 1;
  expiry_plan.archive_submitted = 1;
  expiry_plan.archive_persist_allowed = 1;
  expiry_plan.archive_persisted = 1;
  expiry_plan.journal_submitted = 1;
  expiry_plan.journal_persist_allowed = 1;
  expiry_plan.journal_persisted = 1;
  expiry_plan.ledger_submitted = 1;
  expiry_plan.ledger_persist_allowed = 1;
  expiry_plan.ledger_persisted = 1;
  expiry_plan.receipt_submitted = 1;
  expiry_plan.receipt_persist_allowed = 1;
  expiry_plan.receipt_persisted = 1;
  expiry_plan.record_submitted = 1;
  expiry_plan.record_persisted = 1;
  expiry_plan.audit_submitted = 1;
  expiry_plan.audit_log_appended = 1;
  expiry_plan.seal_submitted = 1;
  expiry_plan.cleanup_submitted = 1;
  expiry_plan.retire_submitted = 1;
  expiry_plan.ack_submitted = 1;
  expiry_plan.completion_reported = 1;
  expiry_plan.deadline_armed = 1;
  expiry_plan.page_flip_allowed = 1;
  fails += expect_true(login_window_credential_screen_purge_plan_build(
                           &expiry_plan, &purge_plan) == 0,
                       "credential purge plan submitted expiry should fail closed");
  fails += expect_true(purge_plan.purge_plan_safe == 0 &&
                           purge_plan.purge_allowed == 0 &&
                           purge_plan.purge_submitted == 0 &&
                           purge_plan.purge_persist_allowed == 0 &&
                           purge_plan.purge_persisted == 0 &&
                           purge_plan.purge_cpu_gpu_sync_allowed == 0 &&
                           purge_plan.purge_cpu_gpu_sync_submitted == 0 &&
                           purge_plan.purge_delete_allowed == 0 &&
                           purge_plan.purge_deleted == 0 &&
                           purge_plan.expiry_submitted == 0 &&
                           purge_plan.expiry_persist_allowed == 0 &&
                           purge_plan.expiry_persisted == 0 &&
                           purge_plan.expiry_cpu_gpu_sync_allowed == 0 &&
                           purge_plan.expiry_cpu_gpu_sync_submitted == 0 &&
                           purge_plan.expiry_delete_allowed == 0 &&
                           purge_plan.expiry_deleted == 0 &&
                           purge_plan.retention_submitted == 0 &&
                           purge_plan.retention_persist_allowed == 0 &&
                           purge_plan.retention_persisted == 0 &&
                           purge_plan.archive_submitted == 0 &&
                           purge_plan.archive_persist_allowed == 0 &&
                           purge_plan.archive_persisted == 0 &&
                           purge_plan.journal_submitted == 0 &&
                           purge_plan.journal_persist_allowed == 0 &&
                           purge_plan.journal_persisted == 0 &&
                           purge_plan.ledger_submitted == 0 &&
                           purge_plan.ledger_persist_allowed == 0 &&
                           purge_plan.ledger_persisted == 0 &&
                           purge_plan.receipt_submitted == 0 &&
                           purge_plan.receipt_persist_allowed == 0 &&
                           purge_plan.receipt_persisted == 0 &&
                           purge_plan.record_submitted == 0 &&
                           purge_plan.record_persisted == 0 &&
                           purge_plan.audit_submitted == 0 &&
                           purge_plan.audit_log_appended == 0 &&
                           purge_plan.seal_submitted == 0 &&
                           purge_plan.cleanup_submitted == 0 &&
                           purge_plan.retire_submitted == 0 &&
                           purge_plan.ack_submitted == 0 &&
                           purge_plan.completion_reported == 0 &&
                           purge_plan.deadline_armed == 0 &&
                           purge_plan.page_flip_allowed == 0 &&
                           purge_plan.page_flip_submitted == 0 &&
                           purge_plan.submit_enabled == 0 &&
                           purge_plan.auth_attempt_allowed == 0,
                       "credential purge plan must not copy unsafe submitted expiry state");
  return fails;
}

int test_login_runtime_credential_purge_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_purge_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_purge_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_purge_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_purge_plan_fails_closed_for_unsafe_or_missing_expiry_plan();
  return fails;
}
