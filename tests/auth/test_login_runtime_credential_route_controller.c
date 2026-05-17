/*
 * tests/auth/test_login_runtime_credential_route_controller.c
 *
 * Credential screen route plan + screen controller coverage for the
 * `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-15 monolith
 * refactor (PR D.8 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_route_plan_build`: 5 tests
 *     covering the stay-on-edit route + the open-text-recovery route
 *     + the resume-text-login route + the submit/unknown text-login
 *     forced route + the missing-or-unsafe UI event fail-closed
 *     default.
 *   - `login_window_credential_screen_controller_build`: 5 tests
 *     covering the edit-focus controller + the open-text-recovery
 *     controller + the resume-text-login controller + the
 *     submit/unknown forced controller + the missing-or-unsafe route
 *     fail-closed default.
 *
 * Also exposes the shared helper
 * `build_loginwindow_credential_screen_controller_for_action`, used
 * by later companion files that chain on top of the credential
 * screen controller (presenter, binding, mount, commit, handoff,
 * etc.). The helper is declared in
 * `tests/auth/test_login_runtime_internal.h`.
 *
 * The companion entry `test_login_runtime_credential_route_controller_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`. Shared fixture state and helpers
 * come from `tests/auth/test_login_runtime_internal.h`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

static int test_loginwindow_credential_screen_route_plan_stays_on_edit(void) {
  int fails = 0;
  char storage[8] = "stale";
  char masked[8] = "mask";
  struct login_runtime_ops ops;
  struct login_window_credential_screen_session session;
  struct login_window_credential_screen_render_plan render;
  struct login_window_credential_screen_action_plan action;
  struct login_window_credential_screen_ui_event event;
  struct login_window_credential_screen_route_plan route;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_credential_screen_session_build(
                           &ops, "en", storage, sizeof(storage),
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'e',
                           masked, sizeof(masked), 0, 0, &session) == 0,
                       "credential route edit session should build");
  fails += expect_true(login_window_credential_screen_render_plan_build(
                           &session, &render) == 0,
                       "credential route edit render plan should build");
  fails += expect_true(login_window_credential_screen_action_plan_build(
                           &render,
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           &action) == 0,
                       "credential route edit action plan should build");
  fails += expect_true(login_window_credential_screen_ui_event_build(
                           &action, &event) == 0,
                       "credential route edit ui event should build");
  fails += expect_true(login_window_credential_screen_route_plan_build(
                           &event, &route) == 0,
                       "credential route edit should build");
  fails += expect_true(route.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_ROUTE_PLAN_VERSION,
                       "credential route plan should expose stable version");
  fails += expect_true(route.ui_event_available == 1 &&
                           route.ui_event_safe == 1 &&
                           route.route_plan_safe == 1,
                       "credential route edit should require safe ui event");
  fails += expect_true(route.route_selected == 1 &&
                           route.route_blocked == 0 &&
                           route.stay_on_credential_screen == 1 &&
                           route.force_text_login_required == 0,
                       "credential route edit should stay on credential screen");
  fails += expect_true(route.input_focus_allowed == 1 &&
                           route.action_allowed == 1 &&
                           route.action_blocked == 0,
                       "credential route edit should allow focus only");
  fails += expect_true(route.raw_secret_exposed == 0 &&
                           route.masked_text_exposed == 0 &&
                           route.submit_requested == 0 &&
                           route.submit_blocked == 1 &&
                           route.submit_enabled == 0 &&
                           route.auth_attempt_allowed == 0,
                       "credential route edit must not expose auth or secrets");
  fails += expect_true(strings_equal(route.route,
                                     "stay-on-credential-screen") &&
                           strings_equal(route.blocked_reason, "ready"),
                       "credential route edit should report stay route");
  return fails;
}

static int test_loginwindow_credential_screen_route_plan_opens_text_recovery(void) {
  int fails = 0;
  char storage[8] = "old";
  char masked[8] = "mask";
  struct login_runtime_ops ops;
  struct login_window_credential_screen_session session;
  struct login_window_credential_screen_render_plan render;
  struct login_window_credential_screen_action_plan action;
  struct login_window_credential_screen_ui_event event;
  struct login_window_credential_screen_route_plan route;

  reset_test_state();
  ops = build_ops();
  ops.maintenance_mode = 1;
  g_runtime_maintenance_active = 1;
  fails += expect_true(login_window_credential_screen_session_build(
                           &ops, "en", storage, sizeof(storage),
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, '\0',
                           masked, sizeof(masked), 0, 0, &session) == 0,
                       "credential route recovery session should build");
  fails += expect_true(login_window_credential_screen_render_plan_build(
                           &session, &render) == 0,
                       "credential route recovery render plan should build");
  fails += expect_true(login_window_credential_screen_action_plan_build(
                           &render,
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           &action) == 0,
                       "credential route recovery action plan should build");
  fails += expect_true(login_window_credential_screen_ui_event_build(
                           &action, &event) == 0,
                       "credential route recovery ui event should build");
  fails += expect_true(login_window_credential_screen_route_plan_build(
                           &event, &route) == 0,
                       "credential route recovery should build");
  fails += expect_true(route.route_plan_safe == 1 &&
                           route.route_selected == 1 &&
                           route.route_blocked == 0,
                       "credential route recovery should select safe route");
  fails += expect_true(route.open_text_recovery_route == 1 &&
                           route.recovery_text_session_required == 1 &&
                           route.force_text_login_required == 1,
                       "credential route recovery should require text recovery route");
  fails += expect_true(route.input_focus_allowed == 0 &&
                           route.submit_blocked == 1 &&
                           route.submit_enabled == 0 &&
                           route.auth_attempt_allowed == 0,
                       "credential route recovery must keep GUI auth disabled");
  fails += expect_true(route.raw_secret_exposed == 0 &&
                           route.masked_text_exposed == 0,
                       "credential route recovery must remain redacted");
  fails += expect_true(strings_equal(route.route,
                                     "open-text-recovery") &&
                           strings_equal(route.blocked_reason,
                                         "text-recovery-only"),
                       "credential route recovery should report recovery route");
  return fails;
}

static int test_loginwindow_credential_screen_route_plan_resumes_text_login(void) {
  int fails = 0;
  char storage[8] = "old";
  char masked[8] = "mask";
  struct login_runtime_ops ops;
  struct login_window_credential_screen_session session;
  struct login_window_credential_screen_render_plan render;
  struct login_window_credential_screen_action_plan action;
  struct login_window_credential_screen_ui_event event;
  struct login_window_credential_screen_route_plan route;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_credential_screen_session_build(
                           &ops, "en", storage, sizeof(storage),
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r',
                           masked, sizeof(masked), 1, 1, &session) == 0,
                       "credential route resume session should build");
  fails += expect_true(login_window_credential_screen_render_plan_build(
                           &session, &render) == 0,
                       "credential route resume render plan should build");
  fails += expect_true(login_window_credential_screen_action_plan_build(
                           &render,
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           &action) == 0,
                       "credential route resume action plan should build");
  fails += expect_true(login_window_credential_screen_ui_event_build(
                           &action, &event) == 0,
                       "credential route resume ui event should build");
  fails += expect_true(login_window_credential_screen_route_plan_build(
                           &event, &route) == 0,
                       "credential route resume should build");
  fails += expect_true(route.route_plan_safe == 1 &&
                           route.route_selected == 1 &&
                           route.route_blocked == 0,
                       "credential route resume should select safe route");
  fails += expect_true(route.resume_text_login_route == 1 &&
                           route.session_reset_required == 1 &&
                           route.login_screen_rerender_required == 1 &&
                           route.force_text_login_required == 1,
                       "credential route resume should require reset and rerender");
  fails += expect_true(route.input_focus_allowed == 0 &&
                           route.submit_requested == 0 &&
                           route.submit_enabled == 0 &&
                           route.auth_attempt_allowed == 0,
                       "credential route resume must keep GUI auth disabled");
  fails += expect_true(strings_equal(route.route,
                                     "resume-text-login") &&
                           strings_equal(route.blocked_reason, "ready"),
                       "credential route resume should report resume route");
  return fails;
}

static int test_loginwindow_credential_screen_route_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  char storage[8] = "stale";
  char masked[8] = "mask";
  struct login_runtime_ops ops;
  struct login_window_credential_screen_session session;
  struct login_window_credential_screen_render_plan render;
  struct login_window_credential_screen_action_plan action;
  struct login_window_credential_screen_ui_event event;
  struct login_window_credential_screen_route_plan route;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_credential_screen_session_build(
                           &ops, "en", storage, sizeof(storage),
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's',
                           masked, sizeof(masked), 0, 0, &session) == 0,
                       "credential route block session should build");
  fails += expect_true(login_window_credential_screen_render_plan_build(
                           &session, &render) == 0,
                       "credential route block render plan should build");
  fails += expect_true(login_window_credential_screen_action_plan_build(
                           &render,
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           &action) == 0,
                       "credential route submit action plan should build");
  fails += expect_true(login_window_credential_screen_ui_event_build(
                           &action, &event) == 0,
                       "credential route submit ui event should build");
  fails += expect_true(login_window_credential_screen_route_plan_build(
                           &event, &route) == 0,
                       "credential route submit should build");
  fails += expect_true(route.route_plan_safe == 1 &&
                           route.route_selected == 1 &&
                           route.route_blocked == 0 &&
                           route.force_text_login_required == 1,
                       "credential route submit should force text login route");
  fails += expect_true(route.submit_requested == 1 &&
                           route.action_allowed == 0 &&
                           route.action_blocked == 1 &&
                           route.input_focus_allowed == 0,
                       "credential route submit should block GUI action");
  fails += expect_true(route.submit_blocked == 1 &&
                           route.submit_enabled == 0 &&
                           route.auth_attempt_allowed == 0 &&
                           route.raw_secret_exposed == 0 &&
                           route.masked_text_exposed == 0,
                       "credential route submit must stay redacted");
  fails += expect_true(strings_equal(route.route,
                                     "force-text-login") &&
                           strings_equal(route.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential route submit should explain text fallback");

  fails += expect_true(login_window_credential_screen_action_plan_build(
                           &render, 99, &action) == 0,
                       "credential route unknown action plan should build");
  fails += expect_true(login_window_credential_screen_ui_event_build(
                           &action, &event) == 0,
                       "credential route unknown ui event should build");
  fails += expect_true(login_window_credential_screen_route_plan_build(
                           &event, &route) == 0,
                       "credential route unknown should build");
  fails += expect_true(route.route_plan_safe == 1 &&
                           route.route_selected == 1 &&
                           route.route_blocked == 0 &&
                           route.force_text_login_required == 1,
                       "credential route unknown should choose text login route");
  fails += expect_true(route.action_allowed == 0 &&
                           route.action_blocked == 1 &&
                           route.input_focus_allowed == 0,
                       "credential route unknown should block GUI action");
  fails += expect_true(strings_equal(route.route,
                                     "force-text-login") &&
                           strings_equal(route.blocked_reason,
                                         "credential-action-unknown"),
                       "credential route unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_route_plan_fails_closed_for_unsafe_or_missing_event(void) {
  int fails = 0;
  char masked[8] = "mask";
  struct login_runtime_ops ops;
  struct login_window_credential_screen_session session;
  struct login_window_credential_screen_render_plan render;
  struct login_window_credential_screen_action_plan action;
  struct login_window_credential_screen_ui_event event;
  struct login_window_credential_screen_route_plan route;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_route_plan_build(
                           NULL, &route) == 0,
                       "credential route missing event should build fail-closed state");
  fails += expect_true(route.ui_event_available == 0 &&
                           route.ui_event_safe == 0 &&
                           route.route_plan_safe == 0 &&
                           route.route_selected == 0 &&
                           route.route_blocked == 1,
                       "credential route missing event should block route");
  fails += expect_true(route.force_text_login_required == 1 &&
                           route.submit_blocked == 1 &&
                           route.submit_enabled == 0 &&
                           route.auth_attempt_allowed == 0 &&
                           route.raw_secret_exposed == 0 &&
                           route.masked_text_exposed == 0,
                       "credential route missing event must stay redacted");
  fails += expect_true(strings_equal(route.route,
                                     "force-text-login") &&
                           strings_equal(route.event_type,
                                         "credential-screen-route-unavailable") &&
                           strings_equal(route.blocked_reason,
                                         "ui-event-unavailable"),
                       "credential route missing event should explain block");

  ops = build_ops();
  fails += expect_true(login_window_credential_screen_session_build(
                           &ops, "en", NULL, 0,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x',
                           masked, sizeof(masked), 0, 0, &session) == 0,
                       "credential route unsafe session should build");
  fails += expect_true(login_window_credential_screen_render_plan_build(
                           &session, &render) == 0,
                       "credential route unsafe render plan should build");
  fails += expect_true(login_window_credential_screen_action_plan_build(
                           &render,
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           &action) == 0,
                       "credential route unsafe action plan should build");
  fails += expect_true(login_window_credential_screen_ui_event_build(
                           &action, &event) == 0,
                       "credential route unsafe ui event should build");
  fails += expect_true(login_window_credential_screen_route_plan_build(
                           &event, &route) == 0,
                       "credential route unsafe should build blocked state");
  fails += expect_true(route.ui_event_available == 1 &&
                           route.ui_event_safe == 0 &&
                           route.route_plan_safe == 0 &&
                           route.route_selected == 0 &&
                           route.route_blocked == 1,
                       "credential route unsafe event should block route");
  fails += expect_true(route.action_allowed == 0 &&
                           route.action_blocked == 1 &&
                           route.input_focus_allowed == 0 &&
                           route.submit_enabled == 0 &&
                           route.auth_attempt_allowed == 0,
                       "credential route unsafe event must keep auth disabled");
  fails += expect_true(strings_equal(route.route,
                                     "force-text-login") &&
                           strings_equal(route.event_type,
                                         "credential-screen-route-unsafe") &&
                           strings_equal(route.blocked_reason,
                                         "credential-route-unsafe"),
                       "credential route unsafe event should force text login");
  return fails;
}


int build_loginwindow_credential_screen_controller_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_controller *controller) {
  char storage[8] = "stale";
  char masked[8] = "mask";
  struct login_runtime_ops ops;
  struct login_window_credential_screen_session session;
  struct login_window_credential_screen_render_plan render;
  struct login_window_credential_screen_action_plan action;
  struct login_window_credential_screen_ui_event event;
  struct login_window_credential_screen_route_plan route;

  reset_test_state();
  ops = build_ops();
  if (maintenance_mode) {
    ops.maintenance_mode = 1;
    g_runtime_maintenance_active = 1;
  }
  if (login_window_credential_screen_session_build(
          &ops, "en", use_storage ? storage : NULL,
          use_storage ? sizeof(storage) : 0, input_action, ch,
          masked, sizeof(masked), recovery_session_active, resume_requested,
          &session) != 0) {
    return -1;
  }
  if (login_window_credential_screen_render_plan_build(&session, &render) != 0) {
    return -1;
  }
  if (login_window_credential_screen_action_plan_build(
          &render, requested_action, &action) != 0) {
    return -1;
  }
  if (login_window_credential_screen_ui_event_build(&action, &event) != 0) {
    return -1;
  }
  if (login_window_credential_screen_route_plan_build(&event, &route) != 0) {
    return -1;
  }
  return login_window_credential_screen_controller_build(&route, controller);
}

static int test_loginwindow_credential_screen_controller_keeps_edit_focus(void) {
  int fails = 0;
  struct login_window_credential_screen_controller controller;

  fails += expect_true(build_loginwindow_credential_screen_controller_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'e', 0, 0, 0,
                           1, &controller) == 0,
                       "credential controller edit should build");
  fails += expect_true(controller.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_CONTROLLER_VERSION,
                       "credential controller should expose stable version");
  fails += expect_true(controller.route_plan_available == 1 &&
                           controller.route_plan_safe == 1 &&
                           controller.controller_safe == 1,
                       "credential controller edit should require safe route plan");
  fails += expect_true(controller.credential_screen_visible == 1 &&
                           controller.credential_input_focus == 1 &&
                           controller.text_login_forced == 0,
                       "credential controller edit should keep credential focus only");
  fails += expect_true(controller.action_allowed == 1 &&
                           controller.action_blocked == 0 &&
                           controller.input_focus_allowed == 1,
                       "credential controller edit should allow only focus action");
  fails += expect_true(controller.raw_secret_exposed == 0 &&
                           controller.masked_text_exposed == 0 &&
                           controller.submit_requested == 0 &&
                           controller.submit_blocked == 1 &&
                           controller.submit_enabled == 0 &&
                           controller.auth_attempt_allowed == 0,
                       "credential controller edit must not expose auth or secrets");
  fails += expect_true(strings_equal(controller.route,
                                     "stay-on-credential-screen") &&
                           strings_equal(controller.state,
                                         "controller-credential-ready") &&
                           strings_equal(controller.blocked_reason, "ready"),
                       "credential controller edit should report credential-ready state");
  return fails;
}

static int test_loginwindow_credential_screen_controller_opens_text_recovery(void) {
  int fails = 0;
  struct login_window_credential_screen_controller controller;

  fails += expect_true(build_loginwindow_credential_screen_controller_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &controller) == 0,
                       "credential controller recovery should build");
  fails += expect_true(controller.controller_safe == 1 &&
                           controller.text_recovery_open == 1 &&
                           controller.text_login_forced == 1,
                       "credential controller recovery should open text recovery");
  fails += expect_true(controller.recovery_text_session_required == 1 &&
                           controller.credential_input_focus == 0 &&
                           controller.input_focus_allowed == 0,
                       "credential controller recovery should block credential focus");
  fails += expect_true(controller.submit_blocked == 1 &&
                           controller.submit_enabled == 0 &&
                           controller.auth_attempt_allowed == 0 &&
                           controller.raw_secret_exposed == 0 &&
                           controller.masked_text_exposed == 0,
                       "credential controller recovery must keep GUI auth disabled");
  fails += expect_true(strings_equal(controller.route,
                                     "open-text-recovery") &&
                           strings_equal(controller.state,
                                         "controller-text-recovery-ready") &&
                           strings_equal(controller.blocked_reason,
                                         "text-recovery-only"),
                       "credential controller recovery should report text recovery state");
  return fails;
}

static int test_loginwindow_credential_screen_controller_resumes_text_login(void) {
  int fails = 0;
  struct login_window_credential_screen_controller controller;

  fails += expect_true(build_loginwindow_credential_screen_controller_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &controller) == 0,
                       "credential controller resume should build");
  fails += expect_true(controller.controller_safe == 1 &&
                           controller.text_login_resume == 1 &&
                           controller.text_login_forced == 1,
                       "credential controller resume should resume text login");
  fails += expect_true(controller.session_reset_required == 1 &&
                           controller.login_screen_rerender_required == 1 &&
                           controller.credential_input_focus == 0,
                       "credential controller resume should require reset and rerender");
  fails += expect_true(controller.submit_requested == 0 &&
                           controller.submit_blocked == 1 &&
                           controller.submit_enabled == 0 &&
                           controller.auth_attempt_allowed == 0,
                       "credential controller resume must keep GUI auth disabled");
  fails += expect_true(strings_equal(controller.route,
                                     "resume-text-login") &&
                           strings_equal(controller.state,
                                         "controller-resume-ready") &&
                           strings_equal(controller.blocked_reason, "ready"),
                       "credential controller resume should report resume state");
  return fails;
}

static int test_loginwindow_credential_screen_controller_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_controller controller;

  fails += expect_true(build_loginwindow_credential_screen_controller_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &controller) == 0,
                       "credential controller submit should build");
  fails += expect_true(controller.controller_safe == 1 &&
                           controller.text_login_forced == 1 &&
                           controller.submit_requested == 1,
                       "credential controller submit should force text login");
  fails += expect_true(controller.action_allowed == 0 &&
                           controller.action_blocked == 1 &&
                           controller.input_focus_allowed == 0 &&
                           controller.credential_input_focus == 0,
                       "credential controller submit should block GUI action");
  fails += expect_true(controller.submit_blocked == 1 &&
                           controller.submit_enabled == 0 &&
                           controller.auth_attempt_allowed == 0 &&
                           controller.raw_secret_exposed == 0 &&
                           controller.masked_text_exposed == 0,
                       "credential controller submit must stay redacted");
  fails += expect_true(strings_equal(controller.route,
                                     "force-text-login") &&
                           strings_equal(controller.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential controller submit should explain text fallback");

  fails += expect_true(build_loginwindow_credential_screen_controller_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &controller) == 0,
                       "credential controller unknown should build");
  fails += expect_true(controller.controller_safe == 1 &&
                           controller.text_login_forced == 1 &&
                           controller.action_allowed == 0 &&
                           controller.action_blocked == 1,
                       "credential controller unknown should force text login");
  fails += expect_true(strings_equal(controller.route,
                                     "force-text-login") &&
                           strings_equal(controller.blocked_reason,
                                         "credential-action-unknown"),
                       "credential controller unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_controller_fails_closed_for_unsafe_or_missing_route(void) {
  int fails = 0;
  struct login_window_credential_screen_controller controller;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_controller_build(
                           NULL, &controller) == 0,
                       "credential controller missing route should build fail-closed state");
  fails += expect_true(controller.route_plan_available == 0 &&
                           controller.route_plan_safe == 0 &&
                           controller.controller_safe == 0 &&
                           controller.route_selected == 0 &&
                           controller.route_blocked == 1,
                       "credential controller missing route should block controller");
  fails += expect_true(controller.text_login_forced == 1 &&
                           controller.submit_blocked == 1 &&
                           controller.submit_enabled == 0 &&
                           controller.auth_attempt_allowed == 0 &&
                           controller.raw_secret_exposed == 0 &&
                           controller.masked_text_exposed == 0,
                       "credential controller missing route must stay redacted");
  fails += expect_true(strings_equal(controller.route,
                                     "force-text-login") &&
                           strings_equal(controller.event_type,
                                         "credential-screen-controller-unavailable") &&
                           strings_equal(controller.blocked_reason,
                                         "route-plan-unavailable"),
                       "credential controller missing route should explain block");

  fails += expect_true(build_loginwindow_credential_screen_controller_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &controller) == 0,
                       "credential controller unsafe route should build blocked state");
  fails += expect_true(controller.route_plan_available == 1 &&
                           controller.route_plan_safe == 0 &&
                           controller.controller_safe == 0 &&
                           controller.route_selected == 0 &&
                           controller.route_blocked == 1,
                       "credential controller unsafe route should block controller");
  fails += expect_true(controller.action_allowed == 0 &&
                           controller.action_blocked == 1 &&
                           controller.input_focus_allowed == 0 &&
                           controller.credential_input_focus == 0 &&
                           controller.text_login_forced == 1,
                       "credential controller unsafe route must force text login");
  fails += expect_true(strings_equal(controller.route,
                                     "force-text-login") &&
                           strings_equal(controller.event_type,
                                         "credential-screen-controller-unsafe") &&
                           strings_equal(controller.blocked_reason,
                                         "credential-controller-unsafe"),
                       "credential controller unsafe route should force text login");
  return fails;
}

int test_login_runtime_credential_route_controller_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_route_plan_stays_on_edit();
  fails += test_loginwindow_credential_screen_route_plan_opens_text_recovery();
  fails += test_loginwindow_credential_screen_route_plan_resumes_text_login();
  fails += test_loginwindow_credential_screen_route_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_route_plan_fails_closed_for_unsafe_or_missing_event();
  fails += test_loginwindow_credential_screen_controller_keeps_edit_focus();
  fails += test_loginwindow_credential_screen_controller_opens_text_recovery();
  fails += test_loginwindow_credential_screen_controller_resumes_text_login();
  fails += test_loginwindow_credential_screen_controller_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_controller_fails_closed_for_unsafe_or_missing_route();
  return fails;
}
