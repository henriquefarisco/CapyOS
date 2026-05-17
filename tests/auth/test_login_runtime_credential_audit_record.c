/*
 * tests/auth/test_login_runtime_credential_audit_record.c
 *
 * Credential screen audit plan + record plan coverage for the
 * `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-16 monolith
 * refactor (PR D.25 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_audit_plan_build`: 4 tests
 *     covering the credential widgets audit + the text-route audit
 *     (recovery + resume) + the submit/unknown fallback audit + the
 *     missing-or-unsafe seal plan fail-closed default.
 *   - `login_window_credential_screen_record_plan_build`: 4 tests
 *     covering the credential widgets record + the text-route
 *     record (recovery + resume) + the submit/unknown fallback
 *     record + the missing-or-unsafe audit plan fail-closed default.
 *
 * Also exposes shared helpers
 * `build_loginwindow_credential_screen_audit_plan_for_action` and
 * `build_loginwindow_credential_screen_record_plan_for_action`,
 * used by later companion files that chain on top of the
 * audit/record stages (receipt, ...).
 *
 * The companion entry `test_login_runtime_credential_audit_record_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_audit_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_audit_plan *audit_plan) {
  struct login_window_credential_screen_seal_plan seal_plan;

  if (build_loginwindow_credential_screen_seal_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &seal_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_audit_plan_build(&seal_plan,
                                                         audit_plan);
}

static int test_loginwindow_credential_screen_audit_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_audit_plan audit_plan;

  fails += expect_true(build_loginwindow_credential_screen_audit_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'a', 0, 0, 0,
                           1, &audit_plan) == 0,
                       "credential audit plan edit should build");
  fails += expect_true(audit_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_AUDIT_PLAN_VERSION,
                       "credential audit plan should expose stable version");
  fails += expect_true(audit_plan.seal_plan_available == 1 &&
                           audit_plan.seal_plan_safe == 1 &&
                           audit_plan.audit_plan_safe == 1,
                       "credential audit plan should require safe seal plan");
  fails += expect_true(audit_plan.audit_required == 1 &&
                           audit_plan.audit_allowed == 1 &&
                           audit_plan.audit_submitted == 0 &&
                           audit_plan.audit_ticket_selected == 1 &&
                           audit_plan.audit_target_selected == 1 &&
                           audit_plan.audit_log_append_allowed == 0 &&
                           audit_plan.audit_log_appended == 0 &&
                           audit_plan.audit_cpu_gpu_sync_allowed == 0 &&
                           audit_plan.audit_cpu_gpu_sync_submitted == 0,
                       "credential audit plan should remain declarative");
  fails += expect_true(audit_plan.seal_submitted == 0 &&
                           audit_plan.seal_state_written == 0 &&
                           audit_plan.cleanup_submitted == 0 &&
                           audit_plan.cleanup_resource_released == 0 &&
                           audit_plan.retire_submitted == 0 &&
                           audit_plan.ack_submitted == 0 &&
                           audit_plan.completion_reported == 0 &&
                           audit_plan.completion_acknowledged == 0 &&
                           audit_plan.deadline_armed == 0 &&
                           audit_plan.deadline_timer_armed == 0 &&
                           audit_plan.deadline_expired == 0 &&
                           audit_plan.sync_submitted == 0 &&
                           audit_plan.sync_wait_allowed == 0 &&
                           audit_plan.sync_signal_allowed == 0 &&
                           audit_plan.timeline_submitted == 0 &&
                           audit_plan.timeline_value_published == 0 &&
                           audit_plan.fence_submitted == 0 &&
                           audit_plan.barrier_submitted == 0 &&
                           audit_plan.flush_submitted == 0 &&
                           audit_plan.framebuffer_mapped == 0 &&
                           audit_plan.framebuffer_written == 0 &&
                           audit_plan.framebuffer_flushed == 0 &&
                           audit_plan.blit_pixels_copied == 0 &&
                           audit_plan.output_submitted == 0 &&
                           audit_plan.display_mode_committed == 0 &&
                           audit_plan.scanout_submitted == 0 &&
                           audit_plan.vsync_submitted == 0 &&
                           audit_plan.schedule_submitted == 0 &&
                           audit_plan.present_submitted == 0 &&
                           audit_plan.damage_submitted == 0 &&
                           audit_plan.page_flip_submitted == 0,
                       "credential audit plan must not execute GUI work");
  fails += expect_true(audit_plan.audit_credential_panel == 1 &&
                           audit_plan.audit_credential_input == 1 &&
                           audit_plan.audit_credential_focus == 1,
                       "credential audit plan should mark credential widgets");
  fails += expect_true(audit_plan.submit_callback_bound == 0 &&
                           audit_plan.auth_callback_bound == 0 &&
                           audit_plan.submit_enabled == 0 &&
                           audit_plan.auth_attempt_allowed == 0 &&
                           audit_plan.raw_secret_exposed == 0 &&
                           audit_plan.masked_text_exposed == 0 &&
                           audit_plan.length_redacted == 1,
                       "credential audit plan must stay redacted and auth-disabled");
  fails += expect_true(strings_equal(audit_plan.audit_ticket,
                                     "credential-screen-audit-ticket") &&
                           strings_equal(audit_plan.seal_ticket,
                                         "credential-screen-seal-ticket") &&
                           strings_equal(audit_plan.audit_policy,
                                         "declarative-audit-no-log-append") &&
                           strings_equal(audit_plan.state,
                                         "audit-credential-ready"),
                       "credential audit plan should report audit ticket");
  return fails;
}

static int test_loginwindow_credential_screen_audit_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_audit_plan audit_plan;

  fails += expect_true(build_loginwindow_credential_screen_audit_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &audit_plan) == 0,
                       "credential audit plan recovery should build");
  fails += expect_true(audit_plan.audit_plan_safe == 1 &&
                           audit_plan.audit_allowed == 1 &&
                           audit_plan.audit_submitted == 0 &&
                           audit_plan.audit_log_appended == 0 &&
                           audit_plan.audit_text_recovery == 1 &&
                           audit_plan.audit_text_login == 1 &&
                           audit_plan.audit_credential_focus == 0,
                       "credential audit plan recovery should mark text recovery");
  fails += expect_true(audit_plan.seal_submitted == 0 &&
                           audit_plan.cleanup_submitted == 0 &&
                           audit_plan.retire_submitted == 0 &&
                           audit_plan.ack_submitted == 0 &&
                           audit_plan.completion_reported == 0 &&
                           audit_plan.audit_cpu_gpu_sync_submitted == 0 &&
                           audit_plan.deadline_armed == 0 &&
                           audit_plan.deadline_timer_armed == 0 &&
                           audit_plan.sync_submitted == 0 &&
                           audit_plan.timeline_submitted == 0 &&
                           audit_plan.fence_submitted == 0 &&
                           audit_plan.barrier_submitted == 0 &&
                           audit_plan.flush_submitted == 0 &&
                           audit_plan.framebuffer_written == 0 &&
                           audit_plan.output_submitted == 0 &&
                           audit_plan.display_mode_committed == 0 &&
                           audit_plan.page_flip_submitted == 0 &&
                           audit_plan.submit_enabled == 0 &&
                           audit_plan.auth_attempt_allowed == 0,
                       "credential audit plan recovery must not append log or output");
  fails += expect_true(strings_equal(audit_plan.audit_ticket,
                                     "text-recovery-audit-ticket") &&
                           strings_equal(audit_plan.compositor_target,
                                         "text-recovery-audit") &&
                           strings_equal(audit_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential audit plan recovery should report recovery ticket");

  fails += expect_true(build_loginwindow_credential_screen_audit_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &audit_plan) == 0,
                       "credential audit plan resume should build");
  fails += expect_true(audit_plan.audit_plan_safe == 1 &&
                           audit_plan.audit_text_login_resume == 1 &&
                           audit_plan.session_reset_required == 1 &&
                           audit_plan.login_screen_rerender_required == 1 &&
                           audit_plan.audit_submitted == 0 &&
                           audit_plan.audit_log_appended == 0 &&
                           audit_plan.seal_submitted == 0 &&
                           audit_plan.submit_enabled == 0 &&
                           audit_plan.auth_attempt_allowed == 0,
                       "credential audit plan resume should keep GUI auth disabled");
  fails += expect_true(strings_equal(audit_plan.audit_ticket,
                                     "text-login-resume-audit-ticket") &&
                           strings_equal(audit_plan.audit_policy,
                                         "full-audit-declarative") &&
                           strings_equal(audit_plan.state,
                                         "audit-resume-ready"),
                       "credential audit plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_audit_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_audit_plan audit_plan;

  fails += expect_true(build_loginwindow_credential_screen_audit_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &audit_plan) == 0,
                       "credential audit plan submit should build");
  fails += expect_true(audit_plan.audit_plan_safe == 1 &&
                           audit_plan.submit_requested == 1 &&
                           audit_plan.audit_text_login_fallback == 1 &&
                           audit_plan.action_allowed == 0 &&
                           audit_plan.action_blocked == 1 &&
                           audit_plan.input_focus_allowed == 0,
                       "credential audit plan submit should force text login fallback");
  fails += expect_true(audit_plan.audit_submitted == 0 &&
                           audit_plan.audit_log_append_allowed == 0 &&
                           audit_plan.audit_log_appended == 0 &&
                           audit_plan.seal_submitted == 0 &&
                           audit_plan.seal_state_written == 0 &&
                           audit_plan.cleanup_submitted == 0 &&
                           audit_plan.retire_submitted == 0 &&
                           audit_plan.ack_submitted == 0 &&
                           audit_plan.completion_reported == 0 &&
                           audit_plan.deadline_armed == 0 &&
                           audit_plan.sync_submitted == 0 &&
                           audit_plan.timeline_submitted == 0 &&
                           audit_plan.timeline_value_published == 0 &&
                           audit_plan.fence_submitted == 0 &&
                           audit_plan.barrier_submitted == 0 &&
                           audit_plan.flush_submitted == 0 &&
                           audit_plan.framebuffer_written == 0 &&
                           audit_plan.blit_pixels_copied == 0 &&
                           audit_plan.output_submitted == 0 &&
                           audit_plan.display_submitted == 0 &&
                           audit_plan.page_flip_submitted == 0 &&
                           audit_plan.submit_callback_bound == 0 &&
                           audit_plan.auth_callback_bound == 0 &&
                           audit_plan.submit_enabled == 0 &&
                           audit_plan.auth_attempt_allowed == 0,
                       "credential audit plan submit must stay declarative");
  fails += expect_true(strings_equal(audit_plan.audit_ticket,
                                     "text-login-fallback-audit-ticket") &&
                           strings_equal(audit_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(audit_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential audit plan submit should explain fallback");

  fails += expect_true(build_loginwindow_credential_screen_audit_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &audit_plan) == 0,
                       "credential audit plan unknown should build");
  fails += expect_true(audit_plan.audit_plan_safe == 1 &&
                           audit_plan.audit_text_login_fallback == 1 &&
                           audit_plan.action_allowed == 0 &&
                           audit_plan.action_blocked == 1,
                       "credential audit plan unknown should force text login fallback");
  fails += expect_true(strings_equal(audit_plan.audit_ticket,
                                     "text-login-fallback-audit-ticket") &&
                           strings_equal(audit_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential audit plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_audit_plan_fails_closed_for_unsafe_or_missing_seal_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_seal_plan seal_plan;
  struct login_window_credential_screen_audit_plan audit_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_audit_plan_build(
                           NULL, &audit_plan) == 0,
                       "credential audit plan missing seal plan should build fail-closed state");
  fails += expect_true(audit_plan.seal_plan_available == 0 &&
                           audit_plan.seal_plan_safe == 0 &&
                           audit_plan.audit_plan_safe == 0 &&
                           audit_plan.route_selected == 0 &&
                           audit_plan.route_blocked == 1,
                       "credential audit plan missing seal plan should block audit plan");
  fails += expect_true(audit_plan.audit_allowed == 0 &&
                           audit_plan.audit_submitted == 0 &&
                           audit_plan.audit_log_appended == 0 &&
                           audit_plan.seal_submitted == 0 &&
                           audit_plan.audit_text_login_fallback == 1 &&
                           audit_plan.submit_enabled == 0 &&
                           audit_plan.auth_attempt_allowed == 0,
                       "credential audit plan missing seal plan must stay redacted");
  fails += expect_true(strings_equal(audit_plan.audit_ticket,
                                     "text-login-fallback-audit-ticket") &&
                           strings_equal(audit_plan.event_type,
                                         "credential-screen-audit-plan-unavailable") &&
                           strings_equal(audit_plan.blocked_reason,
                                         "seal-plan-unavailable"),
                       "credential audit plan missing seal plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_seal_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &seal_plan) == 0,
                       "credential audit plan unsafe seal source should build");
  fails += expect_true(login_window_credential_screen_audit_plan_build(
                           &seal_plan, &audit_plan) == 0,
                       "credential audit plan unsafe seal plan should build blocked state");
  fails += expect_true(audit_plan.seal_plan_available == 1 &&
                           audit_plan.seal_plan_safe == 0 &&
                           audit_plan.audit_plan_safe == 0 &&
                           audit_plan.route_selected == 0 &&
                           audit_plan.route_blocked == 1,
                       "credential audit plan unsafe seal plan should block audit plan");
  fails += expect_true(audit_plan.audit_allowed == 0 &&
                           audit_plan.audit_submitted == 0 &&
                           audit_plan.audit_text_login_fallback == 1 &&
                           audit_plan.submit_enabled == 0 &&
                           audit_plan.auth_attempt_allowed == 0,
                       "credential audit plan unsafe seal plan must force text login fallback");
  fails += expect_true(strings_equal(audit_plan.audit_ticket,
                                     "text-login-fallback-audit-ticket") &&
                           strings_equal(audit_plan.event_type,
                                         "credential-screen-audit-plan-unsafe") &&
                           strings_equal(audit_plan.blocked_reason,
                                         "credential-audit-plan-unsafe"),
                       "credential audit plan unsafe seal plan should force text login");

  fails += expect_true(build_loginwindow_credential_screen_seal_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'y', 0, 0, 0,
                           1, &seal_plan) == 0,
                       "credential audit plan submitted seal source should build");
  seal_plan.seal_submitted = 1;
  seal_plan.seal_state_write_allowed = 1;
  seal_plan.seal_state_written = 1;
  seal_plan.cleanup_submitted = 1;
  seal_plan.retire_submitted = 1;
  seal_plan.ack_submitted = 1;
  seal_plan.completion_reported = 1;
  seal_plan.deadline_armed = 1;
  seal_plan.page_flip_allowed = 1;
  fails += expect_true(login_window_credential_screen_audit_plan_build(
                           &seal_plan, &audit_plan) == 0,
                       "credential audit plan submitted seal should fail closed");
  fails += expect_true(audit_plan.audit_plan_safe == 0 &&
                           audit_plan.audit_allowed == 0 &&
                           audit_plan.audit_submitted == 0 &&
                           audit_plan.audit_log_append_allowed == 0 &&
                           audit_plan.audit_log_appended == 0 &&
                           audit_plan.audit_cpu_gpu_sync_allowed == 0 &&
                           audit_plan.audit_cpu_gpu_sync_submitted == 0 &&
                           audit_plan.seal_submitted == 0 &&
                           audit_plan.seal_state_write_allowed == 0 &&
                           audit_plan.seal_state_written == 0 &&
                           audit_plan.cleanup_submitted == 0 &&
                           audit_plan.retire_submitted == 0 &&
                           audit_plan.ack_submitted == 0 &&
                           audit_plan.completion_reported == 0 &&
                           audit_plan.deadline_armed == 0 &&
                           audit_plan.page_flip_allowed == 0 &&
                           audit_plan.page_flip_submitted == 0 &&
                           audit_plan.submit_enabled == 0 &&
                           audit_plan.auth_attempt_allowed == 0,
                       "credential audit plan must not copy unsafe submitted seal state");
  return fails;
}


int build_loginwindow_credential_screen_record_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_record_plan *record_plan) {
  struct login_window_credential_screen_audit_plan audit_plan;

  if (build_loginwindow_credential_screen_audit_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &audit_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_record_plan_build(&audit_plan,
                                                          record_plan);
}

static int test_loginwindow_credential_screen_record_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_record_plan record_plan;

  fails += expect_true(build_loginwindow_credential_screen_record_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 0, 0, 0,
                           1, &record_plan) == 0,
                       "credential record plan edit should build");
  fails += expect_true(record_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_RECORD_PLAN_VERSION,
                       "credential record plan should expose stable version");
  fails += expect_true(record_plan.audit_plan_available == 1 &&
                           record_plan.audit_plan_safe == 1 &&
                           record_plan.record_plan_safe == 1,
                       "credential record plan should require safe audit plan");
  fails += expect_true(record_plan.record_required == 1 &&
                           record_plan.record_allowed == 1 &&
                           record_plan.record_submitted == 0 &&
                           record_plan.record_ticket_selected == 1 &&
                           record_plan.record_target_selected == 1 &&
                           record_plan.record_persist_allowed == 0 &&
                           record_plan.record_persisted == 0 &&
                           record_plan.record_cpu_gpu_sync_allowed == 0 &&
                           record_plan.record_cpu_gpu_sync_submitted == 0,
                       "credential record plan should remain declarative");
  fails += expect_true(record_plan.audit_submitted == 0 &&
                           record_plan.audit_log_appended == 0 &&
                           record_plan.seal_submitted == 0 &&
                           record_plan.seal_state_written == 0 &&
                           record_plan.cleanup_submitted == 0 &&
                           record_plan.cleanup_resource_released == 0 &&
                           record_plan.retire_submitted == 0 &&
                           record_plan.ack_submitted == 0 &&
                           record_plan.completion_reported == 0 &&
                           record_plan.completion_acknowledged == 0 &&
                           record_plan.deadline_armed == 0 &&
                           record_plan.deadline_timer_armed == 0 &&
                           record_plan.deadline_expired == 0 &&
                           record_plan.sync_submitted == 0 &&
                           record_plan.sync_wait_allowed == 0 &&
                           record_plan.sync_signal_allowed == 0 &&
                           record_plan.timeline_submitted == 0 &&
                           record_plan.timeline_value_published == 0 &&
                           record_plan.fence_submitted == 0 &&
                           record_plan.barrier_submitted == 0 &&
                           record_plan.flush_submitted == 0 &&
                           record_plan.framebuffer_mapped == 0 &&
                           record_plan.framebuffer_written == 0 &&
                           record_plan.framebuffer_flushed == 0 &&
                           record_plan.blit_pixels_copied == 0 &&
                           record_plan.output_submitted == 0 &&
                           record_plan.display_mode_committed == 0 &&
                           record_plan.scanout_submitted == 0 &&
                           record_plan.vsync_submitted == 0 &&
                           record_plan.schedule_submitted == 0 &&
                           record_plan.present_submitted == 0 &&
                           record_plan.damage_submitted == 0 &&
                           record_plan.page_flip_submitted == 0,
                       "credential record plan must not execute GUI work");
  fails += expect_true(record_plan.record_credential_panel == 1 &&
                           record_plan.record_credential_input == 1 &&
                           record_plan.record_credential_focus == 1,
                       "credential record plan should mark credential widgets");
  fails += expect_true(record_plan.submit_callback_bound == 0 &&
                           record_plan.auth_callback_bound == 0 &&
                           record_plan.submit_enabled == 0 &&
                           record_plan.auth_attempt_allowed == 0 &&
                           record_plan.raw_secret_exposed == 0 &&
                           record_plan.masked_text_exposed == 0 &&
                           record_plan.length_redacted == 1,
                       "credential record plan must stay redacted and auth-disabled");
  fails += expect_true(strings_equal(record_plan.record_ticket,
                                     "credential-screen-record-ticket") &&
                           strings_equal(record_plan.audit_ticket,
                                         "credential-screen-audit-ticket") &&
                           strings_equal(record_plan.record_policy,
                                         "declarative-record-no-persist") &&
                           strings_equal(record_plan.state,
                                         "record-credential-ready"),
                       "credential record plan should report record ticket");
  return fails;
}

static int test_loginwindow_credential_screen_record_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_record_plan record_plan;

  fails += expect_true(build_loginwindow_credential_screen_record_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &record_plan) == 0,
                       "credential record plan recovery should build");
  fails += expect_true(record_plan.record_plan_safe == 1 &&
                           record_plan.record_allowed == 1 &&
                           record_plan.record_submitted == 0 &&
                           record_plan.record_persisted == 0 &&
                           record_plan.record_text_recovery == 1 &&
                           record_plan.record_text_login == 1 &&
                           record_plan.record_credential_focus == 0,
                       "credential record plan recovery should mark text recovery");
  fails += expect_true(record_plan.audit_submitted == 0 &&
                           record_plan.audit_log_appended == 0 &&
                           record_plan.seal_submitted == 0 &&
                           record_plan.cleanup_submitted == 0 &&
                           record_plan.retire_submitted == 0 &&
                           record_plan.ack_submitted == 0 &&
                           record_plan.completion_reported == 0 &&
                           record_plan.record_cpu_gpu_sync_submitted == 0 &&
                           record_plan.deadline_armed == 0 &&
                           record_plan.deadline_timer_armed == 0 &&
                           record_plan.sync_submitted == 0 &&
                           record_plan.timeline_submitted == 0 &&
                           record_plan.fence_submitted == 0 &&
                           record_plan.barrier_submitted == 0 &&
                           record_plan.flush_submitted == 0 &&
                           record_plan.framebuffer_written == 0 &&
                           record_plan.output_submitted == 0 &&
                           record_plan.display_mode_committed == 0 &&
                           record_plan.page_flip_submitted == 0 &&
                           record_plan.submit_enabled == 0 &&
                           record_plan.auth_attempt_allowed == 0,
                       "credential record plan recovery must not persist or output");
  fails += expect_true(strings_equal(record_plan.record_ticket,
                                     "text-recovery-record-ticket") &&
                           strings_equal(record_plan.compositor_target,
                                         "text-recovery-record") &&
                           strings_equal(record_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential record plan recovery should report recovery ticket");

  fails += expect_true(build_loginwindow_credential_screen_record_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &record_plan) == 0,
                       "credential record plan resume should build");
  fails += expect_true(record_plan.record_plan_safe == 1 &&
                           record_plan.record_text_login_resume == 1 &&
                           record_plan.session_reset_required == 1 &&
                           record_plan.login_screen_rerender_required == 1 &&
                           record_plan.record_submitted == 0 &&
                           record_plan.record_persisted == 0 &&
                           record_plan.audit_submitted == 0 &&
                           record_plan.submit_enabled == 0 &&
                           record_plan.auth_attempt_allowed == 0,
                       "credential record plan resume should keep GUI auth disabled");
  fails += expect_true(strings_equal(record_plan.record_ticket,
                                     "text-login-resume-record-ticket") &&
                           strings_equal(record_plan.record_policy,
                                         "full-record-declarative") &&
                           strings_equal(record_plan.state,
                                         "record-resume-ready"),
                       "credential record plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_record_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_record_plan record_plan;

  fails += expect_true(build_loginwindow_credential_screen_record_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &record_plan) == 0,
                       "credential record plan submit should build");
  fails += expect_true(record_plan.record_plan_safe == 1 &&
                           record_plan.submit_requested == 1 &&
                           record_plan.record_text_login_fallback == 1 &&
                           record_plan.action_allowed == 0 &&
                           record_plan.action_blocked == 1 &&
                           record_plan.input_focus_allowed == 0,
                       "credential record plan submit should force text login fallback");
  fails += expect_true(record_plan.record_submitted == 0 &&
                           record_plan.record_persist_allowed == 0 &&
                           record_plan.record_persisted == 0 &&
                           record_plan.audit_submitted == 0 &&
                           record_plan.audit_log_appended == 0 &&
                           record_plan.seal_submitted == 0 &&
                           record_plan.seal_state_written == 0 &&
                           record_plan.cleanup_submitted == 0 &&
                           record_plan.retire_submitted == 0 &&
                           record_plan.ack_submitted == 0 &&
                           record_plan.completion_reported == 0 &&
                           record_plan.deadline_armed == 0 &&
                           record_plan.sync_submitted == 0 &&
                           record_plan.timeline_submitted == 0 &&
                           record_plan.timeline_value_published == 0 &&
                           record_plan.fence_submitted == 0 &&
                           record_plan.barrier_submitted == 0 &&
                           record_plan.flush_submitted == 0 &&
                           record_plan.framebuffer_written == 0 &&
                           record_plan.blit_pixels_copied == 0 &&
                           record_plan.output_submitted == 0 &&
                           record_plan.display_submitted == 0 &&
                           record_plan.page_flip_submitted == 0 &&
                           record_plan.submit_callback_bound == 0 &&
                           record_plan.auth_callback_bound == 0 &&
                           record_plan.submit_enabled == 0 &&
                           record_plan.auth_attempt_allowed == 0,
                       "credential record plan submit must stay declarative");
  fails += expect_true(strings_equal(record_plan.record_ticket,
                                     "text-login-fallback-record-ticket") &&
                           strings_equal(record_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(record_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential record plan submit should explain fallback");

  fails += expect_true(build_loginwindow_credential_screen_record_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &record_plan) == 0,
                       "credential record plan unknown should build");
  fails += expect_true(record_plan.record_plan_safe == 1 &&
                           record_plan.record_text_login_fallback == 1 &&
                           record_plan.action_allowed == 0 &&
                           record_plan.action_blocked == 1,
                       "credential record plan unknown should force text login fallback");
  fails += expect_true(strings_equal(record_plan.record_ticket,
                                     "text-login-fallback-record-ticket") &&
                           strings_equal(record_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential record plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_record_plan_fails_closed_for_unsafe_or_missing_audit_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_audit_plan audit_plan;
  struct login_window_credential_screen_record_plan record_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_record_plan_build(
                           NULL, &record_plan) == 0,
                       "credential record plan missing audit plan should build fail-closed state");
  fails += expect_true(record_plan.audit_plan_available == 0 &&
                           record_plan.audit_plan_safe == 0 &&
                           record_plan.record_plan_safe == 0 &&
                           record_plan.route_selected == 0 &&
                           record_plan.route_blocked == 1,
                       "credential record plan missing audit plan should block record plan");
  fails += expect_true(record_plan.record_allowed == 0 &&
                           record_plan.record_submitted == 0 &&
                           record_plan.record_persisted == 0 &&
                           record_plan.audit_submitted == 0 &&
                           record_plan.record_text_login_fallback == 1 &&
                           record_plan.submit_enabled == 0 &&
                           record_plan.auth_attempt_allowed == 0,
                       "credential record plan missing audit plan must stay redacted");
  fails += expect_true(strings_equal(record_plan.record_ticket,
                                     "text-login-fallback-record-ticket") &&
                           strings_equal(record_plan.event_type,
                                         "credential-screen-record-plan-unavailable") &&
                           strings_equal(record_plan.blocked_reason,
                                         "audit-plan-unavailable"),
                       "credential record plan missing audit plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_audit_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &audit_plan) == 0,
                       "credential record plan unsafe audit source should build");
  fails += expect_true(login_window_credential_screen_record_plan_build(
                           &audit_plan, &record_plan) == 0,
                       "credential record plan unsafe audit plan should build blocked state");
  fails += expect_true(record_plan.audit_plan_available == 1 &&
                           record_plan.audit_plan_safe == 0 &&
                           record_plan.record_plan_safe == 0 &&
                           record_plan.route_selected == 0 &&
                           record_plan.route_blocked == 1,
                       "credential record plan unsafe audit plan should block record plan");
  fails += expect_true(record_plan.record_allowed == 0 &&
                           record_plan.record_submitted == 0 &&
                           record_plan.record_text_login_fallback == 1 &&
                           record_plan.submit_enabled == 0 &&
                           record_plan.auth_attempt_allowed == 0,
                       "credential record plan unsafe audit plan must force text login fallback");
  fails += expect_true(strings_equal(record_plan.record_ticket,
                                     "text-login-fallback-record-ticket") &&
                           strings_equal(record_plan.event_type,
                                         "credential-screen-record-plan-unsafe") &&
                           strings_equal(record_plan.blocked_reason,
                                         "credential-record-plan-unsafe"),
                       "credential record plan unsafe audit plan should force text login");

  fails += expect_true(build_loginwindow_credential_screen_audit_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'y', 0, 0, 0,
                           1, &audit_plan) == 0,
                       "credential record plan submitted audit source should build");
  audit_plan.audit_submitted = 1;
  audit_plan.audit_log_append_allowed = 1;
  audit_plan.audit_log_appended = 1;
  audit_plan.seal_submitted = 1;
  audit_plan.cleanup_submitted = 1;
  audit_plan.retire_submitted = 1;
  audit_plan.ack_submitted = 1;
  audit_plan.completion_reported = 1;
  audit_plan.deadline_armed = 1;
  audit_plan.page_flip_allowed = 1;
  fails += expect_true(login_window_credential_screen_record_plan_build(
                           &audit_plan, &record_plan) == 0,
                       "credential record plan submitted audit should fail closed");
  fails += expect_true(record_plan.record_plan_safe == 0 &&
                           record_plan.record_allowed == 0 &&
                           record_plan.record_submitted == 0 &&
                           record_plan.record_persist_allowed == 0 &&
                           record_plan.record_persisted == 0 &&
                           record_plan.record_cpu_gpu_sync_allowed == 0 &&
                           record_plan.record_cpu_gpu_sync_submitted == 0 &&
                           record_plan.audit_submitted == 0 &&
                           record_plan.audit_log_append_allowed == 0 &&
                           record_plan.audit_log_appended == 0 &&
                           record_plan.seal_submitted == 0 &&
                           record_plan.cleanup_submitted == 0 &&
                           record_plan.retire_submitted == 0 &&
                           record_plan.ack_submitted == 0 &&
                           record_plan.completion_reported == 0 &&
                           record_plan.deadline_armed == 0 &&
                           record_plan.page_flip_allowed == 0 &&
                           record_plan.page_flip_submitted == 0 &&
                           record_plan.submit_enabled == 0 &&
                           record_plan.auth_attempt_allowed == 0,
                       "credential record plan must not copy unsafe submitted audit state");
  return fails;
}

int test_login_runtime_credential_audit_record_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_audit_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_audit_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_audit_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_audit_plan_fails_closed_for_unsafe_or_missing_seal_plan();
  fails += test_loginwindow_credential_screen_record_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_record_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_record_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_record_plan_fails_closed_for_unsafe_or_missing_audit_plan();
  return fails;
}
