/*
 * tests/auth/test_login_runtime_credential_release_gui.c
 *
 * Credential screen release plan + GUI plan coverage for the
 * `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-16 monolith
 * refactor (PR D.32 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_release_plan_build`: 4 tests
 *     covering the credential widgets release + the text-route
 *     release (recovery + resume) + the submit/unknown fallback
 *     release + the missing-or-unsafe reclaim plan fail-closed
 *     default.
 *   - `login_window_credential_screen_gui_plan_build`: 4 tests
 *     covering the credential widgets GUI + the text-route GUI
 *     (recovery + resume) + the submit/unknown fallback GUI + the
 *     missing-or-unsafe release plan fail-closed default.
 *
 * Also exposes shared helpers
 * `build_loginwindow_credential_screen_release_plan_for_action` and
 * `build_loginwindow_credential_screen_gui_plan_for_action`, used
 * by later companion files that chain on top of the release/GUI
 * stages (window, ...).
 *
 * The companion entry `test_login_runtime_credential_release_gui_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_release_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_release_plan *release_plan) {
  struct login_window_credential_screen_reclaim_plan reclaim_plan;

  if (build_loginwindow_credential_screen_reclaim_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &reclaim_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_release_plan_build(&reclaim_plan,
                                                          release_plan);
}

static int test_loginwindow_credential_screen_release_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_release_plan release_plan;

  fails += expect_true(build_loginwindow_credential_screen_release_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'c', 0, 0, 0,
                           1, &release_plan) == 0,
                       "credential release plan edit should build");
  fails += expect_true(release_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_RELEASE_PLAN_VERSION,
                       "credential release plan should expose stable version");
  fails += expect_true(release_plan.reclaim_plan_available == 1 &&
                           release_plan.reclaim_plan_safe == 1 &&
                           release_plan.release_plan_safe == 1,
                       "credential release plan should require safe reclaim plan");
  fails += expect_true(release_plan.release_required == 1 &&
                           release_plan.release_allowed == 1 &&
                           release_plan.release_submitted == 0 &&
                           release_plan.release_ticket_selected == 1 &&
                           release_plan.release_target_selected == 1 &&
                           release_plan.release_storage_prune_allowed == 0 &&
                           release_plan.release_storage_pruned == 0 &&
                           release_plan.release_resource_release_allowed == 0 &&
                           release_plan.release_resource_released == 0 &&
                           release_plan.release_cpu_gpu_sync_allowed == 0 &&
                           release_plan.release_cpu_gpu_sync_submitted == 0,
                       "credential release plan should remain declarative");
  fails += expect_true(release_plan.reclaim_allowed == 1 &&
                           release_plan.reclaim_submitted == 0 &&
                           release_plan.reclaim_storage_pruned == 0 &&
                           release_plan.reclaim_resource_released == 0 &&
                           release_plan.compaction_submitted == 0 &&
                           release_plan.compaction_storage_written == 0,
                       "credential release plan must not execute upstream work");
  fails += expect_true(release_plan.release_credential_panel == 1 &&
                           release_plan.release_credential_input == 1 &&
                           release_plan.release_credential_focus == 1 &&
                           release_plan.release_text_login == 0 &&
                           release_plan.release_text_login_fallback == 0,
                       "credential release plan should mark credential widgets");
  fails += expect_true(release_plan.submit_callback_bound == 0 &&
                           release_plan.auth_callback_bound == 0 &&
                           release_plan.submit_enabled == 0 &&
                           release_plan.auth_attempt_allowed == 0 &&
                           release_plan.raw_secret_exposed == 0 &&
                           release_plan.masked_text_exposed == 0,
                       "credential release plan must stay redacted and auth-disabled");
  fails += expect_true(strings_equal(release_plan.release_ticket,
                                     "credential-screen-release-ticket") &&
                           strings_equal(release_plan.reclaim_ticket,
                                         "credential-screen-reclaim-ticket") &&
                           strings_equal(release_plan.release_policy,
                                         "declarative-release-no-resource") &&
                           strings_equal(release_plan.state,
                                         "release-credential-ready"),
                       "credential release plan should report release ticket");
  return fails;
}

static int test_loginwindow_credential_screen_release_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_release_plan release_plan;

  fails += expect_true(build_loginwindow_credential_screen_release_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &release_plan) == 0,
                       "credential release plan recovery should build");
  fails += expect_true(release_plan.release_plan_safe == 1 &&
                           release_plan.release_allowed == 1 &&
                           release_plan.release_submitted == 0 &&
                           release_plan.release_storage_pruned == 0 &&
                           release_plan.release_text_recovery == 1 &&
                           release_plan.release_text_login == 1 &&
                           release_plan.release_credential_focus == 0,
                       "credential release plan recovery should mark text recovery");
  fails += expect_true(release_plan.reclaim_submitted == 0 &&
                           release_plan.reclaim_storage_pruned == 0 &&
                           release_plan.release_resource_released == 0 &&
                           release_plan.release_cpu_gpu_sync_submitted == 0 &&
                           release_plan.submit_enabled == 0 &&
                           release_plan.auth_attempt_allowed == 0,
                       "credential release plan recovery must not release or authenticate");
  fails += expect_true(strings_equal(release_plan.release_ticket,
                                     "text-recovery-release-ticket") &&
                           strings_equal(release_plan.compositor_target,
                                         "text-recovery-release") &&
                           strings_equal(release_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential release plan recovery should report recovery ticket");

  fails += expect_true(build_loginwindow_credential_screen_release_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &release_plan) == 0,
                       "credential release plan resume should build");
  fails += expect_true(release_plan.release_plan_safe == 1 &&
                           release_plan.release_text_login_resume == 1 &&
                           release_plan.session_reset_required == 1 &&
                           release_plan.login_screen_rerender_required == 1 &&
                           release_plan.release_submitted == 0 &&
                           release_plan.release_storage_pruned == 0 &&
                           release_plan.submit_enabled == 0 &&
                           release_plan.auth_attempt_allowed == 0,
                       "credential release plan resume should keep GUI auth disabled");
  fails += expect_true(strings_equal(release_plan.release_ticket,
                                     "text-login-resume-release-ticket") &&
                           strings_equal(release_plan.release_policy,
                                         "full-release-declarative") &&
                           strings_equal(release_plan.state,
                                         "release-resume-ready"),
                       "credential release plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_release_plan_falls_back_for_submit_and_unknown_action(void) {
  int fails = 0;
  struct login_window_credential_screen_release_plan release_plan;

  fails += expect_true(build_loginwindow_credential_screen_release_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_SUBMIT, 0, 0, 0, 0,
                           1, &release_plan) == 0,
                       "credential release plan submit should build");
  fails += expect_true(release_plan.release_plan_safe == 1 &&
                           release_plan.submit_requested == 1 &&
                           release_plan.submit_blocked == 1 &&
                           release_plan.action_allowed == 0 &&
                           release_plan.action_blocked == 1 &&
                           release_plan.input_focus_allowed == 0 &&
                           release_plan.release_text_login == 1 &&
                           release_plan.release_text_login_fallback == 1 &&
                           release_plan.release_submitted == 0 &&
                           release_plan.release_resource_released == 0,
                       "credential release plan submit should force text login");
  fails += expect_true(strings_equal(release_plan.release_ticket,
                                     "text-login-fallback-release-ticket") &&
                           strings_equal(release_plan.release_policy,
                                         "fallback-release-declarative") &&
                           strings_equal(release_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential release plan submit should report disabled GUI submit");

  fails += expect_true(build_loginwindow_credential_screen_release_plan_for_action(
                           9876, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0,
                           0, 0, 1, &release_plan) == 0,
                       "credential release plan unknown action should build");
  fails += expect_true(release_plan.release_plan_safe == 1 &&
                           release_plan.action_allowed == 0 &&
                           release_plan.action_blocked == 1 &&
                           release_plan.input_focus_allowed == 0 &&
                           release_plan.release_text_login == 1 &&
                           release_plan.release_text_login_fallback == 1 &&
                           release_plan.release_submitted == 0 &&
                           release_plan.release_storage_pruned == 0 &&
                           release_plan.release_cpu_gpu_sync_submitted == 0,
                       "credential release plan unknown action should force text login");
  fails += expect_true(strings_equal(release_plan.release_ticket,
                                     "text-login-fallback-release-ticket") &&
                           strings_equal(release_plan.compositor_target,
                                         "text-login-fallback-release") &&
                           strings_equal(release_plan.state,
                                         "release-text-login-ready"),
                       "credential release plan unknown action should report fallback ticket");
  return fails;
}

static int test_loginwindow_credential_screen_release_plan_fails_closed_for_unsafe_or_missing_reclaim_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_reclaim_plan reclaim_plan;
  struct login_window_credential_screen_release_plan release_plan;

  fails += expect_true(login_window_credential_screen_release_plan_build(
                           NULL, &release_plan) == 0,
                       "credential release plan missing reclaim should build fallback");
  fails += expect_true(release_plan.reclaim_plan_available == 0 &&
                           release_plan.reclaim_plan_safe == 0 &&
                           release_plan.release_plan_safe == 0 &&
                           release_plan.route_blocked == 1 &&
                           release_plan.release_allowed == 0 &&
                           release_plan.release_ticket_selected == 0 &&
                           release_plan.release_target_selected == 0 &&
                           release_plan.release_text_login == 1 &&
                           release_plan.release_text_login_fallback == 1 &&
                           release_plan.release_submitted == 0 &&
                           release_plan.release_storage_pruned == 0 &&
                           release_plan.release_resource_released == 0,
                       "credential release plan missing reclaim should fail closed");
  fails += expect_true(strings_equal(release_plan.release_ticket,
                                     "text-login-fallback-release-ticket") &&
                           strings_equal(release_plan.event_type,
                                         "credential-screen-release-plan-unavailable") &&
                           strings_equal(release_plan.blocked_reason,
                                         "reclaim-plan-unavailable"),
                       "credential release plan missing reclaim should report missing upstream");

  fails += expect_true(build_loginwindow_credential_screen_reclaim_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0, 0,
                           1, &reclaim_plan) == 0,
                       "credential release plan unsafe reclaim fixture should build");
  reclaim_plan.reclaim_plan_safe = 0;
  reclaim_plan.raw_secret_exposed = 1;
  reclaim_plan.submit_blocked = 0;
  fails += expect_true(login_window_credential_screen_release_plan_build(
                           &reclaim_plan, &release_plan) == 0,
                       "credential release plan unsafe reclaim should build fallback");
  fails += expect_true(release_plan.reclaim_plan_available == 1 &&
                           release_plan.reclaim_plan_safe == 0 &&
                           release_plan.release_plan_safe == 0 &&
                           release_plan.route_blocked == 1 &&
                           release_plan.release_allowed == 0 &&
                           release_plan.release_ticket_selected == 0 &&
                           release_plan.release_target_selected == 0 &&
                           release_plan.release_text_login == 1 &&
                           release_plan.release_text_login_fallback == 1 &&
                           release_plan.release_submitted == 0 &&
                           release_plan.release_storage_pruned == 0 &&
                           release_plan.release_resource_released == 0 &&
                           release_plan.raw_secret_exposed == 0,
                       "credential release plan unsafe reclaim should fail closed");
  fails += expect_true(strings_equal(release_plan.release_ticket,
                                     "text-login-fallback-release-ticket") &&
                           strings_equal(release_plan.event_type,
                                         "credential-screen-release-plan-unsafe") &&
                           strings_equal(release_plan.blocked_reason,
                                         "credential-release-plan-unsafe"),
                       "credential release plan unsafe reclaim should report unsafe upstream");

  fails += expect_true(build_loginwindow_credential_screen_reclaim_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 0, 0, 0,
                           1, &reclaim_plan) == 0,
                       "credential release plan submitted reclaim fixture should build");
  reclaim_plan.reclaim_submitted = 1;
  reclaim_plan.reclaim_storage_prune_allowed = 1;
  reclaim_plan.reclaim_storage_pruned = 1;
  reclaim_plan.reclaim_resource_release_allowed = 1;
  reclaim_plan.reclaim_resource_released = 1;
  reclaim_plan.reclaim_cpu_gpu_sync_allowed = 1;
  reclaim_plan.reclaim_cpu_gpu_sync_submitted = 1;
  reclaim_plan.compaction_submitted = 1;
  reclaim_plan.compaction_storage_write_allowed = 1;
  reclaim_plan.compaction_storage_written = 1;
  reclaim_plan.compaction_resource_release_allowed = 1;
  reclaim_plan.compaction_resource_released = 1;
  reclaim_plan.compaction_cpu_gpu_sync_allowed = 1;
  reclaim_plan.compaction_cpu_gpu_sync_submitted = 1;
  fails += expect_true(login_window_credential_screen_release_plan_build(
                           &reclaim_plan, &release_plan) == 0,
                       "credential release plan submitted reclaim should build fallback");
  fails += expect_true(release_plan.release_plan_safe == 0 &&
                           release_plan.release_allowed == 0 &&
                           release_plan.release_submitted == 0 &&
                           release_plan.release_storage_prune_allowed == 0 &&
                           release_plan.release_storage_pruned == 0 &&
                           release_plan.release_resource_release_allowed == 0 &&
                           release_plan.release_resource_released == 0 &&
                           release_plan.release_cpu_gpu_sync_allowed == 0 &&
                           release_plan.release_cpu_gpu_sync_submitted == 0 &&
                           release_plan.reclaim_submitted == 0 &&
                           release_plan.reclaim_storage_prune_allowed == 0 &&
                           release_plan.reclaim_storage_pruned == 0 &&
                           release_plan.reclaim_resource_release_allowed == 0 &&
                           release_plan.reclaim_resource_released == 0 &&
                           release_plan.reclaim_cpu_gpu_sync_allowed == 0 &&
                           release_plan.reclaim_cpu_gpu_sync_submitted == 0 &&
                           release_plan.compaction_submitted == 0 &&
                           release_plan.compaction_storage_write_allowed == 0 &&
                           release_plan.compaction_storage_written == 0 &&
                           release_plan.compaction_resource_release_allowed == 0 &&
                           release_plan.compaction_resource_released == 0 &&
                           release_plan.compaction_cpu_gpu_sync_allowed == 0 &&
                           release_plan.compaction_cpu_gpu_sync_submitted == 0 &&
                           release_plan.submit_enabled == 0 &&
                           release_plan.auth_attempt_allowed == 0,
                       "credential release plan must not copy unsafe submitted reclaim state");
  fails += expect_true(strings_equal(release_plan.release_ticket,
                                     "text-login-fallback-release-ticket") &&
                           strings_equal(release_plan.event_type,
                                         "credential-screen-release-plan-unsafe") &&
                           strings_equal(release_plan.blocked_reason,
                                         "credential-release-plan-unsafe"),
                       "credential release plan submitted reclaim should report unsafe upstream");
  return fails;
}

int build_loginwindow_credential_screen_gui_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_gui_plan *gui_plan) {
  struct login_window_credential_screen_release_plan release_plan;

  if (build_loginwindow_credential_screen_release_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &release_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_gui_plan_build(&release_plan, gui_plan);
}

static int test_loginwindow_credential_screen_gui_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_gui_plan gui_plan;

  fails += expect_true(build_loginwindow_credential_screen_gui_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'g', 0, 0, 0,
                           1, &gui_plan) == 0,
                       "credential GUI plan edit should build");
  fails += expect_true(gui_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_GUI_PLAN_VERSION,
                       "credential GUI plan should expose stable version");
  fails += expect_true(gui_plan.release_plan_available == 1 &&
                           gui_plan.release_plan_safe == 1 &&
                           gui_plan.gui_plan_safe == 1,
                       "credential GUI plan should require safe release plan");
  fails += expect_true(gui_plan.gui_required == 1 &&
                           gui_plan.gui_allowed == 1 &&
                           gui_plan.gui_submitted == 0 &&
                           gui_plan.gui_ticket_selected == 1 &&
                           gui_plan.gui_target_selected == 1 &&
                           gui_plan.gui_pixels_write_allowed == 0 &&
                           gui_plan.gui_pixels_written == 0 &&
                           gui_plan.gui_auth_submit_allowed == 0 &&
                           gui_plan.gui_auth_attempt_allowed == 0,
                       "credential GUI plan should remain declarative");
  fails += expect_true(gui_plan.release_allowed == 1 &&
                           gui_plan.release_submitted == 0 &&
                           gui_plan.release_storage_pruned == 0 &&
                           gui_plan.release_resource_released == 0 &&
                           gui_plan.reclaim_submitted == 0 &&
                           gui_plan.compaction_submitted == 0,
                       "credential GUI plan must not execute upstream work");
  fails += expect_true(gui_plan.gui_credential_panel == 1 &&
                           gui_plan.gui_credential_input == 1 &&
                           gui_plan.gui_credential_focus == 1 &&
                           gui_plan.gui_text_login == 0 &&
                           gui_plan.gui_text_login_fallback == 0,
                       "credential GUI plan should mark credential widgets");
  fails += expect_true(gui_plan.submit_callback_bound == 0 &&
                           gui_plan.auth_callback_bound == 0 &&
                           gui_plan.submit_enabled == 0 &&
                           gui_plan.auth_attempt_allowed == 0 &&
                           gui_plan.raw_secret_exposed == 0 &&
                           gui_plan.masked_text_exposed == 0,
                       "credential GUI plan must stay redacted and auth-disabled");
  fails += expect_true(strings_equal(gui_plan.gui_ticket,
                                     "credential-screen-gui-ticket") &&
                           strings_equal(gui_plan.release_ticket,
                                         "credential-screen-release-ticket") &&
                           strings_equal(gui_plan.gui_policy,
                                         "declarative-gui-no-pixels") &&
                           strings_equal(gui_plan.state,
                                         "gui-credential-ready"),
                       "credential GUI plan should report GUI ticket");
  return fails;
}

static int test_loginwindow_credential_screen_gui_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_gui_plan gui_plan;

  fails += expect_true(build_loginwindow_credential_screen_gui_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &gui_plan) == 0,
                       "credential GUI plan recovery should build");
  fails += expect_true(gui_plan.gui_plan_safe == 1 &&
                           gui_plan.gui_allowed == 1 &&
                           gui_plan.gui_submitted == 0 &&
                           gui_plan.gui_pixels_written == 0 &&
                           gui_plan.gui_text_recovery == 1 &&
                           gui_plan.gui_text_login == 1 &&
                           gui_plan.gui_credential_focus == 0,
                       "credential GUI plan recovery should mark text recovery");
  fails += expect_true(gui_plan.release_submitted == 0 &&
                           gui_plan.release_storage_pruned == 0 &&
                           gui_plan.gui_auth_submit_allowed == 0 &&
                           gui_plan.gui_auth_attempt_allowed == 0 &&
                           gui_plan.submit_enabled == 0 &&
                           gui_plan.auth_attempt_allowed == 0,
                       "credential GUI plan recovery must not draw or authenticate");
  fails += expect_true(strings_equal(gui_plan.gui_ticket,
                                     "text-recovery-gui-ticket") &&
                           strings_equal(gui_plan.compositor_target,
                                         "text-recovery-gui") &&
                           strings_equal(gui_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential GUI plan recovery should report recovery ticket");

  fails += expect_true(build_loginwindow_credential_screen_gui_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &gui_plan) == 0,
                       "credential GUI plan resume should build");
  fails += expect_true(gui_plan.gui_plan_safe == 1 &&
                           gui_plan.gui_text_login_resume == 1 &&
                           gui_plan.session_reset_required == 1 &&
                           gui_plan.login_screen_rerender_required == 1 &&
                           gui_plan.gui_submitted == 0 &&
                           gui_plan.gui_pixels_written == 0 &&
                           gui_plan.submit_enabled == 0 &&
                           gui_plan.auth_attempt_allowed == 0,
                       "credential GUI plan resume should keep GUI auth disabled");
  fails += expect_true(strings_equal(gui_plan.gui_ticket,
                                     "text-login-resume-gui-ticket") &&
                           strings_equal(gui_plan.gui_policy,
                                         "full-gui-declarative") &&
                           strings_equal(gui_plan.state,
                                         "gui-resume-ready"),
                       "credential GUI plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_gui_plan_falls_back_for_submit_and_unknown_action(void) {
  int fails = 0;
  struct login_window_credential_screen_gui_plan gui_plan;

  fails += expect_true(build_loginwindow_credential_screen_gui_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_SUBMIT, 0, 0, 0, 0,
                           1, &gui_plan) == 0,
                       "credential GUI plan submit should build");
  fails += expect_true(gui_plan.gui_plan_safe == 1 &&
                           gui_plan.submit_requested == 1 &&
                           gui_plan.submit_blocked == 1 &&
                           gui_plan.action_allowed == 0 &&
                           gui_plan.action_blocked == 1 &&
                           gui_plan.input_focus_allowed == 0 &&
                           gui_plan.gui_text_login == 1 &&
                           gui_plan.gui_text_login_fallback == 1 &&
                           gui_plan.gui_submitted == 0 &&
                           gui_plan.gui_pixels_written == 0,
                       "credential GUI plan submit should force text login");
  fails += expect_true(strings_equal(gui_plan.gui_ticket,
                                     "text-login-fallback-gui-ticket") &&
                           strings_equal(gui_plan.gui_policy,
                                         "fallback-gui-declarative") &&
                           strings_equal(gui_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential GUI plan submit should report disabled GUI submit");

  fails += expect_true(build_loginwindow_credential_screen_gui_plan_for_action(
                           9876, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0,
                           0, 0, 1, &gui_plan) == 0,
                       "credential GUI plan unknown action should build");
  fails += expect_true(gui_plan.gui_plan_safe == 1 &&
                           gui_plan.action_allowed == 0 &&
                           gui_plan.action_blocked == 1 &&
                           gui_plan.input_focus_allowed == 0 &&
                           gui_plan.gui_text_login == 1 &&
                           gui_plan.gui_text_login_fallback == 1 &&
                           gui_plan.gui_submitted == 0 &&
                           gui_plan.gui_pixels_write_allowed == 0 &&
                           gui_plan.gui_pixels_written == 0,
                       "credential GUI plan unknown action should force text login");
  fails += expect_true(strings_equal(gui_plan.gui_ticket,
                                     "text-login-fallback-gui-ticket") &&
                           strings_equal(gui_plan.compositor_target,
                                         "text-login-fallback-gui") &&
                           strings_equal(gui_plan.state,
                                         "gui-text-login-ready"),
                       "credential GUI plan unknown action should report fallback ticket");
  return fails;
}

static int test_loginwindow_credential_screen_gui_plan_fails_closed_for_unsafe_or_missing_release_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_release_plan release_plan;
  struct login_window_credential_screen_gui_plan gui_plan;

  fails += expect_true(login_window_credential_screen_gui_plan_build(
                           NULL, &gui_plan) == 0,
                       "credential GUI plan missing release should build fallback");
  fails += expect_true(gui_plan.release_plan_available == 0 &&
                           gui_plan.release_plan_safe == 0 &&
                           gui_plan.gui_plan_safe == 0 &&
                           gui_plan.route_blocked == 1 &&
                           gui_plan.gui_allowed == 0 &&
                           gui_plan.gui_ticket_selected == 0 &&
                           gui_plan.gui_target_selected == 0 &&
                           gui_plan.gui_text_login == 1 &&
                           gui_plan.gui_text_login_fallback == 1 &&
                           gui_plan.gui_submitted == 0 &&
                           gui_plan.gui_pixels_written == 0,
                       "credential GUI plan missing release should fail closed");
  fails += expect_true(strings_equal(gui_plan.gui_ticket,
                                     "text-login-fallback-gui-ticket") &&
                           strings_equal(gui_plan.event_type,
                                         "credential-screen-gui-plan-unavailable") &&
                           strings_equal(gui_plan.blocked_reason,
                                         "release-plan-unavailable"),
                       "credential GUI plan missing release should report missing upstream");

  fails += expect_true(build_loginwindow_credential_screen_release_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0, 0,
                           1, &release_plan) == 0,
                       "credential GUI plan unsafe release fixture should build");
  release_plan.release_plan_safe = 0;
  release_plan.raw_secret_exposed = 1;
  release_plan.submit_blocked = 0;
  fails += expect_true(login_window_credential_screen_gui_plan_build(
                           &release_plan, &gui_plan) == 0,
                       "credential GUI plan unsafe release should build fallback");
  fails += expect_true(gui_plan.release_plan_available == 1 &&
                           gui_plan.release_plan_safe == 0 &&
                           gui_plan.gui_plan_safe == 0 &&
                           gui_plan.route_blocked == 1 &&
                           gui_plan.gui_allowed == 0 &&
                           gui_plan.gui_ticket_selected == 0 &&
                           gui_plan.gui_target_selected == 0 &&
                           gui_plan.gui_text_login == 1 &&
                           gui_plan.gui_text_login_fallback == 1 &&
                           gui_plan.gui_submitted == 0 &&
                           gui_plan.gui_pixels_written == 0 &&
                           gui_plan.raw_secret_exposed == 0,
                       "credential GUI plan unsafe release should fail closed");
  fails += expect_true(strings_equal(gui_plan.gui_ticket,
                                     "text-login-fallback-gui-ticket") &&
                           strings_equal(gui_plan.event_type,
                                         "credential-screen-gui-plan-unsafe") &&
                           strings_equal(gui_plan.blocked_reason,
                                         "credential-gui-plan-unsafe"),
                       "credential GUI plan unsafe release should report unsafe upstream");

  fails += expect_true(build_loginwindow_credential_screen_release_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'p', 0, 0, 0,
                           1, &release_plan) == 0,
                       "credential GUI plan submitted release fixture should build");
  release_plan.release_submitted = 1;
  release_plan.release_storage_prune_allowed = 1;
  release_plan.release_storage_pruned = 1;
  release_plan.release_resource_release_allowed = 1;
  release_plan.release_resource_released = 1;
  release_plan.release_cpu_gpu_sync_allowed = 1;
  release_plan.release_cpu_gpu_sync_submitted = 1;
  release_plan.reclaim_submitted = 1;
  release_plan.reclaim_storage_prune_allowed = 1;
  release_plan.reclaim_storage_pruned = 1;
  release_plan.reclaim_resource_release_allowed = 1;
  release_plan.reclaim_resource_released = 1;
  release_plan.reclaim_cpu_gpu_sync_allowed = 1;
  release_plan.reclaim_cpu_gpu_sync_submitted = 1;
  release_plan.compaction_submitted = 1;
  release_plan.compaction_storage_write_allowed = 1;
  release_plan.compaction_storage_written = 1;
  release_plan.compaction_resource_release_allowed = 1;
  release_plan.compaction_resource_released = 1;
  release_plan.compaction_cpu_gpu_sync_allowed = 1;
  release_plan.compaction_cpu_gpu_sync_submitted = 1;
  fails += expect_true(login_window_credential_screen_gui_plan_build(
                           &release_plan, &gui_plan) == 0,
                       "credential GUI plan submitted release should build fallback");
  fails += expect_true(gui_plan.gui_plan_safe == 0 &&
                           gui_plan.gui_allowed == 0 &&
                           gui_plan.gui_submitted == 0 &&
                           gui_plan.gui_pixels_write_allowed == 0 &&
                           gui_plan.gui_pixels_written == 0 &&
                           gui_plan.gui_auth_submit_allowed == 0 &&
                           gui_plan.gui_auth_attempt_allowed == 0 &&
                           gui_plan.release_submitted == 0 &&
                           gui_plan.release_storage_prune_allowed == 0 &&
                           gui_plan.release_storage_pruned == 0 &&
                           gui_plan.release_resource_release_allowed == 0 &&
                           gui_plan.release_resource_released == 0 &&
                           gui_plan.release_cpu_gpu_sync_allowed == 0 &&
                           gui_plan.release_cpu_gpu_sync_submitted == 0 &&
                           gui_plan.reclaim_submitted == 0 &&
                           gui_plan.reclaim_storage_prune_allowed == 0 &&
                           gui_plan.reclaim_storage_pruned == 0 &&
                           gui_plan.reclaim_resource_release_allowed == 0 &&
                           gui_plan.reclaim_resource_released == 0 &&
                           gui_plan.reclaim_cpu_gpu_sync_allowed == 0 &&
                           gui_plan.reclaim_cpu_gpu_sync_submitted == 0 &&
                           gui_plan.compaction_submitted == 0 &&
                           gui_plan.compaction_storage_write_allowed == 0 &&
                           gui_plan.compaction_storage_written == 0 &&
                           gui_plan.compaction_resource_release_allowed == 0 &&
                           gui_plan.compaction_resource_released == 0 &&
                           gui_plan.compaction_cpu_gpu_sync_allowed == 0 &&
                           gui_plan.compaction_cpu_gpu_sync_submitted == 0 &&
                           gui_plan.submit_enabled == 0 &&
                           gui_plan.auth_attempt_allowed == 0,
                       "credential GUI plan must not copy unsafe submitted release state");
  fails += expect_true(strings_equal(gui_plan.gui_ticket,
                                     "text-login-fallback-gui-ticket") &&
                           strings_equal(gui_plan.event_type,
                                         "credential-screen-gui-plan-unsafe") &&
                           strings_equal(gui_plan.blocked_reason,
                                         "credential-gui-plan-unsafe"),
                       "credential GUI plan submitted release should report unsafe upstream");
  return fails;
}

int test_login_runtime_credential_release_gui_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_release_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_release_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_release_plan_falls_back_for_submit_and_unknown_action();
  fails += test_loginwindow_credential_screen_release_plan_fails_closed_for_unsafe_or_missing_reclaim_plan();
  fails += test_loginwindow_credential_screen_gui_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_gui_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_gui_plan_falls_back_for_submit_and_unknown_action();
  fails += test_loginwindow_credential_screen_gui_plan_fails_closed_for_unsafe_or_missing_release_plan();
  return fails;
}
