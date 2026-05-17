/*
 * tests/auth/test_login_runtime_credential_expiry_plan.c
 *
 * Credential screen expiry plan coverage for the `login_runtime`
 * host test. Carved out of
 * `tests/auth/test_login_runtime_credential_retention_expiry.c` at
 * the 2026-05-16 preventive refactor (the parent companion had
 * reached 832/900 LOC, 68 lines from the audit ceiling) so each
 * host-test translation unit stays comfortably below the 900-line
 * layout limit.
 *
 * Tests in this file exercise
 * `login_window_credential_screen_expiry_plan_build`:
 *
 *   - the credential widgets expiry,
 *   - the text-route expiry (recovery + resume),
 *   - the submit/unknown fallback expiry,
 *   - the missing-or-unsafe retention plan fail-closed default.
 *
 * Also exposes the shared helper
 * `build_loginwindow_credential_screen_expiry_plan_for_action`,
 * used by later companion files that chain on top of the expiry
 * stage (purge, ...). This helper depends on
 * `build_loginwindow_credential_screen_retention_plan_for_action`,
 * which stays in the parent companion
 * (`test_login_runtime_credential_retention_expiry.c`) and is
 * declared in the internal header for cross-TU linkage.
 *
 * The companion entry
 * `test_login_runtime_credential_expiry_plan_cases` is invoked by
 * `run_login_runtime_tests` in `tests/auth/test_login_runtime.c`
 * directly after the existing `_retention_expiry_cases()` call.
 * Shared fixture state and helpers come from
 * `tests/auth/test_login_runtime_internal.h`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_expiry_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_expiry_plan *expiry_plan) {
  struct login_window_credential_screen_retention_plan retention_plan;

  if (build_loginwindow_credential_screen_retention_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &retention_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_expiry_plan_build(&retention_plan,
                                                          expiry_plan);
}

static int test_loginwindow_credential_screen_expiry_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_expiry_plan expiry_plan;

  fails += expect_true(build_loginwindow_credential_screen_expiry_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'a', 0, 0, 0,
                           1, &expiry_plan) == 0,
                       "credential expiry plan edit should build");
  fails += expect_true(expiry_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_EXPIRY_PLAN_VERSION,
                       "credential expiry plan should expose stable version");
  fails += expect_true(expiry_plan.retention_plan_available == 1 &&
                           expiry_plan.retention_plan_safe == 1 &&
                           expiry_plan.expiry_plan_safe == 1,
                       "credential expiry plan should require safe retention plan");
  fails += expect_true(expiry_plan.expiry_required == 1 &&
                           expiry_plan.expiry_allowed == 1 &&
                           expiry_plan.expiry_submitted == 0 &&
                           expiry_plan.expiry_ticket_selected == 1 &&
                           expiry_plan.expiry_target_selected == 1 &&
                           expiry_plan.expiry_persist_allowed == 0 &&
                           expiry_plan.expiry_persisted == 0 &&
                           expiry_plan.expiry_cpu_gpu_sync_allowed == 0 &&
                           expiry_plan.expiry_cpu_gpu_sync_submitted == 0 &&
                           expiry_plan.expiry_timer_allowed == 0 &&
                           expiry_plan.expiry_timer_armed == 0 &&
                           expiry_plan.expiry_delete_allowed == 0 &&
                           expiry_plan.expiry_deleted == 0,
                       "credential expiry plan should remain declarative");
  fails += expect_true(expiry_plan.retention_submitted == 0 &&
                           expiry_plan.retention_persisted == 0 &&
                           expiry_plan.archive_submitted == 0 &&
                           expiry_plan.archive_persisted == 0 &&
                           expiry_plan.journal_submitted == 0 &&
                           expiry_plan.journal_persisted == 0 &&
                           expiry_plan.ledger_submitted == 0 &&
                           expiry_plan.ledger_persisted == 0 &&
                           expiry_plan.receipt_submitted == 0 &&
                           expiry_plan.receipt_persisted == 0 &&
                           expiry_plan.record_submitted == 0 &&
                           expiry_plan.record_persisted == 0 &&
                           expiry_plan.audit_submitted == 0 &&
                           expiry_plan.audit_log_appended == 0 &&
                           expiry_plan.seal_submitted == 0 &&
                           expiry_plan.cleanup_submitted == 0 &&
                           expiry_plan.retire_submitted == 0 &&
                           expiry_plan.ack_submitted == 0 &&
                           expiry_plan.completion_reported == 0 &&
                           expiry_plan.deadline_armed == 0 &&
                           expiry_plan.sync_submitted == 0 &&
                           expiry_plan.timeline_submitted == 0 &&
                           expiry_plan.fence_submitted == 0 &&
                           expiry_plan.barrier_submitted == 0 &&
                           expiry_plan.flush_submitted == 0 &&
                           expiry_plan.framebuffer_written == 0 &&
                           expiry_plan.blit_pixels_copied == 0 &&
                           expiry_plan.output_submitted == 0 &&
                           expiry_plan.display_mode_committed == 0 &&
                           expiry_plan.page_flip_submitted == 0,
                       "credential expiry plan must not execute GUI work");
  fails += expect_true(expiry_plan.expiry_credential_panel == 1 &&
                           expiry_plan.expiry_credential_input == 1 &&
                           expiry_plan.expiry_credential_focus == 1,
                       "credential expiry plan should mark credential widgets");
  fails += expect_true(expiry_plan.submit_callback_bound == 0 &&
                           expiry_plan.auth_callback_bound == 0 &&
                           expiry_plan.submit_enabled == 0 &&
                           expiry_plan.auth_attempt_allowed == 0 &&
                           expiry_plan.raw_secret_exposed == 0 &&
                           expiry_plan.masked_text_exposed == 0 &&
                           expiry_plan.length_redacted == 1,
                       "credential expiry plan must stay redacted and auth-disabled");
  fails += expect_true(strings_equal(expiry_plan.expiry_ticket,
                                     "credential-screen-expiry-ticket") &&
                           strings_equal(expiry_plan.retention_ticket,
                                         "credential-screen-retention-ticket") &&
                           strings_equal(expiry_plan.expiry_policy,
                                         "declarative-expiry-no-persist") &&
                           strings_equal(expiry_plan.state,
                                         "expiry-credential-ready"),
                       "credential expiry plan should report expiry ticket");
  return fails;
}

static int test_loginwindow_credential_screen_expiry_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_expiry_plan expiry_plan;

  fails += expect_true(build_loginwindow_credential_screen_expiry_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &expiry_plan) == 0,
                       "credential expiry plan recovery should build");
  fails += expect_true(expiry_plan.expiry_plan_safe == 1 &&
                           expiry_plan.expiry_allowed == 1 &&
                           expiry_plan.expiry_submitted == 0 &&
                           expiry_plan.expiry_persisted == 0 &&
                           expiry_plan.expiry_timer_armed == 0 &&
                           expiry_plan.expiry_deleted == 0 &&
                           expiry_plan.expiry_text_recovery == 1 &&
                           expiry_plan.expiry_text_login == 1 &&
                           expiry_plan.expiry_credential_focus == 0,
                       "credential expiry plan recovery should mark text recovery");
  fails += expect_true(expiry_plan.retention_submitted == 0 &&
                           expiry_plan.archive_submitted == 0 &&
                           expiry_plan.journal_submitted == 0 &&
                           expiry_plan.ledger_submitted == 0 &&
                           expiry_plan.receipt_submitted == 0 &&
                           expiry_plan.record_submitted == 0 &&
                           expiry_plan.audit_submitted == 0 &&
                           expiry_plan.audit_log_appended == 0 &&
                           expiry_plan.seal_submitted == 0 &&
                           expiry_plan.cleanup_submitted == 0 &&
                           expiry_plan.retire_submitted == 0 &&
                           expiry_plan.ack_submitted == 0 &&
                           expiry_plan.completion_reported == 0 &&
                           expiry_plan.deadline_armed == 0 &&
                           expiry_plan.sync_submitted == 0 &&
                           expiry_plan.timeline_submitted == 0 &&
                           expiry_plan.fence_submitted == 0 &&
                           expiry_plan.barrier_submitted == 0 &&
                           expiry_plan.flush_submitted == 0 &&
                           expiry_plan.framebuffer_written == 0 &&
                           expiry_plan.output_submitted == 0 &&
                           expiry_plan.display_mode_committed == 0 &&
                           expiry_plan.page_flip_submitted == 0 &&
                           expiry_plan.submit_enabled == 0 &&
                           expiry_plan.auth_attempt_allowed == 0,
                       "credential expiry plan recovery must not persist or output");
  fails += expect_true(strings_equal(expiry_plan.expiry_ticket,
                                     "text-recovery-expiry-ticket") &&
                           strings_equal(expiry_plan.compositor_target,
                                         "text-recovery-expiry") &&
                           strings_equal(expiry_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential expiry plan recovery should report recovery ticket");

  fails += expect_true(build_loginwindow_credential_screen_expiry_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &expiry_plan) == 0,
                       "credential expiry plan resume should build");
  fails += expect_true(expiry_plan.expiry_plan_safe == 1 &&
                           expiry_plan.expiry_text_login_resume == 1 &&
                           expiry_plan.session_reset_required == 1 &&
                           expiry_plan.login_screen_rerender_required == 1 &&
                           expiry_plan.expiry_submitted == 0 &&
                           expiry_plan.expiry_persisted == 0 &&
                           expiry_plan.expiry_timer_armed == 0 &&
                           expiry_plan.expiry_deleted == 0 &&
                           expiry_plan.retention_submitted == 0 &&
                           expiry_plan.submit_enabled == 0 &&
                           expiry_plan.auth_attempt_allowed == 0,
                       "credential expiry plan resume should keep GUI auth disabled");
  fails += expect_true(strings_equal(expiry_plan.expiry_ticket,
                                     "text-login-resume-expiry-ticket") &&
                           strings_equal(expiry_plan.expiry_policy,
                                         "full-expiry-declarative") &&
                           strings_equal(expiry_plan.state,
                                         "expiry-resume-ready"),
                       "credential expiry plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_expiry_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_expiry_plan expiry_plan;

  fails += expect_true(build_loginwindow_credential_screen_expiry_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &expiry_plan) == 0,
                       "credential expiry plan submit should build");
  fails += expect_true(expiry_plan.expiry_plan_safe == 1 &&
                           expiry_plan.submit_requested == 1 &&
                           expiry_plan.expiry_text_login_fallback == 1 &&
                           expiry_plan.action_allowed == 0 &&
                           expiry_plan.action_blocked == 1 &&
                           expiry_plan.input_focus_allowed == 0,
                       "credential expiry plan submit should force text login fallback");
  fails += expect_true(expiry_plan.expiry_submitted == 0 &&
                           expiry_plan.expiry_persist_allowed == 0 &&
                           expiry_plan.expiry_persisted == 0 &&
                           expiry_plan.expiry_cpu_gpu_sync_allowed == 0 &&
                           expiry_plan.expiry_cpu_gpu_sync_submitted == 0 &&
                           expiry_plan.expiry_timer_allowed == 0 &&
                           expiry_plan.expiry_timer_armed == 0 &&
                           expiry_plan.expiry_delete_allowed == 0 &&
                           expiry_plan.expiry_deleted == 0 &&
                           expiry_plan.retention_submitted == 0 &&
                           expiry_plan.retention_persisted == 0 &&
                           expiry_plan.archive_submitted == 0 &&
                           expiry_plan.archive_persisted == 0 &&
                           expiry_plan.journal_submitted == 0 &&
                           expiry_plan.journal_persisted == 0 &&
                           expiry_plan.ledger_submitted == 0 &&
                           expiry_plan.ledger_persisted == 0 &&
                           expiry_plan.receipt_submitted == 0 &&
                           expiry_plan.receipt_persisted == 0 &&
                           expiry_plan.record_submitted == 0 &&
                           expiry_plan.audit_submitted == 0 &&
                           expiry_plan.audit_log_appended == 0 &&
                           expiry_plan.seal_submitted == 0 &&
                           expiry_plan.cleanup_submitted == 0 &&
                           expiry_plan.retire_submitted == 0 &&
                           expiry_plan.ack_submitted == 0 &&
                           expiry_plan.completion_reported == 0 &&
                           expiry_plan.deadline_armed == 0 &&
                           expiry_plan.sync_submitted == 0 &&
                           expiry_plan.timeline_submitted == 0 &&
                           expiry_plan.fence_submitted == 0 &&
                           expiry_plan.barrier_submitted == 0 &&
                           expiry_plan.flush_submitted == 0 &&
                           expiry_plan.framebuffer_written == 0 &&
                           expiry_plan.blit_pixels_copied == 0 &&
                           expiry_plan.output_submitted == 0 &&
                           expiry_plan.display_submitted == 0 &&
                           expiry_plan.page_flip_submitted == 0 &&
                           expiry_plan.submit_callback_bound == 0 &&
                           expiry_plan.auth_callback_bound == 0 &&
                           expiry_plan.submit_enabled == 0 &&
                           expiry_plan.auth_attempt_allowed == 0,
                       "credential expiry plan submit must stay declarative");
  fails += expect_true(strings_equal(expiry_plan.expiry_ticket,
                                     "text-login-fallback-expiry-ticket") &&
                           strings_equal(expiry_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(expiry_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential expiry plan submit should explain fallback");

  fails += expect_true(build_loginwindow_credential_screen_expiry_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &expiry_plan) == 0,
                       "credential expiry plan unknown should build");
  fails += expect_true(expiry_plan.expiry_plan_safe == 1 &&
                           expiry_plan.expiry_text_login_fallback == 1 &&
                           expiry_plan.action_allowed == 0 &&
                           expiry_plan.action_blocked == 1,
                       "credential expiry plan unknown should force text login fallback");
  fails += expect_true(strings_equal(expiry_plan.expiry_ticket,
                                     "text-login-fallback-expiry-ticket") &&
                           strings_equal(expiry_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential expiry plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_expiry_plan_fails_closed_for_unsafe_or_missing_retention_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_retention_plan retention_plan;
  struct login_window_credential_screen_expiry_plan expiry_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_expiry_plan_build(
                           NULL, &expiry_plan) == 0,
                       "credential expiry plan missing retention plan should build fail-closed state");
  fails += expect_true(expiry_plan.retention_plan_available == 0 &&
                           expiry_plan.retention_plan_safe == 0 &&
                           expiry_plan.expiry_plan_safe == 0 &&
                           expiry_plan.route_selected == 0 &&
                           expiry_plan.route_blocked == 1,
                       "credential expiry plan missing retention plan should block expiry plan");
  fails += expect_true(expiry_plan.expiry_allowed == 0 &&
                           expiry_plan.expiry_submitted == 0 &&
                           expiry_plan.expiry_persisted == 0 &&
                           expiry_plan.expiry_timer_armed == 0 &&
                           expiry_plan.expiry_deleted == 0 &&
                           expiry_plan.retention_submitted == 0 &&
                           expiry_plan.expiry_text_login_fallback == 1 &&
                           expiry_plan.submit_enabled == 0 &&
                           expiry_plan.auth_attempt_allowed == 0,
                       "credential expiry plan missing retention plan must stay redacted");
  fails += expect_true(strings_equal(expiry_plan.expiry_ticket,
                                     "text-login-fallback-expiry-ticket") &&
                           strings_equal(expiry_plan.event_type,
                                         "credential-screen-expiry-plan-unavailable") &&
                           strings_equal(expiry_plan.blocked_reason,
                                         "retention-plan-unavailable"),
                       "credential expiry plan missing retention plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_retention_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &retention_plan) == 0,
                       "credential expiry plan unsafe retention source should build");
  fails += expect_true(login_window_credential_screen_expiry_plan_build(
                           &retention_plan, &expiry_plan) == 0,
                       "credential expiry plan unsafe retention plan should build blocked state");
  fails += expect_true(expiry_plan.retention_plan_available == 1 &&
                           expiry_plan.retention_plan_safe == 0 &&
                           expiry_plan.expiry_plan_safe == 0 &&
                           expiry_plan.route_selected == 0 &&
                           expiry_plan.route_blocked == 1,
                       "credential expiry plan unsafe retention plan should block expiry plan");
  fails += expect_true(expiry_plan.expiry_allowed == 0 &&
                           expiry_plan.expiry_submitted == 0 &&
                           expiry_plan.expiry_text_login_fallback == 1 &&
                           expiry_plan.submit_enabled == 0 &&
                           expiry_plan.auth_attempt_allowed == 0,
                       "credential expiry plan unsafe retention plan must force text login fallback");
  fails += expect_true(strings_equal(expiry_plan.expiry_ticket,
                                     "text-login-fallback-expiry-ticket") &&
                           strings_equal(expiry_plan.event_type,
                                         "credential-screen-expiry-plan-unsafe") &&
                           strings_equal(expiry_plan.blocked_reason,
                                         "credential-expiry-plan-unsafe"),
                       "credential expiry plan unsafe retention plan should force text login");

  fails += expect_true(build_loginwindow_credential_screen_retention_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'y', 0, 0, 0,
                           1, &retention_plan) == 0,
                       "credential expiry plan submitted retention source should build");
  retention_plan.retention_submitted = 1;
  retention_plan.retention_persist_allowed = 1;
  retention_plan.retention_persisted = 1;
  retention_plan.retention_cpu_gpu_sync_allowed = 1;
  retention_plan.retention_cpu_gpu_sync_submitted = 1;
  retention_plan.archive_submitted = 1;
  retention_plan.archive_persist_allowed = 1;
  retention_plan.archive_persisted = 1;
  retention_plan.archive_cpu_gpu_sync_allowed = 1;
  retention_plan.archive_cpu_gpu_sync_submitted = 1;
  retention_plan.journal_submitted = 1;
  retention_plan.journal_persist_allowed = 1;
  retention_plan.journal_persisted = 1;
  retention_plan.ledger_submitted = 1;
  retention_plan.ledger_persist_allowed = 1;
  retention_plan.ledger_persisted = 1;
  retention_plan.receipt_submitted = 1;
  retention_plan.receipt_persist_allowed = 1;
  retention_plan.receipt_persisted = 1;
  retention_plan.record_submitted = 1;
  retention_plan.record_persisted = 1;
  retention_plan.audit_submitted = 1;
  retention_plan.audit_log_appended = 1;
  retention_plan.seal_submitted = 1;
  retention_plan.cleanup_submitted = 1;
  retention_plan.retire_submitted = 1;
  retention_plan.ack_submitted = 1;
  retention_plan.completion_reported = 1;
  retention_plan.deadline_armed = 1;
  retention_plan.page_flip_allowed = 1;
  fails += expect_true(login_window_credential_screen_expiry_plan_build(
                           &retention_plan, &expiry_plan) == 0,
                       "credential expiry plan submitted retention should fail closed");
  fails += expect_true(expiry_plan.expiry_plan_safe == 0 &&
                           expiry_plan.expiry_allowed == 0 &&
                           expiry_plan.expiry_submitted == 0 &&
                           expiry_plan.expiry_persist_allowed == 0 &&
                           expiry_plan.expiry_persisted == 0 &&
                           expiry_plan.expiry_cpu_gpu_sync_allowed == 0 &&
                           expiry_plan.expiry_cpu_gpu_sync_submitted == 0 &&
                           expiry_plan.expiry_timer_allowed == 0 &&
                           expiry_plan.expiry_timer_armed == 0 &&
                           expiry_plan.expiry_delete_allowed == 0 &&
                           expiry_plan.expiry_deleted == 0 &&
                           expiry_plan.retention_submitted == 0 &&
                           expiry_plan.retention_persist_allowed == 0 &&
                           expiry_plan.retention_persisted == 0 &&
                           expiry_plan.retention_cpu_gpu_sync_allowed == 0 &&
                           expiry_plan.retention_cpu_gpu_sync_submitted == 0 &&
                           expiry_plan.archive_submitted == 0 &&
                           expiry_plan.archive_persist_allowed == 0 &&
                           expiry_plan.archive_persisted == 0 &&
                           expiry_plan.archive_cpu_gpu_sync_allowed == 0 &&
                           expiry_plan.archive_cpu_gpu_sync_submitted == 0 &&
                           expiry_plan.journal_submitted == 0 &&
                           expiry_plan.journal_persist_allowed == 0 &&
                           expiry_plan.journal_persisted == 0 &&
                           expiry_plan.ledger_submitted == 0 &&
                           expiry_plan.ledger_persist_allowed == 0 &&
                           expiry_plan.ledger_persisted == 0 &&
                           expiry_plan.receipt_submitted == 0 &&
                           expiry_plan.receipt_persist_allowed == 0 &&
                           expiry_plan.receipt_persisted == 0 &&
                           expiry_plan.record_submitted == 0 &&
                           expiry_plan.record_persisted == 0 &&
                           expiry_plan.audit_submitted == 0 &&
                           expiry_plan.audit_log_appended == 0 &&
                           expiry_plan.seal_submitted == 0 &&
                           expiry_plan.cleanup_submitted == 0 &&
                           expiry_plan.retire_submitted == 0 &&
                           expiry_plan.ack_submitted == 0 &&
                           expiry_plan.completion_reported == 0 &&
                           expiry_plan.deadline_armed == 0 &&
                           expiry_plan.page_flip_allowed == 0 &&
                           expiry_plan.page_flip_submitted == 0 &&
                           expiry_plan.submit_enabled == 0 &&
                           expiry_plan.auth_attempt_allowed == 0,
                       "credential expiry plan must not copy unsafe submitted retention state");
  return fails;
}

int test_login_runtime_credential_expiry_plan_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_expiry_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_expiry_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_expiry_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_expiry_plan_fails_closed_for_unsafe_or_missing_retention_plan();
  return fails;
}
