#include <stdio.h>
#include <string.h>

#include "auth/login_runtime.h"

static int expect_true(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "[loginwindow_recovery_decision] %s\n", message);
    return 1;
  }
  return 0;
}

static int strings_equal(const char *a, const char *b) {
  if (!a || !b) return 0;
  return strcmp(a, b) == 0;
}

static void safe_controller(
    struct login_window_credential_screen_controller *controller) {
  memset(controller, 0, sizeof(*controller));
  controller->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_CONTROLLER_VERSION;
  controller->route_plan_available = 1;
  controller->route_plan_safe = 1;
  controller->controller_safe = 1;
  controller->requested_action = LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL;
  controller->route_selected = 1;
  controller->route_blocked = 0;
  controller->action_allowed = 1;
  controller->action_blocked = 0;
  controller->input_focus_allowed = 1;
  controller->credential_screen_visible = 1;
  controller->credential_input_focus = 1;
  controller->text_login_forced = 0;
  controller->recovery_text_session_required = 1;
  controller->session_reset_required = 1;
  controller->login_screen_rerender_required = 1;
  controller->credential_session_safe = 1;
  controller->credential_storage_wiped = 1;
  controller->credential_redacted = 1;
  controller->length_redacted = 1;
  controller->submit_blocked = 1;
  controller->text_login_authoritative = 1;
  controller->route = "stay-on-credential-screen";
  controller->event_type = "credential-screen-edit-focus";
  controller->result_action = "edit-credential";
  controller->state = "controller-credential-ready";
  controller->message = "Credential screen may keep focus.";
  controller->blocked_reason = "ready";
}

static void safe_auth_submit(
    struct login_window_credential_auth_submit *submit) {
  memset(submit, 0, sizeof(*submit));
  submit->version = LOGIN_WINDOW_CREDENTIAL_AUTH_SUBMIT_VERSION;
  submit->attempted = 1;
  submit->gate_evaluated = 1;
  submit->text_login_authoritative = 1;
  submit->wipe_required = 1;
  submit->wipe_attempted = 1;
  submit->wipe_succeeded = 1;
  submit->credential_redacted = 1;
  submit->length_redacted = 1;
  submit->state = "blocked";
  submit->message = "Graphical credential submit unavailable.";
  submit->blocked_reason = "gui-submit-disabled";
}

static int test_recovery_decision_allows_text_recovery_only(void) {
  int fails = 0;
  struct login_window_credential_screen_controller controller;
  struct login_window_credential_auth_submit submit;
  struct login_window_credential_recovery_decision decision;

  safe_controller(&controller);
  safe_auth_submit(&submit);
  controller.requested_action = LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY;
  controller.credential_screen_visible = 0;
  controller.credential_input_focus = 0;
  controller.text_recovery_open = 1;
  controller.text_login_forced = 1;
  controller.route = "open-text-recovery";
  controller.result_action = "open-text-recovery";
  controller.event_type = "credential-screen-open-text-recovery";
  controller.state = "controller-text-recovery-ready";
  controller.blocked_reason = "text-recovery-only";

  fails += expect_true(login_window_credential_recovery_decision_build(
                           &controller, &submit, &decision) == 0,
                       "text recovery decision should build");
  fails += expect_true(decision.version ==
                           LOGIN_WINDOW_CREDENTIAL_RECOVERY_DECISION_VERSION,
                       "text recovery decision should expose version");
  fails += expect_true(decision.decision_safe == 1 &&
                           decision.recovery_allowed == 1 &&
                           decision.open_text_recovery_route == 1,
                       "text recovery decision should allow recovery route");
  fails += expect_true(decision.recovery_text_session_required == 1 &&
                           decision.force_text_login_required == 1 &&
                           decision.text_login_allowed == 1,
                       "text recovery decision should require text login route");
  fails += expect_true(decision.submit_blocked == 1 &&
                           decision.submit_enabled == 0 &&
                           decision.auth_attempt_allowed == 0,
                       "text recovery decision must not enable GUI auth");
  fails += expect_true(decision.credential_redacted == 1 &&
                           decision.length_redacted == 1 &&
                           decision.raw_secret_exposed == 0 &&
                           decision.masked_text_exposed == 0 &&
                           decision.audit_redacted == 1,
                       "text recovery decision should keep audit redacted");
  fails += expect_true(strings_equal(decision.state, "recovery-ready") &&
                           strings_equal(decision.blocked_reason,
                                         "text-recovery-only"),
                       "text recovery decision should report recovery state");
  return fails;
}

