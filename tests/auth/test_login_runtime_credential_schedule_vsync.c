/*
 * tests/auth/test_login_runtime_credential_schedule_vsync.c
 *
 * Credential screen schedule plan + vsync plan coverage for the
 * `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-15 monolith
 * refactor (PR D.16 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_schedule_plan_build`: 4 tests
 *     covering the credential widgets schedule + the text-route
 *     schedule (recovery + resume) + the submit/unknown fallback
 *     schedule + the missing-or-unsafe present plan fail-closed
 *     default.
 *   - `login_window_credential_screen_vsync_plan_build`: 4 tests
 *     covering the credential widgets vsync + the text-route vsync
 *     (recovery + resume) + the submit/unknown fallback vsync + the
 *     missing-or-unsafe schedule plan fail-closed default.
 *
 * Also exposes shared helpers
 * `build_loginwindow_credential_screen_schedule_plan_for_action` and
 * `build_loginwindow_credential_screen_vsync_plan_for_action`, used
 * by later companion files that chain on top of the schedule/vsync
 * stages (scanout, display, output, blit, framebuffer, flush, ...).
 *
 * The companion entry `test_login_runtime_credential_schedule_vsync_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_schedule_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_schedule_plan *schedule_plan) {
  struct login_window_credential_screen_present_plan present_plan;

  if (build_loginwindow_credential_screen_present_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &present_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_schedule_plan_build(&present_plan,
                                                            schedule_plan);
}

static int test_loginwindow_credential_screen_schedule_plan_schedules_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_schedule_plan schedule_plan;

  fails += expect_true(build_loginwindow_credential_screen_schedule_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'p', 0, 0, 0,
                           1, &schedule_plan) == 0,
                       "credential schedule plan edit should build");
  fails += expect_true(schedule_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_SCHEDULE_PLAN_VERSION,
                       "credential schedule plan should expose stable version");
  fails += expect_true(schedule_plan.present_plan_available == 1 &&
                           schedule_plan.present_plan_safe == 1 &&
                           schedule_plan.schedule_plan_safe == 1,
                       "credential schedule plan should require safe present plan");
  fails += expect_true(schedule_plan.schedule_allowed == 1 &&
                           schedule_plan.schedule_submitted == 0 &&
                           schedule_plan.present_submitted == 0 &&
                           schedule_plan.damage_submitted == 0 &&
                           schedule_plan.compositor_damage_submitted == 0 &&
                           schedule_plan.schedule_ticket_selected == 1,
                       "credential schedule plan should remain declarative");
  fails += expect_true(schedule_plan.frame_pacing_required == 1 &&
                           schedule_plan.frame_pacing_allowed == 1 &&
                           schedule_plan.frame_timer_armed == 0 &&
                           schedule_plan.compositor_wake_allowed == 0 &&
                           schedule_plan.compositor_wake_submitted == 0 &&
                           schedule_plan.page_flip_allowed == 0 &&
                           schedule_plan.page_flip_submitted == 0,
                       "credential schedule plan must not arm timers or page flips");
  fails += expect_true(schedule_plan.schedule_incremental_allowed == 1 &&
                           schedule_plan.full_schedule_required == 0 &&
                           schedule_plan.schedule_cache_allowed == 1 &&
                           schedule_plan.schedule_reuse_allowed == 1 &&
                           schedule_plan.schedule_cache_hit == 0,
                       "credential schedule plan should preserve scalable planning");
  fails += expect_true(schedule_plan.schedule_credential_panel == 1 &&
                           schedule_plan.schedule_credential_input == 1 &&
                           schedule_plan.schedule_credential_focus == 1,
                       "credential schedule plan should mark credential widgets");
  fails += expect_true(schedule_plan.submit_callback_bound == 0 &&
                           schedule_plan.auth_callback_bound == 0 &&
                           schedule_plan.submit_enabled == 0 &&
                           schedule_plan.auth_attempt_allowed == 0 &&
                           schedule_plan.raw_secret_exposed == 0 &&
                           schedule_plan.masked_text_exposed == 0 &&
                           schedule_plan.length_redacted == 1,
                       "credential schedule plan must stay redacted and auth-disabled");
  fails += expect_true(strings_equal(schedule_plan.schedule_ticket,
                                     "credential-screen-schedule-ticket") &&
                           strings_equal(schedule_plan.present_ticket,
                                         "credential-screen-present-ticket") &&
                           strings_equal(schedule_plan.schedule_policy,
                                         "incremental-schedule-declarative") &&
                           strings_equal(schedule_plan.state,
                                         "schedule-credential-ready"),
                       "credential schedule plan should report schedule ticket");
  return fails;
}

static int test_loginwindow_credential_screen_schedule_plan_schedules_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_schedule_plan schedule_plan;

  fails += expect_true(build_loginwindow_credential_screen_schedule_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &schedule_plan) == 0,
                       "credential schedule plan recovery should build");
  fails += expect_true(schedule_plan.schedule_plan_safe == 1 &&
                           schedule_plan.schedule_allowed == 1 &&
                           schedule_plan.schedule_submitted == 0 &&
                           schedule_plan.schedule_text_recovery == 1 &&
                           schedule_plan.schedule_text_login == 1 &&
                           schedule_plan.schedule_credential_focus == 0,
                       "credential schedule plan recovery should mark text recovery");
  fails += expect_true(schedule_plan.frame_timer_armed == 0 &&
                           schedule_plan.page_flip_submitted == 0 &&
                           schedule_plan.submit_enabled == 0 &&
                           schedule_plan.auth_attempt_allowed == 0,
                       "credential schedule plan recovery must not schedule real GUI auth");
  fails += expect_true(strings_equal(schedule_plan.schedule_ticket,
                                     "text-recovery-schedule-ticket") &&
                           strings_equal(schedule_plan.compositor_target,
                                         "text-recovery-schedule") &&
                           strings_equal(schedule_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential schedule plan recovery should report recovery ticket");

  fails += expect_true(build_loginwindow_credential_screen_schedule_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &schedule_plan) == 0,
                       "credential schedule plan resume should build");
  fails += expect_true(schedule_plan.schedule_plan_safe == 1 &&
                           schedule_plan.schedule_text_login_resume == 1 &&
                           schedule_plan.session_reset_required == 1 &&
                           schedule_plan.login_screen_rerender_required == 1 &&
                           schedule_plan.schedule_reuse_allowed == 0 &&
                           schedule_plan.schedule_cache_allowed == 0 &&
                           schedule_plan.full_schedule_required == 1 &&
                           schedule_plan.schedule_incremental_allowed == 0,
                       "credential schedule plan resume should require full schedule planning");
  fails += expect_true(schedule_plan.schedule_submitted == 0 &&
                           schedule_plan.present_submitted == 0 &&
                           schedule_plan.damage_submitted == 0 &&
                           schedule_plan.frame_timer_armed == 0 &&
                           schedule_plan.page_flip_submitted == 0 &&
                           schedule_plan.submit_enabled == 0 &&
                           schedule_plan.auth_attempt_allowed == 0,
                       "credential schedule plan resume must keep GUI auth disabled");
  fails += expect_true(strings_equal(schedule_plan.schedule_ticket,
                                     "text-login-resume-schedule-ticket") &&
                           strings_equal(schedule_plan.schedule_policy,
                                         "full-schedule-declarative") &&
                           strings_equal(schedule_plan.state,
                                         "schedule-resume-ready"),
                       "credential schedule plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_schedule_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_schedule_plan schedule_plan;

  fails += expect_true(build_loginwindow_credential_screen_schedule_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &schedule_plan) == 0,
                       "credential schedule plan submit should build");
  fails += expect_true(schedule_plan.schedule_plan_safe == 1 &&
                           schedule_plan.submit_requested == 1 &&
                           schedule_plan.schedule_text_login_fallback == 1 &&
                           schedule_plan.action_allowed == 0 &&
                           schedule_plan.action_blocked == 1 &&
                           schedule_plan.input_focus_allowed == 0,
                       "credential schedule plan submit should force text login fallback");
  fails += expect_true(schedule_plan.schedule_allowed == 1 &&
                           schedule_plan.schedule_submitted == 0 &&
                           schedule_plan.present_submitted == 0 &&
                           schedule_plan.damage_submitted == 0 &&
                           schedule_plan.compositor_damage_submitted == 0 &&
                           schedule_plan.frame_timer_armed == 0 &&
                           schedule_plan.page_flip_submitted == 0 &&
                           schedule_plan.submit_callback_bound == 0 &&
                           schedule_plan.auth_callback_bound == 0 &&
                           schedule_plan.submit_enabled == 0 &&
                           schedule_plan.auth_attempt_allowed == 0,
                       "credential schedule plan submit must stay declarative");
  fails += expect_true(strings_equal(schedule_plan.schedule_ticket,
                                     "text-login-fallback-schedule-ticket") &&
                           strings_equal(schedule_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(schedule_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential schedule plan submit should explain fallback");

  fails += expect_true(build_loginwindow_credential_screen_schedule_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &schedule_plan) == 0,
                       "credential schedule plan unknown should build");
  fails += expect_true(schedule_plan.schedule_plan_safe == 1 &&
                           schedule_plan.schedule_text_login_fallback == 1 &&
                           schedule_plan.action_allowed == 0 &&
                           schedule_plan.action_blocked == 1,
                       "credential schedule plan unknown should force text login fallback");
  fails += expect_true(strings_equal(schedule_plan.schedule_ticket,
                                     "text-login-fallback-schedule-ticket") &&
                           strings_equal(schedule_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential schedule plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_schedule_plan_fails_closed_for_unsafe_or_missing_present_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_present_plan present_plan;
  struct login_window_credential_screen_schedule_plan schedule_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_schedule_plan_build(
                           NULL, &schedule_plan) == 0,
                       "credential schedule plan missing present plan should build fail-closed state");
  fails += expect_true(schedule_plan.present_plan_available == 0 &&
                           schedule_plan.present_plan_safe == 0 &&
                           schedule_plan.schedule_plan_safe == 0 &&
                           schedule_plan.route_selected == 0 &&
                           schedule_plan.route_blocked == 1,
                       "credential schedule plan missing present plan should block schedule plan");
  fails += expect_true(schedule_plan.schedule_allowed == 0 &&
                           schedule_plan.schedule_submitted == 0 &&
                           schedule_plan.present_submitted == 0 &&
                           schedule_plan.damage_submitted == 0 &&
                           schedule_plan.compositor_damage_submitted == 0 &&
                           schedule_plan.frame_timer_armed == 0 &&
                           schedule_plan.page_flip_submitted == 0 &&
                           schedule_plan.schedule_text_login_fallback == 1 &&
                           schedule_plan.submit_enabled == 0 &&
                           schedule_plan.auth_attempt_allowed == 0,
                       "credential schedule plan missing present plan must stay redacted");
  fails += expect_true(strings_equal(schedule_plan.schedule_ticket,
                                     "text-login-fallback-schedule-ticket") &&
                           strings_equal(schedule_plan.event_type,
                                         "credential-screen-schedule-plan-unavailable") &&
                           strings_equal(schedule_plan.blocked_reason,
                                         "present-plan-unavailable"),
                       "credential schedule plan missing present plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_present_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &present_plan) == 0,
                       "credential schedule plan unsafe present source should build");
  fails += expect_true(login_window_credential_screen_schedule_plan_build(
                           &present_plan, &schedule_plan) == 0,
                       "credential schedule plan unsafe present plan should build blocked state");
  fails += expect_true(schedule_plan.present_plan_available == 1 &&
                           schedule_plan.present_plan_safe == 0 &&
                           schedule_plan.schedule_plan_safe == 0 &&
                           schedule_plan.route_selected == 0 &&
                           schedule_plan.route_blocked == 1,
                       "credential schedule plan unsafe present plan should block schedule plan");
  fails += expect_true(schedule_plan.schedule_allowed == 0 &&
                           schedule_plan.schedule_submitted == 0 &&
                           schedule_plan.schedule_credential_focus == 0 &&
                           schedule_plan.schedule_text_login_fallback == 1 &&
                           schedule_plan.submit_enabled == 0 &&
                           schedule_plan.auth_attempt_allowed == 0,
                       "credential schedule plan unsafe present plan must force text login fallback");
  fails += expect_true(strings_equal(schedule_plan.schedule_ticket,
                                     "text-login-fallback-schedule-ticket") &&
                           strings_equal(schedule_plan.event_type,
                                         "credential-screen-schedule-plan-unsafe") &&
                           strings_equal(schedule_plan.blocked_reason,
                                         "credential-schedule-plan-unsafe"),
                       "credential schedule plan unsafe present plan should force text login");

  fails += expect_true(build_loginwindow_credential_screen_present_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'y', 0, 0, 0,
                           1, &present_plan) == 0,
                       "credential schedule plan submitted present source should build");
  present_plan.present_submitted = 1;
  present_plan.submit_enabled = 1;
  fails += expect_true(login_window_credential_screen_schedule_plan_build(
                           &present_plan, &schedule_plan) == 0,
                       "credential schedule plan submitted present should fail closed");
  fails += expect_true(schedule_plan.schedule_plan_safe == 0 &&
                           schedule_plan.schedule_allowed == 0 &&
                           schedule_plan.schedule_submitted == 0 &&
                           schedule_plan.present_submitted == 0 &&
                           schedule_plan.submit_enabled == 0 &&
                           schedule_plan.auth_attempt_allowed == 0,
                       "credential schedule plan must not copy unsafe submitted state");
  return fails;
}


int build_loginwindow_credential_screen_vsync_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_vsync_plan *vsync_plan) {
  struct login_window_credential_screen_schedule_plan schedule_plan;

  if (build_loginwindow_credential_screen_schedule_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &schedule_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_vsync_plan_build(&schedule_plan,
                                                         vsync_plan);
}

static int test_loginwindow_credential_screen_vsync_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_vsync_plan vsync_plan;

  fails += expect_true(build_loginwindow_credential_screen_vsync_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'p', 0, 0, 0,
                           1, &vsync_plan) == 0,
                       "credential vsync plan edit should build");
  fails += expect_true(vsync_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_VSYNC_PLAN_VERSION,
                       "credential vsync plan should expose stable version");
  fails += expect_true(vsync_plan.schedule_plan_available == 1 &&
                           vsync_plan.schedule_plan_safe == 1 &&
                           vsync_plan.vsync_plan_safe == 1,
                       "credential vsync plan should require safe schedule plan");
  fails += expect_true(vsync_plan.vsync_allowed == 1 &&
                           vsync_plan.vsync_submitted == 0 &&
                           vsync_plan.schedule_submitted == 0 &&
                           vsync_plan.present_submitted == 0 &&
                           vsync_plan.damage_submitted == 0 &&
                           vsync_plan.compositor_damage_submitted == 0 &&
                           vsync_plan.vsync_ticket_selected == 1,
                       "credential vsync plan should remain declarative");
  fails += expect_true(vsync_plan.frame_pacing_required == 1 &&
                           vsync_plan.frame_pacing_allowed == 1 &&
                           vsync_plan.frame_timer_armed == 0 &&
                           vsync_plan.compositor_wake_allowed == 0 &&
                           vsync_plan.compositor_wake_submitted == 0 &&
                           vsync_plan.page_flip_allowed == 0 &&
                           vsync_plan.page_flip_submitted == 0 &&
                           vsync_plan.vsync_wait_allowed == 0 &&
                           vsync_plan.vsync_wait_submitted == 0 &&
                           vsync_plan.vsync_fence_armed == 0,
                       "credential vsync plan must not arm waits timers or page flips");
  fails += expect_true(vsync_plan.schedule_incremental_allowed == 1 &&
                           vsync_plan.full_schedule_required == 0 &&
                           vsync_plan.schedule_cache_allowed == 1 &&
                           vsync_plan.schedule_reuse_allowed == 1 &&
                           vsync_plan.schedule_cache_hit == 0,
                       "credential vsync plan should preserve scalable planning");
  fails += expect_true(vsync_plan.vsync_credential_panel == 1 &&
                           vsync_plan.vsync_credential_input == 1 &&
                           vsync_plan.vsync_credential_focus == 1,
                       "credential vsync plan should mark credential widgets");
  fails += expect_true(vsync_plan.submit_callback_bound == 0 &&
                           vsync_plan.auth_callback_bound == 0 &&
                           vsync_plan.submit_enabled == 0 &&
                           vsync_plan.auth_attempt_allowed == 0 &&
                           vsync_plan.raw_secret_exposed == 0 &&
                           vsync_plan.masked_text_exposed == 0 &&
                           vsync_plan.length_redacted == 1,
                       "credential vsync plan must stay redacted and auth-disabled");
  fails += expect_true(strings_equal(vsync_plan.vsync_ticket,
                                     "credential-screen-vsync-ticket") &&
                           strings_equal(vsync_plan.schedule_ticket,
                                         "credential-screen-schedule-ticket") &&
                           strings_equal(vsync_plan.vsync_policy,
                                         "incremental-vsync-declarative") &&
                           strings_equal(vsync_plan.state,
                                         "vsync-credential-ready"),
                       "credential vsync plan should report vsync ticket");
  return fails;
}

static int test_loginwindow_credential_screen_vsync_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_vsync_plan vsync_plan;

  fails += expect_true(build_loginwindow_credential_screen_vsync_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &vsync_plan) == 0,
                       "credential vsync plan recovery should build");
  fails += expect_true(vsync_plan.vsync_plan_safe == 1 &&
                           vsync_plan.vsync_allowed == 1 &&
                           vsync_plan.vsync_submitted == 0 &&
                           vsync_plan.vsync_text_recovery == 1 &&
                           vsync_plan.vsync_text_login == 1 &&
                           vsync_plan.vsync_credential_focus == 0,
                       "credential vsync plan recovery should mark text recovery");
  fails += expect_true(vsync_plan.frame_timer_armed == 0 &&
                           vsync_plan.vsync_wait_submitted == 0 &&
                           vsync_plan.vsync_fence_armed == 0 &&
                           vsync_plan.page_flip_submitted == 0 &&
                           vsync_plan.submit_enabled == 0 &&
                           vsync_plan.auth_attempt_allowed == 0,
                       "credential vsync plan recovery must not schedule real GUI auth");
  fails += expect_true(strings_equal(vsync_plan.vsync_ticket,
                                     "text-recovery-vsync-ticket") &&
                           strings_equal(vsync_plan.compositor_target,
                                         "text-recovery-vsync") &&
                           strings_equal(vsync_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential vsync plan recovery should report recovery ticket");

  fails += expect_true(build_loginwindow_credential_screen_vsync_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &vsync_plan) == 0,
                       "credential vsync plan resume should build");
  fails += expect_true(vsync_plan.vsync_plan_safe == 1 &&
                           vsync_plan.vsync_text_login_resume == 1 &&
                           vsync_plan.session_reset_required == 1 &&
                           vsync_plan.login_screen_rerender_required == 1 &&
                           vsync_plan.schedule_reuse_allowed == 0 &&
                           vsync_plan.schedule_cache_allowed == 0 &&
                           vsync_plan.full_schedule_required == 1 &&
                           vsync_plan.schedule_incremental_allowed == 0,
                       "credential vsync plan resume should require full planning");
  fails += expect_true(vsync_plan.vsync_submitted == 0 &&
                           vsync_plan.schedule_submitted == 0 &&
                           vsync_plan.present_submitted == 0 &&
                           vsync_plan.damage_submitted == 0 &&
                           vsync_plan.frame_timer_armed == 0 &&
                           vsync_plan.vsync_fence_armed == 0 &&
                           vsync_plan.page_flip_submitted == 0 &&
                           vsync_plan.submit_enabled == 0 &&
                           vsync_plan.auth_attempt_allowed == 0,
                       "credential vsync plan resume must keep GUI auth disabled");
  fails += expect_true(strings_equal(vsync_plan.vsync_ticket,
                                     "text-login-resume-vsync-ticket") &&
                           strings_equal(vsync_plan.vsync_policy,
                                         "full-vsync-declarative") &&
                           strings_equal(vsync_plan.state,
                                         "vsync-resume-ready"),
                       "credential vsync plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_vsync_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_vsync_plan vsync_plan;

  fails += expect_true(build_loginwindow_credential_screen_vsync_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &vsync_plan) == 0,
                       "credential vsync plan submit should build");
  fails += expect_true(vsync_plan.vsync_plan_safe == 1 &&
                           vsync_plan.submit_requested == 1 &&
                           vsync_plan.vsync_text_login_fallback == 1 &&
                           vsync_plan.action_allowed == 0 &&
                           vsync_plan.action_blocked == 1 &&
                           vsync_plan.input_focus_allowed == 0,
                       "credential vsync plan submit should force text login fallback");
  fails += expect_true(vsync_plan.vsync_allowed == 1 &&
                           vsync_plan.vsync_submitted == 0 &&
                           vsync_plan.schedule_submitted == 0 &&
                           vsync_plan.present_submitted == 0 &&
                           vsync_plan.damage_submitted == 0 &&
                           vsync_plan.compositor_damage_submitted == 0 &&
                           vsync_plan.frame_timer_armed == 0 &&
                           vsync_plan.vsync_wait_submitted == 0 &&
                           vsync_plan.vsync_fence_armed == 0 &&
                           vsync_plan.page_flip_submitted == 0 &&
                           vsync_plan.submit_callback_bound == 0 &&
                           vsync_plan.auth_callback_bound == 0 &&
                           vsync_plan.submit_enabled == 0 &&
                           vsync_plan.auth_attempt_allowed == 0,
                       "credential vsync plan submit must stay declarative");
  fails += expect_true(strings_equal(vsync_plan.vsync_ticket,
                                     "text-login-fallback-vsync-ticket") &&
                           strings_equal(vsync_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(vsync_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential vsync plan submit should explain fallback");

  fails += expect_true(build_loginwindow_credential_screen_vsync_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &vsync_plan) == 0,
                       "credential vsync plan unknown should build");
  fails += expect_true(vsync_plan.vsync_plan_safe == 1 &&
                           vsync_plan.vsync_text_login_fallback == 1 &&
                           vsync_plan.action_allowed == 0 &&
                           vsync_plan.action_blocked == 1,
                       "credential vsync plan unknown should force text login fallback");
  fails += expect_true(strings_equal(vsync_plan.vsync_ticket,
                                     "text-login-fallback-vsync-ticket") &&
                           strings_equal(vsync_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential vsync plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_vsync_plan_fails_closed_for_unsafe_or_missing_schedule_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_schedule_plan schedule_plan;
  struct login_window_credential_screen_vsync_plan vsync_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_vsync_plan_build(
                           NULL, &vsync_plan) == 0,
                       "credential vsync plan missing schedule plan should build fail-closed state");
  fails += expect_true(vsync_plan.schedule_plan_available == 0 &&
                           vsync_plan.schedule_plan_safe == 0 &&
                           vsync_plan.vsync_plan_safe == 0 &&
                           vsync_plan.route_selected == 0 &&
                           vsync_plan.route_blocked == 1,
                       "credential vsync plan missing schedule plan should block vsync plan");
  fails += expect_true(vsync_plan.vsync_allowed == 0 &&
                           vsync_plan.vsync_submitted == 0 &&
                           vsync_plan.vsync_wait_submitted == 0 &&
                           vsync_plan.vsync_fence_armed == 0 &&
                           vsync_plan.schedule_submitted == 0 &&
                           vsync_plan.present_submitted == 0 &&
                           vsync_plan.damage_submitted == 0 &&
                           vsync_plan.compositor_damage_submitted == 0 &&
                           vsync_plan.frame_timer_armed == 0 &&
                           vsync_plan.page_flip_submitted == 0 &&
                           vsync_plan.vsync_text_login_fallback == 1 &&
                           vsync_plan.submit_enabled == 0 &&
                           vsync_plan.auth_attempt_allowed == 0,
                       "credential vsync plan missing schedule plan must stay redacted");
  fails += expect_true(strings_equal(vsync_plan.vsync_ticket,
                                     "text-login-fallback-vsync-ticket") &&
                           strings_equal(vsync_plan.event_type,
                                         "credential-screen-vsync-plan-unavailable") &&
                           strings_equal(vsync_plan.blocked_reason,
                                         "schedule-plan-unavailable"),
                       "credential vsync plan missing schedule plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_schedule_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &schedule_plan) == 0,
                       "credential vsync plan unsafe schedule source should build");
  fails += expect_true(login_window_credential_screen_vsync_plan_build(
                           &schedule_plan, &vsync_plan) == 0,
                       "credential vsync plan unsafe schedule plan should build blocked state");
  fails += expect_true(vsync_plan.schedule_plan_available == 1 &&
                           vsync_plan.schedule_plan_safe == 0 &&
                           vsync_plan.vsync_plan_safe == 0 &&
                           vsync_plan.route_selected == 0 &&
                           vsync_plan.route_blocked == 1,
                       "credential vsync plan unsafe schedule plan should block vsync plan");
  fails += expect_true(vsync_plan.vsync_allowed == 0 &&
                           vsync_plan.vsync_submitted == 0 &&
                           vsync_plan.vsync_credential_focus == 0 &&
                           vsync_plan.vsync_text_login_fallback == 1 &&
                           vsync_plan.submit_enabled == 0 &&
                           vsync_plan.auth_attempt_allowed == 0,
                       "credential vsync plan unsafe schedule plan must force text login fallback");
  fails += expect_true(strings_equal(vsync_plan.vsync_ticket,
                                     "text-login-fallback-vsync-ticket") &&
                           strings_equal(vsync_plan.event_type,
                                         "credential-screen-vsync-plan-unsafe") &&
                           strings_equal(vsync_plan.blocked_reason,
                                         "credential-vsync-plan-unsafe"),
                       "credential vsync plan unsafe schedule plan should force text login");

  fails += expect_true(build_loginwindow_credential_screen_schedule_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'y', 0, 0, 0,
                           1, &schedule_plan) == 0,
                       "credential vsync plan submitted schedule source should build");
  schedule_plan.schedule_submitted = 1;
  schedule_plan.frame_timer_armed = 1;
  schedule_plan.page_flip_allowed = 1;
  fails += expect_true(login_window_credential_screen_vsync_plan_build(
                           &schedule_plan, &vsync_plan) == 0,
                       "credential vsync plan submitted schedule should fail closed");
  fails += expect_true(vsync_plan.vsync_plan_safe == 0 &&
                           vsync_plan.vsync_allowed == 0 &&
                           vsync_plan.vsync_submitted == 0 &&
                           vsync_plan.schedule_submitted == 0 &&
                           vsync_plan.frame_timer_armed == 0 &&
                           vsync_plan.page_flip_allowed == 0 &&
                           vsync_plan.page_flip_submitted == 0 &&
                           vsync_plan.submit_enabled == 0 &&
                           vsync_plan.auth_attempt_allowed == 0,
                       "credential vsync plan must not copy unsafe submitted state");
  return fails;
}

int test_login_runtime_credential_schedule_vsync_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_schedule_plan_schedules_credential_widgets();
  fails += test_loginwindow_credential_screen_schedule_plan_schedules_text_routes();
  fails += test_loginwindow_credential_screen_schedule_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_schedule_plan_fails_closed_for_unsafe_or_missing_present_plan();
  fails += test_loginwindow_credential_screen_vsync_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_vsync_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_vsync_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_vsync_plan_fails_closed_for_unsafe_or_missing_schedule_plan();
  return fails;
}
