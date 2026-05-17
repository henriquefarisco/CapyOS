/*
 * tests/auth/test_login_runtime_credential_window_present.c
 *
 * Credential screen window present plan coverage for the
 * `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-16 monolith
 * refactor (PR D.35 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_window_present_plan_build`: 4
 *     tests covering the credential widgets present + the text-route
 *     present (recovery + resume) + the submit/unknown fallback
 *     present + the missing-or-unsafe damage plan fail-closed
 *     default.
 *
 * Also exposes shared helper
 * `build_loginwindow_credential_screen_window_present_plan_for_action`,
 * used by later companion files that chain on top of the present
 * stage (window_schedule, ...).
 *
 * Split independently from `window_schedule` (PR D.36) because the
 * combined block exceeded the 900-line layout limit.
 *
 * The companion entry `test_login_runtime_credential_window_present_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_window_present_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage,
    struct login_window_credential_screen_window_present_plan *present_plan) {
  struct login_window_credential_screen_window_damage_plan damage_plan;

  if (build_loginwindow_credential_screen_window_damage_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &damage_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_window_present_plan_build(
      &damage_plan, present_plan);
}

static int test_loginwindow_credential_screen_window_present_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_window_present_plan present_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_present_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'p', 0, 0, 0, 1,
          &present_plan) == 0,
      "credential window present plan edit should build");
  fails += expect_true(
      present_plan.version ==
          LOGIN_WINDOW_CREDENTIAL_SCREEN_WINDOW_PRESENT_PLAN_VERSION,
      "credential window present plan should expose stable version");
  fails += expect_true(present_plan.window_damage_plan_available == 1 &&
                           present_plan.window_compositor_plan_available == 1 &&
                           present_plan.window_surface_plan_available == 1 &&
                           present_plan.window_surface_plan_safe == 1 &&
                           present_plan.window_compositor_plan_safe == 1 &&
                           present_plan.window_damage_plan_safe == 1 &&
                           present_plan.window_present_plan_safe == 1,
                       "credential window present plan should require safe damage plan");
  fails += expect_true(present_plan.present_required == 1 &&
                           present_plan.present_allowed == 1 &&
                           present_plan.present_submitted == 0 &&
                           present_plan.present_ticket_selected == 1 &&
                           present_plan.present_target_selected == 1 &&
                           present_plan.present_incremental_allowed == 1 &&
                           present_plan.full_present_required == 0 &&
                           present_plan.present_cache_allowed == 1 &&
                           present_plan.present_reuse_allowed == 1 &&
                           present_plan.present_cache_hit == 0,
                       "credential window present plan should remain declarative and cache eligible");
  fails += expect_true(present_plan.damage_submitted == 0 &&
                           present_plan.compositor_submitted == 0 &&
                           present_plan.compositor_surface_submitted == 0 &&
                           present_plan.compositor_damage_submitted == 0 &&
                           present_plan.surface_bound == 0 &&
                           present_plan.surface_memory_mapped == 0 &&
                           present_plan.surface_pixels_written == 0 &&
                           present_plan.window_created == 0 &&
                           present_plan.window_surface_bound == 0 &&
                           present_plan.window_input_bound == 0 &&
                           present_plan.gui_submitted == 0 &&
                           present_plan.release_submitted == 0 &&
                           present_plan.reclaim_submitted == 0 &&
                           present_plan.compaction_submitted == 0,
                       "credential window present plan must not execute upstream work");
  fails += expect_true(present_plan.present_credential_panel == 1 &&
                           present_plan.present_credential_input == 1 &&
                           present_plan.present_credential_focus == 1 &&
                           present_plan.present_text_login == 0 &&
                           present_plan.present_text_login_fallback == 0,
                       "credential window present plan should mark credential widgets");
  fails += expect_true(present_plan.submit_callback_bound == 0 &&
                           present_plan.auth_callback_bound == 0 &&
                           present_plan.submit_enabled == 0 &&
                           present_plan.auth_attempt_allowed == 0 &&
                           present_plan.damage_auth_submit_allowed == 0 &&
                           present_plan.damage_auth_attempt_allowed == 0 &&
                           present_plan.present_auth_submit_allowed == 0 &&
                           present_plan.present_auth_attempt_allowed == 0 &&
                           present_plan.raw_secret_exposed == 0 &&
                           present_plan.masked_text_exposed == 0 &&
                           present_plan.damage_error == 0 &&
                           present_plan.present_error == 0,
                       "credential window present plan must stay redacted and auth-disabled");
  fails += expect_true(
      strings_equal(present_plan.present_ticket,
                    "credential-screen-window-present-ticket") &&
          strings_equal(present_plan.damage_ticket,
                        "credential-screen-window-damage-ticket") &&
          strings_equal(present_plan.present_policy,
                        "incremental-window-present-declarative") &&
          strings_equal(present_plan.cache_policy,
                        "window-present-cache-eligible") &&
          strings_equal(present_plan.state, "window-present-credential-ready"),
      "credential window present plan should report present ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_present_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_window_present_plan present_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_present_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
          LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1, 1,
          &present_plan) == 0,
      "credential window present plan recovery should build");
  fails += expect_true(present_plan.window_present_plan_safe == 1 &&
                           present_plan.present_allowed == 1 &&
                           present_plan.present_submitted == 0 &&
                           present_plan.present_text_recovery == 1 &&
                           present_plan.present_text_login == 1 &&
                           present_plan.present_text_login_fallback == 0 &&
                           present_plan.present_credential_focus == 0,
                       "credential window present plan recovery should mark text recovery");
  fails += expect_true(present_plan.full_present_required == 1 &&
                           present_plan.present_incremental_allowed == 0 &&
                           present_plan.present_cache_allowed == 0 &&
                           present_plan.present_reuse_allowed == 0 &&
                           present_plan.input_focus_allowed == 0,
                       "credential window present plan recovery should require full declarative present");
  fails += expect_true(present_plan.damage_submitted == 0 &&
                           present_plan.compositor_damage_submitted == 0 &&
                           present_plan.present_auth_submit_allowed == 0 &&
                           present_plan.present_auth_attempt_allowed == 0 &&
                           present_plan.submit_enabled == 0 &&
                           present_plan.auth_attempt_allowed == 0,
                       "credential window present plan recovery must not submit or authenticate");
  fails += expect_true(
      strings_equal(present_plan.present_ticket,
                    "text-recovery-window-present-ticket") &&
          strings_equal(present_plan.compositor_target,
                        "text-recovery-window-present") &&
          strings_equal(present_plan.blocked_reason, "text-recovery-only"),
      "credential window present plan recovery should report recovery ticket");

  fails += expect_true(
      build_loginwindow_credential_screen_window_present_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0, 1,
          &present_plan) == 0,
      "credential window present plan resume should build");
  fails += expect_true(present_plan.window_present_plan_safe == 1 &&
                           present_plan.present_text_login_resume == 1 &&
                           present_plan.session_reset_required == 1 &&
                           present_plan.login_screen_rerender_required == 1 &&
                           present_plan.full_present_required == 1 &&
                           present_plan.present_incremental_allowed == 0 &&
                           present_plan.present_cache_allowed == 0 &&
                           present_plan.present_reuse_allowed == 0,
                       "credential window present plan resume should require full rerender present");
  fails += expect_true(present_plan.present_submitted == 0 &&
                           present_plan.damage_submitted == 0 &&
                           present_plan.submit_enabled == 0 &&
                           present_plan.auth_attempt_allowed == 0,
                       "credential window present plan resume must keep GUI auth disabled");
  fails += expect_true(
      strings_equal(present_plan.present_ticket,
                    "text-login-resume-window-present-ticket") &&
          strings_equal(present_plan.cache_policy,
                        "window-present-cache-bypassed-for-rerender") &&
          strings_equal(present_plan.state, "window-present-resume-ready"),
      "credential window present plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_present_plan_falls_back_for_submit_and_unknown_action(void) {
  int fails = 0;
  struct login_window_credential_screen_window_present_plan present_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_present_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_SUBMIT, 0, 0, 0, 0, 1,
          &present_plan) == 0,
      "credential window present plan submit should build");
  fails += expect_true(present_plan.window_present_plan_safe == 1 &&
                           present_plan.submit_requested == 1 &&
                           present_plan.submit_blocked == 1 &&
                           present_plan.action_allowed == 0 &&
                           present_plan.action_blocked == 1 &&
                           present_plan.input_focus_allowed == 0 &&
                           present_plan.present_text_login == 1 &&
                           present_plan.present_text_login_fallback == 1 &&
                           present_plan.present_submitted == 0 &&
                           present_plan.damage_submitted == 0,
                       "credential window present plan submit should force text login");
  fails += expect_true(
      strings_equal(present_plan.present_ticket,
                    "text-login-fallback-window-present-ticket") &&
          strings_equal(present_plan.present_policy,
                        "fallback-window-present-declarative") &&
          strings_equal(present_plan.blocked_reason, "gui-submit-disabled"),
      "credential window present plan submit should report disabled GUI submit");

  fails += expect_true(
      build_loginwindow_credential_screen_window_present_plan_for_action(
          9876, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0, 1,
          &present_plan) == 0,
      "credential window present plan unknown action should build");
  fails += expect_true(present_plan.window_present_plan_safe == 1 &&
                           present_plan.action_allowed == 0 &&
                           present_plan.action_blocked == 1 &&
                           present_plan.input_focus_allowed == 0 &&
                           present_plan.present_text_login == 1 &&
                           present_plan.present_text_login_fallback == 1 &&
                           present_plan.present_submitted == 0 &&
                           present_plan.damage_submitted == 0,
                       "credential window present plan unknown action should force text login");
  fails += expect_true(
      strings_equal(present_plan.present_ticket,
                    "text-login-fallback-window-present-ticket") &&
          strings_equal(present_plan.compositor_target,
                        "text-login-fallback-window-present") &&
          strings_equal(present_plan.state, "window-present-text-login-ready"),
      "credential window present plan unknown action should report fallback ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_present_plan_fails_closed_for_unsafe_or_missing_damage_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_window_damage_plan damage_plan;
  struct login_window_credential_screen_window_present_plan present_plan;

  fails += expect_true(
      login_window_credential_screen_window_present_plan_build(
          NULL, &present_plan) == 0,
      "credential window present plan missing damage should build fallback");
  fails += expect_true(present_plan.window_damage_plan_available == 0 &&
                           present_plan.window_damage_plan_safe == 0 &&
                           present_plan.window_present_plan_safe == 0 &&
                           present_plan.route_blocked == 1 &&
                           present_plan.present_allowed == 0 &&
                           present_plan.present_ticket_selected == 0 &&
                           present_plan.present_target_selected == 0 &&
                           present_plan.present_text_login == 1 &&
                           present_plan.present_text_login_fallback == 1 &&
                           present_plan.present_submitted == 0 &&
                           present_plan.damage_submitted == 0,
                       "credential window present plan missing damage should fail closed");
  fails += expect_true(
      strings_equal(present_plan.present_ticket,
                    "text-login-fallback-window-present-ticket") &&
          strings_equal(present_plan.event_type,
                        "credential-screen-window-present-plan-unavailable") &&
          strings_equal(present_plan.blocked_reason,
                        "window-damage-plan-unavailable"),
      "credential window present plan missing damage should report missing upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_damage_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0, 0, 1,
          &damage_plan) == 0,
      "credential window present plan unsafe damage fixture should build");
  damage_plan.window_damage_plan_safe = 0;
  damage_plan.raw_secret_exposed = 1;
  damage_plan.submit_blocked = 0;
  fails += expect_true(
      login_window_credential_screen_window_present_plan_build(
          &damage_plan, &present_plan) == 0,
      "credential window present plan unsafe damage should build fallback");
  fails += expect_true(present_plan.window_damage_plan_available == 1 &&
                           present_plan.window_damage_plan_safe == 0 &&
                           present_plan.window_present_plan_safe == 0 &&
                           present_plan.route_blocked == 1 &&
                           present_plan.present_allowed == 0 &&
                           present_plan.present_ticket_selected == 0 &&
                           present_plan.present_target_selected == 0 &&
                           present_plan.present_text_login == 1 &&
                           present_plan.present_text_login_fallback == 1 &&
                           present_plan.raw_secret_exposed == 0,
                       "credential window present plan unsafe damage should fail closed");
  fails += expect_true(
      strings_equal(present_plan.present_ticket,
                    "text-login-fallback-window-present-ticket") &&
          strings_equal(present_plan.event_type,
                        "credential-screen-window-present-plan-unsafe") &&
          strings_equal(present_plan.blocked_reason,
                        "credential-window-present-plan-unsafe"),
      "credential window present plan unsafe damage should report unsafe upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_damage_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'f', 0, 0, 0, 1,
          &damage_plan) == 0,
      "credential window present plan forged origin fixture should build");
  damage_plan.window_compositor_plan_available = 0;
  damage_plan.window_surface_plan_safe = 0;
  damage_plan.window_damage_plan_safe = 1;
  fails += expect_true(
      login_window_credential_screen_window_present_plan_build(
          &damage_plan, &present_plan) == 0,
      "credential window present plan forged damage origin should build fallback");
  fails += expect_true(present_plan.window_damage_plan_available == 1 &&
                           present_plan.window_compositor_plan_available == 0 &&
                           present_plan.window_surface_plan_safe == 0 &&
                           present_plan.window_damage_plan_safe == 1 &&
                           present_plan.window_present_plan_safe == 0 &&
                           present_plan.present_allowed == 0 &&
                           present_plan.present_ticket_selected == 0 &&
                           present_plan.present_text_login_fallback == 1 &&
                           present_plan.submit_enabled == 0 &&
                           present_plan.auth_attempt_allowed == 0,
                       "credential window present plan should reject forged damage origin");
  fails += expect_true(
      strings_equal(present_plan.present_ticket,
                    "text-login-fallback-window-present-ticket") &&
          strings_equal(present_plan.event_type,
                        "credential-screen-window-present-plan-unsafe") &&
          strings_equal(present_plan.blocked_reason,
                        "credential-window-present-plan-unsafe"),
      "credential window present plan forged origin should report unsafe upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_damage_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0, 1,
          &damage_plan) == 0,
      "credential window present plan submitted damage fixture should build");
  damage_plan.damage_submitted = 1;
  damage_plan.damage_auth_submit_allowed = 1;
  damage_plan.damage_auth_attempt_allowed = 1;
  damage_plan.compositor_submitted = 1;
  damage_plan.compositor_surface_submitted = 1;
  damage_plan.compositor_damage_submitted = 1;
  damage_plan.surface_bound = 1;
  damage_plan.surface_memory_mapped = 1;
  damage_plan.surface_pixels_written = 1;
  damage_plan.surface_compositor_submitted = 1;
  damage_plan.window_created = 1;
  damage_plan.window_surface_bound = 1;
  damage_plan.window_input_bound = 1;
  damage_plan.damage_cache_hit = 1;
  damage_plan.gui_submitted = 1;
  damage_plan.release_submitted = 1;
  damage_plan.reclaim_submitted = 1;
  damage_plan.compaction_submitted = 1;
  fails += expect_true(
      login_window_credential_screen_window_present_plan_build(
          &damage_plan, &present_plan) == 0,
      "credential window present plan submitted damage should build fallback");
  fails += expect_true(present_plan.window_present_plan_safe == 0 &&
                           present_plan.present_allowed == 0 &&
                           present_plan.present_submitted == 0 &&
                           present_plan.present_auth_submit_allowed == 0 &&
                           present_plan.present_auth_attempt_allowed == 0 &&
                           present_plan.damage_submitted == 0 &&
                           present_plan.damage_auth_submit_allowed == 0 &&
                           present_plan.damage_auth_attempt_allowed == 0 &&
                           present_plan.damage_cache_hit == 0 &&
                           present_plan.compositor_submitted == 0 &&
                           present_plan.compositor_surface_submitted == 0 &&
                           present_plan.compositor_damage_submitted == 0 &&
                           present_plan.surface_bound == 0 &&
                           present_plan.surface_memory_mapped == 0 &&
                           present_plan.surface_pixels_written == 0 &&
                           present_plan.surface_compositor_submitted == 0 &&
                           present_plan.window_created == 0 &&
                           present_plan.window_surface_bound == 0 &&
                           present_plan.window_input_bound == 0 &&
                           present_plan.gui_submitted == 0 &&
                           present_plan.release_submitted == 0 &&
                           present_plan.reclaim_submitted == 0 &&
                           present_plan.compaction_submitted == 0 &&
                           present_plan.submit_enabled == 0 &&
                           present_plan.auth_attempt_allowed == 0,
                       "credential window present plan must not copy unsafe submitted damage state");
  fails += expect_true(
      strings_equal(present_plan.present_ticket,
                    "text-login-fallback-window-present-ticket") &&
          strings_equal(present_plan.event_type,
                        "credential-screen-window-present-plan-unsafe") &&
          strings_equal(present_plan.blocked_reason,
                        "credential-window-present-plan-unsafe"),
      "credential window present plan submitted damage should report unsafe upstream");
  return fails;
}

int test_login_runtime_credential_window_present_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_window_present_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_window_present_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_window_present_plan_falls_back_for_submit_and_unknown_action();
  fails += test_loginwindow_credential_screen_window_present_plan_fails_closed_for_unsafe_or_missing_damage_plan();
  return fails;
}