static int test_recovery_decision_allows_resume_with_reset(void) {
  int fails = 0;
  struct login_window_credential_screen_controller controller;
  struct login_window_credential_auth_submit submit;
  struct login_window_credential_recovery_decision decision;

  safe_controller(&controller);
  safe_auth_submit(&submit);
  controller.requested_action = LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN;
  controller.credential_screen_visible = 0;
  controller.credential_input_focus = 0;
  controller.text_login_resume = 1;
  controller.text_login_forced = 1;
  controller.route = "resume-text-login";
  controller.result_action = "resume-text-login";
  controller.event_type = "credential-screen-resume-text-login";
  controller.state = "controller-resume-ready";
  controller.blocked_reason = "ready";

  fails += expect_true(login_window_credential_recovery_decision_build(
                           &controller, &submit, &decision) == 0,
                       "resume decision should build");
  fails += expect_true(decision.decision_safe == 1 &&
                           decision.resume_allowed == 1 &&
                           decision.resume_text_login_route == 1,
                       "resume decision should allow resume route");
  fails += expect_true(decision.session_reset_required == 1 &&
                           decision.login_screen_rerender_required == 1,
                       "resume decision should require reset and rerender");
  fails += expect_true(decision.credential_input_focus_allowed == 0 &&
                           decision.submit_enabled == 0 &&
                           decision.auth_attempt_allowed == 0,
                       "resume decision must not keep credential focus or auth");
  fails += expect_true(strings_equal(decision.state, "resume-ready") &&
                           strings_equal(decision.blocked_reason, "ready"),
                       "resume decision should report ready state");
  return fails;
}

static int test_recovery_decision_blocks_lockout_bypass(void) {
  int fails = 0;
  struct login_window_credential_screen_controller controller;
  struct login_window_credential_auth_submit submit;
  struct login_window_credential_recovery_decision decision;

  safe_controller(&controller);
  safe_auth_submit(&submit);
  controller.requested_action = LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY;
  controller.credential_screen_visible = 0;
  controller.credential_input_focus = 0;
  controller.text_recovery_open = 1;
  controller.text_login_forced = 1;
  controller.route = "open-text-recovery";
  controller.result_action = "open-text-recovery";
  submit.auth_called = 1;
  submit.auth_locked = 1;
  submit.state = "locked";
  submit.blocked_reason = "auth-locked";

  fails += expect_true(login_window_credential_recovery_decision_build(
                           &controller, &submit, &decision) == 0,
                       "lockout recovery decision should build");
  fails += expect_true(decision.decision_safe == 1 &&
                           decision.lockout_bypass_blocked == 1 &&
                           decision.auth_locked == 1,
                       "lockout decision should explicitly block bypass");
  fails += expect_true(decision.recovery_allowed == 0 &&
                           decision.resume_allowed == 0 &&
                           decision.open_text_recovery_route == 0,
                       "lockout decision should suppress recovery routes");
  fails += expect_true(decision.force_text_login_required == 1 &&
                           decision.text_login_allowed == 1 &&
                           decision.submit_enabled == 0,
                       "lockout decision should force authoritative text login");
  fails += expect_true(strings_equal(decision.state, "locked") &&
                           strings_equal(decision.blocked_reason,
                                         "auth-locked"),
                       "lockout decision should preserve lockout status");
  return fails;
}

