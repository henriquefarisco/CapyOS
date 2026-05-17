/*
 * tests/auth/test_login_runtime_credential_cleanup_seal.c
 *
 * Credential screen cleanup plan + seal plan coverage for the
 * `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-16 monolith
 * refactor (PR D.24 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_cleanup_plan_build`: 4 tests
 *     covering the credential widgets cleanup + the text-route
 *     cleanup (recovery + resume) + the submit/unknown fallback
 *     cleanup + the missing-or-unsafe retire plan fail-closed
 *     default.
 *   - `login_window_credential_screen_seal_plan_build`: 4 tests
 *     covering the credential widgets seal + the text-route seal
 *     (recovery + resume) + the submit/unknown fallback seal + the
 *     missing-or-unsafe cleanup plan fail-closed default.
 *
 * Also exposes shared helpers
 * `build_loginwindow_credential_screen_cleanup_plan_for_action` and
 * `build_loginwindow_credential_screen_seal_plan_for_action`, used
 * by later companion files that chain on top of the cleanup/seal
 * stages (audit, ...).
 *
 * The companion entry `test_login_runtime_credential_cleanup_seal_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_cleanup_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_cleanup_plan *cleanup_plan) {
  struct login_window_credential_screen_retire_plan retire_plan;

  if (build_loginwindow_credential_screen_retire_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &retire_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_cleanup_plan_build(&retire_plan,
                                                           cleanup_plan);
}

static int test_loginwindow_credential_screen_cleanup_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_cleanup_plan cleanup_plan;

  fails += expect_true(build_loginwindow_credential_screen_cleanup_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'c', 0, 0, 0,
                           1, &cleanup_plan) == 0,
                       "credential cleanup plan edit should build");
  fails += expect_true(cleanup_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_CLEANUP_PLAN_VERSION,
                       "credential cleanup plan should expose stable version");
  fails += expect_true(cleanup_plan.retire_plan_available == 1 &&
                           cleanup_plan.retire_plan_safe == 1 &&
                           cleanup_plan.cleanup_plan_safe == 1,
                       "credential cleanup plan should require safe retire plan");
  fails += expect_true(cleanup_plan.cleanup_required == 1 &&
                           cleanup_plan.cleanup_allowed == 1 &&
                           cleanup_plan.cleanup_submitted == 0 &&
                           cleanup_plan.cleanup_ticket_selected == 1 &&
                           cleanup_plan.cleanup_target_selected == 1 &&
                           cleanup_plan.cleanup_resource_release_allowed == 0 &&
                           cleanup_plan.cleanup_resource_released == 0 &&
                           cleanup_plan.cleanup_cpu_gpu_sync_allowed == 0 &&
                           cleanup_plan.cleanup_cpu_gpu_sync_submitted == 0,
                       "credential cleanup plan should remain declarative");
  fails += expect_true(cleanup_plan.retire_submitted == 0 &&
                           cleanup_plan.retire_resource_released == 0 &&
                           cleanup_plan.retire_cpu_gpu_sync_submitted == 0 &&
                           cleanup_plan.ack_submitted == 0 &&
                           cleanup_plan.completion_reported == 0 &&
                           cleanup_plan.completion_acknowledged == 0 &&
                           cleanup_plan.deadline_armed == 0 &&
                           cleanup_plan.deadline_timer_armed == 0 &&
                           cleanup_plan.deadline_expired == 0 &&
                           cleanup_plan.sync_submitted == 0 &&
                           cleanup_plan.sync_wait_allowed == 0 &&
                           cleanup_plan.sync_signal_allowed == 0 &&
                           cleanup_plan.timeline_submitted == 0 &&
                           cleanup_plan.timeline_value_published == 0 &&
                           cleanup_plan.fence_submitted == 0 &&
                           cleanup_plan.barrier_submitted == 0 &&
                           cleanup_plan.flush_submitted == 0 &&
                           cleanup_plan.framebuffer_mapped == 0 &&
                           cleanup_plan.framebuffer_written == 0 &&
                           cleanup_plan.framebuffer_flushed == 0 &&
                           cleanup_plan.blit_pixels_copied == 0 &&
                           cleanup_plan.output_submitted == 0 &&
                           cleanup_plan.display_mode_committed == 0 &&
                           cleanup_plan.scanout_submitted == 0 &&
                           cleanup_plan.vsync_submitted == 0 &&
                           cleanup_plan.schedule_submitted == 0 &&
                           cleanup_plan.present_submitted == 0 &&
                           cleanup_plan.damage_submitted == 0 &&
                           cleanup_plan.page_flip_submitted == 0,
                       "credential cleanup plan must not execute GUI work");
  fails += expect_true(cleanup_plan.cleanup_credential_panel == 1 &&
                           cleanup_plan.cleanup_credential_input == 1 &&
                           cleanup_plan.cleanup_credential_focus == 1,
                       "credential cleanup plan should mark credential widgets");
  fails += expect_true(cleanup_plan.submit_callback_bound == 0 &&
                           cleanup_plan.auth_callback_bound == 0 &&
                           cleanup_plan.submit_enabled == 0 &&
                           cleanup_plan.auth_attempt_allowed == 0 &&
                           cleanup_plan.raw_secret_exposed == 0 &&
                           cleanup_plan.masked_text_exposed == 0 &&
                           cleanup_plan.length_redacted == 1,
                       "credential cleanup plan must stay redacted and auth-disabled");
  fails += expect_true(strings_equal(cleanup_plan.cleanup_ticket,
                                     "credential-screen-cleanup-ticket") &&
                           strings_equal(cleanup_plan.retire_ticket,
                                         "credential-screen-retire-ticket") &&
                           strings_equal(cleanup_plan.cleanup_policy,
                                         "declarative-cleanup-no-release") &&
                           strings_equal(cleanup_plan.state,
                                         "cleanup-credential-ready"),
                       "credential cleanup plan should report cleanup ticket");
  return fails;
}

static int test_loginwindow_credential_screen_cleanup_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_cleanup_plan cleanup_plan;

  fails += expect_true(build_loginwindow_credential_screen_cleanup_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &cleanup_plan) == 0,
                       "credential cleanup plan recovery should build");
  fails += expect_true(cleanup_plan.cleanup_plan_safe == 1 &&
                           cleanup_plan.cleanup_allowed == 1 &&
                           cleanup_plan.cleanup_submitted == 0 &&
                           cleanup_plan.cleanup_resource_released == 0 &&
                           cleanup_plan.cleanup_text_recovery == 1 &&
                           cleanup_plan.cleanup_text_login == 1 &&
                           cleanup_plan.cleanup_credential_focus == 0,
                       "credential cleanup plan recovery should mark text recovery");
  fails += expect_true(cleanup_plan.retire_submitted == 0 &&
                           cleanup_plan.ack_submitted == 0 &&
                           cleanup_plan.completion_reported == 0 &&
                           cleanup_plan.cleanup_cpu_gpu_sync_submitted == 0 &&
                           cleanup_plan.deadline_armed == 0 &&
                           cleanup_plan.deadline_timer_armed == 0 &&
                           cleanup_plan.sync_submitted == 0 &&
                           cleanup_plan.timeline_submitted == 0 &&
                           cleanup_plan.fence_submitted == 0 &&
                           cleanup_plan.barrier_submitted == 0 &&
                           cleanup_plan.flush_submitted == 0 &&
                           cleanup_plan.framebuffer_written == 0 &&
                           cleanup_plan.output_submitted == 0 &&
                           cleanup_plan.display_mode_committed == 0 &&
                           cleanup_plan.page_flip_submitted == 0 &&
                           cleanup_plan.submit_enabled == 0 &&
                           cleanup_plan.auth_attempt_allowed == 0,
                       "credential cleanup plan recovery must not cleanup resources or output");
  fails += expect_true(strings_equal(cleanup_plan.cleanup_ticket,
                                     "text-recovery-cleanup-ticket") &&
                           strings_equal(cleanup_plan.compositor_target,
                                         "text-recovery-cleanup") &&
                           strings_equal(cleanup_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential cleanup plan recovery should report recovery ticket");

  fails += expect_true(build_loginwindow_credential_screen_cleanup_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'p', 1, 1, 0,
                           1, &cleanup_plan) == 0,
                       "credential cleanup plan resume should build");
  fails += expect_true(cleanup_plan.cleanup_plan_safe == 1 &&
                           cleanup_plan.cleanup_text_login_resume == 1 &&
                           cleanup_plan.session_reset_required == 1 &&
                           cleanup_plan.login_screen_rerender_required == 1 &&
                           cleanup_plan.cleanup_submitted == 0 &&
                           cleanup_plan.cleanup_resource_released == 0 &&
                           cleanup_plan.retire_submitted == 0 &&
                           cleanup_plan.submit_enabled == 0 &&
                           cleanup_plan.auth_attempt_allowed == 0,
                       "credential cleanup plan resume should keep GUI auth disabled");
  fails += expect_true(strings_equal(cleanup_plan.cleanup_ticket,
                                     "text-login-resume-cleanup-ticket") &&
                           strings_equal(cleanup_plan.cleanup_policy,
                                         "full-cleanup-declarative") &&
                           strings_equal(cleanup_plan.state,
                                         "cleanup-resume-ready"),
                       "credential cleanup plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_cleanup_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_cleanup_plan cleanup_plan;

  fails += expect_true(build_loginwindow_credential_screen_cleanup_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &cleanup_plan) == 0,
                       "credential cleanup plan submit should build");
  fails += expect_true(cleanup_plan.cleanup_plan_safe == 1 &&
                           cleanup_plan.submit_requested == 1 &&
                           cleanup_plan.cleanup_text_login_fallback == 1 &&
                           cleanup_plan.action_allowed == 0 &&
                           cleanup_plan.action_blocked == 1 &&
                           cleanup_plan.input_focus_allowed == 0,
                       "credential cleanup plan submit should force text login fallback");
  fails += expect_true(cleanup_plan.cleanup_submitted == 0 &&
                           cleanup_plan.cleanup_resource_release_allowed == 0 &&
                           cleanup_plan.cleanup_resource_released == 0 &&
                           cleanup_plan.retire_submitted == 0 &&
                           cleanup_plan.ack_submitted == 0 &&
                           cleanup_plan.completion_reported == 0 &&
                           cleanup_plan.deadline_armed == 0 &&
                           cleanup_plan.sync_submitted == 0 &&
                           cleanup_plan.timeline_submitted == 0 &&
                           cleanup_plan.timeline_value_published == 0 &&
                           cleanup_plan.fence_submitted == 0 &&
                           cleanup_plan.barrier_submitted == 0 &&
                           cleanup_plan.flush_submitted == 0 &&
                           cleanup_plan.framebuffer_written == 0 &&
                           cleanup_plan.blit_pixels_copied == 0 &&
                           cleanup_plan.output_submitted == 0 &&
                           cleanup_plan.display_submitted == 0 &&
                           cleanup_plan.page_flip_submitted == 0 &&
                           cleanup_plan.submit_callback_bound == 0 &&
                           cleanup_plan.auth_callback_bound == 0 &&
                           cleanup_plan.submit_enabled == 0 &&
                           cleanup_plan.auth_attempt_allowed == 0,
                       "credential cleanup plan submit must stay declarative");
  fails += expect_true(strings_equal(cleanup_plan.cleanup_ticket,
                                     "text-login-fallback-cleanup-ticket") &&
                           strings_equal(cleanup_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(cleanup_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential cleanup plan submit should explain fallback");

  fails += expect_true(build_loginwindow_credential_screen_cleanup_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &cleanup_plan) == 0,
                       "credential cleanup plan unknown should build");
  fails += expect_true(cleanup_plan.cleanup_plan_safe == 1 &&
                           cleanup_plan.cleanup_text_login_fallback == 1 &&
                           cleanup_plan.action_allowed == 0 &&
                           cleanup_plan.action_blocked == 1,
                       "credential cleanup plan unknown should force text login fallback");
  fails += expect_true(strings_equal(cleanup_plan.cleanup_ticket,
                                     "text-login-fallback-cleanup-ticket") &&
                           strings_equal(cleanup_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential cleanup plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_cleanup_plan_fails_closed_for_unsafe_or_missing_retire_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_retire_plan retire_plan;
  struct login_window_credential_screen_cleanup_plan cleanup_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_cleanup_plan_build(
                           NULL, &cleanup_plan) == 0,
                       "credential cleanup plan missing retire plan should build fail-closed state");
  fails += expect_true(cleanup_plan.retire_plan_available == 0 &&
                           cleanup_plan.retire_plan_safe == 0 &&
                           cleanup_plan.cleanup_plan_safe == 0 &&
                           cleanup_plan.route_selected == 0 &&
                           cleanup_plan.route_blocked == 1,
                       "credential cleanup plan missing retire plan should block cleanup plan");
  fails += expect_true(cleanup_plan.cleanup_allowed == 0 &&
                           cleanup_plan.cleanup_submitted == 0 &&
                           cleanup_plan.cleanup_resource_released == 0 &&
                           cleanup_plan.retire_submitted == 0 &&
                           cleanup_plan.cleanup_text_login_fallback == 1 &&
                           cleanup_plan.submit_enabled == 0 &&
                           cleanup_plan.auth_attempt_allowed == 0,
                       "credential cleanup plan missing retire plan must stay redacted");
  fails += expect_true(strings_equal(cleanup_plan.cleanup_ticket,
                                     "text-login-fallback-cleanup-ticket") &&
                           strings_equal(cleanup_plan.event_type,
                                         "credential-screen-cleanup-plan-unavailable") &&
                           strings_equal(cleanup_plan.blocked_reason,
                                         "retire-plan-unavailable"),
                       "credential cleanup plan missing retire plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_retire_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &retire_plan) == 0,
                       "credential cleanup plan unsafe retire source should build");
  fails += expect_true(login_window_credential_screen_cleanup_plan_build(
                           &retire_plan, &cleanup_plan) == 0,
                       "credential cleanup plan unsafe retire plan should build blocked state");
  fails += expect_true(cleanup_plan.retire_plan_available == 1 &&
                           cleanup_plan.retire_plan_safe == 0 &&
                           cleanup_plan.cleanup_plan_safe == 0 &&
                           cleanup_plan.route_selected == 0 &&
                           cleanup_plan.route_blocked == 1,
                       "credential cleanup plan unsafe retire plan should block cleanup plan");
  fails += expect_true(cleanup_plan.cleanup_allowed == 0 &&
                           cleanup_plan.cleanup_submitted == 0 &&
                           cleanup_plan.cleanup_text_login_fallback == 1 &&
                           cleanup_plan.submit_enabled == 0 &&
                           cleanup_plan.auth_attempt_allowed == 0,
                       "credential cleanup plan unsafe retire plan must force text login fallback");
  fails += expect_true(strings_equal(cleanup_plan.cleanup_ticket,
                                     "text-login-fallback-cleanup-ticket") &&
                           strings_equal(cleanup_plan.event_type,
                                         "credential-screen-cleanup-plan-unsafe") &&
                           strings_equal(cleanup_plan.blocked_reason,
                                         "credential-cleanup-plan-unsafe"),
                       "credential cleanup plan unsafe retire plan should force text login");

  fails += expect_true(build_loginwindow_credential_screen_retire_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'y', 0, 0, 0,
                           1, &retire_plan) == 0,
                       "credential cleanup plan submitted retire source should build");
  retire_plan.retire_submitted = 1;
  retire_plan.retire_resource_release_allowed = 1;
  retire_plan.retire_resource_released = 1;
  retire_plan.ack_submitted = 1;
  retire_plan.completion_reported = 1;
  retire_plan.deadline_armed = 1;
  retire_plan.page_flip_allowed = 1;
  fails += expect_true(login_window_credential_screen_cleanup_plan_build(
                           &retire_plan, &cleanup_plan) == 0,
                       "credential cleanup plan submitted retire should fail closed");
  fails += expect_true(cleanup_plan.cleanup_plan_safe == 0 &&
                           cleanup_plan.cleanup_allowed == 0 &&
                           cleanup_plan.cleanup_submitted == 0 &&
                           cleanup_plan.cleanup_resource_release_allowed == 0 &&
                           cleanup_plan.cleanup_resource_released == 0 &&
                           cleanup_plan.cleanup_cpu_gpu_sync_allowed == 0 &&
                           cleanup_plan.cleanup_cpu_gpu_sync_submitted == 0 &&
                           cleanup_plan.retire_submitted == 0 &&
                           cleanup_plan.retire_resource_release_allowed == 0 &&
                           cleanup_plan.retire_resource_released == 0 &&
                           cleanup_plan.ack_submitted == 0 &&
                           cleanup_plan.completion_reported == 0 &&
                           cleanup_plan.deadline_armed == 0 &&
                           cleanup_plan.page_flip_allowed == 0 &&
                           cleanup_plan.page_flip_submitted == 0 &&
                           cleanup_plan.submit_enabled == 0 &&
                           cleanup_plan.auth_attempt_allowed == 0,
                       "credential cleanup plan must not copy unsafe submitted retire state");
  return fails;
}


int build_loginwindow_credential_screen_seal_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_seal_plan *seal_plan) {
  struct login_window_credential_screen_cleanup_plan cleanup_plan;

  if (build_loginwindow_credential_screen_cleanup_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &cleanup_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_seal_plan_build(&cleanup_plan,
                                                        seal_plan);
}

static int test_loginwindow_credential_screen_seal_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_seal_plan seal_plan;

  fails += expect_true(build_loginwindow_credential_screen_seal_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'z', 0, 0, 0,
                           1, &seal_plan) == 0,
                       "credential seal plan edit should build");
  fails += expect_true(seal_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_SEAL_PLAN_VERSION,
                       "credential seal plan should expose stable version");
  fails += expect_true(seal_plan.cleanup_plan_available == 1 &&
                           seal_plan.cleanup_plan_safe == 1 &&
                           seal_plan.seal_plan_safe == 1,
                       "credential seal plan should require safe cleanup plan");
  fails += expect_true(seal_plan.seal_required == 1 &&
                           seal_plan.seal_allowed == 1 &&
                           seal_plan.seal_submitted == 0 &&
                           seal_plan.seal_ticket_selected == 1 &&
                           seal_plan.seal_target_selected == 1 &&
                           seal_plan.seal_state_write_allowed == 0 &&
                           seal_plan.seal_state_written == 0 &&
                           seal_plan.seal_cpu_gpu_sync_allowed == 0 &&
                           seal_plan.seal_cpu_gpu_sync_submitted == 0,
                       "credential seal plan should remain declarative");
  fails += expect_true(seal_plan.cleanup_submitted == 0 &&
                           seal_plan.cleanup_resource_released == 0 &&
                           seal_plan.retire_submitted == 0 &&
                           seal_plan.retire_resource_released == 0 &&
                           seal_plan.ack_submitted == 0 &&
                           seal_plan.completion_reported == 0 &&
                           seal_plan.completion_acknowledged == 0 &&
                           seal_plan.deadline_armed == 0 &&
                           seal_plan.deadline_timer_armed == 0 &&
                           seal_plan.deadline_expired == 0 &&
                           seal_plan.sync_submitted == 0 &&
                           seal_plan.sync_wait_allowed == 0 &&
                           seal_plan.sync_signal_allowed == 0 &&
                           seal_plan.timeline_submitted == 0 &&
                           seal_plan.timeline_value_published == 0 &&
                           seal_plan.fence_submitted == 0 &&
                           seal_plan.barrier_submitted == 0 &&
                           seal_plan.flush_submitted == 0 &&
                           seal_plan.framebuffer_mapped == 0 &&
                           seal_plan.framebuffer_written == 0 &&
                           seal_plan.framebuffer_flushed == 0 &&
                           seal_plan.blit_pixels_copied == 0 &&
                           seal_plan.output_submitted == 0 &&
                           seal_plan.display_mode_committed == 0 &&
                           seal_plan.scanout_submitted == 0 &&
                           seal_plan.vsync_submitted == 0 &&
                           seal_plan.schedule_submitted == 0 &&
                           seal_plan.present_submitted == 0 &&
                           seal_plan.damage_submitted == 0 &&
                           seal_plan.page_flip_submitted == 0,
                       "credential seal plan must not execute GUI work");
  fails += expect_true(seal_plan.seal_credential_panel == 1 &&
                           seal_plan.seal_credential_input == 1 &&
                           seal_plan.seal_credential_focus == 1,
                       "credential seal plan should mark credential widgets");
  fails += expect_true(seal_plan.submit_callback_bound == 0 &&
                           seal_plan.auth_callback_bound == 0 &&
                           seal_plan.submit_enabled == 0 &&
                           seal_plan.auth_attempt_allowed == 0 &&
                           seal_plan.raw_secret_exposed == 0 &&
                           seal_plan.masked_text_exposed == 0 &&
                           seal_plan.length_redacted == 1,
                       "credential seal plan must stay redacted and auth-disabled");
  fails += expect_true(strings_equal(seal_plan.seal_ticket,
                                     "credential-screen-seal-ticket") &&
                           strings_equal(seal_plan.cleanup_ticket,
                                         "credential-screen-cleanup-ticket") &&
                           strings_equal(seal_plan.seal_policy,
                                         "declarative-seal-no-write") &&
                           strings_equal(seal_plan.state,
                                         "seal-credential-ready"),
                       "credential seal plan should report seal ticket");
  return fails;
}

static int test_loginwindow_credential_screen_seal_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_seal_plan seal_plan;

  fails += expect_true(build_loginwindow_credential_screen_seal_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &seal_plan) == 0,
                       "credential seal plan recovery should build");
  fails += expect_true(seal_plan.seal_plan_safe == 1 &&
                           seal_plan.seal_allowed == 1 &&
                           seal_plan.seal_submitted == 0 &&
                           seal_plan.seal_state_written == 0 &&
                           seal_plan.seal_text_recovery == 1 &&
                           seal_plan.seal_text_login == 1 &&
                           seal_plan.seal_credential_focus == 0,
                       "credential seal plan recovery should mark text recovery");
  fails += expect_true(seal_plan.cleanup_submitted == 0 &&
                           seal_plan.retire_submitted == 0 &&
                           seal_plan.ack_submitted == 0 &&
                           seal_plan.completion_reported == 0 &&
                           seal_plan.seal_cpu_gpu_sync_submitted == 0 &&
                           seal_plan.deadline_armed == 0 &&
                           seal_plan.deadline_timer_armed == 0 &&
                           seal_plan.sync_submitted == 0 &&
                           seal_plan.timeline_submitted == 0 &&
                           seal_plan.fence_submitted == 0 &&
                           seal_plan.barrier_submitted == 0 &&
                           seal_plan.flush_submitted == 0 &&
                           seal_plan.framebuffer_written == 0 &&
                           seal_plan.output_submitted == 0 &&
                           seal_plan.display_mode_committed == 0 &&
                           seal_plan.page_flip_submitted == 0 &&
                           seal_plan.submit_enabled == 0 &&
                           seal_plan.auth_attempt_allowed == 0,
                       "credential seal plan recovery must not write state or output");
  fails += expect_true(strings_equal(seal_plan.seal_ticket,
                                     "text-recovery-seal-ticket") &&
                           strings_equal(seal_plan.compositor_target,
                                         "text-recovery-seal") &&
                           strings_equal(seal_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential seal plan recovery should report recovery ticket");

  fails += expect_true(build_loginwindow_credential_screen_seal_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'q', 1, 1, 0,
                           1, &seal_plan) == 0,
                       "credential seal plan resume should build");
  fails += expect_true(seal_plan.seal_plan_safe == 1 &&
                           seal_plan.seal_text_login_resume == 1 &&
                           seal_plan.session_reset_required == 1 &&
                           seal_plan.login_screen_rerender_required == 1 &&
                           seal_plan.seal_submitted == 0 &&
                           seal_plan.seal_state_written == 0 &&
                           seal_plan.cleanup_submitted == 0 &&
                           seal_plan.submit_enabled == 0 &&
                           seal_plan.auth_attempt_allowed == 0,
                       "credential seal plan resume should keep GUI auth disabled");
  fails += expect_true(strings_equal(seal_plan.seal_ticket,
                                     "text-login-resume-seal-ticket") &&
                           strings_equal(seal_plan.seal_policy,
                                         "full-seal-declarative") &&
                           strings_equal(seal_plan.state,
                                         "seal-resume-ready"),
                       "credential seal plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_seal_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_seal_plan seal_plan;

  fails += expect_true(build_loginwindow_credential_screen_seal_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &seal_plan) == 0,
                       "credential seal plan submit should build");
  fails += expect_true(seal_plan.seal_plan_safe == 1 &&
                           seal_plan.submit_requested == 1 &&
                           seal_plan.seal_text_login_fallback == 1 &&
                           seal_plan.action_allowed == 0 &&
                           seal_plan.action_blocked == 1 &&
                           seal_plan.input_focus_allowed == 0,
                       "credential seal plan submit should force text login fallback");
  fails += expect_true(seal_plan.seal_submitted == 0 &&
                           seal_plan.seal_state_write_allowed == 0 &&
                           seal_plan.seal_state_written == 0 &&
                           seal_plan.cleanup_submitted == 0 &&
                           seal_plan.cleanup_resource_released == 0 &&
                           seal_plan.retire_submitted == 0 &&
                           seal_plan.ack_submitted == 0 &&
                           seal_plan.completion_reported == 0 &&
                           seal_plan.deadline_armed == 0 &&
                           seal_plan.sync_submitted == 0 &&
                           seal_plan.timeline_submitted == 0 &&
                           seal_plan.timeline_value_published == 0 &&
                           seal_plan.fence_submitted == 0 &&
                           seal_plan.barrier_submitted == 0 &&
                           seal_plan.flush_submitted == 0 &&
                           seal_plan.framebuffer_written == 0 &&
                           seal_plan.blit_pixels_copied == 0 &&
                           seal_plan.output_submitted == 0 &&
                           seal_plan.display_submitted == 0 &&
                           seal_plan.page_flip_submitted == 0 &&
                           seal_plan.submit_callback_bound == 0 &&
                           seal_plan.auth_callback_bound == 0 &&
                           seal_plan.submit_enabled == 0 &&
                           seal_plan.auth_attempt_allowed == 0,
                       "credential seal plan submit must stay declarative");
  fails += expect_true(strings_equal(seal_plan.seal_ticket,
                                     "text-login-fallback-seal-ticket") &&
                           strings_equal(seal_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(seal_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential seal plan submit should explain fallback");

  fails += expect_true(build_loginwindow_credential_screen_seal_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &seal_plan) == 0,
                       "credential seal plan unknown should build");
  fails += expect_true(seal_plan.seal_plan_safe == 1 &&
                           seal_plan.seal_text_login_fallback == 1 &&
                           seal_plan.action_allowed == 0 &&
                           seal_plan.action_blocked == 1,
                       "credential seal plan unknown should force text login fallback");
  fails += expect_true(strings_equal(seal_plan.seal_ticket,
                                     "text-login-fallback-seal-ticket") &&
                           strings_equal(seal_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential seal plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_seal_plan_fails_closed_for_unsafe_or_missing_cleanup_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_cleanup_plan cleanup_plan;
  struct login_window_credential_screen_seal_plan seal_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_seal_plan_build(
                           NULL, &seal_plan) == 0,
                       "credential seal plan missing cleanup plan should build fail-closed state");
  fails += expect_true(seal_plan.cleanup_plan_available == 0 &&
                           seal_plan.cleanup_plan_safe == 0 &&
                           seal_plan.seal_plan_safe == 0 &&
                           seal_plan.route_selected == 0 &&
                           seal_plan.route_blocked == 1,
                       "credential seal plan missing cleanup plan should block seal plan");
  fails += expect_true(seal_plan.seal_allowed == 0 &&
                           seal_plan.seal_submitted == 0 &&
                           seal_plan.seal_state_written == 0 &&
                           seal_plan.cleanup_submitted == 0 &&
                           seal_plan.seal_text_login_fallback == 1 &&
                           seal_plan.submit_enabled == 0 &&
                           seal_plan.auth_attempt_allowed == 0,
                       "credential seal plan missing cleanup plan must stay redacted");
  fails += expect_true(strings_equal(seal_plan.seal_ticket,
                                     "text-login-fallback-seal-ticket") &&
                           strings_equal(seal_plan.event_type,
                                         "credential-screen-seal-plan-unavailable") &&
                           strings_equal(seal_plan.blocked_reason,
                                         "cleanup-plan-unavailable"),
                       "credential seal plan missing cleanup plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_cleanup_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &cleanup_plan) == 0,
                       "credential seal plan unsafe cleanup source should build");
  fails += expect_true(login_window_credential_screen_seal_plan_build(
                           &cleanup_plan, &seal_plan) == 0,
                       "credential seal plan unsafe cleanup plan should build blocked state");
  fails += expect_true(seal_plan.cleanup_plan_available == 1 &&
                           seal_plan.cleanup_plan_safe == 0 &&
                           seal_plan.seal_plan_safe == 0 &&
                           seal_plan.route_selected == 0 &&
                           seal_plan.route_blocked == 1,
                       "credential seal plan unsafe cleanup plan should block seal plan");
  fails += expect_true(seal_plan.seal_allowed == 0 &&
                           seal_plan.seal_submitted == 0 &&
                           seal_plan.seal_text_login_fallback == 1 &&
                           seal_plan.submit_enabled == 0 &&
                           seal_plan.auth_attempt_allowed == 0,
                       "credential seal plan unsafe cleanup plan must force text login fallback");
  fails += expect_true(strings_equal(seal_plan.seal_ticket,
                                     "text-login-fallback-seal-ticket") &&
                           strings_equal(seal_plan.event_type,
                                         "credential-screen-seal-plan-unsafe") &&
                           strings_equal(seal_plan.blocked_reason,
                                         "credential-seal-plan-unsafe"),
                       "credential seal plan unsafe cleanup plan should force text login");

  fails += expect_true(build_loginwindow_credential_screen_cleanup_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'y', 0, 0, 0,
                           1, &cleanup_plan) == 0,
                       "credential seal plan submitted cleanup source should build");
  cleanup_plan.cleanup_submitted = 1;
  cleanup_plan.cleanup_resource_release_allowed = 1;
  cleanup_plan.cleanup_resource_released = 1;
  cleanup_plan.retire_submitted = 1;
  cleanup_plan.ack_submitted = 1;
  cleanup_plan.completion_reported = 1;
  cleanup_plan.deadline_armed = 1;
  cleanup_plan.page_flip_allowed = 1;
  fails += expect_true(login_window_credential_screen_seal_plan_build(
                           &cleanup_plan, &seal_plan) == 0,
                       "credential seal plan submitted cleanup should fail closed");
  fails += expect_true(seal_plan.seal_plan_safe == 0 &&
                           seal_plan.seal_allowed == 0 &&
                           seal_plan.seal_submitted == 0 &&
                           seal_plan.seal_state_write_allowed == 0 &&
                           seal_plan.seal_state_written == 0 &&
                           seal_plan.seal_cpu_gpu_sync_allowed == 0 &&
                           seal_plan.seal_cpu_gpu_sync_submitted == 0 &&
                           seal_plan.cleanup_submitted == 0 &&
                           seal_plan.cleanup_resource_release_allowed == 0 &&
                           seal_plan.cleanup_resource_released == 0 &&
                           seal_plan.retire_submitted == 0 &&
                           seal_plan.ack_submitted == 0 &&
                           seal_plan.completion_reported == 0 &&
                           seal_plan.deadline_armed == 0 &&
                           seal_plan.page_flip_allowed == 0 &&
                           seal_plan.page_flip_submitted == 0 &&
                           seal_plan.submit_enabled == 0 &&
                           seal_plan.auth_attempt_allowed == 0,
                       "credential seal plan must not copy unsafe submitted cleanup state");
  return fails;
}

int test_login_runtime_credential_cleanup_seal_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_cleanup_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_cleanup_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_cleanup_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_cleanup_plan_fails_closed_for_unsafe_or_missing_retire_plan();
  fails += test_loginwindow_credential_screen_seal_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_seal_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_seal_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_seal_plan_fails_closed_for_unsafe_or_missing_cleanup_plan();
  return fails;
}
