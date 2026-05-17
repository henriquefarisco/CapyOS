/*
 * tests/auth/test_login_runtime_credential_receipt_ledger.c
 *
 * Credential screen receipt plan + ledger plan coverage for the
 * `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-16 monolith
 * refactor (PR D.26 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_receipt_plan_build`: 4 tests
 *     covering the credential widgets receipt + the text-route
 *     receipt (recovery + resume) + the submit/unknown fallback
 *     receipt + the missing-or-unsafe record plan fail-closed
 *     default.
 *   - `login_window_credential_screen_ledger_plan_build`: 4 tests
 *     covering the credential widgets ledger + the text-route
 *     ledger (recovery + resume) + the submit/unknown fallback
 *     ledger + the missing-or-unsafe receipt plan fail-closed
 *     default.
 *
 * Also exposes shared helpers
 * `build_loginwindow_credential_screen_receipt_plan_for_action` and
 * `build_loginwindow_credential_screen_ledger_plan_for_action`,
 * used by later companion files that chain on top of the
 * receipt/ledger stages (journal, ...).
 *
 * The companion entry `test_login_runtime_credential_receipt_ledger_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_receipt_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_receipt_plan *receipt_plan) {
  struct login_window_credential_screen_record_plan record_plan;

  if (build_loginwindow_credential_screen_record_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &record_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_receipt_plan_build(&record_plan,
                                                           receipt_plan);
}

static int test_loginwindow_credential_screen_receipt_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_receipt_plan receipt_plan;

  fails += expect_true(build_loginwindow_credential_screen_receipt_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'p', 0, 0, 0,
                           1, &receipt_plan) == 0,
                       "credential receipt plan edit should build");
  fails += expect_true(receipt_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_RECEIPT_PLAN_VERSION,
                       "credential receipt plan should expose stable version");
  fails += expect_true(receipt_plan.record_plan_available == 1 &&
                           receipt_plan.record_plan_safe == 1 &&
                           receipt_plan.receipt_plan_safe == 1,
                       "credential receipt plan should require safe record plan");
  fails += expect_true(receipt_plan.receipt_required == 1 &&
                           receipt_plan.receipt_allowed == 1 &&
                           receipt_plan.receipt_submitted == 0 &&
                           receipt_plan.receipt_ticket_selected == 1 &&
                           receipt_plan.receipt_target_selected == 1 &&
                           receipt_plan.receipt_persist_allowed == 0 &&
                           receipt_plan.receipt_persisted == 0 &&
                           receipt_plan.receipt_cpu_gpu_sync_allowed == 0 &&
                           receipt_plan.receipt_cpu_gpu_sync_submitted == 0,
                       "credential receipt plan should remain declarative");
  fails += expect_true(receipt_plan.record_submitted == 0 &&
                           receipt_plan.record_persisted == 0 &&
                           receipt_plan.audit_submitted == 0 &&
                           receipt_plan.audit_log_appended == 0 &&
                           receipt_plan.seal_submitted == 0 &&
                           receipt_plan.seal_state_written == 0 &&
                           receipt_plan.cleanup_submitted == 0 &&
                           receipt_plan.cleanup_resource_released == 0 &&
                           receipt_plan.retire_submitted == 0 &&
                           receipt_plan.ack_submitted == 0 &&
                           receipt_plan.completion_reported == 0 &&
                           receipt_plan.completion_acknowledged == 0 &&
                           receipt_plan.deadline_armed == 0 &&
                           receipt_plan.deadline_timer_armed == 0 &&
                           receipt_plan.deadline_expired == 0 &&
                           receipt_plan.sync_submitted == 0 &&
                           receipt_plan.sync_wait_allowed == 0 &&
                           receipt_plan.sync_signal_allowed == 0 &&
                           receipt_plan.timeline_submitted == 0 &&
                           receipt_plan.timeline_value_published == 0 &&
                           receipt_plan.fence_submitted == 0 &&
                           receipt_plan.barrier_submitted == 0 &&
                           receipt_plan.flush_submitted == 0 &&
                           receipt_plan.framebuffer_mapped == 0 &&
                           receipt_plan.framebuffer_written == 0 &&
                           receipt_plan.blit_pixels_copied == 0 &&
                           receipt_plan.output_submitted == 0 &&
                           receipt_plan.display_mode_committed == 0 &&
                           receipt_plan.scanout_submitted == 0 &&
                           receipt_plan.vsync_submitted == 0 &&
                           receipt_plan.schedule_submitted == 0 &&
                           receipt_plan.present_submitted == 0 &&
                           receipt_plan.damage_submitted == 0 &&
                           receipt_plan.page_flip_submitted == 0,
                       "credential receipt plan must not execute GUI work");
  fails += expect_true(receipt_plan.receipt_credential_panel == 1 &&
                           receipt_plan.receipt_credential_input == 1 &&
                           receipt_plan.receipt_credential_focus == 1,
                       "credential receipt plan should mark credential widgets");
  fails += expect_true(receipt_plan.submit_callback_bound == 0 &&
                           receipt_plan.auth_callback_bound == 0 &&
                           receipt_plan.submit_enabled == 0 &&
                           receipt_plan.auth_attempt_allowed == 0 &&
                           receipt_plan.raw_secret_exposed == 0 &&
                           receipt_plan.masked_text_exposed == 0 &&
                           receipt_plan.length_redacted == 1,
                       "credential receipt plan must stay redacted and auth-disabled");
  fails += expect_true(strings_equal(receipt_plan.receipt_ticket,
                                     "credential-screen-receipt-ticket") &&
                           strings_equal(receipt_plan.record_ticket,
                                         "credential-screen-record-ticket") &&
                           strings_equal(receipt_plan.receipt_policy,
                                         "declarative-receipt-no-persist") &&
                           strings_equal(receipt_plan.state,
                                         "receipt-credential-ready"),
                       "credential receipt plan should report receipt ticket");
  return fails;
}

static int test_loginwindow_credential_screen_receipt_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_receipt_plan receipt_plan;

  fails += expect_true(build_loginwindow_credential_screen_receipt_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &receipt_plan) == 0,
                       "credential receipt plan recovery should build");
  fails += expect_true(receipt_plan.receipt_plan_safe == 1 &&
                           receipt_plan.receipt_allowed == 1 &&
                           receipt_plan.receipt_submitted == 0 &&
                           receipt_plan.receipt_persisted == 0 &&
                           receipt_plan.receipt_text_recovery == 1 &&
                           receipt_plan.receipt_text_login == 1 &&
                           receipt_plan.receipt_credential_focus == 0,
                       "credential receipt plan recovery should mark text recovery");
  fails += expect_true(receipt_plan.record_submitted == 0 &&
                           receipt_plan.record_persisted == 0 &&
                           receipt_plan.audit_submitted == 0 &&
                           receipt_plan.audit_log_appended == 0 &&
                           receipt_plan.seal_submitted == 0 &&
                           receipt_plan.cleanup_submitted == 0 &&
                           receipt_plan.retire_submitted == 0 &&
                           receipt_plan.ack_submitted == 0 &&
                           receipt_plan.completion_reported == 0 &&
                           receipt_plan.receipt_cpu_gpu_sync_submitted == 0 &&
                           receipt_plan.deadline_armed == 0 &&
                           receipt_plan.deadline_timer_armed == 0 &&
                           receipt_plan.sync_submitted == 0 &&
                           receipt_plan.timeline_submitted == 0 &&
                           receipt_plan.fence_submitted == 0 &&
                           receipt_plan.barrier_submitted == 0 &&
                           receipt_plan.flush_submitted == 0 &&
                           receipt_plan.framebuffer_written == 0 &&
                           receipt_plan.output_submitted == 0 &&
                           receipt_plan.display_mode_committed == 0 &&
                           receipt_plan.page_flip_submitted == 0 &&
                           receipt_plan.submit_enabled == 0 &&
                           receipt_plan.auth_attempt_allowed == 0,
                       "credential receipt plan recovery must not persist or output");
  fails += expect_true(strings_equal(receipt_plan.receipt_ticket,
                                     "text-recovery-receipt-ticket") &&
                           strings_equal(receipt_plan.compositor_target,
                                         "text-recovery-receipt") &&
                           strings_equal(receipt_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential receipt plan recovery should report recovery ticket");

  fails += expect_true(build_loginwindow_credential_screen_receipt_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &receipt_plan) == 0,
                       "credential receipt plan resume should build");
  fails += expect_true(receipt_plan.receipt_plan_safe == 1 &&
                           receipt_plan.receipt_text_login_resume == 1 &&
                           receipt_plan.session_reset_required == 1 &&
                           receipt_plan.login_screen_rerender_required == 1 &&
                           receipt_plan.receipt_submitted == 0 &&
                           receipt_plan.receipt_persisted == 0 &&
                           receipt_plan.record_submitted == 0 &&
                           receipt_plan.submit_enabled == 0 &&
                           receipt_plan.auth_attempt_allowed == 0,
                       "credential receipt plan resume should keep GUI auth disabled");
  fails += expect_true(strings_equal(receipt_plan.receipt_ticket,
                                     "text-login-resume-receipt-ticket") &&
                           strings_equal(receipt_plan.receipt_policy,
                                         "full-receipt-declarative") &&
                           strings_equal(receipt_plan.state,
                                         "receipt-resume-ready"),
                       "credential receipt plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_receipt_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_receipt_plan receipt_plan;

  fails += expect_true(build_loginwindow_credential_screen_receipt_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &receipt_plan) == 0,
                       "credential receipt plan submit should build");
  fails += expect_true(receipt_plan.receipt_plan_safe == 1 &&
                           receipt_plan.submit_requested == 1 &&
                           receipt_plan.receipt_text_login_fallback == 1 &&
                           receipt_plan.action_allowed == 0 &&
                           receipt_plan.action_blocked == 1 &&
                           receipt_plan.input_focus_allowed == 0,
                       "credential receipt plan submit should force text login fallback");
  fails += expect_true(receipt_plan.receipt_submitted == 0 &&
                           receipt_plan.receipt_persist_allowed == 0 &&
                           receipt_plan.receipt_persisted == 0 &&
                           receipt_plan.record_submitted == 0 &&
                           receipt_plan.record_persisted == 0 &&
                           receipt_plan.audit_submitted == 0 &&
                           receipt_plan.audit_log_appended == 0 &&
                           receipt_plan.seal_submitted == 0 &&
                           receipt_plan.cleanup_submitted == 0 &&
                           receipt_plan.retire_submitted == 0 &&
                           receipt_plan.ack_submitted == 0 &&
                           receipt_plan.completion_reported == 0 &&
                           receipt_plan.deadline_armed == 0 &&
                           receipt_plan.sync_submitted == 0 &&
                           receipt_plan.timeline_submitted == 0 &&
                           receipt_plan.timeline_value_published == 0 &&
                           receipt_plan.fence_submitted == 0 &&
                           receipt_plan.barrier_submitted == 0 &&
                           receipt_plan.flush_submitted == 0 &&
                           receipt_plan.framebuffer_written == 0 &&
                           receipt_plan.blit_pixels_copied == 0 &&
                           receipt_plan.output_submitted == 0 &&
                           receipt_plan.display_submitted == 0 &&
                           receipt_plan.page_flip_submitted == 0 &&
                           receipt_plan.submit_callback_bound == 0 &&
                           receipt_plan.auth_callback_bound == 0 &&
                           receipt_plan.submit_enabled == 0 &&
                           receipt_plan.auth_attempt_allowed == 0,
                       "credential receipt plan submit must stay declarative");
  fails += expect_true(strings_equal(receipt_plan.receipt_ticket,
                                     "text-login-fallback-receipt-ticket") &&
                           strings_equal(receipt_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(receipt_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential receipt plan submit should explain fallback");

  fails += expect_true(build_loginwindow_credential_screen_receipt_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &receipt_plan) == 0,
                       "credential receipt plan unknown should build");
  fails += expect_true(receipt_plan.receipt_plan_safe == 1 &&
                           receipt_plan.receipt_text_login_fallback == 1 &&
                           receipt_plan.action_allowed == 0 &&
                           receipt_plan.action_blocked == 1,
                       "credential receipt plan unknown should force text login fallback");
  fails += expect_true(strings_equal(receipt_plan.receipt_ticket,
                                     "text-login-fallback-receipt-ticket") &&
                           strings_equal(receipt_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential receipt plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_receipt_plan_fails_closed_for_unsafe_or_missing_record_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_record_plan record_plan;
  struct login_window_credential_screen_receipt_plan receipt_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_receipt_plan_build(
                           NULL, &receipt_plan) == 0,
                       "credential receipt plan missing record plan should build fail-closed state");
  fails += expect_true(receipt_plan.record_plan_available == 0 &&
                           receipt_plan.record_plan_safe == 0 &&
                           receipt_plan.receipt_plan_safe == 0 &&
                           receipt_plan.route_selected == 0 &&
                           receipt_plan.route_blocked == 1,
                       "credential receipt plan missing record plan should block receipt plan");
  fails += expect_true(receipt_plan.receipt_allowed == 0 &&
                           receipt_plan.receipt_submitted == 0 &&
                           receipt_plan.receipt_persisted == 0 &&
                           receipt_plan.record_submitted == 0 &&
                           receipt_plan.receipt_text_login_fallback == 1 &&
                           receipt_plan.submit_enabled == 0 &&
                           receipt_plan.auth_attempt_allowed == 0,
                       "credential receipt plan missing record plan must stay redacted");
  fails += expect_true(strings_equal(receipt_plan.receipt_ticket,
                                     "text-login-fallback-receipt-ticket") &&
                           strings_equal(receipt_plan.event_type,
                                         "credential-screen-receipt-plan-unavailable") &&
                           strings_equal(receipt_plan.blocked_reason,
                                         "record-plan-unavailable"),
                       "credential receipt plan missing record plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_record_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &record_plan) == 0,
                       "credential receipt plan unsafe record source should build");
  fails += expect_true(login_window_credential_screen_receipt_plan_build(
                           &record_plan, &receipt_plan) == 0,
                       "credential receipt plan unsafe record plan should build blocked state");
  fails += expect_true(receipt_plan.record_plan_available == 1 &&
                           receipt_plan.record_plan_safe == 0 &&
                           receipt_plan.receipt_plan_safe == 0 &&
                           receipt_plan.route_selected == 0 &&
                           receipt_plan.route_blocked == 1,
                       "credential receipt plan unsafe record plan should block receipt plan");
  fails += expect_true(receipt_plan.receipt_allowed == 0 &&
                           receipt_plan.receipt_submitted == 0 &&
                           receipt_plan.receipt_text_login_fallback == 1 &&
                           receipt_plan.submit_enabled == 0 &&
                           receipt_plan.auth_attempt_allowed == 0,
                       "credential receipt plan unsafe record plan must force text login fallback");
  fails += expect_true(strings_equal(receipt_plan.receipt_ticket,
                                     "text-login-fallback-receipt-ticket") &&
                           strings_equal(receipt_plan.event_type,
                                         "credential-screen-receipt-plan-unsafe") &&
                           strings_equal(receipt_plan.blocked_reason,
                                         "credential-receipt-plan-unsafe"),
                       "credential receipt plan unsafe record plan should force text login");

  fails += expect_true(build_loginwindow_credential_screen_record_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'y', 0, 0, 0,
                           1, &record_plan) == 0,
                       "credential receipt plan submitted record source should build");
  record_plan.record_submitted = 1;
  record_plan.record_persist_allowed = 1;
  record_plan.record_persisted = 1;
  record_plan.audit_submitted = 1;
  record_plan.audit_log_appended = 1;
  record_plan.seal_submitted = 1;
  record_plan.cleanup_submitted = 1;
  record_plan.retire_submitted = 1;
  record_plan.ack_submitted = 1;
  record_plan.completion_reported = 1;
  record_plan.deadline_armed = 1;
  record_plan.page_flip_allowed = 1;
  fails += expect_true(login_window_credential_screen_receipt_plan_build(
                           &record_plan, &receipt_plan) == 0,
                       "credential receipt plan submitted record should fail closed");
  fails += expect_true(receipt_plan.receipt_plan_safe == 0 &&
                           receipt_plan.receipt_allowed == 0 &&
                           receipt_plan.receipt_submitted == 0 &&
                           receipt_plan.receipt_persist_allowed == 0 &&
                           receipt_plan.receipt_persisted == 0 &&
                           receipt_plan.receipt_cpu_gpu_sync_allowed == 0 &&
                           receipt_plan.receipt_cpu_gpu_sync_submitted == 0 &&
                           receipt_plan.record_submitted == 0 &&
                           receipt_plan.record_persist_allowed == 0 &&
                           receipt_plan.record_persisted == 0 &&
                           receipt_plan.audit_submitted == 0 &&
                           receipt_plan.audit_log_appended == 0 &&
                           receipt_plan.seal_submitted == 0 &&
                           receipt_plan.cleanup_submitted == 0 &&
                           receipt_plan.retire_submitted == 0 &&
                           receipt_plan.ack_submitted == 0 &&
                           receipt_plan.completion_reported == 0 &&
                           receipt_plan.deadline_armed == 0 &&
                           receipt_plan.page_flip_allowed == 0 &&
                           receipt_plan.page_flip_submitted == 0 &&
                           receipt_plan.submit_enabled == 0 &&
                           receipt_plan.auth_attempt_allowed == 0,
                       "credential receipt plan must not copy unsafe submitted record state");
  return fails;
}

int build_loginwindow_credential_screen_ledger_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_ledger_plan *ledger_plan) {
  struct login_window_credential_screen_receipt_plan receipt_plan;

  if (build_loginwindow_credential_screen_receipt_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &receipt_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_ledger_plan_build(&receipt_plan,
                                                          ledger_plan);
}

static int test_loginwindow_credential_screen_ledger_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_ledger_plan ledger_plan;

  fails += expect_true(build_loginwindow_credential_screen_ledger_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'l', 0, 0, 0,
                           1, &ledger_plan) == 0,
                       "credential ledger plan edit should build");
  fails += expect_true(ledger_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_LEDGER_PLAN_VERSION,
                       "credential ledger plan should expose stable version");
  fails += expect_true(ledger_plan.receipt_plan_available == 1 &&
                           ledger_plan.receipt_plan_safe == 1 &&
                           ledger_plan.ledger_plan_safe == 1,
                       "credential ledger plan should require safe receipt plan");
  fails += expect_true(ledger_plan.ledger_required == 1 &&
                           ledger_plan.ledger_allowed == 1 &&
                           ledger_plan.ledger_submitted == 0 &&
                           ledger_plan.ledger_ticket_selected == 1 &&
                           ledger_plan.ledger_target_selected == 1 &&
                           ledger_plan.ledger_persist_allowed == 0 &&
                           ledger_plan.ledger_persisted == 0 &&
                           ledger_plan.ledger_cpu_gpu_sync_allowed == 0 &&
                           ledger_plan.ledger_cpu_gpu_sync_submitted == 0,
                       "credential ledger plan should remain declarative");
  fails += expect_true(ledger_plan.receipt_submitted == 0 &&
                           ledger_plan.receipt_persisted == 0 &&
                           ledger_plan.record_submitted == 0 &&
                           ledger_plan.record_persisted == 0 &&
                           ledger_plan.audit_submitted == 0 &&
                           ledger_plan.audit_log_appended == 0 &&
                           ledger_plan.seal_submitted == 0 &&
                           ledger_plan.cleanup_submitted == 0 &&
                           ledger_plan.retire_submitted == 0 &&
                           ledger_plan.ack_submitted == 0 &&
                           ledger_plan.completion_reported == 0 &&
                           ledger_plan.deadline_armed == 0 &&
                           ledger_plan.sync_submitted == 0 &&
                           ledger_plan.timeline_submitted == 0 &&
                           ledger_plan.fence_submitted == 0 &&
                           ledger_plan.barrier_submitted == 0 &&
                           ledger_plan.flush_submitted == 0 &&
                           ledger_plan.framebuffer_written == 0 &&
                           ledger_plan.blit_pixels_copied == 0 &&
                           ledger_plan.output_submitted == 0 &&
                           ledger_plan.display_mode_committed == 0 &&
                           ledger_plan.page_flip_submitted == 0,
                       "credential ledger plan must not execute GUI work");
  fails += expect_true(ledger_plan.ledger_credential_panel == 1 &&
                           ledger_plan.ledger_credential_input == 1 &&
                           ledger_plan.ledger_credential_focus == 1,
                       "credential ledger plan should mark credential widgets");
  fails += expect_true(ledger_plan.submit_callback_bound == 0 &&
                           ledger_plan.auth_callback_bound == 0 &&
                           ledger_plan.submit_enabled == 0 &&
                           ledger_plan.auth_attempt_allowed == 0 &&
                           ledger_plan.raw_secret_exposed == 0 &&
                           ledger_plan.masked_text_exposed == 0 &&
                           ledger_plan.length_redacted == 1,
                       "credential ledger plan must stay redacted and auth-disabled");
  fails += expect_true(strings_equal(ledger_plan.ledger_ticket,
                                     "credential-screen-ledger-ticket") &&
                           strings_equal(ledger_plan.receipt_ticket,
                                         "credential-screen-receipt-ticket") &&
                           strings_equal(ledger_plan.ledger_policy,
                                         "declarative-ledger-no-persist") &&
                           strings_equal(ledger_plan.state,
                                         "ledger-credential-ready"),
                       "credential ledger plan should report ledger ticket");
  return fails;
}

static int test_loginwindow_credential_screen_ledger_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_ledger_plan ledger_plan;

  fails += expect_true(build_loginwindow_credential_screen_ledger_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &ledger_plan) == 0,
                       "credential ledger plan recovery should build");
  fails += expect_true(ledger_plan.ledger_plan_safe == 1 &&
                           ledger_plan.ledger_allowed == 1 &&
                           ledger_plan.ledger_submitted == 0 &&
                           ledger_plan.ledger_persisted == 0 &&
                           ledger_plan.ledger_text_recovery == 1 &&
                           ledger_plan.ledger_text_login == 1 &&
                           ledger_plan.ledger_credential_focus == 0,
                       "credential ledger plan recovery should mark text recovery");
  fails += expect_true(ledger_plan.receipt_submitted == 0 &&
                           ledger_plan.receipt_persisted == 0 &&
                           ledger_plan.record_submitted == 0 &&
                           ledger_plan.audit_submitted == 0 &&
                           ledger_plan.audit_log_appended == 0 &&
                           ledger_plan.seal_submitted == 0 &&
                           ledger_plan.cleanup_submitted == 0 &&
                           ledger_plan.retire_submitted == 0 &&
                           ledger_plan.ack_submitted == 0 &&
                           ledger_plan.completion_reported == 0 &&
                           ledger_plan.deadline_armed == 0 &&
                           ledger_plan.sync_submitted == 0 &&
                           ledger_plan.timeline_submitted == 0 &&
                           ledger_plan.fence_submitted == 0 &&
                           ledger_plan.barrier_submitted == 0 &&
                           ledger_plan.flush_submitted == 0 &&
                           ledger_plan.framebuffer_written == 0 &&
                           ledger_plan.output_submitted == 0 &&
                           ledger_plan.display_mode_committed == 0 &&
                           ledger_plan.page_flip_submitted == 0 &&
                           ledger_plan.submit_enabled == 0 &&
                           ledger_plan.auth_attempt_allowed == 0,
                       "credential ledger plan recovery must not persist or output");
  fails += expect_true(strings_equal(ledger_plan.ledger_ticket,
                                     "text-recovery-ledger-ticket") &&
                           strings_equal(ledger_plan.compositor_target,
                                         "text-recovery-ledger") &&
                           strings_equal(ledger_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential ledger plan recovery should report recovery ticket");

  fails += expect_true(build_loginwindow_credential_screen_ledger_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &ledger_plan) == 0,
                       "credential ledger plan resume should build");
  fails += expect_true(ledger_plan.ledger_plan_safe == 1 &&
                           ledger_plan.ledger_text_login_resume == 1 &&
                           ledger_plan.session_reset_required == 1 &&
                           ledger_plan.login_screen_rerender_required == 1 &&
                           ledger_plan.ledger_submitted == 0 &&
                           ledger_plan.ledger_persisted == 0 &&
                           ledger_plan.receipt_submitted == 0 &&
                           ledger_plan.submit_enabled == 0 &&
                           ledger_plan.auth_attempt_allowed == 0,
                       "credential ledger plan resume should keep GUI auth disabled");
  fails += expect_true(strings_equal(ledger_plan.ledger_ticket,
                                     "text-login-resume-ledger-ticket") &&
                           strings_equal(ledger_plan.ledger_policy,
                                         "full-ledger-declarative") &&
                           strings_equal(ledger_plan.state,
                                         "ledger-resume-ready"),
                       "credential ledger plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_ledger_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_ledger_plan ledger_plan;

  fails += expect_true(build_loginwindow_credential_screen_ledger_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &ledger_plan) == 0,
                       "credential ledger plan submit should build");
  fails += expect_true(ledger_plan.ledger_plan_safe == 1 &&
                           ledger_plan.submit_requested == 1 &&
                           ledger_plan.ledger_text_login_fallback == 1 &&
                           ledger_plan.action_allowed == 0 &&
                           ledger_plan.action_blocked == 1 &&
                           ledger_plan.input_focus_allowed == 0,
                       "credential ledger plan submit should force text login fallback");
  fails += expect_true(ledger_plan.ledger_submitted == 0 &&
                           ledger_plan.ledger_persist_allowed == 0 &&
                           ledger_plan.ledger_persisted == 0 &&
                           ledger_plan.receipt_submitted == 0 &&
                           ledger_plan.receipt_persisted == 0 &&
                           ledger_plan.record_submitted == 0 &&
                           ledger_plan.audit_submitted == 0 &&
                           ledger_plan.audit_log_appended == 0 &&
                           ledger_plan.seal_submitted == 0 &&
                           ledger_plan.cleanup_submitted == 0 &&
                           ledger_plan.retire_submitted == 0 &&
                           ledger_plan.ack_submitted == 0 &&
                           ledger_plan.completion_reported == 0 &&
                           ledger_plan.deadline_armed == 0 &&
                           ledger_plan.sync_submitted == 0 &&
                           ledger_plan.timeline_submitted == 0 &&
                           ledger_plan.fence_submitted == 0 &&
                           ledger_plan.barrier_submitted == 0 &&
                           ledger_plan.flush_submitted == 0 &&
                           ledger_plan.framebuffer_written == 0 &&
                           ledger_plan.blit_pixels_copied == 0 &&
                           ledger_plan.output_submitted == 0 &&
                           ledger_plan.display_submitted == 0 &&
                           ledger_plan.page_flip_submitted == 0 &&
                           ledger_plan.submit_callback_bound == 0 &&
                           ledger_plan.auth_callback_bound == 0 &&
                           ledger_plan.submit_enabled == 0 &&
                           ledger_plan.auth_attempt_allowed == 0,
                       "credential ledger plan submit must stay declarative");
  fails += expect_true(strings_equal(ledger_plan.ledger_ticket,
                                     "text-login-fallback-ledger-ticket") &&
                           strings_equal(ledger_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(ledger_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential ledger plan submit should explain fallback");

  fails += expect_true(build_loginwindow_credential_screen_ledger_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &ledger_plan) == 0,
                       "credential ledger plan unknown should build");
  fails += expect_true(ledger_plan.ledger_plan_safe == 1 &&
                           ledger_plan.ledger_text_login_fallback == 1 &&
                           ledger_plan.action_allowed == 0 &&
                           ledger_plan.action_blocked == 1,
                       "credential ledger plan unknown should force text login fallback");
  fails += expect_true(strings_equal(ledger_plan.ledger_ticket,
                                     "text-login-fallback-ledger-ticket") &&
                           strings_equal(ledger_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential ledger plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_ledger_plan_fails_closed_for_unsafe_or_missing_receipt_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_receipt_plan receipt_plan;
  struct login_window_credential_screen_ledger_plan ledger_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_ledger_plan_build(
                           NULL, &ledger_plan) == 0,
                       "credential ledger plan missing receipt plan should build fail-closed state");
  fails += expect_true(ledger_plan.receipt_plan_available == 0 &&
                           ledger_plan.receipt_plan_safe == 0 &&
                           ledger_plan.ledger_plan_safe == 0 &&
                           ledger_plan.route_selected == 0 &&
                           ledger_plan.route_blocked == 1,
                       "credential ledger plan missing receipt plan should block ledger plan");
  fails += expect_true(ledger_plan.ledger_allowed == 0 &&
                           ledger_plan.ledger_submitted == 0 &&
                           ledger_plan.ledger_persisted == 0 &&
                           ledger_plan.receipt_submitted == 0 &&
                           ledger_plan.ledger_text_login_fallback == 1 &&
                           ledger_plan.submit_enabled == 0 &&
                           ledger_plan.auth_attempt_allowed == 0,
                       "credential ledger plan missing receipt plan must stay redacted");
  fails += expect_true(strings_equal(ledger_plan.ledger_ticket,
                                     "text-login-fallback-ledger-ticket") &&
                           strings_equal(ledger_plan.event_type,
                                         "credential-screen-ledger-plan-unavailable") &&
                           strings_equal(ledger_plan.blocked_reason,
                                         "receipt-plan-unavailable"),
                       "credential ledger plan missing receipt plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_receipt_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &receipt_plan) == 0,
                       "credential ledger plan unsafe receipt source should build");
  fails += expect_true(login_window_credential_screen_ledger_plan_build(
                           &receipt_plan, &ledger_plan) == 0,
                       "credential ledger plan unsafe receipt plan should build blocked state");
  fails += expect_true(ledger_plan.receipt_plan_available == 1 &&
                           ledger_plan.receipt_plan_safe == 0 &&
                           ledger_plan.ledger_plan_safe == 0 &&
                           ledger_plan.route_selected == 0 &&
                           ledger_plan.route_blocked == 1,
                       "credential ledger plan unsafe receipt plan should block ledger plan");
  fails += expect_true(ledger_plan.ledger_allowed == 0 &&
                           ledger_plan.ledger_submitted == 0 &&
                           ledger_plan.ledger_text_login_fallback == 1 &&
                           ledger_plan.submit_enabled == 0 &&
                           ledger_plan.auth_attempt_allowed == 0,
                       "credential ledger plan unsafe receipt plan must force text login fallback");
  fails += expect_true(strings_equal(ledger_plan.ledger_ticket,
                                     "text-login-fallback-ledger-ticket") &&
                           strings_equal(ledger_plan.event_type,
                                         "credential-screen-ledger-plan-unsafe") &&
                           strings_equal(ledger_plan.blocked_reason,
                                         "credential-ledger-plan-unsafe"),
                       "credential ledger plan unsafe receipt plan should force text login");

  fails += expect_true(build_loginwindow_credential_screen_receipt_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'y', 0, 0, 0,
                           1, &receipt_plan) == 0,
                       "credential ledger plan submitted receipt source should build");
  receipt_plan.receipt_submitted = 1;
  receipt_plan.receipt_persist_allowed = 1;
  receipt_plan.receipt_persisted = 1;
  receipt_plan.record_submitted = 1;
  receipt_plan.record_persisted = 1;
  receipt_plan.audit_submitted = 1;
  receipt_plan.audit_log_appended = 1;
  receipt_plan.seal_submitted = 1;
  receipt_plan.cleanup_submitted = 1;
  receipt_plan.retire_submitted = 1;
  receipt_plan.ack_submitted = 1;
  receipt_plan.completion_reported = 1;
  receipt_plan.deadline_armed = 1;
  receipt_plan.page_flip_allowed = 1;
  fails += expect_true(login_window_credential_screen_ledger_plan_build(
                           &receipt_plan, &ledger_plan) == 0,
                       "credential ledger plan submitted receipt should fail closed");
  fails += expect_true(ledger_plan.ledger_plan_safe == 0 &&
                           ledger_plan.ledger_allowed == 0 &&
                           ledger_plan.ledger_submitted == 0 &&
                           ledger_plan.ledger_persist_allowed == 0 &&
                           ledger_plan.ledger_persisted == 0 &&
                           ledger_plan.ledger_cpu_gpu_sync_allowed == 0 &&
                           ledger_plan.ledger_cpu_gpu_sync_submitted == 0 &&
                           ledger_plan.receipt_submitted == 0 &&
                           ledger_plan.receipt_persist_allowed == 0 &&
                           ledger_plan.receipt_persisted == 0 &&
                           ledger_plan.record_submitted == 0 &&
                           ledger_plan.record_persisted == 0 &&
                           ledger_plan.audit_submitted == 0 &&
                           ledger_plan.audit_log_appended == 0 &&
                           ledger_plan.seal_submitted == 0 &&
                           ledger_plan.cleanup_submitted == 0 &&
                           ledger_plan.retire_submitted == 0 &&
                           ledger_plan.ack_submitted == 0 &&
                           ledger_plan.completion_reported == 0 &&
                           ledger_plan.deadline_armed == 0 &&
                           ledger_plan.page_flip_allowed == 0 &&
                           ledger_plan.page_flip_submitted == 0 &&
                           ledger_plan.submit_enabled == 0 &&
                           ledger_plan.auth_attempt_allowed == 0,
                       "credential ledger plan must not copy unsafe submitted receipt state");
  return fails;
}

int test_login_runtime_credential_receipt_ledger_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_receipt_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_receipt_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_receipt_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_receipt_plan_fails_closed_for_unsafe_or_missing_record_plan();
  fails += test_loginwindow_credential_screen_ledger_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_ledger_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_ledger_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_ledger_plan_fails_closed_for_unsafe_or_missing_receipt_plan();
  return fails;
}
