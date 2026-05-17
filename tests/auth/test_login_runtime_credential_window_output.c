/*
 * tests/auth/test_login_runtime_credential_window_output.c
 *
 * Credential screen window output plan coverage for the
 * `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-16 monolith
 * refactor (PR D.40 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_window_output_plan_build`: 4
 *     tests covering the credential widgets output + the
 *     text-route output (recovery + resume) + the submit/unknown
 *     fallback output + the missing-or-unsafe display plan
 *     fail-closed default.
 *
 * Also exposes shared helper
 * `build_loginwindow_credential_screen_window_output_plan_for_action`,
 * used by later companion files that chain on top of the output
 * stage (window_blit, ...).
 *
 * The companion entry `test_login_runtime_credential_window_output_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_window_output_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage,
    struct login_window_credential_screen_window_output_plan *output_plan) {
  struct login_window_credential_screen_window_display_plan display_plan;

  if (build_loginwindow_credential_screen_window_display_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &display_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_window_output_plan_build(
      &display_plan, output_plan);
}

static int test_loginwindow_credential_screen_window_output_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_window_output_plan output_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_output_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'p', 0, 0, 0, 1,
          &output_plan) == 0,
      "credential window output plan edit should build");
  fails += expect_true(
      output_plan.version ==
          LOGIN_WINDOW_CREDENTIAL_SCREEN_WINDOW_OUTPUT_PLAN_VERSION,
      "credential window output plan should expose stable version");
  fails += expect_true(output_plan.window_display_plan_available == 1 &&
                           output_plan.window_scanout_plan_available == 1 &&
                           output_plan.window_vsync_plan_available == 1 &&
                           output_plan.window_schedule_plan_available == 1 &&
                           output_plan.window_present_plan_available == 1 &&
                           output_plan.window_damage_plan_available == 1 &&
                           output_plan.window_compositor_plan_available == 1 &&
                           output_plan.window_surface_plan_available == 1 &&
                           output_plan.window_surface_plan_safe == 1 &&
                           output_plan.window_compositor_plan_safe == 1 &&
                           output_plan.window_damage_plan_safe == 1 &&
                           output_plan.window_present_plan_safe == 1 &&
                           output_plan.window_schedule_plan_safe == 1 &&
                           output_plan.window_vsync_plan_safe == 1 &&
                           output_plan.window_scanout_plan_safe == 1 &&
                           output_plan.window_display_plan_safe == 1 &&
                           output_plan.window_output_plan_safe == 1,
                       "credential window output plan should require safe display plan");
  fails += expect_true(output_plan.output_required == 1 &&
                           output_plan.output_allowed == 1 &&
                           output_plan.output_submitted == 0 &&
                           output_plan.output_ticket_selected == 1 &&
                           output_plan.output_target_selected == 1 &&
                           output_plan.output_connector_attached == 0 &&
                           output_plan.output_connector_submitted == 0 &&
                           output_plan.output_mode_attached == 0 &&
                           output_plan.output_mode_submitted == 0 &&
                           output_plan.output_signal_armed == 0 &&
                           output_plan.output_signal_submitted == 0 &&
                           output_plan.output_error == 0,
                       "credential window output plan should remain declarative");
  fails += expect_true(output_plan.damage_error == 0 &&
                           output_plan.present_error == 0 &&
                           output_plan.schedule_error == 0 &&
                           output_plan.vsync_error == 0 &&
                           output_plan.scanout_error == 0 &&
                           output_plan.display_error == 0,
                       "credential window output plan should preserve upstream plan success");
  fails += expect_true(output_plan.vsync_submitted == 0 &&
                           output_plan.vsync_wait_submitted == 0 &&
                           output_plan.vsync_fence_armed == 0 &&
                           output_plan.schedule_submitted == 0 &&
                           output_plan.frame_timer_armed == 0 &&
                           output_plan.compositor_wake_allowed == 0 &&
                           output_plan.compositor_wake_submitted == 0 &&
                           output_plan.page_flip_allowed == 0 &&
                           output_plan.page_flip_submitted == 0 &&
                           output_plan.present_submitted == 0 &&
                           output_plan.damage_submitted == 0 &&
                           output_plan.compositor_submitted == 0 &&
                           output_plan.surface_bound == 0 &&
                           output_plan.surface_memory_mapped == 0 &&
                           output_plan.surface_pixels_written == 0 &&
                           output_plan.window_created == 0 &&
                           output_plan.gui_submitted == 0 &&
                           output_plan.scanout_submitted == 0 &&
                           output_plan.scanout_buffer_attached == 0 &&
                           output_plan.scanout_display_flip_submitted == 0 &&
                           output_plan.display_submitted == 0 &&
                           output_plan.display_controller_attached == 0 &&
                           output_plan.display_output_attached == 0 &&
                           output_plan.display_pipeline_attached == 0,
                       "credential window output plan must not attach connector, mode or signal");
  fails += expect_true(output_plan.output_credential_panel == 1 &&
                           output_plan.output_credential_input == 1 &&
                           output_plan.output_credential_focus == 1 &&
                           output_plan.output_text_login == 0 &&
                           output_plan.output_text_login_fallback == 0,
                       "credential window output plan should mark credential widgets");
  fails += expect_true(output_plan.submit_callback_bound == 0 &&
                           output_plan.auth_callback_bound == 0 &&
                           output_plan.submit_enabled == 0 &&
                           output_plan.auth_attempt_allowed == 0 &&
                           output_plan.schedule_auth_submit_allowed == 0 &&
                           output_plan.schedule_auth_attempt_allowed == 0 &&
                           output_plan.raw_secret_exposed == 0 &&
                           output_plan.masked_text_exposed == 0,
                       "credential window output plan must stay redacted and auth-disabled");
  fails += expect_true(
      strings_equal(output_plan.output_ticket,
                    "credential-screen-window-output-ticket") &&
          strings_equal(output_plan.display_ticket,
                        "credential-screen-window-display-ticket") &&
          strings_equal(output_plan.output_policy,
                        "incremental-window-output-declarative") &&
          strings_equal(output_plan.state,
                        "window-output-credential-ready"),
      "credential window output plan should report output ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_output_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_window_output_plan output_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_output_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
          LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1, 1,
          &output_plan) == 0,
      "credential window output plan recovery should build");
  fails += expect_true(output_plan.window_output_plan_safe == 1 &&
                           output_plan.output_allowed == 1 &&
                           output_plan.output_submitted == 0 &&
                           output_plan.output_text_recovery == 1 &&
                           output_plan.output_text_login == 1 &&
                           output_plan.output_text_login_fallback == 0 &&
                           output_plan.output_credential_focus == 0,
                       "credential window output plan recovery should mark text recovery");
  fails += expect_true(output_plan.full_schedule_required == 1 &&
                           output_plan.schedule_incremental_allowed == 0 &&
                           output_plan.schedule_cache_allowed == 0 &&
                           output_plan.schedule_reuse_allowed == 0 &&
                           output_plan.input_focus_allowed == 0 &&
                           output_plan.vsync_fence_armed == 0 &&
                           output_plan.page_flip_submitted == 0 &&
                           output_plan.scanout_buffer_attached == 0 &&
                           output_plan.scanout_display_flip_submitted == 0 &&
                           output_plan.display_controller_attached == 0 &&
                           output_plan.display_output_submitted == 0 &&
                           output_plan.display_pipeline_submitted == 0 &&
                           output_plan.output_connector_attached == 0 &&
                           output_plan.output_mode_submitted == 0 &&
                           output_plan.output_signal_submitted == 0,
                       "credential window output plan recovery should require full declarative output");
  fails += expect_true(
      strings_equal(output_plan.output_ticket,
                    "text-recovery-window-output-ticket") &&
          strings_equal(output_plan.compositor_target,
                        "text-recovery-window-output") &&
          strings_equal(output_plan.blocked_reason, "text-recovery-only"),
      "credential window output plan recovery should report recovery ticket");

  fails += expect_true(
      build_loginwindow_credential_screen_window_output_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0, 1,
          &output_plan) == 0,
      "credential window output plan resume should build");
  fails += expect_true(output_plan.window_output_plan_safe == 1 &&
                           output_plan.output_text_login_resume == 1 &&
                           output_plan.session_reset_required == 1 &&
                           output_plan.login_screen_rerender_required == 1 &&
                           output_plan.full_schedule_required == 1 &&
                           output_plan.schedule_incremental_allowed == 0 &&
                           output_plan.schedule_cache_allowed == 0 &&
                           output_plan.schedule_reuse_allowed == 0,
                       "credential window output plan resume should require full rerender output");
  fails += expect_true(output_plan.output_submitted == 0 &&
                           output_plan.output_connector_attached == 0 &&
                           output_plan.output_mode_submitted == 0 &&
                           output_plan.output_signal_submitted == 0 &&
                           output_plan.display_controller_attached == 0 &&
                           output_plan.scanout_buffer_attached == 0 &&
                           output_plan.scanout_display_flip_submitted == 0 &&
                           output_plan.vsync_fence_armed == 0 &&
                           output_plan.page_flip_submitted == 0 &&
                           output_plan.submit_enabled == 0 &&
                           output_plan.auth_attempt_allowed == 0,
                       "credential window output plan resume must keep GUI auth disabled");
  fails += expect_true(
      strings_equal(output_plan.output_ticket,
                    "text-login-resume-window-output-ticket") &&
          strings_equal(output_plan.cache_policy,
                        "window-output-cache-bypassed-for-rerender") &&
          strings_equal(output_plan.state, "window-output-resume-ready"),
      "credential window output plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_output_plan_falls_back_for_submit_and_unknown_action(void) {
  int fails = 0;
  struct login_window_credential_screen_window_output_plan output_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_output_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_SUBMIT, 0, 0, 0, 0, 1,
          &output_plan) == 0,
      "credential window output plan submit should build");
  fails += expect_true(output_plan.window_output_plan_safe == 1 &&
                           output_plan.submit_requested == 1 &&
                           output_plan.submit_blocked == 1 &&
                           output_plan.action_allowed == 0 &&
                           output_plan.action_blocked == 1 &&
                           output_plan.input_focus_allowed == 0 &&
                           output_plan.output_text_login == 1 &&
                           output_plan.output_text_login_fallback == 1 &&
                           output_plan.output_submitted == 0 &&
                           output_plan.output_connector_attached == 0 &&
                           output_plan.output_signal_submitted == 0,
                       "credential window output plan submit should force text login");
  fails += expect_true(
      strings_equal(output_plan.output_ticket,
                    "text-login-fallback-window-output-ticket") &&
          strings_equal(output_plan.output_policy,
                        "fallback-window-output-declarative") &&
          strings_equal(output_plan.blocked_reason, "gui-submit-disabled"),
      "credential window output plan submit should report disabled GUI submit");

  fails += expect_true(
      build_loginwindow_credential_screen_window_output_plan_for_action(
          9876, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0, 1,
          &output_plan) == 0,
      "credential window output plan unknown action should build");
  fails += expect_true(output_plan.window_output_plan_safe == 1 &&
                           output_plan.action_allowed == 0 &&
                           output_plan.action_blocked == 1 &&
                           output_plan.input_focus_allowed == 0 &&
                           output_plan.output_text_login == 1 &&
                           output_plan.output_text_login_fallback == 1 &&
                           output_plan.output_submitted == 0 &&
                           output_plan.output_signal_submitted == 0,
                       "credential window output plan unknown action should force text login");
  fails += expect_true(
      strings_equal(output_plan.output_ticket,
                    "text-login-fallback-window-output-ticket") &&
          strings_equal(output_plan.compositor_target,
                        "text-login-fallback-window-output") &&
          strings_equal(output_plan.state,
                        "window-output-text-login-ready"),
      "credential window output plan unknown action should report fallback ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_output_plan_fails_closed_for_unsafe_or_missing_display_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_window_display_plan display_plan;
  struct login_window_credential_screen_window_output_plan output_plan;

  fails += expect_true(
      login_window_credential_screen_window_output_plan_build(
          NULL, &output_plan) == 0,
      "credential window output plan missing display should build fallback");
  fails += expect_true(output_plan.window_display_plan_available == 0 &&
                           output_plan.window_display_plan_safe == 0 &&
                           output_plan.window_output_plan_safe == 0 &&
                           output_plan.route_blocked == 1 &&
                           output_plan.output_allowed == 0 &&
                           output_plan.output_ticket_selected == 0 &&
                           output_plan.output_target_selected == 0 &&
                           output_plan.output_text_login == 1 &&
                           output_plan.output_text_login_fallback == 1 &&
                           output_plan.output_submitted == 0 &&
                           output_plan.output_connector_attached == 0 &&
                           output_plan.output_signal_submitted == 0,
                       "credential window output plan missing display should fail closed");
  fails += expect_true(
      strings_equal(output_plan.output_ticket,
                    "text-login-fallback-window-output-ticket") &&
          strings_equal(output_plan.event_type,
                        "credential-screen-window-output-plan-unavailable") &&
          strings_equal(output_plan.blocked_reason,
                        "window-display-plan-unavailable"),
      "credential window output plan missing display should report missing upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_display_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0, 0, 1,
          &display_plan) == 0,
      "credential window output plan unsafe display fixture should build");
  display_plan.window_display_plan_safe = 0;
  display_plan.raw_secret_exposed = 1;
  display_plan.submit_blocked = 0;
  fails += expect_true(
      login_window_credential_screen_window_output_plan_build(
          &display_plan, &output_plan) == 0,
      "credential window output plan unsafe display should build fallback");
  fails += expect_true(output_plan.window_display_plan_available == 1 &&
                           output_plan.window_display_plan_safe == 0 &&
                           output_plan.window_output_plan_safe == 0 &&
                           output_plan.route_blocked == 1 &&
                           output_plan.output_allowed == 0 &&
                           output_plan.output_ticket_selected == 0 &&
                           output_plan.output_target_selected == 0 &&
                           output_plan.output_text_login == 1 &&
                           output_plan.output_text_login_fallback == 1 &&
                           output_plan.raw_secret_exposed == 0,
                       "credential window output plan unsafe display should fail closed");
  fails += expect_true(
      strings_equal(output_plan.output_ticket,
                    "text-login-fallback-window-output-ticket") &&
          strings_equal(output_plan.event_type,
                        "credential-screen-window-output-plan-unsafe") &&
          strings_equal(output_plan.blocked_reason,
                        "credential-window-output-plan-unsafe"),
      "credential window output plan unsafe display should report unsafe upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_display_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'f', 0, 0, 0, 1,
          &display_plan) == 0,
      "credential window output plan forged origin fixture should build");
  display_plan.window_scanout_plan_available = 0;
  display_plan.window_surface_plan_safe = 0;
  display_plan.window_display_plan_safe = 1;
  fails += expect_true(
      login_window_credential_screen_window_output_plan_build(
          &display_plan, &output_plan) == 0,
      "credential window output plan forged display origin should build fallback");
  fails += expect_true(output_plan.window_display_plan_available == 1 &&
                           output_plan.window_scanout_plan_available == 0 &&
                           output_plan.window_surface_plan_safe == 0 &&
                           output_plan.window_display_plan_safe == 1 &&
                           output_plan.window_output_plan_safe == 0 &&
                           output_plan.output_allowed == 0 &&
                           output_plan.output_ticket_selected == 0 &&
                           output_plan.output_text_login_fallback == 1 &&
                           output_plan.submit_enabled == 0 &&
                           output_plan.auth_attempt_allowed == 0,
                       "credential window output plan should reject forged display origin");
  fails += expect_true(
      strings_equal(output_plan.output_ticket,
                    "text-login-fallback-window-output-ticket") &&
          strings_equal(output_plan.event_type,
                        "credential-screen-window-output-plan-unsafe") &&
          strings_equal(output_plan.blocked_reason,
                        "credential-window-output-plan-unsafe"),
      "credential window output plan forged origin should report unsafe upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_display_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0, 1,
          &display_plan) == 0,
      "credential window output plan submitted display fixture should build");
  display_plan.display_submitted = 1;
  display_plan.display_controller_attached = 1;
  display_plan.display_controller_submitted = 1;
  display_plan.display_output_attached = 1;
  display_plan.display_output_submitted = 1;
  display_plan.display_pipeline_attached = 1;
  display_plan.display_pipeline_submitted = 1;
  display_plan.scanout_submitted = 1;
  display_plan.vsync_submitted = 1;
  display_plan.schedule_submitted = 1;
  display_plan.frame_timer_armed = 1;
  display_plan.page_flip_submitted = 1;
  fails += expect_true(
      login_window_credential_screen_window_output_plan_build(
          &display_plan, &output_plan) == 0,
      "credential window output plan submitted display should build fallback");
  fails += expect_true(output_plan.window_output_plan_safe == 0 &&
                           output_plan.output_allowed == 0 &&
                           output_plan.output_submitted == 0 &&
                           output_plan.output_connector_attached == 0 &&
                           output_plan.output_mode_submitted == 0 &&
                           output_plan.output_signal_submitted == 0 &&
                           output_plan.display_submitted == 0 &&
                           output_plan.display_controller_attached == 0 &&
                           output_plan.display_output_submitted == 0 &&
                           output_plan.display_pipeline_submitted == 0 &&
                           output_plan.scanout_submitted == 0 &&
                           output_plan.vsync_submitted == 0 &&
                           output_plan.schedule_submitted == 0 &&
                           output_plan.frame_timer_armed == 0 &&
                           output_plan.page_flip_submitted == 0 &&
                           output_plan.submit_enabled == 0 &&
                           output_plan.auth_attempt_allowed == 0,
                       "credential window output plan must not copy unsafe submitted display state");
  fails += expect_true(
      strings_equal(output_plan.output_ticket,
                    "text-login-fallback-window-output-ticket") &&
          strings_equal(output_plan.event_type,
                        "credential-screen-window-output-plan-unsafe") &&
          strings_equal(output_plan.blocked_reason,
                        "credential-window-output-plan-unsafe"),
      "credential window output plan submitted display should report unsafe upstream");
  return fails;
}

int test_login_runtime_credential_window_output_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_window_output_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_window_output_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_window_output_plan_falls_back_for_submit_and_unknown_action();
  fails += test_loginwindow_credential_screen_window_output_plan_fails_closed_for_unsafe_or_missing_display_plan();
  return fails;
}
