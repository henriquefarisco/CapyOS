/*
 * tests/auth/test_login_runtime_credential_window_vsync.c
 *
 * Credential screen window vsync plan coverage for the
 * `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-16 monolith
 * refactor (PR D.37 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_window_vsync_plan_build`: 4
 *     tests covering the credential widgets vsync + the text-route
 *     vsync (recovery + resume) + the submit/unknown fallback vsync
 *     + the missing-or-unsafe schedule plan fail-closed default.
 *
 * Also exposes shared helper
 * `build_loginwindow_credential_screen_window_vsync_plan_for_action`,
 * used by later companion files that chain on top of the vsync
 * stage (window_scanout, ...).
 *
 * Split independently from `window_scanout` (PR D.38) because the
 * combined block exceeded the 900-line layout limit.
 *
 * The companion entry `test_login_runtime_credential_window_vsync_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_window_vsync_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage,
    struct login_window_credential_screen_window_vsync_plan *vsync_plan) {
  struct login_window_credential_screen_window_schedule_plan schedule_plan;

  if (build_loginwindow_credential_screen_window_schedule_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &schedule_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_window_vsync_plan_build(
      &schedule_plan, vsync_plan);
}

static int test_loginwindow_credential_screen_window_vsync_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_window_vsync_plan vsync_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_vsync_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'p', 0, 0, 0, 1,
          &vsync_plan) == 0,
      "credential window vsync plan edit should build");
  fails += expect_true(
      vsync_plan.version ==
          LOGIN_WINDOW_CREDENTIAL_SCREEN_WINDOW_VSYNC_PLAN_VERSION,
      "credential window vsync plan should expose stable version");
  fails += expect_true(vsync_plan.window_schedule_plan_available == 1 &&
                           vsync_plan.window_present_plan_available == 1 &&
                           vsync_plan.window_damage_plan_available == 1 &&
                           vsync_plan.window_compositor_plan_available == 1 &&
                           vsync_plan.window_surface_plan_available == 1 &&
                           vsync_plan.window_surface_plan_safe == 1 &&
                           vsync_plan.window_compositor_plan_safe == 1 &&
                           vsync_plan.window_damage_plan_safe == 1 &&
                           vsync_plan.window_present_plan_safe == 1 &&
                           vsync_plan.window_schedule_plan_safe == 1 &&
                           vsync_plan.window_vsync_plan_safe == 1,
                       "credential window vsync plan should require safe schedule plan");
  fails += expect_true(vsync_plan.vsync_required == 1 &&
                           vsync_plan.vsync_allowed == 1 &&
                           vsync_plan.vsync_submitted == 0 &&
                           vsync_plan.vsync_ticket_selected == 1 &&
                           vsync_plan.vsync_target_selected == 1 &&
                           vsync_plan.vsync_wait_allowed == 0 &&
                           vsync_plan.vsync_wait_submitted == 0 &&
                           vsync_plan.vsync_fence_required == 1 &&
                           vsync_plan.vsync_fence_armed == 0 &&
                           vsync_plan.vsync_error == 0,
                       "credential window vsync plan should remain declarative");
  fails += expect_true(vsync_plan.damage_error == 0 &&
                           vsync_plan.present_error == 0 &&
                           vsync_plan.schedule_error == 0,
                       "credential window vsync plan should preserve upstream plan success");
  fails += expect_true(vsync_plan.schedule_submitted == 0 &&
                           vsync_plan.frame_timer_armed == 0 &&
                           vsync_plan.compositor_wake_allowed == 0 &&
                           vsync_plan.compositor_wake_submitted == 0 &&
                           vsync_plan.page_flip_allowed == 0 &&
                           vsync_plan.page_flip_submitted == 0 &&
                           vsync_plan.present_submitted == 0 &&
                           vsync_plan.damage_submitted == 0 &&
                           vsync_plan.compositor_submitted == 0,
                       "credential window vsync plan must not arm frame or page flip work");
  fails += expect_true(vsync_plan.vsync_credential_panel == 1 &&
                           vsync_plan.vsync_credential_input == 1 &&
                           vsync_plan.vsync_credential_focus == 1 &&
                           vsync_plan.vsync_text_login == 0 &&
                           vsync_plan.vsync_text_login_fallback == 0,
                       "credential window vsync plan should mark credential widgets");
  fails += expect_true(vsync_plan.submit_callback_bound == 0 &&
                           vsync_plan.auth_callback_bound == 0 &&
                           vsync_plan.submit_enabled == 0 &&
                           vsync_plan.auth_attempt_allowed == 0 &&
                           vsync_plan.schedule_auth_submit_allowed == 0 &&
                           vsync_plan.schedule_auth_attempt_allowed == 0 &&
                           vsync_plan.raw_secret_exposed == 0 &&
                           vsync_plan.masked_text_exposed == 0,
                       "credential window vsync plan must stay redacted and auth-disabled");
  fails += expect_true(
      strings_equal(vsync_plan.vsync_ticket,
                    "credential-screen-window-vsync-ticket") &&
          strings_equal(vsync_plan.schedule_ticket,
                        "credential-screen-window-schedule-ticket") &&
          strings_equal(vsync_plan.vsync_policy,
                        "incremental-window-vsync-declarative") &&
          strings_equal(vsync_plan.state,
                        "window-vsync-credential-ready"),
      "credential window vsync plan should report vsync ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_vsync_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_window_vsync_plan vsync_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_vsync_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
          LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1, 1,
          &vsync_plan) == 0,
      "credential window vsync plan recovery should build");
  fails += expect_true(vsync_plan.window_vsync_plan_safe == 1 &&
                           vsync_plan.vsync_allowed == 1 &&
                           vsync_plan.vsync_submitted == 0 &&
                           vsync_plan.vsync_text_recovery == 1 &&
                           vsync_plan.vsync_text_login == 1 &&
                           vsync_plan.vsync_text_login_fallback == 0 &&
                           vsync_plan.vsync_credential_focus == 0,
                       "credential window vsync plan recovery should mark text recovery");
  fails += expect_true(vsync_plan.full_schedule_required == 1 &&
                           vsync_plan.schedule_incremental_allowed == 0 &&
                           vsync_plan.schedule_cache_allowed == 0 &&
                           vsync_plan.schedule_reuse_allowed == 0 &&
                           vsync_plan.input_focus_allowed == 0 &&
                           vsync_plan.vsync_fence_armed == 0 &&
                           vsync_plan.page_flip_submitted == 0,
                       "credential window vsync plan recovery should require full declarative vsync");
  fails += expect_true(
      strings_equal(vsync_plan.vsync_ticket,
                    "text-recovery-window-vsync-ticket") &&
          strings_equal(vsync_plan.compositor_target,
                        "text-recovery-window-vsync") &&
          strings_equal(vsync_plan.blocked_reason, "text-recovery-only"),
      "credential window vsync plan recovery should report recovery ticket");

  fails += expect_true(
      build_loginwindow_credential_screen_window_vsync_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0, 1,
          &vsync_plan) == 0,
      "credential window vsync plan resume should build");
  fails += expect_true(vsync_plan.window_vsync_plan_safe == 1 &&
                           vsync_plan.vsync_text_login_resume == 1 &&
                           vsync_plan.session_reset_required == 1 &&
                           vsync_plan.login_screen_rerender_required == 1 &&
                           vsync_plan.full_schedule_required == 1 &&
                           vsync_plan.schedule_incremental_allowed == 0 &&
                           vsync_plan.schedule_cache_allowed == 0 &&
                           vsync_plan.schedule_reuse_allowed == 0,
                       "credential window vsync plan resume should require full rerender vsync");
  fails += expect_true(vsync_plan.vsync_submitted == 0 &&
                           vsync_plan.vsync_fence_armed == 0 &&
                           vsync_plan.page_flip_submitted == 0 &&
                           vsync_plan.submit_enabled == 0 &&
                           vsync_plan.auth_attempt_allowed == 0,
                       "credential window vsync plan resume must keep GUI auth disabled");
  fails += expect_true(
      strings_equal(vsync_plan.vsync_ticket,
                    "text-login-resume-window-vsync-ticket") &&
          strings_equal(vsync_plan.cache_policy,
                        "window-vsync-cache-bypassed-for-rerender") &&
          strings_equal(vsync_plan.state, "window-vsync-resume-ready"),
      "credential window vsync plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_vsync_plan_falls_back_for_submit_and_unknown_action(void) {
  int fails = 0;
  struct login_window_credential_screen_window_vsync_plan vsync_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_vsync_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_SUBMIT, 0, 0, 0, 0, 1,
          &vsync_plan) == 0,
      "credential window vsync plan submit should build");
  fails += expect_true(vsync_plan.window_vsync_plan_safe == 1 &&
                           vsync_plan.submit_requested == 1 &&
                           vsync_plan.submit_blocked == 1 &&
                           vsync_plan.action_allowed == 0 &&
                           vsync_plan.action_blocked == 1 &&
                           vsync_plan.input_focus_allowed == 0 &&
                           vsync_plan.vsync_text_login == 1 &&
                           vsync_plan.vsync_text_login_fallback == 1 &&
                           vsync_plan.vsync_submitted == 0 &&
                           vsync_plan.vsync_wait_submitted == 0 &&
                           vsync_plan.vsync_fence_armed == 0,
                       "credential window vsync plan submit should force text login");
  fails += expect_true(
      strings_equal(vsync_plan.vsync_ticket,
                    "text-login-fallback-window-vsync-ticket") &&
          strings_equal(vsync_plan.vsync_policy,
                        "fallback-window-vsync-declarative") &&
          strings_equal(vsync_plan.blocked_reason, "gui-submit-disabled"),
      "credential window vsync plan submit should report disabled GUI submit");

  fails += expect_true(
      build_loginwindow_credential_screen_window_vsync_plan_for_action(
          9876, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0, 1,
          &vsync_plan) == 0,
      "credential window vsync plan unknown action should build");
  fails += expect_true(vsync_plan.window_vsync_plan_safe == 1 &&
                           vsync_plan.action_allowed == 0 &&
                           vsync_plan.action_blocked == 1 &&
                           vsync_plan.input_focus_allowed == 0 &&
                           vsync_plan.vsync_text_login == 1 &&
                           vsync_plan.vsync_text_login_fallback == 1 &&
                           vsync_plan.vsync_submitted == 0 &&
                           vsync_plan.vsync_fence_armed == 0,
                       "credential window vsync plan unknown action should force text login");
  fails += expect_true(
      strings_equal(vsync_plan.vsync_ticket,
                    "text-login-fallback-window-vsync-ticket") &&
          strings_equal(vsync_plan.compositor_target,
                        "text-login-fallback-window-vsync") &&
          strings_equal(vsync_plan.state,
                        "window-vsync-text-login-ready"),
      "credential window vsync plan unknown action should report fallback ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_vsync_plan_fails_closed_for_unsafe_or_missing_schedule_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_window_schedule_plan schedule_plan;
  struct login_window_credential_screen_window_vsync_plan vsync_plan;

  fails += expect_true(
      login_window_credential_screen_window_vsync_plan_build(
          NULL, &vsync_plan) == 0,
      "credential window vsync plan missing schedule should build fallback");
  fails += expect_true(vsync_plan.window_schedule_plan_available == 0 &&
                           vsync_plan.window_schedule_plan_safe == 0 &&
                           vsync_plan.window_vsync_plan_safe == 0 &&
                           vsync_plan.route_blocked == 1 &&
                           vsync_plan.vsync_allowed == 0 &&
                           vsync_plan.vsync_ticket_selected == 0 &&
                           vsync_plan.vsync_target_selected == 0 &&
                           vsync_plan.vsync_text_login == 1 &&
                           vsync_plan.vsync_text_login_fallback == 1 &&
                           vsync_plan.vsync_submitted == 0 &&
                           vsync_plan.vsync_fence_armed == 0,
                       "credential window vsync plan missing schedule should fail closed");
  fails += expect_true(
      strings_equal(vsync_plan.vsync_ticket,
                    "text-login-fallback-window-vsync-ticket") &&
          strings_equal(vsync_plan.event_type,
                        "credential-screen-window-vsync-plan-unavailable") &&
          strings_equal(vsync_plan.blocked_reason,
                        "window-schedule-plan-unavailable"),
      "credential window vsync plan missing schedule should report missing upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_schedule_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0, 0, 1,
          &schedule_plan) == 0,
      "credential window vsync plan unsafe schedule fixture should build");
  schedule_plan.window_schedule_plan_safe = 0;
  schedule_plan.raw_secret_exposed = 1;
  schedule_plan.submit_blocked = 0;
  fails += expect_true(
      login_window_credential_screen_window_vsync_plan_build(
          &schedule_plan, &vsync_plan) == 0,
      "credential window vsync plan unsafe schedule should build fallback");
  fails += expect_true(vsync_plan.window_schedule_plan_available == 1 &&
                           vsync_plan.window_schedule_plan_safe == 0 &&
                           vsync_plan.window_vsync_plan_safe == 0 &&
                           vsync_plan.route_blocked == 1 &&
                           vsync_plan.vsync_allowed == 0 &&
                           vsync_plan.vsync_ticket_selected == 0 &&
                           vsync_plan.vsync_target_selected == 0 &&
                           vsync_plan.vsync_text_login == 1 &&
                           vsync_plan.vsync_text_login_fallback == 1 &&
                           vsync_plan.raw_secret_exposed == 0,
                       "credential window vsync plan unsafe schedule should fail closed");
  fails += expect_true(
      strings_equal(vsync_plan.vsync_ticket,
                    "text-login-fallback-window-vsync-ticket") &&
          strings_equal(vsync_plan.event_type,
                        "credential-screen-window-vsync-plan-unsafe") &&
          strings_equal(vsync_plan.blocked_reason,
                        "credential-window-vsync-plan-unsafe"),
      "credential window vsync plan unsafe schedule should report unsafe upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_schedule_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'f', 0, 0, 0, 1,
          &schedule_plan) == 0,
      "credential window vsync plan forged origin fixture should build");
  schedule_plan.window_present_plan_available = 0;
  schedule_plan.window_surface_plan_safe = 0;
  schedule_plan.window_schedule_plan_safe = 1;
  fails += expect_true(
      login_window_credential_screen_window_vsync_plan_build(
          &schedule_plan, &vsync_plan) == 0,
      "credential window vsync plan forged schedule origin should build fallback");
  fails += expect_true(vsync_plan.window_schedule_plan_available == 1 &&
                           vsync_plan.window_present_plan_available == 0 &&
                           vsync_plan.window_surface_plan_safe == 0 &&
                           vsync_plan.window_schedule_plan_safe == 1 &&
                           vsync_plan.window_vsync_plan_safe == 0 &&
                           vsync_plan.vsync_allowed == 0 &&
                           vsync_plan.vsync_ticket_selected == 0 &&
                           vsync_plan.vsync_text_login_fallback == 1 &&
                           vsync_plan.submit_enabled == 0 &&
                           vsync_plan.auth_attempt_allowed == 0,
                       "credential window vsync plan should reject forged schedule origin");
  fails += expect_true(
      strings_equal(vsync_plan.vsync_ticket,
                    "text-login-fallback-window-vsync-ticket") &&
          strings_equal(vsync_plan.event_type,
                        "credential-screen-window-vsync-plan-unsafe") &&
          strings_equal(vsync_plan.blocked_reason,
                        "credential-window-vsync-plan-unsafe"),
      "credential window vsync plan forged origin should report unsafe upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_schedule_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0, 1,
          &schedule_plan) == 0,
      "credential window vsync plan submitted schedule fixture should build");
  schedule_plan.schedule_submitted = 1;
  schedule_plan.schedule_auth_submit_allowed = 1;
  schedule_plan.schedule_auth_attempt_allowed = 1;
  schedule_plan.present_submitted = 1;
  schedule_plan.damage_submitted = 1;
  schedule_plan.frame_timer_armed = 1;
  schedule_plan.page_flip_submitted = 1;
  fails += expect_true(
      login_window_credential_screen_window_vsync_plan_build(
          &schedule_plan, &vsync_plan) == 0,
      "credential window vsync plan submitted schedule should build fallback");
  fails += expect_true(vsync_plan.window_vsync_plan_safe == 0 &&
                           vsync_plan.vsync_allowed == 0 &&
                           vsync_plan.vsync_submitted == 0 &&
                           vsync_plan.vsync_wait_submitted == 0 &&
                           vsync_plan.vsync_fence_armed == 0 &&
                           vsync_plan.schedule_submitted == 0 &&
                           vsync_plan.schedule_auth_submit_allowed == 0 &&
                           vsync_plan.schedule_auth_attempt_allowed == 0 &&
                           vsync_plan.present_submitted == 0 &&
                           vsync_plan.damage_submitted == 0 &&
                           vsync_plan.frame_timer_armed == 0 &&
                           vsync_plan.page_flip_submitted == 0 &&
                           vsync_plan.submit_enabled == 0 &&
                           vsync_plan.auth_attempt_allowed == 0,
                       "credential window vsync plan must not copy unsafe submitted schedule state");
  fails += expect_true(
      strings_equal(vsync_plan.vsync_ticket,
                    "text-login-fallback-window-vsync-ticket") &&
          strings_equal(vsync_plan.event_type,
                        "credential-screen-window-vsync-plan-unsafe") &&
          strings_equal(vsync_plan.blocked_reason,
                        "credential-window-vsync-plan-unsafe"),
      "credential window vsync plan submitted schedule should report unsafe upstream");
  return fails;
}

int test_login_runtime_credential_window_vsync_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_window_vsync_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_window_vsync_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_window_vsync_plan_falls_back_for_submit_and_unknown_action();
  fails += test_loginwindow_credential_screen_window_vsync_plan_fails_closed_for_unsafe_or_missing_schedule_plan();
  return fails;
}
