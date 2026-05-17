/*
 * tests/auth/test_login_runtime_credential_window_compositor_damage.c
 *
 * Credential screen window compositor plan + window damage plan
 * coverage for the `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-16 monolith
 * refactor (PR D.34 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_window_compositor_plan_build`:
 *     4 tests covering the credential widgets compositor + the
 *     text-route compositor (recovery + resume) + the submit/unknown
 *     fallback compositor + the missing-or-unsafe surface plan
 *     fail-closed default.
 *   - `login_window_credential_screen_window_damage_plan_build`: 4
 *     tests covering the credential widgets damage + the text-route
 *     damage (recovery + resume) + the submit/unknown fallback
 *     damage + the missing-or-unsafe compositor plan fail-closed
 *     default.
 *
 * Also exposes shared helpers
 * `build_loginwindow_credential_screen_window_compositor_plan_for_action`
 * and `build_loginwindow_credential_screen_window_damage_plan_for_action`,
 * used by later companion files that chain on top of the
 * compositor/damage stages (window_present, ...).
 *
 * The companion entry `test_login_runtime_credential_window_compositor_damage_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_window_compositor_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage,
    struct login_window_credential_screen_window_compositor_plan *compositor_plan) {
  struct login_window_credential_screen_window_surface_plan surface_plan;

  if (build_loginwindow_credential_screen_window_surface_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &surface_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_window_compositor_plan_build(
      &surface_plan, compositor_plan);
}

static int test_loginwindow_credential_screen_window_compositor_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_window_compositor_plan compositor_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_compositor_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'c', 0, 0, 0, 1,
          &compositor_plan) == 0,
      "credential window compositor plan edit should build");
  fails += expect_true(
      compositor_plan.version ==
          LOGIN_WINDOW_CREDENTIAL_SCREEN_WINDOW_COMPOSITOR_PLAN_VERSION,
      "credential window compositor plan should expose stable version");
  fails += expect_true(compositor_plan.window_surface_plan_available == 1 &&
                           compositor_plan.window_surface_plan_safe == 1 &&
                           compositor_plan.window_compositor_plan_safe == 1,
                       "credential window compositor plan should require safe surface plan");
  fails += expect_true(compositor_plan.compositor_required == 1 &&
                           compositor_plan.compositor_allowed == 1 &&
                           compositor_plan.compositor_submitted == 0 &&
                           compositor_plan.compositor_ticket_selected == 1 &&
                           compositor_plan.compositor_target_selected == 1 &&
                           compositor_plan.compositor_surface_allowed == 1 &&
                           compositor_plan.compositor_surface_submitted == 0 &&
                           compositor_plan.compositor_damage_planned == 1 &&
                           compositor_plan.compositor_damage_allowed == 1 &&
                           compositor_plan.compositor_damage_submitted == 0 &&
                           compositor_plan.compositor_auth_submit_allowed == 0 &&
                           compositor_plan.compositor_auth_attempt_allowed == 0,
                       "credential window compositor plan should remain declarative");
  fails += expect_true(compositor_plan.surface_bound == 0 &&
                           compositor_plan.surface_memory_mapped == 0 &&
                           compositor_plan.surface_pixels_written == 0 &&
                           compositor_plan.surface_compositor_submitted == 0 &&
                           compositor_plan.window_created == 0 &&
                           compositor_plan.window_surface_bound == 0 &&
                           compositor_plan.window_input_bound == 0 &&
                           compositor_plan.gui_submitted == 0 &&
                           compositor_plan.release_submitted == 0 &&
                           compositor_plan.reclaim_submitted == 0 &&
                           compositor_plan.compaction_submitted == 0,
                       "credential window compositor plan must not execute upstream work");
  fails += expect_true(compositor_plan.compositor_credential_panel == 1 &&
                           compositor_plan.compositor_credential_input == 1 &&
                           compositor_plan.compositor_credential_focus == 1 &&
                           compositor_plan.compositor_text_login == 0 &&
                           compositor_plan.compositor_text_login_fallback == 0,
                       "credential window compositor plan should mark credential widgets");
  fails += expect_true(compositor_plan.submit_callback_bound == 0 &&
                           compositor_plan.auth_callback_bound == 0 &&
                           compositor_plan.submit_enabled == 0 &&
                           compositor_plan.auth_attempt_allowed == 0 &&
                           compositor_plan.raw_secret_exposed == 0 &&
                           compositor_plan.masked_text_exposed == 0,
                       "credential window compositor plan must stay redacted and auth-disabled");
  fails += expect_true(
      strings_equal(compositor_plan.compositor_ticket,
                    "credential-screen-window-compositor-ticket") &&
          strings_equal(compositor_plan.surface_ticket,
                        "credential-screen-window-surface-ticket") &&
          strings_equal(compositor_plan.compositor_policy,
                        "declarative-window-compositor-no-submit") &&
          strings_equal(compositor_plan.state,
                        "window-compositor-credential-ready"),
      "credential window compositor plan should report compositor ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_compositor_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_window_compositor_plan compositor_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_compositor_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
          LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1, 1,
          &compositor_plan) == 0,
      "credential window compositor plan recovery should build");
  fails += expect_true(compositor_plan.window_compositor_plan_safe == 1 &&
                           compositor_plan.compositor_allowed == 1 &&
                           compositor_plan.compositor_submitted == 0 &&
                           compositor_plan.compositor_surface_submitted == 0 &&
                           compositor_plan.compositor_damage_submitted == 0 &&
                           compositor_plan.compositor_text_recovery == 1 &&
                           compositor_plan.compositor_text_login == 1 &&
                           compositor_plan.compositor_credential_focus == 0,
                       "credential window compositor plan recovery should mark text recovery");
  fails += expect_true(compositor_plan.surface_compositor_submitted == 0 &&
                           compositor_plan.surface_pixels_written == 0 &&
                           compositor_plan.compositor_auth_submit_allowed == 0 &&
                           compositor_plan.compositor_auth_attempt_allowed == 0 &&
                           compositor_plan.submit_enabled == 0 &&
                           compositor_plan.auth_attempt_allowed == 0,
                       "credential window compositor plan recovery must not submit or authenticate");
  fails += expect_true(
      strings_equal(compositor_plan.compositor_ticket,
                    "text-recovery-window-compositor-ticket") &&
          strings_equal(compositor_plan.compositor_target,
                        "text-recovery-window-compositor") &&
          strings_equal(compositor_plan.blocked_reason, "text-recovery-only"),
      "credential window compositor plan recovery should report recovery ticket");

  fails += expect_true(
      build_loginwindow_credential_screen_window_compositor_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0, 1,
          &compositor_plan) == 0,
      "credential window compositor plan resume should build");
  fails += expect_true(compositor_plan.window_compositor_plan_safe == 1 &&
                           compositor_plan.compositor_text_login_resume == 1 &&
                           compositor_plan.session_reset_required == 1 &&
                           compositor_plan.login_screen_rerender_required == 1 &&
                           compositor_plan.compositor_submitted == 0 &&
                           compositor_plan.compositor_damage_submitted == 0 &&
                           compositor_plan.submit_enabled == 0 &&
                           compositor_plan.auth_attempt_allowed == 0,
                       "credential window compositor plan resume should keep compositor auth disabled");
  fails += expect_true(
      strings_equal(compositor_plan.compositor_ticket,
                    "text-login-resume-window-compositor-ticket") &&
          strings_equal(compositor_plan.compositor_policy,
                        "full-window-compositor-declarative") &&
          strings_equal(compositor_plan.state, "window-compositor-resume-ready"),
      "credential window compositor plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_compositor_plan_falls_back_for_submit_and_unknown_action(void) {
  int fails = 0;
  struct login_window_credential_screen_window_compositor_plan compositor_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_compositor_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_SUBMIT, 0, 0, 0, 0, 1,
          &compositor_plan) == 0,
      "credential window compositor plan submit should build");
  fails += expect_true(compositor_plan.window_compositor_plan_safe == 1 &&
                           compositor_plan.submit_requested == 1 &&
                           compositor_plan.submit_blocked == 1 &&
                           compositor_plan.action_allowed == 0 &&
                           compositor_plan.action_blocked == 1 &&
                           compositor_plan.input_focus_allowed == 0 &&
                           compositor_plan.compositor_text_login == 1 &&
                           compositor_plan.compositor_text_login_fallback == 1 &&
                           compositor_plan.compositor_submitted == 0 &&
                           compositor_plan.compositor_damage_submitted == 0,
                       "credential window compositor plan submit should force text login");
  fails += expect_true(
      strings_equal(compositor_plan.compositor_ticket,
                    "text-login-fallback-window-compositor-ticket") &&
          strings_equal(compositor_plan.compositor_policy,
                        "fallback-window-compositor-declarative") &&
          strings_equal(compositor_plan.blocked_reason, "gui-submit-disabled"),
      "credential window compositor plan submit should report disabled GUI submit");

  fails += expect_true(
      build_loginwindow_credential_screen_window_compositor_plan_for_action(
          9876, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0, 1,
          &compositor_plan) == 0,
      "credential window compositor plan unknown action should build");
  fails += expect_true(compositor_plan.window_compositor_plan_safe == 1 &&
                           compositor_plan.action_allowed == 0 &&
                           compositor_plan.action_blocked == 1 &&
                           compositor_plan.input_focus_allowed == 0 &&
                           compositor_plan.compositor_text_login == 1 &&
                           compositor_plan.compositor_text_login_fallback == 1 &&
                           compositor_plan.compositor_submitted == 0 &&
                           compositor_plan.compositor_damage_submitted == 0,
                       "credential window compositor plan unknown action should force text login");
  fails += expect_true(
      strings_equal(compositor_plan.compositor_ticket,
                    "text-login-fallback-window-compositor-ticket") &&
          strings_equal(compositor_plan.compositor_target,
                        "text-login-fallback-window-compositor") &&
          strings_equal(compositor_plan.state,
                        "window-compositor-text-login-ready"),
      "credential window compositor plan unknown action should report fallback ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_compositor_plan_fails_closed_for_unsafe_or_missing_surface_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_window_surface_plan surface_plan;
  struct login_window_credential_screen_window_compositor_plan compositor_plan;

  fails += expect_true(
      login_window_credential_screen_window_compositor_plan_build(
          NULL, &compositor_plan) == 0,
      "credential window compositor plan missing surface should build fallback");
  fails += expect_true(compositor_plan.window_surface_plan_available == 0 &&
                           compositor_plan.window_surface_plan_safe == 0 &&
                           compositor_plan.window_compositor_plan_safe == 0 &&
                           compositor_plan.route_blocked == 1 &&
                           compositor_plan.compositor_allowed == 0 &&
                           compositor_plan.compositor_ticket_selected == 0 &&
                           compositor_plan.compositor_target_selected == 0 &&
                           compositor_plan.compositor_text_login == 1 &&
                           compositor_plan.compositor_text_login_fallback == 1 &&
                           compositor_plan.compositor_submitted == 0 &&
                           compositor_plan.compositor_damage_submitted == 0,
                       "credential window compositor plan missing surface should fail closed");
  fails += expect_true(
      strings_equal(compositor_plan.compositor_ticket,
                    "text-login-fallback-window-compositor-ticket") &&
          strings_equal(compositor_plan.event_type,
                        "credential-screen-window-compositor-plan-unavailable") &&
          strings_equal(compositor_plan.blocked_reason,
                        "window-surface-plan-unavailable"),
      "credential window compositor plan missing surface should report missing upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_surface_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0, 0, 1,
          &surface_plan) == 0,
      "credential window compositor plan unsafe surface fixture should build");
  surface_plan.window_surface_plan_safe = 0;
  surface_plan.raw_secret_exposed = 1;
  surface_plan.submit_blocked = 0;
  fails += expect_true(
      login_window_credential_screen_window_compositor_plan_build(
          &surface_plan, &compositor_plan) == 0,
      "credential window compositor plan unsafe surface should build fallback");
  fails += expect_true(compositor_plan.window_surface_plan_available == 1 &&
                           compositor_plan.window_surface_plan_safe == 0 &&
                           compositor_plan.window_compositor_plan_safe == 0 &&
                           compositor_plan.route_blocked == 1 &&
                           compositor_plan.compositor_allowed == 0 &&
                           compositor_plan.compositor_ticket_selected == 0 &&
                           compositor_plan.compositor_target_selected == 0 &&
                           compositor_plan.compositor_text_login == 1 &&
                           compositor_plan.compositor_text_login_fallback == 1 &&
                           compositor_plan.raw_secret_exposed == 0,
                       "credential window compositor plan unsafe surface should fail closed");
  fails += expect_true(
      strings_equal(compositor_plan.compositor_ticket,
                    "text-login-fallback-window-compositor-ticket") &&
          strings_equal(compositor_plan.event_type,
                        "credential-screen-window-compositor-plan-unsafe") &&
          strings_equal(compositor_plan.blocked_reason,
                        "credential-window-compositor-plan-unsafe"),
      "credential window compositor plan unsafe surface should report unsafe upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_surface_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'f', 0, 0, 0, 1,
          &surface_plan) == 0,
      "credential window compositor plan forged origin fixture should build");
  surface_plan.window_plan_available = 0;
  surface_plan.window_plan_safe = 0;
  surface_plan.window_surface_plan_safe = 1;
  fails += expect_true(
      login_window_credential_screen_window_compositor_plan_build(
          &surface_plan, &compositor_plan) == 0,
      "credential window compositor plan forged window origin should build fallback");
  fails += expect_true(compositor_plan.window_surface_plan_available == 1 &&
                           compositor_plan.window_surface_plan_safe == 1 &&
                           compositor_plan.window_compositor_plan_safe == 0 &&
                           compositor_plan.route_blocked == 1 &&
                           compositor_plan.compositor_allowed == 0 &&
                           compositor_plan.compositor_ticket_selected == 0 &&
                           compositor_plan.compositor_target_selected == 0 &&
                           compositor_plan.compositor_text_login == 1 &&
                           compositor_plan.compositor_text_login_fallback == 1 &&
                           compositor_plan.submit_enabled == 0 &&
                           compositor_plan.auth_attempt_allowed == 0,
                       "credential window compositor plan should reject forged window origin");
  fails += expect_true(
      strings_equal(compositor_plan.compositor_ticket,
                    "text-login-fallback-window-compositor-ticket") &&
          strings_equal(compositor_plan.event_type,
                        "credential-screen-window-compositor-plan-unsafe") &&
          strings_equal(compositor_plan.blocked_reason,
                        "credential-window-compositor-plan-unsafe"),
      "credential window compositor plan forged origin should report unsafe upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_surface_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'p', 0, 0, 0, 1,
          &surface_plan) == 0,
      "credential window compositor plan submitted surface fixture should build");
  surface_plan.surface_bound = 1;
  surface_plan.surface_memory_mapped = 1;
  surface_plan.surface_pixels_written = 1;
  surface_plan.surface_compositor_submit_allowed = 1;
  surface_plan.surface_compositor_submitted = 1;
  surface_plan.surface_auth_submit_allowed = 1;
  surface_plan.surface_auth_attempt_allowed = 1;
  surface_plan.window_created = 1;
  surface_plan.window_surface_bound = 1;
  surface_plan.window_input_bound = 1;
  surface_plan.gui_submitted = 1;
  surface_plan.release_submitted = 1;
  surface_plan.reclaim_submitted = 1;
  surface_plan.compaction_submitted = 1;
  fails += expect_true(
      login_window_credential_screen_window_compositor_plan_build(
          &surface_plan, &compositor_plan) == 0,
      "credential window compositor plan submitted surface should build fallback");
  fails += expect_true(compositor_plan.window_compositor_plan_safe == 0 &&
                           compositor_plan.compositor_allowed == 0 &&
                           compositor_plan.compositor_submitted == 0 &&
                           compositor_plan.compositor_surface_submitted == 0 &&
                           compositor_plan.compositor_damage_submitted == 0 &&
                           compositor_plan.compositor_auth_submit_allowed == 0 &&
                           compositor_plan.compositor_auth_attempt_allowed == 0 &&
                           compositor_plan.surface_bound == 0 &&
                           compositor_plan.surface_memory_mapped == 0 &&
                           compositor_plan.surface_pixels_written == 0 &&
                           compositor_plan.surface_compositor_submit_allowed == 0 &&
                           compositor_plan.surface_compositor_submitted == 0 &&
                           compositor_plan.window_created == 0 &&
                           compositor_plan.window_surface_bound == 0 &&
                           compositor_plan.window_input_bound == 0 &&
                           compositor_plan.gui_submitted == 0 &&
                           compositor_plan.release_submitted == 0 &&
                           compositor_plan.reclaim_submitted == 0 &&
                           compositor_plan.compaction_submitted == 0 &&
                           compositor_plan.submit_enabled == 0 &&
                           compositor_plan.auth_attempt_allowed == 0,
                       "credential window compositor plan must not copy unsafe submitted surface state");
  fails += expect_true(
      strings_equal(compositor_plan.compositor_ticket,
                    "text-login-fallback-window-compositor-ticket") &&
          strings_equal(compositor_plan.event_type,
                        "credential-screen-window-compositor-plan-unsafe") &&
          strings_equal(compositor_plan.blocked_reason,
                        "credential-window-compositor-plan-unsafe"),
      "credential window compositor plan submitted surface should report unsafe upstream");
  return fails;
}

int build_loginwindow_credential_screen_window_damage_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage,
    struct login_window_credential_screen_window_damage_plan *damage_plan) {
  struct login_window_credential_screen_window_compositor_plan compositor_plan;

  if (build_loginwindow_credential_screen_window_compositor_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage,
          &compositor_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_window_damage_plan_build(
      &compositor_plan, damage_plan);
}

static int test_loginwindow_credential_screen_window_damage_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_window_damage_plan damage_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_damage_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'd', 0, 0, 0, 1,
          &damage_plan) == 0,
      "credential window damage plan edit should build");
  fails += expect_true(
      damage_plan.version ==
          LOGIN_WINDOW_CREDENTIAL_SCREEN_WINDOW_DAMAGE_PLAN_VERSION,
      "credential window damage plan should expose stable version");
  fails += expect_true(damage_plan.window_compositor_plan_available == 1 &&
                           damage_plan.window_surface_plan_available == 1 &&
                           damage_plan.window_surface_plan_safe == 1 &&
                           damage_plan.window_compositor_plan_safe == 1 &&
                           damage_plan.window_damage_plan_safe == 1,
                       "credential window damage plan should require safe compositor plan");
  fails += expect_true(damage_plan.damage_required == 1 &&
                           damage_plan.damage_allowed == 1 &&
                           damage_plan.damage_submitted == 0 &&
                           damage_plan.damage_ticket_selected == 1 &&
                           damage_plan.damage_target_selected == 1 &&
                           damage_plan.damage_incremental_allowed == 1 &&
                           damage_plan.full_damage_required == 0 &&
                           damage_plan.damage_cache_allowed == 1 &&
                           damage_plan.damage_reuse_allowed == 1 &&
                           damage_plan.damage_cache_hit == 0,
                       "credential window damage plan should remain declarative and scalable");
  fails += expect_true(damage_plan.compositor_submitted == 0 &&
                           damage_plan.compositor_surface_submitted == 0 &&
                           damage_plan.compositor_damage_submitted == 0 &&
                           damage_plan.surface_bound == 0 &&
                           damage_plan.surface_memory_mapped == 0 &&
                           damage_plan.surface_pixels_written == 0 &&
                           damage_plan.window_created == 0 &&
                           damage_plan.window_surface_bound == 0 &&
                           damage_plan.window_input_bound == 0 &&
                           damage_plan.gui_submitted == 0 &&
                           damage_plan.release_submitted == 0 &&
                           damage_plan.reclaim_submitted == 0 &&
                           damage_plan.compaction_submitted == 0,
                       "credential window damage plan must not execute upstream work");
  fails += expect_true(damage_plan.damage_credential_panel == 1 &&
                           damage_plan.damage_credential_input == 1 &&
                           damage_plan.damage_credential_focus == 1 &&
                           damage_plan.damage_text_login == 0 &&
                           damage_plan.damage_text_login_fallback == 0,
                       "credential window damage plan should mark credential widgets");
  fails += expect_true(damage_plan.submit_callback_bound == 0 &&
                           damage_plan.auth_callback_bound == 0 &&
                           damage_plan.submit_enabled == 0 &&
                           damage_plan.auth_attempt_allowed == 0 &&
                           damage_plan.damage_auth_submit_allowed == 0 &&
                           damage_plan.damage_auth_attempt_allowed == 0 &&
                           damage_plan.raw_secret_exposed == 0 &&
                           damage_plan.masked_text_exposed == 0,
                       "credential window damage plan must stay redacted and auth-disabled");
  fails += expect_true(
      strings_equal(damage_plan.damage_ticket,
                    "credential-screen-window-damage-ticket") &&
          strings_equal(damage_plan.compositor_ticket,
                        "credential-screen-window-compositor-ticket") &&
          strings_equal(damage_plan.damage_policy,
                        "incremental-window-damage-declarative") &&
          strings_equal(damage_plan.cache_policy,
                        "window-damage-cache-eligible") &&
          strings_equal(damage_plan.state, "window-damage-credential-ready"),
      "credential window damage plan should report damage ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_damage_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_window_damage_plan damage_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_damage_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
          LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1, 1,
          &damage_plan) == 0,
      "credential window damage plan recovery should build");
  fails += expect_true(damage_plan.window_damage_plan_safe == 1 &&
                           damage_plan.damage_allowed == 1 &&
                           damage_plan.damage_submitted == 0 &&
                           damage_plan.damage_text_recovery == 1 &&
                           damage_plan.damage_text_login == 1 &&
                           damage_plan.damage_text_login_fallback == 0 &&
                           damage_plan.damage_credential_focus == 0,
                       "credential window damage plan recovery should mark text recovery");
  fails += expect_true(damage_plan.full_damage_required == 1 &&
                           damage_plan.damage_incremental_allowed == 0 &&
                           damage_plan.damage_cache_allowed == 0 &&
                           damage_plan.damage_reuse_allowed == 0 &&
                           damage_plan.input_focus_allowed == 0,
                       "credential window damage plan recovery should require full declarative damage");
  fails += expect_true(damage_plan.compositor_damage_submitted == 0 &&
                           damage_plan.damage_auth_submit_allowed == 0 &&
                           damage_plan.damage_auth_attempt_allowed == 0 &&
                           damage_plan.submit_enabled == 0 &&
                           damage_plan.auth_attempt_allowed == 0,
                       "credential window damage plan recovery must not submit or authenticate");
  fails += expect_true(
      strings_equal(damage_plan.damage_ticket,
                    "text-recovery-window-damage-ticket") &&
          strings_equal(damage_plan.compositor_target,
                        "text-recovery-window-damage") &&
          strings_equal(damage_plan.blocked_reason, "text-recovery-only"),
      "credential window damage plan recovery should report recovery ticket");

  fails += expect_true(
      build_loginwindow_credential_screen_window_damage_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0, 1,
          &damage_plan) == 0,
      "credential window damage plan resume should build");
  fails += expect_true(damage_plan.window_damage_plan_safe == 1 &&
                           damage_plan.damage_text_login_resume == 1 &&
                           damage_plan.session_reset_required == 1 &&
                           damage_plan.login_screen_rerender_required == 1 &&
                           damage_plan.full_damage_required == 1 &&
                           damage_plan.damage_incremental_allowed == 0 &&
                           damage_plan.damage_cache_allowed == 0 &&
                           damage_plan.damage_reuse_allowed == 0,
                       "credential window damage plan resume should require full rerender damage");
  fails += expect_true(damage_plan.damage_submitted == 0 &&
                           damage_plan.compositor_damage_submitted == 0 &&
                           damage_plan.submit_enabled == 0 &&
                           damage_plan.auth_attempt_allowed == 0,
                       "credential window damage plan resume must keep GUI auth disabled");
  fails += expect_true(
      strings_equal(damage_plan.damage_ticket,
                    "text-login-resume-window-damage-ticket") &&
          strings_equal(damage_plan.cache_policy,
                        "window-damage-cache-bypassed-for-rerender") &&
          strings_equal(damage_plan.state, "window-damage-resume-ready"),
      "credential window damage plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_damage_plan_falls_back_for_submit_and_unknown_action(void) {
  int fails = 0;
  struct login_window_credential_screen_window_damage_plan damage_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_damage_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_SUBMIT, 0, 0, 0, 0, 1,
          &damage_plan) == 0,
      "credential window damage plan submit should build");
  fails += expect_true(damage_plan.window_damage_plan_safe == 1 &&
                           damage_plan.submit_requested == 1 &&
                           damage_plan.submit_blocked == 1 &&
                           damage_plan.action_allowed == 0 &&
                           damage_plan.action_blocked == 1 &&
                           damage_plan.input_focus_allowed == 0 &&
                           damage_plan.damage_text_login == 1 &&
                           damage_plan.damage_text_login_fallback == 1 &&
                           damage_plan.damage_submitted == 0 &&
                           damage_plan.compositor_damage_submitted == 0,
                       "credential window damage plan submit should force text login");
  fails += expect_true(
      strings_equal(damage_plan.damage_ticket,
                    "text-login-fallback-window-damage-ticket") &&
          strings_equal(damage_plan.damage_policy,
                        "fallback-window-damage-declarative") &&
          strings_equal(damage_plan.blocked_reason, "gui-submit-disabled"),
      "credential window damage plan submit should report disabled GUI submit");

  fails += expect_true(
      build_loginwindow_credential_screen_window_damage_plan_for_action(
          9876, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0, 1,
          &damage_plan) == 0,
      "credential window damage plan unknown action should build");
  fails += expect_true(damage_plan.window_damage_plan_safe == 1 &&
                           damage_plan.action_allowed == 0 &&
                           damage_plan.action_blocked == 1 &&
                           damage_plan.input_focus_allowed == 0 &&
                           damage_plan.damage_text_login == 1 &&
                           damage_plan.damage_text_login_fallback == 1 &&
                           damage_plan.damage_submitted == 0 &&
                           damage_plan.compositor_damage_submitted == 0,
                       "credential window damage plan unknown action should force text login");
  fails += expect_true(
      strings_equal(damage_plan.damage_ticket,
                    "text-login-fallback-window-damage-ticket") &&
          strings_equal(damage_plan.compositor_target,
                        "text-login-fallback-window-damage") &&
          strings_equal(damage_plan.state, "window-damage-text-login-ready"),
      "credential window damage plan unknown action should report fallback ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_damage_plan_fails_closed_for_unsafe_or_missing_compositor_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_window_compositor_plan compositor_plan;
  struct login_window_credential_screen_window_damage_plan damage_plan;

  fails += expect_true(
      login_window_credential_screen_window_damage_plan_build(
          NULL, &damage_plan) == 0,
      "credential window damage plan missing compositor should build fallback");
  fails += expect_true(damage_plan.window_compositor_plan_available == 0 &&
                           damage_plan.window_compositor_plan_safe == 0 &&
                           damage_plan.window_damage_plan_safe == 0 &&
                           damage_plan.route_blocked == 1 &&
                           damage_plan.damage_allowed == 0 &&
                           damage_plan.damage_ticket_selected == 0 &&
                           damage_plan.damage_target_selected == 0 &&
                           damage_plan.damage_text_login == 1 &&
                           damage_plan.damage_text_login_fallback == 1 &&
                           damage_plan.damage_submitted == 0 &&
                           damage_plan.compositor_damage_submitted == 0,
                       "credential window damage plan missing compositor should fail closed");
  fails += expect_true(
      strings_equal(damage_plan.damage_ticket,
                    "text-login-fallback-window-damage-ticket") &&
          strings_equal(damage_plan.event_type,
                        "credential-screen-window-damage-plan-unavailable") &&
          strings_equal(damage_plan.blocked_reason,
                        "window-compositor-plan-unavailable"),
      "credential window damage plan missing compositor should report missing upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_compositor_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0, 0, 1,
          &compositor_plan) == 0,
      "credential window damage plan unsafe compositor fixture should build");
  compositor_plan.window_compositor_plan_safe = 0;
  compositor_plan.raw_secret_exposed = 1;
  compositor_plan.submit_blocked = 0;
  fails += expect_true(
      login_window_credential_screen_window_damage_plan_build(
          &compositor_plan, &damage_plan) == 0,
      "credential window damage plan unsafe compositor should build fallback");
  fails += expect_true(damage_plan.window_compositor_plan_available == 1 &&
                           damage_plan.window_compositor_plan_safe == 0 &&
                           damage_plan.window_damage_plan_safe == 0 &&
                           damage_plan.route_blocked == 1 &&
                           damage_plan.damage_allowed == 0 &&
                           damage_plan.damage_ticket_selected == 0 &&
                           damage_plan.damage_target_selected == 0 &&
                           damage_plan.damage_text_login == 1 &&
                           damage_plan.damage_text_login_fallback == 1 &&
                           damage_plan.raw_secret_exposed == 0,
                       "credential window damage plan unsafe compositor should fail closed");
  fails += expect_true(
      strings_equal(damage_plan.damage_ticket,
                    "text-login-fallback-window-damage-ticket") &&
          strings_equal(damage_plan.event_type,
                        "credential-screen-window-damage-plan-unsafe") &&
          strings_equal(damage_plan.blocked_reason,
                        "credential-window-damage-plan-unsafe"),
      "credential window damage plan unsafe compositor should report unsafe upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_compositor_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'f', 0, 0, 0, 1,
          &compositor_plan) == 0,
      "credential window damage plan forged origin fixture should build");
  compositor_plan.window_surface_plan_available = 0;
  compositor_plan.window_surface_plan_safe = 0;
  compositor_plan.window_compositor_plan_safe = 1;
  fails += expect_true(
      login_window_credential_screen_window_damage_plan_build(
          &compositor_plan, &damage_plan) == 0,
      "credential window damage plan forged surface origin should build fallback");
  fails += expect_true(damage_plan.window_compositor_plan_available == 1 &&
                           damage_plan.window_surface_plan_available == 0 &&
                           damage_plan.window_surface_plan_safe == 0 &&
                           damage_plan.window_compositor_plan_safe == 1 &&
                           damage_plan.window_damage_plan_safe == 0 &&
                           damage_plan.damage_allowed == 0 &&
                           damage_plan.damage_ticket_selected == 0 &&
                           damage_plan.damage_text_login_fallback == 1 &&
                           damage_plan.submit_enabled == 0 &&
                           damage_plan.auth_attempt_allowed == 0,
                       "credential window damage plan should reject forged compositor origin");
  fails += expect_true(
      strings_equal(damage_plan.damage_ticket,
                    "text-login-fallback-window-damage-ticket") &&
          strings_equal(damage_plan.event_type,
                        "credential-screen-window-damage-plan-unsafe") &&
          strings_equal(damage_plan.blocked_reason,
                        "credential-window-damage-plan-unsafe"),
      "credential window damage plan forged origin should report unsafe upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_compositor_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'p', 0, 0, 0, 1,
          &compositor_plan) == 0,
      "credential window damage plan submitted compositor fixture should build");
  compositor_plan.compositor_submitted = 1;
  compositor_plan.compositor_surface_submitted = 1;
  compositor_plan.compositor_damage_submitted = 1;
  compositor_plan.compositor_auth_submit_allowed = 1;
  compositor_plan.compositor_auth_attempt_allowed = 1;
  compositor_plan.surface_bound = 1;
  compositor_plan.surface_memory_mapped = 1;
  compositor_plan.surface_pixels_written = 1;
  compositor_plan.surface_compositor_submitted = 1;
  compositor_plan.window_created = 1;
  compositor_plan.window_surface_bound = 1;
  compositor_plan.window_input_bound = 1;
  compositor_plan.gui_submitted = 1;
  compositor_plan.release_submitted = 1;
  compositor_plan.reclaim_submitted = 1;
  compositor_plan.compaction_submitted = 1;
  fails += expect_true(
      login_window_credential_screen_window_damage_plan_build(
          &compositor_plan, &damage_plan) == 0,
      "credential window damage plan submitted compositor should build fallback");
  fails += expect_true(damage_plan.window_damage_plan_safe == 0 &&
                           damage_plan.damage_allowed == 0 &&
                           damage_plan.damage_submitted == 0 &&
                           damage_plan.damage_auth_submit_allowed == 0 &&
                           damage_plan.damage_auth_attempt_allowed == 0 &&
                           damage_plan.compositor_submitted == 0 &&
                           damage_plan.compositor_surface_submitted == 0 &&
                           damage_plan.compositor_damage_submitted == 0 &&
                           damage_plan.surface_bound == 0 &&
                           damage_plan.surface_memory_mapped == 0 &&
                           damage_plan.surface_pixels_written == 0 &&
                           damage_plan.surface_compositor_submitted == 0 &&
                           damage_plan.window_created == 0 &&
                           damage_plan.window_surface_bound == 0 &&
                           damage_plan.window_input_bound == 0 &&
                           damage_plan.gui_submitted == 0 &&
                           damage_plan.release_submitted == 0 &&
                           damage_plan.reclaim_submitted == 0 &&
                           damage_plan.compaction_submitted == 0 &&
                           damage_plan.submit_enabled == 0 &&
                           damage_plan.auth_attempt_allowed == 0,
                       "credential window damage plan must not copy unsafe submitted compositor state");
  fails += expect_true(
      strings_equal(damage_plan.damage_ticket,
                    "text-login-fallback-window-damage-ticket") &&
          strings_equal(damage_plan.event_type,
                        "credential-screen-window-damage-plan-unsafe") &&
          strings_equal(damage_plan.blocked_reason,
                        "credential-window-damage-plan-unsafe"),
      "credential window damage plan submitted compositor should report unsafe upstream");
  return fails;
}

int test_login_runtime_credential_window_compositor_damage_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_window_compositor_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_window_compositor_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_window_compositor_plan_falls_back_for_submit_and_unknown_action();
  fails += test_loginwindow_credential_screen_window_compositor_plan_fails_closed_for_unsafe_or_missing_surface_plan();
  fails += test_loginwindow_credential_screen_window_damage_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_window_damage_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_window_damage_plan_falls_back_for_submit_and_unknown_action();
  fails += test_loginwindow_credential_screen_window_damage_plan_fails_closed_for_unsafe_or_missing_compositor_plan();
  return fails;
}