static int test_recovery_decision_blocks_authenticated_recovery(void) {
  int fails = 0;
  struct login_window_credential_screen_controller controller;
  struct login_window_credential_auth_submit submit;
  struct login_window_credential_recovery_decision decision;

  safe_controller(&controller);
  safe_auth_submit(&submit);
  controller.requested_action = LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN;
  controller.credential_screen_visible = 0;
  controller.credential_input_focus = 0;
  controller.text_login_resume = 1;
  controller.text_login_forced = 1;
  controller.route = "resume-text-login";
  controller.result_action = "resume-text-login";
  submit.auth_called = 1;
  submit.authenticated = 1;
  submit.user_record_available = 1;
  submit.state = "authenticated";
  submit.blocked_reason = "ready";

  fails += expect_true(login_window_credential_recovery_decision_build(
                           &controller, &submit, &decision) == 0,
                       "authenticated recovery decision should build");
  fails += expect_true(decision.decision_safe == 1 &&
                           decision.authenticated_recovery_blocked == 1 &&
                           decision.authenticated == 1,
                       "authenticated recovery decision should block recovery route");
  fails += expect_true(decision.recovery_allowed == 0 &&
                           decision.resume_allowed == 0 &&
                           decision.force_text_login_required == 1,
                       "authenticated recovery decision should force text fallback");
  fails += expect_true(decision.user_record_available == 1 &&
                           decision.submit_enabled == 0 &&
                           decision.auth_attempt_allowed == 0,
                       "authenticated recovery decision should not start new auth");
  fails += expect_true(strings_equal(decision.blocked_reason,
                                     "authenticated-recovery-blocked"),
                       "authenticated recovery decision should explain block");
  return fails;
}

static int test_recovery_decision_fails_closed_for_unsafe_inputs(void) {
  int fails = 0;
  struct login_window_credential_screen_controller controller;
  struct login_window_credential_auth_submit submit;
  struct login_window_credential_recovery_decision decision;

  fails += expect_true(login_window_credential_recovery_decision_build(
                           NULL, NULL, &decision) == 0,
                       "missing controller decision should build fail-closed");
  fails += expect_true(decision.decision_safe == 0 &&
                           decision.route_blocked == 1 &&
                           decision.force_text_login_required == 1 &&
                           decision.audit_redacted == 1,
                       "missing controller decision should force redacted text login");
  fails += expect_true(strings_equal(decision.blocked_reason,
                                     "controller-unavailable"),
                       "missing controller decision should explain block");

  safe_controller(&controller);
  controller.raw_secret_exposed = 1;
  fails += expect_true(login_window_credential_recovery_decision_build(
                           &controller, NULL, &decision) == 0,
                       "unsafe controller decision should build fail-closed");
  fails += expect_true(decision.decision_safe == 0 &&
                           decision.controller_safe == 0 &&
                           decision.raw_secret_exposed == 1 &&
                           decision.recovery_allowed == 0 &&
                           decision.resume_allowed == 0,
                       "unsafe controller decision should block routes");
  fails += expect_true(strings_equal(decision.blocked_reason,
                                     "controller-unsafe"),
                       "unsafe controller decision should explain block");

  safe_controller(&controller);
  safe_auth_submit(&submit);
  submit.wipe_succeeded = 0;
  fails += expect_true(login_window_credential_recovery_decision_build(
                           &controller, &submit, &decision) == 0,
                       "unsafe auth decision should build fail-closed");
  fails += expect_true(decision.decision_safe == 0 &&
                           decision.auth_submit_safe == 0 &&
                           decision.recovery_allowed == 0 &&
                           decision.resume_allowed == 0 &&
                           decision.auth_attempt_allowed == 0,
                       "unsafe auth decision should block routes and auth");
  fails += expect_true(strings_equal(decision.blocked_reason,
                                     "auth-submit-unsafe"),
                       "unsafe auth decision should explain block");
  return fails;
}

