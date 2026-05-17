/*
 * tests/auth/test_login_runtime_credential_window_schedule.c
 *
 * Credential screen window schedule plan coverage for the
 * `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-16 monolith
 * refactor (PR D.36 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_window_schedule_plan_build`: 4
 *     tests covering the credential widgets schedule + the
 *     text-route schedule (recovery + resume) + the submit/unknown
 *     fallback schedule + the missing-or-unsafe present plan
 *     fail-closed default.
 *
 * Also exposes shared helper
 * `build_loginwindow_credential_screen_window_schedule_plan_for_action`,
 * used by later companion files that chain on top of the schedule
 * stage (window_vsync, ...).
 *
 * Split independently from `window_present` (PR D.35) because the
 * combined block exceeded the 900-line layout limit.
 *
 * The companion entry `test_login_runtime_credential_window_schedule_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_window_schedule_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage,
    struct login_window_credential_screen_window_schedule_plan *schedule_plan) {
  struct login_window_credential_screen_window_present_plan present_plan;

  if (build_loginwindow_credential_screen_window_present_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &present_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_window_schedule_plan_build(
      &present_plan, schedule_plan);
}

static int test_loginwindow_credential_screen_window_schedule_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_window_schedule_plan schedule_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_schedule_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'p', 0, 0, 0, 1,
          &schedule_plan) == 0,
      "credential window schedule plan edit should build");
  fails += expect_true(
      schedule_plan.version ==
          LOGIN_WINDOW_CREDENTIAL_SCREEN_WINDOW_SCHEDULE_PLAN_VERSION,
      "credential window schedule plan should expose stable version");
  fails += expect_true(schedule_plan.window_present_plan_available == 1 &&
                           schedule_plan.window_damage_plan_available == 1 &&
                           schedule_plan.window_compositor_plan_available == 1 &&
                           schedule_plan.window_surface_plan_available == 1 &&
                           schedule_plan.window_surface_plan_safe == 1 &&
                           schedule_plan.window_compositor_plan_safe == 1 &&
                           schedule_plan.window_damage_plan_safe == 1 &&
                           schedule_plan.window_present_plan_safe == 1 &&
                           schedule_plan.window_schedule_plan_safe == 1,
                       "credential window schedule plan should require safe present plan");
  fails += expect_true(schedule_plan.schedule_required == 1 &&
                           schedule_plan.schedule_allowed == 1 &&
                           schedule_plan.schedule_submitted == 0 &&
                           schedule_plan.schedule_ticket_selected == 1 &&
                           schedule_plan.schedule_target_selected == 1 &&
                           schedule_plan.schedule_incremental_allowed == 1 &&
                           schedule_plan.full_schedule_required == 0 &&
                           schedule_plan.schedule_cache_allowed == 1 &&
                           schedule_plan.schedule_reuse_allowed == 1 &&
                           schedule_plan.schedule_cache_hit == 0,
                       "credential window schedule plan should remain declarative and cache eligible");
  fails += expect_true(schedule_plan.frame_pacing_required == 1 &&
                           schedule_plan.frame_pacing_allowed == 1 &&
                           schedule_plan.frame_timer_armed == 0 &&
                           schedule_plan.compositor_wake_allowed == 0 &&
                           schedule_plan.compositor_wake_submitted == 0 &&
                           schedule_plan.page_flip_allowed == 0 &&
                           schedule_plan.page_flip_submitted == 0,
                       "credential window schedule plan must not arm frame or page flip work");
  fails += expect_true(schedule_plan.present_submitted == 0 &&
                           schedule_plan.damage_submitted == 0 &&
                           schedule_plan.compositor_submitted == 0 &&
                           schedule_plan.compositor_surface_submitted == 0 &&
                           schedule_plan.compositor_damage_submitted == 0 &&
                           schedule_plan.surface_bound == 0 &&
                           schedule_plan.surface_memory_mapped == 0 &&
                           schedule_plan.surface_pixels_written == 0 &&
                           schedule_plan.window_created == 0 &&
                           schedule_plan.window_surface_bound == 0 &&
                           schedule_plan.window_input_bound == 0 &&
                           schedule_plan.gui_submitted == 0 &&
                           schedule_plan.release_submitted == 0 &&
                           schedule_plan.reclaim_submitted == 0 &&
                           schedule_plan.compaction_submitted == 0,
                       "credential window schedule plan must not execute upstream work");
  fails += expect_true(schedule_plan.schedule_credential_panel == 1 &&
                           schedule_plan.schedule_credential_input == 1 &&
                           schedule_plan.schedule_credential_focus == 1 &&
                           schedule_plan.schedule_text_login == 0 &&
                           schedule_plan.schedule_text_login_fallback == 0,
                       "credential window schedule plan should mark credential widgets");
  fails += expect_true(schedule_plan.submit_callback_bound == 0 &&
                           schedule_plan.auth_callback_bound == 0 &&
                           schedule_plan.submit_enabled == 0 &&
                           schedule_plan.auth_attempt_allowed == 0 &&
                           schedule_plan.present_auth_submit_allowed == 0 &&
                           schedule_plan.present_auth_attempt_allowed == 0 &&
                           schedule_plan.schedule_auth_submit_allowed == 0 &&
                           schedule_plan.schedule_auth_attempt_allowed == 0 &&
                           schedule_plan.raw_secret_exposed == 0 &&
                           schedule_plan.masked_text_exposed == 0 &&
                           schedule_plan.schedule_error == 0,
                       "credential window schedule plan must stay redacted and auth-disabled");
  fails += expect_true(
      strings_equal(schedule_plan.schedule_ticket,
                    "credential-screen-window-schedule-ticket") &&
          strings_equal(schedule_plan.present_ticket,
                        "credential-screen-window-present-ticket") &&
          strings_equal(schedule_plan.schedule_policy,
                        "incremental-window-schedule-declarative") &&
          strings_equal(schedule_plan.state,
                        "window-schedule-credential-ready"),
      "credential window schedule plan should report schedule ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_schedule_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_window_schedule_plan schedule_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_schedule_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
          LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1, 1,
          &schedule_plan) == 0,
      "credential window schedule plan recovery should build");
  fails += expect_true(schedule_plan.window_schedule_plan_safe == 1 &&
                           schedule_plan.schedule_allowed == 1 &&
                           schedule_plan.schedule_submitted == 0 &&
                           schedule_plan.schedule_text_recovery == 1 &&
                           schedule_plan.schedule_text_login == 1 &&
                           schedule_plan.schedule_text_login_fallback == 0 &&
                           schedule_plan.schedule_credential_focus == 0,
                       "credential window schedule plan recovery should mark text recovery");
  fails += expect_true(schedule_plan.full_schedule_required == 1 &&
                           schedule_plan.schedule_incremental_allowed == 0 &&
                           schedule_plan.schedule_cache_allowed == 0 &&
                           schedule_plan.schedule_reuse_allowed == 0 &&
                           schedule_plan.input_focus_allowed == 0 &&
                           schedule_plan.frame_timer_armed == 0 &&
                           schedule_plan.page_flip_submitted == 0,
                       "credential window schedule plan recovery should require full declarative schedule");
  fails += expect_true(schedule_plan.present_submitted == 0 &&
                           schedule_plan.damage_submitted == 0 &&
                           schedule_plan.schedule_auth_submit_allowed == 0 &&
                           schedule_plan.schedule_auth_attempt_allowed == 0 &&
                           schedule_plan.submit_enabled == 0 &&
                           schedule_plan.auth_attempt_allowed == 0,
                       "credential window schedule plan recovery must not submit or authenticate");
  fails += expect_true(
      strings_equal(schedule_plan.schedule_ticket,
                    "text-recovery-window-schedule-ticket") &&
          strings_equal(schedule_plan.compositor_target,
                        "text-recovery-window-schedule") &&
          strings_equal(schedule_plan.blocked_reason, "text-recovery-only"),
      "credential window schedule plan recovery should report recovery ticket");

  fails += expect_true(
      build_loginwindow_credential_screen_window_schedule_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0, 1,
          &schedule_plan) == 0,
      "credential window schedule plan resume should build");
  fails += expect_true(schedule_plan.window_schedule_plan_safe == 1 &&
                           schedule_plan.schedule_text_login_resume == 1 &&
                           schedule_plan.session_reset_required == 1 &&
                           schedule_plan.login_screen_rerender_required == 1 &&
                           schedule_plan.full_schedule_required == 1 &&
                           schedule_plan.schedule_incremental_allowed == 0 &&
                           schedule_plan.schedule_cache_allowed == 0 &&
                           schedule_plan.schedule_reuse_allowed == 0,
                       "credential window schedule plan resume should require full rerender schedule");
  fails += expect_true(schedule_plan.schedule_submitted == 0 &&
                           schedule_plan.frame_timer_armed == 0 &&
                           schedule_plan.page_flip_submitted == 0 &&
                           schedule_plan.submit_enabled == 0 &&
                           schedule_plan.auth_attempt_allowed == 0,
                       "credential window schedule plan resume must keep GUI auth disabled");
  fails += expect_true(
      strings_equal(schedule_plan.schedule_ticket,
                    "text-login-resume-window-schedule-ticket") &&
          strings_equal(schedule_plan.cache_policy,
                        "window-schedule-cache-bypassed-for-rerender") &&
          strings_equal(schedule_plan.state, "window-schedule-resume-ready"),
      "credential window schedule plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_schedule_plan_falls_back_for_submit_and_unknown_action(void) {
  int fails = 0;
  struct login_window_credential_screen_window_schedule_plan schedule_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_schedule_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_SUBMIT, 0, 0, 0, 0, 1,
          &schedule_plan) == 0,
      "credential window schedule plan submit should build");
  fails += expect_true(schedule_plan.window_schedule_plan_safe == 1 &&
                           schedule_plan.submit_requested == 1 &&
                           schedule_plan.submit_blocked == 1 &&
                           schedule_plan.action_allowed == 0 &&
                           schedule_plan.action_blocked == 1 &&
                           schedule_plan.input_focus_allowed == 0 &&
                           schedule_plan.schedule_text_login == 1 &&
                           schedule_plan.schedule_text_login_fallback == 1 &&
                           schedule_plan.schedule_submitted == 0 &&
                           schedule_plan.frame_timer_armed == 0 &&
                           schedule_plan.page_flip_submitted == 0,
                       "credential window schedule plan submit should force text login");
  fails += expect_true(
      strings_equal(schedule_plan.schedule_ticket,
                    "text-login-fallback-window-schedule-ticket") &&
          strings_equal(schedule_plan.schedule_policy,
                        "fallback-window-schedule-declarative") &&
          strings_equal(schedule_plan.blocked_reason, "gui-submit-disabled"),
      "credential window schedule plan submit should report disabled GUI submit");

  fails += expect_true(
      build_loginwindow_credential_screen_window_schedule_plan_for_action(
          9876, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0, 1,
          &schedule_plan) == 0,
      "credential window schedule plan unknown action should build");
  fails += expect_true(schedule_plan.window_schedule_plan_safe == 1 &&
                           schedule_plan.action_allowed == 0 &&
                           schedule_plan.action_blocked == 1 &&
                           schedule_plan.input_focus_allowed == 0 &&
                           schedule_plan.schedule_text_login == 1 &&
                           schedule_plan.schedule_text_login_fallback == 1 &&
                           schedule_plan.schedule_submitted == 0 &&
                           schedule_plan.page_flip_submitted == 0,
                       "credential window schedule plan unknown action should force text login");
  fails += expect_true(
      strings_equal(schedule_plan.schedule_ticket,
                    "text-login-fallback-window-schedule-ticket") &&
          strings_equal(schedule_plan.compositor_target,
                        "text-login-fallback-window-schedule") &&
          strings_equal(schedule_plan.state,
                        "window-schedule-text-login-ready"),
      "credential window schedule plan unknown action should report fallback ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_schedule_plan_fails_closed_for_unsafe_or_missing_present_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_window_present_plan present_plan;
  struct login_window_credential_screen_window_schedule_plan schedule_plan;

  fails += expect_true(
      login_window_credential_screen_window_schedule_plan_build(
          NULL, &schedule_plan) == 0,
      "credential window schedule plan missing present should build fallback");
  fails += expect_true(schedule_plan.window_present_plan_available == 0 &&
                           schedule_plan.window_present_plan_safe == 0 &&
                           schedule_plan.window_schedule_plan_safe == 0 &&
                           schedule_plan.route_blocked == 1 &&
                           schedule_plan.schedule_allowed == 0 &&
                           schedule_plan.schedule_ticket_selected == 0 &&
                           schedule_plan.schedule_target_selected == 0 &&
                           schedule_plan.schedule_text_login == 1 &&
                           schedule_plan.schedule_text_login_fallback == 1 &&
                           schedule_plan.schedule_submitted == 0 &&
                           schedule_plan.frame_timer_armed == 0,
                       "credential window schedule plan missing present should fail closed");
  fails += expect_true(
      strings_equal(schedule_plan.schedule_ticket,
                    "text-login-fallback-window-schedule-ticket") &&
          strings_equal(schedule_plan.event_type,
                        "credential-screen-window-schedule-plan-unavailable") &&
          strings_equal(schedule_plan.blocked_reason,
                        "window-present-plan-unavailable"),
      "credential window schedule plan missing present should report missing upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_present_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0, 0, 1,
          &present_plan) == 0,
      "credential window schedule plan unsafe present fixture should build");
  present_plan.window_present_plan_safe = 0;
  present_plan.raw_secret_exposed = 1;
  present_plan.submit_blocked = 0;
  fails += expect_true(
      login_window_credential_screen_window_schedule_plan_build(
          &present_plan, &schedule_plan) == 0,
      "credential window schedule plan unsafe present should build fallback");
  fails += expect_true(schedule_plan.window_present_plan_available == 1 &&
                           schedule_plan.window_present_plan_safe == 0 &&
                           schedule_plan.window_schedule_plan_safe == 0 &&
                           schedule_plan.route_blocked == 1 &&
                           schedule_plan.schedule_allowed == 0 &&
                           schedule_plan.schedule_ticket_selected == 0 &&
                           schedule_plan.schedule_target_selected == 0 &&
                           schedule_plan.schedule_text_login == 1 &&
                           schedule_plan.schedule_text_login_fallback == 1 &&
                           schedule_plan.raw_secret_exposed == 0,
                       "credential window schedule plan unsafe present should fail closed");
  fails += expect_true(
      strings_equal(schedule_plan.schedule_ticket,
                    "text-login-fallback-window-schedule-ticket") &&
          strings_equal(schedule_plan.event_type,
                        "credential-screen-window-schedule-plan-unsafe") &&
          strings_equal(schedule_plan.blocked_reason,
                        "credential-window-schedule-plan-unsafe"),
      "credential window schedule plan unsafe present should report unsafe upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_present_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'f', 0, 0, 0, 1,
          &present_plan) == 0,
      "credential window schedule plan forged origin fixture should build");
  present_plan.window_damage_plan_available = 0;
  present_plan.window_surface_plan_safe = 0;
  present_plan.window_present_plan_safe = 1;
  fails += expect_true(
      login_window_credential_screen_window_schedule_plan_build(
          &present_plan, &schedule_plan) == 0,
      "credential window schedule plan forged present origin should build fallback");
  fails += expect_true(schedule_plan.window_present_plan_available == 1 &&
                           schedule_plan.window_damage_plan_available == 0 &&
                           schedule_plan.window_surface_plan_safe == 0 &&
                           schedule_plan.window_present_plan_safe == 1 &&
                           schedule_plan.window_schedule_plan_safe == 0 &&
                           schedule_plan.schedule_allowed == 0 &&
                           schedule_plan.schedule_ticket_selected == 0 &&
                           schedule_plan.schedule_text_login_fallback == 1 &&
                           schedule_plan.submit_enabled == 0 &&
                           schedule_plan.auth_attempt_allowed == 0,
                       "credential window schedule plan should reject forged present origin");
  fails += expect_true(
      strings_equal(schedule_plan.schedule_ticket,
                    "text-login-fallback-window-schedule-ticket") &&
          strings_equal(schedule_plan.event_type,
                        "credential-screen-window-schedule-plan-unsafe") &&
          strings_equal(schedule_plan.blocked_reason,
                        "credential-window-schedule-plan-unsafe"),
      "credential window schedule plan forged origin should report unsafe upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_present_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0, 1,
          &present_plan) == 0,
      "credential window schedule plan submitted present fixture should build");
  present_plan.present_submitted = 1;
  present_plan.present_auth_submit_allowed = 1;
  present_plan.present_auth_attempt_allowed = 1;
  present_plan.damage_submitted = 1;
  present_plan.compositor_submitted = 1;
  fails += expect_true(
      login_window_credential_screen_window_schedule_plan_build(
          &present_plan, &schedule_plan) == 0,
      "credential window schedule plan submitted present should build fallback");
  fails += expect_true(schedule_plan.window_schedule_plan_safe == 0 &&
                           schedule_plan.schedule_allowed == 0 &&
                           schedule_plan.schedule_submitted == 0 &&
                           schedule_plan.schedule_auth_submit_allowed == 0 &&
                           schedule_plan.schedule_auth_attempt_allowed == 0 &&
                           schedule_plan.present_submitted == 0 &&
                           schedule_plan.present_auth_submit_allowed == 0 &&
                           schedule_plan.present_auth_attempt_allowed == 0 &&
                           schedule_plan.damage_submitted == 0 &&
                           schedule_plan.frame_timer_armed == 0 &&
                           schedule_plan.compositor_wake_submitted == 0 &&
                           schedule_plan.page_flip_submitted == 0 &&
                           schedule_plan.submit_enabled == 0 &&
                           schedule_plan.auth_attempt_allowed == 0,
                       "credential window schedule plan must not copy unsafe submitted present state");
  fails += expect_true(
      strings_equal(schedule_plan.schedule_ticket,
                    "text-login-fallback-window-schedule-ticket") &&
          strings_equal(schedule_plan.event_type,
                        "credential-screen-window-schedule-plan-unsafe") &&
          strings_equal(schedule_plan.blocked_reason,
                        "credential-window-schedule-plan-unsafe"),
      "credential window schedule plan submitted present should report unsafe upstream");
  return fails;
}

int test_login_runtime_credential_window_schedule_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_window_schedule_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_window_schedule_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_window_schedule_plan_falls_back_for_submit_and_unknown_action();
  fails += test_loginwindow_credential_screen_window_schedule_plan_fails_closed_for_unsafe_or_missing_present_plan();
  return fails;
}
