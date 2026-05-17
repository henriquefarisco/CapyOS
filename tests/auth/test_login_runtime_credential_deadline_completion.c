/*
 * tests/auth/test_login_runtime_credential_deadline_completion.c
 *
 * Credential screen deadline plan + completion plan coverage for
 * the `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-16 monolith
 * refactor (PR D.22 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_deadline_plan_build`: 4 tests
 *     covering the credential widgets deadline + the text-route
 *     deadline (recovery + resume) + the submit/unknown fallback
 *     deadline + the missing-or-unsafe sync plan fail-closed
 *     default.
 *   - `login_window_credential_screen_completion_plan_build`: 4 tests
 *     covering the credential widgets completion + the text-route
 *     completion (recovery + resume) + the submit/unknown fallback
 *     completion + the missing-or-unsafe deadline plan fail-closed
 *     default.
 *
 * Also exposes shared helpers
 * `build_loginwindow_credential_screen_deadline_plan_for_action` and
 * `build_loginwindow_credential_screen_completion_plan_for_action`,
 * used by later companion files that chain on top of the
 * deadline/completion stages (ack, ...).
 *
 * The companion entry `test_login_runtime_credential_deadline_completion_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_deadline_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_deadline_plan *deadline_plan) {
  struct login_window_credential_screen_sync_plan sync_plan;

  if (build_loginwindow_credential_screen_sync_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &sync_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_deadline_plan_build(&sync_plan,
                                                            deadline_plan);
}

static int test_loginwindow_credential_screen_deadline_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_deadline_plan deadline_plan;

  fails += expect_true(build_loginwindow_credential_screen_deadline_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'd', 0, 0, 0,
                           1, &deadline_plan) == 0,
                       "credential deadline plan edit should build");
  fails += expect_true(deadline_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_DEADLINE_PLAN_VERSION,
                       "credential deadline plan should expose stable version");
  fails += expect_true(deadline_plan.sync_plan_available == 1 &&
                           deadline_plan.sync_plan_safe == 1 &&
                           deadline_plan.deadline_plan_safe == 1,
                       "credential deadline plan should require safe sync plan");
  fails += expect_true(deadline_plan.deadline_required == 1 &&
                           deadline_plan.deadline_allowed == 1 &&
                           deadline_plan.deadline_armed == 0 &&
                           deadline_plan.deadline_ticket_selected == 1 &&
                           deadline_plan.deadline_target_selected == 1 &&
                           deadline_plan.deadline_timer_required == 1 &&
                           deadline_plan.deadline_timer_armed == 0 &&
                           deadline_plan.deadline_expired == 0 &&
                           deadline_plan.deadline_completion_required == 1 &&
                           deadline_plan.deadline_completion_reported == 0 &&
                           deadline_plan.deadline_cpu_gpu_sync_allowed == 0 &&
                           deadline_plan.deadline_cpu_gpu_sync_submitted == 0,
                       "credential deadline plan should remain declarative");
  fails += expect_true(deadline_plan.sync_submitted == 0 &&
                           deadline_plan.sync_wait_allowed == 0 &&
                           deadline_plan.sync_wait_submitted == 0 &&
                           deadline_plan.sync_signal_allowed == 0 &&
                           deadline_plan.sync_signal_submitted == 0 &&
                           deadline_plan.sync_deadline_armed == 0 &&
                           deadline_plan.sync_completion_reported == 0 &&
                           deadline_plan.sync_cpu_gpu_sync_submitted == 0 &&
                           deadline_plan.timeline_submitted == 0 &&
                           deadline_plan.timeline_value_published == 0 &&
                           deadline_plan.fence_submitted == 0 &&
                           deadline_plan.barrier_submitted == 0 &&
                           deadline_plan.flush_submitted == 0 &&
                           deadline_plan.framebuffer_mapped == 0 &&
                           deadline_plan.framebuffer_written == 0 &&
                           deadline_plan.framebuffer_flushed == 0 &&
                           deadline_plan.blit_pixels_copied == 0 &&
                           deadline_plan.output_submitted == 0 &&
                           deadline_plan.display_mode_committed == 0 &&
                           deadline_plan.scanout_submitted == 0 &&
                           deadline_plan.vsync_submitted == 0 &&
                           deadline_plan.schedule_submitted == 0 &&
                           deadline_plan.present_submitted == 0 &&
                           deadline_plan.damage_submitted == 0 &&
                           deadline_plan.page_flip_submitted == 0,
                       "credential deadline plan must not execute GUI work");
  fails += expect_true(deadline_plan.deadline_credential_panel == 1 &&
                           deadline_plan.deadline_credential_input == 1 &&
                           deadline_plan.deadline_credential_focus == 1,
                       "credential deadline plan should mark credential widgets");
  fails += expect_true(deadline_plan.submit_callback_bound == 0 &&
                           deadline_plan.auth_callback_bound == 0 &&
                           deadline_plan.submit_enabled == 0 &&
                           deadline_plan.auth_attempt_allowed == 0 &&
                           deadline_plan.raw_secret_exposed == 0 &&
                           deadline_plan.masked_text_exposed == 0 &&
                           deadline_plan.length_redacted == 1,
                       "credential deadline plan must stay redacted and auth-disabled");
  fails += expect_true(strings_equal(deadline_plan.deadline_ticket,
                                     "credential-screen-deadline-ticket") &&
                           strings_equal(deadline_plan.sync_ticket,
                                         "credential-screen-sync-ticket") &&
                           strings_equal(deadline_plan.deadline_policy,
                                         "declarative-deadline-no-arm") &&
                           strings_equal(deadline_plan.state,
                                         "deadline-credential-ready"),
                       "credential deadline plan should report deadline ticket");
  return fails;
}

static int test_loginwindow_credential_screen_deadline_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_deadline_plan deadline_plan;

  fails += expect_true(build_loginwindow_credential_screen_deadline_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &deadline_plan) == 0,
                       "credential deadline plan recovery should build");
  fails += expect_true(deadline_plan.deadline_plan_safe == 1 &&
                           deadline_plan.deadline_allowed == 1 &&
                           deadline_plan.deadline_armed == 0 &&
                           deadline_plan.deadline_text_recovery == 1 &&
                           deadline_plan.deadline_text_login == 1 &&
                           deadline_plan.deadline_credential_focus == 0,
                       "credential deadline plan recovery should mark text recovery");
  fails += expect_true(deadline_plan.deadline_timer_armed == 0 &&
                           deadline_plan.deadline_expired == 0 &&
                           deadline_plan.deadline_completion_reported == 0 &&
                           deadline_plan.deadline_cpu_gpu_sync_submitted == 0 &&
                           deadline_plan.sync_submitted == 0 &&
                           deadline_plan.sync_deadline_armed == 0 &&
                           deadline_plan.sync_completion_reported == 0 &&
                           deadline_plan.timeline_submitted == 0 &&
                           deadline_plan.timeline_value_published == 0 &&
                           deadline_plan.fence_submitted == 0 &&
                           deadline_plan.barrier_submitted == 0 &&
                           deadline_plan.flush_submitted == 0 &&
                           deadline_plan.framebuffer_flushed == 0 &&
                           deadline_plan.framebuffer_written == 0 &&
                           deadline_plan.output_submitted == 0 &&
                           deadline_plan.display_mode_committed == 0 &&
                           deadline_plan.page_flip_submitted == 0 &&
                           deadline_plan.submit_enabled == 0 &&
                           deadline_plan.auth_attempt_allowed == 0,
                       "credential deadline plan recovery must not arm deadline or output");
  fails += expect_true(strings_equal(deadline_plan.deadline_ticket,
                                     "text-recovery-deadline-ticket") &&
                           strings_equal(deadline_plan.compositor_target,
                                         "text-recovery-deadline") &&
                           strings_equal(deadline_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential deadline plan recovery should report recovery ticket");

  fails += expect_true(build_loginwindow_credential_screen_deadline_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &deadline_plan) == 0,
                       "credential deadline plan resume should build");
  fails += expect_true(deadline_plan.deadline_plan_safe == 1 &&
                           deadline_plan.deadline_text_login_resume == 1 &&
                           deadline_plan.session_reset_required == 1 &&
                           deadline_plan.login_screen_rerender_required == 1 &&
                           deadline_plan.deadline_armed == 0 &&
                           deadline_plan.deadline_timer_armed == 0 &&
                           deadline_plan.deadline_completion_reported == 0 &&
                           deadline_plan.deadline_cpu_gpu_sync_submitted == 0 &&
                           deadline_plan.submit_enabled == 0 &&
                           deadline_plan.auth_attempt_allowed == 0,
                       "credential deadline plan resume should keep GUI auth disabled");
  fails += expect_true(strings_equal(deadline_plan.deadline_ticket,
                                     "text-login-resume-deadline-ticket") &&
                           strings_equal(deadline_plan.deadline_policy,
                                         "full-deadline-declarative") &&
                           strings_equal(deadline_plan.state,
                                         "deadline-resume-ready"),
                       "credential deadline plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_deadline_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_deadline_plan deadline_plan;

  fails += expect_true(build_loginwindow_credential_screen_deadline_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &deadline_plan) == 0,
                       "credential deadline plan submit should build");
  fails += expect_true(deadline_plan.deadline_plan_safe == 1 &&
                           deadline_plan.submit_requested == 1 &&
                           deadline_plan.deadline_text_login_fallback == 1 &&
                           deadline_plan.action_allowed == 0 &&
                           deadline_plan.action_blocked == 1 &&
                           deadline_plan.input_focus_allowed == 0,
                       "credential deadline plan submit should force text login fallback");
  fails += expect_true(deadline_plan.deadline_armed == 0 &&
                           deadline_plan.deadline_timer_armed == 0 &&
                           deadline_plan.deadline_completion_reported == 0 &&
                           deadline_plan.deadline_cpu_gpu_sync_allowed == 0 &&
                           deadline_plan.sync_submitted == 0 &&
                           deadline_plan.sync_wait_allowed == 0 &&
                           deadline_plan.sync_signal_allowed == 0 &&
                           deadline_plan.sync_deadline_armed == 0 &&
                           deadline_plan.sync_completion_reported == 0 &&
                           deadline_plan.timeline_submitted == 0 &&
                           deadline_plan.timeline_value_allocated == 0 &&
                           deadline_plan.timeline_value_published == 0 &&
                           deadline_plan.fence_submitted == 0 &&
                           deadline_plan.barrier_submitted == 0 &&
                           deadline_plan.flush_submitted == 0 &&
                           deadline_plan.framebuffer_written == 0 &&
                           deadline_plan.blit_pixels_copied == 0 &&
                           deadline_plan.output_submitted == 0 &&
                           deadline_plan.display_submitted == 0 &&
                           deadline_plan.page_flip_submitted == 0 &&
                           deadline_plan.submit_callback_bound == 0 &&
                           deadline_plan.auth_callback_bound == 0 &&
                           deadline_plan.submit_enabled == 0 &&
                           deadline_plan.auth_attempt_allowed == 0,
                       "credential deadline plan submit must stay declarative");
  fails += expect_true(strings_equal(deadline_plan.deadline_ticket,
                                     "text-login-fallback-deadline-ticket") &&
                           strings_equal(deadline_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(deadline_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential deadline plan submit should explain fallback");

  fails += expect_true(build_loginwindow_credential_screen_deadline_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &deadline_plan) == 0,
                       "credential deadline plan unknown should build");
  fails += expect_true(deadline_plan.deadline_plan_safe == 1 &&
                           deadline_plan.deadline_text_login_fallback == 1 &&
                           deadline_plan.action_allowed == 0 &&
                           deadline_plan.action_blocked == 1,
                       "credential deadline plan unknown should force text login fallback");
  fails += expect_true(strings_equal(deadline_plan.deadline_ticket,
                                     "text-login-fallback-deadline-ticket") &&
                           strings_equal(deadline_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential deadline plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_deadline_plan_fails_closed_for_unsafe_or_missing_sync_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_sync_plan sync_plan;
  struct login_window_credential_screen_deadline_plan deadline_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_deadline_plan_build(
                           NULL, &deadline_plan) == 0,
                       "credential deadline plan missing sync plan should build fail-closed state");
  fails += expect_true(deadline_plan.sync_plan_available == 0 &&
                           deadline_plan.sync_plan_safe == 0 &&
                           deadline_plan.deadline_plan_safe == 0 &&
                           deadline_plan.route_selected == 0 &&
                           deadline_plan.route_blocked == 1,
                       "credential deadline plan missing sync plan should block deadline plan");
  fails += expect_true(deadline_plan.deadline_allowed == 0 &&
                           deadline_plan.deadline_armed == 0 &&
                           deadline_plan.deadline_timer_armed == 0 &&
                           deadline_plan.deadline_completion_reported == 0 &&
                           deadline_plan.deadline_text_login_fallback == 1 &&
                           deadline_plan.submit_enabled == 0 &&
                           deadline_plan.auth_attempt_allowed == 0,
                       "credential deadline plan missing sync plan must stay redacted");
  fails += expect_true(strings_equal(deadline_plan.deadline_ticket,
                                     "text-login-fallback-deadline-ticket") &&
                           strings_equal(deadline_plan.event_type,
                                         "credential-screen-deadline-plan-unavailable") &&
                           strings_equal(deadline_plan.blocked_reason,
                                         "sync-plan-unavailable"),
                       "credential deadline plan missing sync plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_sync_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &sync_plan) == 0,
                       "credential deadline plan unsafe sync source should build");
  fails += expect_true(login_window_credential_screen_deadline_plan_build(
                           &sync_plan, &deadline_plan) == 0,
                       "credential deadline plan unsafe sync plan should build blocked state");
  fails += expect_true(deadline_plan.sync_plan_available == 1 &&
                           deadline_plan.sync_plan_safe == 0 &&
                           deadline_plan.deadline_plan_safe == 0 &&
                           deadline_plan.route_selected == 0 &&
                           deadline_plan.route_blocked == 1,
                       "credential deadline plan unsafe sync plan should block deadline plan");
  fails += expect_true(deadline_plan.deadline_allowed == 0 &&
                           deadline_plan.deadline_armed == 0 &&
                           deadline_plan.deadline_text_login_fallback == 1 &&
                           deadline_plan.submit_enabled == 0 &&
                           deadline_plan.auth_attempt_allowed == 0,
                       "credential deadline plan unsafe sync plan must force text login fallback");
  fails += expect_true(strings_equal(deadline_plan.deadline_ticket,
                                     "text-login-fallback-deadline-ticket") &&
                           strings_equal(deadline_plan.event_type,
                                         "credential-screen-deadline-plan-unsafe") &&
                           strings_equal(deadline_plan.blocked_reason,
                                         "credential-deadline-plan-unsafe"),
                       "credential deadline plan unsafe sync plan should force text login");

  fails += expect_true(build_loginwindow_credential_screen_sync_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'y', 0, 0, 0,
                           1, &sync_plan) == 0,
                       "credential deadline plan armed sync source should build");
  sync_plan.sync_submitted = 1;
  sync_plan.sync_wait_allowed = 1;
  sync_plan.sync_signal_allowed = 1;
  sync_plan.sync_deadline_armed = 1;
  sync_plan.sync_completion_reported = 1;
  sync_plan.sync_cpu_gpu_sync_allowed = 1;
  sync_plan.page_flip_allowed = 1;
  fails += expect_true(login_window_credential_screen_deadline_plan_build(
                           &sync_plan, &deadline_plan) == 0,
                       "credential deadline plan armed sync should fail closed");
  fails += expect_true(deadline_plan.deadline_plan_safe == 0 &&
                           deadline_plan.deadline_allowed == 0 &&
                           deadline_plan.deadline_armed == 0 &&
                           deadline_plan.deadline_timer_armed == 0 &&
                           deadline_plan.deadline_expired == 0 &&
                           deadline_plan.deadline_completion_reported == 0 &&
                           deadline_plan.deadline_cpu_gpu_sync_allowed == 0 &&
                           deadline_plan.deadline_cpu_gpu_sync_submitted == 0 &&
                           deadline_plan.sync_submitted == 0 &&
                           deadline_plan.sync_wait_allowed == 0 &&
                           deadline_plan.sync_signal_allowed == 0 &&
                           deadline_plan.sync_deadline_armed == 0 &&
                           deadline_plan.sync_completion_reported == 0 &&
                           deadline_plan.sync_cpu_gpu_sync_allowed == 0 &&
                           deadline_plan.page_flip_allowed == 0 &&
                           deadline_plan.page_flip_submitted == 0 &&
                           deadline_plan.submit_enabled == 0 &&
                           deadline_plan.auth_attempt_allowed == 0,
                       "credential deadline plan must not copy unsafe armed state");
  return fails;
}


int build_loginwindow_credential_screen_completion_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_completion_plan *completion_plan) {
  struct login_window_credential_screen_deadline_plan deadline_plan;

  if (build_loginwindow_credential_screen_deadline_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &deadline_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_completion_plan_build(&deadline_plan,
                                                              completion_plan);
}

static int test_loginwindow_credential_screen_completion_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_completion_plan completion_plan;

  fails += expect_true(build_loginwindow_credential_screen_completion_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'c', 0, 0, 0,
                           1, &completion_plan) == 0,
                       "credential completion plan edit should build");
  fails += expect_true(completion_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_COMPLETION_PLAN_VERSION,
                       "credential completion plan should expose stable version");
  fails += expect_true(completion_plan.deadline_plan_available == 1 &&
                           completion_plan.deadline_plan_safe == 1 &&
                           completion_plan.completion_plan_safe == 1,
                       "credential completion plan should require safe deadline plan");
  fails += expect_true(completion_plan.completion_required == 1 &&
                           completion_plan.completion_allowed == 1 &&
                           completion_plan.completion_report_required == 1 &&
                           completion_plan.completion_reported == 0 &&
                           completion_plan.completion_ack_required == 1 &&
                           completion_plan.completion_acknowledged == 0 &&
                           completion_plan.completion_ticket_selected == 1 &&
                           completion_plan.completion_target_selected == 1 &&
                           completion_plan.completion_cpu_gpu_sync_allowed == 0 &&
                           completion_plan.completion_cpu_gpu_sync_submitted == 0,
                       "credential completion plan should remain declarative");
  fails += expect_true(completion_plan.deadline_armed == 0 &&
                           completion_plan.deadline_timer_armed == 0 &&
                           completion_plan.deadline_expired == 0 &&
                           completion_plan.deadline_completion_reported == 0 &&
                           completion_plan.deadline_cpu_gpu_sync_submitted == 0 &&
                           completion_plan.sync_submitted == 0 &&
                           completion_plan.sync_wait_allowed == 0 &&
                           completion_plan.sync_signal_allowed == 0 &&
                           completion_plan.sync_deadline_armed == 0 &&
                           completion_plan.sync_completion_reported == 0 &&
                           completion_plan.timeline_submitted == 0 &&
                           completion_plan.timeline_value_published == 0 &&
                           completion_plan.fence_submitted == 0 &&
                           completion_plan.barrier_submitted == 0 &&
                           completion_plan.flush_submitted == 0 &&
                           completion_plan.framebuffer_mapped == 0 &&
                           completion_plan.framebuffer_written == 0 &&
                           completion_plan.framebuffer_flushed == 0 &&
                           completion_plan.blit_pixels_copied == 0 &&
                           completion_plan.output_submitted == 0 &&
                           completion_plan.display_mode_committed == 0 &&
                           completion_plan.scanout_submitted == 0 &&
                           completion_plan.vsync_submitted == 0 &&
                           completion_plan.schedule_submitted == 0 &&
                           completion_plan.present_submitted == 0 &&
                           completion_plan.damage_submitted == 0 &&
                           completion_plan.page_flip_submitted == 0,
                       "credential completion plan must not execute GUI work");
  fails += expect_true(completion_plan.completion_credential_panel == 1 &&
                           completion_plan.completion_credential_input == 1 &&
                           completion_plan.completion_credential_focus == 1,
                       "credential completion plan should mark credential widgets");
  fails += expect_true(completion_plan.submit_callback_bound == 0 &&
                           completion_plan.auth_callback_bound == 0 &&
                           completion_plan.submit_enabled == 0 &&
                           completion_plan.auth_attempt_allowed == 0 &&
                           completion_plan.raw_secret_exposed == 0 &&
                           completion_plan.masked_text_exposed == 0 &&
                           completion_plan.length_redacted == 1,
                       "credential completion plan must stay redacted and auth-disabled");
  fails += expect_true(strings_equal(completion_plan.completion_ticket,
                                     "credential-screen-completion-ticket") &&
                           strings_equal(completion_plan.deadline_ticket,
                                         "credential-screen-deadline-ticket") &&
                           strings_equal(completion_plan.completion_policy,
                                         "declarative-completion-no-report") &&
                           strings_equal(completion_plan.state,
                                         "completion-credential-ready"),
                       "credential completion plan should report completion ticket");
  return fails;
}

static int test_loginwindow_credential_screen_completion_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_completion_plan completion_plan;

  fails += expect_true(build_loginwindow_credential_screen_completion_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &completion_plan) == 0,
                       "credential completion plan recovery should build");
  fails += expect_true(completion_plan.completion_plan_safe == 1 &&
                           completion_plan.completion_allowed == 1 &&
                           completion_plan.completion_reported == 0 &&
                           completion_plan.completion_acknowledged == 0 &&
                           completion_plan.completion_text_recovery == 1 &&
                           completion_plan.completion_text_login == 1 &&
                           completion_plan.completion_credential_focus == 0,
                       "credential completion plan recovery should mark text recovery");
  fails += expect_true(completion_plan.deadline_armed == 0 &&
                           completion_plan.deadline_timer_armed == 0 &&
                           completion_plan.deadline_completion_reported == 0 &&
                           completion_plan.completion_cpu_gpu_sync_submitted == 0 &&
                           completion_plan.sync_submitted == 0 &&
                           completion_plan.timeline_submitted == 0 &&
                           completion_plan.timeline_value_published == 0 &&
                           completion_plan.fence_submitted == 0 &&
                           completion_plan.barrier_submitted == 0 &&
                           completion_plan.flush_submitted == 0 &&
                           completion_plan.framebuffer_flushed == 0 &&
                           completion_plan.framebuffer_written == 0 &&
                           completion_plan.output_submitted == 0 &&
                           completion_plan.display_mode_committed == 0 &&
                           completion_plan.page_flip_submitted == 0 &&
                           completion_plan.submit_enabled == 0 &&
                           completion_plan.auth_attempt_allowed == 0,
                       "credential completion plan recovery must not report completion or output");
  fails += expect_true(strings_equal(completion_plan.completion_ticket,
                                     "text-recovery-completion-ticket") &&
                           strings_equal(completion_plan.compositor_target,
                                         "text-recovery-completion") &&
                           strings_equal(completion_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential completion plan recovery should report recovery ticket");

  fails += expect_true(build_loginwindow_credential_screen_completion_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'm', 1, 1, 0,
                           1, &completion_plan) == 0,
                       "credential completion plan resume should build");
  fails += expect_true(completion_plan.completion_plan_safe == 1 &&
                           completion_plan.completion_text_login_resume == 1 &&
                           completion_plan.session_reset_required == 1 &&
                           completion_plan.login_screen_rerender_required == 1 &&
                           completion_plan.completion_reported == 0 &&
                           completion_plan.completion_acknowledged == 0 &&
                           completion_plan.completion_cpu_gpu_sync_submitted == 0 &&
                           completion_plan.submit_enabled == 0 &&
                           completion_plan.auth_attempt_allowed == 0,
                       "credential completion plan resume should keep GUI auth disabled");
  fails += expect_true(strings_equal(completion_plan.completion_ticket,
                                     "text-login-resume-completion-ticket") &&
                           strings_equal(completion_plan.completion_policy,
                                         "full-completion-declarative") &&
                           strings_equal(completion_plan.state,
                                         "completion-resume-ready"),
                       "credential completion plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_completion_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_completion_plan completion_plan;

  fails += expect_true(build_loginwindow_credential_screen_completion_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &completion_plan) == 0,
                       "credential completion plan submit should build");
  fails += expect_true(completion_plan.completion_plan_safe == 1 &&
                           completion_plan.submit_requested == 1 &&
                           completion_plan.completion_text_login_fallback == 1 &&
                           completion_plan.action_allowed == 0 &&
                           completion_plan.action_blocked == 1 &&
                           completion_plan.input_focus_allowed == 0,
                       "credential completion plan submit should force text login fallback");
  fails += expect_true(completion_plan.completion_reported == 0 &&
                           completion_plan.completion_acknowledged == 0 &&
                           completion_plan.completion_cpu_gpu_sync_allowed == 0 &&
                           completion_plan.deadline_armed == 0 &&
                           completion_plan.deadline_timer_armed == 0 &&
                           completion_plan.deadline_expired == 0 &&
                           completion_plan.deadline_completion_reported == 0 &&
                           completion_plan.sync_submitted == 0 &&
                           completion_plan.sync_wait_allowed == 0 &&
                           completion_plan.sync_signal_allowed == 0 &&
                           completion_plan.sync_completion_reported == 0 &&
                           completion_plan.timeline_submitted == 0 &&
                           completion_plan.timeline_value_allocated == 0 &&
                           completion_plan.timeline_value_published == 0 &&
                           completion_plan.fence_submitted == 0 &&
                           completion_plan.barrier_submitted == 0 &&
                           completion_plan.flush_submitted == 0 &&
                           completion_plan.framebuffer_written == 0 &&
                           completion_plan.blit_pixels_copied == 0 &&
                           completion_plan.output_submitted == 0 &&
                           completion_plan.display_submitted == 0 &&
                           completion_plan.page_flip_submitted == 0 &&
                           completion_plan.submit_callback_bound == 0 &&
                           completion_plan.auth_callback_bound == 0 &&
                           completion_plan.submit_enabled == 0 &&
                           completion_plan.auth_attempt_allowed == 0,
                       "credential completion plan submit must stay declarative");
  fails += expect_true(strings_equal(completion_plan.completion_ticket,
                                     "text-login-fallback-completion-ticket") &&
                           strings_equal(completion_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(completion_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential completion plan submit should explain fallback");

  fails += expect_true(build_loginwindow_credential_screen_completion_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &completion_plan) == 0,
                       "credential completion plan unknown should build");
  fails += expect_true(completion_plan.completion_plan_safe == 1 &&
                           completion_plan.completion_text_login_fallback == 1 &&
                           completion_plan.action_allowed == 0 &&
                           completion_plan.action_blocked == 1,
                       "credential completion plan unknown should force text login fallback");
  fails += expect_true(strings_equal(completion_plan.completion_ticket,
                                     "text-login-fallback-completion-ticket") &&
                           strings_equal(completion_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential completion plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_completion_plan_fails_closed_for_unsafe_or_missing_deadline_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_deadline_plan deadline_plan;
  struct login_window_credential_screen_completion_plan completion_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_completion_plan_build(
                           NULL, &completion_plan) == 0,
                       "credential completion plan missing deadline plan should build fail-closed state");
  fails += expect_true(completion_plan.deadline_plan_available == 0 &&
                           completion_plan.deadline_plan_safe == 0 &&
                           completion_plan.completion_plan_safe == 0 &&
                           completion_plan.route_selected == 0 &&
                           completion_plan.route_blocked == 1,
                       "credential completion plan missing deadline plan should block completion plan");
  fails += expect_true(completion_plan.completion_allowed == 0 &&
                           completion_plan.completion_reported == 0 &&
                           completion_plan.completion_acknowledged == 0 &&
                           completion_plan.completion_cpu_gpu_sync_allowed == 0 &&
                           completion_plan.completion_text_login_fallback == 1 &&
                           completion_plan.submit_enabled == 0 &&
                           completion_plan.auth_attempt_allowed == 0,
                       "credential completion plan missing deadline plan must stay redacted");
  fails += expect_true(strings_equal(completion_plan.completion_ticket,
                                     "text-login-fallback-completion-ticket") &&
                           strings_equal(completion_plan.event_type,
                                         "credential-screen-completion-plan-unavailable") &&
                           strings_equal(completion_plan.blocked_reason,
                                         "deadline-plan-unavailable"),
                       "credential completion plan missing deadline plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_deadline_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &deadline_plan) == 0,
                       "credential completion plan unsafe deadline source should build");
  fails += expect_true(login_window_credential_screen_completion_plan_build(
                           &deadline_plan, &completion_plan) == 0,
                       "credential completion plan unsafe deadline plan should build blocked state");
  fails += expect_true(completion_plan.deadline_plan_available == 1 &&
                           completion_plan.deadline_plan_safe == 0 &&
                           completion_plan.completion_plan_safe == 0 &&
                           completion_plan.route_selected == 0 &&
                           completion_plan.route_blocked == 1,
                       "credential completion plan unsafe deadline plan should block completion plan");
  fails += expect_true(completion_plan.completion_allowed == 0 &&
                           completion_plan.completion_reported == 0 &&
                           completion_plan.completion_text_login_fallback == 1 &&
                           completion_plan.submit_enabled == 0 &&
                           completion_plan.auth_attempt_allowed == 0,
                       "credential completion plan unsafe deadline plan must force text login fallback");
  fails += expect_true(strings_equal(completion_plan.completion_ticket,
                                     "text-login-fallback-completion-ticket") &&
                           strings_equal(completion_plan.event_type,
                                         "credential-screen-completion-plan-unsafe") &&
                           strings_equal(completion_plan.blocked_reason,
                                         "credential-completion-plan-unsafe"),
                       "credential completion plan unsafe deadline plan should force text login");

  fails += expect_true(build_loginwindow_credential_screen_deadline_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'y', 0, 0, 0,
                           1, &deadline_plan) == 0,
                       "credential completion plan reported deadline source should build");
  deadline_plan.deadline_armed = 1;
  deadline_plan.deadline_timer_armed = 1;
  deadline_plan.deadline_expired = 1;
  deadline_plan.deadline_completion_reported = 1;
  deadline_plan.deadline_cpu_gpu_sync_allowed = 1;
  deadline_plan.sync_completion_reported = 1;
  deadline_plan.page_flip_allowed = 1;
  fails += expect_true(login_window_credential_screen_completion_plan_build(
                           &deadline_plan, &completion_plan) == 0,
                       "credential completion plan reported deadline should fail closed");
  fails += expect_true(completion_plan.completion_plan_safe == 0 &&
                           completion_plan.completion_allowed == 0 &&
                           completion_plan.completion_reported == 0 &&
                           completion_plan.completion_acknowledged == 0 &&
                           completion_plan.completion_cpu_gpu_sync_allowed == 0 &&
                           completion_plan.completion_cpu_gpu_sync_submitted == 0 &&
                           completion_plan.deadline_armed == 0 &&
                           completion_plan.deadline_timer_armed == 0 &&
                           completion_plan.deadline_expired == 0 &&
                           completion_plan.deadline_completion_reported == 0 &&
                           completion_plan.deadline_cpu_gpu_sync_allowed == 0 &&
                           completion_plan.sync_completion_reported == 0 &&
                           completion_plan.page_flip_allowed == 0 &&
                           completion_plan.page_flip_submitted == 0 &&
                           completion_plan.submit_enabled == 0 &&
                           completion_plan.auth_attempt_allowed == 0,
                       "credential completion plan must not copy unsafe reported state");
  return fails;
}

int test_login_runtime_credential_deadline_completion_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_deadline_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_deadline_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_deadline_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_deadline_plan_fails_closed_for_unsafe_or_missing_sync_plan();
  fails += test_loginwindow_credential_screen_completion_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_completion_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_completion_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_completion_plan_fails_closed_for_unsafe_or_missing_deadline_plan();
  return fails;
}
