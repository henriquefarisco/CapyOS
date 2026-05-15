#include <stdio.h>
#include <string.h>

#include "auth/login_runtime.h"

static int expect_true(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "[loginwindow_session_handoff] %s\n", message);
    return 1;
  }
  return 0;
}

static int strings_equal(const char *a, const char *b) {
  if (!a || !b) return 0;
  return strcmp(a, b) == 0;
}

static void safe_auth_submit_success(
    struct login_window_credential_auth_submit *submit) {
  memset(submit, 0, sizeof(*submit));
  submit->version = LOGIN_WINDOW_CREDENTIAL_AUTH_SUBMIT_VERSION;
  submit->policy_available = 1;
  submit->buffer_available = 1;
  submit->username_available = 1;
  submit->authenticate_available = 1;
  submit->gate_evaluated = 1;
  submit->attempted = 1;
  submit->buffer_had_secret = 1;
  submit->auth_called = 1;
  submit->authenticated = 1;
  submit->user_record_available = 1;
  submit->submit_allowed = 1;
  submit->auth_attempt_allowed = 1;
  submit->text_login_authoritative = 1;
  submit->wipe_required = 1;
  submit->wipe_attempted = 1;
  submit->wipe_succeeded = 1;
  submit->credential_redacted = 1;
  submit->length_redacted = 1;
  submit->state = "authenticated";
  submit->message = "Graphical credential authenticated.";
  submit->blocked_reason = "ready";
}

static void safe_recovery_decision_success(
    struct login_window_credential_recovery_decision *decision) {
  memset(decision, 0, sizeof(*decision));
  decision->version = LOGIN_WINDOW_CREDENTIAL_RECOVERY_DECISION_VERSION;
  decision->controller_available = 1;
  decision->auth_submit_available = 1;
  decision->controller_safe = 1;
  decision->auth_submit_safe = 1;
  decision->decision_safe = 1;
  decision->requested_action = LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL;
  decision->route_selected = 1;
  decision->route_blocked = 0;
  decision->stay_on_credential_screen = 1;
  decision->credential_session_safe = 1;
  decision->credential_storage_wiped = 1;
  decision->credential_redacted = 1;
  decision->length_redacted = 1;
  decision->submit_requested = 1;
  decision->submit_blocked = 1;
  decision->text_login_authoritative = 1;
  decision->auth_called = 1;
  decision->authenticated = 1;
  decision->user_record_available = 1;
  decision->audit_required = 1;
  decision->audit_redacted = 1;
  decision->route = "stay-on-credential-screen";
  decision->result_action = "submit-credential";
  decision->event_type = "credential-recovery-decision";
  decision->state = "credential-screen-ready";
  decision->message = "Stay on credential screen.";
  decision->blocked_reason = "ready";
}

static void safe_user(struct user_record *user) {
  memset(user, 0, sizeof(*user));
  strcpy(user->username, "admin");
  strcpy(user->role, "admin");
  strcpy(user->home, "/home/admin");
  user->uid = 1000;
  user->gid = 1000;
}

static int test_session_handoff_allows_authenticated_desktop(void) {
  int fails = 0;
  struct login_window_credential_auth_submit submit;
  struct login_window_credential_recovery_decision decision;
  struct login_window_credential_session_handoff handoff;
  struct user_record user;

  safe_auth_submit_success(&submit);
  safe_recovery_decision_success(&decision);
  safe_user(&user);
  fails += expect_true(login_window_credential_session_handoff_build(
                           &submit, &decision, &user, &handoff) == 0,
                       "authenticated handoff should build");
  fails += expect_true(handoff.version ==
                           LOGIN_WINDOW_CREDENTIAL_SESSION_HANDOFF_VERSION,
                       "authenticated handoff should expose version");
  fails += expect_true(handoff.handoff_safe == 1 &&
                           handoff.auth_submit_safe == 1 &&
                           handoff.recovery_decision_safe == 1 &&
                           handoff.user_record_safe == 1,
                       "authenticated handoff should require safe inputs");
  fails += expect_true(handoff.session_begin_allowed == 1 &&
                           handoff.session_activate_allowed == 1 &&
                           handoff.shell_context_init_required == 1,
                       "authenticated handoff should allow session activation");
  fails += expect_true(handoff.graphical_session_allowed == 1 &&
                           handoff.desktop_autostart_required == 1 &&
                           handoff.logout_allowed == 1,
                       "authenticated handoff should request desktop autostart and logout path");
  fails += expect_true(handoff.fallback_text_login_allowed == 0 &&
                           handoff.audit_redacted == 1 &&
                           handoff.raw_secret_exposed == 0,
                       "authenticated handoff should leave fallback inactive and audit redacted");
  fails += expect_true(strings_equal(handoff.route, "graphical-session") &&
                           strings_equal(handoff.session_target,
                                         "desktop-session") &&
                           strings_equal(handoff.blocked_reason, "ready"),
                       "authenticated handoff should report graphical session target");
  return fails;
}

