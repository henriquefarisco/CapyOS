/*
 * tests/auth/test_login_runtime_credential_journal_archive.c
 *
 * Credential screen journal plan + archive plan coverage for the
 * `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-16 monolith
 * refactor (PR D.27 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_journal_plan_build`: 4 tests
 *     covering the credential widgets journal + the text-route
 *     journal (recovery + resume) + the submit/unknown fallback
 *     journal + the missing-or-unsafe ledger plan fail-closed
 *     default.
 *   - `login_window_credential_screen_archive_plan_build`: 4 tests
 *     covering the credential widgets archive + the text-route
 *     archive (recovery + resume) + the submit/unknown fallback
 *     archive + the missing-or-unsafe journal plan fail-closed
 *     default.
 *
 * Also exposes shared helpers
 * `build_loginwindow_credential_screen_journal_plan_for_action` and
 * `build_loginwindow_credential_screen_archive_plan_for_action`,
 * used by later companion files that chain on top of the
 * journal/archive stages (retention, ...).
 *
 * The companion entry `test_login_runtime_credential_journal_archive_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_journal_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_journal_plan *journal_plan) {
  struct login_window_credential_screen_ledger_plan ledger_plan;

  if (build_loginwindow_credential_screen_ledger_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &ledger_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_journal_plan_build(&ledger_plan,
                                                           journal_plan);
}

static int test_loginwindow_credential_screen_journal_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_journal_plan journal_plan;

  fails += expect_true(build_loginwindow_credential_screen_journal_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'j', 0, 0, 0,
                           1, &journal_plan) == 0,
                       "credential journal plan edit should build");
  fails += expect_true(journal_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_JOURNAL_PLAN_VERSION,
                       "credential journal plan should expose stable version");
  fails += expect_true(journal_plan.ledger_plan_available == 1 &&
                           journal_plan.ledger_plan_safe == 1 &&
                           journal_plan.journal_plan_safe == 1,
                       "credential journal plan should require safe ledger plan");
  fails += expect_true(journal_plan.journal_required == 1 &&
                           journal_plan.journal_allowed == 1 &&
                           journal_plan.journal_submitted == 0 &&
                           journal_plan.journal_ticket_selected == 1 &&
                           journal_plan.journal_target_selected == 1 &&
                           journal_plan.journal_persist_allowed == 0 &&
                           journal_plan.journal_persisted == 0 &&
                           journal_plan.journal_cpu_gpu_sync_allowed == 0 &&
                           journal_plan.journal_cpu_gpu_sync_submitted == 0,
                       "credential journal plan should remain declarative");
  fails += expect_true(journal_plan.ledger_submitted == 0 &&
                           journal_plan.ledger_persisted == 0 &&
                           journal_plan.receipt_submitted == 0 &&
                           journal_plan.receipt_persisted == 0 &&
                           journal_plan.record_submitted == 0 &&
                           journal_plan.record_persisted == 0 &&
                           journal_plan.audit_submitted == 0 &&
                           journal_plan.audit_log_appended == 0 &&
                           journal_plan.seal_submitted == 0 &&
                           journal_plan.cleanup_submitted == 0 &&
                           journal_plan.retire_submitted == 0 &&
                           journal_plan.ack_submitted == 0 &&
                           journal_plan.completion_reported == 0 &&
                           journal_plan.deadline_armed == 0 &&
                           journal_plan.sync_submitted == 0 &&
                           journal_plan.timeline_submitted == 0 &&
                           journal_plan.fence_submitted == 0 &&
                           journal_plan.barrier_submitted == 0 &&
                           journal_plan.flush_submitted == 0 &&
                           journal_plan.framebuffer_written == 0 &&
                           journal_plan.blit_pixels_copied == 0 &&
                           journal_plan.output_submitted == 0 &&
                           journal_plan.display_mode_committed == 0 &&
                           journal_plan.page_flip_submitted == 0,
                       "credential journal plan must not execute GUI work");
  fails += expect_true(journal_plan.journal_credential_panel == 1 &&
                           journal_plan.journal_credential_input == 1 &&
                           journal_plan.journal_credential_focus == 1,
                       "credential journal plan should mark credential widgets");
  fails += expect_true(journal_plan.submit_callback_bound == 0 &&
                           journal_plan.auth_callback_bound == 0 &&
                           journal_plan.submit_enabled == 0 &&
                           journal_plan.auth_attempt_allowed == 0 &&
                           journal_plan.raw_secret_exposed == 0 &&
                           journal_plan.masked_text_exposed == 0 &&
                           journal_plan.length_redacted == 1,
                       "credential journal plan must stay redacted and auth-disabled");
  fails += expect_true(strings_equal(journal_plan.journal_ticket,
                                     "credential-screen-journal-ticket") &&
                           strings_equal(journal_plan.ledger_ticket,
                                         "credential-screen-ledger-ticket") &&
                           strings_equal(journal_plan.journal_policy,
                                         "declarative-journal-no-persist") &&
                           strings_equal(journal_plan.state,
                                         "journal-credential-ready"),
                       "credential journal plan should report journal ticket");
  return fails;
}

static int test_loginwindow_credential_screen_journal_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_journal_plan journal_plan;

  fails += expect_true(build_loginwindow_credential_screen_journal_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &journal_plan) == 0,
                       "credential journal plan recovery should build");
  fails += expect_true(journal_plan.journal_plan_safe == 1 &&
                           journal_plan.journal_allowed == 1 &&
                           journal_plan.journal_submitted == 0 &&
                           journal_plan.journal_persisted == 0 &&
                           journal_plan.journal_text_recovery == 1 &&
                           journal_plan.journal_text_login == 1 &&
                           journal_plan.journal_credential_focus == 0,
                       "credential journal plan recovery should mark text recovery");
  fails += expect_true(journal_plan.ledger_submitted == 0 &&
                           journal_plan.receipt_submitted == 0 &&
                           journal_plan.record_submitted == 0 &&
                           journal_plan.audit_submitted == 0 &&
                           journal_plan.audit_log_appended == 0 &&
                           journal_plan.seal_submitted == 0 &&
                           journal_plan.cleanup_submitted == 0 &&
                           journal_plan.retire_submitted == 0 &&
                           journal_plan.ack_submitted == 0 &&
                           journal_plan.completion_reported == 0 &&
                           journal_plan.deadline_armed == 0 &&
                           journal_plan.sync_submitted == 0 &&
                           journal_plan.timeline_submitted == 0 &&
                           journal_plan.fence_submitted == 0 &&
                           journal_plan.barrier_submitted == 0 &&
                           journal_plan.flush_submitted == 0 &&
                           journal_plan.framebuffer_written == 0 &&
                           journal_plan.output_submitted == 0 &&
                           journal_plan.display_mode_committed == 0 &&
                           journal_plan.page_flip_submitted == 0 &&
                           journal_plan.submit_enabled == 0 &&
                           journal_plan.auth_attempt_allowed == 0,
                       "credential journal plan recovery must not persist or output");
  fails += expect_true(strings_equal(journal_plan.journal_ticket,
                                     "text-recovery-journal-ticket") &&
                           strings_equal(journal_plan.compositor_target,
                                         "text-recovery-journal") &&
                           strings_equal(journal_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential journal plan recovery should report recovery ticket");

  fails += expect_true(build_loginwindow_credential_screen_journal_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &journal_plan) == 0,
                       "credential journal plan resume should build");
  fails += expect_true(journal_plan.journal_plan_safe == 1 &&
                           journal_plan.journal_text_login_resume == 1 &&
                           journal_plan.session_reset_required == 1 &&
                           journal_plan.login_screen_rerender_required == 1 &&
                           journal_plan.journal_submitted == 0 &&
                           journal_plan.journal_persisted == 0 &&
                           journal_plan.ledger_submitted == 0 &&
                           journal_plan.submit_enabled == 0 &&
                           journal_plan.auth_attempt_allowed == 0,
                       "credential journal plan resume should keep GUI auth disabled");
  fails += expect_true(strings_equal(journal_plan.journal_ticket,
                                     "text-login-resume-journal-ticket") &&
                           strings_equal(journal_plan.journal_policy,
                                         "full-journal-declarative") &&
                           strings_equal(journal_plan.state,
                                         "journal-resume-ready"),
                       "credential journal plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_journal_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_journal_plan journal_plan;

  fails += expect_true(build_loginwindow_credential_screen_journal_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &journal_plan) == 0,
                       "credential journal plan submit should build");
  fails += expect_true(journal_plan.journal_plan_safe == 1 &&
                           journal_plan.submit_requested == 1 &&
                           journal_plan.journal_text_login_fallback == 1 &&
                           journal_plan.action_allowed == 0 &&
                           journal_plan.action_blocked == 1 &&
                           journal_plan.input_focus_allowed == 0,
                       "credential journal plan submit should force text login fallback");
  fails += expect_true(journal_plan.journal_submitted == 0 &&
                           journal_plan.journal_persist_allowed == 0 &&
                           journal_plan.journal_persisted == 0 &&
                           journal_plan.ledger_submitted == 0 &&
                           journal_plan.ledger_persisted == 0 &&
                           journal_plan.receipt_submitted == 0 &&
                           journal_plan.receipt_persisted == 0 &&
                           journal_plan.record_submitted == 0 &&
                           journal_plan.audit_submitted == 0 &&
                           journal_plan.audit_log_appended == 0 &&
                           journal_plan.seal_submitted == 0 &&
                           journal_plan.cleanup_submitted == 0 &&
                           journal_plan.retire_submitted == 0 &&
                           journal_plan.ack_submitted == 0 &&
                           journal_plan.completion_reported == 0 &&
                           journal_plan.deadline_armed == 0 &&
                           journal_plan.sync_submitted == 0 &&
                           journal_plan.timeline_submitted == 0 &&
                           journal_plan.fence_submitted == 0 &&
                           journal_plan.barrier_submitted == 0 &&
                           journal_plan.flush_submitted == 0 &&
                           journal_plan.framebuffer_written == 0 &&
                           journal_plan.blit_pixels_copied == 0 &&
                           journal_plan.output_submitted == 0 &&
                           journal_plan.display_submitted == 0 &&
                           journal_plan.page_flip_submitted == 0 &&
                           journal_plan.submit_callback_bound == 0 &&
                           journal_plan.auth_callback_bound == 0 &&
                           journal_plan.submit_enabled == 0 &&
                           journal_plan.auth_attempt_allowed == 0,
                       "credential journal plan submit must stay declarative");
  fails += expect_true(strings_equal(journal_plan.journal_ticket,
                                     "text-login-fallback-journal-ticket") &&
                           strings_equal(journal_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(journal_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential journal plan submit should explain fallback");

  fails += expect_true(build_loginwindow_credential_screen_journal_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &journal_plan) == 0,
                       "credential journal plan unknown should build");
  fails += expect_true(journal_plan.journal_plan_safe == 1 &&
                           journal_plan.journal_text_login_fallback == 1 &&
                           journal_plan.action_allowed == 0 &&
                           journal_plan.action_blocked == 1,
                       "credential journal plan unknown should force text login fallback");
  fails += expect_true(strings_equal(journal_plan.journal_ticket,
                                     "text-login-fallback-journal-ticket") &&
                           strings_equal(journal_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential journal plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_journal_plan_fails_closed_for_unsafe_or_missing_ledger_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_ledger_plan ledger_plan;
  struct login_window_credential_screen_journal_plan journal_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_journal_plan_build(
                           NULL, &journal_plan) == 0,
                       "credential journal plan missing ledger plan should build fail-closed state");
  fails += expect_true(journal_plan.ledger_plan_available == 0 &&
                           journal_plan.ledger_plan_safe == 0 &&
                           journal_plan.journal_plan_safe == 0 &&
                           journal_plan.route_selected == 0 &&
                           journal_plan.route_blocked == 1,
                       "credential journal plan missing ledger plan should block journal plan");
  fails += expect_true(journal_plan.journal_allowed == 0 &&
                           journal_plan.journal_submitted == 0 &&
                           journal_plan.journal_persisted == 0 &&
                           journal_plan.ledger_submitted == 0 &&
                           journal_plan.journal_text_login_fallback == 1 &&
                           journal_plan.submit_enabled == 0 &&
                           journal_plan.auth_attempt_allowed == 0,
                       "credential journal plan missing ledger plan must stay redacted");
  fails += expect_true(strings_equal(journal_plan.journal_ticket,
                                     "text-login-fallback-journal-ticket") &&
                           strings_equal(journal_plan.event_type,
                                         "credential-screen-journal-plan-unavailable") &&
                           strings_equal(journal_plan.blocked_reason,
                                         "ledger-plan-unavailable"),
                       "credential journal plan missing ledger plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_ledger_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &ledger_plan) == 0,
                       "credential journal plan unsafe ledger source should build");
  fails += expect_true(login_window_credential_screen_journal_plan_build(
                           &ledger_plan, &journal_plan) == 0,
                       "credential journal plan unsafe ledger plan should build blocked state");
  fails += expect_true(journal_plan.ledger_plan_available == 1 &&
                           journal_plan.ledger_plan_safe == 0 &&
                           journal_plan.journal_plan_safe == 0 &&
                           journal_plan.route_selected == 0 &&
                           journal_plan.route_blocked == 1,
                       "credential journal plan unsafe ledger plan should block journal plan");
  fails += expect_true(journal_plan.journal_allowed == 0 &&
                           journal_plan.journal_submitted == 0 &&
                           journal_plan.journal_text_login_fallback == 1 &&
                           journal_plan.submit_enabled == 0 &&
                           journal_plan.auth_attempt_allowed == 0,
                       "credential journal plan unsafe ledger plan must force text login fallback");
  fails += expect_true(strings_equal(journal_plan.journal_ticket,
                                     "text-login-fallback-journal-ticket") &&
                           strings_equal(journal_plan.event_type,
                                         "credential-screen-journal-plan-unsafe") &&
                           strings_equal(journal_plan.blocked_reason,
                                         "credential-journal-plan-unsafe"),
                       "credential journal plan unsafe ledger plan should force text login");

  fails += expect_true(build_loginwindow_credential_screen_ledger_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'y', 0, 0, 0,
                           1, &ledger_plan) == 0,
                       "credential journal plan submitted ledger source should build");
  ledger_plan.ledger_submitted = 1;
  ledger_plan.ledger_persist_allowed = 1;
  ledger_plan.ledger_persisted = 1;
  ledger_plan.receipt_submitted = 1;
  ledger_plan.receipt_persist_allowed = 1;
  ledger_plan.receipt_persisted = 1;
  ledger_plan.record_submitted = 1;
  ledger_plan.record_persisted = 1;
  ledger_plan.audit_submitted = 1;
  ledger_plan.audit_log_appended = 1;
  ledger_plan.seal_submitted = 1;
  ledger_plan.cleanup_submitted = 1;
  ledger_plan.retire_submitted = 1;
  ledger_plan.ack_submitted = 1;
  ledger_plan.completion_reported = 1;
  ledger_plan.deadline_armed = 1;
  ledger_plan.page_flip_allowed = 1;
  fails += expect_true(login_window_credential_screen_journal_plan_build(
                           &ledger_plan, &journal_plan) == 0,
                       "credential journal plan submitted ledger should fail closed");
  fails += expect_true(journal_plan.journal_plan_safe == 0 &&
                           journal_plan.journal_allowed == 0 &&
                           journal_plan.journal_submitted == 0 &&
                           journal_plan.journal_persist_allowed == 0 &&
                           journal_plan.journal_persisted == 0 &&
                           journal_plan.journal_cpu_gpu_sync_allowed == 0 &&
                           journal_plan.journal_cpu_gpu_sync_submitted == 0 &&
                           journal_plan.ledger_submitted == 0 &&
                           journal_plan.ledger_persist_allowed == 0 &&
                           journal_plan.ledger_persisted == 0 &&
                           journal_plan.receipt_submitted == 0 &&
                           journal_plan.receipt_persist_allowed == 0 &&
                           journal_plan.receipt_persisted == 0 &&
                           journal_plan.record_submitted == 0 &&
                           journal_plan.record_persisted == 0 &&
                           journal_plan.audit_submitted == 0 &&
                           journal_plan.audit_log_appended == 0 &&
                           journal_plan.seal_submitted == 0 &&
                           journal_plan.cleanup_submitted == 0 &&
                           journal_plan.retire_submitted == 0 &&
                           journal_plan.ack_submitted == 0 &&
                           journal_plan.completion_reported == 0 &&
                           journal_plan.deadline_armed == 0 &&
                           journal_plan.page_flip_allowed == 0 &&
                           journal_plan.page_flip_submitted == 0 &&
                           journal_plan.submit_enabled == 0 &&
                           journal_plan.auth_attempt_allowed == 0,
                       "credential journal plan must not copy unsafe submitted ledger state");
  return fails;
}

int build_loginwindow_credential_screen_archive_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_archive_plan *archive_plan) {
  struct login_window_credential_screen_journal_plan journal_plan;

  if (build_loginwindow_credential_screen_journal_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &journal_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_archive_plan_build(&journal_plan,
                                                           archive_plan);
}

static int test_loginwindow_credential_screen_archive_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_archive_plan archive_plan;

  fails += expect_true(build_loginwindow_credential_screen_archive_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'a', 0, 0, 0,
                           1, &archive_plan) == 0,
                       "credential archive plan edit should build");
  fails += expect_true(archive_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_ARCHIVE_PLAN_VERSION,
                       "credential archive plan should expose stable version");
  fails += expect_true(archive_plan.journal_plan_available == 1 &&
                           archive_plan.journal_plan_safe == 1 &&
                           archive_plan.archive_plan_safe == 1,
                       "credential archive plan should require safe journal plan");
  fails += expect_true(archive_plan.archive_required == 1 &&
                           archive_plan.archive_allowed == 1 &&
                           archive_plan.archive_submitted == 0 &&
                           archive_plan.archive_ticket_selected == 1 &&
                           archive_plan.archive_target_selected == 1 &&
                           archive_plan.archive_persist_allowed == 0 &&
                           archive_plan.archive_persisted == 0 &&
                           archive_plan.archive_cpu_gpu_sync_allowed == 0 &&
                           archive_plan.archive_cpu_gpu_sync_submitted == 0,
                       "credential archive plan should remain declarative");
  fails += expect_true(archive_plan.journal_submitted == 0 &&
                           archive_plan.journal_persisted == 0 &&
                           archive_plan.ledger_submitted == 0 &&
                           archive_plan.ledger_persisted == 0 &&
                           archive_plan.receipt_submitted == 0 &&
                           archive_plan.receipt_persisted == 0 &&
                           archive_plan.record_submitted == 0 &&
                           archive_plan.record_persisted == 0 &&
                           archive_plan.audit_submitted == 0 &&
                           archive_plan.audit_log_appended == 0 &&
                           archive_plan.seal_submitted == 0 &&
                           archive_plan.cleanup_submitted == 0 &&
                           archive_plan.retire_submitted == 0 &&
                           archive_plan.ack_submitted == 0 &&
                           archive_plan.completion_reported == 0 &&
                           archive_plan.deadline_armed == 0 &&
                           archive_plan.sync_submitted == 0 &&
                           archive_plan.timeline_submitted == 0 &&
                           archive_plan.fence_submitted == 0 &&
                           archive_plan.barrier_submitted == 0 &&
                           archive_plan.flush_submitted == 0 &&
                           archive_plan.framebuffer_written == 0 &&
                           archive_plan.blit_pixels_copied == 0 &&
                           archive_plan.output_submitted == 0 &&
                           archive_plan.display_mode_committed == 0 &&
                           archive_plan.page_flip_submitted == 0,
                       "credential archive plan must not execute GUI work");
  fails += expect_true(archive_plan.archive_credential_panel == 1 &&
                           archive_plan.archive_credential_input == 1 &&
                           archive_plan.archive_credential_focus == 1,
                       "credential archive plan should mark credential widgets");
  fails += expect_true(archive_plan.submit_callback_bound == 0 &&
                           archive_plan.auth_callback_bound == 0 &&
                           archive_plan.submit_enabled == 0 &&
                           archive_plan.auth_attempt_allowed == 0 &&
                           archive_plan.raw_secret_exposed == 0 &&
                           archive_plan.masked_text_exposed == 0 &&
                           archive_plan.length_redacted == 1,
                       "credential archive plan must stay redacted and auth-disabled");
  fails += expect_true(strings_equal(archive_plan.archive_ticket,
                                     "credential-screen-archive-ticket") &&
                           strings_equal(archive_plan.journal_ticket,
                                         "credential-screen-journal-ticket") &&
                           strings_equal(archive_plan.archive_policy,
                                         "declarative-archive-no-persist") &&
                           strings_equal(archive_plan.state,
                                         "archive-credential-ready"),
                       "credential archive plan should report archive ticket");
  return fails;
}

static int test_loginwindow_credential_screen_archive_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_archive_plan archive_plan;

  fails += expect_true(build_loginwindow_credential_screen_archive_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &archive_plan) == 0,
                       "credential archive plan recovery should build");
  fails += expect_true(archive_plan.archive_plan_safe == 1 &&
                           archive_plan.archive_allowed == 1 &&
                           archive_plan.archive_submitted == 0 &&
                           archive_plan.archive_persisted == 0 &&
                           archive_plan.archive_text_recovery == 1 &&
                           archive_plan.archive_text_login == 1 &&
                           archive_plan.archive_credential_focus == 0,
                       "credential archive plan recovery should mark text recovery");
  fails += expect_true(archive_plan.journal_submitted == 0 &&
                           archive_plan.ledger_submitted == 0 &&
                           archive_plan.receipt_submitted == 0 &&
                           archive_plan.record_submitted == 0 &&
                           archive_plan.audit_submitted == 0 &&
                           archive_plan.audit_log_appended == 0 &&
                           archive_plan.seal_submitted == 0 &&
                           archive_plan.cleanup_submitted == 0 &&
                           archive_plan.retire_submitted == 0 &&
                           archive_plan.ack_submitted == 0 &&
                           archive_plan.completion_reported == 0 &&
                           archive_plan.deadline_armed == 0 &&
                           archive_plan.sync_submitted == 0 &&
                           archive_plan.timeline_submitted == 0 &&
                           archive_plan.fence_submitted == 0 &&
                           archive_plan.barrier_submitted == 0 &&
                           archive_plan.flush_submitted == 0 &&
                           archive_plan.framebuffer_written == 0 &&
                           archive_plan.output_submitted == 0 &&
                           archive_plan.display_mode_committed == 0 &&
                           archive_plan.page_flip_submitted == 0 &&
                           archive_plan.submit_enabled == 0 &&
                           archive_plan.auth_attempt_allowed == 0,
                       "credential archive plan recovery must not persist or output");
  fails += expect_true(strings_equal(archive_plan.archive_ticket,
                                     "text-recovery-archive-ticket") &&
                           strings_equal(archive_plan.compositor_target,
                                         "text-recovery-archive") &&
                           strings_equal(archive_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential archive plan recovery should report recovery ticket");

  fails += expect_true(build_loginwindow_credential_screen_archive_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &archive_plan) == 0,
                       "credential archive plan resume should build");
  fails += expect_true(archive_plan.archive_plan_safe == 1 &&
                           archive_plan.archive_text_login_resume == 1 &&
                           archive_plan.session_reset_required == 1 &&
                           archive_plan.login_screen_rerender_required == 1 &&
                           archive_plan.archive_submitted == 0 &&
                           archive_plan.archive_persisted == 0 &&
                           archive_plan.journal_submitted == 0 &&
                           archive_plan.submit_enabled == 0 &&
                           archive_plan.auth_attempt_allowed == 0,
                       "credential archive plan resume should keep GUI auth disabled");
  fails += expect_true(strings_equal(archive_plan.archive_ticket,
                                     "text-login-resume-archive-ticket") &&
                           strings_equal(archive_plan.archive_policy,
                                         "full-archive-declarative") &&
                           strings_equal(archive_plan.state,
                                         "archive-resume-ready"),
                       "credential archive plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_archive_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_archive_plan archive_plan;

  fails += expect_true(build_loginwindow_credential_screen_archive_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &archive_plan) == 0,
                       "credential archive plan submit should build");
  fails += expect_true(archive_plan.archive_plan_safe == 1 &&
                           archive_plan.submit_requested == 1 &&
                           archive_plan.archive_text_login_fallback == 1 &&
                           archive_plan.action_allowed == 0 &&
                           archive_plan.action_blocked == 1 &&
                           archive_plan.input_focus_allowed == 0,
                       "credential archive plan submit should force text login fallback");
  fails += expect_true(archive_plan.archive_submitted == 0 &&
                           archive_plan.archive_persist_allowed == 0 &&
                           archive_plan.archive_persisted == 0 &&
                           archive_plan.journal_submitted == 0 &&
                           archive_plan.journal_persisted == 0 &&
                           archive_plan.ledger_submitted == 0 &&
                           archive_plan.ledger_persisted == 0 &&
                           archive_plan.receipt_submitted == 0 &&
                           archive_plan.receipt_persisted == 0 &&
                           archive_plan.record_submitted == 0 &&
                           archive_plan.audit_submitted == 0 &&
                           archive_plan.audit_log_appended == 0 &&
                           archive_plan.seal_submitted == 0 &&
                           archive_plan.cleanup_submitted == 0 &&
                           archive_plan.retire_submitted == 0 &&
                           archive_plan.ack_submitted == 0 &&
                           archive_plan.completion_reported == 0 &&
                           archive_plan.deadline_armed == 0 &&
                           archive_plan.sync_submitted == 0 &&
                           archive_plan.timeline_submitted == 0 &&
                           archive_plan.fence_submitted == 0 &&
                           archive_plan.barrier_submitted == 0 &&
                           archive_plan.flush_submitted == 0 &&
                           archive_plan.framebuffer_written == 0 &&
                           archive_plan.blit_pixels_copied == 0 &&
                           archive_plan.output_submitted == 0 &&
                           archive_plan.display_submitted == 0 &&
                           archive_plan.page_flip_submitted == 0 &&
                           archive_plan.submit_callback_bound == 0 &&
                           archive_plan.auth_callback_bound == 0 &&
                           archive_plan.submit_enabled == 0 &&
                           archive_plan.auth_attempt_allowed == 0,
                       "credential archive plan submit must stay declarative");
  fails += expect_true(strings_equal(archive_plan.archive_ticket,
                                     "text-login-fallback-archive-ticket") &&
                           strings_equal(archive_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(archive_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential archive plan submit should explain fallback");

  fails += expect_true(build_loginwindow_credential_screen_archive_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &archive_plan) == 0,
                       "credential archive plan unknown should build");
  fails += expect_true(archive_plan.archive_plan_safe == 1 &&
                           archive_plan.archive_text_login_fallback == 1 &&
                           archive_plan.action_allowed == 0 &&
                           archive_plan.action_blocked == 1,
                       "credential archive plan unknown should force text login fallback");
  fails += expect_true(strings_equal(archive_plan.archive_ticket,
                                     "text-login-fallback-archive-ticket") &&
                           strings_equal(archive_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential archive plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_archive_plan_fails_closed_for_unsafe_or_missing_journal_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_journal_plan journal_plan;
  struct login_window_credential_screen_archive_plan archive_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_archive_plan_build(
                           NULL, &archive_plan) == 0,
                       "credential archive plan missing journal plan should build fail-closed state");
  fails += expect_true(archive_plan.journal_plan_available == 0 &&
                           archive_plan.journal_plan_safe == 0 &&
                           archive_plan.archive_plan_safe == 0 &&
                           archive_plan.route_selected == 0 &&
                           archive_plan.route_blocked == 1,
                       "credential archive plan missing journal plan should block archive plan");
  fails += expect_true(archive_plan.archive_allowed == 0 &&
                           archive_plan.archive_submitted == 0 &&
                           archive_plan.archive_persisted == 0 &&
                           archive_plan.journal_submitted == 0 &&
                           archive_plan.archive_text_login_fallback == 1 &&
                           archive_plan.submit_enabled == 0 &&
                           archive_plan.auth_attempt_allowed == 0,
                       "credential archive plan missing journal plan must stay redacted");
  fails += expect_true(strings_equal(archive_plan.archive_ticket,
                                     "text-login-fallback-archive-ticket") &&
                           strings_equal(archive_plan.event_type,
                                         "credential-screen-archive-plan-unavailable") &&
                           strings_equal(archive_plan.blocked_reason,
                                         "journal-plan-unavailable"),
                       "credential archive plan missing journal plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_journal_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &journal_plan) == 0,
                       "credential archive plan unsafe journal source should build");
  fails += expect_true(login_window_credential_screen_archive_plan_build(
                           &journal_plan, &archive_plan) == 0,
                       "credential archive plan unsafe journal plan should build blocked state");
  fails += expect_true(archive_plan.journal_plan_available == 1 &&
                           archive_plan.journal_plan_safe == 0 &&
                           archive_plan.archive_plan_safe == 0 &&
                           archive_plan.route_selected == 0 &&
                           archive_plan.route_blocked == 1,
                       "credential archive plan unsafe journal plan should block archive plan");
  fails += expect_true(archive_plan.archive_allowed == 0 &&
                           archive_plan.archive_submitted == 0 &&
                           archive_plan.archive_text_login_fallback == 1 &&
                           archive_plan.submit_enabled == 0 &&
                           archive_plan.auth_attempt_allowed == 0,
                       "credential archive plan unsafe journal plan must force text login fallback");
  fails += expect_true(strings_equal(archive_plan.archive_ticket,
                                     "text-login-fallback-archive-ticket") &&
                           strings_equal(archive_plan.event_type,
                                         "credential-screen-archive-plan-unsafe") &&
                           strings_equal(archive_plan.blocked_reason,
                                         "credential-archive-plan-unsafe"),
                       "credential archive plan unsafe journal plan should force text login");

  fails += expect_true(build_loginwindow_credential_screen_journal_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'y', 0, 0, 0,
                           1, &journal_plan) == 0,
                       "credential archive plan submitted journal source should build");
  journal_plan.journal_submitted = 1;
  journal_plan.journal_persist_allowed = 1;
  journal_plan.journal_persisted = 1;
  journal_plan.ledger_submitted = 1;
  journal_plan.ledger_persist_allowed = 1;
  journal_plan.ledger_persisted = 1;
  journal_plan.receipt_submitted = 1;
  journal_plan.receipt_persist_allowed = 1;
  journal_plan.receipt_persisted = 1;
  journal_plan.record_submitted = 1;
  journal_plan.record_persisted = 1;
  journal_plan.audit_submitted = 1;
  journal_plan.audit_log_appended = 1;
  journal_plan.seal_submitted = 1;
  journal_plan.cleanup_submitted = 1;
  journal_plan.retire_submitted = 1;
  journal_plan.ack_submitted = 1;
  journal_plan.completion_reported = 1;
  journal_plan.deadline_armed = 1;
  journal_plan.page_flip_allowed = 1;
  fails += expect_true(login_window_credential_screen_archive_plan_build(
                           &journal_plan, &archive_plan) == 0,
                       "credential archive plan submitted journal should fail closed");
  fails += expect_true(archive_plan.archive_plan_safe == 0 &&
                           archive_plan.archive_allowed == 0 &&
                           archive_plan.archive_submitted == 0 &&
                           archive_plan.archive_persist_allowed == 0 &&
                           archive_plan.archive_persisted == 0 &&
                           archive_plan.archive_cpu_gpu_sync_allowed == 0 &&
                           archive_plan.archive_cpu_gpu_sync_submitted == 0 &&
                           archive_plan.journal_submitted == 0 &&
                           archive_plan.journal_persist_allowed == 0 &&
                           archive_plan.journal_persisted == 0 &&
                           archive_plan.ledger_submitted == 0 &&
                           archive_plan.ledger_persist_allowed == 0 &&
                           archive_plan.ledger_persisted == 0 &&
                           archive_plan.receipt_submitted == 0 &&
                           archive_plan.receipt_persist_allowed == 0 &&
                           archive_plan.receipt_persisted == 0 &&
                           archive_plan.record_submitted == 0 &&
                           archive_plan.record_persisted == 0 &&
                           archive_plan.audit_submitted == 0 &&
                           archive_plan.audit_log_appended == 0 &&
                           archive_plan.seal_submitted == 0 &&
                           archive_plan.cleanup_submitted == 0 &&
                           archive_plan.retire_submitted == 0 &&
                           archive_plan.ack_submitted == 0 &&
                           archive_plan.completion_reported == 0 &&
                           archive_plan.deadline_armed == 0 &&
                           archive_plan.page_flip_allowed == 0 &&
                           archive_plan.page_flip_submitted == 0 &&
                           archive_plan.submit_enabled == 0 &&
                           archive_plan.auth_attempt_allowed == 0,
                       "credential archive plan must not copy unsafe submitted journal state");
  return fails;
}

int test_login_runtime_credential_journal_archive_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_journal_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_journal_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_journal_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_journal_plan_fails_closed_for_unsafe_or_missing_ledger_plan();
  fails += test_loginwindow_credential_screen_archive_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_archive_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_archive_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_archive_plan_fails_closed_for_unsafe_or_missing_journal_plan();
  return fails;
}
