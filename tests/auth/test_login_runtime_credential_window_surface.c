/*
 * tests/auth/test_login_runtime_credential_window_surface.c
 *
 * Credential screen window plan + window surface plan coverage for
 * the `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-16 monolith
 * refactor (PR D.33 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_window_plan_build`: 4 tests
 *     covering the credential widgets window + the text-route
 *     window (recovery + resume) + the submit/unknown fallback
 *     window + the missing-or-unsafe GUI plan fail-closed default.
 *   - `login_window_credential_screen_window_surface_plan_build`: 4
 *     tests covering the credential widgets surface + the text-route
 *     surface (recovery + resume) + the submit/unknown fallback
 *     surface + the missing-or-unsafe window plan fail-closed
 *     default.
 *
 * Also exposes shared helpers
 * `build_loginwindow_credential_screen_window_plan_for_action` and
 * `build_loginwindow_credential_screen_window_surface_plan_for_action`,
 * used by later companion files that chain on top of the
 * window/surface stages (window_compositor, ...).
 *
 * The companion entry `test_login_runtime_credential_window_surface_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_window_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_window_plan *window_plan) {
  struct login_window_credential_screen_gui_plan gui_plan;

  if (build_loginwindow_credential_screen_gui_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &gui_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_window_plan_build(&gui_plan,
                                                         window_plan);
}

static int test_loginwindow_credential_screen_window_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_window_plan window_plan;

  fails += expect_true(build_loginwindow_credential_screen_window_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'w', 0, 0, 0,
                           1, &window_plan) == 0,
                       "credential window plan edit should build");
  fails += expect_true(window_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_WINDOW_PLAN_VERSION,
                       "credential window plan should expose stable version");
  fails += expect_true(window_plan.gui_plan_available == 1 &&
                           window_plan.gui_plan_safe == 1 &&
                           window_plan.window_plan_safe == 1,
                       "credential window plan should require safe GUI plan");
  fails += expect_true(window_plan.window_required == 1 &&
                           window_plan.window_allowed == 1 &&
                           window_plan.window_created == 0 &&
                           window_plan.window_ticket_selected == 1 &&
                           window_plan.window_target_selected == 1 &&
                           window_plan.window_surface_bound == 0 &&
                           window_plan.window_input_bound == 0 &&
                           window_plan.window_auth_submit_allowed == 0 &&
                           window_plan.window_auth_attempt_allowed == 0,
                       "credential window plan should remain declarative");
  fails += expect_true(window_plan.gui_allowed == 1 &&
                           window_plan.gui_submitted == 0 &&
                           window_plan.gui_pixels_written == 0 &&
                           window_plan.release_submitted == 0 &&
                           window_plan.reclaim_submitted == 0 &&
                           window_plan.compaction_submitted == 0,
                       "credential window plan must not execute upstream work");
  fails += expect_true(window_plan.window_credential_panel == 1 &&
                           window_plan.window_credential_input == 1 &&
                           window_plan.window_credential_focus == 1 &&
                           window_plan.window_text_login == 0 &&
                           window_plan.window_text_login_fallback == 0,
                       "credential window plan should mark credential widgets");
  fails += expect_true(window_plan.submit_callback_bound == 0 &&
                           window_plan.auth_callback_bound == 0 &&
                           window_plan.submit_enabled == 0 &&
                           window_plan.auth_attempt_allowed == 0 &&
                           window_plan.raw_secret_exposed == 0 &&
                           window_plan.masked_text_exposed == 0,
                       "credential window plan must stay redacted and auth-disabled");
  fails += expect_true(strings_equal(window_plan.window_ticket,
                                     "credential-screen-window-ticket") &&
                           strings_equal(window_plan.gui_ticket,
                                         "credential-screen-gui-ticket") &&
                           strings_equal(window_plan.window_policy,
                                         "declarative-window-no-create") &&
                           strings_equal(window_plan.state,
                                         "window-credential-ready"),
                       "credential window plan should report window ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_window_plan window_plan;

  fails += expect_true(build_loginwindow_credential_screen_window_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &window_plan) == 0,
                       "credential window plan recovery should build");
  fails += expect_true(window_plan.window_plan_safe == 1 &&
                           window_plan.window_allowed == 1 &&
                           window_plan.window_created == 0 &&
                           window_plan.window_surface_bound == 0 &&
                           window_plan.window_text_recovery == 1 &&
                           window_plan.window_text_login == 1 &&
                           window_plan.window_credential_focus == 0,
                       "credential window plan recovery should mark text recovery");
  fails += expect_true(window_plan.gui_submitted == 0 &&
                           window_plan.gui_pixels_written == 0 &&
                           window_plan.window_auth_submit_allowed == 0 &&
                           window_plan.window_auth_attempt_allowed == 0 &&
                           window_plan.submit_enabled == 0 &&
                           window_plan.auth_attempt_allowed == 0,
                       "credential window plan recovery must not create or authenticate");
  fails += expect_true(strings_equal(window_plan.window_ticket,
                                     "text-recovery-window-ticket") &&
                           strings_equal(window_plan.compositor_target,
                                         "text-recovery-window") &&
                           strings_equal(window_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential window plan recovery should report recovery ticket");

  fails += expect_true(build_loginwindow_credential_screen_window_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &window_plan) == 0,
                       "credential window plan resume should build");
  fails += expect_true(window_plan.window_plan_safe == 1 &&
                           window_plan.window_text_login_resume == 1 &&
                           window_plan.session_reset_required == 1 &&
                           window_plan.login_screen_rerender_required == 1 &&
                           window_plan.window_created == 0 &&
                           window_plan.window_input_bound == 0 &&
                           window_plan.submit_enabled == 0 &&
                           window_plan.auth_attempt_allowed == 0,
                       "credential window plan resume should keep window auth disabled");
  fails += expect_true(strings_equal(window_plan.window_ticket,
                                     "text-login-resume-window-ticket") &&
                           strings_equal(window_plan.window_policy,
                                         "full-window-declarative") &&
                           strings_equal(window_plan.state,
                                         "window-resume-ready"),
                       "credential window plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_plan_falls_back_for_submit_and_unknown_action(void) {
  int fails = 0;
  struct login_window_credential_screen_window_plan window_plan;

  fails += expect_true(build_loginwindow_credential_screen_window_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_SUBMIT, 0, 0, 0, 0,
                           1, &window_plan) == 0,
                       "credential window plan submit should build");
  fails += expect_true(window_plan.window_plan_safe == 1 &&
                           window_plan.submit_requested == 1 &&
                           window_plan.submit_blocked == 1 &&
                           window_plan.action_allowed == 0 &&
                           window_plan.action_blocked == 1 &&
                           window_plan.input_focus_allowed == 0 &&
                           window_plan.window_text_login == 1 &&
                           window_plan.window_text_login_fallback == 1 &&
                           window_plan.window_created == 0 &&
                           window_plan.window_input_bound == 0,
                       "credential window plan submit should force text login");
  fails += expect_true(strings_equal(window_plan.window_ticket,
                                     "text-login-fallback-window-ticket") &&
                           strings_equal(window_plan.window_policy,
                                         "fallback-window-declarative") &&
                           strings_equal(window_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential window plan submit should report disabled GUI submit");

  fails += expect_true(build_loginwindow_credential_screen_window_plan_for_action(
                           9876, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0,
                           0, 0, 1, &window_plan) == 0,
                       "credential window plan unknown action should build");
  fails += expect_true(window_plan.window_plan_safe == 1 &&
                           window_plan.action_allowed == 0 &&
                           window_plan.action_blocked == 1 &&
                           window_plan.input_focus_allowed == 0 &&
                           window_plan.window_text_login == 1 &&
                           window_plan.window_text_login_fallback == 1 &&
                           window_plan.window_created == 0 &&
                           window_plan.window_surface_bound == 0 &&
                           window_plan.window_input_bound == 0,
                       "credential window plan unknown action should force text login");
  fails += expect_true(strings_equal(window_plan.window_ticket,
                                     "text-login-fallback-window-ticket") &&
                           strings_equal(window_plan.compositor_target,
                                         "text-login-fallback-window") &&
                           strings_equal(window_plan.state,
                                         "window-text-login-ready"),
                       "credential window plan unknown action should report fallback ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_plan_fails_closed_for_unsafe_or_missing_gui_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_gui_plan gui_plan;
  struct login_window_credential_screen_window_plan window_plan;

  fails += expect_true(login_window_credential_screen_window_plan_build(
                           NULL, &window_plan) == 0,
                       "credential window plan missing GUI should build fallback");
  fails += expect_true(window_plan.gui_plan_available == 0 &&
                           window_plan.gui_plan_safe == 0 &&
                           window_plan.window_plan_safe == 0 &&
                           window_plan.route_blocked == 1 &&
                           window_plan.window_allowed == 0 &&
                           window_plan.window_ticket_selected == 0 &&
                           window_plan.window_target_selected == 0 &&
                           window_plan.window_text_login == 1 &&
                           window_plan.window_text_login_fallback == 1 &&
                           window_plan.window_created == 0 &&
                           window_plan.window_surface_bound == 0,
                       "credential window plan missing GUI should fail closed");
  fails += expect_true(strings_equal(window_plan.window_ticket,
                                     "text-login-fallback-window-ticket") &&
                           strings_equal(window_plan.event_type,
                                         "credential-screen-window-plan-unavailable") &&
                           strings_equal(window_plan.blocked_reason,
                                         "gui-plan-unavailable"),
                       "credential window plan missing GUI should report missing upstream");

  fails += expect_true(build_loginwindow_credential_screen_gui_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0, 0,
                           1, &gui_plan) == 0,
                       "credential window plan unsafe GUI fixture should build");
  gui_plan.gui_plan_safe = 0;
  gui_plan.raw_secret_exposed = 1;
  gui_plan.submit_blocked = 0;
  fails += expect_true(login_window_credential_screen_window_plan_build(
                           &gui_plan, &window_plan) == 0,
                       "credential window plan unsafe GUI should build fallback");
  fails += expect_true(window_plan.gui_plan_available == 1 &&
                           window_plan.gui_plan_safe == 0 &&
                           window_plan.window_plan_safe == 0 &&
                           window_plan.route_blocked == 1 &&
                           window_plan.window_allowed == 0 &&
                           window_plan.window_ticket_selected == 0 &&
                           window_plan.window_target_selected == 0 &&
                           window_plan.window_text_login == 1 &&
                           window_plan.window_text_login_fallback == 1 &&
                           window_plan.window_created == 0 &&
                           window_plan.raw_secret_exposed == 0,
                       "credential window plan unsafe GUI should fail closed");
  fails += expect_true(strings_equal(window_plan.window_ticket,
                                     "text-login-fallback-window-ticket") &&
                           strings_equal(window_plan.event_type,
                                         "credential-screen-window-plan-unsafe") &&
                           strings_equal(window_plan.blocked_reason,
                                         "credential-window-plan-unsafe"),
                       "credential window plan unsafe GUI should report unsafe upstream");

  fails += expect_true(build_loginwindow_credential_screen_gui_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'p', 0, 0, 0,
                           1, &gui_plan) == 0,
                       "credential window plan submitted GUI fixture should build");
  gui_plan.gui_submitted = 1;
  gui_plan.gui_pixels_write_allowed = 1;
  gui_plan.gui_pixels_written = 1;
  gui_plan.gui_auth_submit_allowed = 1;
  gui_plan.gui_auth_attempt_allowed = 1;
  gui_plan.release_submitted = 1;
  gui_plan.release_storage_prune_allowed = 1;
  gui_plan.release_storage_pruned = 1;
  gui_plan.release_resource_release_allowed = 1;
  gui_plan.release_resource_released = 1;
  gui_plan.release_cpu_gpu_sync_allowed = 1;
  gui_plan.release_cpu_gpu_sync_submitted = 1;
  gui_plan.reclaim_submitted = 1;
  gui_plan.reclaim_storage_prune_allowed = 1;
  gui_plan.reclaim_storage_pruned = 1;
  gui_plan.reclaim_resource_release_allowed = 1;
  gui_plan.reclaim_resource_released = 1;
  gui_plan.reclaim_cpu_gpu_sync_allowed = 1;
  gui_plan.reclaim_cpu_gpu_sync_submitted = 1;
  gui_plan.compaction_submitted = 1;
  gui_plan.compaction_storage_write_allowed = 1;
  gui_plan.compaction_storage_written = 1;
  gui_plan.compaction_resource_release_allowed = 1;
  gui_plan.compaction_resource_released = 1;
  gui_plan.compaction_cpu_gpu_sync_allowed = 1;
  gui_plan.compaction_cpu_gpu_sync_submitted = 1;
  fails += expect_true(login_window_credential_screen_window_plan_build(
                           &gui_plan, &window_plan) == 0,
                       "credential window plan submitted GUI should build fallback");
  fails += expect_true(window_plan.window_plan_safe == 0 &&
                           window_plan.window_allowed == 0 &&
                           window_plan.window_created == 0 &&
                           window_plan.window_surface_bound == 0 &&
                           window_plan.window_input_bound == 0 &&
                           window_plan.window_auth_submit_allowed == 0 &&
                           window_plan.window_auth_attempt_allowed == 0 &&
                           window_plan.gui_submitted == 0 &&
                           window_plan.gui_pixels_write_allowed == 0 &&
                           window_plan.gui_pixels_written == 0 &&
                           window_plan.gui_auth_submit_allowed == 0 &&
                           window_plan.gui_auth_attempt_allowed == 0 &&
                           window_plan.release_submitted == 0 &&
                           window_plan.release_storage_prune_allowed == 0 &&
                           window_plan.release_storage_pruned == 0 &&
                           window_plan.reclaim_submitted == 0 &&
                           window_plan.compaction_submitted == 0 &&
                           window_plan.submit_enabled == 0 &&
                           window_plan.auth_attempt_allowed == 0,
                       "credential window plan must not copy unsafe submitted GUI state");
  fails += expect_true(strings_equal(window_plan.window_ticket,
                                     "text-login-fallback-window-ticket") &&
                           strings_equal(window_plan.event_type,
                                         "credential-screen-window-plan-unsafe") &&
                           strings_equal(window_plan.blocked_reason,
                                         "credential-window-plan-unsafe"),
                       "credential window plan submitted GUI should report unsafe upstream");
  return fails;
}

int build_loginwindow_credential_screen_window_surface_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage,
    struct login_window_credential_screen_window_surface_plan *surface_plan) {
  struct login_window_credential_screen_window_plan window_plan;

  if (build_loginwindow_credential_screen_window_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &window_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_window_surface_plan_build(
      &window_plan, surface_plan);
}

static int test_loginwindow_credential_screen_window_surface_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_window_surface_plan surface_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_surface_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0, 1,
          &surface_plan) == 0,
      "credential window surface plan edit should build");
  fails += expect_true(
      surface_plan.version ==
          LOGIN_WINDOW_CREDENTIAL_SCREEN_WINDOW_SURFACE_PLAN_VERSION,
      "credential window surface plan should expose stable version");
  fails += expect_true(surface_plan.window_plan_available == 1 &&
                           surface_plan.window_plan_safe == 1 &&
                           surface_plan.window_surface_plan_safe == 1,
                       "credential window surface plan should require safe window plan");
  fails += expect_true(surface_plan.surface_required == 1 &&
                           surface_plan.surface_allowed == 1 &&
                           surface_plan.surface_bound == 0 &&
                           surface_plan.surface_ticket_selected == 1 &&
                           surface_plan.surface_target_selected == 1 &&
                           surface_plan.surface_memory_mapped == 0 &&
                           surface_plan.surface_pixels_written == 0 &&
                           surface_plan.surface_compositor_submit_allowed == 0 &&
                           surface_plan.surface_compositor_submitted == 0 &&
                           surface_plan.surface_auth_submit_allowed == 0 &&
                           surface_plan.surface_auth_attempt_allowed == 0,
                       "credential window surface plan should remain declarative");
  fails += expect_true(surface_plan.window_allowed == 1 &&
                           surface_plan.window_created == 0 &&
                           surface_plan.window_surface_bound == 0 &&
                           surface_plan.window_input_bound == 0 &&
                           surface_plan.gui_submitted == 0 &&
                           surface_plan.gui_pixels_written == 0 &&
                           surface_plan.release_submitted == 0 &&
                           surface_plan.reclaim_submitted == 0 &&
                           surface_plan.compaction_submitted == 0,
                       "credential window surface plan must not execute upstream work");
  fails += expect_true(surface_plan.surface_credential_panel == 1 &&
                           surface_plan.surface_credential_input == 1 &&
                           surface_plan.surface_credential_focus == 1 &&
                           surface_plan.surface_text_login == 0 &&
                           surface_plan.surface_text_login_fallback == 0,
                       "credential window surface plan should mark credential widgets");
  fails += expect_true(surface_plan.submit_callback_bound == 0 &&
                           surface_plan.auth_callback_bound == 0 &&
                           surface_plan.submit_enabled == 0 &&
                           surface_plan.auth_attempt_allowed == 0 &&
                           surface_plan.raw_secret_exposed == 0 &&
                           surface_plan.masked_text_exposed == 0,
                       "credential window surface plan must stay redacted and auth-disabled");
  fails += expect_true(
      strings_equal(surface_plan.surface_ticket,
                    "credential-screen-window-surface-ticket") &&
          strings_equal(surface_plan.window_ticket,
                        "credential-screen-window-ticket") &&
          strings_equal(surface_plan.surface_policy,
                        "declarative-surface-no-bind") &&
          strings_equal(surface_plan.state,
                        "window-surface-credential-ready"),
      "credential window surface plan should report surface ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_surface_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_window_surface_plan surface_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_surface_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
          LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1, 1,
          &surface_plan) == 0,
      "credential window surface plan recovery should build");
  fails += expect_true(surface_plan.window_surface_plan_safe == 1 &&
                           surface_plan.surface_allowed == 1 &&
                           surface_plan.surface_bound == 0 &&
                           surface_plan.surface_memory_mapped == 0 &&
                           surface_plan.surface_text_recovery == 1 &&
                           surface_plan.surface_text_login == 1 &&
                           surface_plan.surface_credential_focus == 0,
                       "credential window surface plan recovery should mark text recovery");
  fails += expect_true(surface_plan.surface_compositor_submitted == 0 &&
                           surface_plan.surface_pixels_written == 0 &&
                           surface_plan.surface_auth_submit_allowed == 0 &&
                           surface_plan.surface_auth_attempt_allowed == 0 &&
                           surface_plan.submit_enabled == 0 &&
                           surface_plan.auth_attempt_allowed == 0,
                       "credential window surface plan recovery must not bind or authenticate");
  fails += expect_true(
      strings_equal(surface_plan.surface_ticket,
                    "text-recovery-window-surface-ticket") &&
          strings_equal(surface_plan.compositor_target,
                        "text-recovery-window-surface") &&
          strings_equal(surface_plan.blocked_reason, "text-recovery-only"),
      "credential window surface plan recovery should report recovery ticket");

  fails += expect_true(
      build_loginwindow_credential_screen_window_surface_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0, 1,
          &surface_plan) == 0,
      "credential window surface plan resume should build");
  fails += expect_true(surface_plan.window_surface_plan_safe == 1 &&
                           surface_plan.surface_text_login_resume == 1 &&
                           surface_plan.session_reset_required == 1 &&
                           surface_plan.login_screen_rerender_required == 1 &&
                           surface_plan.surface_bound == 0 &&
                           surface_plan.surface_compositor_submitted == 0 &&
                           surface_plan.submit_enabled == 0 &&
                           surface_plan.auth_attempt_allowed == 0,
                       "credential window surface plan resume should keep surface auth disabled");
  fails += expect_true(
      strings_equal(surface_plan.surface_ticket,
                    "text-login-resume-window-surface-ticket") &&
          strings_equal(surface_plan.surface_policy,
                        "full-window-surface-declarative") &&
          strings_equal(surface_plan.state, "window-surface-resume-ready"),
      "credential window surface plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_surface_plan_falls_back_for_submit_and_unknown_action(void) {
  int fails = 0;
  struct login_window_credential_screen_window_surface_plan surface_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_surface_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_SUBMIT, 0, 0, 0, 0, 1,
          &surface_plan) == 0,
      "credential window surface plan submit should build");
  fails += expect_true(surface_plan.window_surface_plan_safe == 1 &&
                           surface_plan.submit_requested == 1 &&
                           surface_plan.submit_blocked == 1 &&
                           surface_plan.action_allowed == 0 &&
                           surface_plan.action_blocked == 1 &&
                           surface_plan.input_focus_allowed == 0 &&
                           surface_plan.surface_text_login == 1 &&
                           surface_plan.surface_text_login_fallback == 1 &&
                           surface_plan.surface_bound == 0 &&
                           surface_plan.surface_compositor_submitted == 0,
                       "credential window surface plan submit should force text login");
  fails += expect_true(
      strings_equal(surface_plan.surface_ticket,
                    "text-login-fallback-window-surface-ticket") &&
          strings_equal(surface_plan.surface_policy,
                        "fallback-window-surface-declarative") &&
          strings_equal(surface_plan.blocked_reason, "gui-submit-disabled"),
      "credential window surface plan submit should report disabled GUI submit");

  fails += expect_true(
      build_loginwindow_credential_screen_window_surface_plan_for_action(
          9876, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0, 1,
          &surface_plan) == 0,
      "credential window surface plan unknown action should build");
  fails += expect_true(surface_plan.window_surface_plan_safe == 1 &&
                           surface_plan.action_allowed == 0 &&
                           surface_plan.action_blocked == 1 &&
                           surface_plan.input_focus_allowed == 0 &&
                           surface_plan.surface_text_login == 1 &&
                           surface_plan.surface_text_login_fallback == 1 &&
                           surface_plan.surface_bound == 0 &&
                           surface_plan.surface_memory_mapped == 0 &&
                           surface_plan.surface_compositor_submitted == 0,
                       "credential window surface plan unknown action should force text login");
  fails += expect_true(
      strings_equal(surface_plan.surface_ticket,
                    "text-login-fallback-window-surface-ticket") &&
          strings_equal(surface_plan.compositor_target,
                        "text-login-fallback-window-surface") &&
          strings_equal(surface_plan.state,
                        "window-surface-text-login-ready"),
      "credential window surface plan unknown action should report fallback ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_surface_plan_fails_closed_for_unsafe_or_missing_window_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_window_plan window_plan;
  struct login_window_credential_screen_window_surface_plan surface_plan;

  fails += expect_true(
      login_window_credential_screen_window_surface_plan_build(
          NULL, &surface_plan) == 0,
      "credential window surface plan missing window should build fallback");
  fails += expect_true(surface_plan.window_plan_available == 0 &&
                           surface_plan.window_plan_safe == 0 &&
                           surface_plan.window_surface_plan_safe == 0 &&
                           surface_plan.route_blocked == 1 &&
                           surface_plan.surface_allowed == 0 &&
                           surface_plan.surface_ticket_selected == 0 &&
                           surface_plan.surface_target_selected == 0 &&
                           surface_plan.surface_text_login == 1 &&
                           surface_plan.surface_text_login_fallback == 1 &&
                           surface_plan.surface_bound == 0 &&
                           surface_plan.surface_memory_mapped == 0,
                       "credential window surface plan missing window should fail closed");
  fails += expect_true(
      strings_equal(surface_plan.surface_ticket,
                    "text-login-fallback-window-surface-ticket") &&
          strings_equal(surface_plan.event_type,
                        "credential-screen-window-surface-plan-unavailable") &&
          strings_equal(surface_plan.blocked_reason,
                        "window-plan-unavailable"),
      "credential window surface plan missing window should report missing upstream");

  fails += expect_true(build_loginwindow_credential_screen_window_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0, 0,
                           1, &window_plan) == 0,
                       "credential window surface plan unsafe window fixture should build");
  window_plan.window_plan_safe = 0;
  window_plan.raw_secret_exposed = 1;
  window_plan.submit_blocked = 0;
  fails += expect_true(
      login_window_credential_screen_window_surface_plan_build(
          &window_plan, &surface_plan) == 0,
      "credential window surface plan unsafe window should build fallback");
  fails += expect_true(surface_plan.window_plan_available == 1 &&
                           surface_plan.window_plan_safe == 0 &&
                           surface_plan.window_surface_plan_safe == 0 &&
                           surface_plan.route_blocked == 1 &&
                           surface_plan.surface_allowed == 0 &&
                           surface_plan.surface_ticket_selected == 0 &&
                           surface_plan.surface_target_selected == 0 &&
                           surface_plan.surface_text_login == 1 &&
                           surface_plan.surface_text_login_fallback == 1 &&
                           surface_plan.surface_bound == 0 &&
                           surface_plan.raw_secret_exposed == 0,
                       "credential window surface plan unsafe window should fail closed");
  fails += expect_true(
      strings_equal(surface_plan.surface_ticket,
                    "text-login-fallback-window-surface-ticket") &&
          strings_equal(surface_plan.event_type,
                        "credential-screen-window-surface-plan-unsafe") &&
          strings_equal(surface_plan.blocked_reason,
                        "credential-window-surface-plan-unsafe"),
      "credential window surface plan unsafe window should report unsafe upstream");

  fails += expect_true(build_loginwindow_credential_screen_window_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'g', 0, 0, 0,
                           1, &window_plan) == 0,
                       "credential window surface plan forged GUI fixture should build");
  window_plan.gui_plan_available = 0;
  window_plan.gui_plan_safe = 0;
  fails += expect_true(
      login_window_credential_screen_window_surface_plan_build(
          &window_plan, &surface_plan) == 0,
      "credential window surface plan forged GUI origin should build fallback");
  fails += expect_true(surface_plan.window_plan_available == 1 &&
                           surface_plan.window_plan_safe == 1 &&
                           surface_plan.window_surface_plan_safe == 0 &&
                           surface_plan.route_blocked == 1 &&
                           surface_plan.surface_allowed == 0 &&
                           surface_plan.surface_text_login == 1 &&
                           surface_plan.surface_bound == 0 &&
                           surface_plan.surface_compositor_submitted == 0,
                       "credential window surface plan should reject unsafe GUI origin");
  fails += expect_true(
      strings_equal(surface_plan.event_type,
                    "credential-screen-window-surface-plan-unsafe") &&
          strings_equal(surface_plan.blocked_reason,
                        "credential-window-surface-plan-unsafe"),
      "credential window surface plan forged GUI origin should report unsafe upstream");

  fails += expect_true(build_loginwindow_credential_screen_window_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'p', 0, 0, 0,
                           1, &window_plan) == 0,
                       "credential window surface plan bound window fixture should build");
  window_plan.window_created = 1;
  window_plan.window_surface_bound = 1;
  window_plan.window_input_bound = 1;
  window_plan.window_auth_submit_allowed = 1;
  window_plan.window_auth_attempt_allowed = 1;
  window_plan.gui_submitted = 1;
  window_plan.gui_pixels_write_allowed = 1;
  window_plan.gui_pixels_written = 1;
  window_plan.gui_auth_submit_allowed = 1;
  window_plan.gui_auth_attempt_allowed = 1;
  window_plan.release_submitted = 1;
  window_plan.release_storage_prune_allowed = 1;
  window_plan.release_storage_pruned = 1;
  window_plan.reclaim_submitted = 1;
  window_plan.compaction_submitted = 1;
  fails += expect_true(
      login_window_credential_screen_window_surface_plan_build(
          &window_plan, &surface_plan) == 0,
      "credential window surface plan bound window should build fallback");
  fails += expect_true(surface_plan.window_surface_plan_safe == 0 &&
                           surface_plan.surface_allowed == 0 &&
                           surface_plan.surface_bound == 0 &&
                           surface_plan.surface_memory_mapped == 0 &&
                           surface_plan.surface_pixels_written == 0 &&
                           surface_plan.surface_compositor_submit_allowed == 0 &&
                           surface_plan.surface_compositor_submitted == 0 &&
                           surface_plan.surface_auth_submit_allowed == 0 &&
                           surface_plan.surface_auth_attempt_allowed == 0 &&
                           surface_plan.window_created == 0 &&
                           surface_plan.window_surface_bound == 0 &&
                           surface_plan.window_input_bound == 0 &&
                           surface_plan.window_auth_submit_allowed == 0 &&
                           surface_plan.window_auth_attempt_allowed == 0 &&
                           surface_plan.gui_submitted == 0 &&
                           surface_plan.gui_pixels_write_allowed == 0 &&
                           surface_plan.gui_pixels_written == 0 &&
                           surface_plan.release_submitted == 0 &&
                           surface_plan.reclaim_submitted == 0 &&
                           surface_plan.compaction_submitted == 0 &&
                           surface_plan.submit_enabled == 0 &&
                           surface_plan.auth_attempt_allowed == 0,
                       "credential window surface plan must not copy unsafe bound window state");
  fails += expect_true(
      strings_equal(surface_plan.surface_ticket,
                    "text-login-fallback-window-surface-ticket") &&
          strings_equal(surface_plan.event_type,
                        "credential-screen-window-surface-plan-unsafe") &&
          strings_equal(surface_plan.blocked_reason,
                        "credential-window-surface-plan-unsafe"),
      "credential window surface plan bound window should report unsafe upstream");
  return fails;
}

int test_login_runtime_credential_window_surface_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_window_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_window_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_window_plan_falls_back_for_submit_and_unknown_action();
  fails += test_loginwindow_credential_screen_window_plan_fails_closed_for_unsafe_or_missing_gui_plan();
  fails += test_loginwindow_credential_screen_window_surface_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_window_surface_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_window_surface_plan_falls_back_for_submit_and_unknown_action();
  fails += test_loginwindow_credential_screen_window_surface_plan_fails_closed_for_unsafe_or_missing_window_plan();
  return fails;
}
