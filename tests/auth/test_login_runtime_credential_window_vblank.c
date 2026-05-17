/*
 * tests/auth/test_login_runtime_credential_window_vblank.c
 *
 * Credential screen window vblank plan coverage for the
 * `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-16 monolith
 * refactor (PR D.44 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_window_vblank_plan_build`: 4
 *     tests covering the credential widgets vblank + the
 *     text-route vblank (recovery + resume) + the submit/unknown
 *     fallback vblank + the missing-or-unsafe flip plan
 *     fail-closed default.
 *
 * Also exposes shared helper
 * `build_loginwindow_credential_screen_window_vblank_plan_for_action`,
 * used by later companion files that chain on top of the vblank
 * stage (window_event, ...).
 *
 * The companion entry `test_login_runtime_credential_window_vblank_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_window_vblank_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage,
    struct login_window_credential_screen_window_vblank_plan *vblank_plan) {
  struct login_window_credential_screen_window_flip_plan flip_plan;

  if (build_loginwindow_credential_screen_window_flip_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &flip_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_window_vblank_plan_build(
      &flip_plan, vblank_plan);
}

static int test_loginwindow_credential_screen_window_vblank_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_window_vblank_plan vblank_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_vblank_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'p', 0, 0, 0, 1,
          &vblank_plan) == 0,
      "credential window vblank plan edit should build");
  fails += expect_true(
      vblank_plan.version ==
          LOGIN_WINDOW_CREDENTIAL_SCREEN_WINDOW_VBLANK_PLAN_VERSION,
      "credential window vblank plan should expose stable version");
  fails += expect_true(vblank_plan.window_flip_plan_available == 1 &&
                           vblank_plan.window_commit_plan_available == 1 &&
                           vblank_plan.window_blit_plan_available == 1 &&
                           vblank_plan.window_output_plan_available == 1 &&
                           vblank_plan.window_display_plan_available == 1 &&
                           vblank_plan.window_scanout_plan_available == 1 &&
                           vblank_plan.window_vsync_plan_available == 1 &&
                           vblank_plan.window_schedule_plan_available == 1 &&
                           vblank_plan.window_present_plan_available == 1 &&
                           vblank_plan.window_damage_plan_available == 1 &&
                           vblank_plan.window_compositor_plan_available == 1 &&
                           vblank_plan.window_surface_plan_available == 1 &&
                           vblank_plan.window_surface_plan_safe == 1 &&
                           vblank_plan.window_compositor_plan_safe == 1 &&
                           vblank_plan.window_damage_plan_safe == 1 &&
                           vblank_plan.window_present_plan_safe == 1 &&
                           vblank_plan.window_schedule_plan_safe == 1 &&
                           vblank_plan.window_vsync_plan_safe == 1 &&
                           vblank_plan.window_scanout_plan_safe == 1 &&
                           vblank_plan.window_display_plan_safe == 1 &&
                           vblank_plan.window_output_plan_safe == 1 &&
                           vblank_plan.window_blit_plan_safe == 1 &&
                           vblank_plan.window_commit_plan_safe == 1 &&
                           vblank_plan.window_flip_plan_safe == 1 &&
                           vblank_plan.window_vblank_plan_safe == 1,
                       "credential window vblank plan should require safe flip plan");
  fails += expect_true(vblank_plan.vblank_required == 1 &&
                           vblank_plan.vblank_allowed == 1 &&
                           vblank_plan.vblank_submitted == 0 &&
                           vblank_plan.vblank_ticket_selected == 1 &&
                           vblank_plan.vblank_target_selected == 1 &&
                           vblank_plan.vblank_event_armed == 0 &&
                           vblank_plan.vblank_event_submitted == 0 &&
                           vblank_plan.vblank_callback_armed == 0 &&
                           vblank_plan.vblank_callback_submitted == 0 &&
                           vblank_plan.vblank_timestamp_captured == 0 &&
                           vblank_plan.vblank_timestamp_submitted == 0 &&
                           vblank_plan.vblank_frame_completed == 0 &&
                           vblank_plan.vblank_frame_submitted == 0 &&
                           vblank_plan.vblank_error == 0,
                       "credential window vblank plan should remain declarative");
  fails += expect_true(vblank_plan.damage_error == 0 &&
                           vblank_plan.present_error == 0 &&
                           vblank_plan.schedule_error == 0 &&
                           vblank_plan.vsync_error == 0 &&
                           vblank_plan.scanout_error == 0 &&
                           vblank_plan.display_error == 0 &&
                           vblank_plan.output_error == 0 &&
                           vblank_plan.blit_error == 0 &&
                           vblank_plan.commit_error == 0 &&
                           vblank_plan.flip_error == 0,
                       "credential window vblank plan should preserve upstream plan success");
  fails += expect_true(vblank_plan.flip_submitted == 0 &&
                           vblank_plan.flip_buffer_attached == 0 &&
                           vblank_plan.flip_buffer_submitted == 0 &&
                           vblank_plan.flip_vblank_armed == 0 &&
                           vblank_plan.flip_vblank_submitted == 0 &&
                           vblank_plan.flip_event_armed == 0 &&
                           vblank_plan.flip_event_submitted == 0 &&
                           vblank_plan.flip_async_allowed == 0 &&
                           vblank_plan.flip_async_submitted == 0 &&
                           vblank_plan.commit_submitted == 0 &&
                           vblank_plan.commit_state_attached == 0 &&
                           vblank_plan.commit_state_submitted == 0 &&
                           vblank_plan.commit_atomic_allowed == 0 &&
                           vblank_plan.commit_atomic_submitted == 0 &&
                           vblank_plan.commit_callback_armed == 0 &&
                           vblank_plan.commit_callback_submitted == 0 &&
                           vblank_plan.blit_submitted == 0 &&
                           vblank_plan.output_submitted == 0 &&
                           vblank_plan.output_connector_attached == 0 &&
                           vblank_plan.output_signal_submitted == 0 &&
                           vblank_plan.display_submitted == 0 &&
                           vblank_plan.display_controller_attached == 0 &&
                           vblank_plan.scanout_submitted == 0 &&
                           vblank_plan.scanout_buffer_attached == 0 &&
                           vblank_plan.scanout_display_flip_submitted == 0 &&
                           vblank_plan.vsync_submitted == 0 &&
                           vblank_plan.vsync_wait_submitted == 0 &&
                           vblank_plan.vsync_fence_armed == 0 &&
                           vblank_plan.schedule_submitted == 0 &&
                           vblank_plan.frame_timer_armed == 0 &&
                           vblank_plan.compositor_wake_submitted == 0 &&
                           vblank_plan.page_flip_submitted == 0 &&
                           vblank_plan.present_submitted == 0 &&
                           vblank_plan.damage_submitted == 0 &&
                           vblank_plan.compositor_submitted == 0 &&
                           vblank_plan.surface_bound == 0 &&
                           vblank_plan.surface_memory_mapped == 0 &&
                           vblank_plan.surface_pixels_written == 0 &&
                           vblank_plan.window_created == 0 &&
                           vblank_plan.gui_submitted == 0,
                       "credential window vblank plan must not copy any real upstream effect");
  fails += expect_true(vblank_plan.vblank_credential_panel == 1 &&
                           vblank_plan.vblank_credential_input == 1 &&
                           vblank_plan.vblank_credential_focus == 1 &&
                           vblank_plan.vblank_text_login == 0 &&
                           vblank_plan.vblank_text_login_fallback == 0,
                       "credential window vblank plan should mark credential widgets");
  fails += expect_true(vblank_plan.submit_callback_bound == 0 &&
                           vblank_plan.auth_callback_bound == 0 &&
                           vblank_plan.submit_enabled == 0 &&
                           vblank_plan.auth_attempt_allowed == 0 &&
                           vblank_plan.credential_redacted == 1 &&
                           vblank_plan.length_redacted == 1 &&
                           vblank_plan.raw_secret_exposed == 0 &&
                           vblank_plan.masked_text_exposed == 0,
                       "credential window vblank plan must stay redacted and auth-disabled");
  fails += expect_true(
      strings_equal(vblank_plan.vblank_ticket,
                    "credential-screen-window-vblank-ticket") &&
          strings_equal(vblank_plan.flip_ticket,
                        "credential-screen-window-flip-ticket") &&
          strings_equal(vblank_plan.vblank_policy,
                        "incremental-window-vblank-declarative") &&
          strings_equal(vblank_plan.state,
                        "window-vblank-credential-ready"),
      "credential window vblank plan should report vblank ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_vblank_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_window_vblank_plan vblank_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_vblank_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
          LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1, 1,
          &vblank_plan) == 0,
      "credential window vblank plan recovery should build");
  fails += expect_true(vblank_plan.window_vblank_plan_safe == 1 &&
                           vblank_plan.vblank_allowed == 1 &&
                           vblank_plan.vblank_submitted == 0 &&
                           vblank_plan.vblank_text_recovery == 1 &&
                           vblank_plan.vblank_text_login == 1 &&
                           vblank_plan.vblank_text_login_fallback == 0 &&
                           vblank_plan.vblank_credential_focus == 0,
                       "credential window vblank plan recovery should mark text recovery");
  fails += expect_true(vblank_plan.full_schedule_required == 1 &&
                           vblank_plan.schedule_incremental_allowed == 0 &&
                           vblank_plan.schedule_cache_allowed == 0 &&
                           vblank_plan.schedule_reuse_allowed == 0 &&
                           vblank_plan.input_focus_allowed == 0 &&
                           vblank_plan.vblank_event_armed == 0 &&
                           vblank_plan.vblank_callback_armed == 0 &&
                           vblank_plan.vblank_timestamp_captured == 0 &&
                           vblank_plan.vblank_frame_completed == 0 &&
                           vblank_plan.flip_buffer_attached == 0 &&
                           vblank_plan.flip_vblank_armed == 0 &&
                           vblank_plan.flip_async_submitted == 0 &&
                           vblank_plan.commit_state_attached == 0 &&
                           vblank_plan.commit_atomic_submitted == 0 &&
                           vblank_plan.output_signal_submitted == 0 &&
                           vblank_plan.display_controller_attached == 0 &&
                           vblank_plan.scanout_buffer_attached == 0 &&
                           vblank_plan.vsync_fence_armed == 0 &&
                           vblank_plan.page_flip_submitted == 0,
                       "credential window vblank plan recovery should require full declarative vblank");
  fails += expect_true(
      strings_equal(vblank_plan.vblank_ticket,
                    "text-recovery-window-vblank-ticket") &&
          strings_equal(vblank_plan.compositor_target,
                        "text-recovery-window-vblank") &&
          strings_equal(vblank_plan.blocked_reason, "text-recovery-only"),
      "credential window vblank plan recovery should report recovery ticket");

  fails += expect_true(
      build_loginwindow_credential_screen_window_vblank_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0, 1,
          &vblank_plan) == 0,
      "credential window vblank plan resume should build");
  fails += expect_true(vblank_plan.window_vblank_plan_safe == 1 &&
                           vblank_plan.vblank_text_login_resume == 1 &&
                           vblank_plan.session_reset_required == 1 &&
                           vblank_plan.login_screen_rerender_required == 1 &&
                           vblank_plan.full_schedule_required == 1 &&
                           vblank_plan.schedule_incremental_allowed == 0 &&
                           vblank_plan.schedule_cache_allowed == 0 &&
                           vblank_plan.schedule_reuse_allowed == 0,
                       "credential window vblank plan resume should require full rerender vblank");
  fails += expect_true(vblank_plan.vblank_submitted == 0 &&
                           vblank_plan.vblank_event_armed == 0 &&
                           vblank_plan.vblank_event_submitted == 0 &&
                           vblank_plan.vblank_callback_armed == 0 &&
                           vblank_plan.vblank_callback_submitted == 0 &&
                           vblank_plan.vblank_timestamp_captured == 0 &&
                           vblank_plan.vblank_timestamp_submitted == 0 &&
                           vblank_plan.vblank_frame_completed == 0 &&
                           vblank_plan.vblank_frame_submitted == 0 &&
                           vblank_plan.flip_submitted == 0 &&
                           vblank_plan.flip_buffer_attached == 0 &&
                           vblank_plan.flip_buffer_submitted == 0 &&
                           vblank_plan.flip_vblank_armed == 0 &&
                           vblank_plan.flip_vblank_submitted == 0 &&
                           vblank_plan.flip_event_armed == 0 &&
                           vblank_plan.flip_event_submitted == 0 &&
                           vblank_plan.flip_async_allowed == 0 &&
                           vblank_plan.flip_async_submitted == 0 &&
                           vblank_plan.commit_state_attached == 0 &&
                           vblank_plan.commit_atomic_allowed == 0 &&
                           vblank_plan.commit_callback_submitted == 0 &&
                           vblank_plan.output_signal_submitted == 0 &&
                           vblank_plan.display_controller_attached == 0 &&
                           vblank_plan.scanout_buffer_attached == 0 &&
                           vblank_plan.vsync_fence_armed == 0 &&
                           vblank_plan.page_flip_submitted == 0 &&
                           vblank_plan.submit_enabled == 0 &&
                           vblank_plan.auth_attempt_allowed == 0,
                       "credential window vblank plan resume must keep GUI auth disabled");
  fails += expect_true(
      strings_equal(vblank_plan.vblank_ticket,
                    "text-login-resume-window-vblank-ticket") &&
          strings_equal(vblank_plan.cache_policy,
                        "window-vblank-cache-bypassed-for-rerender") &&
          strings_equal(vblank_plan.state, "window-vblank-resume-ready"),
      "credential window vblank plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_vblank_plan_falls_back_for_submit_and_unknown_action(void) {
  int fails = 0;
  struct login_window_credential_screen_window_vblank_plan vblank_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_vblank_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_SUBMIT, 0, 0, 0, 0, 1,
          &vblank_plan) == 0,
      "credential window vblank plan submit should build");
  fails += expect_true(vblank_plan.window_vblank_plan_safe == 1 &&
                           vblank_plan.submit_requested == 1 &&
                           vblank_plan.submit_blocked == 1 &&
                           vblank_plan.action_allowed == 0 &&
                           vblank_plan.action_blocked == 1 &&
                           vblank_plan.input_focus_allowed == 0 &&
                           vblank_plan.vblank_text_login == 1 &&
                           vblank_plan.vblank_text_login_fallback == 1 &&
                           vblank_plan.vblank_submitted == 0 &&
                           vblank_plan.vblank_event_armed == 0 &&
                           vblank_plan.vblank_callback_submitted == 0 &&
                           vblank_plan.vblank_frame_completed == 0,
                       "credential window vblank plan submit should force text login");
  fails += expect_true(
      strings_equal(vblank_plan.vblank_ticket,
                    "text-login-fallback-window-vblank-ticket") &&
          strings_equal(vblank_plan.vblank_policy,
                        "fallback-window-vblank-declarative") &&
          strings_equal(vblank_plan.blocked_reason, "gui-submit-disabled"),
      "credential window vblank plan submit should report disabled GUI submit");

  fails += expect_true(
      build_loginwindow_credential_screen_window_vblank_plan_for_action(
          9876, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0, 1,
          &vblank_plan) == 0,
      "credential window vblank plan unknown action should build");
  fails += expect_true(vblank_plan.window_vblank_plan_safe == 1 &&
                           vblank_plan.action_allowed == 0 &&
                           vblank_plan.action_blocked == 1 &&
                           vblank_plan.input_focus_allowed == 0 &&
                           vblank_plan.vblank_text_login == 1 &&
                           vblank_plan.vblank_text_login_fallback == 1 &&
                           vblank_plan.vblank_submitted == 0 &&
                           vblank_plan.vblank_frame_submitted == 0,
                       "credential window vblank plan unknown action should force text login");
  fails += expect_true(
      strings_equal(vblank_plan.vblank_ticket,
                    "text-login-fallback-window-vblank-ticket") &&
          strings_equal(vblank_plan.compositor_target,
                        "text-login-fallback-window-vblank") &&
          strings_equal(vblank_plan.state,
                        "window-vblank-text-login-ready"),
      "credential window vblank plan unknown action should report fallback ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_vblank_plan_fails_closed_for_unsafe_or_missing_flip_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_window_flip_plan flip_plan;
  struct login_window_credential_screen_window_vblank_plan vblank_plan;

  fails += expect_true(
      login_window_credential_screen_window_vblank_plan_build(
          NULL, &vblank_plan) == 0,
      "credential window vblank plan missing flip should build fallback");
  fails += expect_true(vblank_plan.window_flip_plan_available == 0 &&
                           vblank_plan.window_flip_plan_safe == 0 &&
                           vblank_plan.window_vblank_plan_safe == 0 &&
                           vblank_plan.route_blocked == 1 &&
                           vblank_plan.vblank_allowed == 0 &&
                           vblank_plan.vblank_ticket_selected == 0 &&
                           vblank_plan.vblank_target_selected == 0 &&
                           vblank_plan.vblank_text_login == 1 &&
                           vblank_plan.vblank_text_login_fallback == 1 &&
                           vblank_plan.vblank_submitted == 0 &&
                           vblank_plan.vblank_frame_submitted == 0,
                       "credential window vblank plan missing flip should fail closed");
  fails += expect_true(
      strings_equal(vblank_plan.vblank_ticket,
                    "text-login-fallback-window-vblank-ticket") &&
          strings_equal(vblank_plan.event_type,
                        "credential-screen-window-vblank-plan-unavailable") &&
          strings_equal(vblank_plan.blocked_reason,
                        "window-flip-plan-unavailable"),
      "credential window vblank plan missing flip should report missing upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_flip_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0, 0, 1,
          &flip_plan) == 0,
      "credential window vblank plan unsafe flip fixture should build");
  flip_plan.window_flip_plan_safe = 0;
  flip_plan.raw_secret_exposed = 1;
  flip_plan.submit_blocked = 0;
  fails += expect_true(
      login_window_credential_screen_window_vblank_plan_build(
          &flip_plan, &vblank_plan) == 0,
      "credential window vblank plan unsafe flip should build fallback");
  fails += expect_true(vblank_plan.window_flip_plan_available == 1 &&
                           vblank_plan.window_flip_plan_safe == 0 &&
                           vblank_plan.window_vblank_plan_safe == 0 &&
                           vblank_plan.route_blocked == 1 &&
                           vblank_plan.vblank_allowed == 0 &&
                           vblank_plan.vblank_ticket_selected == 0 &&
                           vblank_plan.vblank_target_selected == 0 &&
                           vblank_plan.vblank_text_login == 1 &&
                           vblank_plan.vblank_text_login_fallback == 1 &&
                           vblank_plan.raw_secret_exposed == 0,
                       "credential window vblank plan unsafe flip should fail closed");
  fails += expect_true(
      strings_equal(vblank_plan.vblank_ticket,
                    "text-login-fallback-window-vblank-ticket") &&
          strings_equal(vblank_plan.event_type,
                        "credential-screen-window-vblank-plan-unsafe") &&
          strings_equal(vblank_plan.blocked_reason,
                        "credential-window-vblank-plan-unsafe"),
      "credential window vblank plan unsafe flip should report unsafe upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_flip_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'f', 0, 0, 0, 1,
          &flip_plan) == 0,
      "credential window vblank plan forged origin fixture should build");
  flip_plan.window_commit_plan_available = 0;
  flip_plan.window_surface_plan_safe = 0;
  flip_plan.window_flip_plan_safe = 1;
  fails += expect_true(
      login_window_credential_screen_window_vblank_plan_build(
          &flip_plan, &vblank_plan) == 0,
      "credential window vblank plan forged flip origin should build fallback");
  fails += expect_true(vblank_plan.window_flip_plan_available == 1 &&
                           vblank_plan.window_commit_plan_available == 0 &&
                           vblank_plan.window_surface_plan_safe == 0 &&
                           vblank_plan.window_flip_plan_safe == 1 &&
                           vblank_plan.window_vblank_plan_safe == 0 &&
                           vblank_plan.vblank_allowed == 0 &&
                           vblank_plan.vblank_ticket_selected == 0 &&
                           vblank_plan.vblank_text_login_fallback == 1 &&
                           vblank_plan.submit_enabled == 0 &&
                           vblank_plan.auth_attempt_allowed == 0,
                       "credential window vblank plan should reject forged flip origin");
  fails += expect_true(
      strings_equal(vblank_plan.vblank_ticket,
                    "text-login-fallback-window-vblank-ticket") &&
          strings_equal(vblank_plan.event_type,
                        "credential-screen-window-vblank-plan-unsafe") &&
          strings_equal(vblank_plan.blocked_reason,
                        "credential-window-vblank-plan-unsafe"),
      "credential window vblank plan forged origin should report unsafe upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_flip_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0, 1,
          &flip_plan) == 0,
      "credential window vblank plan submitted flip fixture should build");
  flip_plan.flip_submitted = 1;
  flip_plan.flip_buffer_attached = 1;
  flip_plan.flip_buffer_submitted = 1;
  flip_plan.flip_vblank_armed = 1;
  flip_plan.flip_vblank_submitted = 1;
  flip_plan.flip_event_armed = 1;
  flip_plan.flip_event_submitted = 1;
  flip_plan.flip_async_allowed = 1;
  flip_plan.flip_async_submitted = 1;
  flip_plan.commit_submitted = 1;
  flip_plan.blit_submitted = 1;
  flip_plan.output_submitted = 1;
  flip_plan.display_submitted = 1;
  flip_plan.scanout_submitted = 1;
  flip_plan.vsync_submitted = 1;
  flip_plan.schedule_submitted = 1;
  flip_plan.frame_timer_armed = 1;
  flip_plan.page_flip_submitted = 1;
  fails += expect_true(
      login_window_credential_screen_window_vblank_plan_build(
          &flip_plan, &vblank_plan) == 0,
      "credential window vblank plan submitted flip should build fallback");
  fails += expect_true(vblank_plan.window_vblank_plan_safe == 0 &&
                           vblank_plan.vblank_allowed == 0 &&
                           vblank_plan.vblank_submitted == 0 &&
                           vblank_plan.vblank_event_armed == 0 &&
                           vblank_plan.vblank_event_submitted == 0 &&
                           vblank_plan.vblank_callback_armed == 0 &&
                           vblank_plan.vblank_callback_submitted == 0 &&
                           vblank_plan.vblank_timestamp_captured == 0 &&
                           vblank_plan.vblank_timestamp_submitted == 0 &&
                           vblank_plan.vblank_frame_completed == 0 &&
                           vblank_plan.vblank_frame_submitted == 0 &&
                           vblank_plan.flip_submitted == 0 &&
                           vblank_plan.flip_buffer_attached == 0 &&
                           vblank_plan.flip_vblank_armed == 0 &&
                           vblank_plan.flip_async_submitted == 0 &&
                           vblank_plan.commit_submitted == 0 &&
                           vblank_plan.blit_submitted == 0 &&
                           vblank_plan.output_submitted == 0 &&
                           vblank_plan.display_submitted == 0 &&
                           vblank_plan.scanout_submitted == 0 &&
                           vblank_plan.vsync_submitted == 0 &&
                           vblank_plan.schedule_submitted == 0 &&
                           vblank_plan.frame_timer_armed == 0 &&
                           vblank_plan.page_flip_submitted == 0 &&
                           vblank_plan.submit_enabled == 0 &&
                           vblank_plan.auth_attempt_allowed == 0,
                       "credential window vblank plan must not copy unsafe submitted flip state");
  fails += expect_true(
      strings_equal(vblank_plan.vblank_ticket,
                    "text-login-fallback-window-vblank-ticket") &&
          strings_equal(vblank_plan.event_type,
                        "credential-screen-window-vblank-plan-unsafe") &&
          strings_equal(vblank_plan.blocked_reason,
                        "credential-window-vblank-plan-unsafe"),
      "credential window vblank plan submitted flip should report unsafe upstream");
  return fails;
}

int test_login_runtime_credential_window_vblank_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_window_vblank_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_window_vblank_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_window_vblank_plan_falls_back_for_submit_and_unknown_action();
  fails += test_loginwindow_credential_screen_window_vblank_plan_fails_closed_for_unsafe_or_missing_flip_plan();
  return fails;
}