static int test_session_handoff_blocks_failure_and_lockout(void) {
  int fails = 0;
  struct login_window_credential_auth_submit submit;
  struct login_window_credential_recovery_decision decision;
  struct login_window_credential_session_handoff handoff;
  struct user_record user;

  safe_auth_submit_success(&submit);
  safe_recovery_decision_success(&decision);
  safe_user(&user);
  submit.authenticated = 0;
  submit.auth_failed = 1;
  submit.user_record_available = 0;
  decision.authenticated = 0;
  decision.auth_failed = 1;
  decision.user_record_available = 0;
  fails += expect_true(login_window_credential_session_handoff_build(
                           &submit, &decision, &user, &handoff) == 0,
                       "failed handoff should build");
  fails += expect_true(handoff.handoff_safe == 0 &&
                           handoff.graphical_session_allowed == 0 &&
                           handoff.fallback_text_login_allowed == 1,
                       "failed handoff should force text fallback");
  fails += expect_true(strings_equal(handoff.state, "failed") &&
                           strings_equal(handoff.blocked_reason, "auth-failed"),
                       "failed handoff should preserve failure reason");

  safe_auth_submit_success(&submit);
  safe_recovery_decision_success(&decision);
  submit.authenticated = 0;
  submit.auth_locked = 1;
  submit.user_record_available = 0;
  decision.authenticated = 0;
  decision.auth_locked = 1;
  decision.user_record_available = 0;
  decision.lockout_bypass_blocked = 1;
  fails += expect_true(login_window_credential_session_handoff_build(
                           &submit, &decision, &user, &handoff) == 0,
                       "locked handoff should build");
  fails += expect_true(handoff.handoff_safe == 0 &&
                           handoff.lockout_blocked == 1 &&
                           handoff.graphical_session_allowed == 0,
                       "locked handoff should block graphical session");
  fails += expect_true(strings_equal(handoff.state, "locked") &&
                           strings_equal(handoff.blocked_reason, "auth-locked"),
                       "locked handoff should preserve lockout reason");
  return fails;
}

static int test_session_handoff_blocks_recovery_routes(void) {
  int fails = 0;
  struct login_window_credential_auth_submit submit;
  struct login_window_credential_recovery_decision decision;
  struct login_window_credential_session_handoff handoff;
  struct user_record user;

  safe_auth_submit_success(&submit);
  safe_recovery_decision_success(&decision);
  safe_user(&user);
  decision.stay_on_credential_screen = 0;
  decision.open_text_recovery_route = 1;
  decision.recovery_allowed = 1;
  decision.route = "open-text-recovery";
  fails += expect_true(login_window_credential_session_handoff_build(
                           &submit, &decision, &user, &handoff) == 0,
                       "recovery route handoff should build");
  fails += expect_true(handoff.handoff_safe == 0 &&
                           handoff.recovery_route_active == 1 &&
                           handoff.graphical_session_allowed == 0,
                       "recovery route handoff should block graphical session");
  fails += expect_true(strings_equal(handoff.blocked_reason,
                                     "recovery-route-active"),
                       "recovery route handoff should explain active recovery route");

  safe_auth_submit_success(&submit);
  safe_recovery_decision_success(&decision);
  decision.authenticated_recovery_blocked = 1;
  decision.force_text_login_required = 1;
  fails += expect_true(login_window_credential_session_handoff_build(
                           &submit, &decision, &user, &handoff) == 0,
                       "authenticated recovery block handoff should build");
  fails += expect_true(handoff.handoff_safe == 0 &&
                           handoff.authenticated_recovery_blocked == 1 &&
                           handoff.graphical_session_allowed == 0,
                       "authenticated recovery block should prevent handoff");
  fails += expect_true(strings_equal(handoff.blocked_reason,
                                     "authenticated-recovery-blocked"),
                       "authenticated recovery block should explain reason");
  return fails;
}

