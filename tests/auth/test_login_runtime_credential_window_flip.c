/*
 * tests/auth/test_login_runtime_credential_window_flip.c
 *
 * Credential screen window flip plan coverage for the
 * `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-16 monolith
 * refactor (PR D.43 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_window_flip_plan_build`: 4
 *     tests covering the credential widgets flip + the text-route
 *     flip (recovery + resume) + the submit/unknown fallback flip
 *     + the missing-or-unsafe commit plan fail-closed default.
 *
 * Also exposes shared helper
 * `build_loginwindow_credential_screen_window_flip_plan_for_action`,
 * used by later companion files that chain on top of the flip
 * stage (window_vblank, ...).
 *
 * The companion entry `test_login_runtime_credential_window_flip_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_window_flip_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage,
    struct login_window_credential_screen_window_flip_plan *flip_plan) {
  struct login_window_credential_screen_window_commit_plan commit_plan;

  if (build_loginwindow_credential_screen_window_commit_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &commit_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_window_flip_plan_build(
      &commit_plan, flip_plan);
}

static int test_loginwindow_credential_screen_window_flip_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_window_flip_plan flip_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_flip_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'p', 0, 0, 0, 1,
          &flip_plan) == 0,
      "credential window flip plan edit should build");
  fails += expect_true(
      flip_plan.version ==
          LOGIN_WINDOW_CREDENTIAL_SCREEN_WINDOW_FLIP_PLAN_VERSION,
      "credential window flip plan should expose stable version");
  fails += expect_true(flip_plan.window_commit_plan_available == 1 &&
                           flip_plan.window_blit_plan_available == 1 &&
                           flip_plan.window_output_plan_available == 1 &&
                           flip_plan.window_display_plan_available == 1 &&
                           flip_plan.window_scanout_plan_available == 1 &&
                           flip_plan.window_vsync_plan_available == 1 &&
                           flip_plan.window_schedule_plan_available == 1 &&
                           flip_plan.window_present_plan_available == 1 &&
                           flip_plan.window_damage_plan_available == 1 &&
                           flip_plan.window_compositor_plan_available == 1 &&
                           flip_plan.window_surface_plan_available == 1 &&
                           flip_plan.window_surface_plan_safe == 1 &&
                           flip_plan.window_compositor_plan_safe == 1 &&
                           flip_plan.window_damage_plan_safe == 1 &&
                           flip_plan.window_present_plan_safe == 1 &&
                           flip_plan.window_schedule_plan_safe == 1 &&
                           flip_plan.window_vsync_plan_safe == 1 &&
                           flip_plan.window_scanout_plan_safe == 1 &&
                           flip_plan.window_display_plan_safe == 1 &&
                           flip_plan.window_output_plan_safe == 1 &&
                           flip_plan.window_blit_plan_safe == 1 &&
                           flip_plan.window_commit_plan_safe == 1 &&
                           flip_plan.window_flip_plan_safe == 1,
                       "credential window flip plan should require safe commit plan");
  fails += expect_true(flip_plan.flip_required == 1 &&
                           flip_plan.flip_allowed == 1 &&
                           flip_plan.flip_submitted == 0 &&
                           flip_plan.flip_ticket_selected == 1 &&
                           flip_plan.flip_target_selected == 1 &&
                           flip_plan.flip_buffer_attached == 0 &&
                           flip_plan.flip_buffer_submitted == 0 &&
                           flip_plan.flip_vblank_armed == 0 &&
                           flip_plan.flip_vblank_submitted == 0 &&
                           flip_plan.flip_event_armed == 0 &&
                           flip_plan.flip_event_submitted == 0 &&
                           flip_plan.flip_async_allowed == 0 &&
                           flip_plan.flip_async_submitted == 0 &&
                           flip_plan.flip_error == 0,
                       "credential window flip plan should remain declarative");
  fails += expect_true(flip_plan.damage_error == 0 &&
                           flip_plan.present_error == 0 &&
                           flip_plan.schedule_error == 0 &&
                           flip_plan.vsync_error == 0 &&
                           flip_plan.scanout_error == 0 &&
                           flip_plan.display_error == 0 &&
                           flip_plan.output_error == 0 &&
                           flip_plan.blit_error == 0 &&
                           flip_plan.commit_error == 0,
                       "credential window flip plan should preserve upstream plan success");
  fails += expect_true(flip_plan.commit_submitted == 0 &&
                           flip_plan.commit_state_attached == 0 &&
                           flip_plan.commit_state_submitted == 0 &&
                           flip_plan.commit_atomic_allowed == 0 &&
                           flip_plan.commit_atomic_submitted == 0 &&
                           flip_plan.commit_callback_armed == 0 &&
                           flip_plan.commit_callback_submitted == 0 &&
                           flip_plan.blit_submitted == 0 &&
                           flip_plan.output_submitted == 0 &&
                           flip_plan.output_connector_attached == 0 &&
                           flip_plan.output_signal_submitted == 0 &&
                           flip_plan.display_submitted == 0 &&
                           flip_plan.display_controller_attached == 0 &&
                           flip_plan.scanout_submitted == 0 &&
                           flip_plan.scanout_buffer_attached == 0 &&
                           flip_plan.scanout_display_flip_submitted == 0 &&
                           flip_plan.vsync_submitted == 0 &&
                           flip_plan.vsync_wait_submitted == 0 &&
                           flip_plan.vsync_fence_armed == 0 &&
                           flip_plan.schedule_submitted == 0 &&
                           flip_plan.frame_timer_armed == 0 &&
                           flip_plan.compositor_wake_submitted == 0 &&
                           flip_plan.page_flip_submitted == 0 &&
                           flip_plan.present_submitted == 0 &&
                           flip_plan.damage_submitted == 0 &&
                           flip_plan.compositor_submitted == 0 &&
                           flip_plan.surface_bound == 0 &&
                           flip_plan.surface_memory_mapped == 0 &&
                           flip_plan.surface_pixels_written == 0 &&
                           flip_plan.window_created == 0 &&
                           flip_plan.gui_submitted == 0,
                       "credential window flip plan must not copy any real upstream effect");
  fails += expect_true(flip_plan.flip_credential_panel == 1 &&
                           flip_plan.flip_credential_input == 1 &&
                           flip_plan.flip_credential_focus == 1 &&
                           flip_plan.flip_text_login == 0 &&
                           flip_plan.flip_text_login_fallback == 0,
                       "credential window flip plan should mark credential widgets");
  fails += expect_true(flip_plan.submit_callback_bound == 0 &&
                           flip_plan.auth_callback_bound == 0 &&
                           flip_plan.submit_enabled == 0 &&
                           flip_plan.auth_attempt_allowed == 0 &&
                           flip_plan.credential_redacted == 1 &&
                           flip_plan.length_redacted == 1 &&
                           flip_plan.raw_secret_exposed == 0 &&
                           flip_plan.masked_text_exposed == 0,
                       "credential window flip plan must stay redacted and auth-disabled");
  fails += expect_true(
      strings_equal(flip_plan.flip_ticket,
                    "credential-screen-window-flip-ticket") &&
          strings_equal(flip_plan.commit_ticket,
                        "credential-screen-window-commit-ticket") &&
          strings_equal(flip_plan.flip_policy,
                        "incremental-window-flip-declarative") &&
          strings_equal(flip_plan.state,
                        "window-flip-credential-ready"),
      "credential window flip plan should report flip ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_flip_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_window_flip_plan flip_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_flip_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
          LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1, 1,
          &flip_plan) == 0,
      "credential window flip plan recovery should build");
  fails += expect_true(flip_plan.window_flip_plan_safe == 1 &&
                           flip_plan.flip_allowed == 1 &&
                           flip_plan.flip_submitted == 0 &&
                           flip_plan.flip_text_recovery == 1 &&
                           flip_plan.flip_text_login == 1 &&
                           flip_plan.flip_text_login_fallback == 0 &&
                           flip_plan.flip_credential_focus == 0,
                       "credential window flip plan recovery should mark text recovery");
  fails += expect_true(flip_plan.full_schedule_required == 1 &&
                           flip_plan.schedule_incremental_allowed == 0 &&
                           flip_plan.schedule_cache_allowed == 0 &&
                           flip_plan.schedule_reuse_allowed == 0 &&
                           flip_plan.input_focus_allowed == 0 &&
                           flip_plan.flip_buffer_attached == 0 &&
                           flip_plan.flip_vblank_armed == 0 &&
                           flip_plan.flip_async_submitted == 0 &&
                           flip_plan.commit_state_attached == 0 &&
                           flip_plan.commit_atomic_submitted == 0 &&
                           flip_plan.output_signal_submitted == 0 &&
                           flip_plan.display_controller_attached == 0 &&
                           flip_plan.scanout_buffer_attached == 0 &&
                           flip_plan.vsync_fence_armed == 0 &&
                           flip_plan.page_flip_submitted == 0,
                       "credential window flip plan recovery should require full declarative flip");
  fails += expect_true(
      strings_equal(flip_plan.flip_ticket,
                    "text-recovery-window-flip-ticket") &&
          strings_equal(flip_plan.compositor_target,
                        "text-recovery-window-flip") &&
          strings_equal(flip_plan.blocked_reason, "text-recovery-only"),
      "credential window flip plan recovery should report recovery ticket");

  fails += expect_true(
      build_loginwindow_credential_screen_window_flip_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0, 1,
          &flip_plan) == 0,
      "credential window flip plan resume should build");
  fails += expect_true(flip_plan.window_flip_plan_safe == 1 &&
                           flip_plan.flip_text_login_resume == 1 &&
                           flip_plan.session_reset_required == 1 &&
                           flip_plan.login_screen_rerender_required == 1 &&
                           flip_plan.full_schedule_required == 1 &&
                           flip_plan.schedule_incremental_allowed == 0 &&
                           flip_plan.schedule_cache_allowed == 0 &&
                           flip_plan.schedule_reuse_allowed == 0,
                       "credential window flip plan resume should require full rerender flip");
  fails += expect_true(flip_plan.flip_submitted == 0 &&
                           flip_plan.flip_buffer_attached == 0 &&
                           flip_plan.flip_buffer_submitted == 0 &&
                           flip_plan.flip_vblank_armed == 0 &&
                           flip_plan.flip_vblank_submitted == 0 &&
                           flip_plan.flip_event_armed == 0 &&
                           flip_plan.flip_event_submitted == 0 &&
                           flip_plan.flip_async_allowed == 0 &&
                           flip_plan.flip_async_submitted == 0 &&
                           flip_plan.commit_state_attached == 0 &&
                           flip_plan.commit_atomic_allowed == 0 &&
                           flip_plan.commit_callback_submitted == 0 &&
                           flip_plan.output_signal_submitted == 0 &&
                           flip_plan.display_controller_attached == 0 &&
                           flip_plan.scanout_buffer_attached == 0 &&
                           flip_plan.vsync_fence_armed == 0 &&
                           flip_plan.page_flip_submitted == 0 &&
                           flip_plan.submit_enabled == 0 &&
                           flip_plan.auth_attempt_allowed == 0,
                       "credential window flip plan resume must keep GUI auth disabled");
  fails += expect_true(
      strings_equal(flip_plan.flip_ticket,
                    "text-login-resume-window-flip-ticket") &&
          strings_equal(flip_plan.cache_policy,
                        "window-flip-cache-bypassed-for-rerender") &&
          strings_equal(flip_plan.state, "window-flip-resume-ready"),
      "credential window flip plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_flip_plan_falls_back_for_submit_and_unknown_action(void) {
  int fails = 0;
  struct login_window_credential_screen_window_flip_plan flip_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_flip_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_SUBMIT, 0, 0, 0, 0, 1,
          &flip_plan) == 0,
      "credential window flip plan submit should build");
  fails += expect_true(flip_plan.window_flip_plan_safe == 1 &&
                           flip_plan.submit_requested == 1 &&
                           flip_plan.submit_blocked == 1 &&
                           flip_plan.action_allowed == 0 &&
                           flip_plan.action_blocked == 1 &&
                           flip_plan.input_focus_allowed == 0 &&
                           flip_plan.flip_text_login == 1 &&
                           flip_plan.flip_text_login_fallback == 1 &&
                           flip_plan.flip_submitted == 0 &&
                           flip_plan.flip_buffer_attached == 0 &&
                           flip_plan.flip_vblank_submitted == 0 &&
                           flip_plan.flip_async_submitted == 0,
                       "credential window flip plan submit should force text login");
  fails += expect_true(
      strings_equal(flip_plan.flip_ticket,
                    "text-login-fallback-window-flip-ticket") &&
          strings_equal(flip_plan.flip_policy,
                        "fallback-window-flip-declarative") &&
          strings_equal(flip_plan.blocked_reason, "gui-submit-disabled"),
      "credential window flip plan submit should report disabled GUI submit");

  fails += expect_true(
      build_loginwindow_credential_screen_window_flip_plan_for_action(
          9876, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0, 1,
          &flip_plan) == 0,
      "credential window flip plan unknown action should build");
  fails += expect_true(flip_plan.window_flip_plan_safe == 1 &&
                           flip_plan.action_allowed == 0 &&
                           flip_plan.action_blocked == 1 &&
                           flip_plan.input_focus_allowed == 0 &&
                           flip_plan.flip_text_login == 1 &&
                           flip_plan.flip_text_login_fallback == 1 &&
                           flip_plan.flip_submitted == 0 &&
                           flip_plan.flip_async_submitted == 0,
                       "credential window flip plan unknown action should force text login");
  fails += expect_true(
      strings_equal(flip_plan.flip_ticket,
                    "text-login-fallback-window-flip-ticket") &&
          strings_equal(flip_plan.compositor_target,
                        "text-login-fallback-window-flip") &&
          strings_equal(flip_plan.state,
                        "window-flip-text-login-ready"),
      "credential window flip plan unknown action should report fallback ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_flip_plan_fails_closed_for_unsafe_or_missing_commit_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_window_commit_plan commit_plan;
  struct login_window_credential_screen_window_flip_plan flip_plan;

  fails += expect_true(
      login_window_credential_screen_window_flip_plan_build(
          NULL, &flip_plan) == 0,
      "credential window flip plan missing commit should build fallback");
  fails += expect_true(flip_plan.window_commit_plan_available == 0 &&
                           flip_plan.window_commit_plan_safe == 0 &&
                           flip_plan.window_flip_plan_safe == 0 &&
                           flip_plan.route_blocked == 1 &&
                           flip_plan.flip_allowed == 0 &&
                           flip_plan.flip_ticket_selected == 0 &&
                           flip_plan.flip_target_selected == 0 &&
                           flip_plan.flip_text_login == 1 &&
                           flip_plan.flip_text_login_fallback == 1 &&
                           flip_plan.flip_submitted == 0 &&
                           flip_plan.flip_async_submitted == 0,
                       "credential window flip plan missing commit should fail closed");
  fails += expect_true(
      strings_equal(flip_plan.flip_ticket,
                    "text-login-fallback-window-flip-ticket") &&
          strings_equal(flip_plan.event_type,
                        "credential-screen-window-flip-plan-unavailable") &&
          strings_equal(flip_plan.blocked_reason,
                        "window-commit-plan-unavailable"),
      "credential window flip plan missing commit should report missing upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_commit_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0, 0, 1,
          &commit_plan) == 0,
      "credential window flip plan unsafe commit fixture should build");
  commit_plan.window_commit_plan_safe = 0;
  commit_plan.raw_secret_exposed = 1;
  commit_plan.submit_blocked = 0;
  fails += expect_true(
      login_window_credential_screen_window_flip_plan_build(
          &commit_plan, &flip_plan) == 0,
      "credential window flip plan unsafe commit should build fallback");
  fails += expect_true(flip_plan.window_commit_plan_available == 1 &&
                           flip_plan.window_commit_plan_safe == 0 &&
                           flip_plan.window_flip_plan_safe == 0 &&
                           flip_plan.route_blocked == 1 &&
                           flip_plan.flip_allowed == 0 &&
                           flip_plan.flip_ticket_selected == 0 &&
                           flip_plan.flip_target_selected == 0 &&
                           flip_plan.flip_text_login == 1 &&
                           flip_plan.flip_text_login_fallback == 1 &&
                           flip_plan.raw_secret_exposed == 0,
                       "credential window flip plan unsafe commit should fail closed");
  fails += expect_true(
      strings_equal(flip_plan.flip_ticket,
                    "text-login-fallback-window-flip-ticket") &&
          strings_equal(flip_plan.event_type,
                        "credential-screen-window-flip-plan-unsafe") &&
          strings_equal(flip_plan.blocked_reason,
                        "credential-window-flip-plan-unsafe"),
      "credential window flip plan unsafe commit should report unsafe upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_commit_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'f', 0, 0, 0, 1,
          &commit_plan) == 0,
      "credential window flip plan forged origin fixture should build");
  commit_plan.window_blit_plan_available = 0;
  commit_plan.window_surface_plan_safe = 0;
  commit_plan.window_commit_plan_safe = 1;
  fails += expect_true(
      login_window_credential_screen_window_flip_plan_build(
          &commit_plan, &flip_plan) == 0,
      "credential window flip plan forged commit origin should build fallback");
  fails += expect_true(flip_plan.window_commit_plan_available == 1 &&
                           flip_plan.window_blit_plan_available == 0 &&
                           flip_plan.window_surface_plan_safe == 0 &&
                           flip_plan.window_commit_plan_safe == 1 &&
                           flip_plan.window_flip_plan_safe == 0 &&
                           flip_plan.flip_allowed == 0 &&
                           flip_plan.flip_ticket_selected == 0 &&
                           flip_plan.flip_text_login_fallback == 1 &&
                           flip_plan.submit_enabled == 0 &&
                           flip_plan.auth_attempt_allowed == 0,
                       "credential window flip plan should reject forged commit origin");
  fails += expect_true(
      strings_equal(flip_plan.flip_ticket,
                    "text-login-fallback-window-flip-ticket") &&
          strings_equal(flip_plan.event_type,
                        "credential-screen-window-flip-plan-unsafe") &&
          strings_equal(flip_plan.blocked_reason,
                        "credential-window-flip-plan-unsafe"),
      "credential window flip plan forged origin should report unsafe upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_commit_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0, 1,
          &commit_plan) == 0,
      "credential window flip plan submitted commit fixture should build");
  commit_plan.commit_submitted = 1;
  commit_plan.commit_state_attached = 1;
  commit_plan.commit_state_submitted = 1;
  commit_plan.commit_atomic_allowed = 1;
  commit_plan.commit_atomic_submitted = 1;
  commit_plan.commit_callback_armed = 1;
  commit_plan.commit_callback_submitted = 1;
  commit_plan.blit_submitted = 1;
  commit_plan.output_submitted = 1;
  commit_plan.display_submitted = 1;
  commit_plan.scanout_submitted = 1;
  commit_plan.vsync_submitted = 1;
  commit_plan.schedule_submitted = 1;
  commit_plan.frame_timer_armed = 1;
  commit_plan.page_flip_submitted = 1;
  fails += expect_true(
      login_window_credential_screen_window_flip_plan_build(
          &commit_plan, &flip_plan) == 0,
      "credential window flip plan submitted commit should build fallback");
  fails += expect_true(flip_plan.window_flip_plan_safe == 0 &&
                           flip_plan.flip_allowed == 0 &&
                           flip_plan.flip_submitted == 0 &&
                           flip_plan.flip_buffer_attached == 0 &&
                           flip_plan.flip_buffer_submitted == 0 &&
                           flip_plan.flip_vblank_armed == 0 &&
                           flip_plan.flip_vblank_submitted == 0 &&
                           flip_plan.flip_event_armed == 0 &&
                           flip_plan.flip_event_submitted == 0 &&
                           flip_plan.flip_async_allowed == 0 &&
                           flip_plan.flip_async_submitted == 0 &&
                           flip_plan.commit_submitted == 0 &&
                           flip_plan.commit_state_attached == 0 &&
                           flip_plan.commit_atomic_submitted == 0 &&
                           flip_plan.commit_callback_submitted == 0 &&
                           flip_plan.blit_submitted == 0 &&
                           flip_plan.output_submitted == 0 &&
                           flip_plan.display_submitted == 0 &&
                           flip_plan.scanout_submitted == 0 &&
                           flip_plan.vsync_submitted == 0 &&
                           flip_plan.schedule_submitted == 0 &&
                           flip_plan.frame_timer_armed == 0 &&
                           flip_plan.page_flip_submitted == 0 &&
                           flip_plan.submit_enabled == 0 &&
                           flip_plan.auth_attempt_allowed == 0,
                       "credential window flip plan must not copy unsafe submitted commit state");
  fails += expect_true(
      strings_equal(flip_plan.flip_ticket,
                    "text-login-fallback-window-flip-ticket") &&
          strings_equal(flip_plan.event_type,
                        "credential-screen-window-flip-plan-unsafe") &&
          strings_equal(flip_plan.blocked_reason,
                        "credential-window-flip-plan-unsafe"),
      "credential window flip plan submitted commit should report unsafe upstream");
  return fails;
}

int test_login_runtime_credential_window_flip_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_window_flip_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_window_flip_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_window_flip_plan_falls_back_for_submit_and_unknown_action();
  fails += test_loginwindow_credential_screen_window_flip_plan_fails_closed_for_unsafe_or_missing_commit_plan();
  return fails;
}
