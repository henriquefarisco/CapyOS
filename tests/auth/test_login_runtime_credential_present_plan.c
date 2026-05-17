/*
 * tests/auth/test_login_runtime_credential_present_plan.c
 *
 * Credential screen present plan coverage for the `login_runtime`
 * host test. Carved out of `tests/auth/test_login_runtime.c` at
 * the 2026-05-15 monolith refactor (PR D.15 of the Estagio D
 * dedicated plan) so each host-test translation unit stays under
 * the 900-line layout limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_present_plan_build`: 5 tests
 *     covering the credential widgets present + the text-recovery
 *     present + the resume-text-login present + the submit/unknown
 *     fallback present + the missing-or-unsafe damage plan
 *     fail-closed default.
 *
 * Also exposes shared helper
 * `build_loginwindow_credential_screen_present_plan_for_action`, used
 * by later companion files that chain on top of the present stage
 * (window_schedule, window_vsync, ...).
 *
 * The companion entry `test_login_runtime_credential_present_plan_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_present_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_present_plan *present_plan) {
  struct login_window_credential_screen_damage_plan damage_plan;

  if (build_loginwindow_credential_screen_damage_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &damage_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_present_plan_build(&damage_plan,
                                                           present_plan);
}

static int test_loginwindow_credential_screen_present_plan_presents_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_present_plan present_plan;

  fails += expect_true(build_loginwindow_credential_screen_present_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'p', 0, 0, 0,
                           1, &present_plan) == 0,
                       "credential present plan edit should build");
  fails += expect_true(present_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_PRESENT_PLAN_VERSION,
                       "credential present plan should expose stable version");
  fails += expect_true(present_plan.damage_plan_available == 1 &&
                           present_plan.damage_plan_safe == 1 &&
                           present_plan.present_plan_safe == 1,
                       "credential present plan edit should require safe damage plan");
  fails += expect_true(present_plan.present_allowed == 1 &&
                           present_plan.present_submitted == 0 &&
                           present_plan.damage_submitted == 0 &&
                           present_plan.compositor_damage_submitted == 0 &&
                           present_plan.present_ticket_selected == 1,
                       "credential present plan edit should remain declarative");
  fails += expect_true(present_plan.present_incremental_allowed == 1 &&
                           present_plan.full_present_required == 0 &&
                           present_plan.present_cache_allowed == 1 &&
                           present_plan.present_reuse_allowed == 1 &&
                           present_plan.present_cache_hit == 0,
                       "credential present plan edit should preserve scalable present planning");
  fails += expect_true(present_plan.present_credential_panel == 1 &&
                           present_plan.present_credential_input == 1 &&
                           present_plan.present_credential_focus == 1,
                       "credential present plan edit should mark credential widgets");
  fails += expect_true(present_plan.submit_callback_bound == 0 &&
                           present_plan.auth_callback_bound == 0 &&
                           present_plan.submit_enabled == 0 &&
                           present_plan.auth_attempt_allowed == 0,
                       "credential present plan edit must not bind auth callbacks");
  fails += expect_true(present_plan.raw_secret_exposed == 0 &&
                           present_plan.masked_text_exposed == 0 &&
                           present_plan.length_redacted == 1,
                       "credential present plan edit must stay redacted");
  fails += expect_true(strings_equal(present_plan.present_ticket,
                                     "credential-screen-present-ticket") &&
                           strings_equal(present_plan.damage_ticket,
                                         "credential-screen-damage-ticket") &&
                           strings_equal(present_plan.present_policy,
                                         "incremental-present-declarative") &&
                           strings_equal(present_plan.state,
                                         "present-credential-ready"),
                       "credential present plan edit should report present state");
  return fails;
}

static int test_loginwindow_credential_screen_present_plan_presents_text_recovery(void) {
  int fails = 0;
  struct login_window_credential_screen_present_plan present_plan;

  fails += expect_true(build_loginwindow_credential_screen_present_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &present_plan) == 0,
                       "credential present plan recovery should build");
  fails += expect_true(present_plan.present_plan_safe == 1 &&
                           present_plan.present_allowed == 1 &&
                           present_plan.present_submitted == 0 &&
                           present_plan.present_text_recovery == 1 &&
                           present_plan.present_text_login == 1,
                       "credential present plan recovery should mark text recovery");
  fails += expect_true(present_plan.present_credential_input == 0 &&
                           present_plan.present_credential_focus == 0 &&
                           present_plan.input_focus_allowed == 0,
                       "credential present plan recovery should block credential focus");
  fails += expect_true(present_plan.damage_submitted == 0 &&
                           present_plan.compositor_damage_submitted == 0 &&
                           present_plan.submit_callback_bound == 0 &&
                           present_plan.auth_callback_bound == 0 &&
                           present_plan.submit_enabled == 0 &&
                           present_plan.auth_attempt_allowed == 0,
                       "credential present plan recovery must keep compositor and auth disabled");
  fails += expect_true(strings_equal(present_plan.present_ticket,
                                     "text-recovery-present-ticket") &&
                           strings_equal(present_plan.compositor_target,
                                         "text-recovery-present") &&
                           strings_equal(present_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential present plan recovery should report recovery present ticket");
  return fails;
}

static int test_loginwindow_credential_screen_present_plan_presents_resume_text_login(void) {
  int fails = 0;
  struct login_window_credential_screen_present_plan present_plan;

  fails += expect_true(build_loginwindow_credential_screen_present_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &present_plan) == 0,
                       "credential present plan resume should build");
  fails += expect_true(present_plan.present_plan_safe == 1 &&
                           present_plan.present_text_login == 1 &&
                           present_plan.present_text_login_resume == 1,
                       "credential present plan resume should mark text login resume");
  fails += expect_true(present_plan.session_reset_required == 1 &&
                           present_plan.login_screen_rerender_required == 1 &&
                           present_plan.present_reuse_allowed == 0 &&
                           present_plan.present_cache_allowed == 0 &&
                           present_plan.full_present_required == 1 &&
                           present_plan.present_incremental_allowed == 0,
                       "credential present plan resume should require full present planning");
  fails += expect_true(present_plan.present_submitted == 0 &&
                           present_plan.damage_submitted == 0 &&
                           present_plan.compositor_damage_submitted == 0 &&
                           present_plan.submit_enabled == 0 &&
                           present_plan.auth_attempt_allowed == 0,
                       "credential present plan resume must keep GUI auth disabled");
  fails += expect_true(strings_equal(present_plan.present_ticket,
                                     "text-login-resume-present-ticket") &&
                           strings_equal(present_plan.present_policy,
                                         "full-present-declarative") &&
                           strings_equal(present_plan.state,
                                         "present-resume-ready"),
                       "credential present plan resume should report resume present ticket");
  return fails;
}

static int test_loginwindow_credential_screen_present_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_present_plan present_plan;

  fails += expect_true(build_loginwindow_credential_screen_present_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &present_plan) == 0,
                       "credential present plan submit should build");
  fails += expect_true(present_plan.present_plan_safe == 1 &&
                           present_plan.submit_requested == 1 &&
                           present_plan.present_text_login_fallback == 1,
                       "credential present plan submit should mark text login fallback");
  fails += expect_true(present_plan.action_allowed == 0 &&
                           present_plan.action_blocked == 1 &&
                           present_plan.input_focus_allowed == 0 &&
                           present_plan.present_credential_focus == 0,
                       "credential present plan submit should block GUI action");
  fails += expect_true(present_plan.present_allowed == 1 &&
                           present_plan.present_submitted == 0 &&
                           present_plan.damage_submitted == 0 &&
                           present_plan.compositor_damage_submitted == 0 &&
                           present_plan.submit_callback_bound == 0 &&
                           present_plan.auth_callback_bound == 0 &&
                           present_plan.submit_enabled == 0 &&
                           present_plan.auth_attempt_allowed == 0,
                       "credential present plan submit must stay declarative and redacted");
  fails += expect_true(strings_equal(present_plan.present_ticket,
                                     "text-login-fallback-present-ticket") &&
                           strings_equal(present_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(present_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential present plan submit should explain fallback present ticket");

  fails += expect_true(build_loginwindow_credential_screen_present_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &present_plan) == 0,
                       "credential present plan unknown should build");
  fails += expect_true(present_plan.present_plan_safe == 1 &&
                           present_plan.present_text_login_fallback == 1 &&
                           present_plan.action_allowed == 0 &&
                           present_plan.action_blocked == 1,
                       "credential present plan unknown should force text login fallback");
  fails += expect_true(strings_equal(present_plan.present_ticket,
                                     "text-login-fallback-present-ticket") &&
                           strings_equal(present_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential present plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_present_plan_fails_closed_for_unsafe_or_missing_damage_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_present_plan present_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_present_plan_build(
                           NULL, &present_plan) == 0,
                       "credential present plan missing damage plan should build fail-closed state");
  fails += expect_true(present_plan.damage_plan_available == 0 &&
                           present_plan.damage_plan_safe == 0 &&
                           present_plan.present_plan_safe == 0 &&
                           present_plan.route_selected == 0 &&
                           present_plan.route_blocked == 1,
                       "credential present plan missing damage plan should block present plan");
  fails += expect_true(present_plan.present_allowed == 0 &&
                           present_plan.present_submitted == 0 &&
                           present_plan.damage_submitted == 0 &&
                           present_plan.compositor_damage_submitted == 0 &&
                           present_plan.present_text_login == 1 &&
                           present_plan.present_text_login_fallback == 1 &&
                           present_plan.submit_callback_bound == 0 &&
                           present_plan.auth_callback_bound == 0 &&
                           present_plan.submit_enabled == 0 &&
                           present_plan.auth_attempt_allowed == 0,
                       "credential present plan missing damage plan must stay redacted");
  fails += expect_true(strings_equal(present_plan.present_ticket,
                                     "text-login-fallback-present-ticket") &&
                           strings_equal(present_plan.event_type,
                                         "credential-screen-present-plan-unavailable") &&
                           strings_equal(present_plan.blocked_reason,
                                         "damage-plan-unavailable"),
                       "credential present plan missing damage plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_present_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &present_plan) == 0,
                       "credential present plan unsafe damage plan should build blocked state");
  fails += expect_true(present_plan.damage_plan_available == 1 &&
                           present_plan.damage_plan_safe == 0 &&
                           present_plan.present_plan_safe == 0 &&
                           present_plan.route_selected == 0 &&
                           present_plan.route_blocked == 1,
                       "credential present plan unsafe damage plan should block present plan");
  fails += expect_true(present_plan.action_allowed == 0 &&
                           present_plan.action_blocked == 1 &&
                           present_plan.input_focus_allowed == 0 &&
                           present_plan.present_credential_focus == 0 &&
                           present_plan.present_text_login_fallback == 1,
                       "credential present plan unsafe damage plan must force text login fallback");
  fails += expect_true(strings_equal(present_plan.present_ticket,
                                     "text-login-fallback-present-ticket") &&
                           strings_equal(present_plan.event_type,
                                         "credential-screen-present-plan-unsafe") &&
                           strings_equal(present_plan.blocked_reason,
                                         "credential-present-plan-unsafe"),
                       "credential present plan unsafe damage plan should force text login");
  return fails;
}

int test_login_runtime_credential_present_plan_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_present_plan_presents_credential_widgets();
  fails += test_loginwindow_credential_screen_present_plan_presents_text_recovery();
  fails += test_loginwindow_credential_screen_present_plan_presents_resume_text_login();
  fails += test_loginwindow_credential_screen_present_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_present_plan_fails_closed_for_unsafe_or_missing_damage_plan();
  return fails;
}