static int test_session_handoff_fails_closed_for_unsafe_inputs(void) {
  int fails = 0;
  struct login_window_credential_auth_submit submit;
  struct login_window_credential_recovery_decision decision;
  struct login_window_credential_session_handoff handoff;
  struct user_record user;

  safe_auth_submit_success(&submit);
  safe_recovery_decision_success(&decision);
  safe_user(&user);
  submit.wipe_succeeded = 0;
  fails += expect_true(login_window_credential_session_handoff_build(
                           &submit, &decision, &user, &handoff) == 0,
                       "unsafe auth handoff should build fail-closed");
  fails += expect_true(handoff.handoff_safe == 0 &&
                           handoff.auth_submit_safe == 0 &&
                           handoff.graphical_session_allowed == 0,
                       "unsafe auth handoff should block graphical session");
  fails += expect_true(strings_equal(handoff.blocked_reason,
                                     "auth-submit-unsafe"),
                       "unsafe auth handoff should explain block");

  safe_auth_submit_success(&submit);
  safe_recovery_decision_success(&decision);
  decision.raw_secret_exposed = 1;
  fails += expect_true(login_window_credential_session_handoff_build(
                           &submit, &decision, &user, &handoff) == 0,
                       "unsafe recovery decision handoff should build fail-closed");
  fails += expect_true(handoff.handoff_safe == 0 &&
                           handoff.recovery_decision_safe == 0 &&
                           handoff.audit_redacted == 0,
                       "unsafe recovery decision should block graphical session");
  fails += expect_true(strings_equal(handoff.blocked_reason,
                                     "recovery-decision-unsafe"),
                       "unsafe recovery decision should explain block");

  safe_auth_submit_success(&submit);
  safe_recovery_decision_success(&decision);
  submit.text_login_authoritative = 0;
  fails += expect_true(login_window_credential_session_handoff_build(
                           &submit, &decision, &user, &handoff) == 0,
                       "non-authoritative text login handoff should build fail-closed");
  fails += expect_true(handoff.handoff_safe == 0 &&
                           handoff.auth_submit_safe == 1 &&
                           handoff.recovery_decision_safe == 1 &&
                           handoff.text_login_authoritative == 0,
                       "non-authoritative text login should fail after hygiene checks");
  fails += expect_true(strings_equal(handoff.blocked_reason,
                                     "text-login-not-authoritative"),
                       "non-authoritative text login should explain block");
  return fails;
}

static int test_session_handoff_blocks_missing_or_recovery_user(void) {
  int fails = 0;
  struct login_window_credential_auth_submit submit;
  struct login_window_credential_recovery_decision decision;
  struct login_window_credential_session_handoff handoff;
  struct user_record user;

  safe_auth_submit_success(&submit);
  safe_recovery_decision_success(&decision);
  safe_user(&user);
  user.username[0] = '\0';
  fails += expect_true(login_window_credential_session_handoff_build(
                           &submit, &decision, &user, &handoff) == 0,
                       "missing user handoff should build fail-closed");
  fails += expect_true(handoff.handoff_safe == 0 &&
                           handoff.user_record_safe == 0 &&
                           handoff.graphical_session_allowed == 0,
                       "missing user should block graphical session");
  fails += expect_true(strings_equal(handoff.blocked_reason,
                                     "user-record-unavailable"),
                       "missing user should explain block");

  safe_user(&user);
  strcpy(user.role, "recovery");
  fails += expect_true(login_window_credential_session_handoff_build(
                           &submit, &decision, &user, &handoff) == 0,
                       "recovery user handoff should build fail-closed");
  fails += expect_true(handoff.handoff_safe == 0 &&
                           handoff.user_record_safe == 1 &&
                           handoff.desktop_user_eligible == 0,
                       "recovery user should be authenticated but desktop-ineligible");
  fails += expect_true(strings_equal(handoff.blocked_reason,
                                     "graphical-user-ineligible"),
                       "recovery user should explain desktop ineligibility");
  return fails;
}

int run_loginwindow_session_handoff_tests(void) {
  int fails = 0;
  fails += test_session_handoff_allows_authenticated_desktop();
  fails += test_session_handoff_blocks_failure_and_lockout();
  fails += test_session_handoff_blocks_recovery_routes();
  fails += test_session_handoff_fails_closed_for_unsafe_inputs();
  fails += test_session_handoff_blocks_missing_or_recovery_user();
  if (fails == 0) {
    printf("[tests] loginwindow_session_handoff: ok\n");
  }
  return fails;
}
