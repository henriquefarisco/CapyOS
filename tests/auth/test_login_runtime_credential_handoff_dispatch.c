/*
 * tests/auth/test_login_runtime_credential_handoff_dispatch.c
 *
 * Credential screen handoff plan + dispatch plan coverage for the
 * `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-15 monolith
 * refactor (PR D.11 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_handoff_plan_build`: 5 tests
 *     covering the credential widgets handoff + the text-recovery
 *     handoff + the resume-text-login handoff + the submit/unknown
 *     fallback handoff + the missing-or-unsafe commit plan
 *     fail-closed default.
 *   - `login_window_credential_screen_dispatch_plan_build`: 5 tests
 *     covering the credential widgets dispatch + the text-recovery
 *     dispatch + the resume-text-login dispatch + the submit/unknown
 *     fallback dispatch + the missing-or-unsafe handoff plan
 *     fail-closed default.
 *
 * Also exposes shared helpers
 * `build_loginwindow_credential_screen_handoff_plan_for_action` and
 * `build_loginwindow_credential_screen_dispatch_plan_for_action`,
 * used by later companion files that chain on top of the
 * handoff/dispatch stages (queue, activation, frame, surface, ...).
 *
 * The companion entry `test_login_runtime_credential_handoff_dispatch_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_handoff_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_handoff_plan *handoff_plan) {
  struct login_window_credential_screen_commit_plan commit_plan;

  if (build_loginwindow_credential_screen_commit_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &commit_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_handoff_plan_build(&commit_plan, handoff_plan);
}

static int test_loginwindow_credential_screen_handoff_plan_hands_off_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_handoff_plan handoff_plan;

  fails += expect_true(build_loginwindow_credential_screen_handoff_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'e', 0, 0, 0,
                           1, &handoff_plan) == 0,
                       "credential handoff plan edit should build");
  fails += expect_true(handoff_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_HANDOFF_PLAN_VERSION,
                       "credential handoff plan should expose stable version");
  fails += expect_true(handoff_plan.commit_plan_available == 1 &&
                           handoff_plan.commit_plan_safe == 1 &&
                           handoff_plan.handoff_plan_safe == 1,
                       "credential handoff plan edit should require safe commit plan");
  fails += expect_true(handoff_plan.window_handoff_allowed == 1 &&
                           handoff_plan.window_handoff_delivered == 0 &&
                           handoff_plan.envelope_selected == 1,
                       "credential handoff plan edit should remain declarative");
  fails += expect_true(handoff_plan.handoff_credential_panel == 1 &&
                           handoff_plan.handoff_credential_input == 1 &&
                           handoff_plan.handoff_credential_focus == 1,
                       "credential handoff plan edit should hand off credential widgets");
  fails += expect_true(handoff_plan.submit_callback_bound == 0 &&
                           handoff_plan.auth_callback_bound == 0 &&
                           handoff_plan.submit_enabled == 0 &&
                           handoff_plan.auth_attempt_allowed == 0,
                       "credential handoff plan edit must not bind auth callbacks");
  fails += expect_true(handoff_plan.raw_secret_exposed == 0 &&
                           handoff_plan.masked_text_exposed == 0 &&
                           handoff_plan.length_redacted == 1,
                       "credential handoff plan edit must stay redacted");
  fails += expect_true(strings_equal(handoff_plan.handoff_envelope,
                                     "credential-screen-handoff-envelope") &&
                           strings_equal(handoff_plan.focus_target,
                                         "credential-input") &&
                           strings_equal(handoff_plan.state,
                                         "handoff-credential-ready"),
                       "credential handoff plan edit should report handoff state");
  return fails;
}

static int test_loginwindow_credential_screen_handoff_plan_hands_off_text_recovery(void) {
  int fails = 0;
  struct login_window_credential_screen_handoff_plan handoff_plan;

  fails += expect_true(build_loginwindow_credential_screen_handoff_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &handoff_plan) == 0,
                       "credential handoff plan recovery should build");
  fails += expect_true(handoff_plan.handoff_plan_safe == 1 &&
                           handoff_plan.window_handoff_allowed == 1 &&
                           handoff_plan.window_handoff_delivered == 0 &&
                           handoff_plan.handoff_text_recovery == 1 &&
                           handoff_plan.handoff_text_login == 1,
                       "credential handoff plan recovery should hand off text recovery");
  fails += expect_true(handoff_plan.handoff_credential_input == 0 &&
                           handoff_plan.handoff_credential_focus == 0 &&
                           handoff_plan.input_focus_allowed == 0,
                       "credential handoff plan recovery should block credential focus");
  fails += expect_true(handoff_plan.submit_callback_bound == 0 &&
                           handoff_plan.auth_callback_bound == 0 &&
                           handoff_plan.submit_enabled == 0 &&
                           handoff_plan.auth_attempt_allowed == 0,
                       "credential handoff plan recovery must keep auth disabled");
  fails += expect_true(strings_equal(handoff_plan.handoff_envelope,
                                     "text-recovery-handoff-envelope") &&
                           strings_equal(handoff_plan.primary_action,
                                         "open-text-recovery") &&
                           strings_equal(handoff_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential handoff plan recovery should report recovery handoff");
  return fails;
}

static int test_loginwindow_credential_screen_handoff_plan_hands_off_resume_text_login(void) {
  int fails = 0;
  struct login_window_credential_screen_handoff_plan handoff_plan;

  fails += expect_true(build_loginwindow_credential_screen_handoff_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &handoff_plan) == 0,
                       "credential handoff plan resume should build");
  fails += expect_true(handoff_plan.handoff_plan_safe == 1 &&
                           handoff_plan.handoff_text_login == 1 &&
                           handoff_plan.handoff_text_login_resume == 1,
                       "credential handoff plan resume should hand off text login resume");
  fails += expect_true(handoff_plan.session_reset_required == 1 &&
                           handoff_plan.login_screen_rerender_required == 1 &&
                           handoff_plan.handoff_credential_focus == 0,
                       "credential handoff plan resume should require reset and rerender");
  fails += expect_true(handoff_plan.submit_callback_bound == 0 &&
                           handoff_plan.auth_callback_bound == 0 &&
                           handoff_plan.submit_enabled == 0 &&
                           handoff_plan.auth_attempt_allowed == 0,
                       "credential handoff plan resume must keep GUI auth disabled");
  fails += expect_true(strings_equal(handoff_plan.handoff_envelope,
                                     "text-login-resume-handoff-envelope") &&
                           strings_equal(handoff_plan.primary_action,
                                         "resume-text-login") &&
                           strings_equal(handoff_plan.state,
                                         "handoff-resume-ready"),
                       "credential handoff plan resume should report resume handoff");
  return fails;
}

static int test_loginwindow_credential_screen_handoff_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_handoff_plan handoff_plan;

  fails += expect_true(build_loginwindow_credential_screen_handoff_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &handoff_plan) == 0,
                       "credential handoff plan submit should build");
  fails += expect_true(handoff_plan.handoff_plan_safe == 1 &&
                           handoff_plan.submit_requested == 1 &&
                           handoff_plan.handoff_text_login_fallback == 1,
                       "credential handoff plan submit should hand off text login fallback");
  fails += expect_true(handoff_plan.action_allowed == 0 &&
                           handoff_plan.action_blocked == 1 &&
                           handoff_plan.input_focus_allowed == 0 &&
                           handoff_plan.handoff_credential_focus == 0,
                       "credential handoff plan submit should block GUI action");
  fails += expect_true(handoff_plan.window_handoff_allowed == 1 &&
                           handoff_plan.window_handoff_delivered == 0 &&
                           handoff_plan.submit_callback_bound == 0 &&
                           handoff_plan.auth_callback_bound == 0 &&
                           handoff_plan.submit_enabled == 0 &&
                           handoff_plan.auth_attempt_allowed == 0 &&
                           handoff_plan.raw_secret_exposed == 0 &&
                           handoff_plan.masked_text_exposed == 0,
                       "credential handoff plan submit must stay declarative and redacted");
  fails += expect_true(strings_equal(handoff_plan.handoff_envelope,
                                     "text-login-fallback-handoff-envelope") &&
                           strings_equal(handoff_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(handoff_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential handoff plan submit should explain fallback handoff");

  fails += expect_true(build_loginwindow_credential_screen_handoff_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &handoff_plan) == 0,
                       "credential handoff plan unknown should build");
  fails += expect_true(handoff_plan.handoff_plan_safe == 1 &&
                           handoff_plan.handoff_text_login_fallback == 1 &&
                           handoff_plan.action_allowed == 0 &&
                           handoff_plan.action_blocked == 1,
                       "credential handoff plan unknown should force text login fallback");
  fails += expect_true(strings_equal(handoff_plan.handoff_envelope,
                                     "text-login-fallback-handoff-envelope") &&
                           strings_equal(handoff_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential handoff plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_handoff_plan_fails_closed_for_unsafe_or_missing_commit_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_handoff_plan handoff_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_handoff_plan_build(
                           NULL, &handoff_plan) == 0,
                       "credential handoff plan missing commit plan should build fail-closed state");
  fails += expect_true(handoff_plan.commit_plan_available == 0 &&
                           handoff_plan.commit_plan_safe == 0 &&
                           handoff_plan.handoff_plan_safe == 0 &&
                           handoff_plan.route_selected == 0 &&
                           handoff_plan.route_blocked == 1,
                       "credential handoff plan missing commit plan should block handoff plan");
  fails += expect_true(handoff_plan.window_handoff_allowed == 0 &&
                           handoff_plan.window_handoff_delivered == 0 &&
                           handoff_plan.handoff_text_login == 1 &&
                           handoff_plan.handoff_text_login_fallback == 1 &&
                           handoff_plan.submit_callback_bound == 0 &&
                           handoff_plan.auth_callback_bound == 0 &&
                           handoff_plan.submit_enabled == 0 &&
                           handoff_plan.auth_attempt_allowed == 0,
                       "credential handoff plan missing commit plan must stay redacted");
  fails += expect_true(strings_equal(handoff_plan.handoff_envelope,
                                     "text-login-fallback-handoff-envelope") &&
                           strings_equal(handoff_plan.event_type,
                                         "credential-screen-handoff-plan-unavailable") &&
                           strings_equal(handoff_plan.blocked_reason,
                                         "commit-plan-unavailable"),
                       "credential handoff plan missing commit plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_handoff_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &handoff_plan) == 0,
                       "credential handoff plan unsafe commit plan should build blocked state");
  fails += expect_true(handoff_plan.commit_plan_available == 1 &&
                           handoff_plan.commit_plan_safe == 0 &&
                           handoff_plan.handoff_plan_safe == 0 &&
                           handoff_plan.route_selected == 0 &&
                           handoff_plan.route_blocked == 1,
                       "credential handoff plan unsafe commit plan should block handoff plan");
  fails += expect_true(handoff_plan.action_allowed == 0 &&
                           handoff_plan.action_blocked == 1 &&
                           handoff_plan.input_focus_allowed == 0 &&
                           handoff_plan.handoff_credential_focus == 0 &&
                           handoff_plan.handoff_text_login_fallback == 1,
                       "credential handoff plan unsafe commit plan must force text login fallback");
  fails += expect_true(strings_equal(handoff_plan.handoff_envelope,
                                     "text-login-fallback-handoff-envelope") &&
                           strings_equal(handoff_plan.event_type,
                                         "credential-screen-handoff-plan-unsafe") &&
                           strings_equal(handoff_plan.blocked_reason,
                                         "credential-handoff-plan-unsafe"),
                       "credential handoff plan unsafe commit plan should force text login");
  return fails;
}


int build_loginwindow_credential_screen_dispatch_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_dispatch_plan *dispatch_plan) {
  struct login_window_credential_screen_handoff_plan handoff_plan;

  if (build_loginwindow_credential_screen_handoff_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &handoff_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_dispatch_plan_build(&handoff_plan, dispatch_plan);
}

static int test_loginwindow_credential_screen_dispatch_plan_dispatches_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_dispatch_plan dispatch_plan;

  fails += expect_true(build_loginwindow_credential_screen_dispatch_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'e', 0, 0, 0,
                           1, &dispatch_plan) == 0,
                       "credential dispatch plan edit should build");
  fails += expect_true(dispatch_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_DISPATCH_PLAN_VERSION,
                       "credential dispatch plan should expose stable version");
  fails += expect_true(dispatch_plan.handoff_plan_available == 1 &&
                           dispatch_plan.handoff_plan_safe == 1 &&
                           dispatch_plan.dispatch_plan_safe == 1,
                       "credential dispatch plan edit should require safe handoff plan");
  fails += expect_true(dispatch_plan.window_dispatch_allowed == 1 &&
                           dispatch_plan.window_dispatch_delivered == 0 &&
                           dispatch_plan.dispatch_ticket_selected == 1,
                       "credential dispatch plan edit should remain declarative");
  fails += expect_true(dispatch_plan.dispatch_credential_panel == 1 &&
                           dispatch_plan.dispatch_credential_input == 1 &&
                           dispatch_plan.dispatch_credential_focus == 1,
                       "credential dispatch plan edit should dispatch credential widgets");
  fails += expect_true(dispatch_plan.submit_callback_bound == 0 &&
                           dispatch_plan.auth_callback_bound == 0 &&
                           dispatch_plan.submit_enabled == 0 &&
                           dispatch_plan.auth_attempt_allowed == 0,
                       "credential dispatch plan edit must not bind auth callbacks");
  fails += expect_true(dispatch_plan.raw_secret_exposed == 0 &&
                           dispatch_plan.masked_text_exposed == 0 &&
                           dispatch_plan.length_redacted == 1,
                       "credential dispatch plan edit must stay redacted");
  fails += expect_true(strings_equal(dispatch_plan.dispatch_ticket,
                                     "credential-screen-dispatch-ticket") &&
                           strings_equal(dispatch_plan.focus_target,
                                         "credential-input") &&
                           strings_equal(dispatch_plan.state,
                                         "dispatch-credential-ready"),
                       "credential dispatch plan edit should report dispatch state");
  return fails;
}

static int test_loginwindow_credential_screen_dispatch_plan_dispatches_text_recovery(void) {
  int fails = 0;
  struct login_window_credential_screen_dispatch_plan dispatch_plan;

  fails += expect_true(build_loginwindow_credential_screen_dispatch_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &dispatch_plan) == 0,
                       "credential dispatch plan recovery should build");
  fails += expect_true(dispatch_plan.dispatch_plan_safe == 1 &&
                           dispatch_plan.window_dispatch_allowed == 1 &&
                           dispatch_plan.window_dispatch_delivered == 0 &&
                           dispatch_plan.dispatch_text_recovery == 1 &&
                           dispatch_plan.dispatch_text_login == 1,
                       "credential dispatch plan recovery should dispatch text recovery");
  fails += expect_true(dispatch_plan.dispatch_credential_input == 0 &&
                           dispatch_plan.dispatch_credential_focus == 0 &&
                           dispatch_plan.input_focus_allowed == 0,
                       "credential dispatch plan recovery should block credential focus");
  fails += expect_true(dispatch_plan.submit_callback_bound == 0 &&
                           dispatch_plan.auth_callback_bound == 0 &&
                           dispatch_plan.submit_enabled == 0 &&
                           dispatch_plan.auth_attempt_allowed == 0,
                       "credential dispatch plan recovery must keep auth disabled");
  fails += expect_true(strings_equal(dispatch_plan.dispatch_ticket,
                                     "text-recovery-dispatch-ticket") &&
                           strings_equal(dispatch_plan.primary_action,
                                         "open-text-recovery") &&
                           strings_equal(dispatch_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential dispatch plan recovery should report recovery dispatch");
  return fails;
}

static int test_loginwindow_credential_screen_dispatch_plan_dispatches_resume_text_login(void) {
  int fails = 0;
  struct login_window_credential_screen_dispatch_plan dispatch_plan;

  fails += expect_true(build_loginwindow_credential_screen_dispatch_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &dispatch_plan) == 0,
                       "credential dispatch plan resume should build");
  fails += expect_true(dispatch_plan.dispatch_plan_safe == 1 &&
                           dispatch_plan.dispatch_text_login == 1 &&
                           dispatch_plan.dispatch_text_login_resume == 1,
                       "credential dispatch plan resume should dispatch text login resume");
  fails += expect_true(dispatch_plan.session_reset_required == 1 &&
                           dispatch_plan.login_screen_rerender_required == 1 &&
                           dispatch_plan.dispatch_credential_focus == 0,
                       "credential dispatch plan resume should require reset and rerender");
  fails += expect_true(dispatch_plan.submit_callback_bound == 0 &&
                           dispatch_plan.auth_callback_bound == 0 &&
                           dispatch_plan.submit_enabled == 0 &&
                           dispatch_plan.auth_attempt_allowed == 0,
                       "credential dispatch plan resume must keep GUI auth disabled");
  fails += expect_true(strings_equal(dispatch_plan.dispatch_ticket,
                                     "text-login-resume-dispatch-ticket") &&
                           strings_equal(dispatch_plan.primary_action,
                                         "resume-text-login") &&
                           strings_equal(dispatch_plan.state,
                                         "dispatch-resume-ready"),
                       "credential dispatch plan resume should report resume dispatch");
  return fails;
}

static int test_loginwindow_credential_screen_dispatch_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_dispatch_plan dispatch_plan;

  fails += expect_true(build_loginwindow_credential_screen_dispatch_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &dispatch_plan) == 0,
                       "credential dispatch plan submit should build");
  fails += expect_true(dispatch_plan.dispatch_plan_safe == 1 &&
                           dispatch_plan.submit_requested == 1 &&
                           dispatch_plan.dispatch_text_login_fallback == 1,
                       "credential dispatch plan submit should dispatch text login fallback");
  fails += expect_true(dispatch_plan.action_allowed == 0 &&
                           dispatch_plan.action_blocked == 1 &&
                           dispatch_plan.input_focus_allowed == 0 &&
                           dispatch_plan.dispatch_credential_focus == 0,
                       "credential dispatch plan submit should block GUI action");
  fails += expect_true(dispatch_plan.window_dispatch_allowed == 1 &&
                           dispatch_plan.window_dispatch_delivered == 0 &&
                           dispatch_plan.submit_callback_bound == 0 &&
                           dispatch_plan.auth_callback_bound == 0 &&
                           dispatch_plan.submit_enabled == 0 &&
                           dispatch_plan.auth_attempt_allowed == 0 &&
                           dispatch_plan.raw_secret_exposed == 0 &&
                           dispatch_plan.masked_text_exposed == 0,
                       "credential dispatch plan submit must stay declarative and redacted");
  fails += expect_true(strings_equal(dispatch_plan.dispatch_ticket,
                                     "text-login-fallback-dispatch-ticket") &&
                           strings_equal(dispatch_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(dispatch_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential dispatch plan submit should explain fallback dispatch");

  fails += expect_true(build_loginwindow_credential_screen_dispatch_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &dispatch_plan) == 0,
                       "credential dispatch plan unknown should build");
  fails += expect_true(dispatch_plan.dispatch_plan_safe == 1 &&
                           dispatch_plan.dispatch_text_login_fallback == 1 &&
                           dispatch_plan.action_allowed == 0 &&
                           dispatch_plan.action_blocked == 1,
                       "credential dispatch plan unknown should force text login fallback");
  fails += expect_true(strings_equal(dispatch_plan.dispatch_ticket,
                                     "text-login-fallback-dispatch-ticket") &&
                           strings_equal(dispatch_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential dispatch plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_dispatch_plan_fails_closed_for_unsafe_or_missing_handoff_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_dispatch_plan dispatch_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_dispatch_plan_build(
                           NULL, &dispatch_plan) == 0,
                       "credential dispatch plan missing handoff plan should build fail-closed state");
  fails += expect_true(dispatch_plan.handoff_plan_available == 0 &&
                           dispatch_plan.handoff_plan_safe == 0 &&
                           dispatch_plan.dispatch_plan_safe == 0 &&
                           dispatch_plan.route_selected == 0 &&
                           dispatch_plan.route_blocked == 1,
                       "credential dispatch plan missing handoff plan should block dispatch plan");
  fails += expect_true(dispatch_plan.window_dispatch_allowed == 0 &&
                           dispatch_plan.window_dispatch_delivered == 0 &&
                           dispatch_plan.dispatch_text_login == 1 &&
                           dispatch_plan.dispatch_text_login_fallback == 1 &&
                           dispatch_plan.submit_callback_bound == 0 &&
                           dispatch_plan.auth_callback_bound == 0 &&
                           dispatch_plan.submit_enabled == 0 &&
                           dispatch_plan.auth_attempt_allowed == 0,
                       "credential dispatch plan missing handoff plan must stay redacted");
  fails += expect_true(strings_equal(dispatch_plan.dispatch_ticket,
                                     "text-login-fallback-dispatch-ticket") &&
                           strings_equal(dispatch_plan.event_type,
                                         "credential-screen-dispatch-plan-unavailable") &&
                           strings_equal(dispatch_plan.blocked_reason,
                                         "handoff-plan-unavailable"),
                       "credential dispatch plan missing handoff plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_dispatch_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &dispatch_plan) == 0,
                       "credential dispatch plan unsafe handoff plan should build blocked state");
  fails += expect_true(dispatch_plan.handoff_plan_available == 1 &&
                           dispatch_plan.handoff_plan_safe == 0 &&
                           dispatch_plan.dispatch_plan_safe == 0 &&
                           dispatch_plan.route_selected == 0 &&
                           dispatch_plan.route_blocked == 1,
                       "credential dispatch plan unsafe handoff plan should block dispatch plan");
  fails += expect_true(dispatch_plan.action_allowed == 0 &&
                           dispatch_plan.action_blocked == 1 &&
                           dispatch_plan.input_focus_allowed == 0 &&
                           dispatch_plan.dispatch_credential_focus == 0 &&
                           dispatch_plan.dispatch_text_login_fallback == 1,
                       "credential dispatch plan unsafe handoff plan must force text login fallback");
  fails += expect_true(strings_equal(dispatch_plan.dispatch_ticket,
                                     "text-login-fallback-dispatch-ticket") &&
                           strings_equal(dispatch_plan.event_type,
                                         "credential-screen-dispatch-plan-unsafe") &&
                           strings_equal(dispatch_plan.blocked_reason,
                                         "credential-dispatch-plan-unsafe"),
                       "credential dispatch plan unsafe handoff plan should force text login");
  return fails;
}

int test_login_runtime_credential_handoff_dispatch_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_handoff_plan_hands_off_credential_widgets();
  fails += test_loginwindow_credential_screen_handoff_plan_hands_off_text_recovery();
  fails += test_loginwindow_credential_screen_handoff_plan_hands_off_resume_text_login();
  fails += test_loginwindow_credential_screen_handoff_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_handoff_plan_fails_closed_for_unsafe_or_missing_commit_plan();
  fails += test_loginwindow_credential_screen_dispatch_plan_dispatches_credential_widgets();
  fails += test_loginwindow_credential_screen_dispatch_plan_dispatches_text_recovery();
  fails += test_loginwindow_credential_screen_dispatch_plan_dispatches_resume_text_login();
  fails += test_loginwindow_credential_screen_dispatch_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_dispatch_plan_fails_closed_for_unsafe_or_missing_handoff_plan();
  return fails;
}