static int test_recovery_decision_blocks_non_authoritative_text_login(void) {
  int fails = 0;
  struct login_window_credential_screen_controller controller;
  struct login_window_credential_auth_submit submit;
  struct login_window_credential_recovery_decision decision;

  safe_controller(&controller);
  safe_auth_submit(&submit);
  controller.text_login_authoritative = 0;
  fails += expect_true(login_window_credential_recovery_decision_build(
                           &controller, &submit, &decision) == 0,
                       "non-authoritative text login decision should build");
  fails += expect_true(decision.decision_safe == 0 &&
                           decision.controller_safe == 1 &&
                           decision.auth_submit_safe == 1,
                       "non-authoritative text login should fail after hygiene checks");
  fails += expect_true(decision.route_blocked == 1 &&
                           decision.text_login_allowed == 0 &&
                           decision.recovery_allowed == 0 &&
                           decision.resume_allowed == 0,
                       "non-authoritative text login should block all routes");
  fails += expect_true(strings_equal(decision.blocked_reason,
                                     "text-login-not-authoritative"),
                       "non-authoritative text login should explain block");

  safe_controller(&controller);
  safe_auth_submit(&submit);
  submit.text_login_authoritative = 0;
  fails += expect_true(login_window_credential_recovery_decision_build(
                           &controller, &submit, &decision) == 0,
                       "non-authoritative auth submit decision should build");
  fails += expect_true(decision.decision_safe == 0 &&
                           decision.text_login_authoritative == 0,
                       "non-authoritative auth submit should block decision");
  fails += expect_true(strings_equal(decision.blocked_reason,
                                     "text-login-not-authoritative"),
                       "non-authoritative auth submit should explain block");
  return fails;
}

static int test_recovery_decision_blocks_resume_without_reset_or_rerender(void) {
  int fails = 0;
  struct login_window_credential_screen_controller controller;
  struct login_window_credential_auth_submit submit;
  struct login_window_credential_recovery_decision decision;

  safe_controller(&controller);
  safe_auth_submit(&submit);
  controller.requested_action = LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN;
  controller.credential_screen_visible = 0;
  controller.credential_input_focus = 0;
  controller.text_login_resume = 1;
  controller.text_login_forced = 1;
  controller.session_reset_required = 0;
  controller.login_screen_rerender_required = 1;
  controller.route = "resume-text-login";
  controller.result_action = "resume-text-login";
  fails += expect_true(login_window_credential_recovery_decision_build(
                           &controller, &submit, &decision) == 0,
                       "resume without reset decision should build");
  fails += expect_true(decision.decision_safe == 0 &&
                           decision.resume_allowed == 0 &&
                           decision.resume_text_login_route == 0,
                       "resume without reset should block route");
  fails += expect_true(decision.force_text_login_required == 1 &&
                           decision.submit_enabled == 0 &&
                           decision.auth_attempt_allowed == 0,
                       "resume without reset should force safe fallback");
  fails += expect_true(strings_equal(decision.blocked_reason,
                                     "resume-reset-rerender-required"),
                       "resume without reset should explain block");
  return fails;
}

int run_loginwindow_recovery_decision_tests(void) {
  int fails = 0;
  fails += test_recovery_decision_allows_text_recovery_only();
  fails += test_recovery_decision_allows_resume_with_reset();
  fails += test_recovery_decision_blocks_lockout_bypass();
  fails += test_recovery_decision_blocks_authenticated_recovery();
  fails += test_recovery_decision_fails_closed_for_unsafe_inputs();
  fails += test_recovery_decision_blocks_non_authoritative_text_login();
  fails += test_recovery_decision_blocks_resume_without_reset_or_rerender();
  if (fails == 0) {
    printf("[tests] loginwindow_recovery_decision: ok\n");
  }
  return fails;
}
