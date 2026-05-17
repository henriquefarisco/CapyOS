/*
 * tests/auth/test_login_runtime_credential_audit_view.c
 *
 * Credential readiness + audit + view-model coverage for the
 * `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-15 monolith
 * refactor (PR D.4 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_readiness_evaluate`: 4 tests covering
 *     the ready masked panel + the submit interaction + the
 *     missing-policy fail-closed default + the overflowed buffer
 *     rejection.
 *   - `login_window_credential_audit_event_build`: 4 tests covering
 *     the redacted ready panel + the redacted submit interaction +
 *     the input-block without mutation + the missing-readiness
 *     fail-closed default.
 *   - `login_window_credential_view_model_build`: 4 tests covering
 *     the redacted ready state + the blocked-submit message + the
 *     input-block fallback + the unsafe-audit fail-closed default.
 *
 * The companion entry `test_login_runtime_credential_audit_view_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`. Shared fixture state and helpers
 * come from `tests/auth/test_login_runtime_internal.h`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

static int test_loginwindow_credential_readiness_reports_ready_masked_panel(void) {
  int fails = 0;
  char storage[8];
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_panel panel;
  struct login_window_credential_readiness readiness;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "credential readiness contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "credential readiness policy should build");
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "credential readiness buffer should initialize");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'a') == 1,
                       "credential readiness should append secret");
  fails += expect_true(login_window_credential_panel_build(
                           &policy, &buffer, NULL, masked, sizeof(masked),
                           &panel) == 0,
                       "credential readiness should build panel");
  fails += expect_true(login_window_credential_readiness_evaluate(
                           &policy, &buffer, &panel, NULL,
                           &readiness) == 0,
                       "credential readiness should evaluate panel");
  fails += expect_true(readiness.version == LOGIN_WINDOW_CREDENTIAL_READINESS_VERSION,
                       "credential readiness should expose stable version");
  fails += expect_true(readiness.ready_for_render == 1 &&
                           readiness.ready_for_input == 1 &&
                           readiness.ready_for_masked_text == 1,
                       "credential readiness should allow render and input only");
  fails += expect_true(readiness.submit_blocked == 1 &&
                           readiness.submit_allowed == 0 &&
                           readiness.auth_attempt_allowed == 0,
                       "credential readiness must keep GUI auth blocked");
  fails += expect_true(readiness.has_secret == 1 && readiness.length == 1,
                       "credential readiness should expose safe length metadata");
  fails += expect_true(strings_equal(readiness.state, "ready") &&
                           strings_equal(readiness.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential readiness should explain submit block");
  fails += expect_true(strings_equal(masked, "*"),
                       "credential readiness should not alter masked output");
  return fails;
}

static int test_loginwindow_credential_readiness_reflects_submit_interaction(void) {
  int fails = 0;
  char storage[8];
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_interaction interaction;
  struct login_window_credential_readiness readiness;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "credential readiness submit contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "credential readiness submit policy should build");
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "credential readiness submit buffer should initialize");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'p') == 1,
                       "credential readiness submit should append secret");
  fails += expect_true(login_window_credential_interaction_step(
                           &policy, &buffer,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_SUBMIT, '\0',
                           masked, sizeof(masked), &interaction) == 0,
                       "credential readiness submit interaction should run");
  fails += expect_true(login_window_credential_readiness_evaluate(
                           &policy, &buffer, &interaction.panel,
                           &interaction, &readiness) == 0,
                       "credential readiness should evaluate submit interaction");
  fails += expect_true(readiness.interaction_available == 1 &&
                           readiness.panel_available == 1,
                       "credential readiness should expose interaction and panel");
  fails += expect_true(readiness.wipe_attempted == 1 &&
                           readiness.wipe_succeeded == 1 &&
                           readiness.has_secret == 0,
                       "credential readiness should reflect wiped submit");
  fails += expect_true(readiness.ready_for_render == 1 &&
                           readiness.ready_for_masked_text == 1,
                       "credential readiness submit should keep safe render state");
  fails += expect_true(readiness.submit_blocked == 1 &&
                           readiness.submit_allowed == 0 &&
                           readiness.auth_attempt_allowed == 0,
                       "credential readiness submit must keep GUI auth blocked");
  fails += expect_true(strings_equal(readiness.state, "submit-blocked") &&
                           strings_equal(readiness.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential readiness should expose submit-blocked state");
  return fails;
}

static int test_loginwindow_credential_readiness_fails_closed_without_policy(void) {
  int fails = 0;
  struct login_window_credential_readiness readiness;

  reset_test_state();
  fails += expect_true(login_window_credential_readiness_evaluate(
                           NULL, NULL, NULL, NULL, &readiness) == 0,
                       "credential readiness should evaluate missing policy");
  fails += expect_true(readiness.policy_available == 0 &&
                           readiness.ready_for_render == 0 &&
                           readiness.ready_for_input == 0,
                       "credential readiness missing policy should not be ready");
  fails += expect_true(readiness.submit_blocked == 1 &&
                           readiness.submit_allowed == 0 &&
                           readiness.auth_attempt_allowed == 0,
                       "credential readiness missing policy must block GUI auth");
  fails += expect_true(strings_equal(readiness.state, "blocked") &&
                           strings_equal(readiness.blocked_reason,
                                         "policy-unavailable"),
                       "credential readiness missing policy should explain block");
  return fails;
}

static int test_loginwindow_credential_readiness_blocks_overflowed_buffer(void) {
  int fails = 0;
  char storage[3];
  char masked[3];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_panel panel;
  struct login_window_credential_readiness readiness;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "credential readiness overflow contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "credential readiness overflow policy should build");
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "credential readiness overflow buffer should initialize");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'a') == 1,
                       "credential readiness overflow should append first byte");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'b') == 1,
                       "credential readiness overflow should append second byte");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'c') == 0,
                       "credential readiness overflow should block third byte");
  fails += expect_true(login_window_credential_panel_build(
                           &policy, &buffer, NULL, masked, sizeof(masked),
                           &panel) == 0,
                       "credential readiness overflow should build panel");
  fails += expect_true(login_window_credential_readiness_evaluate(
                           &policy, &buffer, &panel, NULL,
                           &readiness) == 0,
                       "credential readiness should evaluate overflow");
  fails += expect_true(readiness.overflow_blocked == 1 &&
                           readiness.ready_for_input == 0,
                       "credential readiness overflow should block input readiness");
  fails += expect_true(readiness.submit_blocked == 1 &&
                           readiness.submit_allowed == 0 &&
                           readiness.auth_attempt_allowed == 0,
                       "credential readiness overflow must block GUI auth");
  fails += expect_true(strings_equal(readiness.blocked_reason,
                                     "credential-overflow-blocked"),
                       "credential readiness overflow should explain block");
  fails += expect_true(login_window_credential_buffer_wipe(&buffer) == 0,
                       "credential readiness overflow test should wipe buffer");
  return fails;
}


static int test_loginwindow_credential_audit_event_redacts_ready_panel(void) {
  int fails = 0;
  char storage[8];
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_panel panel;
  struct login_window_credential_readiness readiness;
  struct login_window_credential_audit_event event;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "credential audit ready contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "credential audit ready policy should build");
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "credential audit ready buffer should initialize");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'a') == 1,
                       "credential audit ready should append secret");
  fails += expect_true(login_window_credential_panel_build(
                           &policy, &buffer, NULL, masked, sizeof(masked),
                           &panel) == 0,
                       "credential audit ready should build panel");
  fails += expect_true(login_window_credential_readiness_evaluate(
                           &policy, &buffer, &panel, NULL,
                           &readiness) == 0,
                       "credential audit ready should evaluate readiness");
  fails += expect_true(login_window_credential_audit_event_build(
                           &readiness, NULL, &event) == 0,
                       "credential audit ready should build event");
  fails += expect_true(event.version == LOGIN_WINDOW_CREDENTIAL_AUDIT_EVENT_VERSION,
                       "credential audit should expose stable version");
  fails += expect_true(event.readiness_available == 1 &&
                           event.interaction_available == 0,
                       "credential audit ready should expose source availability");
  fails += expect_true(strings_equal(event.event_type, "credential-ready") &&
                           strings_equal(event.state, "ready"),
                       "credential audit ready should classify event");
  fails += expect_true(event.credential_present == 1 &&
                           event.raw_secret_exposed == 0 &&
                           event.masked_text_exposed == 0,
                       "credential audit ready must redact secret and mask");
  fails += expect_true(event.secret_redacted == 1 &&
                           event.length_redacted == 1,
                       "credential audit ready should redact secret metadata");
  fails += expect_true(event.submit_blocked == 1 &&
                           event.submit_allowed == 0 &&
                           event.auth_attempt_allowed == 0,
                       "credential audit ready must keep GUI auth blocked");
  return fails;
}

static int test_loginwindow_credential_audit_event_redacts_submit_interaction(void) {
  int fails = 0;
  char storage[8];
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_interaction interaction;
  struct login_window_credential_readiness readiness;
  struct login_window_credential_audit_event event;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "credential audit submit contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "credential audit submit policy should build");
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "credential audit submit buffer should initialize");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'p') == 1,
                       "credential audit submit should append secret");
  fails += expect_true(login_window_credential_interaction_step(
                           &policy, &buffer,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_SUBMIT, '\0',
                           masked, sizeof(masked), &interaction) == 0,
                       "credential audit submit interaction should run");
  fails += expect_true(login_window_credential_readiness_evaluate(
                           &policy, &buffer, &interaction.panel,
                           &interaction, &readiness) == 0,
                       "credential audit submit should evaluate readiness");
  fails += expect_true(login_window_credential_audit_event_build(
                           &readiness, &interaction, &event) == 0,
                       "credential audit submit should build event");
  fails += expect_true(event.interaction_available == 1 &&
                           event.input_applied == 1 &&
                           event.action == LOGIN_WINDOW_CREDENTIAL_INPUT_SUBMIT,
                       "credential audit submit should expose safe action metadata");
  fails += expect_true(event.submit_attempted == 1 &&
                           event.wipe_attempted == 1 &&
                           event.wipe_succeeded == 1,
                       "credential audit submit should expose wipe metadata");
  fails += expect_true(strings_equal(event.event_type,
                                     "credential-submit-blocked") &&
                           strings_equal(event.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential audit submit should classify blocked submit");
  fails += expect_true(event.credential_present == 0 &&
                           event.raw_secret_exposed == 0 &&
                           event.masked_text_exposed == 0,
                       "credential audit submit must not expose wiped secret");
  fails += expect_true(event.secret_redacted == 1 &&
                           event.length_redacted == 1,
                       "credential audit submit should redact length and secret");
  fails += expect_true(event.submit_blocked == 1 &&
                           event.submit_allowed == 0 &&
                           event.auth_attempt_allowed == 0,
                       "credential audit submit must keep GUI auth blocked");
  return fails;
}

static int test_loginwindow_credential_audit_event_reports_input_block_without_mutation(void) {
  int fails = 0;
  char storage[8];
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_interaction interaction;
  struct login_window_credential_readiness readiness;
  struct login_window_credential_audit_event event;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "credential audit blocked contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "credential audit blocked policy should build");
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "credential audit blocked buffer should initialize");
  fails += expect_true(login_window_credential_interaction_step(
                           &policy, &buffer, 99, '\0', masked, sizeof(masked),
                           &interaction) == 0,
                       "credential audit blocked interaction should run");
  fails += expect_true(login_window_credential_readiness_evaluate(
                           &policy, &buffer, &interaction.panel,
                           &interaction, &readiness) == 0,
                       "credential audit blocked should evaluate readiness");
  fails += expect_true(login_window_credential_audit_event_build(
                           &readiness, &interaction, &event) == 0,
                       "credential audit blocked should build event");
  fails += expect_true(strings_equal(event.event_type,
                                     "credential-input-blocked") &&
                           strings_equal(event.blocked_reason,
                                         "input-action-unknown"),
                       "credential audit blocked should classify input block");
  fails += expect_true(event.input_blocked == 1 &&
                           event.input_accepted == 0 &&
                           event.credential_present == 0,
                       "credential audit blocked should avoid mutation metadata leak");
  fails += expect_true(event.raw_secret_exposed == 0 &&
                           event.masked_text_exposed == 0 &&
                           event.secret_redacted == 1 &&
                           event.length_redacted == 1,
                       "credential audit blocked must remain redacted");
  fails += expect_true(event.submit_allowed == 0 &&
                           event.auth_attempt_allowed == 0,
                       "credential audit blocked must keep GUI auth blocked");
  return fails;
}

static int test_loginwindow_credential_audit_event_fails_closed_without_readiness(void) {
  int fails = 0;
  struct login_window_credential_audit_event event;

  reset_test_state();
  fails += expect_true(login_window_credential_audit_event_build(
                           NULL, NULL, &event) == 0,
                       "credential audit should evaluate missing readiness");
  fails += expect_true(event.readiness_available == 0 &&
                           event.interaction_available == 0,
                       "credential audit missing readiness should expose absence");
  fails += expect_true(strings_equal(event.event_type,
                                     "credential-audit-unavailable") &&
                           strings_equal(event.blocked_reason,
                                         "readiness-unavailable"),
                       "credential audit missing readiness should explain block");
  fails += expect_true(event.raw_secret_exposed == 0 &&
                           event.masked_text_exposed == 0 &&
                           event.secret_redacted == 1 &&
                           event.length_redacted == 1,
                       "credential audit missing readiness must be redacted");
  fails += expect_true(event.submit_blocked == 1 &&
                           event.submit_allowed == 0 &&
                           event.auth_attempt_allowed == 0,
                       "credential audit missing readiness must keep GUI auth blocked");
  return fails;
}


static int test_loginwindow_credential_view_model_renders_redacted_ready_state(void) {
  int fails = 0;
  char storage[8];
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_panel panel;
  struct login_window_credential_readiness readiness;
  struct login_window_credential_audit_event audit;
  struct login_window_credential_view_model view;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "credential view ready contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "credential view ready policy should build");
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "credential view ready buffer should initialize");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'a') == 1,
                       "credential view ready should append secret");
  fails += expect_true(login_window_credential_panel_build(
                           &policy, &buffer, NULL, masked, sizeof(masked),
                           &panel) == 0,
                       "credential view ready should build panel");
  fails += expect_true(login_window_credential_readiness_evaluate(
                           &policy, &buffer, &panel, NULL,
                           &readiness) == 0,
                       "credential view ready should evaluate readiness");
  fails += expect_true(login_window_credential_audit_event_build(
                           &readiness, NULL, &audit) == 0,
                       "credential view ready should build audit");
  fails += expect_true(login_window_credential_view_model_build(
                           &readiness, &audit, &view) == 0,
                       "credential view ready should build view model");
  fails += expect_true(view.version == LOGIN_WINDOW_CREDENTIAL_VIEW_MODEL_VERSION,
                       "credential view should expose stable version");
  fails += expect_true(view.renderable == 1 &&
                           view.input_enabled == 1 &&
                           view.password_field_visible == 1,
                       "credential view ready should render input field");
  fails += expect_true(view.masked_text_ready == 1 &&
                           view.masked_text_redacted == 1,
                       "credential view ready should require redacted mask");
  fails += expect_true(view.credential_present == 1 &&
                           view.credential_redacted == 1 &&
                           view.length_redacted == 1,
                       "credential view ready should redact credential metadata");
  fails += expect_true(view.submit_visible == 0 &&
                           view.submit_enabled == 0 &&
                           view.submit_blocked == 1 &&
                           view.auth_attempt_allowed == 0,
                       "credential view ready must keep GUI submit disabled");
  fails += expect_true(view.cancel_visible == 1 && view.cancel_enabled == 1,
                       "credential view ready should allow cancel UI only");
  fails += expect_true(view.raw_secret_exposed == 0 &&
                           view.masked_text_exposed == 0,
                       "credential view ready must not expose secret or mask");
  fails += expect_true(strings_equal(view.event_type, "credential-ready") &&
                           strings_equal(view.state, "ready") &&
                           view.error_visible == 0,
                       "credential view ready should expose safe ready state");
  return fails;
}

static int test_loginwindow_credential_view_model_reflects_blocked_submit(void) {
  int fails = 0;
  char storage[8];
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_interaction interaction;
  struct login_window_credential_readiness readiness;
  struct login_window_credential_audit_event audit;
  struct login_window_credential_view_model view;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "credential view submit contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "credential view submit policy should build");
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "credential view submit buffer should initialize");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'p') == 1,
                       "credential view submit should append secret");
  fails += expect_true(login_window_credential_interaction_step(
                           &policy, &buffer,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_SUBMIT, '\0',
                           masked, sizeof(masked), &interaction) == 0,
                       "credential view submit interaction should run");
  fails += expect_true(login_window_credential_readiness_evaluate(
                           &policy, &buffer, &interaction.panel,
                           &interaction, &readiness) == 0,
                       "credential view submit should evaluate readiness");
  fails += expect_true(login_window_credential_audit_event_build(
                           &readiness, &interaction, &audit) == 0,
                       "credential view submit should build audit");
  fails += expect_true(login_window_credential_view_model_build(
                           &readiness, &audit, &view) == 0,
                       "credential view submit should build view model");
  fails += expect_true(view.renderable == 1 && view.input_enabled == 0,
                       "credential view submit should render but disable input");
  fails += expect_true(view.wipe_attempted == 1 &&
                           view.wipe_succeeded == 1 &&
                           view.credential_present == 0,
                       "credential view submit should reflect wiped credential");
  fails += expect_true(strings_equal(view.event_type,
                                     "credential-submit-blocked") &&
                           strings_equal(view.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential view submit should expose blocked submit event");
  fails += expect_true(strings_equal(view.message,
                                     "Submit blocked; use text login."),
                       "credential view submit should guide to text login");
  fails += expect_true(view.submit_visible == 0 &&
                           view.submit_enabled == 0 &&
                           view.submit_blocked == 1 &&
                           view.auth_attempt_allowed == 0,
                       "credential view submit must keep GUI auth disabled");
  fails += expect_true(view.raw_secret_exposed == 0 &&
                           view.masked_text_exposed == 0 &&
                           view.credential_redacted == 1 &&
                           view.length_redacted == 1,
                       "credential view submit must remain redacted");
  return fails;
}

static int test_loginwindow_credential_view_model_reflects_input_block(void) {
  int fails = 0;
  char storage[8];
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_interaction interaction;
  struct login_window_credential_readiness readiness;
  struct login_window_credential_audit_event audit;
  struct login_window_credential_view_model view;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "credential view blocked contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "credential view blocked policy should build");
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "credential view blocked buffer should initialize");
  fails += expect_true(login_window_credential_interaction_step(
                           &policy, &buffer, 99, '\0', masked, sizeof(masked),
                           &interaction) == 0,
                       "credential view blocked interaction should run");
  fails += expect_true(login_window_credential_readiness_evaluate(
                           &policy, &buffer, &interaction.panel,
                           &interaction, &readiness) == 0,
                       "credential view blocked should evaluate readiness");
  fails += expect_true(login_window_credential_audit_event_build(
                           &readiness, &interaction, &audit) == 0,
                       "credential view blocked should build audit");
  fails += expect_true(login_window_credential_view_model_build(
                           &readiness, &audit, &view) == 0,
                       "credential view blocked should build view model");
  fails += expect_true(view.renderable == 1 && view.input_enabled == 0,
                       "credential view blocked should render diagnostic field only");
  fails += expect_true(strings_equal(view.event_type,
                                     "credential-input-blocked") &&
                           strings_equal(view.blocked_reason,
                                         "input-action-unknown"),
                       "credential view blocked should expose input-blocked event");
  fails += expect_true(strings_equal(view.message,
                                     "Credential input blocked; use text login."),
                       "credential view blocked should guide fallback");
  fails += expect_true(view.submit_visible == 0 &&
                           view.submit_enabled == 0 &&
                           view.auth_attempt_allowed == 0,
                       "credential view blocked must keep GUI auth disabled");
  fails += expect_true(view.raw_secret_exposed == 0 &&
                           view.masked_text_exposed == 0,
                       "credential view blocked must remain redacted");
  return fails;
}

static int test_loginwindow_credential_view_model_fails_closed_without_safe_audit(void) {
  int fails = 0;
  struct login_window_credential_readiness readiness;
  struct login_window_credential_audit_event audit;
  struct login_window_credential_view_model view;

  reset_test_state();
  fails += expect_true(login_window_credential_view_model_build(
                           NULL, NULL, &view) == 0,
                       "credential view should handle missing audit");
  fails += expect_true(view.audit_available == 0 &&
                           view.readiness_available == 0 &&
                           view.renderable == 0,
                       "credential view missing audit should not render");
  fails += expect_true(strings_equal(view.event_type,
                                     "credential-view-unavailable") &&
                           strings_equal(view.blocked_reason,
                                         "audit-unavailable"),
                       "credential view missing audit should explain block");
  fails += expect_true(view.submit_blocked == 1 &&
                           view.submit_enabled == 0 &&
                           view.auth_attempt_allowed == 0,
                       "credential view missing audit must keep GUI auth disabled");
  fails += expect_true(login_window_credential_readiness_evaluate(
                           NULL, NULL, NULL, NULL, &readiness) == 0,
                       "credential view unsafe audit should build readiness shell");
  fails += expect_true(login_window_credential_audit_event_build(
                           &readiness, NULL, &audit) == 0,
                       "credential view unsafe audit should build audit shell");
  audit.raw_secret_exposed = 1;
  fails += expect_true(login_window_credential_view_model_build(
                           &readiness, &audit, &view) == 0,
                       "credential view should reject unsafe audit");
  fails += expect_true(view.renderable == 0 &&
                           strings_equal(view.event_type,
                                         "credential-view-blocked") &&
                           strings_equal(view.blocked_reason,
                                         "credential-audit-unsafe"),
                       "credential view unsafe audit should fail closed");
  fails += expect_true(view.submit_enabled == 0 &&
                           view.auth_attempt_allowed == 0,
                       "credential view unsafe audit must keep GUI auth disabled");
  return fails;
}

int test_login_runtime_credential_audit_view_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_readiness_reports_ready_masked_panel();
  fails += test_loginwindow_credential_readiness_reflects_submit_interaction();
  fails += test_loginwindow_credential_readiness_fails_closed_without_policy();
  fails += test_loginwindow_credential_readiness_blocks_overflowed_buffer();
  fails += test_loginwindow_credential_audit_event_redacts_ready_panel();
  fails += test_loginwindow_credential_audit_event_redacts_submit_interaction();
  fails += test_loginwindow_credential_audit_event_reports_input_block_without_mutation();
  fails += test_loginwindow_credential_audit_event_fails_closed_without_readiness();
  fails += test_loginwindow_credential_view_model_renders_redacted_ready_state();
  fails += test_loginwindow_credential_view_model_reflects_blocked_submit();
  fails += test_loginwindow_credential_view_model_reflects_input_block();
  fails += test_loginwindow_credential_view_model_fails_closed_without_safe_audit();
  return fails;
}
