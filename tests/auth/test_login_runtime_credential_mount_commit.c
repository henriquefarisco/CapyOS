/*
 * tests/auth/test_login_runtime_credential_mount_commit.c
 *
 * Credential screen mount plan + commit plan coverage for the
 * `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-15 monolith
 * refactor (PR D.10 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_mount_plan_build`: 5 tests
 *     covering the credential widgets mount + the text-recovery
 *     mount + the resume-text-login mount + the submit/unknown
 *     fallback mount + the missing-or-unsafe binding fail-closed
 *     default.
 *   - `login_window_credential_screen_commit_plan_build`: 5 tests
 *     covering the credential widgets commit + the text-recovery
 *     commit + the resume-text-login commit + the submit/unknown
 *     fallback commit + the missing-or-unsafe mount plan fail-closed
 *     default.
 *
 * Also exposes shared helpers
 * `build_loginwindow_credential_screen_mount_plan_for_action` and
 * `build_loginwindow_credential_screen_commit_plan_for_action`, used
 * by later companion files that chain on top of the mount/commit
 * stages (handoff, dispatch, queue, activation, frame, surface, ...).
 *
 * The companion entry `test_login_runtime_credential_mount_commit_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_mount_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_mount_plan *mount_plan) {
  struct login_window_credential_screen_binding binding;

  if (build_loginwindow_credential_screen_binding_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &binding) != 0) {
    return -1;
  }
  return login_window_credential_screen_mount_plan_build(&binding, mount_plan);
}

static int test_loginwindow_credential_screen_mount_plan_mounts_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_mount_plan mount_plan;

  fails += expect_true(build_loginwindow_credential_screen_mount_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'e', 0, 0, 0,
                           1, &mount_plan) == 0,
                       "credential mount plan edit should build");
  fails += expect_true(mount_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_MOUNT_PLAN_VERSION,
                       "credential mount plan should expose stable version");
  fails += expect_true(mount_plan.binding_available == 1 &&
                           mount_plan.binding_safe == 1 &&
                           mount_plan.mount_plan_safe == 1,
                       "credential mount plan edit should require safe binding");
  fails += expect_true(mount_plan.window_mount_allowed == 1 &&
                           mount_plan.widget_tree_selected == 1 &&
                           mount_plan.mount_credential_panel == 1 &&
                           mount_plan.mount_credential_input == 1 &&
                           mount_plan.request_credential_focus == 1,
                       "credential mount plan edit should mount credential widgets");
  fails += expect_true(mount_plan.submit_callback_bound == 0 &&
                           mount_plan.auth_callback_bound == 0 &&
                           mount_plan.submit_enabled == 0 &&
                           mount_plan.auth_attempt_allowed == 0,
                       "credential mount plan edit must not bind auth callbacks");
  fails += expect_true(mount_plan.raw_secret_exposed == 0 &&
                           mount_plan.masked_text_exposed == 0 &&
                           mount_plan.length_redacted == 1,
                       "credential mount plan edit must stay redacted");
  fails += expect_true(strings_equal(mount_plan.mount_transaction,
                                     "credential-screen-mount-plan") &&
                           strings_equal(mount_plan.focus_target,
                                         "credential-input") &&
                           strings_equal(mount_plan.state,
                                         "mount-credential-ready"),
                       "credential mount plan edit should report mount state");
  return fails;
}

static int test_loginwindow_credential_screen_mount_plan_mounts_text_recovery(void) {
  int fails = 0;
  struct login_window_credential_screen_mount_plan mount_plan;

  fails += expect_true(build_loginwindow_credential_screen_mount_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &mount_plan) == 0,
                       "credential mount plan recovery should build");
  fails += expect_true(mount_plan.mount_plan_safe == 1 &&
                           mount_plan.window_mount_allowed == 1 &&
                           mount_plan.mount_text_recovery == 1 &&
                           mount_plan.mount_text_login == 1,
                       "credential mount plan recovery should mount text recovery");
  fails += expect_true(mount_plan.mount_credential_input == 0 &&
                           mount_plan.request_credential_focus == 0 &&
                           mount_plan.input_focus_allowed == 0,
                       "credential mount plan recovery should block credential focus");
  fails += expect_true(mount_plan.submit_callback_bound == 0 &&
                           mount_plan.auth_callback_bound == 0 &&
                           mount_plan.submit_enabled == 0 &&
                           mount_plan.auth_attempt_allowed == 0,
                       "credential mount plan recovery must keep auth disabled");
  fails += expect_true(strings_equal(mount_plan.mount_transaction,
                                     "text-recovery-mount-plan") &&
                           strings_equal(mount_plan.primary_action,
                                         "open-text-recovery") &&
                           strings_equal(mount_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential mount plan recovery should report recovery mount");
  return fails;
}

static int test_loginwindow_credential_screen_mount_plan_mounts_resume_text_login(void) {
  int fails = 0;
  struct login_window_credential_screen_mount_plan mount_plan;

  fails += expect_true(build_loginwindow_credential_screen_mount_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &mount_plan) == 0,
                       "credential mount plan resume should build");
  fails += expect_true(mount_plan.mount_plan_safe == 1 &&
                           mount_plan.mount_text_login == 1 &&
                           mount_plan.mount_text_login_resume == 1,
                       "credential mount plan resume should mount text login resume");
  fails += expect_true(mount_plan.session_reset_required == 1 &&
                           mount_plan.login_screen_rerender_required == 1 &&
                           mount_plan.request_credential_focus == 0,
                       "credential mount plan resume should require reset and rerender");
  fails += expect_true(mount_plan.submit_callback_bound == 0 &&
                           mount_plan.auth_callback_bound == 0 &&
                           mount_plan.submit_enabled == 0 &&
                           mount_plan.auth_attempt_allowed == 0,
                       "credential mount plan resume must keep GUI auth disabled");
  fails += expect_true(strings_equal(mount_plan.mount_transaction,
                                     "text-login-resume-mount-plan") &&
                           strings_equal(mount_plan.primary_action,
                                         "resume-text-login") &&
                           strings_equal(mount_plan.state,
                                         "mount-resume-ready"),
                       "credential mount plan resume should report resume mount");
  return fails;
}

static int test_loginwindow_credential_screen_mount_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_mount_plan mount_plan;

  fails += expect_true(build_loginwindow_credential_screen_mount_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &mount_plan) == 0,
                       "credential mount plan submit should build");
  fails += expect_true(mount_plan.mount_plan_safe == 1 &&
                           mount_plan.submit_requested == 1 &&
                           mount_plan.mount_text_login_fallback == 1,
                       "credential mount plan submit should mount text login fallback");
  fails += expect_true(mount_plan.action_allowed == 0 &&
                           mount_plan.action_blocked == 1 &&
                           mount_plan.input_focus_allowed == 0 &&
                           mount_plan.request_credential_focus == 0,
                       "credential mount plan submit should block GUI action");
  fails += expect_true(mount_plan.submit_callback_bound == 0 &&
                           mount_plan.auth_callback_bound == 0 &&
                           mount_plan.submit_enabled == 0 &&
                           mount_plan.auth_attempt_allowed == 0 &&
                           mount_plan.raw_secret_exposed == 0 &&
                           mount_plan.masked_text_exposed == 0,
                       "credential mount plan submit must stay redacted");
  fails += expect_true(strings_equal(mount_plan.mount_transaction,
                                     "text-login-fallback-mount-plan") &&
                           strings_equal(mount_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(mount_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential mount plan submit should explain fallback mount");

  fails += expect_true(build_loginwindow_credential_screen_mount_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &mount_plan) == 0,
                       "credential mount plan unknown should build");
  fails += expect_true(mount_plan.mount_plan_safe == 1 &&
                           mount_plan.mount_text_login_fallback == 1 &&
                           mount_plan.action_allowed == 0 &&
                           mount_plan.action_blocked == 1,
                       "credential mount plan unknown should force text login fallback");
  fails += expect_true(strings_equal(mount_plan.mount_transaction,
                                     "text-login-fallback-mount-plan") &&
                           strings_equal(mount_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential mount plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_mount_plan_fails_closed_for_unsafe_or_missing_binding(void) {
  int fails = 0;
  struct login_window_credential_screen_mount_plan mount_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_mount_plan_build(
                           NULL, &mount_plan) == 0,
                       "credential mount plan missing binding should build fail-closed state");
  fails += expect_true(mount_plan.binding_available == 0 &&
                           mount_plan.binding_safe == 0 &&
                           mount_plan.mount_plan_safe == 0 &&
                           mount_plan.route_selected == 0 &&
                           mount_plan.route_blocked == 1,
                       "credential mount plan missing binding should block mount plan");
  fails += expect_true(mount_plan.window_mount_allowed == 0 &&
                           mount_plan.mount_text_login == 1 &&
                           mount_plan.mount_text_login_fallback == 1 &&
                           mount_plan.submit_callback_bound == 0 &&
                           mount_plan.auth_callback_bound == 0 &&
                           mount_plan.submit_enabled == 0 &&
                           mount_plan.auth_attempt_allowed == 0,
                       "credential mount plan missing binding must stay redacted");
  fails += expect_true(strings_equal(mount_plan.mount_transaction,
                                     "text-login-fallback-mount-plan") &&
                           strings_equal(mount_plan.event_type,
                                         "credential-screen-mount-plan-unavailable") &&
                           strings_equal(mount_plan.blocked_reason,
                                         "binding-unavailable"),
                       "credential mount plan missing binding should explain block");

  fails += expect_true(build_loginwindow_credential_screen_mount_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &mount_plan) == 0,
                       "credential mount plan unsafe binding should build blocked state");
  fails += expect_true(mount_plan.binding_available == 1 &&
                           mount_plan.binding_safe == 0 &&
                           mount_plan.mount_plan_safe == 0 &&
                           mount_plan.route_selected == 0 &&
                           mount_plan.route_blocked == 1,
                       "credential mount plan unsafe binding should block mount plan");
  fails += expect_true(mount_plan.action_allowed == 0 &&
                           mount_plan.action_blocked == 1 &&
                           mount_plan.input_focus_allowed == 0 &&
                           mount_plan.request_credential_focus == 0 &&
                           mount_plan.mount_text_login_fallback == 1,
                       "credential mount plan unsafe binding must force text login fallback");
  fails += expect_true(strings_equal(mount_plan.mount_transaction,
                                     "text-login-fallback-mount-plan") &&
                           strings_equal(mount_plan.event_type,
                                         "credential-screen-mount-plan-unsafe") &&
                           strings_equal(mount_plan.blocked_reason,
                                         "credential-mount-plan-unsafe"),
                       "credential mount plan unsafe binding should force text login");
  return fails;
}


int build_loginwindow_credential_screen_commit_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_commit_plan *commit_plan) {
  struct login_window_credential_screen_mount_plan mount_plan;

  if (build_loginwindow_credential_screen_mount_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &mount_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_commit_plan_build(&mount_plan, commit_plan);
}

static int test_loginwindow_credential_screen_commit_plan_commits_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_commit_plan commit_plan;

  fails += expect_true(build_loginwindow_credential_screen_commit_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'e', 0, 0, 0,
                           1, &commit_plan) == 0,
                       "credential commit plan edit should build");
  fails += expect_true(commit_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_COMMIT_PLAN_VERSION,
                       "credential commit plan should expose stable version");
  fails += expect_true(commit_plan.mount_plan_available == 1 &&
                           commit_plan.mount_plan_safe == 1 &&
                           commit_plan.commit_plan_safe == 1,
                       "credential commit plan edit should require safe mount plan");
  fails += expect_true(commit_plan.window_commit_allowed == 1 &&
                           commit_plan.window_commit_executed == 0 &&
                           commit_plan.widget_tree_selected == 1,
                       "credential commit plan edit should remain declarative");
  fails += expect_true(commit_plan.commit_credential_panel == 1 &&
                           commit_plan.commit_credential_input == 1 &&
                           commit_plan.commit_credential_focus == 1,
                       "credential commit plan edit should commit credential widgets");
  fails += expect_true(commit_plan.submit_callback_bound == 0 &&
                           commit_plan.auth_callback_bound == 0 &&
                           commit_plan.submit_enabled == 0 &&
                           commit_plan.auth_attempt_allowed == 0,
                       "credential commit plan edit must not bind auth callbacks");
  fails += expect_true(commit_plan.raw_secret_exposed == 0 &&
                           commit_plan.masked_text_exposed == 0 &&
                           commit_plan.length_redacted == 1,
                       "credential commit plan edit must stay redacted");
  fails += expect_true(strings_equal(commit_plan.commit_transaction,
                                     "credential-screen-commit-plan") &&
                           strings_equal(commit_plan.focus_target,
                                         "credential-input") &&
                           strings_equal(commit_plan.state,
                                         "commit-credential-ready"),
                       "credential commit plan edit should report commit state");
  return fails;
}

static int test_loginwindow_credential_screen_commit_plan_commits_text_recovery(void) {
  int fails = 0;
  struct login_window_credential_screen_commit_plan commit_plan;

  fails += expect_true(build_loginwindow_credential_screen_commit_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &commit_plan) == 0,
                       "credential commit plan recovery should build");
  fails += expect_true(commit_plan.commit_plan_safe == 1 &&
                           commit_plan.window_commit_allowed == 1 &&
                           commit_plan.window_commit_executed == 0 &&
                           commit_plan.commit_text_recovery == 1 &&
                           commit_plan.commit_text_login == 1,
                       "credential commit plan recovery should commit text recovery");
  fails += expect_true(commit_plan.commit_credential_input == 0 &&
                           commit_plan.commit_credential_focus == 0 &&
                           commit_plan.input_focus_allowed == 0,
                       "credential commit plan recovery should block credential focus");
  fails += expect_true(commit_plan.submit_callback_bound == 0 &&
                           commit_plan.auth_callback_bound == 0 &&
                           commit_plan.submit_enabled == 0 &&
                           commit_plan.auth_attempt_allowed == 0,
                       "credential commit plan recovery must keep auth disabled");
  fails += expect_true(strings_equal(commit_plan.commit_transaction,
                                     "text-recovery-commit-plan") &&
                           strings_equal(commit_plan.primary_action,
                                         "open-text-recovery") &&
                           strings_equal(commit_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential commit plan recovery should report recovery commit");
  return fails;
}

static int test_loginwindow_credential_screen_commit_plan_commits_resume_text_login(void) {
  int fails = 0;
  struct login_window_credential_screen_commit_plan commit_plan;

  fails += expect_true(build_loginwindow_credential_screen_commit_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &commit_plan) == 0,
                       "credential commit plan resume should build");
  fails += expect_true(commit_plan.commit_plan_safe == 1 &&
                           commit_plan.commit_text_login == 1 &&
                           commit_plan.commit_text_login_resume == 1,
                       "credential commit plan resume should commit text login resume");
  fails += expect_true(commit_plan.session_reset_required == 1 &&
                           commit_plan.login_screen_rerender_required == 1 &&
                           commit_plan.commit_credential_focus == 0,
                       "credential commit plan resume should require reset and rerender");
  fails += expect_true(commit_plan.submit_callback_bound == 0 &&
                           commit_plan.auth_callback_bound == 0 &&
                           commit_plan.submit_enabled == 0 &&
                           commit_plan.auth_attempt_allowed == 0,
                       "credential commit plan resume must keep GUI auth disabled");
  fails += expect_true(strings_equal(commit_plan.commit_transaction,
                                     "text-login-resume-commit-plan") &&
                           strings_equal(commit_plan.primary_action,
                                         "resume-text-login") &&
                           strings_equal(commit_plan.state,
                                         "commit-resume-ready"),
                       "credential commit plan resume should report resume commit");
  return fails;
}

static int test_loginwindow_credential_screen_commit_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_commit_plan commit_plan;

  fails += expect_true(build_loginwindow_credential_screen_commit_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &commit_plan) == 0,
                       "credential commit plan submit should build");
  fails += expect_true(commit_plan.commit_plan_safe == 1 &&
                           commit_plan.submit_requested == 1 &&
                           commit_plan.commit_text_login_fallback == 1,
                       "credential commit plan submit should commit text login fallback");
  fails += expect_true(commit_plan.action_allowed == 0 &&
                           commit_plan.action_blocked == 1 &&
                           commit_plan.input_focus_allowed == 0 &&
                           commit_plan.commit_credential_focus == 0,
                       "credential commit plan submit should block GUI action");
  fails += expect_true(commit_plan.window_commit_allowed == 1 &&
                           commit_plan.window_commit_executed == 0 &&
                           commit_plan.submit_callback_bound == 0 &&
                           commit_plan.auth_callback_bound == 0 &&
                           commit_plan.submit_enabled == 0 &&
                           commit_plan.auth_attempt_allowed == 0 &&
                           commit_plan.raw_secret_exposed == 0 &&
                           commit_plan.masked_text_exposed == 0,
                       "credential commit plan submit must stay declarative and redacted");
  fails += expect_true(strings_equal(commit_plan.commit_transaction,
                                     "text-login-fallback-commit-plan") &&
                           strings_equal(commit_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(commit_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential commit plan submit should explain fallback commit");

  fails += expect_true(build_loginwindow_credential_screen_commit_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &commit_plan) == 0,
                       "credential commit plan unknown should build");
  fails += expect_true(commit_plan.commit_plan_safe == 1 &&
                           commit_plan.commit_text_login_fallback == 1 &&
                           commit_plan.action_allowed == 0 &&
                           commit_plan.action_blocked == 1,
                       "credential commit plan unknown should force text login fallback");
  fails += expect_true(strings_equal(commit_plan.commit_transaction,
                                     "text-login-fallback-commit-plan") &&
                           strings_equal(commit_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential commit plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_commit_plan_fails_closed_for_unsafe_or_missing_mount_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_commit_plan commit_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_commit_plan_build(
                           NULL, &commit_plan) == 0,
                       "credential commit plan missing mount plan should build fail-closed state");
  fails += expect_true(commit_plan.mount_plan_available == 0 &&
                           commit_plan.mount_plan_safe == 0 &&
                           commit_plan.commit_plan_safe == 0 &&
                           commit_plan.route_selected == 0 &&
                           commit_plan.route_blocked == 1,
                       "credential commit plan missing mount plan should block commit plan");
  fails += expect_true(commit_plan.window_commit_allowed == 0 &&
                           commit_plan.window_commit_executed == 0 &&
                           commit_plan.commit_text_login == 1 &&
                           commit_plan.commit_text_login_fallback == 1 &&
                           commit_plan.submit_callback_bound == 0 &&
                           commit_plan.auth_callback_bound == 0 &&
                           commit_plan.submit_enabled == 0 &&
                           commit_plan.auth_attempt_allowed == 0,
                       "credential commit plan missing mount plan must stay redacted");
  fails += expect_true(strings_equal(commit_plan.commit_transaction,
                                     "text-login-fallback-commit-plan") &&
                           strings_equal(commit_plan.event_type,
                                         "credential-screen-commit-plan-unavailable") &&
                           strings_equal(commit_plan.blocked_reason,
                                         "mount-plan-unavailable"),
                       "credential commit plan missing mount plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_commit_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &commit_plan) == 0,
                       "credential commit plan unsafe mount plan should build blocked state");
  fails += expect_true(commit_plan.mount_plan_available == 1 &&
                           commit_plan.mount_plan_safe == 0 &&
                           commit_plan.commit_plan_safe == 0 &&
                           commit_plan.route_selected == 0 &&
                           commit_plan.route_blocked == 1,
                       "credential commit plan unsafe mount plan should block commit plan");
  fails += expect_true(commit_plan.action_allowed == 0 &&
                           commit_plan.action_blocked == 1 &&
                           commit_plan.input_focus_allowed == 0 &&
                           commit_plan.commit_credential_focus == 0 &&
                           commit_plan.commit_text_login_fallback == 1,
                       "credential commit plan unsafe mount plan must force text login fallback");
  fails += expect_true(strings_equal(commit_plan.commit_transaction,
                                     "text-login-fallback-commit-plan") &&
                           strings_equal(commit_plan.event_type,
                                         "credential-screen-commit-plan-unsafe") &&
                           strings_equal(commit_plan.blocked_reason,
                                         "credential-commit-plan-unsafe"),
                       "credential commit plan unsafe mount plan should force text login");
  return fails;
}

int test_login_runtime_credential_mount_commit_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_mount_plan_mounts_credential_widgets();
  fails += test_loginwindow_credential_screen_mount_plan_mounts_text_recovery();
  fails += test_loginwindow_credential_screen_mount_plan_mounts_resume_text_login();
  fails += test_loginwindow_credential_screen_mount_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_mount_plan_fails_closed_for_unsafe_or_missing_binding();
  fails += test_loginwindow_credential_screen_commit_plan_commits_credential_widgets();
  fails += test_loginwindow_credential_screen_commit_plan_commits_text_recovery();
  fails += test_loginwindow_credential_screen_commit_plan_commits_resume_text_login();
  fails += test_loginwindow_credential_screen_commit_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_commit_plan_fails_closed_for_unsafe_or_missing_mount_plan();
  return fails;
}
