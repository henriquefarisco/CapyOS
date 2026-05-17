/*
 * tests/auth/test_login_runtime_credential_action_event.c
 *
 * Credential screen action plan + screen UI event coverage for the
 * `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-15 monolith
 * refactor (PR D.7 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_action_plan_build`: 5 tests
 *     covering the edit-focus action + the text-recovery action +
 *     the resume-text-login action + the submit/unknown action block
 *     + the missing-or-unsafe render plan fail-closed default.
 *   - `login_window_credential_screen_ui_event_build`: 5 tests
 *     covering the edit-focus event + the text-recovery event + the
 *     resume-text-login event + the submit/unknown block event +
 *     the missing-or-unsafe action plan fail-closed default.
 *
 * The companion entry `test_login_runtime_credential_action_event_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`. Shared fixture state and helpers
 * come from `tests/auth/test_login_runtime_internal.h`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

static int test_loginwindow_credential_screen_action_plan_allows_edit_focus(void) {
  int fails = 0;
  char storage[8] = "stale";
  char masked[8] = "mask";
  struct login_runtime_ops ops;
  struct login_window_credential_screen_session session;
  struct login_window_credential_screen_render_plan render;
  struct login_window_credential_screen_action_plan action;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_credential_screen_session_build(
                           &ops, "en", storage, sizeof(storage),
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'e',
                           masked, sizeof(masked), 0, 0, &session) == 0,
                       "credential action edit session should build");
  fails += expect_true(login_window_credential_screen_render_plan_build(
                           &session, &render) == 0,
                       "credential action edit render plan should build");
  fails += expect_true(login_window_credential_screen_action_plan_build(
                           &render,
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           &action) == 0,
                       "credential action edit plan should build");
  fails += expect_true(action.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_PLAN_VERSION,
                       "credential action plan should expose stable version");
  fails += expect_true(action.render_plan_available == 1 &&
                           action.render_plan_safe == 1 &&
                           action.action_allowed == 1 &&
                           action.action_blocked == 0,
                       "credential action edit should allow safe action");
  fails += expect_true(action.edit_credential_requested == 1 &&
                           action.edit_credential_allowed == 1 &&
                           action.input_focus_allowed == 1 &&
                           action.use_text_login_required == 0,
                       "credential action edit should allow input focus only");
  fails += expect_true(action.credential_session_safe == 1 &&
                           action.credential_storage_wiped == 1 &&
                           action.credential_redacted == 1 &&
                           action.length_redacted == 1,
                       "credential action edit should require safe redacted session");
  fails += expect_true(action.raw_secret_exposed == 0 &&
                           action.masked_text_exposed == 0 &&
                           action.submit_requested == 0 &&
                           action.submit_blocked == 1 &&
                           action.submit_enabled == 0 &&
                           action.auth_attempt_allowed == 0,
                       "credential action edit must not expose auth or secrets");
  fails += expect_true(strings_equal(action.result_action,
                                     "edit-credential") &&
                           strings_equal(action.blocked_reason, "ready"),
                       "credential action edit should report ready result");
  return fails;
}

static int test_loginwindow_credential_screen_action_plan_allows_text_recovery(void) {
  int fails = 0;
  char storage[8] = "old";
  char masked[8] = "mask";
  struct login_runtime_ops ops;
  struct login_window_credential_screen_session session;
  struct login_window_credential_screen_render_plan render;
  struct login_window_credential_screen_action_plan action;

  reset_test_state();
  ops = build_ops();
  ops.maintenance_mode = 1;
  g_runtime_maintenance_active = 1;
  fails += expect_true(login_window_credential_screen_session_build(
                           &ops, "en", storage, sizeof(storage),
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, '\0',
                           masked, sizeof(masked), 0, 0, &session) == 0,
                       "credential action recovery session should build");
  fails += expect_true(login_window_credential_screen_render_plan_build(
                           &session, &render) == 0,
                       "credential action recovery render plan should build");
  fails += expect_true(login_window_credential_screen_action_plan_build(
                           &render,
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           &action) == 0,
                       "credential action recovery plan should build");
  fails += expect_true(action.render_plan_safe == 1 &&
                           action.action_allowed == 1 &&
                           action.action_blocked == 0,
                       "credential action recovery should allow safe text action");
  fails += expect_true(action.open_text_recovery_requested == 1 &&
                           action.open_text_recovery_allowed == 1 &&
                           action.recovery_text_session_required == 1 &&
                           action.use_text_login_required == 1,
                       "credential action recovery should require text session");
  fails += expect_true(action.input_focus_allowed == 0 &&
                           action.submit_blocked == 1 &&
                           action.submit_enabled == 0 &&
                           action.auth_attempt_allowed == 0,
                       "credential action recovery must not focus GUI auth");
  fails += expect_true(action.raw_secret_exposed == 0 &&
                           action.masked_text_exposed == 0,
                       "credential action recovery must remain redacted");
  fails += expect_true(strings_equal(action.result_action,
                                     "open-text-recovery") &&
                           strings_equal(action.blocked_reason,
                                         "text-recovery-only"),
                       "credential action recovery should report text recovery result");
  return fails;
}

static int test_loginwindow_credential_screen_action_plan_allows_resume_text_login(void) {
  int fails = 0;
  char storage[8] = "old";
  char masked[8] = "mask";
  struct login_runtime_ops ops;
  struct login_window_credential_screen_session session;
  struct login_window_credential_screen_render_plan render;
  struct login_window_credential_screen_action_plan action;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_credential_screen_session_build(
                           &ops, "en", storage, sizeof(storage),
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r',
                           masked, sizeof(masked), 1, 1, &session) == 0,
                       "credential action resume session should build");
  fails += expect_true(login_window_credential_screen_render_plan_build(
                           &session, &render) == 0,
                       "credential action resume render plan should build");
  fails += expect_true(login_window_credential_screen_action_plan_build(
                           &render,
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           &action) == 0,
                       "credential action resume plan should build");
  fails += expect_true(action.render_plan_safe == 1 &&
                           action.action_allowed == 1 &&
                           action.action_blocked == 0,
                       "credential action resume should allow safe resume");
  fails += expect_true(action.resume_text_login_requested == 1 &&
                           action.resume_text_login_allowed == 1 &&
                           action.session_reset_required == 1 &&
                           action.login_screen_rerender_required == 1,
                       "credential action resume should require reset and rerender");
  fails += expect_true(action.input_focus_allowed == 0 &&
                           action.submit_requested == 0 &&
                           action.submit_enabled == 0 &&
                           action.auth_attempt_allowed == 0,
                       "credential action resume must not use GUI auth");
  fails += expect_true(strings_equal(action.result_action,
                                     "resume-text-login") &&
                           strings_equal(action.blocked_reason, "ready"),
                       "credential action resume should report resume result");
  return fails;
}

static int test_loginwindow_credential_screen_action_plan_blocks_submit_and_unknown(void) {
  int fails = 0;
  char storage[8] = "stale";
  char masked[8] = "mask";
  struct login_runtime_ops ops;
  struct login_window_credential_screen_session session;
  struct login_window_credential_screen_render_plan render;
  struct login_window_credential_screen_action_plan action;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_credential_screen_session_build(
                           &ops, "en", storage, sizeof(storage),
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's',
                           masked, sizeof(masked), 0, 0, &session) == 0,
                       "credential action block session should build");
  fails += expect_true(login_window_credential_screen_render_plan_build(
                           &session, &render) == 0,
                       "credential action block render plan should build");
  fails += expect_true(login_window_credential_screen_action_plan_build(
                           &render,
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           &action) == 0,
                       "credential action submit should build blocked state");
  fails += expect_true(action.render_plan_safe == 1 &&
                           action.submit_requested == 1 &&
                           action.submit_blocked == 1 &&
                           action.submit_enabled == 0 &&
                           action.auth_attempt_allowed == 0,
                       "credential action submit must stay blocked");
  fails += expect_true(action.action_allowed == 0 &&
                           action.action_blocked == 1 &&
                           action.input_focus_allowed == 0,
                       "credential action submit should not allow action");
  fails += expect_true(strings_equal(action.result_action,
                                     "use-text-login") &&
                           strings_equal(action.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential action submit should force text login");
  fails += expect_true(login_window_credential_screen_action_plan_build(
                           &render, 99, &action) == 0,
                       "credential action unknown should build blocked state");
  fails += expect_true(action.render_plan_safe == 1 &&
                           action.action_allowed == 0 &&
                           action.action_blocked == 1 &&
                           action.submit_enabled == 0 &&
                           action.auth_attempt_allowed == 0,
                       "credential action unknown should remain blocked");
  fails += expect_true(strings_equal(action.blocked_reason,
                                     "credential-action-unknown"),
                       "credential action unknown should explain block");
  return fails;
}

static int test_loginwindow_credential_screen_action_plan_fails_closed_for_unsafe_or_missing_plan(void) {
  int fails = 0;
  char masked[8] = "mask";
  struct login_runtime_ops ops;
  struct login_window_credential_screen_session session;
  struct login_window_credential_screen_render_plan render;
  struct login_window_credential_screen_action_plan action;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_action_plan_build(
                           NULL,
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           &action) == 0,
                       "credential action missing render plan should build fail-closed state");
  fails += expect_true(action.render_plan_available == 0 &&
                           action.render_plan_safe == 0 &&
                           action.action_allowed == 0 &&
                           action.action_blocked == 1,
                       "credential action missing render plan should block action");
  fails += expect_true(action.submit_blocked == 1 &&
                           action.submit_enabled == 0 &&
                           action.auth_attempt_allowed == 0 &&
                           action.raw_secret_exposed == 0 &&
                           action.masked_text_exposed == 0,
                       "credential action missing render plan must stay redacted");
  fails += expect_true(strings_equal(action.result_action,
                                     "use-text-login") &&
                           strings_equal(action.blocked_reason,
                                         "render-plan-unavailable"),
                       "credential action missing render plan should force text login");

  ops = build_ops();
  fails += expect_true(login_window_credential_screen_session_build(
                           &ops, "en", NULL, 0,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x',
                           masked, sizeof(masked), 0, 0, &session) == 0,
                       "credential action unsafe session should build");
  fails += expect_true(login_window_credential_screen_render_plan_build(
                           &session, &render) == 0,
                       "credential action unsafe render plan should build");
  fails += expect_true(login_window_credential_screen_action_plan_build(
                           &render,
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           &action) == 0,
                       "credential action unsafe plan should build blocked state");
  fails += expect_true(action.render_plan_available == 1 &&
                           action.render_plan_safe == 0 &&
                           action.action_allowed == 0 &&
                           action.action_blocked == 1,
                       "credential action unsafe render plan should block action");
  fails += expect_true(action.submit_blocked == 1 &&
                           action.submit_enabled == 0 &&
                           action.auth_attempt_allowed == 0,
                       "credential action unsafe render plan must keep auth disabled");
  fails += expect_true(strings_equal(action.result_action,
                                     "use-text-login") &&
                           strings_equal(action.blocked_reason,
                                         "credential-action-unsafe"),
                       "credential action unsafe render plan should force text login");
  return fails;
}


static int test_loginwindow_credential_screen_ui_event_reports_edit_focus(void) {
  int fails = 0;
  char storage[8] = "stale";
  char masked[8] = "mask";
  struct login_runtime_ops ops;
  struct login_window_credential_screen_session session;
  struct login_window_credential_screen_render_plan render;
  struct login_window_credential_screen_action_plan action;
  struct login_window_credential_screen_ui_event event;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_credential_screen_session_build(
                           &ops, "en", storage, sizeof(storage),
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'e',
                           masked, sizeof(masked), 0, 0, &session) == 0,
                       "credential ui event edit session should build");
  fails += expect_true(login_window_credential_screen_render_plan_build(
                           &session, &render) == 0,
                       "credential ui event edit render plan should build");
  fails += expect_true(login_window_credential_screen_action_plan_build(
                           &render,
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           &action) == 0,
                       "credential ui event edit action plan should build");
  fails += expect_true(login_window_credential_screen_ui_event_build(
                           &action, &event) == 0,
                       "credential ui event edit should build");
  fails += expect_true(event.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_UI_EVENT_VERSION,
                       "credential ui event should expose stable version");
  fails += expect_true(event.action_plan_available == 1 &&
                           event.action_plan_safe == 1 &&
                           event.ui_event_safe == 1,
                       "credential ui event edit should require safe action plan");
  fails += expect_true(event.action_allowed == 1 &&
                           event.action_blocked == 0 &&
                           event.input_focus_allowed == 1 &&
                           event.edit_credential_requested == 1,
                       "credential ui event edit should expose focus intent");
  fails += expect_true(event.credential_session_safe == 1 &&
                           event.credential_storage_wiped == 1 &&
                           event.credential_redacted == 1 &&
                           event.length_redacted == 1,
                       "credential ui event edit should carry safe redaction flags");
  fails += expect_true(event.raw_secret_exposed == 0 &&
                           event.masked_text_exposed == 0 &&
                           event.submit_requested == 0 &&
                           event.submit_blocked == 1 &&
                           event.submit_enabled == 0 &&
                           event.auth_attempt_allowed == 0,
                       "credential ui event edit must not expose auth or secrets");
  fails += expect_true(strings_equal(event.event_type,
                                     "credential-screen-edit-focus") &&
                           strings_equal(event.result_action,
                                         "edit-credential"),
                       "credential ui event edit should classify focus event");
  return fails;
}

static int test_loginwindow_credential_screen_ui_event_reports_text_recovery(void) {
  int fails = 0;
  char storage[8] = "old";
  char masked[8] = "mask";
  struct login_runtime_ops ops;
  struct login_window_credential_screen_session session;
  struct login_window_credential_screen_render_plan render;
  struct login_window_credential_screen_action_plan action;
  struct login_window_credential_screen_ui_event event;

  reset_test_state();
  ops = build_ops();
  ops.maintenance_mode = 1;
  g_runtime_maintenance_active = 1;
  fails += expect_true(login_window_credential_screen_session_build(
                           &ops, "en", storage, sizeof(storage),
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, '\0',
                           masked, sizeof(masked), 0, 0, &session) == 0,
                       "credential ui event recovery session should build");
  fails += expect_true(login_window_credential_screen_render_plan_build(
                           &session, &render) == 0,
                       "credential ui event recovery render plan should build");
  fails += expect_true(login_window_credential_screen_action_plan_build(
                           &render,
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           &action) == 0,
                       "credential ui event recovery action plan should build");
  fails += expect_true(login_window_credential_screen_ui_event_build(
                           &action, &event) == 0,
                       "credential ui event recovery should build");
  fails += expect_true(event.ui_event_safe == 1 &&
                           event.action_allowed == 1 &&
                           event.open_text_recovery_requested == 1 &&
                           event.recovery_text_session_required == 1,
                       "credential ui event recovery should expose text recovery intent");
  fails += expect_true(event.input_focus_allowed == 0 &&
                           event.submit_blocked == 1 &&
                           event.submit_enabled == 0 &&
                           event.auth_attempt_allowed == 0,
                       "credential ui event recovery must keep GUI auth disabled");
  fails += expect_true(event.raw_secret_exposed == 0 &&
                           event.masked_text_exposed == 0,
                       "credential ui event recovery must remain redacted");
  fails += expect_true(strings_equal(event.event_type,
                                     "credential-screen-open-text-recovery") &&
                           strings_equal(event.result_action,
                                         "open-text-recovery"),
                       "credential ui event recovery should classify recovery event");
  return fails;
}

static int test_loginwindow_credential_screen_ui_event_reports_resume_text_login(void) {
  int fails = 0;
  char storage[8] = "old";
  char masked[8] = "mask";
  struct login_runtime_ops ops;
  struct login_window_credential_screen_session session;
  struct login_window_credential_screen_render_plan render;
  struct login_window_credential_screen_action_plan action;
  struct login_window_credential_screen_ui_event event;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_credential_screen_session_build(
                           &ops, "en", storage, sizeof(storage),
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r',
                           masked, sizeof(masked), 1, 1, &session) == 0,
                       "credential ui event resume session should build");
  fails += expect_true(login_window_credential_screen_render_plan_build(
                           &session, &render) == 0,
                       "credential ui event resume render plan should build");
  fails += expect_true(login_window_credential_screen_action_plan_build(
                           &render,
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           &action) == 0,
                       "credential ui event resume action plan should build");
  fails += expect_true(login_window_credential_screen_ui_event_build(
                           &action, &event) == 0,
                       "credential ui event resume should build");
  fails += expect_true(event.ui_event_safe == 1 &&
                           event.action_allowed == 1 &&
                           event.resume_text_login_requested == 1 &&
                           event.session_reset_required == 1 &&
                           event.login_screen_rerender_required == 1,
                       "credential ui event resume should expose reset intent");
  fails += expect_true(event.input_focus_allowed == 0 &&
                           event.submit_requested == 0 &&
                           event.submit_enabled == 0 &&
                           event.auth_attempt_allowed == 0,
                       "credential ui event resume must keep GUI auth disabled");
  fails += expect_true(strings_equal(event.event_type,
                                     "credential-screen-resume-text-login") &&
                           strings_equal(event.result_action,
                                         "resume-text-login"),
                       "credential ui event resume should classify resume event");
  return fails;
}

static int test_loginwindow_credential_screen_ui_event_reports_submit_and_unknown_blocks(void) {
  int fails = 0;
  char storage[8] = "stale";
  char masked[8] = "mask";
  struct login_runtime_ops ops;
  struct login_window_credential_screen_session session;
  struct login_window_credential_screen_render_plan render;
  struct login_window_credential_screen_action_plan action;
  struct login_window_credential_screen_ui_event event;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_credential_screen_session_build(
                           &ops, "en", storage, sizeof(storage),
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's',
                           masked, sizeof(masked), 0, 0, &session) == 0,
                       "credential ui event blocked session should build");
  fails += expect_true(login_window_credential_screen_render_plan_build(
                           &session, &render) == 0,
                       "credential ui event blocked render plan should build");
  fails += expect_true(login_window_credential_screen_action_plan_build(
                           &render,
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           &action) == 0,
                       "credential ui event submit action plan should build");
  fails += expect_true(login_window_credential_screen_ui_event_build(
                           &action, &event) == 0,
                       "credential ui event submit should build");
  fails += expect_true(event.ui_event_safe == 1 &&
                           event.submit_requested == 1 &&
                           event.action_allowed == 0 &&
                           event.action_blocked == 1,
                       "credential ui event submit should stay blocked");
  fails += expect_true(event.submit_blocked == 1 &&
                           event.submit_enabled == 0 &&
                           event.auth_attempt_allowed == 0 &&
                           event.raw_secret_exposed == 0 &&
                           event.masked_text_exposed == 0,
                       "credential ui event submit must stay redacted");
  fails += expect_true(strings_equal(event.event_type,
                                     "credential-screen-submit-blocked") &&
                           strings_equal(event.result_action,
                                         "use-text-login") &&
                           strings_equal(event.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential ui event submit should classify blocked submit");
  fails += expect_true(login_window_credential_screen_action_plan_build(
                           &render, 99, &action) == 0,
                       "credential ui event unknown action plan should build");
  fails += expect_true(login_window_credential_screen_ui_event_build(
                           &action, &event) == 0,
                       "credential ui event unknown should build");
  fails += expect_true(event.ui_event_safe == 1 &&
                           event.action_allowed == 0 &&
                           event.action_blocked == 1 &&
                           event.input_focus_allowed == 0,
                       "credential ui event unknown should block action");
  fails += expect_true(strings_equal(event.event_type,
                                     "credential-screen-action-blocked") &&
                           strings_equal(event.result_action,
                                         "use-text-login") &&
                           strings_equal(event.blocked_reason,
                                         "credential-action-unknown"),
                       "credential ui event unknown should classify blocked action");
  return fails;
}

static int test_loginwindow_credential_screen_ui_event_fails_closed_for_unsafe_or_missing_action_plan(void) {
  int fails = 0;
  char masked[8] = "mask";
  struct login_runtime_ops ops;
  struct login_window_credential_screen_session session;
  struct login_window_credential_screen_render_plan render;
  struct login_window_credential_screen_action_plan action;
  struct login_window_credential_screen_ui_event event;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_ui_event_build(
                           NULL, &event) == 0,
                       "credential ui event missing action plan should build fail-closed state");
  fails += expect_true(event.action_plan_available == 0 &&
                           event.action_plan_safe == 0 &&
                           event.ui_event_safe == 0 &&
                           event.action_allowed == 0 &&
                           event.action_blocked == 1,
                       "credential ui event missing action plan should block event");
  fails += expect_true(event.submit_blocked == 1 &&
                           event.submit_enabled == 0 &&
                           event.auth_attempt_allowed == 0 &&
                           event.raw_secret_exposed == 0 &&
                           event.masked_text_exposed == 0,
                       "credential ui event missing action plan must stay redacted");
  fails += expect_true(strings_equal(event.event_type,
                                     "credential-screen-action-unavailable") &&
                           strings_equal(event.blocked_reason,
                                         "action-plan-unavailable"),
                       "credential ui event missing action plan should explain block");

  ops = build_ops();
  fails += expect_true(login_window_credential_screen_session_build(
                           &ops, "en", NULL, 0,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x',
                           masked, sizeof(masked), 0, 0, &session) == 0,
                       "credential ui event unsafe session should build");
  fails += expect_true(login_window_credential_screen_render_plan_build(
                           &session, &render) == 0,
                       "credential ui event unsafe render plan should build");
  fails += expect_true(login_window_credential_screen_action_plan_build(
                           &render,
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           &action) == 0,
                       "credential ui event unsafe action plan should build");
  fails += expect_true(login_window_credential_screen_ui_event_build(
                           &action, &event) == 0,
                       "credential ui event unsafe should build blocked state");
  fails += expect_true(event.action_plan_available == 1 &&
                           event.action_plan_safe == 0 &&
                           event.ui_event_safe == 0 &&
                           event.action_allowed == 0 &&
                           event.action_blocked == 1,
                       "credential ui event unsafe action plan should block event");
  fails += expect_true(event.submit_blocked == 1 &&
                           event.submit_enabled == 0 &&
                           event.auth_attempt_allowed == 0,
                       "credential ui event unsafe action plan must keep auth disabled");
  fails += expect_true(strings_equal(event.event_type,
                                     "credential-screen-action-unsafe") &&
                           strings_equal(event.result_action,
                                         "use-text-login") &&
                           strings_equal(event.blocked_reason,
                                         "credential-ui-event-unsafe"),
                       "credential ui event unsafe action plan should force text login");
  return fails;
}

int test_login_runtime_credential_action_event_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_action_plan_allows_edit_focus();
  fails += test_loginwindow_credential_screen_action_plan_allows_text_recovery();
  fails += test_loginwindow_credential_screen_action_plan_allows_resume_text_login();
  fails += test_loginwindow_credential_screen_action_plan_blocks_submit_and_unknown();
  fails += test_loginwindow_credential_screen_action_plan_fails_closed_for_unsafe_or_missing_plan();
  fails += test_loginwindow_credential_screen_ui_event_reports_edit_focus();
  fails += test_loginwindow_credential_screen_ui_event_reports_text_recovery();
  fails += test_loginwindow_credential_screen_ui_event_reports_resume_text_login();
  fails += test_loginwindow_credential_screen_ui_event_reports_submit_and_unknown_blocks();
  fails += test_loginwindow_credential_screen_ui_event_fails_closed_for_unsafe_or_missing_action_plan();
  return fails;
}
