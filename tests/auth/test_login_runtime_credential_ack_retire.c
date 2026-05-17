/*
 * tests/auth/test_login_runtime_credential_ack_retire.c
 *
 * Credential screen ack plan + retire plan coverage for the
 * `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-16 monolith
 * refactor (PR D.23 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_ack_plan_build`: 4 tests
 *     covering the credential widgets ack + the text-route ack
 *     (recovery + resume) + the submit/unknown fallback ack + the
 *     missing-or-unsafe completion plan fail-closed default.
 *   - `login_window_credential_screen_retire_plan_build`: 4 tests
 *     covering the credential widgets retire + the text-route
 *     retire (recovery + resume) + the submit/unknown fallback
 *     retire + the missing-or-unsafe ack plan fail-closed default.
 *
 * Also exposes shared helpers
 * `build_loginwindow_credential_screen_ack_plan_for_action` and
 * `build_loginwindow_credential_screen_retire_plan_for_action`,
 * used by later companion files that chain on top of the
 * ack/retire stages (cleanup, ...).
 *
 * The companion entry `test_login_runtime_credential_ack_retire_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_ack_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_ack_plan *ack_plan) {
  struct login_window_credential_screen_completion_plan completion_plan;

  if (build_loginwindow_credential_screen_completion_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &completion_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_ack_plan_build(&completion_plan,
                                                       ack_plan);
}

static int test_loginwindow_credential_screen_ack_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_ack_plan ack_plan;

  fails += expect_true(build_loginwindow_credential_screen_ack_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'a', 0, 0, 0,
                           1, &ack_plan) == 0,
                       "credential ack plan edit should build");
  fails += expect_true(ack_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_ACK_PLAN_VERSION,
                       "credential ack plan should expose stable version");
  fails += expect_true(ack_plan.completion_plan_available == 1 &&
                           ack_plan.completion_plan_safe == 1 &&
                           ack_plan.ack_plan_safe == 1,
                       "credential ack plan should require safe completion plan");
  fails += expect_true(ack_plan.ack_required == 1 &&
                           ack_plan.ack_allowed == 1 &&
                           ack_plan.ack_submitted == 0 &&
                           ack_plan.ack_ticket_selected == 1 &&
                           ack_plan.ack_target_selected == 1 &&
                           ack_plan.ack_cpu_gpu_sync_allowed == 0 &&
                           ack_plan.ack_cpu_gpu_sync_submitted == 0,
                       "credential ack plan should remain declarative");
  fails += expect_true(ack_plan.completion_reported == 0 &&
                           ack_plan.completion_acknowledged == 0 &&
                           ack_plan.completion_cpu_gpu_sync_submitted == 0 &&
                           ack_plan.deadline_armed == 0 &&
                           ack_plan.deadline_timer_armed == 0 &&
                           ack_plan.deadline_expired == 0 &&
                           ack_plan.deadline_completion_reported == 0 &&
                           ack_plan.sync_submitted == 0 &&
                           ack_plan.sync_wait_allowed == 0 &&
                           ack_plan.sync_signal_allowed == 0 &&
                           ack_plan.sync_completion_reported == 0 &&
                           ack_plan.timeline_submitted == 0 &&
                           ack_plan.timeline_value_published == 0 &&
                           ack_plan.fence_submitted == 0 &&
                           ack_plan.barrier_submitted == 0 &&
                           ack_plan.flush_submitted == 0 &&
                           ack_plan.framebuffer_mapped == 0 &&
                           ack_plan.framebuffer_written == 0 &&
                           ack_plan.framebuffer_flushed == 0 &&
                           ack_plan.blit_pixels_copied == 0 &&
                           ack_plan.output_submitted == 0 &&
                           ack_plan.display_mode_committed == 0 &&
                           ack_plan.scanout_submitted == 0 &&
                           ack_plan.vsync_submitted == 0 &&
                           ack_plan.schedule_submitted == 0 &&
                           ack_plan.present_submitted == 0 &&
                           ack_plan.damage_submitted == 0 &&
                           ack_plan.page_flip_submitted == 0,
                       "credential ack plan must not execute GUI work");
  fails += expect_true(ack_plan.ack_credential_panel == 1 &&
                           ack_plan.ack_credential_input == 1 &&
                           ack_plan.ack_credential_focus == 1,
                       "credential ack plan should mark credential widgets");
  fails += expect_true(ack_plan.submit_callback_bound == 0 &&
                           ack_plan.auth_callback_bound == 0 &&
                           ack_plan.submit_enabled == 0 &&
                           ack_plan.auth_attempt_allowed == 0 &&
                           ack_plan.raw_secret_exposed == 0 &&
                           ack_plan.masked_text_exposed == 0 &&
                           ack_plan.length_redacted == 1,
                       "credential ack plan must stay redacted and auth-disabled");
  fails += expect_true(strings_equal(ack_plan.ack_ticket,
                                     "credential-screen-ack-ticket") &&
                           strings_equal(ack_plan.completion_ticket,
                                         "credential-screen-completion-ticket") &&
                           strings_equal(ack_plan.ack_policy,
                                         "declarative-ack-no-submit") &&
                           strings_equal(ack_plan.state,
                                         "ack-credential-ready"),
                       "credential ack plan should report ack ticket");
  return fails;
}

static int test_loginwindow_credential_screen_ack_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_ack_plan ack_plan;

  fails += expect_true(build_loginwindow_credential_screen_ack_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &ack_plan) == 0,
                       "credential ack plan recovery should build");
  fails += expect_true(ack_plan.ack_plan_safe == 1 &&
                           ack_plan.ack_allowed == 1 &&
                           ack_plan.ack_submitted == 0 &&
                           ack_plan.ack_text_recovery == 1 &&
                           ack_plan.ack_text_login == 1 &&
                           ack_plan.ack_credential_focus == 0,
                       "credential ack plan recovery should mark text recovery");
  fails += expect_true(ack_plan.completion_reported == 0 &&
                           ack_plan.completion_acknowledged == 0 &&
                           ack_plan.ack_cpu_gpu_sync_submitted == 0 &&
                           ack_plan.deadline_armed == 0 &&
                           ack_plan.deadline_timer_armed == 0 &&
                           ack_plan.sync_submitted == 0 &&
                           ack_plan.timeline_submitted == 0 &&
                           ack_plan.fence_submitted == 0 &&
                           ack_plan.barrier_submitted == 0 &&
                           ack_plan.flush_submitted == 0 &&
                           ack_plan.framebuffer_written == 0 &&
                           ack_plan.output_submitted == 0 &&
                           ack_plan.display_mode_committed == 0 &&
                           ack_plan.page_flip_submitted == 0 &&
                           ack_plan.submit_enabled == 0 &&
                           ack_plan.auth_attempt_allowed == 0,
                       "credential ack plan recovery must not submit ack or output");
  fails += expect_true(strings_equal(ack_plan.ack_ticket,
                                     "text-recovery-ack-ticket") &&
                           strings_equal(ack_plan.compositor_target,
                                         "text-recovery-ack") &&
                           strings_equal(ack_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential ack plan recovery should report recovery ticket");

  fails += expect_true(build_loginwindow_credential_screen_ack_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'k', 1, 1, 0,
                           1, &ack_plan) == 0,
                       "credential ack plan resume should build");
  fails += expect_true(ack_plan.ack_plan_safe == 1 &&
                           ack_plan.ack_text_login_resume == 1 &&
                           ack_plan.session_reset_required == 1 &&
                           ack_plan.login_screen_rerender_required == 1 &&
                           ack_plan.ack_submitted == 0 &&
                           ack_plan.completion_acknowledged == 0 &&
                           ack_plan.ack_cpu_gpu_sync_submitted == 0 &&
                           ack_plan.submit_enabled == 0 &&
                           ack_plan.auth_attempt_allowed == 0,
                       "credential ack plan resume should keep GUI auth disabled");
  fails += expect_true(strings_equal(ack_plan.ack_ticket,
                                     "text-login-resume-ack-ticket") &&
                           strings_equal(ack_plan.ack_policy,
                                         "full-ack-declarative") &&
                           strings_equal(ack_plan.state,
                                         "ack-resume-ready"),
                       "credential ack plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_ack_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_ack_plan ack_plan;

  fails += expect_true(build_loginwindow_credential_screen_ack_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &ack_plan) == 0,
                       "credential ack plan submit should build");
  fails += expect_true(ack_plan.ack_plan_safe == 1 &&
                           ack_plan.submit_requested == 1 &&
                           ack_plan.ack_text_login_fallback == 1 &&
                           ack_plan.action_allowed == 0 &&
                           ack_plan.action_blocked == 1 &&
                           ack_plan.input_focus_allowed == 0,
                       "credential ack plan submit should force text login fallback");
  fails += expect_true(ack_plan.ack_submitted == 0 &&
                           ack_plan.completion_reported == 0 &&
                           ack_plan.completion_acknowledged == 0 &&
                           ack_plan.ack_cpu_gpu_sync_allowed == 0 &&
                           ack_plan.deadline_armed == 0 &&
                           ack_plan.deadline_timer_armed == 0 &&
                           ack_plan.sync_submitted == 0 &&
                           ack_plan.timeline_submitted == 0 &&
                           ack_plan.timeline_value_allocated == 0 &&
                           ack_plan.timeline_value_published == 0 &&
                           ack_plan.fence_submitted == 0 &&
                           ack_plan.barrier_submitted == 0 &&
                           ack_plan.flush_submitted == 0 &&
                           ack_plan.framebuffer_written == 0 &&
                           ack_plan.blit_pixels_copied == 0 &&
                           ack_plan.output_submitted == 0 &&
                           ack_plan.display_submitted == 0 &&
                           ack_plan.page_flip_submitted == 0 &&
                           ack_plan.submit_callback_bound == 0 &&
                           ack_plan.auth_callback_bound == 0 &&
                           ack_plan.submit_enabled == 0 &&
                           ack_plan.auth_attempt_allowed == 0,
                       "credential ack plan submit must stay declarative");
  fails += expect_true(strings_equal(ack_plan.ack_ticket,
                                     "text-login-fallback-ack-ticket") &&
                           strings_equal(ack_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(ack_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential ack plan submit should explain fallback");

  fails += expect_true(build_loginwindow_credential_screen_ack_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &ack_plan) == 0,
                       "credential ack plan unknown should build");
  fails += expect_true(ack_plan.ack_plan_safe == 1 &&
                           ack_plan.ack_text_login_fallback == 1 &&
                           ack_plan.action_allowed == 0 &&
                           ack_plan.action_blocked == 1,
                       "credential ack plan unknown should force text login fallback");
  fails += expect_true(strings_equal(ack_plan.ack_ticket,
                                     "text-login-fallback-ack-ticket") &&
                           strings_equal(ack_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential ack plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_ack_plan_fails_closed_for_unsafe_or_missing_completion_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_completion_plan completion_plan;
  struct login_window_credential_screen_ack_plan ack_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_ack_plan_build(
                           NULL, &ack_plan) == 0,
                       "credential ack plan missing completion plan should build fail-closed state");
  fails += expect_true(ack_plan.completion_plan_available == 0 &&
                           ack_plan.completion_plan_safe == 0 &&
                           ack_plan.ack_plan_safe == 0 &&
                           ack_plan.route_selected == 0 &&
                           ack_plan.route_blocked == 1,
                       "credential ack plan missing completion plan should block ack plan");
  fails += expect_true(ack_plan.ack_allowed == 0 &&
                           ack_plan.ack_submitted == 0 &&
                           ack_plan.completion_acknowledged == 0 &&
                           ack_plan.ack_cpu_gpu_sync_allowed == 0 &&
                           ack_plan.ack_text_login_fallback == 1 &&
                           ack_plan.submit_enabled == 0 &&
                           ack_plan.auth_attempt_allowed == 0,
                       "credential ack plan missing completion plan must stay redacted");
  fails += expect_true(strings_equal(ack_plan.ack_ticket,
                                     "text-login-fallback-ack-ticket") &&
                           strings_equal(ack_plan.event_type,
                                         "credential-screen-ack-plan-unavailable") &&
                           strings_equal(ack_plan.blocked_reason,
                                         "completion-plan-unavailable"),
                       "credential ack plan missing completion plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_completion_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &completion_plan) == 0,
                       "credential ack plan unsafe completion source should build");
  fails += expect_true(login_window_credential_screen_ack_plan_build(
                           &completion_plan, &ack_plan) == 0,
                       "credential ack plan unsafe completion plan should build blocked state");
  fails += expect_true(ack_plan.completion_plan_available == 1 &&
                           ack_plan.completion_plan_safe == 0 &&
                           ack_plan.ack_plan_safe == 0 &&
                           ack_plan.route_selected == 0 &&
                           ack_plan.route_blocked == 1,
                       "credential ack plan unsafe completion plan should block ack plan");
  fails += expect_true(ack_plan.ack_allowed == 0 &&
                           ack_plan.ack_submitted == 0 &&
                           ack_plan.ack_text_login_fallback == 1 &&
                           ack_plan.submit_enabled == 0 &&
                           ack_plan.auth_attempt_allowed == 0,
                       "credential ack plan unsafe completion plan must force text login fallback");
  fails += expect_true(strings_equal(ack_plan.ack_ticket,
                                     "text-login-fallback-ack-ticket") &&
                           strings_equal(ack_plan.event_type,
                                         "credential-screen-ack-plan-unsafe") &&
                           strings_equal(ack_plan.blocked_reason,
                                         "credential-ack-plan-unsafe"),
                       "credential ack plan unsafe completion plan should force text login");

  fails += expect_true(build_loginwindow_credential_screen_completion_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'y', 0, 0, 0,
                           1, &completion_plan) == 0,
                       "credential ack plan acknowledged completion source should build");
  completion_plan.completion_reported = 1;
  completion_plan.completion_acknowledged = 1;
  completion_plan.completion_cpu_gpu_sync_allowed = 1;
  completion_plan.deadline_armed = 1;
  completion_plan.page_flip_allowed = 1;
  fails += expect_true(login_window_credential_screen_ack_plan_build(
                           &completion_plan, &ack_plan) == 0,
                       "credential ack plan acknowledged completion should fail closed");
  fails += expect_true(ack_plan.ack_plan_safe == 0 &&
                           ack_plan.ack_allowed == 0 &&
                           ack_plan.ack_submitted == 0 &&
                           ack_plan.ack_cpu_gpu_sync_allowed == 0 &&
                           ack_plan.ack_cpu_gpu_sync_submitted == 0 &&
                           ack_plan.completion_reported == 0 &&
                           ack_plan.completion_acknowledged == 0 &&
                           ack_plan.completion_cpu_gpu_sync_allowed == 0 &&
                           ack_plan.deadline_armed == 0 &&
                           ack_plan.page_flip_allowed == 0 &&
                           ack_plan.page_flip_submitted == 0 &&
                           ack_plan.submit_enabled == 0 &&
                           ack_plan.auth_attempt_allowed == 0,
                       "credential ack plan must not copy unsafe acknowledged state");
  return fails;
}


int build_loginwindow_credential_screen_retire_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_retire_plan *retire_plan) {
  struct login_window_credential_screen_ack_plan ack_plan;

  if (build_loginwindow_credential_screen_ack_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &ack_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_retire_plan_build(&ack_plan,
                                                          retire_plan);
}

static int test_loginwindow_credential_screen_retire_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_retire_plan retire_plan;

  fails += expect_true(build_loginwindow_credential_screen_retire_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 0, 0, 0,
                           1, &retire_plan) == 0,
                       "credential retire plan edit should build");
  fails += expect_true(retire_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_RETIRE_PLAN_VERSION,
                       "credential retire plan should expose stable version");
  fails += expect_true(retire_plan.ack_plan_available == 1 &&
                           retire_plan.ack_plan_safe == 1 &&
                           retire_plan.retire_plan_safe == 1,
                       "credential retire plan should require safe ack plan");
  fails += expect_true(retire_plan.retire_required == 1 &&
                           retire_plan.retire_allowed == 1 &&
                           retire_plan.retire_submitted == 0 &&
                           retire_plan.retire_ticket_selected == 1 &&
                           retire_plan.retire_target_selected == 1 &&
                           retire_plan.retire_resource_release_allowed == 0 &&
                           retire_plan.retire_resource_released == 0 &&
                           retire_plan.retire_cpu_gpu_sync_allowed == 0 &&
                           retire_plan.retire_cpu_gpu_sync_submitted == 0,
                       "credential retire plan should remain declarative");
  fails += expect_true(retire_plan.ack_submitted == 0 &&
                           retire_plan.ack_cpu_gpu_sync_submitted == 0 &&
                           retire_plan.completion_reported == 0 &&
                           retire_plan.completion_acknowledged == 0 &&
                           retire_plan.completion_cpu_gpu_sync_submitted == 0 &&
                           retire_plan.deadline_armed == 0 &&
                           retire_plan.deadline_timer_armed == 0 &&
                           retire_plan.deadline_expired == 0 &&
                           retire_plan.deadline_completion_reported == 0 &&
                           retire_plan.sync_submitted == 0 &&
                           retire_plan.sync_wait_allowed == 0 &&
                           retire_plan.sync_signal_allowed == 0 &&
                           retire_plan.sync_completion_reported == 0 &&
                           retire_plan.timeline_submitted == 0 &&
                           retire_plan.timeline_value_published == 0 &&
                           retire_plan.fence_submitted == 0 &&
                           retire_plan.barrier_submitted == 0 &&
                           retire_plan.flush_submitted == 0 &&
                           retire_plan.framebuffer_mapped == 0 &&
                           retire_plan.framebuffer_written == 0 &&
                           retire_plan.framebuffer_flushed == 0 &&
                           retire_plan.blit_pixels_copied == 0 &&
                           retire_plan.output_submitted == 0 &&
                           retire_plan.display_mode_committed == 0 &&
                           retire_plan.scanout_submitted == 0 &&
                           retire_plan.vsync_submitted == 0 &&
                           retire_plan.schedule_submitted == 0 &&
                           retire_plan.present_submitted == 0 &&
                           retire_plan.damage_submitted == 0 &&
                           retire_plan.page_flip_submitted == 0,
                       "credential retire plan must not execute GUI work");
  fails += expect_true(retire_plan.retire_credential_panel == 1 &&
                           retire_plan.retire_credential_input == 1 &&
                           retire_plan.retire_credential_focus == 1,
                       "credential retire plan should mark credential widgets");
  fails += expect_true(retire_plan.submit_callback_bound == 0 &&
                           retire_plan.auth_callback_bound == 0 &&
                           retire_plan.submit_enabled == 0 &&
                           retire_plan.auth_attempt_allowed == 0 &&
                           retire_plan.raw_secret_exposed == 0 &&
                           retire_plan.masked_text_exposed == 0 &&
                           retire_plan.length_redacted == 1,
                       "credential retire plan must stay redacted and auth-disabled");
  fails += expect_true(strings_equal(retire_plan.retire_ticket,
                                     "credential-screen-retire-ticket") &&
                           strings_equal(retire_plan.ack_ticket,
                                         "credential-screen-ack-ticket") &&
                           strings_equal(retire_plan.retire_policy,
                                         "declarative-retire-no-release") &&
                           strings_equal(retire_plan.state,
                                         "retire-credential-ready"),
                       "credential retire plan should report retire ticket");
  return fails;
}

static int test_loginwindow_credential_screen_retire_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_retire_plan retire_plan;

  fails += expect_true(build_loginwindow_credential_screen_retire_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &retire_plan) == 0,
                       "credential retire plan recovery should build");
  fails += expect_true(retire_plan.retire_plan_safe == 1 &&
                           retire_plan.retire_allowed == 1 &&
                           retire_plan.retire_submitted == 0 &&
                           retire_plan.retire_resource_released == 0 &&
                           retire_plan.retire_text_recovery == 1 &&
                           retire_plan.retire_text_login == 1 &&
                           retire_plan.retire_credential_focus == 0,
                       "credential retire plan recovery should mark text recovery");
  fails += expect_true(retire_plan.ack_submitted == 0 &&
                           retire_plan.completion_reported == 0 &&
                           retire_plan.completion_acknowledged == 0 &&
                           retire_plan.retire_cpu_gpu_sync_submitted == 0 &&
                           retire_plan.deadline_armed == 0 &&
                           retire_plan.deadline_timer_armed == 0 &&
                           retire_plan.sync_submitted == 0 &&
                           retire_plan.timeline_submitted == 0 &&
                           retire_plan.fence_submitted == 0 &&
                           retire_plan.barrier_submitted == 0 &&
                           retire_plan.flush_submitted == 0 &&
                           retire_plan.framebuffer_written == 0 &&
                           retire_plan.output_submitted == 0 &&
                           retire_plan.display_mode_committed == 0 &&
                           retire_plan.page_flip_submitted == 0 &&
                           retire_plan.submit_enabled == 0 &&
                           retire_plan.auth_attempt_allowed == 0,
                       "credential retire plan recovery must not release resources or output");
  fails += expect_true(strings_equal(retire_plan.retire_ticket,
                                     "text-recovery-retire-ticket") &&
                           strings_equal(retire_plan.compositor_target,
                                         "text-recovery-retire") &&
                           strings_equal(retire_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential retire plan recovery should report recovery ticket");

  fails += expect_true(build_loginwindow_credential_screen_retire_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 't', 1, 1, 0,
                           1, &retire_plan) == 0,
                       "credential retire plan resume should build");
  fails += expect_true(retire_plan.retire_plan_safe == 1 &&
                           retire_plan.retire_text_login_resume == 1 &&
                           retire_plan.session_reset_required == 1 &&
                           retire_plan.login_screen_rerender_required == 1 &&
                           retire_plan.retire_submitted == 0 &&
                           retire_plan.retire_resource_released == 0 &&
                           retire_plan.ack_submitted == 0 &&
                           retire_plan.submit_enabled == 0 &&
                           retire_plan.auth_attempt_allowed == 0,
                       "credential retire plan resume should keep GUI auth disabled");
  fails += expect_true(strings_equal(retire_plan.retire_ticket,
                                     "text-login-resume-retire-ticket") &&
                           strings_equal(retire_plan.retire_policy,
                                         "full-retire-declarative") &&
                           strings_equal(retire_plan.state,
                                         "retire-resume-ready"),
                       "credential retire plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_retire_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_retire_plan retire_plan;

  fails += expect_true(build_loginwindow_credential_screen_retire_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &retire_plan) == 0,
                       "credential retire plan submit should build");
  fails += expect_true(retire_plan.retire_plan_safe == 1 &&
                           retire_plan.submit_requested == 1 &&
                           retire_plan.retire_text_login_fallback == 1 &&
                           retire_plan.action_allowed == 0 &&
                           retire_plan.action_blocked == 1 &&
                           retire_plan.input_focus_allowed == 0,
                       "credential retire plan submit should force text login fallback");
  fails += expect_true(retire_plan.retire_submitted == 0 &&
                           retire_plan.retire_resource_release_allowed == 0 &&
                           retire_plan.retire_resource_released == 0 &&
                           retire_plan.ack_submitted == 0 &&
                           retire_plan.completion_reported == 0 &&
                           retire_plan.completion_acknowledged == 0 &&
                           retire_plan.deadline_armed == 0 &&
                           retire_plan.sync_submitted == 0 &&
                           retire_plan.timeline_submitted == 0 &&
                           retire_plan.timeline_value_published == 0 &&
                           retire_plan.fence_submitted == 0 &&
                           retire_plan.barrier_submitted == 0 &&
                           retire_plan.flush_submitted == 0 &&
                           retire_plan.framebuffer_written == 0 &&
                           retire_plan.blit_pixels_copied == 0 &&
                           retire_plan.output_submitted == 0 &&
                           retire_plan.display_submitted == 0 &&
                           retire_plan.page_flip_submitted == 0 &&
                           retire_plan.submit_callback_bound == 0 &&
                           retire_plan.auth_callback_bound == 0 &&
                           retire_plan.submit_enabled == 0 &&
                           retire_plan.auth_attempt_allowed == 0,
                       "credential retire plan submit must stay declarative");
  fails += expect_true(strings_equal(retire_plan.retire_ticket,
                                     "text-login-fallback-retire-ticket") &&
                           strings_equal(retire_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(retire_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential retire plan submit should explain fallback");

  fails += expect_true(build_loginwindow_credential_screen_retire_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &retire_plan) == 0,
                       "credential retire plan unknown should build");
  fails += expect_true(retire_plan.retire_plan_safe == 1 &&
                           retire_plan.retire_text_login_fallback == 1 &&
                           retire_plan.action_allowed == 0 &&
                           retire_plan.action_blocked == 1,
                       "credential retire plan unknown should force text login fallback");
  fails += expect_true(strings_equal(retire_plan.retire_ticket,
                                     "text-login-fallback-retire-ticket") &&
                           strings_equal(retire_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential retire plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_retire_plan_fails_closed_for_unsafe_or_missing_ack_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_ack_plan ack_plan;
  struct login_window_credential_screen_retire_plan retire_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_retire_plan_build(
                           NULL, &retire_plan) == 0,
                       "credential retire plan missing ack plan should build fail-closed state");
  fails += expect_true(retire_plan.ack_plan_available == 0 &&
                           retire_plan.ack_plan_safe == 0 &&
                           retire_plan.retire_plan_safe == 0 &&
                           retire_plan.route_selected == 0 &&
                           retire_plan.route_blocked == 1,
                       "credential retire plan missing ack plan should block retire plan");
  fails += expect_true(retire_plan.retire_allowed == 0 &&
                           retire_plan.retire_submitted == 0 &&
                           retire_plan.retire_resource_released == 0 &&
                           retire_plan.ack_submitted == 0 &&
                           retire_plan.retire_text_login_fallback == 1 &&
                           retire_plan.submit_enabled == 0 &&
                           retire_plan.auth_attempt_allowed == 0,
                       "credential retire plan missing ack plan must stay redacted");
  fails += expect_true(strings_equal(retire_plan.retire_ticket,
                                     "text-login-fallback-retire-ticket") &&
                           strings_equal(retire_plan.event_type,
                                         "credential-screen-retire-plan-unavailable") &&
                           strings_equal(retire_plan.blocked_reason,
                                         "ack-plan-unavailable"),
                       "credential retire plan missing ack plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_ack_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &ack_plan) == 0,
                       "credential retire plan unsafe ack source should build");
  fails += expect_true(login_window_credential_screen_retire_plan_build(
                           &ack_plan, &retire_plan) == 0,
                       "credential retire plan unsafe ack plan should build blocked state");
  fails += expect_true(retire_plan.ack_plan_available == 1 &&
                           retire_plan.ack_plan_safe == 0 &&
                           retire_plan.retire_plan_safe == 0 &&
                           retire_plan.route_selected == 0 &&
                           retire_plan.route_blocked == 1,
                       "credential retire plan unsafe ack plan should block retire plan");
  fails += expect_true(retire_plan.retire_allowed == 0 &&
                           retire_plan.retire_submitted == 0 &&
                           retire_plan.retire_text_login_fallback == 1 &&
                           retire_plan.submit_enabled == 0 &&
                           retire_plan.auth_attempt_allowed == 0,
                       "credential retire plan unsafe ack plan must force text login fallback");
  fails += expect_true(strings_equal(retire_plan.retire_ticket,
                                     "text-login-fallback-retire-ticket") &&
                           strings_equal(retire_plan.event_type,
                                         "credential-screen-retire-plan-unsafe") &&
                           strings_equal(retire_plan.blocked_reason,
                                         "credential-retire-plan-unsafe"),
                       "credential retire plan unsafe ack plan should force text login");

  fails += expect_true(build_loginwindow_credential_screen_ack_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'y', 0, 0, 0,
                           1, &ack_plan) == 0,
                       "credential retire plan submitted ack source should build");
  ack_plan.ack_submitted = 1;
  ack_plan.ack_cpu_gpu_sync_allowed = 1;
  ack_plan.completion_reported = 1;
  ack_plan.deadline_armed = 1;
  ack_plan.page_flip_allowed = 1;
  fails += expect_true(login_window_credential_screen_retire_plan_build(
                           &ack_plan, &retire_plan) == 0,
                       "credential retire plan submitted ack should fail closed");
  fails += expect_true(retire_plan.retire_plan_safe == 0 &&
                           retire_plan.retire_allowed == 0 &&
                           retire_plan.retire_submitted == 0 &&
                           retire_plan.retire_resource_release_allowed == 0 &&
                           retire_plan.retire_resource_released == 0 &&
                           retire_plan.retire_cpu_gpu_sync_allowed == 0 &&
                           retire_plan.retire_cpu_gpu_sync_submitted == 0 &&
                           retire_plan.ack_submitted == 0 &&
                           retire_plan.ack_cpu_gpu_sync_allowed == 0 &&
                           retire_plan.completion_reported == 0 &&
                           retire_plan.deadline_armed == 0 &&
                           retire_plan.page_flip_allowed == 0 &&
                           retire_plan.page_flip_submitted == 0 &&
                           retire_plan.submit_enabled == 0 &&
                           retire_plan.auth_attempt_allowed == 0,
                       "credential retire plan must not copy unsafe submitted ack state");
  return fails;
}

int test_login_runtime_credential_ack_retire_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_ack_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_ack_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_ack_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_ack_plan_fails_closed_for_unsafe_or_missing_completion_plan();
  fails += test_loginwindow_credential_screen_retire_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_retire_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_retire_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_retire_plan_fails_closed_for_unsafe_or_missing_ack_plan();
  return fails;
}
