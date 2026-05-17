/*
 * tests/auth/test_login_runtime_credential_frame_surface.c
 *
 * Credential screen frame plan + surface plan coverage for the
 * `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-15 monolith
 * refactor (PR D.13 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_frame_plan_build`: 5 tests
 *     covering the credential widgets frame + the text-recovery
 *     frame + the resume-text-login frame + the submit/unknown
 *     fallback frame + the missing-or-unsafe activation plan
 *     fail-closed default.
 *   - `login_window_credential_screen_surface_plan_build`: 5 tests
 *     covering the credential widgets surface + the text-recovery
 *     surface + the resume-text-login surface + the submit/unknown
 *     fallback surface + the missing-or-unsafe frame plan
 *     fail-closed default.
 *
 * Also exposes shared helpers
 * `build_loginwindow_credential_screen_frame_plan_for_action` and
 * `build_loginwindow_credential_screen_surface_plan_for_action`,
 * used by later companion files that chain on top of the
 * frame/surface stages (compositor, damage, present, schedule, ...).
 *
 * The companion entry `test_login_runtime_credential_frame_surface_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_frame_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_frame_plan *frame_plan) {
  struct login_window_credential_screen_activation_plan activation_plan;

  if (build_loginwindow_credential_screen_activation_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &activation_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_frame_plan_build(&activation_plan,
                                                         frame_plan);
}

static int test_loginwindow_credential_screen_frame_plan_frames_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_frame_plan frame_plan;

  fails += expect_true(build_loginwindow_credential_screen_frame_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'e', 0, 0, 0,
                           1, &frame_plan) == 0,
                       "credential frame plan edit should build");
  fails += expect_true(frame_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_FRAME_PLAN_VERSION,
                       "credential frame plan should expose stable version");
  fails += expect_true(frame_plan.activation_plan_available == 1 &&
                           frame_plan.activation_plan_safe == 1 &&
                           frame_plan.frame_plan_safe == 1,
                       "credential frame plan edit should require safe activation plan");
  fails += expect_true(frame_plan.window_frame_allowed == 1 &&
                           frame_plan.window_frame_rendered == 0 &&
                           frame_plan.frame_ticket_selected == 1,
                       "credential frame plan edit should remain declarative");
  fails += expect_true(frame_plan.frame_credential_panel == 1 &&
                           frame_plan.frame_credential_input == 1 &&
                           frame_plan.frame_credential_focus == 1,
                       "credential frame plan edit should frame credential widgets");
  fails += expect_true(frame_plan.submit_callback_bound == 0 &&
                           frame_plan.auth_callback_bound == 0 &&
                           frame_plan.submit_enabled == 0 &&
                           frame_plan.auth_attempt_allowed == 0,
                       "credential frame plan edit must not bind auth callbacks");
  fails += expect_true(frame_plan.raw_secret_exposed == 0 &&
                           frame_plan.masked_text_exposed == 0 &&
                           frame_plan.length_redacted == 1,
                       "credential frame plan edit must stay redacted");
  fails += expect_true(strings_equal(frame_plan.frame_ticket,
                                     "credential-screen-frame-ticket") &&
                           strings_equal(frame_plan.activation_ticket,
                                         "credential-screen-activation-ticket") &&
                           strings_equal(frame_plan.state,
                                         "frame-credential-ready"),
                       "credential frame plan edit should report frame state");
  return fails;
}

static int test_loginwindow_credential_screen_frame_plan_frames_text_recovery(void) {
  int fails = 0;
  struct login_window_credential_screen_frame_plan frame_plan;

  fails += expect_true(build_loginwindow_credential_screen_frame_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &frame_plan) == 0,
                       "credential frame plan recovery should build");
  fails += expect_true(frame_plan.frame_plan_safe == 1 &&
                           frame_plan.window_frame_allowed == 1 &&
                           frame_plan.window_frame_rendered == 0 &&
                           frame_plan.frame_text_recovery == 1 &&
                           frame_plan.frame_text_login == 1,
                       "credential frame plan recovery should frame text recovery");
  fails += expect_true(frame_plan.frame_credential_input == 0 &&
                           frame_plan.frame_credential_focus == 0 &&
                           frame_plan.input_focus_allowed == 0,
                       "credential frame plan recovery should block credential focus");
  fails += expect_true(frame_plan.submit_callback_bound == 0 &&
                           frame_plan.auth_callback_bound == 0 &&
                           frame_plan.submit_enabled == 0 &&
                           frame_plan.auth_attempt_allowed == 0,
                       "credential frame plan recovery must keep auth disabled");
  fails += expect_true(strings_equal(frame_plan.frame_ticket,
                                     "text-recovery-frame-ticket") &&
                           strings_equal(frame_plan.primary_action,
                                         "open-text-recovery") &&
                           strings_equal(frame_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential frame plan recovery should report recovery frame");
  return fails;
}

static int test_loginwindow_credential_screen_frame_plan_frames_resume_text_login(void) {
  int fails = 0;
  struct login_window_credential_screen_frame_plan frame_plan;

  fails += expect_true(build_loginwindow_credential_screen_frame_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &frame_plan) == 0,
                       "credential frame plan resume should build");
  fails += expect_true(frame_plan.frame_plan_safe == 1 &&
                           frame_plan.frame_text_login == 1 &&
                           frame_plan.frame_text_login_resume == 1,
                       "credential frame plan resume should frame text login resume");
  fails += expect_true(frame_plan.session_reset_required == 1 &&
                           frame_plan.login_screen_rerender_required == 1 &&
                           frame_plan.frame_credential_focus == 0,
                       "credential frame plan resume should require reset and rerender");
  fails += expect_true(frame_plan.submit_callback_bound == 0 &&
                           frame_plan.auth_callback_bound == 0 &&
                           frame_plan.submit_enabled == 0 &&
                           frame_plan.auth_attempt_allowed == 0,
                       "credential frame plan resume must keep GUI auth disabled");
  fails += expect_true(strings_equal(frame_plan.frame_ticket,
                                     "text-login-resume-frame-ticket") &&
                           strings_equal(frame_plan.primary_action,
                                         "resume-text-login") &&
                           strings_equal(frame_plan.state,
                                         "frame-resume-ready"),
                       "credential frame plan resume should report resume frame");
  return fails;
}

static int test_loginwindow_credential_screen_frame_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_frame_plan frame_plan;

  fails += expect_true(build_loginwindow_credential_screen_frame_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &frame_plan) == 0,
                       "credential frame plan submit should build");
  fails += expect_true(frame_plan.frame_plan_safe == 1 &&
                           frame_plan.submit_requested == 1 &&
                           frame_plan.frame_text_login_fallback == 1,
                       "credential frame plan submit should frame text login fallback");
  fails += expect_true(frame_plan.action_allowed == 0 &&
                           frame_plan.action_blocked == 1 &&
                           frame_plan.input_focus_allowed == 0 &&
                           frame_plan.frame_credential_focus == 0,
                       "credential frame plan submit should block GUI action");
  fails += expect_true(frame_plan.window_frame_allowed == 1 &&
                           frame_plan.window_frame_rendered == 0 &&
                           frame_plan.submit_callback_bound == 0 &&
                           frame_plan.auth_callback_bound == 0 &&
                           frame_plan.submit_enabled == 0 &&
                           frame_plan.auth_attempt_allowed == 0 &&
                           frame_plan.raw_secret_exposed == 0 &&
                           frame_plan.masked_text_exposed == 0,
                       "credential frame plan submit must stay declarative and redacted");
  fails += expect_true(strings_equal(frame_plan.frame_ticket,
                                     "text-login-fallback-frame-ticket") &&
                           strings_equal(frame_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(frame_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential frame plan submit should explain fallback frame");

  fails += expect_true(build_loginwindow_credential_screen_frame_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &frame_plan) == 0,
                       "credential frame plan unknown should build");
  fails += expect_true(frame_plan.frame_plan_safe == 1 &&
                           frame_plan.frame_text_login_fallback == 1 &&
                           frame_plan.action_allowed == 0 &&
                           frame_plan.action_blocked == 1,
                       "credential frame plan unknown should force text login fallback");
  fails += expect_true(strings_equal(frame_plan.frame_ticket,
                                     "text-login-fallback-frame-ticket") &&
                           strings_equal(frame_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential frame plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_frame_plan_fails_closed_for_unsafe_or_missing_activation_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_frame_plan frame_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_frame_plan_build(
                           NULL, &frame_plan) == 0,
                       "credential frame plan missing activation plan should build fail-closed state");
  fails += expect_true(frame_plan.activation_plan_available == 0 &&
                           frame_plan.activation_plan_safe == 0 &&
                           frame_plan.frame_plan_safe == 0 &&
                           frame_plan.route_selected == 0 &&
                           frame_plan.route_blocked == 1,
                       "credential frame plan missing activation plan should block frame plan");
  fails += expect_true(frame_plan.window_frame_allowed == 0 &&
                           frame_plan.window_frame_rendered == 0 &&
                           frame_plan.frame_text_login == 1 &&
                           frame_plan.frame_text_login_fallback == 1 &&
                           frame_plan.submit_callback_bound == 0 &&
                           frame_plan.auth_callback_bound == 0 &&
                           frame_plan.submit_enabled == 0 &&
                           frame_plan.auth_attempt_allowed == 0,
                       "credential frame plan missing activation plan must stay redacted");
  fails += expect_true(strings_equal(frame_plan.frame_ticket,
                                     "text-login-fallback-frame-ticket") &&
                           strings_equal(frame_plan.event_type,
                                         "credential-screen-frame-plan-unavailable") &&
                           strings_equal(frame_plan.blocked_reason,
                                         "activation-plan-unavailable"),
                       "credential frame plan missing activation plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_frame_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &frame_plan) == 0,
                       "credential frame plan unsafe activation plan should build blocked state");
  fails += expect_true(frame_plan.activation_plan_available == 1 &&
                           frame_plan.activation_plan_safe == 0 &&
                           frame_plan.frame_plan_safe == 0 &&
                           frame_plan.route_selected == 0 &&
                           frame_plan.route_blocked == 1,
                       "credential frame plan unsafe activation plan should block frame plan");
  fails += expect_true(frame_plan.action_allowed == 0 &&
                           frame_plan.action_blocked == 1 &&
                           frame_plan.input_focus_allowed == 0 &&
                           frame_plan.frame_credential_focus == 0 &&
                           frame_plan.frame_text_login_fallback == 1,
                       "credential frame plan unsafe activation plan must force text login fallback");
  fails += expect_true(strings_equal(frame_plan.frame_ticket,
                                     "text-login-fallback-frame-ticket") &&
                           strings_equal(frame_plan.event_type,
                                         "credential-screen-frame-plan-unsafe") &&
                           strings_equal(frame_plan.blocked_reason,
                                         "credential-frame-plan-unsafe"),
                       "credential frame plan unsafe activation plan should force text login");
  return fails;
}

int build_loginwindow_credential_screen_surface_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_surface_plan *surface_plan) {
  struct login_window_credential_screen_frame_plan frame_plan;

  if (build_loginwindow_credential_screen_frame_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &frame_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_surface_plan_build(&frame_plan,
                                                           surface_plan);
}

static int test_loginwindow_credential_screen_surface_plan_surfaces_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_surface_plan surface_plan;

  fails += expect_true(build_loginwindow_credential_screen_surface_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'e', 0, 0, 0,
                           1, &surface_plan) == 0,
                       "credential surface plan edit should build");
  fails += expect_true(surface_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_SURFACE_PLAN_VERSION,
                       "credential surface plan should expose stable version");
  fails += expect_true(surface_plan.frame_plan_available == 1 &&
                           surface_plan.frame_plan_safe == 1 &&
                           surface_plan.surface_plan_safe == 1,
                       "credential surface plan edit should require safe frame plan");
  fails += expect_true(surface_plan.window_surface_allowed == 1 &&
                           surface_plan.window_surface_submitted == 0 &&
                           surface_plan.compositor_damage_submitted == 0 &&
                           surface_plan.surface_ticket_selected == 1,
                       "credential surface plan edit should remain declarative");
  fails += expect_true(surface_plan.surface_credential_panel == 1 &&
                           surface_plan.surface_credential_input == 1 &&
                           surface_plan.surface_credential_focus == 1,
                       "credential surface plan edit should surface credential widgets");
  fails += expect_true(surface_plan.surface_reuse_allowed == 1 &&
                           surface_plan.surface_cache_allowed == 1 &&
                           surface_plan.compositor_damage_planned == 1 &&
                           surface_plan.full_damage_required == 0,
                       "credential surface plan edit should prepare scalable incremental damage");
  fails += expect_true(surface_plan.submit_callback_bound == 0 &&
                           surface_plan.auth_callback_bound == 0 &&
                           surface_plan.submit_enabled == 0 &&
                           surface_plan.auth_attempt_allowed == 0,
                       "credential surface plan edit must not bind auth callbacks");
  fails += expect_true(surface_plan.raw_secret_exposed == 0 &&
                           surface_plan.masked_text_exposed == 0 &&
                           surface_plan.length_redacted == 1,
                       "credential surface plan edit must stay redacted");
  fails += expect_true(strings_equal(surface_plan.surface_ticket,
                                     "credential-screen-surface-ticket") &&
                           strings_equal(surface_plan.frame_ticket,
                                         "credential-screen-frame-ticket") &&
                           strings_equal(surface_plan.state,
                                         "surface-credential-ready"),
                       "credential surface plan edit should report surface state");
  return fails;
}

static int test_loginwindow_credential_screen_surface_plan_surfaces_text_recovery(void) {
  int fails = 0;
  struct login_window_credential_screen_surface_plan surface_plan;

  fails += expect_true(build_loginwindow_credential_screen_surface_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &surface_plan) == 0,
                       "credential surface plan recovery should build");
  fails += expect_true(surface_plan.surface_plan_safe == 1 &&
                           surface_plan.window_surface_allowed == 1 &&
                           surface_plan.window_surface_submitted == 0 &&
                           surface_plan.surface_text_recovery == 1 &&
                           surface_plan.surface_text_login == 1,
                       "credential surface plan recovery should surface text recovery");
  fails += expect_true(surface_plan.surface_credential_input == 0 &&
                           surface_plan.surface_credential_focus == 0 &&
                           surface_plan.input_focus_allowed == 0,
                       "credential surface plan recovery should block credential focus");
  fails += expect_true(surface_plan.submit_callback_bound == 0 &&
                           surface_plan.auth_callback_bound == 0 &&
                           surface_plan.submit_enabled == 0 &&
                           surface_plan.auth_attempt_allowed == 0 &&
                           surface_plan.compositor_damage_submitted == 0,
                       "credential surface plan recovery must keep compositor and auth disabled");
  fails += expect_true(strings_equal(surface_plan.surface_ticket,
                                     "text-recovery-surface-ticket") &&
                           strings_equal(surface_plan.compositor_target,
                                         "text-recovery-surface") &&
                           strings_equal(surface_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential surface plan recovery should report recovery surface");
  return fails;
}

static int test_loginwindow_credential_screen_surface_plan_surfaces_resume_text_login(void) {
  int fails = 0;
  struct login_window_credential_screen_surface_plan surface_plan;

  fails += expect_true(build_loginwindow_credential_screen_surface_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &surface_plan) == 0,
                       "credential surface plan resume should build");
  fails += expect_true(surface_plan.surface_plan_safe == 1 &&
                           surface_plan.surface_text_login == 1 &&
                           surface_plan.surface_text_login_resume == 1,
                       "credential surface plan resume should surface text login resume");
  fails += expect_true(surface_plan.session_reset_required == 1 &&
                           surface_plan.login_screen_rerender_required == 1 &&
                           surface_plan.surface_reuse_allowed == 0 &&
                           surface_plan.full_damage_required == 1,
                       "credential surface plan resume should require reset-aware rerender planning");
  fails += expect_true(surface_plan.window_surface_submitted == 0 &&
                           surface_plan.compositor_damage_submitted == 0 &&
                           surface_plan.submit_enabled == 0 &&
                           surface_plan.auth_attempt_allowed == 0,
                       "credential surface plan resume must keep GUI auth disabled");
  fails += expect_true(strings_equal(surface_plan.surface_ticket,
                                     "text-login-resume-surface-ticket") &&
                           strings_equal(surface_plan.damage_policy,
                                         "full-rerender-declarative") &&
                           strings_equal(surface_plan.state,
                                         "surface-resume-ready"),
                       "credential surface plan resume should report resume surface");
  return fails;
}

static int test_loginwindow_credential_screen_surface_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_surface_plan surface_plan;

  fails += expect_true(build_loginwindow_credential_screen_surface_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &surface_plan) == 0,
                       "credential surface plan submit should build");
  fails += expect_true(surface_plan.surface_plan_safe == 1 &&
                           surface_plan.submit_requested == 1 &&
                           surface_plan.surface_text_login_fallback == 1,
                       "credential surface plan submit should surface text login fallback");
  fails += expect_true(surface_plan.action_allowed == 0 &&
                           surface_plan.action_blocked == 1 &&
                           surface_plan.input_focus_allowed == 0 &&
                           surface_plan.surface_credential_focus == 0,
                       "credential surface plan submit should block GUI action");
  fails += expect_true(surface_plan.window_surface_allowed == 1 &&
                           surface_plan.window_surface_submitted == 0 &&
                           surface_plan.compositor_damage_submitted == 0 &&
                           surface_plan.submit_callback_bound == 0 &&
                           surface_plan.auth_callback_bound == 0 &&
                           surface_plan.submit_enabled == 0 &&
                           surface_plan.auth_attempt_allowed == 0,
                       "credential surface plan submit must stay declarative and redacted");
  fails += expect_true(strings_equal(surface_plan.surface_ticket,
                                     "text-login-fallback-surface-ticket") &&
                           strings_equal(surface_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(surface_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential surface plan submit should explain fallback surface");

  fails += expect_true(build_loginwindow_credential_screen_surface_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &surface_plan) == 0,
                       "credential surface plan unknown should build");
  fails += expect_true(surface_plan.surface_plan_safe == 1 &&
                           surface_plan.surface_text_login_fallback == 1 &&
                           surface_plan.action_allowed == 0 &&
                           surface_plan.action_blocked == 1,
                       "credential surface plan unknown should force text login fallback");
  fails += expect_true(strings_equal(surface_plan.surface_ticket,
                                     "text-login-fallback-surface-ticket") &&
                           strings_equal(surface_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential surface plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_surface_plan_fails_closed_for_unsafe_or_missing_frame_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_surface_plan surface_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_surface_plan_build(
                           NULL, &surface_plan) == 0,
                       "credential surface plan missing frame plan should build fail-closed state");
  fails += expect_true(surface_plan.frame_plan_available == 0 &&
                           surface_plan.frame_plan_safe == 0 &&
                           surface_plan.surface_plan_safe == 0 &&
                           surface_plan.route_selected == 0 &&
                           surface_plan.route_blocked == 1,
                       "credential surface plan missing frame plan should block surface plan");
  fails += expect_true(surface_plan.window_surface_allowed == 0 &&
                           surface_plan.window_surface_submitted == 0 &&
                           surface_plan.compositor_damage_submitted == 0 &&
                           surface_plan.surface_text_login == 1 &&
                           surface_plan.surface_text_login_fallback == 1 &&
                           surface_plan.submit_callback_bound == 0 &&
                           surface_plan.auth_callback_bound == 0 &&
                           surface_plan.submit_enabled == 0 &&
                           surface_plan.auth_attempt_allowed == 0,
                       "credential surface plan missing frame plan must stay redacted");
  fails += expect_true(strings_equal(surface_plan.surface_ticket,
                                     "text-login-fallback-surface-ticket") &&
                           strings_equal(surface_plan.event_type,
                                         "credential-screen-surface-plan-unavailable") &&
                           strings_equal(surface_plan.blocked_reason,
                                         "frame-plan-unavailable"),
                       "credential surface plan missing frame plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_surface_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &surface_plan) == 0,
                       "credential surface plan unsafe frame plan should build blocked state");
  fails += expect_true(surface_plan.frame_plan_available == 1 &&
                           surface_plan.frame_plan_safe == 0 &&
                           surface_plan.surface_plan_safe == 0 &&
                           surface_plan.route_selected == 0 &&
                           surface_plan.route_blocked == 1,
                       "credential surface plan unsafe frame plan should block surface plan");
  fails += expect_true(surface_plan.action_allowed == 0 &&
                           surface_plan.action_blocked == 1 &&
                           surface_plan.input_focus_allowed == 0 &&
                           surface_plan.surface_credential_focus == 0 &&
                           surface_plan.surface_text_login_fallback == 1,
                       "credential surface plan unsafe frame plan must force text login fallback");
  fails += expect_true(strings_equal(surface_plan.surface_ticket,
                                     "text-login-fallback-surface-ticket") &&
                           strings_equal(surface_plan.event_type,
                                         "credential-screen-surface-plan-unsafe") &&
                           strings_equal(surface_plan.blocked_reason,
                                         "credential-surface-plan-unsafe"),
                       "credential surface plan unsafe frame plan should force text login");
  return fails;
}

int test_login_runtime_credential_frame_surface_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_frame_plan_frames_credential_widgets();
  fails += test_loginwindow_credential_screen_frame_plan_frames_text_recovery();
  fails += test_loginwindow_credential_screen_frame_plan_frames_resume_text_login();
  fails += test_loginwindow_credential_screen_frame_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_frame_plan_fails_closed_for_unsafe_or_missing_activation_plan();
  fails += test_loginwindow_credential_screen_surface_plan_surfaces_credential_widgets();
  fails += test_loginwindow_credential_screen_surface_plan_surfaces_text_recovery();
  fails += test_loginwindow_credential_screen_surface_plan_surfaces_resume_text_login();
  fails += test_loginwindow_credential_screen_surface_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_surface_plan_fails_closed_for_unsafe_or_missing_frame_plan();
  return fails;
}
