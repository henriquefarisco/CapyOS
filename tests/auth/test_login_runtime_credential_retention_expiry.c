/*
 * tests/auth/test_login_runtime_credential_retention_expiry.c
 *
 * Credential screen retention plan coverage for the `login_runtime`
 * host test. Originally carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-16 monolith
 * refactor (PR D.28 of the Estagio D dedicated plan), and further
 * split at the 2026-05-16 preventive refactor (the 4 expiry plan
 * tests + the `build_loginwindow_credential_screen_expiry_plan_for_action`
 * helper moved to the sibling
 * `tests/auth/test_login_runtime_credential_expiry_plan.c`) so
 * each host-test translation unit stays comfortably below the
 * 900-line layout limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_retention_plan_build`: 4 tests
 *     covering the credential widgets retention + the text-route
 *     retention (recovery + resume) + the submit/unknown fallback
 *     retention + the missing-or-unsafe archive plan fail-closed
 *     default.
 *
 * Also exposes the shared helper
 * `build_loginwindow_credential_screen_retention_plan_for_action`,
 * used by `test_login_runtime_credential_expiry_plan.c` (via
 * internal header) and by later companion files that chain on top
 * of the retention stage.
 *
 * The companion entry `test_login_runtime_credential_retention_expiry_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`, which also invokes the new
 * sibling entry `test_login_runtime_credential_expiry_plan_cases`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_retention_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_retention_plan *retention_plan) {
  struct login_window_credential_screen_archive_plan archive_plan;

  if (build_loginwindow_credential_screen_archive_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &archive_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_retention_plan_build(&archive_plan,
                                                             retention_plan);
}

static int test_loginwindow_credential_screen_retention_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_retention_plan retention_plan;

  fails += expect_true(build_loginwindow_credential_screen_retention_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'a', 0, 0, 0,
                           1, &retention_plan) == 0,
                       "credential retention plan edit should build");
  fails += expect_true(retention_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_RETENTION_PLAN_VERSION,
                       "credential retention plan should expose stable version");
  fails += expect_true(retention_plan.archive_plan_available == 1 &&
                           retention_plan.archive_plan_safe == 1 &&
                           retention_plan.retention_plan_safe == 1,
                       "credential retention plan should require safe archive plan");
  fails += expect_true(retention_plan.retention_required == 1 &&
                           retention_plan.retention_allowed == 1 &&
                           retention_plan.retention_submitted == 0 &&
                           retention_plan.retention_ticket_selected == 1 &&
                           retention_plan.retention_target_selected == 1 &&
                           retention_plan.retention_persist_allowed == 0 &&
                           retention_plan.retention_persisted == 0 &&
                           retention_plan.retention_cpu_gpu_sync_allowed == 0 &&
                           retention_plan.retention_cpu_gpu_sync_submitted == 0,
                       "credential retention plan should remain declarative");
  fails += expect_true(retention_plan.archive_submitted == 0 &&
                           retention_plan.archive_persisted == 0 &&
                           retention_plan.journal_submitted == 0 &&
                           retention_plan.journal_persisted == 0 &&
                           retention_plan.ledger_submitted == 0 &&
                           retention_plan.ledger_persisted == 0 &&
                           retention_plan.receipt_submitted == 0 &&
                           retention_plan.receipt_persisted == 0 &&
                           retention_plan.record_submitted == 0 &&
                           retention_plan.record_persisted == 0 &&
                           retention_plan.audit_submitted == 0 &&
                           retention_plan.audit_log_appended == 0 &&
                           retention_plan.seal_submitted == 0 &&
                           retention_plan.cleanup_submitted == 0 &&
                           retention_plan.retire_submitted == 0 &&
                           retention_plan.ack_submitted == 0 &&
                           retention_plan.completion_reported == 0 &&
                           retention_plan.deadline_armed == 0 &&
                           retention_plan.sync_submitted == 0 &&
                           retention_plan.timeline_submitted == 0 &&
                           retention_plan.fence_submitted == 0 &&
                           retention_plan.barrier_submitted == 0 &&
                           retention_plan.flush_submitted == 0 &&
                           retention_plan.framebuffer_written == 0 &&
                           retention_plan.blit_pixels_copied == 0 &&
                           retention_plan.output_submitted == 0 &&
                           retention_plan.display_mode_committed == 0 &&
                           retention_plan.page_flip_submitted == 0,
                       "credential retention plan must not execute GUI work");
  fails += expect_true(retention_plan.retention_credential_panel == 1 &&
                           retention_plan.retention_credential_input == 1 &&
                           retention_plan.retention_credential_focus == 1,
                       "credential retention plan should mark credential widgets");
  fails += expect_true(retention_plan.submit_callback_bound == 0 &&
                           retention_plan.auth_callback_bound == 0 &&
                           retention_plan.submit_enabled == 0 &&
                           retention_plan.auth_attempt_allowed == 0 &&
                           retention_plan.raw_secret_exposed == 0 &&
                           retention_plan.masked_text_exposed == 0 &&
                           retention_plan.length_redacted == 1,
                       "credential retention plan must stay redacted and auth-disabled");
  fails += expect_true(strings_equal(retention_plan.retention_ticket,
                                     "credential-screen-retention-ticket") &&
                           strings_equal(retention_plan.archive_ticket,
                                         "credential-screen-archive-ticket") &&
                           strings_equal(retention_plan.retention_policy,
                                         "declarative-retention-no-persist") &&
                           strings_equal(retention_plan.state,
                                         "retention-credential-ready"),
                       "credential retention plan should report retention ticket");
  return fails;
}

static int test_loginwindow_credential_screen_retention_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_retention_plan retention_plan;

  fails += expect_true(build_loginwindow_credential_screen_retention_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &retention_plan) == 0,
                       "credential retention plan recovery should build");
  fails += expect_true(retention_plan.retention_plan_safe == 1 &&
                           retention_plan.retention_allowed == 1 &&
                           retention_plan.retention_submitted == 0 &&
                           retention_plan.retention_persisted == 0 &&
                           retention_plan.retention_text_recovery == 1 &&
                           retention_plan.retention_text_login == 1 &&
                           retention_plan.retention_credential_focus == 0,
                       "credential retention plan recovery should mark text recovery");
  fails += expect_true(retention_plan.archive_submitted == 0 &&
                           retention_plan.journal_submitted == 0 &&
                           retention_plan.ledger_submitted == 0 &&
                           retention_plan.receipt_submitted == 0 &&
                           retention_plan.record_submitted == 0 &&
                           retention_plan.audit_submitted == 0 &&
                           retention_plan.audit_log_appended == 0 &&
                           retention_plan.seal_submitted == 0 &&
                           retention_plan.cleanup_submitted == 0 &&
                           retention_plan.retire_submitted == 0 &&
                           retention_plan.ack_submitted == 0 &&
                           retention_plan.completion_reported == 0 &&
                           retention_plan.deadline_armed == 0 &&
                           retention_plan.sync_submitted == 0 &&
                           retention_plan.timeline_submitted == 0 &&
                           retention_plan.fence_submitted == 0 &&
                           retention_plan.barrier_submitted == 0 &&
                           retention_plan.flush_submitted == 0 &&
                           retention_plan.framebuffer_written == 0 &&
                           retention_plan.output_submitted == 0 &&
                           retention_plan.display_mode_committed == 0 &&
                           retention_plan.page_flip_submitted == 0 &&
                           retention_plan.submit_enabled == 0 &&
                           retention_plan.auth_attempt_allowed == 0,
                       "credential retention plan recovery must not persist or output");
  fails += expect_true(strings_equal(retention_plan.retention_ticket,
                                     "text-recovery-retention-ticket") &&
                           strings_equal(retention_plan.compositor_target,
                                         "text-recovery-retention") &&
                           strings_equal(retention_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential retention plan recovery should report recovery ticket");

  fails += expect_true(build_loginwindow_credential_screen_retention_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &retention_plan) == 0,
                       "credential retention plan resume should build");
  fails += expect_true(retention_plan.retention_plan_safe == 1 &&
                           retention_plan.retention_text_login_resume == 1 &&
                           retention_plan.session_reset_required == 1 &&
                           retention_plan.login_screen_rerender_required == 1 &&
                           retention_plan.retention_submitted == 0 &&
                           retention_plan.retention_persisted == 0 &&
                           retention_plan.archive_submitted == 0 &&
                           retention_plan.submit_enabled == 0 &&
                           retention_plan.auth_attempt_allowed == 0,
                       "credential retention plan resume should keep GUI auth disabled");
  fails += expect_true(strings_equal(retention_plan.retention_ticket,
                                     "text-login-resume-retention-ticket") &&
                           strings_equal(retention_plan.retention_policy,
                                         "full-retention-declarative") &&
                           strings_equal(retention_plan.state,
                                         "retention-resume-ready"),
                       "credential retention plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_retention_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_retention_plan retention_plan;

  fails += expect_true(build_loginwindow_credential_screen_retention_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &retention_plan) == 0,
                       "credential retention plan submit should build");
  fails += expect_true(retention_plan.retention_plan_safe == 1 &&
                           retention_plan.submit_requested == 1 &&
                           retention_plan.retention_text_login_fallback == 1 &&
                           retention_plan.action_allowed == 0 &&
                           retention_plan.action_blocked == 1 &&
                           retention_plan.input_focus_allowed == 0,
                       "credential retention plan submit should force text login fallback");
  fails += expect_true(retention_plan.retention_submitted == 0 &&
                           retention_plan.retention_persist_allowed == 0 &&
                           retention_plan.retention_persisted == 0 &&
                           retention_plan.archive_submitted == 0 &&
                           retention_plan.archive_persisted == 0 &&
                           retention_plan.journal_submitted == 0 &&
                           retention_plan.journal_persisted == 0 &&
                           retention_plan.ledger_submitted == 0 &&
                           retention_plan.ledger_persisted == 0 &&
                           retention_plan.receipt_submitted == 0 &&
                           retention_plan.receipt_persisted == 0 &&
                           retention_plan.record_submitted == 0 &&
                           retention_plan.audit_submitted == 0 &&
                           retention_plan.audit_log_appended == 0 &&
                           retention_plan.seal_submitted == 0 &&
                           retention_plan.cleanup_submitted == 0 &&
                           retention_plan.retire_submitted == 0 &&
                           retention_plan.ack_submitted == 0 &&
                           retention_plan.completion_reported == 0 &&
                           retention_plan.deadline_armed == 0 &&
                           retention_plan.sync_submitted == 0 &&
                           retention_plan.timeline_submitted == 0 &&
                           retention_plan.fence_submitted == 0 &&
                           retention_plan.barrier_submitted == 0 &&
                           retention_plan.flush_submitted == 0 &&
                           retention_plan.framebuffer_written == 0 &&
                           retention_plan.blit_pixels_copied == 0 &&
                           retention_plan.output_submitted == 0 &&
                           retention_plan.display_submitted == 0 &&
                           retention_plan.page_flip_submitted == 0 &&
                           retention_plan.submit_callback_bound == 0 &&
                           retention_plan.auth_callback_bound == 0 &&
                           retention_plan.submit_enabled == 0 &&
                           retention_plan.auth_attempt_allowed == 0,
                       "credential retention plan submit must stay declarative");
  fails += expect_true(strings_equal(retention_plan.retention_ticket,
                                     "text-login-fallback-retention-ticket") &&
                           strings_equal(retention_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(retention_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential retention plan submit should explain fallback");

  fails += expect_true(build_loginwindow_credential_screen_retention_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &retention_plan) == 0,
                       "credential retention plan unknown should build");
  fails += expect_true(retention_plan.retention_plan_safe == 1 &&
                           retention_plan.retention_text_login_fallback == 1 &&
                           retention_plan.action_allowed == 0 &&
                           retention_plan.action_blocked == 1,
                       "credential retention plan unknown should force text login fallback");
  fails += expect_true(strings_equal(retention_plan.retention_ticket,
                                     "text-login-fallback-retention-ticket") &&
                           strings_equal(retention_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential retention plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_retention_plan_fails_closed_for_unsafe_or_missing_archive_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_archive_plan archive_plan;
  struct login_window_credential_screen_retention_plan retention_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_retention_plan_build(
                           NULL, &retention_plan) == 0,
                       "credential retention plan missing archive plan should build fail-closed state");
  fails += expect_true(retention_plan.archive_plan_available == 0 &&
                           retention_plan.archive_plan_safe == 0 &&
                           retention_plan.retention_plan_safe == 0 &&
                           retention_plan.route_selected == 0 &&
                           retention_plan.route_blocked == 1,
                       "credential retention plan missing archive plan should block retention plan");
  fails += expect_true(retention_plan.retention_allowed == 0 &&
                           retention_plan.retention_submitted == 0 &&
                           retention_plan.retention_persisted == 0 &&
                           retention_plan.archive_submitted == 0 &&
                           retention_plan.retention_text_login_fallback == 1 &&
                           retention_plan.submit_enabled == 0 &&
                           retention_plan.auth_attempt_allowed == 0,
                       "credential retention plan missing archive plan must stay redacted");
  fails += expect_true(strings_equal(retention_plan.retention_ticket,
                                     "text-login-fallback-retention-ticket") &&
                           strings_equal(retention_plan.event_type,
                                         "credential-screen-retention-plan-unavailable") &&
                           strings_equal(retention_plan.blocked_reason,
                                         "archive-plan-unavailable"),
                       "credential retention plan missing archive plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_archive_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &archive_plan) == 0,
                       "credential retention plan unsafe archive source should build");
  fails += expect_true(login_window_credential_screen_retention_plan_build(
                           &archive_plan, &retention_plan) == 0,
                       "credential retention plan unsafe archive plan should build blocked state");
  fails += expect_true(retention_plan.archive_plan_available == 1 &&
                           retention_plan.archive_plan_safe == 0 &&
                           retention_plan.retention_plan_safe == 0 &&
                           retention_plan.route_selected == 0 &&
                           retention_plan.route_blocked == 1,
                       "credential retention plan unsafe archive plan should block retention plan");
  fails += expect_true(retention_plan.retention_allowed == 0 &&
                           retention_plan.retention_submitted == 0 &&
                           retention_plan.retention_text_login_fallback == 1 &&
                           retention_plan.submit_enabled == 0 &&
                           retention_plan.auth_attempt_allowed == 0,
                       "credential retention plan unsafe archive plan must force text login fallback");
  fails += expect_true(strings_equal(retention_plan.retention_ticket,
                                     "text-login-fallback-retention-ticket") &&
                           strings_equal(retention_plan.event_type,
                                         "credential-screen-retention-plan-unsafe") &&
                           strings_equal(retention_plan.blocked_reason,
                                         "credential-retention-plan-unsafe"),
                       "credential retention plan unsafe archive plan should force text login");

  fails += expect_true(build_loginwindow_credential_screen_archive_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'y', 0, 0, 0,
                           1, &archive_plan) == 0,
                       "credential retention plan submitted archive source should build");
  archive_plan.archive_submitted = 1;
  archive_plan.archive_persist_allowed = 1;
  archive_plan.archive_persisted = 1;
  archive_plan.archive_cpu_gpu_sync_allowed = 1;
  archive_plan.archive_cpu_gpu_sync_submitted = 1;
  archive_plan.journal_submitted = 1;
  archive_plan.journal_persist_allowed = 1;
  archive_plan.journal_persisted = 1;
  archive_plan.ledger_submitted = 1;
  archive_plan.ledger_persist_allowed = 1;
  archive_plan.ledger_persisted = 1;
  archive_plan.receipt_submitted = 1;
  archive_plan.receipt_persist_allowed = 1;
  archive_plan.receipt_persisted = 1;
  archive_plan.record_submitted = 1;
  archive_plan.record_persisted = 1;
  archive_plan.audit_submitted = 1;
  archive_plan.audit_log_appended = 1;
  archive_plan.seal_submitted = 1;
  archive_plan.cleanup_submitted = 1;
  archive_plan.retire_submitted = 1;
  archive_plan.ack_submitted = 1;
  archive_plan.completion_reported = 1;
  archive_plan.deadline_armed = 1;
  archive_plan.page_flip_allowed = 1;
  fails += expect_true(login_window_credential_screen_retention_plan_build(
                           &archive_plan, &retention_plan) == 0,
                       "credential retention plan submitted archive should fail closed");
  fails += expect_true(retention_plan.retention_plan_safe == 0 &&
                           retention_plan.retention_allowed == 0 &&
                           retention_plan.retention_submitted == 0 &&
                           retention_plan.retention_persist_allowed == 0 &&
                           retention_plan.retention_persisted == 0 &&
                           retention_plan.retention_cpu_gpu_sync_allowed == 0 &&
                           retention_plan.retention_cpu_gpu_sync_submitted == 0 &&
                           retention_plan.archive_submitted == 0 &&
                           retention_plan.archive_persist_allowed == 0 &&
                           retention_plan.archive_persisted == 0 &&
                           retention_plan.archive_cpu_gpu_sync_allowed == 0 &&
                           retention_plan.archive_cpu_gpu_sync_submitted == 0 &&
                           retention_plan.journal_submitted == 0 &&
                           retention_plan.journal_persist_allowed == 0 &&
                           retention_plan.journal_persisted == 0 &&
                           retention_plan.ledger_submitted == 0 &&
                           retention_plan.ledger_persist_allowed == 0 &&
                           retention_plan.ledger_persisted == 0 &&
                           retention_plan.receipt_submitted == 0 &&
                           retention_plan.receipt_persist_allowed == 0 &&
                           retention_plan.receipt_persisted == 0 &&
                           retention_plan.record_submitted == 0 &&
                           retention_plan.record_persisted == 0 &&
                           retention_plan.audit_submitted == 0 &&
                           retention_plan.audit_log_appended == 0 &&
                           retention_plan.seal_submitted == 0 &&
                           retention_plan.cleanup_submitted == 0 &&
                           retention_plan.retire_submitted == 0 &&
                           retention_plan.ack_submitted == 0 &&
                           retention_plan.completion_reported == 0 &&
                           retention_plan.deadline_armed == 0 &&
                           retention_plan.page_flip_allowed == 0 &&
                           retention_plan.page_flip_submitted == 0 &&
                           retention_plan.submit_enabled == 0 &&
                           retention_plan.auth_attempt_allowed == 0,
                       "credential retention plan must not copy unsafe submitted archive state");
  return fails;
}

int test_login_runtime_credential_retention_expiry_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_retention_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_retention_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_retention_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_retention_plan_fails_closed_for_unsafe_or_missing_archive_plan();
  return fails;
}
