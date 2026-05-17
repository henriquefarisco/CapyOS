/*
 * tests/auth/test_login_runtime_credential_compositor_damage.c
 *
 * Credential screen compositor plan + damage plan coverage for the
 * `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-15 monolith
 * refactor (PR D.14 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_compositor_plan_build`: 5 tests
 *     covering the credential widgets compose + the text-recovery
 *     compose + the resume-text-login compose + the submit/unknown
 *     fallback compose + the missing-or-unsafe surface plan
 *     fail-closed default.
 *   - `login_window_credential_screen_damage_plan_build`: 5 tests
 *     covering the credential widgets damage + the text-recovery
 *     damage + the resume-text-login damage + the submit/unknown
 *     fallback damage + the missing-or-unsafe compositor plan
 *     fail-closed default.
 *
 * Also exposes shared helpers
 * `build_loginwindow_credential_screen_compositor_plan_for_action`
 * and
 * `build_loginwindow_credential_screen_damage_plan_for_action`, used
 * by later companion files that chain on top of the compositor/damage
 * stages (present, schedule, vsync, scanout, display, output, ...).
 *
 * The companion entry `test_login_runtime_credential_compositor_damage_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_compositor_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_compositor_plan *compositor_plan) {
  struct login_window_credential_screen_surface_plan surface_plan;

  if (build_loginwindow_credential_screen_surface_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &surface_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_compositor_plan_build(&surface_plan,
                                                              compositor_plan);
}

static int test_loginwindow_credential_screen_compositor_plan_composes_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_compositor_plan compositor_plan;

  fails += expect_true(build_loginwindow_credential_screen_compositor_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'e', 0, 0, 0,
                           1, &compositor_plan) == 0,
                       "credential compositor plan edit should build");
  fails += expect_true(compositor_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_COMPOSITOR_PLAN_VERSION,
                       "credential compositor plan should expose stable version");
  fails += expect_true(compositor_plan.surface_plan_available == 1 &&
                           compositor_plan.surface_plan_safe == 1 &&
                           compositor_plan.compositor_plan_safe == 1,
                       "credential compositor plan edit should require safe surface plan");
  fails += expect_true(compositor_plan.compositor_surface_allowed == 1 &&
                           compositor_plan.compositor_surface_submitted == 0 &&
                           compositor_plan.compositor_damage_allowed == 1 &&
                           compositor_plan.compositor_damage_submitted == 0 &&
                           compositor_plan.compositor_ticket_selected == 1,
                       "credential compositor plan edit should remain declarative");
  fails += expect_true(compositor_plan.compositor_credential_panel == 1 &&
                           compositor_plan.compositor_credential_input == 1 &&
                           compositor_plan.compositor_credential_focus == 1,
                       "credential compositor plan edit should compose credential widgets");
  fails += expect_true(compositor_plan.compositor_reuse_allowed == 1 &&
                           compositor_plan.compositor_cache_allowed == 1 &&
                           compositor_plan.compositor_cache_hit == 0 &&
                           compositor_plan.full_damage_required == 0,
                       "credential compositor plan edit should preserve scalable cache planning");
  fails += expect_true(compositor_plan.submit_callback_bound == 0 &&
                           compositor_plan.auth_callback_bound == 0 &&
                           compositor_plan.submit_enabled == 0 &&
                           compositor_plan.auth_attempt_allowed == 0,
                       "credential compositor plan edit must not bind auth callbacks");
  fails += expect_true(compositor_plan.raw_secret_exposed == 0 &&
                           compositor_plan.masked_text_exposed == 0 &&
                           compositor_plan.length_redacted == 1,
                       "credential compositor plan edit must stay redacted");
  fails += expect_true(strings_equal(compositor_plan.compositor_ticket,
                                     "credential-screen-compositor-ticket") &&
                           strings_equal(compositor_plan.surface_ticket,
                                         "credential-screen-surface-ticket") &&
                           strings_equal(compositor_plan.state,
                                         "compositor-credential-ready"),
                       "credential compositor plan edit should report compositor state");
  return fails;
}

static int test_loginwindow_credential_screen_compositor_plan_composes_text_recovery(void) {
  int fails = 0;
  struct login_window_credential_screen_compositor_plan compositor_plan;

  fails += expect_true(build_loginwindow_credential_screen_compositor_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &compositor_plan) == 0,
                       "credential compositor plan recovery should build");
  fails += expect_true(compositor_plan.compositor_plan_safe == 1 &&
                           compositor_plan.compositor_surface_allowed == 1 &&
                           compositor_plan.compositor_surface_submitted == 0 &&
                           compositor_plan.compositor_text_recovery == 1 &&
                           compositor_plan.compositor_text_login == 1,
                       "credential compositor plan recovery should compose text recovery");
  fails += expect_true(compositor_plan.compositor_credential_input == 0 &&
                           compositor_plan.compositor_credential_focus == 0 &&
                           compositor_plan.input_focus_allowed == 0,
                       "credential compositor plan recovery should block credential focus");
  fails += expect_true(compositor_plan.compositor_damage_allowed == 1 &&
                           compositor_plan.compositor_damage_submitted == 0 &&
                           compositor_plan.submit_callback_bound == 0 &&
                           compositor_plan.auth_callback_bound == 0 &&
                           compositor_plan.submit_enabled == 0 &&
                           compositor_plan.auth_attempt_allowed == 0,
                       "credential compositor plan recovery must keep compositor and auth disabled");
  fails += expect_true(strings_equal(compositor_plan.compositor_ticket,
                                     "text-recovery-compositor-ticket") &&
                           strings_equal(compositor_plan.compositor_target,
                                         "text-recovery-compositor") &&
                           strings_equal(compositor_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential compositor plan recovery should report recovery compositor ticket");
  return fails;
}

static int test_loginwindow_credential_screen_compositor_plan_composes_resume_text_login(void) {
  int fails = 0;
  struct login_window_credential_screen_compositor_plan compositor_plan;

  fails += expect_true(build_loginwindow_credential_screen_compositor_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &compositor_plan) == 0,
                       "credential compositor plan resume should build");
  fails += expect_true(compositor_plan.compositor_plan_safe == 1 &&
                           compositor_plan.compositor_text_login == 1 &&
                           compositor_plan.compositor_text_login_resume == 1,
                       "credential compositor plan resume should compose text login resume");
  fails += expect_true(compositor_plan.session_reset_required == 1 &&
                           compositor_plan.login_screen_rerender_required == 1 &&
                           compositor_plan.compositor_reuse_allowed == 0 &&
                           compositor_plan.full_damage_required == 1,
                       "credential compositor plan resume should require reset-aware full damage planning");
  fails += expect_true(compositor_plan.compositor_surface_submitted == 0 &&
                           compositor_plan.compositor_damage_submitted == 0 &&
                           compositor_plan.submit_enabled == 0 &&
                           compositor_plan.auth_attempt_allowed == 0,
                       "credential compositor plan resume must keep GUI auth disabled");
  fails += expect_true(strings_equal(compositor_plan.compositor_ticket,
                                     "text-login-resume-compositor-ticket") &&
                           strings_equal(compositor_plan.damage_policy,
                                         "full-rerender-declarative") &&
                           strings_equal(compositor_plan.state,
                                         "compositor-resume-ready"),
                       "credential compositor plan resume should report resume compositor ticket");
  return fails;
}

static int test_loginwindow_credential_screen_compositor_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_compositor_plan compositor_plan;

  fails += expect_true(build_loginwindow_credential_screen_compositor_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &compositor_plan) == 0,
                       "credential compositor plan submit should build");
  fails += expect_true(compositor_plan.compositor_plan_safe == 1 &&
                           compositor_plan.submit_requested == 1 &&
                           compositor_plan.compositor_text_login_fallback == 1,
                       "credential compositor plan submit should compose text login fallback");
  fails += expect_true(compositor_plan.action_allowed == 0 &&
                           compositor_plan.action_blocked == 1 &&
                           compositor_plan.input_focus_allowed == 0 &&
                           compositor_plan.compositor_credential_focus == 0,
                       "credential compositor plan submit should block GUI action");
  fails += expect_true(compositor_plan.compositor_surface_allowed == 1 &&
                           compositor_plan.compositor_surface_submitted == 0 &&
                           compositor_plan.compositor_damage_submitted == 0 &&
                           compositor_plan.submit_callback_bound == 0 &&
                           compositor_plan.auth_callback_bound == 0 &&
                           compositor_plan.submit_enabled == 0 &&
                           compositor_plan.auth_attempt_allowed == 0,
                       "credential compositor plan submit must stay declarative and redacted");
  fails += expect_true(strings_equal(compositor_plan.compositor_ticket,
                                     "text-login-fallback-compositor-ticket") &&
                           strings_equal(compositor_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(compositor_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential compositor plan submit should explain fallback compositor ticket");

  fails += expect_true(build_loginwindow_credential_screen_compositor_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &compositor_plan) == 0,
                       "credential compositor plan unknown should build");
  fails += expect_true(compositor_plan.compositor_plan_safe == 1 &&
                           compositor_plan.compositor_text_login_fallback == 1 &&
                           compositor_plan.action_allowed == 0 &&
                           compositor_plan.action_blocked == 1,
                       "credential compositor plan unknown should force text login fallback");
  fails += expect_true(strings_equal(compositor_plan.compositor_ticket,
                                     "text-login-fallback-compositor-ticket") &&
                           strings_equal(compositor_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential compositor plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_compositor_plan_fails_closed_for_unsafe_or_missing_surface_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_compositor_plan compositor_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_compositor_plan_build(
                           NULL, &compositor_plan) == 0,
                       "credential compositor plan missing surface plan should build fail-closed state");
  fails += expect_true(compositor_plan.surface_plan_available == 0 &&
                           compositor_plan.surface_plan_safe == 0 &&
                           compositor_plan.compositor_plan_safe == 0 &&
                           compositor_plan.route_selected == 0 &&
                           compositor_plan.route_blocked == 1,
                       "credential compositor plan missing surface plan should block compositor plan");
  fails += expect_true(compositor_plan.compositor_surface_allowed == 0 &&
                           compositor_plan.compositor_surface_submitted == 0 &&
                           compositor_plan.compositor_damage_allowed == 0 &&
                           compositor_plan.compositor_damage_submitted == 0 &&
                           compositor_plan.compositor_text_login == 1 &&
                           compositor_plan.compositor_text_login_fallback == 1 &&
                           compositor_plan.submit_callback_bound == 0 &&
                           compositor_plan.auth_callback_bound == 0 &&
                           compositor_plan.submit_enabled == 0 &&
                           compositor_plan.auth_attempt_allowed == 0,
                       "credential compositor plan missing surface plan must stay redacted");
  fails += expect_true(strings_equal(compositor_plan.compositor_ticket,
                                     "text-login-fallback-compositor-ticket") &&
                           strings_equal(compositor_plan.event_type,
                                         "credential-screen-compositor-plan-unavailable") &&
                           strings_equal(compositor_plan.blocked_reason,
                                         "surface-plan-unavailable"),
                       "credential compositor plan missing surface plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_compositor_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &compositor_plan) == 0,
                       "credential compositor plan unsafe surface plan should build blocked state");
  fails += expect_true(compositor_plan.surface_plan_available == 1 &&
                           compositor_plan.surface_plan_safe == 0 &&
                           compositor_plan.compositor_plan_safe == 0 &&
                           compositor_plan.route_selected == 0 &&
                           compositor_plan.route_blocked == 1,
                       "credential compositor plan unsafe surface plan should block compositor plan");
  fails += expect_true(compositor_plan.action_allowed == 0 &&
                           compositor_plan.action_blocked == 1 &&
                           compositor_plan.input_focus_allowed == 0 &&
                           compositor_plan.compositor_credential_focus == 0 &&
                           compositor_plan.compositor_text_login_fallback == 1,
                       "credential compositor plan unsafe surface plan must force text login fallback");
  fails += expect_true(strings_equal(compositor_plan.compositor_ticket,
                                     "text-login-fallback-compositor-ticket") &&
                           strings_equal(compositor_plan.event_type,
                                         "credential-screen-compositor-plan-unsafe") &&
                           strings_equal(compositor_plan.blocked_reason,
                                         "credential-compositor-plan-unsafe"),
                       "credential compositor plan unsafe surface plan should force text login");
  return fails;
}

int build_loginwindow_credential_screen_damage_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_damage_plan *damage_plan) {
  struct login_window_credential_screen_compositor_plan compositor_plan;

  if (build_loginwindow_credential_screen_compositor_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &compositor_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_damage_plan_build(&compositor_plan,
                                                          damage_plan);
}

static int test_loginwindow_credential_screen_damage_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_damage_plan damage_plan;

  fails += expect_true(build_loginwindow_credential_screen_damage_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'e', 0, 0, 0,
                           1, &damage_plan) == 0,
                       "credential damage plan edit should build");
  fails += expect_true(damage_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_DAMAGE_PLAN_VERSION,
                       "credential damage plan should expose stable version");
  fails += expect_true(damage_plan.compositor_plan_available == 1 &&
                           damage_plan.compositor_plan_safe == 1 &&
                           damage_plan.damage_plan_safe == 1,
                       "credential damage plan edit should require safe compositor plan");
  fails += expect_true(damage_plan.damage_allowed == 1 &&
                           damage_plan.damage_submitted == 0 &&
                           damage_plan.compositor_damage_submitted == 0 &&
                           damage_plan.damage_ticket_selected == 1,
                       "credential damage plan edit should remain declarative");
  fails += expect_true(damage_plan.damage_incremental_allowed == 1 &&
                           damage_plan.full_damage_required == 0 &&
                           damage_plan.damage_cache_allowed == 1 &&
                           damage_plan.damage_reuse_allowed == 1 &&
                           damage_plan.damage_cache_hit == 0,
                       "credential damage plan edit should preserve scalable incremental damage planning");
  fails += expect_true(damage_plan.damage_credential_panel == 1 &&
                           damage_plan.damage_credential_input == 1 &&
                           damage_plan.damage_credential_focus == 1,
                       "credential damage plan edit should mark credential widgets");
  fails += expect_true(damage_plan.submit_callback_bound == 0 &&
                           damage_plan.auth_callback_bound == 0 &&
                           damage_plan.submit_enabled == 0 &&
                           damage_plan.auth_attempt_allowed == 0,
                       "credential damage plan edit must not bind auth callbacks");
  fails += expect_true(damage_plan.raw_secret_exposed == 0 &&
                           damage_plan.masked_text_exposed == 0 &&
                           damage_plan.length_redacted == 1,
                       "credential damage plan edit must stay redacted");
  fails += expect_true(strings_equal(damage_plan.damage_ticket,
                                     "credential-screen-damage-ticket") &&
                           strings_equal(damage_plan.compositor_ticket,
                                         "credential-screen-compositor-ticket") &&
                           strings_equal(damage_plan.state,
                                         "damage-credential-ready"),
                       "credential damage plan edit should report damage state");
  return fails;
}

static int test_loginwindow_credential_screen_damage_plan_marks_text_recovery(void) {
  int fails = 0;
  struct login_window_credential_screen_damage_plan damage_plan;

  fails += expect_true(build_loginwindow_credential_screen_damage_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &damage_plan) == 0,
                       "credential damage plan recovery should build");
  fails += expect_true(damage_plan.damage_plan_safe == 1 &&
                           damage_plan.damage_allowed == 1 &&
                           damage_plan.damage_submitted == 0 &&
                           damage_plan.damage_text_recovery == 1 &&
                           damage_plan.damage_text_login == 1,
                       "credential damage plan recovery should mark text recovery");
  fails += expect_true(damage_plan.damage_credential_input == 0 &&
                           damage_plan.damage_credential_focus == 0 &&
                           damage_plan.input_focus_allowed == 0,
                       "credential damage plan recovery should block credential focus");
  fails += expect_true(damage_plan.compositor_damage_submitted == 0 &&
                           damage_plan.submit_callback_bound == 0 &&
                           damage_plan.auth_callback_bound == 0 &&
                           damage_plan.submit_enabled == 0 &&
                           damage_plan.auth_attempt_allowed == 0,
                       "credential damage plan recovery must keep compositor and auth disabled");
  fails += expect_true(strings_equal(damage_plan.damage_ticket,
                                     "text-recovery-damage-ticket") &&
                           strings_equal(damage_plan.compositor_target,
                                         "text-recovery-damage") &&
                           strings_equal(damage_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential damage plan recovery should report recovery damage ticket");
  return fails;
}

static int test_loginwindow_credential_screen_damage_plan_marks_resume_text_login(void) {
  int fails = 0;
  struct login_window_credential_screen_damage_plan damage_plan;

  fails += expect_true(build_loginwindow_credential_screen_damage_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &damage_plan) == 0,
                       "credential damage plan resume should build");
  fails += expect_true(damage_plan.damage_plan_safe == 1 &&
                           damage_plan.damage_text_login == 1 &&
                           damage_plan.damage_text_login_resume == 1,
                       "credential damage plan resume should mark text login resume");
  fails += expect_true(damage_plan.session_reset_required == 1 &&
                           damage_plan.login_screen_rerender_required == 1 &&
                           damage_plan.damage_reuse_allowed == 0 &&
                           damage_plan.damage_cache_allowed == 0 &&
                           damage_plan.full_damage_required == 1 &&
                           damage_plan.damage_incremental_allowed == 0,
                       "credential damage plan resume should require full rerender damage planning");
  fails += expect_true(damage_plan.damage_submitted == 0 &&
                           damage_plan.compositor_damage_submitted == 0 &&
                           damage_plan.submit_enabled == 0 &&
                           damage_plan.auth_attempt_allowed == 0,
                       "credential damage plan resume must keep GUI auth disabled");
  fails += expect_true(strings_equal(damage_plan.damage_ticket,
                                     "text-login-resume-damage-ticket") &&
                           strings_equal(damage_plan.damage_policy,
                                         "full-rerender-declarative") &&
                           strings_equal(damage_plan.state,
                                         "damage-resume-ready"),
                       "credential damage plan resume should report resume damage ticket");
  return fails;
}

static int test_loginwindow_credential_screen_damage_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_damage_plan damage_plan;

  fails += expect_true(build_loginwindow_credential_screen_damage_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &damage_plan) == 0,
                       "credential damage plan submit should build");
  fails += expect_true(damage_plan.damage_plan_safe == 1 &&
                           damage_plan.submit_requested == 1 &&
                           damage_plan.damage_text_login_fallback == 1,
                       "credential damage plan submit should mark text login fallback");
  fails += expect_true(damage_plan.action_allowed == 0 &&
                           damage_plan.action_blocked == 1 &&
                           damage_plan.input_focus_allowed == 0 &&
                           damage_plan.damage_credential_focus == 0,
                       "credential damage plan submit should block GUI action");
  fails += expect_true(damage_plan.damage_allowed == 1 &&
                           damage_plan.damage_submitted == 0 &&
                           damage_plan.compositor_damage_submitted == 0 &&
                           damage_plan.submit_callback_bound == 0 &&
                           damage_plan.auth_callback_bound == 0 &&
                           damage_plan.submit_enabled == 0 &&
                           damage_plan.auth_attempt_allowed == 0,
                       "credential damage plan submit must stay declarative and redacted");
  fails += expect_true(strings_equal(damage_plan.damage_ticket,
                                     "text-login-fallback-damage-ticket") &&
                           strings_equal(damage_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(damage_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential damage plan submit should explain fallback damage ticket");

  fails += expect_true(build_loginwindow_credential_screen_damage_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &damage_plan) == 0,
                       "credential damage plan unknown should build");
  fails += expect_true(damage_plan.damage_plan_safe == 1 &&
                           damage_plan.damage_text_login_fallback == 1 &&
                           damage_plan.action_allowed == 0 &&
                           damage_plan.action_blocked == 1,
                       "credential damage plan unknown should force text login fallback");
  fails += expect_true(strings_equal(damage_plan.damage_ticket,
                                     "text-login-fallback-damage-ticket") &&
                           strings_equal(damage_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential damage plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_damage_plan_fails_closed_for_unsafe_or_missing_compositor_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_damage_plan damage_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_damage_plan_build(
                           NULL, &damage_plan) == 0,
                       "credential damage plan missing compositor plan should build fail-closed state");
  fails += expect_true(damage_plan.compositor_plan_available == 0 &&
                           damage_plan.compositor_plan_safe == 0 &&
                           damage_plan.damage_plan_safe == 0 &&
                           damage_plan.route_selected == 0 &&
                           damage_plan.route_blocked == 1,
                       "credential damage plan missing compositor plan should block damage plan");
  fails += expect_true(damage_plan.damage_allowed == 0 &&
                           damage_plan.damage_submitted == 0 &&
                           damage_plan.compositor_damage_submitted == 0 &&
                           damage_plan.damage_text_login == 1 &&
                           damage_plan.damage_text_login_fallback == 1 &&
                           damage_plan.submit_callback_bound == 0 &&
                           damage_plan.auth_callback_bound == 0 &&
                           damage_plan.submit_enabled == 0 &&
                           damage_plan.auth_attempt_allowed == 0,
                       "credential damage plan missing compositor plan must stay redacted");
  fails += expect_true(strings_equal(damage_plan.damage_ticket,
                                     "text-login-fallback-damage-ticket") &&
                           strings_equal(damage_plan.event_type,
                                         "credential-screen-damage-plan-unavailable") &&
                           strings_equal(damage_plan.blocked_reason,
                                         "compositor-plan-unavailable"),
                       "credential damage plan missing compositor plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_damage_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &damage_plan) == 0,
                       "credential damage plan unsafe compositor plan should build blocked state");
  fails += expect_true(damage_plan.compositor_plan_available == 1 &&
                           damage_plan.compositor_plan_safe == 0 &&
                           damage_plan.damage_plan_safe == 0 &&
                           damage_plan.route_selected == 0 &&
                           damage_plan.route_blocked == 1,
                       "credential damage plan unsafe compositor plan should block damage plan");
  fails += expect_true(damage_plan.action_allowed == 0 &&
                           damage_plan.action_blocked == 1 &&
                           damage_plan.input_focus_allowed == 0 &&
                           damage_plan.damage_credential_focus == 0 &&
                           damage_plan.damage_text_login_fallback == 1,
                       "credential damage plan unsafe compositor plan must force text login fallback");
  fails += expect_true(strings_equal(damage_plan.damage_ticket,
                                     "text-login-fallback-damage-ticket") &&
                           strings_equal(damage_plan.event_type,
                                         "credential-screen-damage-plan-unsafe") &&
                           strings_equal(damage_plan.blocked_reason,
                                         "credential-damage-plan-unsafe"),
                       "credential damage plan unsafe compositor plan should force text login");
  return fails;
}

int test_login_runtime_credential_compositor_damage_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_compositor_plan_composes_credential_widgets();
  fails += test_loginwindow_credential_screen_compositor_plan_composes_text_recovery();
  fails += test_loginwindow_credential_screen_compositor_plan_composes_resume_text_login();
  fails += test_loginwindow_credential_screen_compositor_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_compositor_plan_fails_closed_for_unsafe_or_missing_surface_plan();
  fails += test_loginwindow_credential_screen_damage_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_damage_plan_marks_text_recovery();
  fails += test_loginwindow_credential_screen_damage_plan_marks_resume_text_login();
  fails += test_loginwindow_credential_screen_damage_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_damage_plan_fails_closed_for_unsafe_or_missing_compositor_plan();
  return fails;
}
