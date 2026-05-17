/*
 * tests/auth/test_login_runtime_credential_scanout_display.c
 *
 * Credential screen scanout plan + display plan coverage for the
 * `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-15 monolith
 * refactor (PR D.17 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_scanout_plan_build`: 4 tests
 *     covering the credential widgets scanout + the text-route
 *     scanout (recovery + resume) + the submit/unknown fallback
 *     scanout + the missing-or-unsafe vsync plan fail-closed
 *     default.
 *   - `login_window_credential_screen_display_plan_build`: 4 tests
 *     covering the credential widgets display + the text-route
 *     display (recovery + resume) + the submit/unknown fallback
 *     display + the missing-or-unsafe scanout plan fail-closed
 *     default.
 *
 * Also exposes shared helpers
 * `build_loginwindow_credential_screen_scanout_plan_for_action` and
 * `build_loginwindow_credential_screen_display_plan_for_action`,
 * used by later companion files that chain on top of the
 * scanout/display stages (output, blit, framebuffer, flush, ...).
 *
 * The companion entry `test_login_runtime_credential_scanout_display_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_scanout_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_scanout_plan *scanout_plan) {
  struct login_window_credential_screen_vsync_plan vsync_plan;

  if (build_loginwindow_credential_screen_vsync_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &vsync_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_scanout_plan_build(&vsync_plan,
                                                           scanout_plan);
}

static int test_loginwindow_credential_screen_scanout_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_scanout_plan scanout_plan;

  fails += expect_true(build_loginwindow_credential_screen_scanout_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'p', 0, 0, 0,
                           1, &scanout_plan) == 0,
                       "credential scanout plan edit should build");
  fails += expect_true(scanout_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_SCANOUT_PLAN_VERSION,
                       "credential scanout plan should expose stable version");
  fails += expect_true(scanout_plan.vsync_plan_available == 1 &&
                           scanout_plan.vsync_plan_safe == 1 &&
                           scanout_plan.scanout_plan_safe == 1,
                       "credential scanout plan should require safe vsync plan");
  fails += expect_true(scanout_plan.scanout_allowed == 1 &&
                           scanout_plan.scanout_submitted == 0 &&
                           scanout_plan.scanout_ticket_selected == 1 &&
                           scanout_plan.scanout_target_selected == 1 &&
                           scanout_plan.scanout_buffer_attached == 0 &&
                           scanout_plan.scanout_buffer_submitted == 0 &&
                           scanout_plan.display_mode_committed == 0,
                       "credential scanout plan should remain declarative");
  fails += expect_true(scanout_plan.vsync_submitted == 0 &&
                           scanout_plan.vsync_wait_submitted == 0 &&
                           scanout_plan.vsync_fence_armed == 0 &&
                           scanout_plan.schedule_submitted == 0 &&
                           scanout_plan.present_submitted == 0 &&
                           scanout_plan.damage_submitted == 0 &&
                           scanout_plan.compositor_damage_submitted == 0 &&
                           scanout_plan.frame_timer_armed == 0 &&
                           scanout_plan.page_flip_submitted == 0,
                       "credential scanout plan must not submit upstream GUI work");
  fails += expect_true(scanout_plan.schedule_incremental_allowed == 1 &&
                           scanout_plan.full_schedule_required == 0 &&
                           scanout_plan.schedule_cache_allowed == 1 &&
                           scanout_plan.schedule_reuse_allowed == 1 &&
                           scanout_plan.schedule_cache_hit == 0,
                       "credential scanout plan should preserve scalable planning");
  fails += expect_true(scanout_plan.scanout_credential_panel == 1 &&
                           scanout_plan.scanout_credential_input == 1 &&
                           scanout_plan.scanout_credential_focus == 1,
                       "credential scanout plan should mark credential widgets");
  fails += expect_true(scanout_plan.submit_callback_bound == 0 &&
                           scanout_plan.auth_callback_bound == 0 &&
                           scanout_plan.submit_enabled == 0 &&
                           scanout_plan.auth_attempt_allowed == 0 &&
                           scanout_plan.raw_secret_exposed == 0 &&
                           scanout_plan.masked_text_exposed == 0 &&
                           scanout_plan.length_redacted == 1,
                       "credential scanout plan must stay redacted and auth-disabled");
  fails += expect_true(strings_equal(scanout_plan.scanout_ticket,
                                     "credential-screen-scanout-ticket") &&
                           strings_equal(scanout_plan.vsync_ticket,
                                         "credential-screen-vsync-ticket") &&
                           strings_equal(scanout_plan.scanout_policy,
                                         "incremental-scanout-declarative") &&
                           strings_equal(scanout_plan.state,
                                         "scanout-credential-ready"),
                       "credential scanout plan should report scanout ticket");
  return fails;
}

static int test_loginwindow_credential_screen_scanout_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_scanout_plan scanout_plan;

  fails += expect_true(build_loginwindow_credential_screen_scanout_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &scanout_plan) == 0,
                       "credential scanout plan recovery should build");
  fails += expect_true(scanout_plan.scanout_plan_safe == 1 &&
                           scanout_plan.scanout_allowed == 1 &&
                           scanout_plan.scanout_submitted == 0 &&
                           scanout_plan.scanout_text_recovery == 1 &&
                           scanout_plan.scanout_text_login == 1 &&
                           scanout_plan.scanout_credential_focus == 0,
                       "credential scanout plan recovery should mark text recovery");
  fails += expect_true(scanout_plan.scanout_buffer_attached == 0 &&
                           scanout_plan.display_mode_committed == 0 &&
                           scanout_plan.vsync_fence_armed == 0 &&
                           scanout_plan.page_flip_submitted == 0 &&
                           scanout_plan.submit_enabled == 0 &&
                           scanout_plan.auth_attempt_allowed == 0,
                       "credential scanout plan recovery must not submit real display work");
  fails += expect_true(strings_equal(scanout_plan.scanout_ticket,
                                     "text-recovery-scanout-ticket") &&
                           strings_equal(scanout_plan.compositor_target,
                                         "text-recovery-scanout") &&
                           strings_equal(scanout_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential scanout plan recovery should report recovery ticket");

  fails += expect_true(build_loginwindow_credential_screen_scanout_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &scanout_plan) == 0,
                       "credential scanout plan resume should build");
  fails += expect_true(scanout_plan.scanout_plan_safe == 1 &&
                           scanout_plan.scanout_text_login_resume == 1 &&
                           scanout_plan.session_reset_required == 1 &&
                           scanout_plan.login_screen_rerender_required == 1 &&
                           scanout_plan.schedule_reuse_allowed == 0 &&
                           scanout_plan.schedule_cache_allowed == 0 &&
                           scanout_plan.full_schedule_required == 1 &&
                           scanout_plan.schedule_incremental_allowed == 0,
                       "credential scanout plan resume should require full planning");
  fails += expect_true(scanout_plan.scanout_submitted == 0 &&
                           scanout_plan.scanout_buffer_submitted == 0 &&
                           scanout_plan.display_mode_committed == 0 &&
                           scanout_plan.vsync_submitted == 0 &&
                           scanout_plan.schedule_submitted == 0 &&
                           scanout_plan.present_submitted == 0 &&
                           scanout_plan.damage_submitted == 0 &&
                           scanout_plan.submit_enabled == 0 &&
                           scanout_plan.auth_attempt_allowed == 0,
                       "credential scanout plan resume must keep GUI auth disabled");
  fails += expect_true(strings_equal(scanout_plan.scanout_ticket,
                                     "text-login-resume-scanout-ticket") &&
                           strings_equal(scanout_plan.scanout_policy,
                                         "full-scanout-declarative") &&
                           strings_equal(scanout_plan.state,
                                         "scanout-resume-ready"),
                       "credential scanout plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_scanout_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_scanout_plan scanout_plan;

  fails += expect_true(build_loginwindow_credential_screen_scanout_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &scanout_plan) == 0,
                       "credential scanout plan submit should build");
  fails += expect_true(scanout_plan.scanout_plan_safe == 1 &&
                           scanout_plan.submit_requested == 1 &&
                           scanout_plan.scanout_text_login_fallback == 1 &&
                           scanout_plan.action_allowed == 0 &&
                           scanout_plan.action_blocked == 1 &&
                           scanout_plan.input_focus_allowed == 0,
                       "credential scanout plan submit should force text login fallback");
  fails += expect_true(scanout_plan.scanout_allowed == 1 &&
                           scanout_plan.scanout_submitted == 0 &&
                           scanout_plan.scanout_buffer_attached == 0 &&
                           scanout_plan.scanout_buffer_submitted == 0 &&
                           scanout_plan.display_mode_committed == 0 &&
                           scanout_plan.vsync_submitted == 0 &&
                           scanout_plan.page_flip_submitted == 0 &&
                           scanout_plan.submit_callback_bound == 0 &&
                           scanout_plan.auth_callback_bound == 0 &&
                           scanout_plan.submit_enabled == 0 &&
                           scanout_plan.auth_attempt_allowed == 0,
                       "credential scanout plan submit must stay declarative");
  fails += expect_true(strings_equal(scanout_plan.scanout_ticket,
                                     "text-login-fallback-scanout-ticket") &&
                           strings_equal(scanout_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(scanout_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential scanout plan submit should explain fallback");

  fails += expect_true(build_loginwindow_credential_screen_scanout_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &scanout_plan) == 0,
                       "credential scanout plan unknown should build");
  fails += expect_true(scanout_plan.scanout_plan_safe == 1 &&
                           scanout_plan.scanout_text_login_fallback == 1 &&
                           scanout_plan.action_allowed == 0 &&
                           scanout_plan.action_blocked == 1,
                       "credential scanout plan unknown should force text login fallback");
  fails += expect_true(strings_equal(scanout_plan.scanout_ticket,
                                     "text-login-fallback-scanout-ticket") &&
                           strings_equal(scanout_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential scanout plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_scanout_plan_fails_closed_for_unsafe_or_missing_vsync_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_vsync_plan vsync_plan;
  struct login_window_credential_screen_scanout_plan scanout_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_scanout_plan_build(
                           NULL, &scanout_plan) == 0,
                       "credential scanout plan missing vsync plan should build fail-closed state");
  fails += expect_true(scanout_plan.vsync_plan_available == 0 &&
                           scanout_plan.vsync_plan_safe == 0 &&
                           scanout_plan.scanout_plan_safe == 0 &&
                           scanout_plan.route_selected == 0 &&
                           scanout_plan.route_blocked == 1,
                       "credential scanout plan missing vsync plan should block scanout plan");
  fails += expect_true(scanout_plan.scanout_allowed == 0 &&
                           scanout_plan.scanout_submitted == 0 &&
                           scanout_plan.scanout_buffer_attached == 0 &&
                           scanout_plan.display_mode_committed == 0 &&
                           scanout_plan.vsync_submitted == 0 &&
                           scanout_plan.schedule_submitted == 0 &&
                           scanout_plan.present_submitted == 0 &&
                           scanout_plan.damage_submitted == 0 &&
                           scanout_plan.page_flip_submitted == 0 &&
                           scanout_plan.scanout_text_login_fallback == 1 &&
                           scanout_plan.submit_enabled == 0 &&
                           scanout_plan.auth_attempt_allowed == 0,
                       "credential scanout plan missing vsync plan must stay redacted");
  fails += expect_true(strings_equal(scanout_plan.scanout_ticket,
                                     "text-login-fallback-scanout-ticket") &&
                           strings_equal(scanout_plan.event_type,
                                         "credential-screen-scanout-plan-unavailable") &&
                           strings_equal(scanout_plan.blocked_reason,
                                         "vsync-plan-unavailable"),
                       "credential scanout plan missing vsync plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_vsync_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &vsync_plan) == 0,
                       "credential scanout plan unsafe vsync source should build");
  fails += expect_true(login_window_credential_screen_scanout_plan_build(
                           &vsync_plan, &scanout_plan) == 0,
                       "credential scanout plan unsafe vsync plan should build blocked state");
  fails += expect_true(scanout_plan.vsync_plan_available == 1 &&
                           scanout_plan.vsync_plan_safe == 0 &&
                           scanout_plan.scanout_plan_safe == 0 &&
                           scanout_plan.route_selected == 0 &&
                           scanout_plan.route_blocked == 1,
                       "credential scanout plan unsafe vsync plan should block scanout plan");
  fails += expect_true(scanout_plan.scanout_allowed == 0 &&
                           scanout_plan.scanout_submitted == 0 &&
                           scanout_plan.scanout_credential_focus == 0 &&
                           scanout_plan.scanout_text_login_fallback == 1 &&
                           scanout_plan.submit_enabled == 0 &&
                           scanout_plan.auth_attempt_allowed == 0,
                       "credential scanout plan unsafe vsync plan must force text login fallback");
  fails += expect_true(strings_equal(scanout_plan.scanout_ticket,
                                     "text-login-fallback-scanout-ticket") &&
                           strings_equal(scanout_plan.event_type,
                                         "credential-screen-scanout-plan-unsafe") &&
                           strings_equal(scanout_plan.blocked_reason,
                                         "credential-scanout-plan-unsafe"),
                       "credential scanout plan unsafe vsync plan should force text login");

  fails += expect_true(build_loginwindow_credential_screen_vsync_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'y', 0, 0, 0,
                           1, &vsync_plan) == 0,
                       "credential scanout plan submitted vsync source should build");
  vsync_plan.vsync_submitted = 1;
  vsync_plan.vsync_wait_allowed = 1;
  vsync_plan.vsync_fence_armed = 1;
  vsync_plan.page_flip_allowed = 1;
  fails += expect_true(login_window_credential_screen_scanout_plan_build(
                           &vsync_plan, &scanout_plan) == 0,
                       "credential scanout plan submitted vsync should fail closed");
  fails += expect_true(scanout_plan.scanout_plan_safe == 0 &&
                           scanout_plan.scanout_allowed == 0 &&
                           scanout_plan.scanout_submitted == 0 &&
                           scanout_plan.scanout_buffer_attached == 0 &&
                           scanout_plan.vsync_submitted == 0 &&
                           scanout_plan.vsync_wait_allowed == 0 &&
                           scanout_plan.vsync_fence_armed == 0 &&
                           scanout_plan.page_flip_allowed == 0 &&
                           scanout_plan.page_flip_submitted == 0 &&
                           scanout_plan.submit_enabled == 0 &&
                           scanout_plan.auth_attempt_allowed == 0,
                       "credential scanout plan must not copy unsafe submitted state");
  return fails;
}


int build_loginwindow_credential_screen_display_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_display_plan *display_plan) {
  struct login_window_credential_screen_scanout_plan scanout_plan;

  if (build_loginwindow_credential_screen_scanout_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &scanout_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_display_plan_build(&scanout_plan,
                                                           display_plan);
}

static int test_loginwindow_credential_screen_display_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_display_plan display_plan;

  fails += expect_true(build_loginwindow_credential_screen_display_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'p', 0, 0, 0,
                           1, &display_plan) == 0,
                       "credential display plan edit should build");
  fails += expect_true(display_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_DISPLAY_PLAN_VERSION,
                       "credential display plan should expose stable version");
  fails += expect_true(display_plan.scanout_plan_available == 1 &&
                           display_plan.scanout_plan_safe == 1 &&
                           display_plan.display_plan_safe == 1,
                       "credential display plan should require safe scanout plan");
  fails += expect_true(display_plan.display_allowed == 1 &&
                           display_plan.display_submitted == 0 &&
                           display_plan.display_ticket_selected == 1 &&
                           display_plan.display_target_selected == 1 &&
                           display_plan.display_buffer_attached == 0 &&
                           display_plan.display_buffer_submitted == 0 &&
                           display_plan.display_mode_committed == 0 &&
                           display_plan.display_flip_allowed == 0 &&
                           display_plan.display_flip_submitted == 0,
                       "credential display plan should remain declarative");
  fails += expect_true(display_plan.scanout_submitted == 0 &&
                           display_plan.scanout_buffer_attached == 0 &&
                           display_plan.scanout_buffer_submitted == 0 &&
                           display_plan.vsync_submitted == 0 &&
                           display_plan.vsync_fence_armed == 0 &&
                           display_plan.schedule_submitted == 0 &&
                           display_plan.present_submitted == 0 &&
                           display_plan.damage_submitted == 0 &&
                           display_plan.page_flip_submitted == 0,
                       "credential display plan must not submit upstream GUI work");
  fails += expect_true(display_plan.schedule_incremental_allowed == 1 &&
                           display_plan.full_schedule_required == 0 &&
                           display_plan.schedule_cache_allowed == 1 &&
                           display_plan.schedule_reuse_allowed == 1 &&
                           display_plan.schedule_cache_hit == 0,
                       "credential display plan should preserve scalable planning");
  fails += expect_true(display_plan.display_credential_panel == 1 &&
                           display_plan.display_credential_input == 1 &&
                           display_plan.display_credential_focus == 1,
                       "credential display plan should mark credential widgets");
  fails += expect_true(display_plan.submit_callback_bound == 0 &&
                           display_plan.auth_callback_bound == 0 &&
                           display_plan.submit_enabled == 0 &&
                           display_plan.auth_attempt_allowed == 0 &&
                           display_plan.raw_secret_exposed == 0 &&
                           display_plan.masked_text_exposed == 0 &&
                           display_plan.length_redacted == 1,
                       "credential display plan must stay redacted and auth-disabled");
  fails += expect_true(strings_equal(display_plan.display_ticket,
                                     "credential-screen-display-ticket") &&
                           strings_equal(display_plan.scanout_ticket,
                                         "credential-screen-scanout-ticket") &&
                           strings_equal(display_plan.display_policy,
                                         "incremental-display-declarative") &&
                           strings_equal(display_plan.state,
                                         "display-credential-ready"),
                       "credential display plan should report display ticket");
  return fails;
}

static int test_loginwindow_credential_screen_display_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_display_plan display_plan;

  fails += expect_true(build_loginwindow_credential_screen_display_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &display_plan) == 0,
                       "credential display plan recovery should build");
  fails += expect_true(display_plan.display_plan_safe == 1 &&
                           display_plan.display_allowed == 1 &&
                           display_plan.display_submitted == 0 &&
                           display_plan.display_text_recovery == 1 &&
                           display_plan.display_text_login == 1 &&
                           display_plan.display_credential_focus == 0,
                       "credential display plan recovery should mark text recovery");
  fails += expect_true(display_plan.display_buffer_attached == 0 &&
                           display_plan.display_mode_committed == 0 &&
                           display_plan.display_flip_submitted == 0 &&
                           display_plan.scanout_buffer_attached == 0 &&
                           display_plan.page_flip_submitted == 0 &&
                           display_plan.submit_enabled == 0 &&
                           display_plan.auth_attempt_allowed == 0,
                       "credential display plan recovery must not submit real display work");
  fails += expect_true(strings_equal(display_plan.display_ticket,
                                     "text-recovery-display-ticket") &&
                           strings_equal(display_plan.compositor_target,
                                         "text-recovery-display") &&
                           strings_equal(display_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential display plan recovery should report recovery ticket");

  fails += expect_true(build_loginwindow_credential_screen_display_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &display_plan) == 0,
                       "credential display plan resume should build");
  fails += expect_true(display_plan.display_plan_safe == 1 &&
                           display_plan.display_text_login_resume == 1 &&
                           display_plan.session_reset_required == 1 &&
                           display_plan.login_screen_rerender_required == 1 &&
                           display_plan.schedule_reuse_allowed == 0 &&
                           display_plan.schedule_cache_allowed == 0 &&
                           display_plan.full_schedule_required == 1 &&
                           display_plan.schedule_incremental_allowed == 0,
                       "credential display plan resume should require full planning");
  fails += expect_true(display_plan.display_submitted == 0 &&
                           display_plan.display_buffer_submitted == 0 &&
                           display_plan.display_mode_committed == 0 &&
                           display_plan.display_flip_submitted == 0 &&
                           display_plan.scanout_submitted == 0 &&
                           display_plan.vsync_submitted == 0 &&
                           display_plan.schedule_submitted == 0 &&
                           display_plan.submit_enabled == 0 &&
                           display_plan.auth_attempt_allowed == 0,
                       "credential display plan resume must keep GUI auth disabled");
  fails += expect_true(strings_equal(display_plan.display_ticket,
                                     "text-login-resume-display-ticket") &&
                           strings_equal(display_plan.display_policy,
                                         "full-display-declarative") &&
                           strings_equal(display_plan.state,
                                         "display-resume-ready"),
                       "credential display plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_display_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_display_plan display_plan;

  fails += expect_true(build_loginwindow_credential_screen_display_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &display_plan) == 0,
                       "credential display plan submit should build");
  fails += expect_true(display_plan.display_plan_safe == 1 &&
                           display_plan.submit_requested == 1 &&
                           display_plan.display_text_login_fallback == 1 &&
                           display_plan.action_allowed == 0 &&
                           display_plan.action_blocked == 1 &&
                           display_plan.input_focus_allowed == 0,
                       "credential display plan submit should force text login fallback");
  fails += expect_true(display_plan.display_allowed == 1 &&
                           display_plan.display_submitted == 0 &&
                           display_plan.display_buffer_attached == 0 &&
                           display_plan.display_buffer_submitted == 0 &&
                           display_plan.display_mode_committed == 0 &&
                           display_plan.display_flip_submitted == 0 &&
                           display_plan.scanout_submitted == 0 &&
                           display_plan.page_flip_submitted == 0 &&
                           display_plan.submit_callback_bound == 0 &&
                           display_plan.auth_callback_bound == 0 &&
                           display_plan.submit_enabled == 0 &&
                           display_plan.auth_attempt_allowed == 0,
                       "credential display plan submit must stay declarative");
  fails += expect_true(strings_equal(display_plan.display_ticket,
                                     "text-login-fallback-display-ticket") &&
                           strings_equal(display_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(display_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential display plan submit should explain fallback");

  fails += expect_true(build_loginwindow_credential_screen_display_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &display_plan) == 0,
                       "credential display plan unknown should build");
  fails += expect_true(display_plan.display_plan_safe == 1 &&
                           display_plan.display_text_login_fallback == 1 &&
                           display_plan.action_allowed == 0 &&
                           display_plan.action_blocked == 1,
                       "credential display plan unknown should force text login fallback");
  fails += expect_true(strings_equal(display_plan.display_ticket,
                                     "text-login-fallback-display-ticket") &&
                           strings_equal(display_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential display plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_display_plan_fails_closed_for_unsafe_or_missing_scanout_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_scanout_plan scanout_plan;
  struct login_window_credential_screen_display_plan display_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_display_plan_build(
                           NULL, &display_plan) == 0,
                       "credential display plan missing scanout plan should build fail-closed state");
  fails += expect_true(display_plan.scanout_plan_available == 0 &&
                           display_plan.scanout_plan_safe == 0 &&
                           display_plan.display_plan_safe == 0 &&
                           display_plan.route_selected == 0 &&
                           display_plan.route_blocked == 1,
                       "credential display plan missing scanout plan should block display plan");
  fails += expect_true(display_plan.display_allowed == 0 &&
                           display_plan.display_submitted == 0 &&
                           display_plan.display_buffer_attached == 0 &&
                           display_plan.display_mode_committed == 0 &&
                           display_plan.display_flip_submitted == 0 &&
                           display_plan.scanout_submitted == 0 &&
                           display_plan.vsync_submitted == 0 &&
                           display_plan.schedule_submitted == 0 &&
                           display_plan.page_flip_submitted == 0 &&
                           display_plan.display_text_login_fallback == 1 &&
                           display_plan.submit_enabled == 0 &&
                           display_plan.auth_attempt_allowed == 0,
                       "credential display plan missing scanout plan must stay redacted");
  fails += expect_true(strings_equal(display_plan.display_ticket,
                                     "text-login-fallback-display-ticket") &&
                           strings_equal(display_plan.event_type,
                                         "credential-screen-display-plan-unavailable") &&
                           strings_equal(display_plan.blocked_reason,
                                         "scanout-plan-unavailable"),
                       "credential display plan missing scanout plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_scanout_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &scanout_plan) == 0,
                       "credential display plan unsafe scanout source should build");
  fails += expect_true(login_window_credential_screen_display_plan_build(
                           &scanout_plan, &display_plan) == 0,
                       "credential display plan unsafe scanout plan should build blocked state");
  fails += expect_true(display_plan.scanout_plan_available == 1 &&
                           display_plan.scanout_plan_safe == 0 &&
                           display_plan.display_plan_safe == 0 &&
                           display_plan.route_selected == 0 &&
                           display_plan.route_blocked == 1,
                       "credential display plan unsafe scanout plan should block display plan");
  fails += expect_true(display_plan.display_allowed == 0 &&
                           display_plan.display_submitted == 0 &&
                           display_plan.display_credential_focus == 0 &&
                           display_plan.display_text_login_fallback == 1 &&
                           display_plan.submit_enabled == 0 &&
                           display_plan.auth_attempt_allowed == 0,
                       "credential display plan unsafe scanout plan must force text login fallback");
  fails += expect_true(strings_equal(display_plan.display_ticket,
                                     "text-login-fallback-display-ticket") &&
                           strings_equal(display_plan.event_type,
                                         "credential-screen-display-plan-unsafe") &&
                           strings_equal(display_plan.blocked_reason,
                                         "credential-display-plan-unsafe"),
                       "credential display plan unsafe scanout plan should force text login");

  fails += expect_true(build_loginwindow_credential_screen_scanout_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'y', 0, 0, 0,
                           1, &scanout_plan) == 0,
                       "credential display plan submitted scanout source should build");
  scanout_plan.scanout_submitted = 1;
  scanout_plan.scanout_buffer_attached = 1;
  scanout_plan.display_mode_committed = 1;
  scanout_plan.page_flip_allowed = 1;
  fails += expect_true(login_window_credential_screen_display_plan_build(
                           &scanout_plan, &display_plan) == 0,
                       "credential display plan submitted scanout should fail closed");
  fails += expect_true(display_plan.display_plan_safe == 0 &&
                           display_plan.display_allowed == 0 &&
                           display_plan.display_submitted == 0 &&
                           display_plan.display_buffer_attached == 0 &&
                           display_plan.display_mode_committed == 0 &&
                           display_plan.display_flip_allowed == 0 &&
                           display_plan.scanout_submitted == 0 &&
                           display_plan.scanout_buffer_attached == 0 &&
                           display_plan.page_flip_allowed == 0 &&
                           display_plan.page_flip_submitted == 0 &&
                           display_plan.submit_enabled == 0 &&
                           display_plan.auth_attempt_allowed == 0,
                       "credential display plan must not copy unsafe submitted state");
  return fails;
}

int test_login_runtime_credential_scanout_display_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_scanout_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_scanout_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_scanout_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_scanout_plan_fails_closed_for_unsafe_or_missing_vsync_plan();
  fails += test_loginwindow_credential_screen_display_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_display_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_display_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_display_plan_fails_closed_for_unsafe_or_missing_scanout_plan();
  return fails;
}
