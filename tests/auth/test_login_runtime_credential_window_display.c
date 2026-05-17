/*
 * tests/auth/test_login_runtime_credential_window_display.c
 *
 * Credential screen window display plan coverage for the
 * `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-16 monolith
 * refactor (PR D.39 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_window_display_plan_build`: 4
 *     tests covering the credential widgets display + the
 *     text-route display (recovery + resume) + the submit/unknown
 *     fallback display + the missing-or-unsafe scanout plan
 *     fail-closed default.
 *
 * Also exposes shared helper
 * `build_loginwindow_credential_screen_window_display_plan_for_action`,
 * used by later companion files that chain on top of the display
 * stage (window_output, ...).
 *
 * The companion entry `test_login_runtime_credential_window_display_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_window_display_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage,
    struct login_window_credential_screen_window_display_plan *display_plan) {
  struct login_window_credential_screen_window_scanout_plan scanout_plan;

  if (build_loginwindow_credential_screen_window_scanout_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &scanout_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_window_display_plan_build(
      &scanout_plan, display_plan);
}

static int test_loginwindow_credential_screen_window_display_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_window_display_plan display_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_display_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'p', 0, 0, 0, 1,
          &display_plan) == 0,
      "credential window display plan edit should build");
  fails += expect_true(
      display_plan.version ==
          LOGIN_WINDOW_CREDENTIAL_SCREEN_WINDOW_DISPLAY_PLAN_VERSION,
      "credential window display plan should expose stable version");
  fails += expect_true(display_plan.window_scanout_plan_available == 1 &&
                           display_plan.window_vsync_plan_available == 1 &&
                           display_plan.window_schedule_plan_available == 1 &&
                           display_plan.window_present_plan_available == 1 &&
                           display_plan.window_damage_plan_available == 1 &&
                           display_plan.window_compositor_plan_available == 1 &&
                           display_plan.window_surface_plan_available == 1 &&
                           display_plan.window_surface_plan_safe == 1 &&
                           display_plan.window_compositor_plan_safe == 1 &&
                           display_plan.window_damage_plan_safe == 1 &&
                           display_plan.window_present_plan_safe == 1 &&
                           display_plan.window_schedule_plan_safe == 1 &&
                           display_plan.window_vsync_plan_safe == 1 &&
                           display_plan.window_scanout_plan_safe == 1 &&
                           display_plan.window_display_plan_safe == 1,
                       "credential window display plan should require safe scanout plan");
  fails += expect_true(display_plan.display_required == 1 &&
                           display_plan.display_allowed == 1 &&
                           display_plan.display_submitted == 0 &&
                           display_plan.display_ticket_selected == 1 &&
                           display_plan.display_target_selected == 1 &&
                           display_plan.display_controller_attached == 0 &&
                           display_plan.display_controller_submitted == 0 &&
                           display_plan.display_output_attached == 0 &&
                           display_plan.display_output_submitted == 0 &&
                           display_plan.display_pipeline_attached == 0 &&
                           display_plan.display_pipeline_submitted == 0 &&
                           display_plan.display_error == 0,
                       "credential window display plan should remain declarative");
  fails += expect_true(display_plan.damage_error == 0 &&
                           display_plan.present_error == 0 &&
                           display_plan.schedule_error == 0 &&
                           display_plan.vsync_error == 0 &&
                           display_plan.scanout_error == 0,
                       "credential window display plan should preserve upstream plan success");
  fails += expect_true(display_plan.vsync_submitted == 0 &&
                           display_plan.vsync_wait_submitted == 0 &&
                           display_plan.vsync_fence_armed == 0 &&
                           display_plan.schedule_submitted == 0 &&
                           display_plan.frame_timer_armed == 0 &&
                           display_plan.compositor_wake_allowed == 0 &&
                           display_plan.compositor_wake_submitted == 0 &&
                           display_plan.page_flip_allowed == 0 &&
                           display_plan.page_flip_submitted == 0 &&
                           display_plan.present_submitted == 0 &&
                           display_plan.damage_submitted == 0 &&
                           display_plan.compositor_submitted == 0 &&
                           display_plan.surface_bound == 0 &&
                           display_plan.surface_memory_mapped == 0 &&
                           display_plan.surface_pixels_written == 0 &&
                           display_plan.window_created == 0 &&
                           display_plan.gui_submitted == 0 &&
                           display_plan.scanout_submitted == 0 &&
                           display_plan.scanout_buffer_attached == 0 &&
                           display_plan.scanout_display_flip_submitted == 0,
                       "credential window display plan must not attach controller, output or pipeline");
  fails += expect_true(display_plan.display_credential_panel == 1 &&
                           display_plan.display_credential_input == 1 &&
                           display_plan.display_credential_focus == 1 &&
                           display_plan.display_text_login == 0 &&
                           display_plan.display_text_login_fallback == 0,
                       "credential window display plan should mark credential widgets");
  fails += expect_true(display_plan.submit_callback_bound == 0 &&
                           display_plan.auth_callback_bound == 0 &&
                           display_plan.submit_enabled == 0 &&
                           display_plan.auth_attempt_allowed == 0 &&
                           display_plan.schedule_auth_submit_allowed == 0 &&
                           display_plan.schedule_auth_attempt_allowed == 0 &&
                           display_plan.raw_secret_exposed == 0 &&
                           display_plan.masked_text_exposed == 0,
                       "credential window display plan must stay redacted and auth-disabled");
  fails += expect_true(
      strings_equal(display_plan.display_ticket,
                    "credential-screen-window-display-ticket") &&
          strings_equal(display_plan.scanout_ticket,
                        "credential-screen-window-scanout-ticket") &&
          strings_equal(display_plan.display_policy,
                        "incremental-window-display-declarative") &&
          strings_equal(display_plan.state,
                        "window-display-credential-ready"),
      "credential window display plan should report display ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_display_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_window_display_plan display_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_display_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
          LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1, 1,
          &display_plan) == 0,
      "credential window display plan recovery should build");
  fails += expect_true(display_plan.window_display_plan_safe == 1 &&
                           display_plan.display_allowed == 1 &&
                           display_plan.display_submitted == 0 &&
                           display_plan.display_text_recovery == 1 &&
                           display_plan.display_text_login == 1 &&
                           display_plan.display_text_login_fallback == 0 &&
                           display_plan.display_credential_focus == 0,
                       "credential window display plan recovery should mark text recovery");
  fails += expect_true(display_plan.full_schedule_required == 1 &&
                           display_plan.schedule_incremental_allowed == 0 &&
                           display_plan.schedule_cache_allowed == 0 &&
                           display_plan.schedule_reuse_allowed == 0 &&
                           display_plan.input_focus_allowed == 0 &&
                           display_plan.vsync_fence_armed == 0 &&
                           display_plan.page_flip_submitted == 0 &&
                           display_plan.scanout_buffer_attached == 0 &&
                           display_plan.scanout_display_flip_submitted == 0 &&
                           display_plan.display_controller_attached == 0 &&
                           display_plan.display_output_submitted == 0 &&
                           display_plan.display_pipeline_submitted == 0,
                       "credential window display plan recovery should require full declarative display");
  fails += expect_true(
      strings_equal(display_plan.display_ticket,
                    "text-recovery-window-display-ticket") &&
          strings_equal(display_plan.compositor_target,
                        "text-recovery-window-display") &&
          strings_equal(display_plan.blocked_reason, "text-recovery-only"),
      "credential window display plan recovery should report recovery ticket");

  fails += expect_true(
      build_loginwindow_credential_screen_window_display_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0, 1,
          &display_plan) == 0,
      "credential window display plan resume should build");
  fails += expect_true(display_plan.window_display_plan_safe == 1 &&
                           display_plan.display_text_login_resume == 1 &&
                           display_plan.session_reset_required == 1 &&
                           display_plan.login_screen_rerender_required == 1 &&
                           display_plan.full_schedule_required == 1 &&
                           display_plan.schedule_incremental_allowed == 0 &&
                           display_plan.schedule_cache_allowed == 0 &&
                           display_plan.schedule_reuse_allowed == 0,
                       "credential window display plan resume should require full rerender display");
  fails += expect_true(display_plan.display_submitted == 0 &&
                           display_plan.display_controller_attached == 0 &&
                           display_plan.display_output_submitted == 0 &&
                           display_plan.display_pipeline_submitted == 0 &&
                           display_plan.scanout_buffer_attached == 0 &&
                           display_plan.scanout_display_flip_submitted == 0 &&
                           display_plan.vsync_fence_armed == 0 &&
                           display_plan.page_flip_submitted == 0 &&
                           display_plan.submit_enabled == 0 &&
                           display_plan.auth_attempt_allowed == 0,
                       "credential window display plan resume must keep GUI auth disabled");
  fails += expect_true(
      strings_equal(display_plan.display_ticket,
                    "text-login-resume-window-display-ticket") &&
          strings_equal(display_plan.cache_policy,
                        "window-display-cache-bypassed-for-rerender") &&
          strings_equal(display_plan.state, "window-display-resume-ready"),
      "credential window display plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_display_plan_falls_back_for_submit_and_unknown_action(void) {
  int fails = 0;
  struct login_window_credential_screen_window_display_plan display_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_display_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_SUBMIT, 0, 0, 0, 0, 1,
          &display_plan) == 0,
      "credential window display plan submit should build");
  fails += expect_true(display_plan.window_display_plan_safe == 1 &&
                           display_plan.submit_requested == 1 &&
                           display_plan.submit_blocked == 1 &&
                           display_plan.action_allowed == 0 &&
                           display_plan.action_blocked == 1 &&
                           display_plan.input_focus_allowed == 0 &&
                           display_plan.display_text_login == 1 &&
                           display_plan.display_text_login_fallback == 1 &&
                           display_plan.display_submitted == 0 &&
                           display_plan.display_controller_attached == 0 &&
                           display_plan.display_pipeline_submitted == 0,
                       "credential window display plan submit should force text login");
  fails += expect_true(
      strings_equal(display_plan.display_ticket,
                    "text-login-fallback-window-display-ticket") &&
          strings_equal(display_plan.display_policy,
                        "fallback-window-display-declarative") &&
          strings_equal(display_plan.blocked_reason, "gui-submit-disabled"),
      "credential window display plan submit should report disabled GUI submit");

  fails += expect_true(
      build_loginwindow_credential_screen_window_display_plan_for_action(
          9876, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0, 1,
          &display_plan) == 0,
      "credential window display plan unknown action should build");
  fails += expect_true(display_plan.window_display_plan_safe == 1 &&
                           display_plan.action_allowed == 0 &&
                           display_plan.action_blocked == 1 &&
                           display_plan.input_focus_allowed == 0 &&
                           display_plan.display_text_login == 1 &&
                           display_plan.display_text_login_fallback == 1 &&
                           display_plan.display_submitted == 0 &&
                           display_plan.display_pipeline_submitted == 0,
                       "credential window display plan unknown action should force text login");
  fails += expect_true(
      strings_equal(display_plan.display_ticket,
                    "text-login-fallback-window-display-ticket") &&
          strings_equal(display_plan.compositor_target,
                        "text-login-fallback-window-display") &&
          strings_equal(display_plan.state,
                        "window-display-text-login-ready"),
      "credential window display plan unknown action should report fallback ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_display_plan_fails_closed_for_unsafe_or_missing_scanout_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_window_scanout_plan scanout_plan;
  struct login_window_credential_screen_window_display_plan display_plan;

  fails += expect_true(
      login_window_credential_screen_window_display_plan_build(
          NULL, &display_plan) == 0,
      "credential window display plan missing scanout should build fallback");
  fails += expect_true(display_plan.window_scanout_plan_available == 0 &&
                           display_plan.window_scanout_plan_safe == 0 &&
                           display_plan.window_display_plan_safe == 0 &&
                           display_plan.route_blocked == 1 &&
                           display_plan.display_allowed == 0 &&
                           display_plan.display_ticket_selected == 0 &&
                           display_plan.display_target_selected == 0 &&
                           display_plan.display_text_login == 1 &&
                           display_plan.display_text_login_fallback == 1 &&
                           display_plan.display_submitted == 0 &&
                           display_plan.display_controller_attached == 0 &&
                           display_plan.display_pipeline_submitted == 0,
                       "credential window display plan missing scanout should fail closed");
  fails += expect_true(
      strings_equal(display_plan.display_ticket,
                    "text-login-fallback-window-display-ticket") &&
          strings_equal(display_plan.event_type,
                        "credential-screen-window-display-plan-unavailable") &&
          strings_equal(display_plan.blocked_reason,
                        "window-scanout-plan-unavailable"),
      "credential window display plan missing scanout should report missing upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_scanout_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0, 0, 1,
          &scanout_plan) == 0,
      "credential window display plan unsafe scanout fixture should build");
  scanout_plan.window_scanout_plan_safe = 0;
  scanout_plan.raw_secret_exposed = 1;
  scanout_plan.submit_blocked = 0;
  fails += expect_true(
      login_window_credential_screen_window_display_plan_build(
          &scanout_plan, &display_plan) == 0,
      "credential window display plan unsafe scanout should build fallback");
  fails += expect_true(display_plan.window_scanout_plan_available == 1 &&
                           display_plan.window_scanout_plan_safe == 0 &&
                           display_plan.window_display_plan_safe == 0 &&
                           display_plan.route_blocked == 1 &&
                           display_plan.display_allowed == 0 &&
                           display_plan.display_ticket_selected == 0 &&
                           display_plan.display_target_selected == 0 &&
                           display_plan.display_text_login == 1 &&
                           display_plan.display_text_login_fallback == 1 &&
                           display_plan.raw_secret_exposed == 0,
                       "credential window display plan unsafe scanout should fail closed");
  fails += expect_true(
      strings_equal(display_plan.display_ticket,
                    "text-login-fallback-window-display-ticket") &&
          strings_equal(display_plan.event_type,
                        "credential-screen-window-display-plan-unsafe") &&
          strings_equal(display_plan.blocked_reason,
                        "credential-window-display-plan-unsafe"),
      "credential window display plan unsafe scanout should report unsafe upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_scanout_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'f', 0, 0, 0, 1,
          &scanout_plan) == 0,
      "credential window display plan forged origin fixture should build");
  scanout_plan.window_vsync_plan_available = 0;
  scanout_plan.window_surface_plan_safe = 0;
  scanout_plan.window_scanout_plan_safe = 1;
  fails += expect_true(
      login_window_credential_screen_window_display_plan_build(
          &scanout_plan, &display_plan) == 0,
      "credential window display plan forged scanout origin should build fallback");
  fails += expect_true(display_plan.window_scanout_plan_available == 1 &&
                           display_plan.window_vsync_plan_available == 0 &&
                           display_plan.window_surface_plan_safe == 0 &&
                           display_plan.window_scanout_plan_safe == 1 &&
                           display_plan.window_display_plan_safe == 0 &&
                           display_plan.display_allowed == 0 &&
                           display_plan.display_ticket_selected == 0 &&
                           display_plan.display_text_login_fallback == 1 &&
                           display_plan.submit_enabled == 0 &&
                           display_plan.auth_attempt_allowed == 0,
                       "credential window display plan should reject forged scanout origin");
  fails += expect_true(
      strings_equal(display_plan.display_ticket,
                    "text-login-fallback-window-display-ticket") &&
          strings_equal(display_plan.event_type,
                        "credential-screen-window-display-plan-unsafe") &&
          strings_equal(display_plan.blocked_reason,
                        "credential-window-display-plan-unsafe"),
      "credential window display plan forged origin should report unsafe upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_scanout_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0, 1,
          &scanout_plan) == 0,
      "credential window display plan submitted scanout fixture should build");
  scanout_plan.scanout_submitted = 1;
  scanout_plan.scanout_buffer_attached = 1;
  scanout_plan.scanout_buffer_submitted = 1;
  scanout_plan.scanout_display_flip_allowed = 1;
  scanout_plan.scanout_display_flip_submitted = 1;
  scanout_plan.vsync_submitted = 1;
  scanout_plan.schedule_submitted = 1;
  scanout_plan.frame_timer_armed = 1;
  scanout_plan.page_flip_submitted = 1;
  fails += expect_true(
      login_window_credential_screen_window_display_plan_build(
          &scanout_plan, &display_plan) == 0,
      "credential window display plan submitted scanout should build fallback");
  fails += expect_true(display_plan.window_display_plan_safe == 0 &&
                           display_plan.display_allowed == 0 &&
                           display_plan.display_submitted == 0 &&
                           display_plan.display_controller_attached == 0 &&
                           display_plan.display_output_submitted == 0 &&
                           display_plan.display_pipeline_submitted == 0 &&
                           display_plan.scanout_submitted == 0 &&
                           display_plan.scanout_buffer_attached == 0 &&
                           display_plan.scanout_display_flip_submitted == 0 &&
                           display_plan.vsync_submitted == 0 &&
                           display_plan.schedule_submitted == 0 &&
                           display_plan.frame_timer_armed == 0 &&
                           display_plan.page_flip_submitted == 0 &&
                           display_plan.submit_enabled == 0 &&
                           display_plan.auth_attempt_allowed == 0,
                       "credential window display plan must not copy unsafe submitted scanout state");
  fails += expect_true(
      strings_equal(display_plan.display_ticket,
                    "text-login-fallback-window-display-ticket") &&
          strings_equal(display_plan.event_type,
                        "credential-screen-window-display-plan-unsafe") &&
          strings_equal(display_plan.blocked_reason,
                        "credential-window-display-plan-unsafe"),
      "credential window display plan submitted scanout should report unsafe upstream");
  return fails;
}

int test_login_runtime_credential_window_display_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_window_display_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_window_display_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_window_display_plan_falls_back_for_submit_and_unknown_action();
  fails += test_loginwindow_credential_screen_window_display_plan_fails_closed_for_unsafe_or_missing_scanout_plan();
  return fails;
}
