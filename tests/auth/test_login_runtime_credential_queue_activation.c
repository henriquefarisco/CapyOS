/*
 * tests/auth/test_login_runtime_credential_queue_activation.c
 *
 * Credential screen queue plan + activation plan coverage for the
 * `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-15 monolith
 * refactor (PR D.12 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_queue_plan_build`: 5 tests
 *     covering the credential widgets queue + the text-recovery
 *     queue + the resume-text-login queue + the submit/unknown
 *     fallback queue + the missing-or-unsafe dispatch plan
 *     fail-closed default.
 *   - `login_window_credential_screen_activation_plan_build`: 5 tests
 *     covering the credential widgets activation + the text-recovery
 *     activation + the resume-text-login activation + the
 *     submit/unknown fallback activation + the missing-or-unsafe
 *     queue plan fail-closed default.
 *
 * Also exposes shared helpers
 * `build_loginwindow_credential_screen_queue_plan_for_action` and
 * `build_loginwindow_credential_screen_activation_plan_for_action`,
 * used by later companion files that chain on top of the
 * queue/activation stages (frame, surface, compositor, damage, ...).
 *
 * The companion entry `test_login_runtime_credential_queue_activation_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_queue_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_queue_plan *queue_plan) {
  struct login_window_credential_screen_dispatch_plan dispatch_plan;

  if (build_loginwindow_credential_screen_dispatch_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &dispatch_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_queue_plan_build(&dispatch_plan, queue_plan);
}

static int test_loginwindow_credential_screen_queue_plan_queues_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_queue_plan queue_plan;

  fails += expect_true(build_loginwindow_credential_screen_queue_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'e', 0, 0, 0,
                           1, &queue_plan) == 0,
                       "credential queue plan edit should build");
  fails += expect_true(queue_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_QUEUE_PLAN_VERSION,
                       "credential queue plan should expose stable version");
  fails += expect_true(queue_plan.dispatch_plan_available == 1 &&
                           queue_plan.dispatch_plan_safe == 1 &&
                           queue_plan.queue_plan_safe == 1,
                       "credential queue plan edit should require safe dispatch plan");
  fails += expect_true(queue_plan.window_queue_allowed == 1 &&
                           queue_plan.window_queue_enqueued == 0 &&
                           queue_plan.queue_ticket_selected == 1,
                       "credential queue plan edit should remain declarative");
  fails += expect_true(queue_plan.queue_credential_panel == 1 &&
                           queue_plan.queue_credential_input == 1 &&
                           queue_plan.queue_credential_focus == 1,
                       "credential queue plan edit should queue credential widgets");
  fails += expect_true(queue_plan.submit_callback_bound == 0 &&
                           queue_plan.auth_callback_bound == 0 &&
                           queue_plan.submit_enabled == 0 &&
                           queue_plan.auth_attempt_allowed == 0,
                       "credential queue plan edit must not bind auth callbacks");
  fails += expect_true(queue_plan.raw_secret_exposed == 0 &&
                           queue_plan.masked_text_exposed == 0 &&
                           queue_plan.length_redacted == 1,
                       "credential queue plan edit must stay redacted");
  fails += expect_true(strings_equal(queue_plan.queue_ticket,
                                     "credential-screen-queue-ticket") &&
                           strings_equal(queue_plan.focus_target,
                                         "credential-input") &&
                           strings_equal(queue_plan.state,
                                         "queue-credential-ready"),
                       "credential queue plan edit should report queue state");
  return fails;
}

static int test_loginwindow_credential_screen_queue_plan_queues_text_recovery(void) {
  int fails = 0;
  struct login_window_credential_screen_queue_plan queue_plan;

  fails += expect_true(build_loginwindow_credential_screen_queue_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &queue_plan) == 0,
                       "credential queue plan recovery should build");
  fails += expect_true(queue_plan.queue_plan_safe == 1 &&
                           queue_plan.window_queue_allowed == 1 &&
                           queue_plan.window_queue_enqueued == 0 &&
                           queue_plan.queue_text_recovery == 1 &&
                           queue_plan.queue_text_login == 1,
                       "credential queue plan recovery should queue text recovery");
  fails += expect_true(queue_plan.queue_credential_input == 0 &&
                           queue_plan.queue_credential_focus == 0 &&
                           queue_plan.input_focus_allowed == 0,
                       "credential queue plan recovery should block credential focus");
  fails += expect_true(queue_plan.submit_callback_bound == 0 &&
                           queue_plan.auth_callback_bound == 0 &&
                           queue_plan.submit_enabled == 0 &&
                           queue_plan.auth_attempt_allowed == 0,
                       "credential queue plan recovery must keep auth disabled");
  fails += expect_true(strings_equal(queue_plan.queue_ticket,
                                     "text-recovery-queue-ticket") &&
                           strings_equal(queue_plan.primary_action,
                                         "open-text-recovery") &&
                           strings_equal(queue_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential queue plan recovery should report recovery queue");
  return fails;
}

static int test_loginwindow_credential_screen_queue_plan_queues_resume_text_login(void) {
  int fails = 0;
  struct login_window_credential_screen_queue_plan queue_plan;

  fails += expect_true(build_loginwindow_credential_screen_queue_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &queue_plan) == 0,
                       "credential queue plan resume should build");
  fails += expect_true(queue_plan.queue_plan_safe == 1 &&
                           queue_plan.queue_text_login == 1 &&
                           queue_plan.queue_text_login_resume == 1,
                       "credential queue plan resume should queue text login resume");
  fails += expect_true(queue_plan.session_reset_required == 1 &&
                           queue_plan.login_screen_rerender_required == 1 &&
                           queue_plan.queue_credential_focus == 0,
                       "credential queue plan resume should require reset and rerender");
  fails += expect_true(queue_plan.submit_callback_bound == 0 &&
                           queue_plan.auth_callback_bound == 0 &&
                           queue_plan.submit_enabled == 0 &&
                           queue_plan.auth_attempt_allowed == 0,
                       "credential queue plan resume must keep GUI auth disabled");
  fails += expect_true(strings_equal(queue_plan.queue_ticket,
                                     "text-login-resume-queue-ticket") &&
                           strings_equal(queue_plan.primary_action,
                                         "resume-text-login") &&
                           strings_equal(queue_plan.state,
                                         "queue-resume-ready"),
                       "credential queue plan resume should report resume queue");
  return fails;
}

static int test_loginwindow_credential_screen_queue_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_queue_plan queue_plan;

  fails += expect_true(build_loginwindow_credential_screen_queue_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &queue_plan) == 0,
                       "credential queue plan submit should build");
  fails += expect_true(queue_plan.queue_plan_safe == 1 &&
                           queue_plan.submit_requested == 1 &&
                           queue_plan.queue_text_login_fallback == 1,
                       "credential queue plan submit should queue text login fallback");
  fails += expect_true(queue_plan.action_allowed == 0 &&
                           queue_plan.action_blocked == 1 &&
                           queue_plan.input_focus_allowed == 0 &&
                           queue_plan.queue_credential_focus == 0,
                       "credential queue plan submit should block GUI action");
  fails += expect_true(queue_plan.window_queue_allowed == 1 &&
                           queue_plan.window_queue_enqueued == 0 &&
                           queue_plan.submit_callback_bound == 0 &&
                           queue_plan.auth_callback_bound == 0 &&
                           queue_plan.submit_enabled == 0 &&
                           queue_plan.auth_attempt_allowed == 0 &&
                           queue_plan.raw_secret_exposed == 0 &&
                           queue_plan.masked_text_exposed == 0,
                       "credential queue plan submit must stay declarative and redacted");
  fails += expect_true(strings_equal(queue_plan.queue_ticket,
                                     "text-login-fallback-queue-ticket") &&
                           strings_equal(queue_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(queue_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential queue plan submit should explain fallback queue");

  fails += expect_true(build_loginwindow_credential_screen_queue_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &queue_plan) == 0,
                       "credential queue plan unknown should build");
  fails += expect_true(queue_plan.queue_plan_safe == 1 &&
                           queue_plan.queue_text_login_fallback == 1 &&
                           queue_plan.action_allowed == 0 &&
                           queue_plan.action_blocked == 1,
                       "credential queue plan unknown should force text login fallback");
  fails += expect_true(strings_equal(queue_plan.queue_ticket,
                                     "text-login-fallback-queue-ticket") &&
                           strings_equal(queue_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential queue plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_queue_plan_fails_closed_for_unsafe_or_missing_dispatch_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_queue_plan queue_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_queue_plan_build(
                           NULL, &queue_plan) == 0,
                       "credential queue plan missing dispatch plan should build fail-closed state");
  fails += expect_true(queue_plan.dispatch_plan_available == 0 &&
                           queue_plan.dispatch_plan_safe == 0 &&
                           queue_plan.queue_plan_safe == 0 &&
                           queue_plan.route_selected == 0 &&
                           queue_plan.route_blocked == 1,
                       "credential queue plan missing dispatch plan should block queue plan");
  fails += expect_true(queue_plan.window_queue_allowed == 0 &&
                           queue_plan.window_queue_enqueued == 0 &&
                           queue_plan.queue_text_login == 1 &&
                           queue_plan.queue_text_login_fallback == 1 &&
                           queue_plan.submit_callback_bound == 0 &&
                           queue_plan.auth_callback_bound == 0 &&
                           queue_plan.submit_enabled == 0 &&
                           queue_plan.auth_attempt_allowed == 0,
                       "credential queue plan missing dispatch plan must stay redacted");
  fails += expect_true(strings_equal(queue_plan.queue_ticket,
                                     "text-login-fallback-queue-ticket") &&
                           strings_equal(queue_plan.event_type,
                                         "credential-screen-queue-plan-unavailable") &&
                           strings_equal(queue_plan.blocked_reason,
                                         "dispatch-plan-unavailable"),
                       "credential queue plan missing dispatch plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_queue_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &queue_plan) == 0,
                       "credential queue plan unsafe dispatch plan should build blocked state");
  fails += expect_true(queue_plan.dispatch_plan_available == 1 &&
                           queue_plan.dispatch_plan_safe == 0 &&
                           queue_plan.queue_plan_safe == 0 &&
                           queue_plan.route_selected == 0 &&
                           queue_plan.route_blocked == 1,
                       "credential queue plan unsafe dispatch plan should block queue plan");
  fails += expect_true(queue_plan.action_allowed == 0 &&
                           queue_plan.action_blocked == 1 &&
                           queue_plan.input_focus_allowed == 0 &&
                           queue_plan.queue_credential_focus == 0 &&
                           queue_plan.queue_text_login_fallback == 1,
                       "credential queue plan unsafe dispatch plan must force text login fallback");
  fails += expect_true(strings_equal(queue_plan.queue_ticket,
                                     "text-login-fallback-queue-ticket") &&
                           strings_equal(queue_plan.event_type,
                                         "credential-screen-queue-plan-unsafe") &&
                           strings_equal(queue_plan.blocked_reason,
                                         "credential-queue-plan-unsafe"),
                       "credential queue plan unsafe dispatch plan should force text login");
  return fails;
}


int build_loginwindow_credential_screen_activation_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_activation_plan *activation_plan) {
  struct login_window_credential_screen_queue_plan queue_plan;

  if (build_loginwindow_credential_screen_queue_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &queue_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_activation_plan_build(&queue_plan,
                                                              activation_plan);
}

static int test_loginwindow_credential_screen_activation_plan_activates_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_activation_plan activation_plan;

  fails += expect_true(build_loginwindow_credential_screen_activation_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'e', 0, 0, 0,
                           1, &activation_plan) == 0,
                       "credential activation plan edit should build");
  fails += expect_true(activation_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTIVATION_PLAN_VERSION,
                       "credential activation plan should expose stable version");
  fails += expect_true(activation_plan.queue_plan_available == 1 &&
                           activation_plan.queue_plan_safe == 1 &&
                           activation_plan.activation_plan_safe == 1,
                       "credential activation plan edit should require safe queue plan");
  fails += expect_true(activation_plan.window_activation_allowed == 1 &&
                           activation_plan.window_activation_applied == 0 &&
                           activation_plan.activation_ticket_selected == 1,
                       "credential activation plan edit should remain declarative");
  fails += expect_true(activation_plan.activate_credential_panel == 1 &&
                           activation_plan.activate_credential_input == 1 &&
                           activation_plan.activate_credential_focus == 1,
                       "credential activation plan edit should activate credential widgets");
  fails += expect_true(activation_plan.submit_callback_bound == 0 &&
                           activation_plan.auth_callback_bound == 0 &&
                           activation_plan.submit_enabled == 0 &&
                           activation_plan.auth_attempt_allowed == 0,
                       "credential activation plan edit must not bind auth callbacks");
  fails += expect_true(activation_plan.raw_secret_exposed == 0 &&
                           activation_plan.masked_text_exposed == 0 &&
                           activation_plan.length_redacted == 1,
                       "credential activation plan edit must stay redacted");
  fails += expect_true(strings_equal(activation_plan.activation_ticket,
                                     "credential-screen-activation-ticket") &&
                           strings_equal(activation_plan.focus_target,
                                         "credential-input") &&
                           strings_equal(activation_plan.state,
                                         "activation-credential-ready"),
                       "credential activation plan edit should report activation state");
  return fails;
}

static int test_loginwindow_credential_screen_activation_plan_activates_text_recovery(void) {
  int fails = 0;
  struct login_window_credential_screen_activation_plan activation_plan;

  fails += expect_true(build_loginwindow_credential_screen_activation_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &activation_plan) == 0,
                       "credential activation plan recovery should build");
  fails += expect_true(activation_plan.activation_plan_safe == 1 &&
                           activation_plan.window_activation_allowed == 1 &&
                           activation_plan.window_activation_applied == 0 &&
                           activation_plan.activate_text_recovery == 1 &&
                           activation_plan.activate_text_login == 1,
                       "credential activation plan recovery should activate text recovery");
  fails += expect_true(activation_plan.activate_credential_input == 0 &&
                           activation_plan.activate_credential_focus == 0 &&
                           activation_plan.input_focus_allowed == 0,
                       "credential activation plan recovery should block credential focus");
  fails += expect_true(activation_plan.submit_callback_bound == 0 &&
                           activation_plan.auth_callback_bound == 0 &&
                           activation_plan.submit_enabled == 0 &&
                           activation_plan.auth_attempt_allowed == 0,
                       "credential activation plan recovery must keep auth disabled");
  fails += expect_true(strings_equal(activation_plan.activation_ticket,
                                     "text-recovery-activation-ticket") &&
                           strings_equal(activation_plan.primary_action,
                                         "open-text-recovery") &&
                           strings_equal(activation_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential activation plan recovery should report recovery activation");
  return fails;
}

static int test_loginwindow_credential_screen_activation_plan_activates_resume_text_login(void) {
  int fails = 0;
  struct login_window_credential_screen_activation_plan activation_plan;

  fails += expect_true(build_loginwindow_credential_screen_activation_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &activation_plan) == 0,
                       "credential activation plan resume should build");
  fails += expect_true(activation_plan.activation_plan_safe == 1 &&
                           activation_plan.activate_text_login == 1 &&
                           activation_plan.activate_text_login_resume == 1,
                       "credential activation plan resume should activate text login resume");
  fails += expect_true(activation_plan.session_reset_required == 1 &&
                           activation_plan.login_screen_rerender_required == 1 &&
                           activation_plan.activate_credential_focus == 0,
                       "credential activation plan resume should require reset and rerender");
  fails += expect_true(activation_plan.submit_callback_bound == 0 &&
                           activation_plan.auth_callback_bound == 0 &&
                           activation_plan.submit_enabled == 0 &&
                           activation_plan.auth_attempt_allowed == 0,
                       "credential activation plan resume must keep GUI auth disabled");
  fails += expect_true(strings_equal(activation_plan.activation_ticket,
                                     "text-login-resume-activation-ticket") &&
                           strings_equal(activation_plan.primary_action,
                                         "resume-text-login") &&
                           strings_equal(activation_plan.state,
                                         "activation-resume-ready"),
                       "credential activation plan resume should report resume activation");
  return fails;
}

static int test_loginwindow_credential_screen_activation_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_activation_plan activation_plan;

  fails += expect_true(build_loginwindow_credential_screen_activation_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &activation_plan) == 0,
                       "credential activation plan submit should build");
  fails += expect_true(activation_plan.activation_plan_safe == 1 &&
                           activation_plan.submit_requested == 1 &&
                           activation_plan.activate_text_login_fallback == 1,
                       "credential activation plan submit should activate text login fallback");
  fails += expect_true(activation_plan.action_allowed == 0 &&
                           activation_plan.action_blocked == 1 &&
                           activation_plan.input_focus_allowed == 0 &&
                           activation_plan.activate_credential_focus == 0,
                       "credential activation plan submit should block GUI action");
  fails += expect_true(activation_plan.window_activation_allowed == 1 &&
                           activation_plan.window_activation_applied == 0 &&
                           activation_plan.submit_callback_bound == 0 &&
                           activation_plan.auth_callback_bound == 0 &&
                           activation_plan.submit_enabled == 0 &&
                           activation_plan.auth_attempt_allowed == 0 &&
                           activation_plan.raw_secret_exposed == 0 &&
                           activation_plan.masked_text_exposed == 0,
                       "credential activation plan submit must stay declarative and redacted");
  fails += expect_true(strings_equal(activation_plan.activation_ticket,
                                     "text-login-fallback-activation-ticket") &&
                           strings_equal(activation_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(activation_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential activation plan submit should explain fallback activation");

  fails += expect_true(build_loginwindow_credential_screen_activation_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &activation_plan) == 0,
                       "credential activation plan unknown should build");
  fails += expect_true(activation_plan.activation_plan_safe == 1 &&
                           activation_plan.activate_text_login_fallback == 1 &&
                           activation_plan.action_allowed == 0 &&
                           activation_plan.action_blocked == 1,
                       "credential activation plan unknown should force text login fallback");
  fails += expect_true(strings_equal(activation_plan.activation_ticket,
                                     "text-login-fallback-activation-ticket") &&
                           strings_equal(activation_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential activation plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_activation_plan_fails_closed_for_unsafe_or_missing_queue_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_activation_plan activation_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_activation_plan_build(
                           NULL, &activation_plan) == 0,
                       "credential activation plan missing queue plan should build fail-closed state");
  fails += expect_true(activation_plan.queue_plan_available == 0 &&
                           activation_plan.queue_plan_safe == 0 &&
                           activation_plan.activation_plan_safe == 0 &&
                           activation_plan.route_selected == 0 &&
                           activation_plan.route_blocked == 1,
                       "credential activation plan missing queue plan should block activation plan");
  fails += expect_true(activation_plan.window_activation_allowed == 0 &&
                           activation_plan.window_activation_applied == 0 &&
                           activation_plan.activate_text_login == 1 &&
                           activation_plan.activate_text_login_fallback == 1 &&
                           activation_plan.submit_callback_bound == 0 &&
                           activation_plan.auth_callback_bound == 0 &&
                           activation_plan.submit_enabled == 0 &&
                           activation_plan.auth_attempt_allowed == 0,
                       "credential activation plan missing queue plan must stay redacted");
  fails += expect_true(strings_equal(activation_plan.activation_ticket,
                                     "text-login-fallback-activation-ticket") &&
                           strings_equal(activation_plan.event_type,
                                         "credential-screen-activation-plan-unavailable") &&
                           strings_equal(activation_plan.blocked_reason,
                                         "queue-plan-unavailable"),
                       "credential activation plan missing queue plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_activation_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &activation_plan) == 0,
                       "credential activation plan unsafe queue plan should build blocked state");
  fails += expect_true(activation_plan.queue_plan_available == 1 &&
                           activation_plan.queue_plan_safe == 0 &&
                           activation_plan.activation_plan_safe == 0 &&
                           activation_plan.route_selected == 0 &&
                           activation_plan.route_blocked == 1,
                       "credential activation plan unsafe queue plan should block activation plan");
  fails += expect_true(activation_plan.action_allowed == 0 &&
                           activation_plan.action_blocked == 1 &&
                           activation_plan.input_focus_allowed == 0 &&
                           activation_plan.activate_credential_focus == 0 &&
                           activation_plan.activate_text_login_fallback == 1,
                       "credential activation plan unsafe queue plan must force text login fallback");
  fails += expect_true(strings_equal(activation_plan.activation_ticket,
                                     "text-login-fallback-activation-ticket") &&
                           strings_equal(activation_plan.event_type,
                                         "credential-screen-activation-plan-unsafe") &&
                           strings_equal(activation_plan.blocked_reason,
                                         "credential-activation-plan-unsafe"),
                       "credential activation plan unsafe queue plan should force text login");
  return fails;
}

int test_login_runtime_credential_queue_activation_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_queue_plan_queues_credential_widgets();
  fails += test_loginwindow_credential_screen_queue_plan_queues_text_recovery();
  fails += test_loginwindow_credential_screen_queue_plan_queues_resume_text_login();
  fails += test_loginwindow_credential_screen_queue_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_queue_plan_fails_closed_for_unsafe_or_missing_dispatch_plan();
  fails += test_loginwindow_credential_screen_activation_plan_activates_credential_widgets();
  fails += test_loginwindow_credential_screen_activation_plan_activates_text_recovery();
  fails += test_loginwindow_credential_screen_activation_plan_activates_resume_text_login();
  fails += test_loginwindow_credential_screen_activation_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_activation_plan_fails_closed_for_unsafe_or_missing_queue_plan();
  return fails;
}
