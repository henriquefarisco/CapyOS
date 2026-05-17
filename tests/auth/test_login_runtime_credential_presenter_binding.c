/*
 * tests/auth/test_login_runtime_credential_presenter_binding.c
 *
 * Credential screen presenter + binding coverage for the
 * `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-15 monolith
 * refactor (PR D.9 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_presenter_build`: 5 tests
 *     covering the credential-focus presenter + the text-recovery
 *     presenter + the resume-text-login presenter + the
 *     submit/unknown forced presenter + the missing-or-unsafe
 *     controller fail-closed default.
 *   - `login_window_credential_screen_binding_build`: 5 tests
 *     covering the credential widgets binding + the text-recovery
 *     binding + the resume-text-login binding + the submit/unknown
 *     fallback binding + the missing-or-unsafe presenter fail-closed
 *     default.
 *
 * Also exposes shared helpers
 * `build_loginwindow_credential_screen_presenter_for_action` and
 * `build_loginwindow_credential_screen_binding_for_action`, used by
 * later companion files that chain on top of the presenter/binding
 * stages (mount, commit, handoff, dispatch, queue, activation, ...).
 * The helpers are declared in
 * `tests/auth/test_login_runtime_internal.h`.
 *
 * The companion entry `test_login_runtime_credential_presenter_binding_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_presenter_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_presenter *presenter) {
  struct login_window_credential_screen_controller controller;

  if (build_loginwindow_credential_screen_controller_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &controller) != 0) {
    return -1;
  }
  return login_window_credential_screen_presenter_build(&controller, presenter);
}

static int test_loginwindow_credential_screen_presenter_shows_credential_focus(void) {
  int fails = 0;
  struct login_window_credential_screen_presenter presenter;

  fails += expect_true(build_loginwindow_credential_screen_presenter_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'e', 0, 0, 0,
                           1, &presenter) == 0,
                       "credential presenter edit should build");
  fails += expect_true(presenter.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_PRESENTER_VERSION,
                       "credential presenter should expose stable version");
  fails += expect_true(presenter.controller_available == 1 &&
                           presenter.controller_safe == 1 &&
                           presenter.presenter_safe == 1,
                       "credential presenter edit should require safe controller");
  fails += expect_true(presenter.credential_screen_visible == 1 &&
                           presenter.credential_panel_visible == 1 &&
                           presenter.credential_input_visible == 1 &&
                           presenter.credential_input_focus == 1,
                       "credential presenter edit should expose credential focus view");
  fails += expect_true(presenter.text_login_visible == 0 &&
                           presenter.text_login_forced == 0 &&
                           presenter.error_visible == 0,
                       "credential presenter edit should not force text login view");
  fails += expect_true(presenter.submit_blocked == 1 &&
                           presenter.submit_enabled == 0 &&
                           presenter.auth_attempt_allowed == 0 &&
                           presenter.raw_secret_exposed == 0 &&
                           presenter.masked_text_exposed == 0,
                       "credential presenter edit must stay redacted");
  fails += expect_true(strings_equal(presenter.view, "credential-screen") &&
                           strings_equal(presenter.focus_target,
                                         "credential-input") &&
                           strings_equal(presenter.primary_action,
                                         "edit-credential") &&
                           strings_equal(presenter.state,
                                         "presenter-credential-ready"),
                       "credential presenter edit should report presentation state");
  return fails;
}

static int test_loginwindow_credential_screen_presenter_shows_text_recovery(void) {
  int fails = 0;
  struct login_window_credential_screen_presenter presenter;

  fails += expect_true(build_loginwindow_credential_screen_presenter_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &presenter) == 0,
                       "credential presenter recovery should build");
  fails += expect_true(presenter.presenter_safe == 1 &&
                           presenter.text_recovery_visible == 1 &&
                           presenter.text_recovery_open == 1 &&
                           presenter.text_login_visible == 1,
                       "credential presenter recovery should show text recovery");
  fails += expect_true(presenter.credential_input_visible == 0 &&
                           presenter.credential_input_focus == 0 &&
                           presenter.input_focus_allowed == 0,
                       "credential presenter recovery should block credential focus");
  fails += expect_true(presenter.submit_blocked == 1 &&
                           presenter.submit_enabled == 0 &&
                           presenter.auth_attempt_allowed == 0 &&
                           presenter.raw_secret_exposed == 0 &&
                           presenter.masked_text_exposed == 0,
                       "credential presenter recovery must keep auth disabled");
  fails += expect_true(strings_equal(presenter.view, "text-recovery") &&
                           strings_equal(presenter.primary_action,
                                         "open-text-recovery") &&
                           strings_equal(presenter.state,
                                         "presenter-text-recovery-ready") &&
                           strings_equal(presenter.blocked_reason,
                                         "text-recovery-only"),
                       "credential presenter recovery should report recovery presentation");
  return fails;
}

static int test_loginwindow_credential_screen_presenter_shows_resume_text_login(void) {
  int fails = 0;
  struct login_window_credential_screen_presenter presenter;

  fails += expect_true(build_loginwindow_credential_screen_presenter_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &presenter) == 0,
                       "credential presenter resume should build");
  fails += expect_true(presenter.presenter_safe == 1 &&
                           presenter.text_login_resume == 1 &&
                           presenter.text_login_visible == 1 &&
                           presenter.text_login_forced == 1,
                       "credential presenter resume should show text login resume");
  fails += expect_true(presenter.session_reset_required == 1 &&
                           presenter.login_screen_rerender_required == 1 &&
                           presenter.credential_input_focus == 0,
                       "credential presenter resume should require reset and rerender");
  fails += expect_true(presenter.submit_blocked == 1 &&
                           presenter.submit_enabled == 0 &&
                           presenter.auth_attempt_allowed == 0,
                       "credential presenter resume must keep GUI auth disabled");
  fails += expect_true(strings_equal(presenter.view, "text-login") &&
                           strings_equal(presenter.primary_action,
                                         "resume-text-login") &&
                           strings_equal(presenter.state,
                                         "presenter-resume-ready"),
                       "credential presenter resume should report resume presentation");
  return fails;
}

static int test_loginwindow_credential_screen_presenter_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_presenter presenter;

  fails += expect_true(build_loginwindow_credential_screen_presenter_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &presenter) == 0,
                       "credential presenter submit should build");
  fails += expect_true(presenter.presenter_safe == 1 &&
                           presenter.submit_requested == 1 &&
                           presenter.text_login_forced == 1,
                       "credential presenter submit should force text login");
  fails += expect_true(presenter.action_allowed == 0 &&
                           presenter.action_blocked == 1 &&
                           presenter.input_focus_allowed == 0 &&
                           presenter.credential_input_focus == 0,
                       "credential presenter submit should block GUI action");
  fails += expect_true(presenter.submit_blocked == 1 &&
                           presenter.submit_enabled == 0 &&
                           presenter.auth_attempt_allowed == 0 &&
                           presenter.raw_secret_exposed == 0 &&
                           presenter.masked_text_exposed == 0,
                       "credential presenter submit must stay redacted");
  fails += expect_true(strings_equal(presenter.view,
                                     "text-login-fallback") &&
                           strings_equal(presenter.primary_action,
                                         "use-text-login") &&
                           strings_equal(presenter.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential presenter submit should explain text fallback");

  fails += expect_true(build_loginwindow_credential_screen_presenter_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &presenter) == 0,
                       "credential presenter unknown should build");
  fails += expect_true(presenter.presenter_safe == 1 &&
                           presenter.text_login_forced == 1 &&
                           presenter.action_allowed == 0 &&
                           presenter.action_blocked == 1,
                       "credential presenter unknown should force text login");
  fails += expect_true(strings_equal(presenter.view,
                                     "text-login-fallback") &&
                           strings_equal(presenter.blocked_reason,
                                         "credential-action-unknown"),
                       "credential presenter unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_presenter_fails_closed_for_unsafe_or_missing_controller(void) {
  int fails = 0;
  struct login_window_credential_screen_presenter presenter;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_presenter_build(
                           NULL, &presenter) == 0,
                       "credential presenter missing controller should build fail-closed state");
  fails += expect_true(presenter.controller_available == 0 &&
                           presenter.controller_safe == 0 &&
                           presenter.presenter_safe == 0 &&
                           presenter.route_selected == 0 &&
                           presenter.route_blocked == 1,
                       "credential presenter missing controller should block presentation");
  fails += expect_true(presenter.text_login_visible == 1 &&
                           presenter.text_login_forced == 1 &&
                           presenter.submit_blocked == 1 &&
                           presenter.submit_enabled == 0 &&
                           presenter.auth_attempt_allowed == 0 &&
                           presenter.raw_secret_exposed == 0 &&
                           presenter.masked_text_exposed == 0,
                       "credential presenter missing controller must stay redacted");
  fails += expect_true(strings_equal(presenter.view,
                                     "text-login-fallback") &&
                           strings_equal(presenter.event_type,
                                         "credential-screen-presenter-unavailable") &&
                           strings_equal(presenter.blocked_reason,
                                         "controller-unavailable"),
                       "credential presenter missing controller should explain block");

  fails += expect_true(build_loginwindow_credential_screen_presenter_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &presenter) == 0,
                       "credential presenter unsafe controller should build blocked state");
  fails += expect_true(presenter.controller_available == 1 &&
                           presenter.controller_safe == 0 &&
                           presenter.presenter_safe == 0 &&
                           presenter.route_selected == 0 &&
                           presenter.route_blocked == 1,
                       "credential presenter unsafe controller should block presentation");
  fails += expect_true(presenter.action_allowed == 0 &&
                           presenter.action_blocked == 1 &&
                           presenter.input_focus_allowed == 0 &&
                           presenter.credential_input_focus == 0 &&
                           presenter.text_login_forced == 1,
                       "credential presenter unsafe controller must force text login");
  fails += expect_true(strings_equal(presenter.view,
                                     "text-login-fallback") &&
                           strings_equal(presenter.event_type,
                                         "credential-screen-presenter-unsafe") &&
                           strings_equal(presenter.blocked_reason,
                                         "credential-presenter-unsafe"),
                       "credential presenter unsafe controller should force text login");
  return fails;
}


int build_loginwindow_credential_screen_binding_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_binding *binding) {
  struct login_window_credential_screen_presenter presenter;

  if (build_loginwindow_credential_screen_presenter_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &presenter) != 0) {
    return -1;
  }
  return login_window_credential_screen_binding_build(&presenter, binding);
}

static int test_loginwindow_credential_screen_binding_binds_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_binding binding;

  fails += expect_true(build_loginwindow_credential_screen_binding_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'e', 0, 0, 0,
                           1, &binding) == 0,
                       "credential binding edit should build");
  fails += expect_true(binding.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_BINDING_VERSION,
                       "credential binding should expose stable version");
  fails += expect_true(binding.presenter_available == 1 &&
                           binding.presenter_safe == 1 &&
                           binding.binding_safe == 1,
                       "credential binding edit should require safe presenter");
  fails += expect_true(binding.credential_panel_bound == 1 &&
                           binding.credential_input_bound == 1 &&
                           binding.credential_input_focus_requested == 1,
                       "credential binding edit should bind credential widgets");
  fails += expect_true(binding.text_login_bound == 0 &&
                           binding.text_login_fallback_bound == 0 &&
                           binding.error_bound == 0,
                       "credential binding edit should not bind text fallback");
  fails += expect_true(binding.submit_blocked == 1 &&
                           binding.submit_enabled == 0 &&
                           binding.auth_attempt_allowed == 0 &&
                           binding.raw_secret_exposed == 0 &&
                           binding.masked_text_exposed == 0,
                       "credential binding edit must stay redacted");
  fails += expect_true(strings_equal(binding.view, "credential-screen") &&
                           strings_equal(binding.widget_tree,
                                         "credential-screen-bindings") &&
                           strings_equal(binding.focus_target,
                                         "credential-input") &&
                           strings_equal(binding.state,
                                         "binding-credential-ready"),
                       "credential binding edit should report binding state");
  return fails;
}

static int test_loginwindow_credential_screen_binding_binds_text_recovery(void) {
  int fails = 0;
  struct login_window_credential_screen_binding binding;

  fails += expect_true(build_loginwindow_credential_screen_binding_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &binding) == 0,
                       "credential binding recovery should build");
  fails += expect_true(binding.binding_safe == 1 &&
                           binding.text_recovery_bound == 1 &&
                           binding.text_login_bound == 1,
                       "credential binding recovery should bind text recovery");
  fails += expect_true(binding.credential_input_bound == 0 &&
                           binding.credential_input_focus_requested == 0 &&
                           binding.input_focus_allowed == 0,
                       "credential binding recovery should block credential focus");
  fails += expect_true(binding.submit_blocked == 1 &&
                           binding.submit_enabled == 0 &&
                           binding.auth_attempt_allowed == 0 &&
                           binding.raw_secret_exposed == 0 &&
                           binding.masked_text_exposed == 0,
                       "credential binding recovery must keep auth disabled");
  fails += expect_true(strings_equal(binding.view, "text-recovery") &&
                           strings_equal(binding.widget_tree,
                                         "text-recovery-bindings") &&
                           strings_equal(binding.primary_action,
                                         "open-text-recovery") &&
                           strings_equal(binding.blocked_reason,
                                         "text-recovery-only"),
                       "credential binding recovery should report recovery binding");
  return fails;
}

static int test_loginwindow_credential_screen_binding_binds_resume_text_login(void) {
  int fails = 0;
  struct login_window_credential_screen_binding binding;

  fails += expect_true(build_loginwindow_credential_screen_binding_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &binding) == 0,
                       "credential binding resume should build");
  fails += expect_true(binding.binding_safe == 1 &&
                           binding.text_login_bound == 1 &&
                           binding.text_login_resume_bound == 1,
                       "credential binding resume should bind text login resume");
  fails += expect_true(binding.session_reset_required == 1 &&
                           binding.login_screen_rerender_required == 1 &&
                           binding.credential_input_focus_requested == 0,
                       "credential binding resume should require reset and rerender");
  fails += expect_true(binding.submit_blocked == 1 &&
                           binding.submit_enabled == 0 &&
                           binding.auth_attempt_allowed == 0,
                       "credential binding resume must keep GUI auth disabled");
  fails += expect_true(strings_equal(binding.view, "text-login") &&
                           strings_equal(binding.widget_tree,
                                         "text-login-resume-bindings") &&
                           strings_equal(binding.primary_action,
                                         "resume-text-login") &&
                           strings_equal(binding.state,
                                         "binding-resume-ready"),
                       "credential binding resume should report resume binding");
  return fails;
}

static int test_loginwindow_credential_screen_binding_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_binding binding;

  fails += expect_true(build_loginwindow_credential_screen_binding_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &binding) == 0,
                       "credential binding submit should build");
  fails += expect_true(binding.binding_safe == 1 &&
                           binding.submit_requested == 1 &&
                           binding.text_login_fallback_bound == 1,
                       "credential binding submit should bind text login fallback");
  fails += expect_true(binding.action_allowed == 0 &&
                           binding.action_blocked == 1 &&
                           binding.input_focus_allowed == 0 &&
                           binding.credential_input_focus_requested == 0,
                       "credential binding submit should block GUI action");
  fails += expect_true(binding.submit_blocked == 1 &&
                           binding.submit_enabled == 0 &&
                           binding.auth_attempt_allowed == 0 &&
                           binding.raw_secret_exposed == 0 &&
                           binding.masked_text_exposed == 0,
                       "credential binding submit must stay redacted");
  fails += expect_true(strings_equal(binding.widget_tree,
                                     "text-login-fallback-bindings") &&
                           strings_equal(binding.primary_action,
                                         "use-text-login") &&
                           strings_equal(binding.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential binding submit should explain fallback binding");

  fails += expect_true(build_loginwindow_credential_screen_binding_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &binding) == 0,
                       "credential binding unknown should build");
  fails += expect_true(binding.binding_safe == 1 &&
                           binding.text_login_fallback_bound == 1 &&
                           binding.action_allowed == 0 &&
                           binding.action_blocked == 1,
                       "credential binding unknown should force text login fallback");
  fails += expect_true(strings_equal(binding.view,
                                     "text-login-fallback") &&
                           strings_equal(binding.blocked_reason,
                                         "credential-action-unknown"),
                       "credential binding unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_binding_fails_closed_for_unsafe_or_missing_presenter(void) {
  int fails = 0;
  struct login_window_credential_screen_binding binding;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_binding_build(
                           NULL, &binding) == 0,
                       "credential binding missing presenter should build fail-closed state");
  fails += expect_true(binding.presenter_available == 0 &&
                           binding.presenter_safe == 0 &&
                           binding.binding_safe == 0 &&
                           binding.route_selected == 0 &&
                           binding.route_blocked == 1,
                       "credential binding missing presenter should block binding");
  fails += expect_true(binding.text_login_bound == 1 &&
                           binding.text_login_fallback_bound == 1 &&
                           binding.submit_blocked == 1 &&
                           binding.submit_enabled == 0 &&
                           binding.auth_attempt_allowed == 0 &&
                           binding.raw_secret_exposed == 0 &&
                           binding.masked_text_exposed == 0,
                       "credential binding missing presenter must stay redacted");
  fails += expect_true(strings_equal(binding.widget_tree,
                                     "text-login-fallback-bindings") &&
                           strings_equal(binding.event_type,
                                         "credential-screen-binding-unavailable") &&
                           strings_equal(binding.blocked_reason,
                                         "presenter-unavailable"),
                       "credential binding missing presenter should explain block");

  fails += expect_true(build_loginwindow_credential_screen_binding_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &binding) == 0,
                       "credential binding unsafe presenter should build blocked state");
  fails += expect_true(binding.presenter_available == 1 &&
                           binding.presenter_safe == 0 &&
                           binding.binding_safe == 0 &&
                           binding.route_selected == 0 &&
                           binding.route_blocked == 1,
                       "credential binding unsafe presenter should block binding");
  fails += expect_true(binding.action_allowed == 0 &&
                           binding.action_blocked == 1 &&
                           binding.input_focus_allowed == 0 &&
                           binding.credential_input_focus_requested == 0 &&
                           binding.text_login_fallback_bound == 1,
                       "credential binding unsafe presenter must force text login fallback");
  fails += expect_true(strings_equal(binding.widget_tree,
                                     "text-login-fallback-bindings") &&
                           strings_equal(binding.event_type,
                                         "credential-screen-binding-unsafe") &&
                           strings_equal(binding.blocked_reason,
                                         "credential-binding-unsafe"),
                       "credential binding unsafe presenter should force text login");
  return fails;
}

int test_login_runtime_credential_presenter_binding_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_presenter_shows_credential_focus();
  fails += test_loginwindow_credential_screen_presenter_shows_text_recovery();
  fails += test_loginwindow_credential_screen_presenter_shows_resume_text_login();
  fails += test_loginwindow_credential_screen_presenter_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_presenter_fails_closed_for_unsafe_or_missing_controller();
  fails += test_loginwindow_credential_screen_binding_binds_credential_widgets();
  fails += test_loginwindow_credential_screen_binding_binds_text_recovery();
  fails += test_loginwindow_credential_screen_binding_binds_resume_text_login();
  fails += test_loginwindow_credential_screen_binding_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_binding_fails_closed_for_unsafe_or_missing_presenter();
  return fails;
}
