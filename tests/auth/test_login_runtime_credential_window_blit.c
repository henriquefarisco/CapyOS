/*
 * tests/auth/test_login_runtime_credential_window_blit.c
 *
 * Credential screen window blit plan coverage for the
 * `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-16 monolith
 * refactor (PR D.41 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_window_blit_plan_build`: 4
 *     tests covering the credential widgets blit + the text-route
 *     blit (recovery + resume) + the submit/unknown fallback blit
 *     + the missing-or-unsafe output plan fail-closed default.
 *
 * Also exposes shared helper
 * `build_loginwindow_credential_screen_window_blit_plan_for_action`,
 * used by later companion files that chain on top of the blit
 * stage (window_commit, ...).
 *
 * The companion entry `test_login_runtime_credential_window_blit_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_window_blit_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage,
    struct login_window_credential_screen_window_blit_plan *blit_plan) {
  struct login_window_credential_screen_window_output_plan output_plan;

  if (build_loginwindow_credential_screen_window_output_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &output_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_window_blit_plan_build(
      &output_plan, blit_plan);
}

static int test_loginwindow_credential_screen_window_blit_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_window_blit_plan blit_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_blit_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'p', 0, 0, 0, 1,
          &blit_plan) == 0,
      "credential window blit plan edit should build");
  fails += expect_true(
      blit_plan.version ==
          LOGIN_WINDOW_CREDENTIAL_SCREEN_WINDOW_BLIT_PLAN_VERSION,
      "credential window blit plan should expose stable version");
  fails += expect_true(blit_plan.window_output_plan_available == 1 &&
                           blit_plan.window_display_plan_available == 1 &&
                           blit_plan.window_scanout_plan_available == 1 &&
                           blit_plan.window_vsync_plan_available == 1 &&
                           blit_plan.window_schedule_plan_available == 1 &&
                           blit_plan.window_present_plan_available == 1 &&
                           blit_plan.window_damage_plan_available == 1 &&
                           blit_plan.window_compositor_plan_available == 1 &&
                           blit_plan.window_surface_plan_available == 1 &&
                           blit_plan.window_surface_plan_safe == 1 &&
                           blit_plan.window_compositor_plan_safe == 1 &&
                           blit_plan.window_damage_plan_safe == 1 &&
                           blit_plan.window_present_plan_safe == 1 &&
                           blit_plan.window_schedule_plan_safe == 1 &&
                           blit_plan.window_vsync_plan_safe == 1 &&
                           blit_plan.window_scanout_plan_safe == 1 &&
                           blit_plan.window_display_plan_safe == 1 &&
                           blit_plan.window_output_plan_safe == 1 &&
                           blit_plan.window_blit_plan_safe == 1,
                       "credential window blit plan should require safe output plan");
  fails += expect_true(blit_plan.blit_required == 1 &&
                           blit_plan.blit_allowed == 1 &&
                           blit_plan.blit_submitted == 0 &&
                           blit_plan.blit_ticket_selected == 1 &&
                           blit_plan.blit_target_selected == 1 &&
                           blit_plan.blit_source_buffer_mapped == 0 &&
                           blit_plan.blit_destination_buffer_mapped == 0 &&
                           blit_plan.blit_pixels_copied == 0 &&
                           blit_plan.blit_dma_allowed == 0 &&
                           blit_plan.blit_dma_submitted == 0 &&
                           blit_plan.blit_error == 0,
                       "credential window blit plan should remain declarative");
  fails += expect_true(blit_plan.damage_error == 0 &&
                           blit_plan.present_error == 0 &&
                           blit_plan.schedule_error == 0 &&
                           blit_plan.vsync_error == 0 &&
                           blit_plan.scanout_error == 0 &&
                           blit_plan.display_error == 0 &&
                           blit_plan.output_error == 0,
                       "credential window blit plan should preserve upstream plan success");
  fails += expect_true(blit_plan.output_submitted == 0 &&
                           blit_plan.output_connector_attached == 0 &&
                           blit_plan.output_mode_attached == 0 &&
                           blit_plan.output_signal_armed == 0 &&
                           blit_plan.output_signal_submitted == 0 &&
                           blit_plan.display_submitted == 0 &&
                           blit_plan.display_controller_attached == 0 &&
                           blit_plan.display_output_attached == 0 &&
                           blit_plan.display_pipeline_attached == 0 &&
                           blit_plan.scanout_submitted == 0 &&
                           blit_plan.scanout_buffer_attached == 0 &&
                           blit_plan.scanout_display_flip_submitted == 0 &&
                           blit_plan.vsync_submitted == 0 &&
                           blit_plan.vsync_wait_submitted == 0 &&
                           blit_plan.vsync_fence_armed == 0 &&
                           blit_plan.schedule_submitted == 0 &&
                           blit_plan.frame_timer_armed == 0 &&
                           blit_plan.compositor_wake_allowed == 0 &&
                           blit_plan.compositor_wake_submitted == 0 &&
                           blit_plan.page_flip_allowed == 0 &&
                           blit_plan.page_flip_submitted == 0 &&
                           blit_plan.present_submitted == 0 &&
                           blit_plan.damage_submitted == 0 &&
                           blit_plan.compositor_submitted == 0 &&
                           blit_plan.surface_bound == 0 &&
                           blit_plan.surface_memory_mapped == 0 &&
                           blit_plan.surface_pixels_written == 0 &&
                           blit_plan.window_created == 0 &&
                           blit_plan.gui_submitted == 0,
                       "credential window blit plan must not copy any real upstream effect");
  fails += expect_true(blit_plan.blit_credential_panel == 1 &&
                           blit_plan.blit_credential_input == 1 &&
                           blit_plan.blit_credential_focus == 1 &&
                           blit_plan.blit_text_login == 0 &&
                           blit_plan.blit_text_login_fallback == 0,
                       "credential window blit plan should mark credential widgets");
  fails += expect_true(blit_plan.submit_callback_bound == 0 &&
                           blit_plan.auth_callback_bound == 0 &&
                           blit_plan.submit_enabled == 0 &&
                           blit_plan.auth_attempt_allowed == 0 &&
                           blit_plan.credential_redacted == 1 &&
                           blit_plan.length_redacted == 1 &&
                           blit_plan.raw_secret_exposed == 0 &&
                           blit_plan.masked_text_exposed == 0,
                       "credential window blit plan must stay redacted and auth-disabled");
  fails += expect_true(
      strings_equal(blit_plan.blit_ticket,
                    "credential-screen-window-blit-ticket") &&
          strings_equal(blit_plan.output_ticket,
                        "credential-screen-window-output-ticket") &&
          strings_equal(blit_plan.blit_policy,
                        "incremental-window-blit-declarative") &&
          strings_equal(blit_plan.state,
                        "window-blit-credential-ready"),
      "credential window blit plan should report blit ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_blit_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_window_blit_plan blit_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_blit_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
          LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1, 1,
          &blit_plan) == 0,
      "credential window blit plan recovery should build");
  fails += expect_true(blit_plan.window_blit_plan_safe == 1 &&
                           blit_plan.blit_allowed == 1 &&
                           blit_plan.blit_submitted == 0 &&
                           blit_plan.blit_text_recovery == 1 &&
                           blit_plan.blit_text_login == 1 &&
                           blit_plan.blit_text_login_fallback == 0 &&
                           blit_plan.blit_credential_focus == 0,
                       "credential window blit plan recovery should mark text recovery");
  fails += expect_true(blit_plan.full_schedule_required == 1 &&
                           blit_plan.schedule_incremental_allowed == 0 &&
                           blit_plan.schedule_cache_allowed == 0 &&
                           blit_plan.schedule_reuse_allowed == 0 &&
                           blit_plan.input_focus_allowed == 0 &&
                           blit_plan.blit_source_buffer_mapped == 0 &&
                           blit_plan.blit_destination_buffer_mapped == 0 &&
                           blit_plan.blit_pixels_copied == 0 &&
                           blit_plan.blit_dma_submitted == 0 &&
                           blit_plan.output_signal_submitted == 0 &&
                           blit_plan.display_controller_attached == 0 &&
                           blit_plan.scanout_buffer_attached == 0 &&
                           blit_plan.vsync_fence_armed == 0 &&
                           blit_plan.page_flip_submitted == 0,
                       "credential window blit plan recovery should require full declarative blit");
  fails += expect_true(
      strings_equal(blit_plan.blit_ticket,
                    "text-recovery-window-blit-ticket") &&
          strings_equal(blit_plan.compositor_target,
                        "text-recovery-window-blit") &&
          strings_equal(blit_plan.blocked_reason, "text-recovery-only"),
      "credential window blit plan recovery should report recovery ticket");

  fails += expect_true(
      build_loginwindow_credential_screen_window_blit_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0, 1,
          &blit_plan) == 0,
      "credential window blit plan resume should build");
  fails += expect_true(blit_plan.window_blit_plan_safe == 1 &&
                           blit_plan.blit_text_login_resume == 1 &&
                           blit_plan.session_reset_required == 1 &&
                           blit_plan.login_screen_rerender_required == 1 &&
                           blit_plan.full_schedule_required == 1 &&
                           blit_plan.schedule_incremental_allowed == 0 &&
                           blit_plan.schedule_cache_allowed == 0 &&
                           blit_plan.schedule_reuse_allowed == 0,
                       "credential window blit plan resume should require full rerender blit");
  fails += expect_true(blit_plan.blit_submitted == 0 &&
                           blit_plan.blit_source_buffer_mapped == 0 &&
                           blit_plan.blit_destination_buffer_mapped == 0 &&
                           blit_plan.blit_pixels_copied == 0 &&
                           blit_plan.blit_dma_allowed == 0 &&
                           blit_plan.blit_dma_submitted == 0 &&
                           blit_plan.output_signal_submitted == 0 &&
                           blit_plan.display_controller_attached == 0 &&
                           blit_plan.scanout_buffer_attached == 0 &&
                           blit_plan.vsync_fence_armed == 0 &&
                           blit_plan.page_flip_submitted == 0 &&
                           blit_plan.submit_enabled == 0 &&
                           blit_plan.auth_attempt_allowed == 0,
                       "credential window blit plan resume must keep GUI auth disabled");
  fails += expect_true(
      strings_equal(blit_plan.blit_ticket,
                    "text-login-resume-window-blit-ticket") &&
          strings_equal(blit_plan.cache_policy,
                        "window-blit-cache-bypassed-for-rerender") &&
          strings_equal(blit_plan.state, "window-blit-resume-ready"),
      "credential window blit plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_blit_plan_falls_back_for_submit_and_unknown_action(void) {
  int fails = 0;
  struct login_window_credential_screen_window_blit_plan blit_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_blit_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_SUBMIT, 0, 0, 0, 0, 1,
          &blit_plan) == 0,
      "credential window blit plan submit should build");
  fails += expect_true(blit_plan.window_blit_plan_safe == 1 &&
                           blit_plan.submit_requested == 1 &&
                           blit_plan.submit_blocked == 1 &&
                           blit_plan.action_allowed == 0 &&
                           blit_plan.action_blocked == 1 &&
                           blit_plan.input_focus_allowed == 0 &&
                           blit_plan.blit_text_login == 1 &&
                           blit_plan.blit_text_login_fallback == 1 &&
                           blit_plan.blit_submitted == 0 &&
                           blit_plan.blit_source_buffer_mapped == 0 &&
                           blit_plan.blit_dma_submitted == 0,
                       "credential window blit plan submit should force text login");
  fails += expect_true(
      strings_equal(blit_plan.blit_ticket,
                    "text-login-fallback-window-blit-ticket") &&
          strings_equal(blit_plan.blit_policy,
                        "fallback-window-blit-declarative") &&
          strings_equal(blit_plan.blocked_reason, "gui-submit-disabled"),
      "credential window blit plan submit should report disabled GUI submit");

  fails += expect_true(
      build_loginwindow_credential_screen_window_blit_plan_for_action(
          9876, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0, 1,
          &blit_plan) == 0,
      "credential window blit plan unknown action should build");
  fails += expect_true(blit_plan.window_blit_plan_safe == 1 &&
                           blit_plan.action_allowed == 0 &&
                           blit_plan.action_blocked == 1 &&
                           blit_plan.input_focus_allowed == 0 &&
                           blit_plan.blit_text_login == 1 &&
                           blit_plan.blit_text_login_fallback == 1 &&
                           blit_plan.blit_submitted == 0 &&
                           blit_plan.blit_dma_submitted == 0,
                       "credential window blit plan unknown action should force text login");
  fails += expect_true(
      strings_equal(blit_plan.blit_ticket,
                    "text-login-fallback-window-blit-ticket") &&
          strings_equal(blit_plan.compositor_target,
                        "text-login-fallback-window-blit") &&
          strings_equal(blit_plan.state,
                        "window-blit-text-login-ready"),
      "credential window blit plan unknown action should report fallback ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_blit_plan_fails_closed_for_unsafe_or_missing_output_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_window_output_plan output_plan;
  struct login_window_credential_screen_window_blit_plan blit_plan;

  fails += expect_true(
      login_window_credential_screen_window_blit_plan_build(
          NULL, &blit_plan) == 0,
      "credential window blit plan missing output should build fallback");
  fails += expect_true(blit_plan.window_output_plan_available == 0 &&
                           blit_plan.window_output_plan_safe == 0 &&
                           blit_plan.window_blit_plan_safe == 0 &&
                           blit_plan.route_blocked == 1 &&
                           blit_plan.blit_allowed == 0 &&
                           blit_plan.blit_ticket_selected == 0 &&
                           blit_plan.blit_target_selected == 0 &&
                           blit_plan.blit_text_login == 1 &&
                           blit_plan.blit_text_login_fallback == 1 &&
                           blit_plan.blit_submitted == 0 &&
                           blit_plan.blit_dma_submitted == 0,
                       "credential window blit plan missing output should fail closed");
  fails += expect_true(
      strings_equal(blit_plan.blit_ticket,
                    "text-login-fallback-window-blit-ticket") &&
          strings_equal(blit_plan.event_type,
                        "credential-screen-window-blit-plan-unavailable") &&
          strings_equal(blit_plan.blocked_reason,
                        "window-output-plan-unavailable"),
      "credential window blit plan missing output should report missing upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_output_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0, 0, 1,
          &output_plan) == 0,
      "credential window blit plan unsafe output fixture should build");
  output_plan.window_output_plan_safe = 0;
  output_plan.raw_secret_exposed = 1;
  output_plan.submit_blocked = 0;
  fails += expect_true(
      login_window_credential_screen_window_blit_plan_build(
          &output_plan, &blit_plan) == 0,
      "credential window blit plan unsafe output should build fallback");
  fails += expect_true(blit_plan.window_output_plan_available == 1 &&
                           blit_plan.window_output_plan_safe == 0 &&
                           blit_plan.window_blit_plan_safe == 0 &&
                           blit_plan.route_blocked == 1 &&
                           blit_plan.blit_allowed == 0 &&
                           blit_plan.blit_ticket_selected == 0 &&
                           blit_plan.blit_target_selected == 0 &&
                           blit_plan.blit_text_login == 1 &&
                           blit_plan.blit_text_login_fallback == 1 &&
                           blit_plan.raw_secret_exposed == 0,
                       "credential window blit plan unsafe output should fail closed");
  fails += expect_true(
      strings_equal(blit_plan.blit_ticket,
                    "text-login-fallback-window-blit-ticket") &&
          strings_equal(blit_plan.event_type,
                        "credential-screen-window-blit-plan-unsafe") &&
          strings_equal(blit_plan.blocked_reason,
                        "credential-window-blit-plan-unsafe"),
      "credential window blit plan unsafe output should report unsafe upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_output_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'f', 0, 0, 0, 1,
          &output_plan) == 0,
      "credential window blit plan forged origin fixture should build");
  output_plan.window_display_plan_available = 0;
  output_plan.window_surface_plan_safe = 0;
  output_plan.window_output_plan_safe = 1;
  fails += expect_true(
      login_window_credential_screen_window_blit_plan_build(
          &output_plan, &blit_plan) == 0,
      "credential window blit plan forged output origin should build fallback");
  fails += expect_true(blit_plan.window_output_plan_available == 1 &&
                           blit_plan.window_display_plan_available == 0 &&
                           blit_plan.window_surface_plan_safe == 0 &&
                           blit_plan.window_output_plan_safe == 1 &&
                           blit_plan.window_blit_plan_safe == 0 &&
                           blit_plan.blit_allowed == 0 &&
                           blit_plan.blit_ticket_selected == 0 &&
                           blit_plan.blit_text_login_fallback == 1 &&
                           blit_plan.submit_enabled == 0 &&
                           blit_plan.auth_attempt_allowed == 0,
                       "credential window blit plan should reject forged output origin");
  fails += expect_true(
      strings_equal(blit_plan.blit_ticket,
                    "text-login-fallback-window-blit-ticket") &&
          strings_equal(blit_plan.event_type,
                        "credential-screen-window-blit-plan-unsafe") &&
          strings_equal(blit_plan.blocked_reason,
                        "credential-window-blit-plan-unsafe"),
      "credential window blit plan forged origin should report unsafe upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_output_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0, 1,
          &output_plan) == 0,
      "credential window blit plan submitted output fixture should build");
  output_plan.output_submitted = 1;
  output_plan.output_connector_attached = 1;
  output_plan.output_mode_attached = 1;
  output_plan.output_mode_submitted = 1;
  output_plan.output_signal_armed = 1;
  output_plan.output_signal_submitted = 1;
  output_plan.display_submitted = 1;
  output_plan.display_controller_attached = 1;
  output_plan.scanout_submitted = 1;
  output_plan.vsync_submitted = 1;
  output_plan.schedule_submitted = 1;
  output_plan.frame_timer_armed = 1;
  output_plan.page_flip_submitted = 1;
  fails += expect_true(
      login_window_credential_screen_window_blit_plan_build(
          &output_plan, &blit_plan) == 0,
      "credential window blit plan submitted output should build fallback");
  fails += expect_true(blit_plan.window_blit_plan_safe == 0 &&
                           blit_plan.blit_allowed == 0 &&
                           blit_plan.blit_submitted == 0 &&
                           blit_plan.blit_source_buffer_mapped == 0 &&
                           blit_plan.blit_destination_buffer_mapped == 0 &&
                           blit_plan.blit_pixels_copied == 0 &&
                           blit_plan.blit_dma_allowed == 0 &&
                           blit_plan.blit_dma_submitted == 0 &&
                           blit_plan.output_submitted == 0 &&
                           blit_plan.output_connector_attached == 0 &&
                           blit_plan.output_mode_submitted == 0 &&
                           blit_plan.output_signal_submitted == 0 &&
                           blit_plan.display_submitted == 0 &&
                           blit_plan.display_controller_attached == 0 &&
                           blit_plan.scanout_submitted == 0 &&
                           blit_plan.vsync_submitted == 0 &&
                           blit_plan.schedule_submitted == 0 &&
                           blit_plan.frame_timer_armed == 0 &&
                           blit_plan.page_flip_submitted == 0 &&
                           blit_plan.submit_enabled == 0 &&
                           blit_plan.auth_attempt_allowed == 0,
                       "credential window blit plan must not copy unsafe submitted output state");
  fails += expect_true(
      strings_equal(blit_plan.blit_ticket,
                    "text-login-fallback-window-blit-ticket") &&
          strings_equal(blit_plan.event_type,
                        "credential-screen-window-blit-plan-unsafe") &&
          strings_equal(blit_plan.blocked_reason,
                        "credential-window-blit-plan-unsafe"),
      "credential window blit plan submitted output should report unsafe upstream");
  return fails;
}

int test_login_runtime_credential_window_blit_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_window_blit_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_window_blit_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_window_blit_plan_falls_back_for_submit_and_unknown_action();
  fails += test_loginwindow_credential_screen_window_blit_plan_fails_closed_for_unsafe_or_missing_output_plan();
  return fails;
}
