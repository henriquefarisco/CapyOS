/*
 * tests/auth/test_login_runtime_credential_compaction_reclaim.c
 *
 * Credential screen compaction plan + reclaim plan coverage for the
 * `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-16 monolith
 * refactor (PR D.31 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_compaction_plan_build`: 4 tests
 *     covering the credential widgets compaction + the text-route
 *     compaction (recovery + resume) + the submit/unknown fallback
 *     compaction + the missing-or-unsafe tombstone plan fail-closed
 *     default.
 *   - `login_window_credential_screen_reclaim_plan_build`: 4 tests
 *     covering the credential widgets reclaim + the text-route
 *     reclaim (recovery + resume) + the submit/unknown fallback
 *     reclaim + the missing-or-unsafe compaction plan fail-closed
 *     default.
 *
 * Also exposes shared helpers
 * `build_loginwindow_credential_screen_compaction_plan_for_action`
 * and `build_loginwindow_credential_screen_reclaim_plan_for_action`,
 * used by later companion files that chain on top of the
 * compaction/reclaim stages (release, ...).
 *
 * The companion entry `test_login_runtime_credential_compaction_reclaim_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_compaction_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_compaction_plan *compaction_plan) {
  struct login_window_credential_screen_tombstone_plan tombstone_plan;

  if (build_loginwindow_credential_screen_tombstone_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &tombstone_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_compaction_plan_build(&tombstone_plan,
                                                             compaction_plan);
}

static int test_loginwindow_credential_screen_compaction_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_compaction_plan compaction_plan;

  fails += expect_true(build_loginwindow_credential_screen_compaction_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'c', 0, 0, 0,
                           1, &compaction_plan) == 0,
                       "credential compaction plan edit should build");
  fails += expect_true(compaction_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_COMPACTION_PLAN_VERSION,
                       "credential compaction plan should expose stable version");
  fails += expect_true(compaction_plan.tombstone_plan_available == 1 &&
                           compaction_plan.tombstone_plan_safe == 1 &&
                           compaction_plan.compaction_plan_safe == 1,
                       "credential compaction plan should require safe tombstone plan");
  fails += expect_true(compaction_plan.compaction_required == 1 &&
                           compaction_plan.compaction_allowed == 1 &&
                           compaction_plan.compaction_submitted == 0 &&
                           compaction_plan.compaction_ticket_selected == 1 &&
                           compaction_plan.compaction_target_selected == 1 &&
                           compaction_plan.compaction_storage_write_allowed == 0 &&
                           compaction_plan.compaction_storage_written == 0 &&
                           compaction_plan.compaction_resource_release_allowed == 0 &&
                           compaction_plan.compaction_resource_released == 0 &&
                           compaction_plan.compaction_cpu_gpu_sync_allowed == 0 &&
                           compaction_plan.compaction_cpu_gpu_sync_submitted == 0,
                       "credential compaction plan should remain declarative");
  fails += expect_true(compaction_plan.tombstone_allowed == 1 &&
                           compaction_plan.tombstone_submitted == 0 &&
                           compaction_plan.tombstone_persisted == 0 &&
                           compaction_plan.tombstone_cpu_gpu_sync_submitted == 0,
                       "credential compaction plan must not execute tombstone work");
  fails += expect_true(compaction_plan.compaction_credential_panel == 1 &&
                           compaction_plan.compaction_credential_input == 1 &&
                           compaction_plan.compaction_credential_focus == 1 &&
                           compaction_plan.compaction_text_login == 0 &&
                           compaction_plan.compaction_text_login_fallback == 0,
                       "credential compaction plan should mark credential widgets");
  fails += expect_true(compaction_plan.submit_callback_bound == 0 &&
                           compaction_plan.auth_callback_bound == 0 &&
                           compaction_plan.submit_enabled == 0 &&
                           compaction_plan.auth_attempt_allowed == 0 &&
                           compaction_plan.raw_secret_exposed == 0 &&
                           compaction_plan.masked_text_exposed == 0 &&
                           compaction_plan.length_redacted == 1,
                       "credential compaction plan must stay redacted and auth-disabled");
  fails += expect_true(strings_equal(compaction_plan.compaction_ticket,
                                     "credential-screen-compaction-ticket") &&
                           strings_equal(compaction_plan.tombstone_ticket,
                                         "credential-screen-tombstone-ticket") &&
                           strings_equal(compaction_plan.compaction_policy,
                                         "declarative-compaction-no-write") &&
                           strings_equal(compaction_plan.state,
                                         "compaction-credential-ready"),
                       "credential compaction plan should report compaction ticket");
  return fails;
}

static int test_loginwindow_credential_screen_compaction_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_compaction_plan compaction_plan;

  fails += expect_true(build_loginwindow_credential_screen_compaction_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &compaction_plan) == 0,
                       "credential compaction plan recovery should build");
  fails += expect_true(compaction_plan.compaction_plan_safe == 1 &&
                           compaction_plan.compaction_allowed == 1 &&
                           compaction_plan.compaction_submitted == 0 &&
                           compaction_plan.compaction_storage_written == 0 &&
                           compaction_plan.compaction_text_recovery == 1 &&
                           compaction_plan.compaction_text_login == 1 &&
                           compaction_plan.compaction_credential_focus == 0,
                       "credential compaction plan recovery should mark text recovery");
  fails += expect_true(compaction_plan.tombstone_submitted == 0 &&
                           compaction_plan.tombstone_persisted == 0 &&
                           compaction_plan.compaction_resource_released == 0 &&
                           compaction_plan.compaction_cpu_gpu_sync_submitted == 0 &&
                           compaction_plan.submit_enabled == 0 &&
                           compaction_plan.auth_attempt_allowed == 0,
                       "credential compaction plan recovery must not write or authenticate");
  fails += expect_true(strings_equal(compaction_plan.compaction_ticket,
                                     "text-recovery-compaction-ticket") &&
                           strings_equal(compaction_plan.compositor_target,
                                         "text-recovery-compaction") &&
                           strings_equal(compaction_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential compaction plan recovery should report recovery ticket");

  fails += expect_true(build_loginwindow_credential_screen_compaction_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &compaction_plan) == 0,
                       "credential compaction plan resume should build");
  fails += expect_true(compaction_plan.compaction_plan_safe == 1 &&
                           compaction_plan.compaction_text_login_resume == 1 &&
                           compaction_plan.session_reset_required == 1 &&
                           compaction_plan.login_screen_rerender_required == 1 &&
                           compaction_plan.compaction_submitted == 0 &&
                           compaction_plan.compaction_storage_written == 0 &&
                           compaction_plan.submit_enabled == 0 &&
                           compaction_plan.auth_attempt_allowed == 0,
                       "credential compaction plan resume should keep GUI auth disabled");
  fails += expect_true(strings_equal(compaction_plan.compaction_ticket,
                                     "text-login-resume-compaction-ticket") &&
                           strings_equal(compaction_plan.compaction_policy,
                                         "full-compaction-declarative") &&
                           strings_equal(compaction_plan.state,
                                         "compaction-resume-ready"),
                       "credential compaction plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_compaction_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_compaction_plan compaction_plan;

  fails += expect_true(build_loginwindow_credential_screen_compaction_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &compaction_plan) == 0,
                       "credential compaction plan submit should build");
  fails += expect_true(compaction_plan.compaction_plan_safe == 1 &&
                           compaction_plan.submit_requested == 1 &&
                           compaction_plan.compaction_text_login_fallback == 1 &&
                           compaction_plan.action_allowed == 0 &&
                           compaction_plan.action_blocked == 1 &&
                           compaction_plan.input_focus_allowed == 0,
                       "credential compaction plan submit should force text login fallback");
  fails += expect_true(compaction_plan.compaction_submitted == 0 &&
                           compaction_plan.compaction_storage_write_allowed == 0 &&
                           compaction_plan.compaction_storage_written == 0 &&
                           compaction_plan.compaction_resource_release_allowed == 0 &&
                           compaction_plan.compaction_resource_released == 0 &&
                           compaction_plan.compaction_cpu_gpu_sync_allowed == 0 &&
                           compaction_plan.compaction_cpu_gpu_sync_submitted == 0 &&
                           compaction_plan.tombstone_submitted == 0 &&
                           compaction_plan.tombstone_persisted == 0 &&
                           compaction_plan.submit_callback_bound == 0 &&
                           compaction_plan.auth_callback_bound == 0 &&
                           compaction_plan.submit_enabled == 0 &&
                           compaction_plan.auth_attempt_allowed == 0,
                       "credential compaction plan submit must stay declarative");
  fails += expect_true(strings_equal(compaction_plan.compaction_ticket,
                                     "text-login-fallback-compaction-ticket") &&
                           strings_equal(compaction_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(compaction_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential compaction plan submit should explain fallback");

  fails += expect_true(build_loginwindow_credential_screen_compaction_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &compaction_plan) == 0,
                       "credential compaction plan unknown should build");
  fails += expect_true(compaction_plan.compaction_plan_safe == 1 &&
                           compaction_plan.compaction_text_login_fallback == 1 &&
                           compaction_plan.action_allowed == 0 &&
                           compaction_plan.action_blocked == 1,
                       "credential compaction plan unknown should force text login fallback");
  fails += expect_true(strings_equal(compaction_plan.compaction_ticket,
                                     "text-login-fallback-compaction-ticket") &&
                           strings_equal(compaction_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential compaction plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_compaction_plan_fails_closed_for_unsafe_or_missing_tombstone_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_tombstone_plan tombstone_plan;
  struct login_window_credential_screen_compaction_plan compaction_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_compaction_plan_build(
                           NULL, &compaction_plan) == 0,
                       "credential compaction plan missing tombstone plan should build fail-closed state");
  fails += expect_true(compaction_plan.tombstone_plan_available == 0 &&
                           compaction_plan.tombstone_plan_safe == 0 &&
                           compaction_plan.compaction_plan_safe == 0 &&
                           compaction_plan.route_selected == 0 &&
                           compaction_plan.route_blocked == 1,
                       "credential compaction plan missing tombstone plan should block compaction plan");
  fails += expect_true(compaction_plan.compaction_allowed == 0 &&
                           compaction_plan.compaction_submitted == 0 &&
                           compaction_plan.compaction_storage_written == 0 &&
                           compaction_plan.compaction_text_login_fallback == 1 &&
                           compaction_plan.submit_enabled == 0 &&
                           compaction_plan.auth_attempt_allowed == 0,
                       "credential compaction plan missing tombstone plan must stay redacted");
  fails += expect_true(strings_equal(compaction_plan.compaction_ticket,
                                     "text-login-fallback-compaction-ticket") &&
                           strings_equal(compaction_plan.event_type,
                                         "credential-screen-compaction-plan-unavailable") &&
                           strings_equal(compaction_plan.blocked_reason,
                                         "tombstone-plan-unavailable"),
                       "credential compaction plan missing tombstone plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_tombstone_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'z', 0, 0, 0,
                           0, &tombstone_plan) == 0,
                       "credential compaction plan unsafe tombstone source should build");
  fails += expect_true(login_window_credential_screen_compaction_plan_build(
                           &tombstone_plan, &compaction_plan) == 0,
                       "credential compaction plan unsafe tombstone plan should build blocked state");
  fails += expect_true(compaction_plan.tombstone_plan_available == 1 &&
                           compaction_plan.tombstone_plan_safe == 0 &&
                           compaction_plan.compaction_plan_safe == 0 &&
                           compaction_plan.route_selected == 0 &&
                           compaction_plan.route_blocked == 1,
                       "credential compaction plan unsafe tombstone plan should block compaction plan");
  fails += expect_true(compaction_plan.compaction_allowed == 0 &&
                           compaction_plan.compaction_submitted == 0 &&
                           compaction_plan.compaction_text_login_fallback == 1 &&
                           compaction_plan.submit_enabled == 0 &&
                           compaction_plan.auth_attempt_allowed == 0,
                       "credential compaction plan unsafe tombstone plan must force text login fallback");
  fails += expect_true(strings_equal(compaction_plan.compaction_ticket,
                                     "text-login-fallback-compaction-ticket") &&
                           strings_equal(compaction_plan.event_type,
                                         "credential-screen-compaction-plan-unsafe") &&
                           strings_equal(compaction_plan.blocked_reason,
                                         "credential-compaction-plan-unsafe"),
                       "credential compaction plan unsafe tombstone plan should force text login");

  fails += expect_true(build_loginwindow_credential_screen_tombstone_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'q', 0, 0, 0,
                           1, &tombstone_plan) == 0,
                       "credential compaction plan submitted tombstone source should build");
  tombstone_plan.tombstone_submitted = 1;
  tombstone_plan.tombstone_persist_allowed = 1;
  tombstone_plan.tombstone_persisted = 1;
  tombstone_plan.tombstone_cpu_gpu_sync_allowed = 1;
  tombstone_plan.tombstone_cpu_gpu_sync_submitted = 1;
  tombstone_plan.purge_submitted = 1;
  tombstone_plan.purge_persist_allowed = 1;
  tombstone_plan.purge_persisted = 1;
  tombstone_plan.purge_deleted = 1;
  tombstone_plan.expiry_submitted = 1;
  tombstone_plan.expiry_timer_armed = 1;
  tombstone_plan.audit_submitted = 1;
  tombstone_plan.audit_log_appended = 1;
  tombstone_plan.seal_submitted = 1;
  tombstone_plan.cleanup_submitted = 1;
  tombstone_plan.retire_submitted = 1;
  tombstone_plan.ack_submitted = 1;
  tombstone_plan.completion_reported = 1;
  tombstone_plan.deadline_armed = 1;
  tombstone_plan.sync_submitted = 1;
  tombstone_plan.timeline_submitted = 1;
  tombstone_plan.fence_submitted = 1;
  tombstone_plan.barrier_submitted = 1;
  tombstone_plan.flush_submitted = 1;
  tombstone_plan.framebuffer_written = 1;
  tombstone_plan.blit_pixels_copied = 1;
  tombstone_plan.output_submitted = 1;
  tombstone_plan.display_submitted = 1;
  tombstone_plan.page_flip_submitted = 1;
  fails += expect_true(login_window_credential_screen_compaction_plan_build(
                           &tombstone_plan, &compaction_plan) == 0,
                       "credential compaction plan submitted tombstone should fail closed");
  fails += expect_true(compaction_plan.compaction_plan_safe == 0 &&
                           compaction_plan.compaction_allowed == 0 &&
                           compaction_plan.compaction_submitted == 0 &&
                           compaction_plan.compaction_storage_write_allowed == 0 &&
                           compaction_plan.compaction_storage_written == 0 &&
                           compaction_plan.compaction_resource_release_allowed == 0 &&
                           compaction_plan.compaction_resource_released == 0 &&
                           compaction_plan.compaction_cpu_gpu_sync_allowed == 0 &&
                           compaction_plan.compaction_cpu_gpu_sync_submitted == 0 &&
                           compaction_plan.tombstone_submitted == 0 &&
                           compaction_plan.tombstone_persist_allowed == 0 &&
                           compaction_plan.tombstone_persisted == 0 &&
                           compaction_plan.tombstone_cpu_gpu_sync_allowed == 0 &&
                           compaction_plan.tombstone_cpu_gpu_sync_submitted == 0 &&
                           compaction_plan.submit_enabled == 0 &&
                           compaction_plan.auth_attempt_allowed == 0,
                       "credential compaction plan must not copy unsafe submitted tombstone state");
  fails += expect_true(strings_equal(compaction_plan.compaction_ticket,
                                     "text-login-fallback-compaction-ticket") &&
                           strings_equal(compaction_plan.event_type,
                                         "credential-screen-compaction-plan-unsafe") &&
                           strings_equal(compaction_plan.blocked_reason,
                                         "credential-compaction-plan-unsafe"),
                       "credential compaction plan unsafe tombstone should force text login");
  return fails;
}

int build_loginwindow_credential_screen_reclaim_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_reclaim_plan *reclaim_plan) {
  struct login_window_credential_screen_compaction_plan compaction_plan;

  if (build_loginwindow_credential_screen_compaction_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &compaction_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_reclaim_plan_build(&compaction_plan,
                                                          reclaim_plan);
}

static int test_loginwindow_credential_screen_reclaim_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_reclaim_plan reclaim_plan;

  fails += expect_true(build_loginwindow_credential_screen_reclaim_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'c', 0, 0, 0,
                           1, &reclaim_plan) == 0,
                       "credential reclaim plan edit should build");
  fails += expect_true(reclaim_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_RECLAIM_PLAN_VERSION,
                       "credential reclaim plan should expose stable version");
  fails += expect_true(reclaim_plan.compaction_plan_available == 1 &&
                           reclaim_plan.compaction_plan_safe == 1 &&
                           reclaim_plan.reclaim_plan_safe == 1,
                       "credential reclaim plan should require safe compaction plan");
  fails += expect_true(reclaim_plan.reclaim_required == 1 &&
                           reclaim_plan.reclaim_allowed == 1 &&
                           reclaim_plan.reclaim_submitted == 0 &&
                           reclaim_plan.reclaim_ticket_selected == 1 &&
                           reclaim_plan.reclaim_target_selected == 1 &&
                           reclaim_plan.reclaim_storage_prune_allowed == 0 &&
                           reclaim_plan.reclaim_storage_pruned == 0 &&
                           reclaim_plan.reclaim_resource_release_allowed == 0 &&
                           reclaim_plan.reclaim_resource_released == 0 &&
                           reclaim_plan.reclaim_cpu_gpu_sync_allowed == 0 &&
                           reclaim_plan.reclaim_cpu_gpu_sync_submitted == 0,
                       "credential reclaim plan should remain declarative");
  fails += expect_true(reclaim_plan.compaction_allowed == 1 &&
                           reclaim_plan.compaction_submitted == 0 &&
                           reclaim_plan.compaction_storage_written == 0 &&
                           reclaim_plan.compaction_resource_released == 0,
                       "credential reclaim plan must not execute compaction work");
  fails += expect_true(reclaim_plan.reclaim_credential_panel == 1 &&
                           reclaim_plan.reclaim_credential_input == 1 &&
                           reclaim_plan.reclaim_credential_focus == 1 &&
                           reclaim_plan.reclaim_text_login == 0 &&
                           reclaim_plan.reclaim_text_login_fallback == 0,
                       "credential reclaim plan should mark credential widgets");
  fails += expect_true(reclaim_plan.submit_callback_bound == 0 &&
                           reclaim_plan.auth_callback_bound == 0 &&
                           reclaim_plan.submit_enabled == 0 &&
                           reclaim_plan.auth_attempt_allowed == 0 &&
                           reclaim_plan.raw_secret_exposed == 0 &&
                           reclaim_plan.masked_text_exposed == 0,
                       "credential reclaim plan must stay redacted and auth-disabled");
  fails += expect_true(strings_equal(reclaim_plan.reclaim_ticket,
                                     "credential-screen-reclaim-ticket") &&
                           strings_equal(reclaim_plan.compaction_ticket,
                                         "credential-screen-compaction-ticket") &&
                           strings_equal(reclaim_plan.reclaim_policy,
                                         "declarative-reclaim-no-release") &&
                           strings_equal(reclaim_plan.state,
                                         "reclaim-credential-ready"),
                       "credential reclaim plan should report reclaim ticket");
  return fails;
}

static int test_loginwindow_credential_screen_reclaim_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_reclaim_plan reclaim_plan;

  fails += expect_true(build_loginwindow_credential_screen_reclaim_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &reclaim_plan) == 0,
                       "credential reclaim plan recovery should build");
  fails += expect_true(reclaim_plan.reclaim_plan_safe == 1 &&
                           reclaim_plan.reclaim_allowed == 1 &&
                           reclaim_plan.reclaim_submitted == 0 &&
                           reclaim_plan.reclaim_storage_pruned == 0 &&
                           reclaim_plan.reclaim_text_recovery == 1 &&
                           reclaim_plan.reclaim_text_login == 1 &&
                           reclaim_plan.reclaim_credential_focus == 0,
                       "credential reclaim plan recovery should mark text recovery");
  fails += expect_true(reclaim_plan.compaction_submitted == 0 &&
                           reclaim_plan.compaction_storage_written == 0 &&
                           reclaim_plan.reclaim_resource_released == 0 &&
                           reclaim_plan.reclaim_cpu_gpu_sync_submitted == 0 &&
                           reclaim_plan.submit_enabled == 0 &&
                           reclaim_plan.auth_attempt_allowed == 0,
                       "credential reclaim plan recovery must not write or authenticate");
  fails += expect_true(strings_equal(reclaim_plan.reclaim_ticket,
                                     "text-recovery-reclaim-ticket") &&
                           strings_equal(reclaim_plan.compositor_target,
                                         "text-recovery-reclaim") &&
                           strings_equal(reclaim_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential reclaim plan recovery should report recovery ticket");

  fails += expect_true(build_loginwindow_credential_screen_reclaim_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &reclaim_plan) == 0,
                       "credential reclaim plan resume should build");
  fails += expect_true(reclaim_plan.reclaim_plan_safe == 1 &&
                           reclaim_plan.reclaim_text_login_resume == 1 &&
                           reclaim_plan.session_reset_required == 1 &&
                           reclaim_plan.login_screen_rerender_required == 1 &&
                           reclaim_plan.reclaim_submitted == 0 &&
                           reclaim_plan.reclaim_storage_pruned == 0 &&
                           reclaim_plan.submit_enabled == 0 &&
                           reclaim_plan.auth_attempt_allowed == 0,
                       "credential reclaim plan resume should keep GUI auth disabled");
  fails += expect_true(strings_equal(reclaim_plan.reclaim_ticket,
                                     "text-login-resume-reclaim-ticket") &&
                           strings_equal(reclaim_plan.reclaim_policy,
                                         "full-reclaim-declarative") &&
                           strings_equal(reclaim_plan.state,
                                         "reclaim-resume-ready"),
                       "credential reclaim plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_reclaim_plan_falls_back_for_submit_and_unknown_action(void) {
  int fails = 0;
  struct login_window_credential_screen_reclaim_plan reclaim_plan;

  fails += expect_true(build_loginwindow_credential_screen_reclaim_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_SUBMIT, 0, 0, 0, 0,
                           1, &reclaim_plan) == 0,
                       "credential reclaim plan submit should build");
  fails += expect_true(reclaim_plan.reclaim_plan_safe == 1 &&
                           reclaim_plan.submit_requested == 1 &&
                           reclaim_plan.submit_blocked == 1 &&
                           reclaim_plan.action_allowed == 0 &&
                           reclaim_plan.action_blocked == 1 &&
                           reclaim_plan.input_focus_allowed == 0 &&
                           reclaim_plan.reclaim_text_login == 1 &&
                           reclaim_plan.reclaim_text_login_fallback == 1 &&
                           reclaim_plan.reclaim_submitted == 0 &&
                           reclaim_plan.reclaim_resource_released == 0,
                       "credential reclaim plan submit should force text login");
  fails += expect_true(strings_equal(reclaim_plan.reclaim_ticket,
                                     "text-login-fallback-reclaim-ticket") &&
                           strings_equal(reclaim_plan.reclaim_policy,
                                         "fallback-reclaim-declarative") &&
                           strings_equal(reclaim_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential reclaim plan submit should report disabled GUI submit");

  fails += expect_true(build_loginwindow_credential_screen_reclaim_plan_for_action(
                           9876, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0,
                           0, 0, 1, &reclaim_plan) == 0,
                       "credential reclaim plan unknown action should build");
  fails += expect_true(reclaim_plan.reclaim_plan_safe == 1 &&
                           reclaim_plan.action_allowed == 0 &&
                           reclaim_plan.action_blocked == 1 &&
                           reclaim_plan.input_focus_allowed == 0 &&
                           reclaim_plan.reclaim_text_login == 1 &&
                           reclaim_plan.reclaim_text_login_fallback == 1 &&
                           reclaim_plan.reclaim_submitted == 0 &&
                           reclaim_plan.reclaim_storage_pruned == 0 &&
                           reclaim_plan.reclaim_cpu_gpu_sync_submitted == 0,
                       "credential reclaim plan unknown action should force text login");
  fails += expect_true(strings_equal(reclaim_plan.reclaim_ticket,
                                     "text-login-fallback-reclaim-ticket") &&
                           strings_equal(reclaim_plan.compositor_target,
                                         "text-login-fallback-reclaim") &&
                           strings_equal(reclaim_plan.state,
                                         "reclaim-text-login-ready"),
                       "credential reclaim plan unknown action should report fallback ticket");
  return fails;
}

static int test_loginwindow_credential_screen_reclaim_plan_fails_closed_for_unsafe_or_missing_compaction_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_compaction_plan compaction_plan;
  struct login_window_credential_screen_reclaim_plan reclaim_plan;

  fails += expect_true(login_window_credential_screen_reclaim_plan_build(
                           NULL, &reclaim_plan) == 0,
                       "credential reclaim plan missing compaction should build fallback");
  fails += expect_true(reclaim_plan.compaction_plan_available == 0 &&
                           reclaim_plan.compaction_plan_safe == 0 &&
                           reclaim_plan.reclaim_plan_safe == 0 &&
                           reclaim_plan.route_blocked == 1 &&
                           reclaim_plan.reclaim_allowed == 0 &&
                           reclaim_plan.reclaim_ticket_selected == 0 &&
                           reclaim_plan.reclaim_target_selected == 0 &&
                           reclaim_plan.reclaim_text_login == 1 &&
                           reclaim_plan.reclaim_text_login_fallback == 1 &&
                           reclaim_plan.reclaim_submitted == 0 &&
                           reclaim_plan.reclaim_storage_pruned == 0 &&
                           reclaim_plan.reclaim_resource_released == 0,
                       "credential reclaim plan missing compaction should fail closed");
  fails += expect_true(strings_equal(reclaim_plan.reclaim_ticket,
                                     "text-login-fallback-reclaim-ticket") &&
                           strings_equal(reclaim_plan.event_type,
                                         "credential-screen-reclaim-plan-unavailable") &&
                           strings_equal(reclaim_plan.blocked_reason,
                                         "compaction-plan-unavailable"),
                       "credential reclaim plan missing compaction should report missing upstream");

  fails += expect_true(build_loginwindow_credential_screen_compaction_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0, 0,
                           1, &compaction_plan) == 0,
                       "credential reclaim plan unsafe compaction fixture should build");
  compaction_plan.compaction_plan_safe = 0;
  compaction_plan.raw_secret_exposed = 1;
  compaction_plan.submit_blocked = 0;
  fails += expect_true(login_window_credential_screen_reclaim_plan_build(
                           &compaction_plan, &reclaim_plan) == 0,
                       "credential reclaim plan unsafe compaction should build fallback");
  fails += expect_true(reclaim_plan.compaction_plan_available == 1 &&
                           reclaim_plan.compaction_plan_safe == 0 &&
                           reclaim_plan.reclaim_plan_safe == 0 &&
                           reclaim_plan.route_blocked == 1 &&
                           reclaim_plan.reclaim_allowed == 0 &&
                           reclaim_plan.reclaim_ticket_selected == 0 &&
                           reclaim_plan.reclaim_target_selected == 0 &&
                           reclaim_plan.reclaim_text_login == 1 &&
                           reclaim_plan.reclaim_text_login_fallback == 1 &&
                           reclaim_plan.reclaim_submitted == 0 &&
                           reclaim_plan.reclaim_storage_pruned == 0 &&
                           reclaim_plan.reclaim_resource_released == 0 &&
                           reclaim_plan.raw_secret_exposed == 0,
                       "credential reclaim plan unsafe compaction should fail closed");
  fails += expect_true(strings_equal(reclaim_plan.reclaim_ticket,
                                     "text-login-fallback-reclaim-ticket") &&
                           strings_equal(reclaim_plan.event_type,
                                         "credential-screen-reclaim-plan-unsafe") &&
                           strings_equal(reclaim_plan.blocked_reason,
                                         "credential-reclaim-plan-unsafe"),
                       "credential reclaim plan unsafe compaction should report unsafe upstream");

  fails += expect_true(build_loginwindow_credential_screen_compaction_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 0, 0, 0,
                           1, &compaction_plan) == 0,
                       "credential reclaim plan submitted compaction fixture should build");
  compaction_plan.compaction_submitted = 1;
  compaction_plan.compaction_storage_write_allowed = 1;
  compaction_plan.compaction_storage_written = 1;
  compaction_plan.compaction_resource_release_allowed = 1;
  compaction_plan.compaction_resource_released = 1;
  compaction_plan.compaction_cpu_gpu_sync_allowed = 1;
  compaction_plan.compaction_cpu_gpu_sync_submitted = 1;
  compaction_plan.tombstone_submitted = 1;
  compaction_plan.tombstone_persist_allowed = 1;
  compaction_plan.tombstone_persisted = 1;
  compaction_plan.tombstone_cpu_gpu_sync_allowed = 1;
  compaction_plan.tombstone_cpu_gpu_sync_submitted = 1;
  fails += expect_true(login_window_credential_screen_reclaim_plan_build(
                           &compaction_plan, &reclaim_plan) == 0,
                       "credential reclaim plan submitted compaction should build fallback");
  fails += expect_true(reclaim_plan.reclaim_plan_safe == 0 &&
                           reclaim_plan.reclaim_allowed == 0 &&
                           reclaim_plan.reclaim_submitted == 0 &&
                           reclaim_plan.reclaim_storage_prune_allowed == 0 &&
                           reclaim_plan.reclaim_storage_pruned == 0 &&
                           reclaim_plan.reclaim_resource_release_allowed == 0 &&
                           reclaim_plan.reclaim_resource_released == 0 &&
                           reclaim_plan.reclaim_cpu_gpu_sync_allowed == 0 &&
                           reclaim_plan.reclaim_cpu_gpu_sync_submitted == 0 &&
                           reclaim_plan.compaction_submitted == 0 &&
                           reclaim_plan.compaction_storage_write_allowed == 0 &&
                           reclaim_plan.compaction_storage_written == 0 &&
                           reclaim_plan.compaction_resource_release_allowed == 0 &&
                           reclaim_plan.compaction_resource_released == 0 &&
                           reclaim_plan.compaction_cpu_gpu_sync_allowed == 0 &&
                           reclaim_plan.compaction_cpu_gpu_sync_submitted == 0 &&
                           reclaim_plan.submit_enabled == 0 &&
                           reclaim_plan.auth_attempt_allowed == 0,
                       "credential reclaim plan must not copy unsafe submitted compaction state");
  fails += expect_true(strings_equal(reclaim_plan.reclaim_ticket,
                                     "text-login-fallback-reclaim-ticket") &&
                           strings_equal(reclaim_plan.event_type,
                                         "credential-screen-reclaim-plan-unsafe") &&
                           strings_equal(reclaim_plan.blocked_reason,
                                         "credential-reclaim-plan-unsafe"),
                       "credential reclaim plan submitted compaction should report unsafe upstream");
  return fails;
}

int test_login_runtime_credential_compaction_reclaim_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_compaction_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_compaction_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_compaction_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_compaction_plan_fails_closed_for_unsafe_or_missing_tombstone_plan();
  fails += test_loginwindow_credential_screen_reclaim_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_reclaim_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_reclaim_plan_falls_back_for_submit_and_unknown_action();
  fails += test_loginwindow_credential_screen_reclaim_plan_fails_closed_for_unsafe_or_missing_compaction_plan();
  return fails;
}
