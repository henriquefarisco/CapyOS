/*
 * tests/auth/test_login_runtime_credential_timeline_sync.c
 *
 * Credential screen timeline plan + sync plan coverage for the
 * `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-16 monolith
 * refactor (PR D.21 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_timeline_plan_build`: 4 tests
 *     covering the credential widgets timeline + the text-route
 *     timeline (recovery + resume) + the submit/unknown fallback
 *     timeline + the missing-or-unsafe fence plan fail-closed
 *     default.
 *   - `login_window_credential_screen_sync_plan_build`: 4 tests
 *     covering the credential widgets sync + the text-route sync
 *     (recovery + resume) + the submit/unknown fallback sync + the
 *     missing-or-unsafe timeline plan fail-closed default.
 *
 * Also exposes shared helpers
 * `build_loginwindow_credential_screen_timeline_plan_for_action` and
 * `build_loginwindow_credential_screen_sync_plan_for_action`, used
 * by later companion files that chain on top of the timeline/sync
 * stages (deadline, ...).
 *
 * The companion entry `test_login_runtime_credential_timeline_sync_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_timeline_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_timeline_plan *timeline_plan) {
  struct login_window_credential_screen_fence_plan fence_plan;

  if (build_loginwindow_credential_screen_fence_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &fence_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_timeline_plan_build(&fence_plan,
                                                            timeline_plan);
}

static int test_loginwindow_credential_screen_timeline_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_timeline_plan timeline_plan;

  fails += expect_true(build_loginwindow_credential_screen_timeline_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 't', 0, 0, 0,
                           1, &timeline_plan) == 0,
                       "credential timeline plan edit should build");
  fails += expect_true(timeline_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_TIMELINE_PLAN_VERSION,
                       "credential timeline plan should expose stable version");
  fails += expect_true(timeline_plan.fence_plan_available == 1 &&
                           timeline_plan.fence_plan_safe == 1 &&
                           timeline_plan.timeline_plan_safe == 1,
                       "credential timeline plan should require safe fence plan");
  fails += expect_true(timeline_plan.timeline_required == 1 &&
                           timeline_plan.timeline_allowed == 1 &&
                           timeline_plan.timeline_submitted == 0 &&
                           timeline_plan.timeline_ticket_selected == 1 &&
                           timeline_plan.timeline_target_selected == 1 &&
                           timeline_plan.timeline_wait_required == 1 &&
                           timeline_plan.timeline_wait_allowed == 0 &&
                           timeline_plan.timeline_wait_submitted == 0 &&
                           timeline_plan.timeline_signal_allowed == 0 &&
                           timeline_plan.timeline_signal_submitted == 0 &&
                           timeline_plan.timeline_semaphore_allowed == 0 &&
                           timeline_plan.timeline_semaphore_submitted == 0 &&
                           timeline_plan.timeline_value_required == 1 &&
                           timeline_plan.timeline_value_allocated == 0 &&
                           timeline_plan.timeline_value_published == 0 &&
                           timeline_plan.timeline_cpu_gpu_sync_allowed == 0 &&
                           timeline_plan.timeline_cpu_gpu_sync_submitted == 0,
                       "credential timeline plan should remain declarative");
  fails += expect_true(timeline_plan.fence_submitted == 0 &&
                           timeline_plan.fence_wait_allowed == 0 &&
                           timeline_plan.fence_signal_allowed == 0 &&
                           timeline_plan.fence_fd_exported == 0 &&
                           timeline_plan.fence_cpu_gpu_sync_submitted == 0 &&
                           timeline_plan.barrier_submitted == 0 &&
                           timeline_plan.barrier_memory_visibility_established == 0 &&
                           timeline_plan.barrier_cache_visibility_established == 0 &&
                           timeline_plan.flush_submitted == 0 &&
                           timeline_plan.flush_cache_cleaned == 0 &&
                           timeline_plan.framebuffer_mapped == 0 &&
                           timeline_plan.framebuffer_written == 0 &&
                           timeline_plan.framebuffer_flushed == 0 &&
                           timeline_plan.blit_pixels_copied == 0 &&
                           timeline_plan.output_submitted == 0 &&
                           timeline_plan.display_mode_committed == 0 &&
                           timeline_plan.scanout_submitted == 0 &&
                           timeline_plan.vsync_submitted == 0 &&
                           timeline_plan.schedule_submitted == 0 &&
                           timeline_plan.present_submitted == 0 &&
                           timeline_plan.damage_submitted == 0 &&
                           timeline_plan.page_flip_submitted == 0,
                       "credential timeline plan must not execute GUI work");
  fails += expect_true(timeline_plan.timeline_credential_panel == 1 &&
                           timeline_plan.timeline_credential_input == 1 &&
                           timeline_plan.timeline_credential_focus == 1,
                       "credential timeline plan should mark credential widgets");
  fails += expect_true(timeline_plan.submit_callback_bound == 0 &&
                           timeline_plan.auth_callback_bound == 0 &&
                           timeline_plan.submit_enabled == 0 &&
                           timeline_plan.auth_attempt_allowed == 0 &&
                           timeline_plan.raw_secret_exposed == 0 &&
                           timeline_plan.masked_text_exposed == 0 &&
                           timeline_plan.length_redacted == 1,
                       "credential timeline plan must stay redacted and auth-disabled");
  fails += expect_true(strings_equal(timeline_plan.timeline_ticket,
                                     "credential-screen-timeline-ticket") &&
                           strings_equal(timeline_plan.fence_ticket,
                                         "credential-screen-fence-ticket") &&
                           strings_equal(timeline_plan.timeline_policy,
                                         "declarative-timeline-no-submit") &&
                           strings_equal(timeline_plan.state,
                                         "timeline-credential-ready"),
                       "credential timeline plan should report timeline ticket");
  return fails;
}

static int test_loginwindow_credential_screen_timeline_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_timeline_plan timeline_plan;

  fails += expect_true(build_loginwindow_credential_screen_timeline_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &timeline_plan) == 0,
                       "credential timeline plan recovery should build");
  fails += expect_true(timeline_plan.timeline_plan_safe == 1 &&
                           timeline_plan.timeline_allowed == 1 &&
                           timeline_plan.timeline_submitted == 0 &&
                           timeline_plan.timeline_text_recovery == 1 &&
                           timeline_plan.timeline_text_login == 1 &&
                           timeline_plan.timeline_credential_focus == 0,
                       "credential timeline plan recovery should mark text recovery");
  fails += expect_true(timeline_plan.timeline_wait_allowed == 0 &&
                           timeline_plan.timeline_wait_submitted == 0 &&
                           timeline_plan.timeline_signal_allowed == 0 &&
                           timeline_plan.timeline_signal_submitted == 0 &&
                           timeline_plan.timeline_semaphore_submitted == 0 &&
                           timeline_plan.timeline_value_allocated == 0 &&
                           timeline_plan.timeline_value_published == 0 &&
                           timeline_plan.timeline_cpu_gpu_sync_submitted == 0 &&
                           timeline_plan.fence_submitted == 0 &&
                           timeline_plan.barrier_submitted == 0 &&
                           timeline_plan.flush_submitted == 0 &&
                           timeline_plan.framebuffer_flushed == 0 &&
                           timeline_plan.framebuffer_written == 0 &&
                           timeline_plan.output_submitted == 0 &&
                           timeline_plan.display_mode_committed == 0 &&
                           timeline_plan.page_flip_submitted == 0 &&
                           timeline_plan.submit_enabled == 0 &&
                           timeline_plan.auth_attempt_allowed == 0,
                       "credential timeline plan recovery must not submit timeline or output");
  fails += expect_true(strings_equal(timeline_plan.timeline_ticket,
                                     "text-recovery-timeline-ticket") &&
                           strings_equal(timeline_plan.compositor_target,
                                         "text-recovery-timeline") &&
                           strings_equal(timeline_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential timeline plan recovery should report recovery ticket");

  fails += expect_true(build_loginwindow_credential_screen_timeline_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &timeline_plan) == 0,
                       "credential timeline plan resume should build");
  fails += expect_true(timeline_plan.timeline_plan_safe == 1 &&
                           timeline_plan.timeline_text_login_resume == 1 &&
                           timeline_plan.session_reset_required == 1 &&
                           timeline_plan.login_screen_rerender_required == 1 &&
                           timeline_plan.timeline_submitted == 0 &&
                           timeline_plan.timeline_wait_submitted == 0 &&
                           timeline_plan.timeline_signal_submitted == 0 &&
                           timeline_plan.timeline_cpu_gpu_sync_submitted == 0 &&
                           timeline_plan.submit_enabled == 0 &&
                           timeline_plan.auth_attempt_allowed == 0,
                       "credential timeline plan resume should keep GUI auth disabled");
  fails += expect_true(strings_equal(timeline_plan.timeline_ticket,
                                     "text-login-resume-timeline-ticket") &&
                           strings_equal(timeline_plan.timeline_policy,
                                         "full-timeline-declarative") &&
                           strings_equal(timeline_plan.state,
                                         "timeline-resume-ready"),
                       "credential timeline plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_timeline_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_timeline_plan timeline_plan;

  fails += expect_true(build_loginwindow_credential_screen_timeline_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &timeline_plan) == 0,
                       "credential timeline plan submit should build");
  fails += expect_true(timeline_plan.timeline_plan_safe == 1 &&
                           timeline_plan.submit_requested == 1 &&
                           timeline_plan.timeline_text_login_fallback == 1 &&
                           timeline_plan.action_allowed == 0 &&
                           timeline_plan.action_blocked == 1 &&
                           timeline_plan.input_focus_allowed == 0,
                       "credential timeline plan submit should force text login fallback");
  fails += expect_true(timeline_plan.timeline_submitted == 0 &&
                           timeline_plan.timeline_wait_allowed == 0 &&
                           timeline_plan.timeline_signal_allowed == 0 &&
                           timeline_plan.timeline_semaphore_allowed == 0 &&
                           timeline_plan.timeline_value_allocated == 0 &&
                           timeline_plan.timeline_value_published == 0 &&
                           timeline_plan.timeline_cpu_gpu_sync_allowed == 0 &&
                           timeline_plan.fence_submitted == 0 &&
                           timeline_plan.fence_wait_allowed == 0 &&
                           timeline_plan.fence_signal_allowed == 0 &&
                           timeline_plan.barrier_submitted == 0 &&
                           timeline_plan.flush_submitted == 0 &&
                           timeline_plan.framebuffer_written == 0 &&
                           timeline_plan.blit_pixels_copied == 0 &&
                           timeline_plan.output_submitted == 0 &&
                           timeline_plan.display_submitted == 0 &&
                           timeline_plan.page_flip_submitted == 0 &&
                           timeline_plan.submit_callback_bound == 0 &&
                           timeline_plan.auth_callback_bound == 0 &&
                           timeline_plan.submit_enabled == 0 &&
                           timeline_plan.auth_attempt_allowed == 0,
                       "credential timeline plan submit must stay declarative");
  fails += expect_true(strings_equal(timeline_plan.timeline_ticket,
                                     "text-login-fallback-timeline-ticket") &&
                           strings_equal(timeline_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(timeline_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential timeline plan submit should explain fallback");

  fails += expect_true(build_loginwindow_credential_screen_timeline_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &timeline_plan) == 0,
                       "credential timeline plan unknown should build");
  fails += expect_true(timeline_plan.timeline_plan_safe == 1 &&
                           timeline_plan.timeline_text_login_fallback == 1 &&
                           timeline_plan.action_allowed == 0 &&
                           timeline_plan.action_blocked == 1,
                       "credential timeline plan unknown should force text login fallback");
  fails += expect_true(strings_equal(timeline_plan.timeline_ticket,
                                     "text-login-fallback-timeline-ticket") &&
                           strings_equal(timeline_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential timeline plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_timeline_plan_fails_closed_for_unsafe_or_missing_fence_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_fence_plan fence_plan;
  struct login_window_credential_screen_timeline_plan timeline_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_timeline_plan_build(
                           NULL, &timeline_plan) == 0,
                       "credential timeline plan missing fence plan should build fail-closed state");
  fails += expect_true(timeline_plan.fence_plan_available == 0 &&
                           timeline_plan.fence_plan_safe == 0 &&
                           timeline_plan.timeline_plan_safe == 0 &&
                           timeline_plan.route_selected == 0 &&
                           timeline_plan.route_blocked == 1,
                       "credential timeline plan missing fence plan should block timeline plan");
  fails += expect_true(timeline_plan.timeline_allowed == 0 &&
                           timeline_plan.timeline_submitted == 0 &&
                           timeline_plan.timeline_wait_allowed == 0 &&
                           timeline_plan.timeline_signal_allowed == 0 &&
                           timeline_plan.timeline_semaphore_submitted == 0 &&
                           timeline_plan.timeline_value_published == 0 &&
                           timeline_plan.timeline_text_login_fallback == 1 &&
                           timeline_plan.submit_enabled == 0 &&
                           timeline_plan.auth_attempt_allowed == 0,
                       "credential timeline plan missing fence plan must stay redacted");
  fails += expect_true(strings_equal(timeline_plan.timeline_ticket,
                                     "text-login-fallback-timeline-ticket") &&
                           strings_equal(timeline_plan.event_type,
                                         "credential-screen-timeline-plan-unavailable") &&
                           strings_equal(timeline_plan.blocked_reason,
                                         "fence-plan-unavailable"),
                       "credential timeline plan missing fence plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_fence_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &fence_plan) == 0,
                       "credential timeline plan unsafe fence source should build");
  fails += expect_true(login_window_credential_screen_timeline_plan_build(
                           &fence_plan, &timeline_plan) == 0,
                       "credential timeline plan unsafe fence plan should build blocked state");
  fails += expect_true(timeline_plan.fence_plan_available == 1 &&
                           timeline_plan.fence_plan_safe == 0 &&
                           timeline_plan.timeline_plan_safe == 0 &&
                           timeline_plan.route_selected == 0 &&
                           timeline_plan.route_blocked == 1,
                       "credential timeline plan unsafe fence plan should block timeline plan");
  fails += expect_true(timeline_plan.timeline_allowed == 0 &&
                           timeline_plan.timeline_submitted == 0 &&
                           timeline_plan.timeline_text_login_fallback == 1 &&
                           timeline_plan.submit_enabled == 0 &&
                           timeline_plan.auth_attempt_allowed == 0,
                       "credential timeline plan unsafe fence plan must force text login fallback");
  fails += expect_true(strings_equal(timeline_plan.timeline_ticket,
                                     "text-login-fallback-timeline-ticket") &&
                           strings_equal(timeline_plan.event_type,
                                         "credential-screen-timeline-plan-unsafe") &&
                           strings_equal(timeline_plan.blocked_reason,
                                         "credential-timeline-plan-unsafe"),
                       "credential timeline plan unsafe fence plan should force text login");

  fails += expect_true(build_loginwindow_credential_screen_fence_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'y', 0, 0, 0,
                           1, &fence_plan) == 0,
                       "credential timeline plan submitted fence source should build");
  fence_plan.fence_submitted = 1;
  fence_plan.fence_wait_allowed = 1;
  fence_plan.fence_signal_allowed = 1;
  fence_plan.fence_fd_export_allowed = 1;
  fence_plan.fence_cpu_gpu_sync_allowed = 1;
  fence_plan.page_flip_allowed = 1;
  fails += expect_true(login_window_credential_screen_timeline_plan_build(
                           &fence_plan, &timeline_plan) == 0,
                       "credential timeline plan submitted fence should fail closed");
  fails += expect_true(timeline_plan.timeline_plan_safe == 0 &&
                           timeline_plan.timeline_allowed == 0 &&
                           timeline_plan.timeline_submitted == 0 &&
                           timeline_plan.timeline_wait_allowed == 0 &&
                           timeline_plan.timeline_wait_submitted == 0 &&
                           timeline_plan.timeline_signal_allowed == 0 &&
                           timeline_plan.timeline_signal_submitted == 0 &&
                           timeline_plan.timeline_semaphore_allowed == 0 &&
                           timeline_plan.timeline_semaphore_submitted == 0 &&
                           timeline_plan.timeline_value_allocated == 0 &&
                           timeline_plan.timeline_value_published == 0 &&
                           timeline_plan.timeline_cpu_gpu_sync_allowed == 0 &&
                           timeline_plan.timeline_cpu_gpu_sync_submitted == 0 &&
                           timeline_plan.fence_submitted == 0 &&
                           timeline_plan.fence_wait_allowed == 0 &&
                           timeline_plan.fence_signal_allowed == 0 &&
                           timeline_plan.fence_fd_export_allowed == 0 &&
                           timeline_plan.fence_cpu_gpu_sync_allowed == 0 &&
                           timeline_plan.page_flip_allowed == 0 &&
                           timeline_plan.page_flip_submitted == 0 &&
                           timeline_plan.submit_enabled == 0 &&
                           timeline_plan.auth_attempt_allowed == 0,
                       "credential timeline plan must not copy unsafe submitted state");
  return fails;
}


int build_loginwindow_credential_screen_sync_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_sync_plan *sync_plan) {
  struct login_window_credential_screen_timeline_plan timeline_plan;

  if (build_loginwindow_credential_screen_timeline_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &timeline_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_sync_plan_build(&timeline_plan,
                                                        sync_plan);
}

static int test_loginwindow_credential_screen_sync_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_sync_plan sync_plan;

  fails += expect_true(build_loginwindow_credential_screen_sync_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 't', 0, 0, 0,
                           1, &sync_plan) == 0,
                       "credential sync plan edit should build");
  fails += expect_true(sync_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_SYNC_PLAN_VERSION,
                       "credential sync plan should expose stable version");
  fails += expect_true(sync_plan.timeline_plan_available == 1 &&
                           sync_plan.timeline_plan_safe == 1 &&
                           sync_plan.sync_plan_safe == 1,
                       "credential sync plan should require safe timeline plan");
  fails += expect_true(sync_plan.sync_required == 1 &&
                           sync_plan.sync_allowed == 1 &&
                           sync_plan.sync_submitted == 0 &&
                           sync_plan.sync_ticket_selected == 1 &&
                           sync_plan.sync_target_selected == 1 &&
                           sync_plan.sync_wait_required == 1 &&
                           sync_plan.sync_wait_allowed == 0 &&
                           sync_plan.sync_wait_submitted == 0 &&
                           sync_plan.sync_signal_allowed == 0 &&
                           sync_plan.sync_signal_submitted == 0 &&
                           sync_plan.sync_deadline_required == 1 &&
                           sync_plan.sync_deadline_armed == 0 &&
                           sync_plan.sync_completion_required == 1 &&
                           sync_plan.sync_completion_reported == 0 &&
                           sync_plan.sync_cpu_gpu_sync_allowed == 0 &&
                           sync_plan.sync_cpu_gpu_sync_submitted == 0,
                       "credential sync plan should remain declarative");
  fails += expect_true(sync_plan.timeline_submitted == 0 &&
                           sync_plan.timeline_wait_allowed == 0 &&
                           sync_plan.timeline_wait_submitted == 0 &&
                           sync_plan.timeline_signal_allowed == 0 &&
                           sync_plan.timeline_signal_submitted == 0 &&
                           sync_plan.timeline_semaphore_allowed == 0 &&
                           sync_plan.timeline_semaphore_submitted == 0 &&
                           sync_plan.timeline_value_allocated == 0 &&
                           sync_plan.timeline_value_published == 0 &&
                           sync_plan.timeline_cpu_gpu_sync_submitted == 0 &&
                           sync_plan.fence_submitted == 0 &&
                           sync_plan.barrier_submitted == 0 &&
                           sync_plan.flush_submitted == 0 &&
                           sync_plan.framebuffer_mapped == 0 &&
                           sync_plan.framebuffer_written == 0 &&
                           sync_plan.framebuffer_flushed == 0 &&
                           sync_plan.blit_pixels_copied == 0 &&
                           sync_plan.output_submitted == 0 &&
                           sync_plan.display_mode_committed == 0 &&
                           sync_plan.scanout_submitted == 0 &&
                           sync_plan.vsync_submitted == 0 &&
                           sync_plan.schedule_submitted == 0 &&
                           sync_plan.present_submitted == 0 &&
                           sync_plan.damage_submitted == 0 &&
                           sync_plan.page_flip_submitted == 0,
                       "credential sync plan must not execute GUI work");
  fails += expect_true(sync_plan.sync_credential_panel == 1 &&
                           sync_plan.sync_credential_input == 1 &&
                           sync_plan.sync_credential_focus == 1,
                       "credential sync plan should mark credential widgets");
  fails += expect_true(sync_plan.submit_callback_bound == 0 &&
                           sync_plan.auth_callback_bound == 0 &&
                           sync_plan.submit_enabled == 0 &&
                           sync_plan.auth_attempt_allowed == 0 &&
                           sync_plan.raw_secret_exposed == 0 &&
                           sync_plan.masked_text_exposed == 0 &&
                           sync_plan.length_redacted == 1,
                       "credential sync plan must stay redacted and auth-disabled");
  fails += expect_true(strings_equal(sync_plan.sync_ticket,
                                     "credential-screen-sync-ticket") &&
                           strings_equal(sync_plan.timeline_ticket,
                                         "credential-screen-timeline-ticket") &&
                           strings_equal(sync_plan.sync_policy,
                                         "declarative-sync-no-submit") &&
                           strings_equal(sync_plan.state,
                                         "sync-credential-ready"),
                       "credential sync plan should report sync ticket");
  return fails;
}

static int test_loginwindow_credential_screen_sync_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_sync_plan sync_plan;

  fails += expect_true(build_loginwindow_credential_screen_sync_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &sync_plan) == 0,
                       "credential sync plan recovery should build");
  fails += expect_true(sync_plan.sync_plan_safe == 1 &&
                           sync_plan.sync_allowed == 1 &&
                           sync_plan.sync_submitted == 0 &&
                           sync_plan.sync_text_recovery == 1 &&
                           sync_plan.sync_text_login == 1 &&
                           sync_plan.sync_credential_focus == 0,
                       "credential sync plan recovery should mark text recovery");
  fails += expect_true(sync_plan.sync_wait_allowed == 0 &&
                           sync_plan.sync_wait_submitted == 0 &&
                           sync_plan.sync_signal_allowed == 0 &&
                           sync_plan.sync_signal_submitted == 0 &&
                           sync_plan.sync_deadline_armed == 0 &&
                           sync_plan.sync_completion_reported == 0 &&
                           sync_plan.sync_cpu_gpu_sync_submitted == 0 &&
                           sync_plan.timeline_submitted == 0 &&
                           sync_plan.timeline_semaphore_submitted == 0 &&
                           sync_plan.timeline_value_published == 0 &&
                           sync_plan.fence_submitted == 0 &&
                           sync_plan.barrier_submitted == 0 &&
                           sync_plan.flush_submitted == 0 &&
                           sync_plan.framebuffer_flushed == 0 &&
                           sync_plan.framebuffer_written == 0 &&
                           sync_plan.output_submitted == 0 &&
                           sync_plan.display_mode_committed == 0 &&
                           sync_plan.page_flip_submitted == 0 &&
                           sync_plan.submit_enabled == 0 &&
                           sync_plan.auth_attempt_allowed == 0,
                       "credential sync plan recovery must not submit sync or output");
  fails += expect_true(strings_equal(sync_plan.sync_ticket,
                                     "text-recovery-sync-ticket") &&
                           strings_equal(sync_plan.compositor_target,
                                         "text-recovery-sync") &&
                           strings_equal(sync_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential sync plan recovery should report recovery ticket");

  fails += expect_true(build_loginwindow_credential_screen_sync_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &sync_plan) == 0,
                       "credential sync plan resume should build");
  fails += expect_true(sync_plan.sync_plan_safe == 1 &&
                           sync_plan.sync_text_login_resume == 1 &&
                           sync_plan.session_reset_required == 1 &&
                           sync_plan.login_screen_rerender_required == 1 &&
                           sync_plan.sync_submitted == 0 &&
                           sync_plan.sync_wait_submitted == 0 &&
                           sync_plan.sync_signal_submitted == 0 &&
                           sync_plan.sync_deadline_armed == 0 &&
                           sync_plan.sync_completion_reported == 0 &&
                           sync_plan.sync_cpu_gpu_sync_submitted == 0 &&
                           sync_plan.submit_enabled == 0 &&
                           sync_plan.auth_attempt_allowed == 0,
                       "credential sync plan resume should keep GUI auth disabled");
  fails += expect_true(strings_equal(sync_plan.sync_ticket,
                                     "text-login-resume-sync-ticket") &&
                           strings_equal(sync_plan.sync_policy,
                                         "full-sync-declarative") &&
                           strings_equal(sync_plan.state,
                                         "sync-resume-ready"),
                       "credential sync plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_sync_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_sync_plan sync_plan;

  fails += expect_true(build_loginwindow_credential_screen_sync_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &sync_plan) == 0,
                       "credential sync plan submit should build");
  fails += expect_true(sync_plan.sync_plan_safe == 1 &&
                           sync_plan.submit_requested == 1 &&
                           sync_plan.sync_text_login_fallback == 1 &&
                           sync_plan.action_allowed == 0 &&
                           sync_plan.action_blocked == 1 &&
                           sync_plan.input_focus_allowed == 0,
                       "credential sync plan submit should force text login fallback");
  fails += expect_true(sync_plan.sync_submitted == 0 &&
                           sync_plan.sync_wait_allowed == 0 &&
                           sync_plan.sync_signal_allowed == 0 &&
                           sync_plan.sync_deadline_armed == 0 &&
                           sync_plan.sync_completion_reported == 0 &&
                           sync_plan.sync_cpu_gpu_sync_allowed == 0 &&
                           sync_plan.timeline_submitted == 0 &&
                           sync_plan.timeline_wait_allowed == 0 &&
                           sync_plan.timeline_signal_allowed == 0 &&
                           sync_plan.timeline_semaphore_allowed == 0 &&
                           sync_plan.timeline_value_allocated == 0 &&
                           sync_plan.timeline_value_published == 0 &&
                           sync_plan.fence_submitted == 0 &&
                           sync_plan.barrier_submitted == 0 &&
                           sync_plan.flush_submitted == 0 &&
                           sync_plan.framebuffer_written == 0 &&
                           sync_plan.blit_pixels_copied == 0 &&
                           sync_plan.output_submitted == 0 &&
                           sync_plan.display_submitted == 0 &&
                           sync_plan.page_flip_submitted == 0 &&
                           sync_plan.submit_callback_bound == 0 &&
                           sync_plan.auth_callback_bound == 0 &&
                           sync_plan.submit_enabled == 0 &&
                           sync_plan.auth_attempt_allowed == 0,
                       "credential sync plan submit must stay declarative");
  fails += expect_true(strings_equal(sync_plan.sync_ticket,
                                     "text-login-fallback-sync-ticket") &&
                           strings_equal(sync_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(sync_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential sync plan submit should explain fallback");

  fails += expect_true(build_loginwindow_credential_screen_sync_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &sync_plan) == 0,
                       "credential sync plan unknown should build");
  fails += expect_true(sync_plan.sync_plan_safe == 1 &&
                           sync_plan.sync_text_login_fallback == 1 &&
                           sync_plan.action_allowed == 0 &&
                           sync_plan.action_blocked == 1,
                       "credential sync plan unknown should force text login fallback");
  fails += expect_true(strings_equal(sync_plan.sync_ticket,
                                     "text-login-fallback-sync-ticket") &&
                           strings_equal(sync_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential sync plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_sync_plan_fails_closed_for_unsafe_or_missing_timeline_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_timeline_plan timeline_plan;
  struct login_window_credential_screen_sync_plan sync_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_sync_plan_build(
                           NULL, &sync_plan) == 0,
                       "credential sync plan missing timeline plan should build fail-closed state");
  fails += expect_true(sync_plan.timeline_plan_available == 0 &&
                           sync_plan.timeline_plan_safe == 0 &&
                           sync_plan.sync_plan_safe == 0 &&
                           sync_plan.route_selected == 0 &&
                           sync_plan.route_blocked == 1,
                       "credential sync plan missing timeline plan should block sync plan");
  fails += expect_true(sync_plan.sync_allowed == 0 &&
                           sync_plan.sync_submitted == 0 &&
                           sync_plan.sync_wait_allowed == 0 &&
                           sync_plan.sync_signal_allowed == 0 &&
                           sync_plan.sync_deadline_armed == 0 &&
                           sync_plan.sync_completion_reported == 0 &&
                           sync_plan.sync_text_login_fallback == 1 &&
                           sync_plan.submit_enabled == 0 &&
                           sync_plan.auth_attempt_allowed == 0,
                       "credential sync plan missing timeline plan must stay redacted");
  fails += expect_true(strings_equal(sync_plan.sync_ticket,
                                     "text-login-fallback-sync-ticket") &&
                           strings_equal(sync_plan.event_type,
                                         "credential-screen-sync-plan-unavailable") &&
                           strings_equal(sync_plan.blocked_reason,
                                         "timeline-plan-unavailable"),
                       "credential sync plan missing timeline plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_timeline_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &timeline_plan) == 0,
                       "credential sync plan unsafe timeline source should build");
  fails += expect_true(login_window_credential_screen_sync_plan_build(
                           &timeline_plan, &sync_plan) == 0,
                       "credential sync plan unsafe timeline plan should build blocked state");
  fails += expect_true(sync_plan.timeline_plan_available == 1 &&
                           sync_plan.timeline_plan_safe == 0 &&
                           sync_plan.sync_plan_safe == 0 &&
                           sync_plan.route_selected == 0 &&
                           sync_plan.route_blocked == 1,
                       "credential sync plan unsafe timeline plan should block sync plan");
  fails += expect_true(sync_plan.sync_allowed == 0 &&
                           sync_plan.sync_submitted == 0 &&
                           sync_plan.sync_text_login_fallback == 1 &&
                           sync_plan.submit_enabled == 0 &&
                           sync_plan.auth_attempt_allowed == 0,
                       "credential sync plan unsafe timeline plan must force text login fallback");
  fails += expect_true(strings_equal(sync_plan.sync_ticket,
                                     "text-login-fallback-sync-ticket") &&
                           strings_equal(sync_plan.event_type,
                                         "credential-screen-sync-plan-unsafe") &&
                           strings_equal(sync_plan.blocked_reason,
                                         "credential-sync-plan-unsafe"),
                       "credential sync plan unsafe timeline plan should force text login");

  fails += expect_true(build_loginwindow_credential_screen_timeline_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'y', 0, 0, 0,
                           1, &timeline_plan) == 0,
                       "credential sync plan submitted timeline source should build");
  timeline_plan.timeline_submitted = 1;
  timeline_plan.timeline_wait_allowed = 1;
  timeline_plan.timeline_signal_allowed = 1;
  timeline_plan.timeline_semaphore_allowed = 1;
  timeline_plan.timeline_value_allocated = 1;
  timeline_plan.timeline_value_published = 1;
  timeline_plan.timeline_cpu_gpu_sync_allowed = 1;
  timeline_plan.page_flip_allowed = 1;
  fails += expect_true(login_window_credential_screen_sync_plan_build(
                           &timeline_plan, &sync_plan) == 0,
                       "credential sync plan submitted timeline should fail closed");
  fails += expect_true(sync_plan.sync_plan_safe == 0 &&
                           sync_plan.sync_allowed == 0 &&
                           sync_plan.sync_submitted == 0 &&
                           sync_plan.sync_wait_allowed == 0 &&
                           sync_plan.sync_wait_submitted == 0 &&
                           sync_plan.sync_signal_allowed == 0 &&
                           sync_plan.sync_signal_submitted == 0 &&
                           sync_plan.sync_deadline_armed == 0 &&
                           sync_plan.sync_completion_reported == 0 &&
                           sync_plan.sync_cpu_gpu_sync_allowed == 0 &&
                           sync_plan.sync_cpu_gpu_sync_submitted == 0 &&
                           sync_plan.timeline_submitted == 0 &&
                           sync_plan.timeline_wait_allowed == 0 &&
                           sync_plan.timeline_signal_allowed == 0 &&
                           sync_plan.timeline_semaphore_allowed == 0 &&
                           sync_plan.timeline_value_allocated == 0 &&
                           sync_plan.timeline_value_published == 0 &&
                           sync_plan.timeline_cpu_gpu_sync_allowed == 0 &&
                           sync_plan.page_flip_allowed == 0 &&
                           sync_plan.page_flip_submitted == 0 &&
                           sync_plan.submit_enabled == 0 &&
                           sync_plan.auth_attempt_allowed == 0,
                       "credential sync plan must not copy unsafe submitted state");
  return fails;
}

int test_login_runtime_credential_timeline_sync_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_timeline_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_timeline_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_timeline_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_timeline_plan_fails_closed_for_unsafe_or_missing_fence_plan();
  fails += test_loginwindow_credential_screen_sync_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_sync_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_sync_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_sync_plan_fails_closed_for_unsafe_or_missing_timeline_plan();
  return fails;
}
