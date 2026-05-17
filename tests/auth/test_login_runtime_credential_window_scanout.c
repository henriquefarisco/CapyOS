/*
 * tests/auth/test_login_runtime_credential_window_scanout.c
 *
 * Credential screen window scanout plan coverage for the
 * `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-16 monolith
 * refactor (PR D.38 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_window_scanout_plan_build`: 4
 *     tests covering the credential widgets scanout + the
 *     text-route scanout (recovery + resume) + the submit/unknown
 *     fallback scanout + the missing-or-unsafe vsync plan
 *     fail-closed default.
 *
 * Also exposes shared helper
 * `build_loginwindow_credential_screen_window_scanout_plan_for_action`,
 * used by later companion files that chain on top of the scanout
 * stage (window_display, ...).
 *
 * Split independently from `window_vsync` (PR D.37) because the
 * combined block exceeded the 900-line layout limit.
 *
 * The companion entry `test_login_runtime_credential_window_scanout_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_window_scanout_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage,
    struct login_window_credential_screen_window_scanout_plan *scanout_plan) {
  struct login_window_credential_screen_window_vsync_plan vsync_plan;

  if (build_loginwindow_credential_screen_window_vsync_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &vsync_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_window_scanout_plan_build(
      &vsync_plan, scanout_plan);
}

static int test_loginwindow_credential_screen_window_scanout_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_window_scanout_plan scanout_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_scanout_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'p', 0, 0, 0, 1,
          &scanout_plan) == 0,
      "credential window scanout plan edit should build");
  fails += expect_true(
      scanout_plan.version ==
          LOGIN_WINDOW_CREDENTIAL_SCREEN_WINDOW_SCANOUT_PLAN_VERSION,
      "credential window scanout plan should expose stable version");
  fails += expect_true(scanout_plan.window_vsync_plan_available == 1 &&
                           scanout_plan.window_schedule_plan_available == 1 &&
                           scanout_plan.window_present_plan_available == 1 &&
                           scanout_plan.window_damage_plan_available == 1 &&
                           scanout_plan.window_compositor_plan_available == 1 &&
                           scanout_plan.window_surface_plan_available == 1 &&
                           scanout_plan.window_surface_plan_safe == 1 &&
                           scanout_plan.window_compositor_plan_safe == 1 &&
                           scanout_plan.window_damage_plan_safe == 1 &&
                           scanout_plan.window_present_plan_safe == 1 &&
                           scanout_plan.window_schedule_plan_safe == 1 &&
                           scanout_plan.window_vsync_plan_safe == 1 &&
                           scanout_plan.window_scanout_plan_safe == 1,
                       "credential window scanout plan should require safe vsync plan");
  fails += expect_true(scanout_plan.scanout_required == 1 &&
                           scanout_plan.scanout_allowed == 1 &&
                           scanout_plan.scanout_submitted == 0 &&
                           scanout_plan.scanout_ticket_selected == 1 &&
                           scanout_plan.scanout_target_selected == 1 &&
                           scanout_plan.scanout_buffer_attached == 0 &&
                           scanout_plan.scanout_buffer_submitted == 0 &&
                           scanout_plan.scanout_display_flip_allowed == 0 &&
                           scanout_plan.scanout_display_flip_submitted == 0 &&
                           scanout_plan.scanout_error == 0,
                       "credential window scanout plan should remain declarative");
  fails += expect_true(scanout_plan.damage_error == 0 &&
                           scanout_plan.present_error == 0 &&
                           scanout_plan.schedule_error == 0 &&
                           scanout_plan.vsync_error == 0,
                       "credential window scanout plan should preserve upstream plan success");
  fails += expect_true(scanout_plan.vsync_submitted == 0 &&
                           scanout_plan.vsync_wait_submitted == 0 &&
                           scanout_plan.vsync_fence_armed == 0 &&
                           scanout_plan.schedule_submitted == 0 &&
                           scanout_plan.frame_timer_armed == 0 &&
                           scanout_plan.compositor_wake_allowed == 0 &&
                           scanout_plan.compositor_wake_submitted == 0 &&
                           scanout_plan.page_flip_allowed == 0 &&
                           scanout_plan.page_flip_submitted == 0 &&
                           scanout_plan.present_submitted == 0 &&
                           scanout_plan.damage_submitted == 0 &&
                           scanout_plan.compositor_submitted == 0 &&
                           scanout_plan.surface_bound == 0 &&
                           scanout_plan.surface_memory_mapped == 0 &&
                           scanout_plan.surface_pixels_written == 0 &&
                           scanout_plan.window_created == 0 &&
                           scanout_plan.gui_submitted == 0,
                       "credential window scanout plan must not arm display flip or buffer submit");
  fails += expect_true(scanout_plan.scanout_credential_panel == 1 &&
                           scanout_plan.scanout_credential_input == 1 &&
                           scanout_plan.scanout_credential_focus == 1 &&
                           scanout_plan.scanout_text_login == 0 &&
                           scanout_plan.scanout_text_login_fallback == 0,
                       "credential window scanout plan should mark credential widgets");
  fails += expect_true(scanout_plan.submit_callback_bound == 0 &&
                           scanout_plan.auth_callback_bound == 0 &&
                           scanout_plan.submit_enabled == 0 &&
                           scanout_plan.auth_attempt_allowed == 0 &&
                           scanout_plan.schedule_auth_submit_allowed == 0 &&
                           scanout_plan.schedule_auth_attempt_allowed == 0 &&
                           scanout_plan.raw_secret_exposed == 0 &&
                           scanout_plan.masked_text_exposed == 0,
                       "credential window scanout plan must stay redacted and auth-disabled");
  fails += expect_true(
      strings_equal(scanout_plan.scanout_ticket,
                    "credential-screen-window-scanout-ticket") &&
          strings_equal(scanout_plan.vsync_ticket,
                        "credential-screen-window-vsync-ticket") &&
          strings_equal(scanout_plan.scanout_policy,
                        "incremental-window-scanout-declarative") &&
          strings_equal(scanout_plan.state,
                        "window-scanout-credential-ready"),
      "credential window scanout plan should report scanout ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_scanout_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_window_scanout_plan scanout_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_scanout_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
          LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1, 1,
          &scanout_plan) == 0,
      "credential window scanout plan recovery should build");
  fails += expect_true(scanout_plan.window_scanout_plan_safe == 1 &&
                           scanout_plan.scanout_allowed == 1 &&
                           scanout_plan.scanout_submitted == 0 &&
                           scanout_plan.scanout_text_recovery == 1 &&
                           scanout_plan.scanout_text_login == 1 &&
                           scanout_plan.scanout_text_login_fallback == 0 &&
                           scanout_plan.scanout_credential_focus == 0,
                       "credential window scanout plan recovery should mark text recovery");
  fails += expect_true(scanout_plan.full_schedule_required == 1 &&
                           scanout_plan.schedule_incremental_allowed == 0 &&
                           scanout_plan.schedule_cache_allowed == 0 &&
                           scanout_plan.schedule_reuse_allowed == 0 &&
                           scanout_plan.input_focus_allowed == 0 &&
                           scanout_plan.vsync_fence_armed == 0 &&
                           scanout_plan.page_flip_submitted == 0 &&
                           scanout_plan.scanout_buffer_attached == 0 &&
                           scanout_plan.scanout_display_flip_submitted == 0,
                       "credential window scanout plan recovery should require full declarative scanout");
  fails += expect_true(
      strings_equal(scanout_plan.scanout_ticket,
                    "text-recovery-window-scanout-ticket") &&
          strings_equal(scanout_plan.compositor_target,
                        "text-recovery-window-scanout") &&
          strings_equal(scanout_plan.blocked_reason, "text-recovery-only"),
      "credential window scanout plan recovery should report recovery ticket");

  fails += expect_true(
      build_loginwindow_credential_screen_window_scanout_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0, 1,
          &scanout_plan) == 0,
      "credential window scanout plan resume should build");
  fails += expect_true(scanout_plan.window_scanout_plan_safe == 1 &&
                           scanout_plan.scanout_text_login_resume == 1 &&
                           scanout_plan.session_reset_required == 1 &&
                           scanout_plan.login_screen_rerender_required == 1 &&
                           scanout_plan.full_schedule_required == 1 &&
                           scanout_plan.schedule_incremental_allowed == 0 &&
                           scanout_plan.schedule_cache_allowed == 0 &&
                           scanout_plan.schedule_reuse_allowed == 0,
                       "credential window scanout plan resume should require full rerender scanout");
  fails += expect_true(scanout_plan.scanout_submitted == 0 &&
                           scanout_plan.scanout_buffer_attached == 0 &&
                           scanout_plan.scanout_display_flip_submitted == 0 &&
                           scanout_plan.vsync_fence_armed == 0 &&
                           scanout_plan.page_flip_submitted == 0 &&
                           scanout_plan.submit_enabled == 0 &&
                           scanout_plan.auth_attempt_allowed == 0,
                       "credential window scanout plan resume must keep GUI auth disabled");
  fails += expect_true(
      strings_equal(scanout_plan.scanout_ticket,
                    "text-login-resume-window-scanout-ticket") &&
          strings_equal(scanout_plan.cache_policy,
                        "window-scanout-cache-bypassed-for-rerender") &&
          strings_equal(scanout_plan.state, "window-scanout-resume-ready"),
      "credential window scanout plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_scanout_plan_falls_back_for_submit_and_unknown_action(void) {
  int fails = 0;
  struct login_window_credential_screen_window_scanout_plan scanout_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_scanout_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_SUBMIT, 0, 0, 0, 0, 1,
          &scanout_plan) == 0,
      "credential window scanout plan submit should build");
  fails += expect_true(scanout_plan.window_scanout_plan_safe == 1 &&
                           scanout_plan.submit_requested == 1 &&
                           scanout_plan.submit_blocked == 1 &&
                           scanout_plan.action_allowed == 0 &&
                           scanout_plan.action_blocked == 1 &&
                           scanout_plan.input_focus_allowed == 0 &&
                           scanout_plan.scanout_text_login == 1 &&
                           scanout_plan.scanout_text_login_fallback == 1 &&
                           scanout_plan.scanout_submitted == 0 &&
                           scanout_plan.scanout_buffer_attached == 0 &&
                           scanout_plan.scanout_display_flip_submitted == 0,
                       "credential window scanout plan submit should force text login");
  fails += expect_true(
      strings_equal(scanout_plan.scanout_ticket,
                    "text-login-fallback-window-scanout-ticket") &&
          strings_equal(scanout_plan.scanout_policy,
                        "fallback-window-scanout-declarative") &&
          strings_equal(scanout_plan.blocked_reason, "gui-submit-disabled"),
      "credential window scanout plan submit should report disabled GUI submit");

  fails += expect_true(
      build_loginwindow_credential_screen_window_scanout_plan_for_action(
          9876, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0, 1,
          &scanout_plan) == 0,
      "credential window scanout plan unknown action should build");
  fails += expect_true(scanout_plan.window_scanout_plan_safe == 1 &&
                           scanout_plan.action_allowed == 0 &&
                           scanout_plan.action_blocked == 1 &&
                           scanout_plan.input_focus_allowed == 0 &&
                           scanout_plan.scanout_text_login == 1 &&
                           scanout_plan.scanout_text_login_fallback == 1 &&
                           scanout_plan.scanout_submitted == 0 &&
                           scanout_plan.scanout_display_flip_submitted == 0,
                       "credential window scanout plan unknown action should force text login");
  fails += expect_true(
      strings_equal(scanout_plan.scanout_ticket,
                    "text-login-fallback-window-scanout-ticket") &&
          strings_equal(scanout_plan.compositor_target,
                        "text-login-fallback-window-scanout") &&
          strings_equal(scanout_plan.state,
                        "window-scanout-text-login-ready"),
      "credential window scanout plan unknown action should report fallback ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_scanout_plan_fails_closed_for_unsafe_or_missing_vsync_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_window_vsync_plan vsync_plan;
  struct login_window_credential_screen_window_scanout_plan scanout_plan;

  fails += expect_true(
      login_window_credential_screen_window_scanout_plan_build(
          NULL, &scanout_plan) == 0,
      "credential window scanout plan missing vsync should build fallback");
  fails += expect_true(scanout_plan.window_vsync_plan_available == 0 &&
                           scanout_plan.window_vsync_plan_safe == 0 &&
                           scanout_plan.window_scanout_plan_safe == 0 &&
                           scanout_plan.route_blocked == 1 &&
                           scanout_plan.scanout_allowed == 0 &&
                           scanout_plan.scanout_ticket_selected == 0 &&
                           scanout_plan.scanout_target_selected == 0 &&
                           scanout_plan.scanout_text_login == 1 &&
                           scanout_plan.scanout_text_login_fallback == 1 &&
                           scanout_plan.scanout_submitted == 0 &&
                           scanout_plan.scanout_buffer_attached == 0 &&
                           scanout_plan.scanout_display_flip_submitted == 0,
                       "credential window scanout plan missing vsync should fail closed");
  fails += expect_true(
      strings_equal(scanout_plan.scanout_ticket,
                    "text-login-fallback-window-scanout-ticket") &&
          strings_equal(scanout_plan.event_type,
                        "credential-screen-window-scanout-plan-unavailable") &&
          strings_equal(scanout_plan.blocked_reason,
                        "window-vsync-plan-unavailable"),
      "credential window scanout plan missing vsync should report missing upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_vsync_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0, 0, 1,
          &vsync_plan) == 0,
      "credential window scanout plan unsafe vsync fixture should build");
  vsync_plan.window_vsync_plan_safe = 0;
  vsync_plan.raw_secret_exposed = 1;
  vsync_plan.submit_blocked = 0;
  fails += expect_true(
      login_window_credential_screen_window_scanout_plan_build(
          &vsync_plan, &scanout_plan) == 0,
      "credential window scanout plan unsafe vsync should build fallback");
  fails += expect_true(scanout_plan.window_vsync_plan_available == 1 &&
                           scanout_plan.window_vsync_plan_safe == 0 &&
                           scanout_plan.window_scanout_plan_safe == 0 &&
                           scanout_plan.route_blocked == 1 &&
                           scanout_plan.scanout_allowed == 0 &&
                           scanout_plan.scanout_ticket_selected == 0 &&
                           scanout_plan.scanout_target_selected == 0 &&
                           scanout_plan.scanout_text_login == 1 &&
                           scanout_plan.scanout_text_login_fallback == 1 &&
                           scanout_plan.raw_secret_exposed == 0,
                       "credential window scanout plan unsafe vsync should fail closed");
  fails += expect_true(
      strings_equal(scanout_plan.scanout_ticket,
                    "text-login-fallback-window-scanout-ticket") &&
          strings_equal(scanout_plan.event_type,
                        "credential-screen-window-scanout-plan-unsafe") &&
          strings_equal(scanout_plan.blocked_reason,
                        "credential-window-scanout-plan-unsafe"),
      "credential window scanout plan unsafe vsync should report unsafe upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_vsync_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'f', 0, 0, 0, 1,
          &vsync_plan) == 0,
      "credential window scanout plan forged origin fixture should build");
  vsync_plan.window_schedule_plan_available = 0;
  vsync_plan.window_surface_plan_safe = 0;
  vsync_plan.window_vsync_plan_safe = 1;
  fails += expect_true(
      login_window_credential_screen_window_scanout_plan_build(
          &vsync_plan, &scanout_plan) == 0,
      "credential window scanout plan forged vsync origin should build fallback");
  fails += expect_true(scanout_plan.window_vsync_plan_available == 1 &&
                           scanout_plan.window_schedule_plan_available == 0 &&
                           scanout_plan.window_surface_plan_safe == 0 &&
                           scanout_plan.window_vsync_plan_safe == 1 &&
                           scanout_plan.window_scanout_plan_safe == 0 &&
                           scanout_plan.scanout_allowed == 0 &&
                           scanout_plan.scanout_ticket_selected == 0 &&
                           scanout_plan.scanout_text_login_fallback == 1 &&
                           scanout_plan.submit_enabled == 0 &&
                           scanout_plan.auth_attempt_allowed == 0,
                       "credential window scanout plan should reject forged vsync origin");
  fails += expect_true(
      strings_equal(scanout_plan.scanout_ticket,
                    "text-login-fallback-window-scanout-ticket") &&
          strings_equal(scanout_plan.event_type,
                        "credential-screen-window-scanout-plan-unsafe") &&
          strings_equal(scanout_plan.blocked_reason,
                        "credential-window-scanout-plan-unsafe"),
      "credential window scanout plan forged origin should report unsafe upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_vsync_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0, 1,
          &vsync_plan) == 0,
      "credential window scanout plan submitted vsync fixture should build");
  vsync_plan.vsync_submitted = 1;
  vsync_plan.vsync_wait_submitted = 1;
  vsync_plan.vsync_fence_armed = 1;
  vsync_plan.schedule_submitted = 1;
  vsync_plan.frame_timer_armed = 1;
  vsync_plan.page_flip_submitted = 1;
  fails += expect_true(
      login_window_credential_screen_window_scanout_plan_build(
          &vsync_plan, &scanout_plan) == 0,
      "credential window scanout plan submitted vsync should build fallback");
  fails += expect_true(scanout_plan.window_scanout_plan_safe == 0 &&
                           scanout_plan.scanout_allowed == 0 &&
                           scanout_plan.scanout_submitted == 0 &&
                           scanout_plan.scanout_buffer_attached == 0 &&
                           scanout_plan.scanout_display_flip_submitted == 0 &&
                           scanout_plan.vsync_submitted == 0 &&
                           scanout_plan.vsync_wait_submitted == 0 &&
                           scanout_plan.vsync_fence_armed == 0 &&
                           scanout_plan.schedule_submitted == 0 &&
                           scanout_plan.frame_timer_armed == 0 &&
                           scanout_plan.page_flip_submitted == 0 &&
                           scanout_plan.submit_enabled == 0 &&
                           scanout_plan.auth_attempt_allowed == 0,
                       "credential window scanout plan must not copy unsafe submitted vsync state");
  fails += expect_true(
      strings_equal(scanout_plan.scanout_ticket,
                    "text-login-fallback-window-scanout-ticket") &&
          strings_equal(scanout_plan.event_type,
                        "credential-screen-window-scanout-plan-unsafe") &&
          strings_equal(scanout_plan.blocked_reason,
                        "credential-window-scanout-plan-unsafe"),
      "credential window scanout plan submitted vsync should report unsafe upstream");
  return fails;
}

int test_login_runtime_credential_window_scanout_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_window_scanout_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_window_scanout_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_window_scanout_plan_falls_back_for_submit_and_unknown_action();
  fails += test_loginwindow_credential_screen_window_scanout_plan_fails_closed_for_unsafe_or_missing_vsync_plan();
  return fails;
}
