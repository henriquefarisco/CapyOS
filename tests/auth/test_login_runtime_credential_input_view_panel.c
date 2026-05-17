/*
 * tests/auth/test_login_runtime_credential_input_view_panel.c
 *
 * Credential panel + interaction coverage for the `login_runtime`
 * host test. Carved out of
 * `tests/auth/test_login_runtime_credential_input_view.c` at the
 * 2026-05-16 preventive refactor (the parent companion had reached
 * 856/900 LOC) so each host-test translation unit stays comfortably
 * below the 900-line layout limit. Tests in this file exercise:
 *
 *   - `login_window_credential_panel_build`: 4 tests covering the
 *     ready masked panel + the panel reflecting append input + the
 *     panel reflecting submit/cancel + the panel failing closed on
 *     unsafe policy or blocked input.
 *   - `login_window_credential_interaction_step`: 4 tests covering
 *     the append/rebuild pipeline + the submit wipe gate + the
 *     cancel wipe + the missing-policy/unknown-action fail-closed
 *     default.
 *
 * The companion entry
 * `test_login_runtime_credential_input_view_panel_cases` is invoked
 * by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`. Shared fixture state and
 * helpers come from `tests/auth/test_login_runtime_internal.h`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

static int test_loginwindow_credential_panel_renders_masked_ready_state(void) {
  int fails = 0;
  char storage[8];
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_panel panel;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "credential panel contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "credential panel policy should build");
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "credential panel buffer should initialize");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'a') == 1,
                       "credential panel should append first secret byte");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'b') == 1,
                       "credential panel should append second secret byte");
  fails += expect_true(login_window_credential_panel_build(
                           &policy, &buffer, NULL, masked, sizeof(masked),
                           &panel) == 0,
                       "credential panel should build ready state");
  fails += expect_true(panel.version == LOGIN_WINDOW_CREDENTIAL_PANEL_VERSION,
                       "credential panel should expose stable version");
  fails += expect_true(panel.panel_renderable == 1 && panel.field_renderable == 1,
                       "credential panel should render safe field");
  fails += expect_true(panel.masked_text_available == 1 &&
                           panel.masked_text_truncated == 0,
                       "credential panel should expose complete mask");
  fails += expect_true(strings_equal(masked, "**"),
                       "credential panel should expose only masked output");
  fails += expect_true(strings_equal(storage, "ab"),
                       "credential panel should not mutate raw storage");
  fails += expect_true(panel.has_secret == 1 && panel.length == 2,
                       "credential panel should expose length metadata");
  fails += expect_true(panel.submit_allowed == 0 &&
                           panel.auth_attempt_allowed == 0,
                       "credential panel must not allow GUI auth");
  fails += expect_true(strings_equal(panel.state, "filled") &&
                           strings_equal(panel.blocked_reason, "ready"),
                       "credential panel should expose filled ready state");
  return fails;
}

static int test_loginwindow_credential_panel_reflects_append_input(void) {
  int fails = 0;
  char storage[8];
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_input_result input;
  struct login_window_credential_panel panel;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "credential panel append contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "credential panel append policy should build");
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "credential panel append buffer should initialize");
  fails += expect_true(login_window_credential_input_apply(
                           &policy, &buffer,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x',
                           &input) == 0,
                       "credential panel append input should apply");
  fails += expect_true(login_window_credential_panel_build(
                           &policy, &buffer, &input, masked, sizeof(masked),
                           &panel) == 0,
                       "credential panel should build after append");
  fails += expect_true(panel.input_result_available == 1 &&
                           panel.input_accepted == 1 &&
                           panel.input_blocked == 0,
                       "credential panel should reflect accepted input");
  fails += expect_true(panel.buffer_changed == 1 && panel.length == 1,
                       "credential panel should reflect changed buffer");
  fails += expect_true(strings_equal(panel.state, "editing") &&
                           strings_equal(panel.blocked_reason, "ready"),
                       "credential panel should expose editing state");
  fails += expect_true(strings_equal(masked, "*"),
                       "credential panel append should remain masked");
  fails += expect_true(panel.submit_allowed == 0 &&
                           panel.auth_attempt_allowed == 0,
                       "credential panel append must not allow GUI auth");
  return fails;
}

static int test_loginwindow_credential_panel_reflects_submit_and_cancel(void) {
  int fails = 0;
  char storage[8];
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_input_result input;
  struct login_window_credential_panel panel;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "credential panel submit contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "credential panel submit policy should build");
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "credential panel submit buffer should initialize");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'p') == 1,
                       "credential panel submit should append secret");
  fails += expect_true(login_window_credential_input_apply(
                           &policy, &buffer,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_SUBMIT, '\0',
                           &input) == 0,
                       "credential panel submit input should apply");
  fails += expect_true(login_window_credential_panel_build(
                           &policy, &buffer, &input, masked, sizeof(masked),
                           &panel) == 0,
                       "credential panel should build after submit");
  fails += expect_true(panel.submit_attempted == 1 &&
                           panel.wipe_attempted == 1 &&
                           panel.wipe_succeeded == 1,
                       "credential panel should reflect wiped submit");
  fails += expect_true(strings_equal(panel.state, "submit-blocked") &&
                           strings_equal(panel.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential panel should expose blocked submit");
  fails += expect_true(panel.length == 0 && masked[0] == '\0',
                       "credential panel submit should leave empty mask");
  fails += expect_true(panel.submit_allowed == 0 &&
                           panel.auth_attempt_allowed == 0,
                       "credential panel submit must not allow GUI auth");

  fails += expect_true(login_window_credential_buffer_append(&buffer, 'x') == 1,
                       "credential panel cancel should append secret");
  fails += expect_true(login_window_credential_input_apply(
                           &policy, &buffer,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, '\0',
                           &input) == 0,
                       "credential panel cancel input should apply");
  fails += expect_true(login_window_credential_panel_build(
                           &policy, &buffer, &input, masked, sizeof(masked),
                           &panel) == 0,
                       "credential panel should build after cancel");
  fails += expect_true(panel.cancel_consumed == 1 &&
                           panel.wipe_attempted == 1 &&
                           panel.wipe_succeeded == 1,
                       "credential panel should reflect wiped cancel");
  fails += expect_true(strings_equal(panel.state, "cancelled") &&
                           strings_equal(panel.blocked_reason, "cancelled"),
                       "credential panel should expose cancelled state");
  fails += expect_true(panel.length == 0 && masked[0] == '\0',
                       "credential panel cancel should leave empty mask");
  return fails;
}

static int test_loginwindow_credential_panel_fails_closed_on_blocked_field_or_input(void) {
  int fails = 0;
  char storage[8];
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_input_result input;
  struct login_window_credential_panel panel;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "credential panel blocked contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "credential panel blocked policy should build");
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "credential panel blocked buffer should initialize");
  policy.password_mask_required = 0;
  fails += expect_true(login_window_credential_panel_build(
                           &policy, &buffer, NULL, masked, sizeof(masked),
                           &panel) == 0,
                       "credential panel should build blocked unsafe policy");
  fails += expect_true(panel.panel_renderable == 0 &&
                           panel.field_renderable == 0,
                       "credential panel unsafe policy should not render field");
  fails += expect_true(strings_equal(panel.blocked_reason,
                                     "password-mask-required"),
                       "credential panel should explain unsafe policy");
  fails += expect_true(panel.submit_allowed == 0 &&
                           panel.auth_attempt_allowed == 0,
                       "credential panel unsafe policy must not allow GUI auth");
  policy.password_mask_required = 1;
  fails += expect_true(login_window_credential_input_apply(
                           NULL, &buffer,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x',
                           &input) == 0,
                       "credential panel blocked input should evaluate");
  fails += expect_true(login_window_credential_panel_build(
                           &policy, &buffer, &input, masked, sizeof(masked),
                           &panel) == 0,
                       "credential panel should build blocked input");
  fails += expect_true(panel.input_result_available == 1 &&
                           panel.input_blocked == 1,
                       "credential panel should expose blocked input");
  fails += expect_true(strings_equal(panel.state, "input-blocked") &&
                           strings_equal(panel.blocked_reason,
                                         "policy-unavailable"),
                       "credential panel should explain blocked input");
  fails += expect_true(panel.submit_allowed == 0 &&
                           panel.auth_attempt_allowed == 0,
                       "credential panel blocked input must not allow GUI auth");
  return fails;
}


static int test_loginwindow_credential_interaction_appends_and_rebuilds_panel(void) {
  int fails = 0;
  char storage[8];
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_interaction interaction;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "credential interaction contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "credential interaction policy should build");
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "credential interaction buffer should initialize");
  fails += expect_true(login_window_credential_interaction_step(
                           &policy, &buffer,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'a',
                           masked, sizeof(masked), &interaction) == 0,
                       "credential interaction should append and rebuild panel");
  fails += expect_true(interaction.version == LOGIN_WINDOW_CREDENTIAL_INTERACTION_VERSION,
                       "credential interaction should expose stable version");
  fails += expect_true(interaction.input_applied == 1 &&
                           interaction.panel_built == 1,
                       "credential interaction should apply input and build panel");
  fails += expect_true(interaction.input_accepted == 1 &&
                           interaction.input_blocked == 0,
                       "credential interaction should expose accepted input");
  fails += expect_true(interaction.buffer_changed == 1 &&
                           interaction.length == 1,
                       "credential interaction should expose changed buffer length");
  fails += expect_true(strings_equal(masked, "*"),
                       "credential interaction should expose only mask");
  fails += expect_true(strings_equal(storage, "a"),
                       "credential interaction should keep raw storage internal");
  fails += expect_true(strings_equal(interaction.state, "editing") &&
                           strings_equal(interaction.blocked_reason, "ready"),
                       "credential interaction should expose editing state");
  fails += expect_true(interaction.submit_allowed == 0 &&
                           interaction.auth_attempt_allowed == 0,
                       "credential interaction append must not allow GUI auth");
  return fails;
}

static int test_loginwindow_credential_interaction_submit_wipes_and_blocks_auth(void) {
  int fails = 0;
  char storage[8];
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_interaction interaction;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "credential interaction submit contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "credential interaction submit policy should build");
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "credential interaction submit buffer should initialize");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'p') == 1,
                       "credential interaction submit should prepare secret");
  fails += expect_true(login_window_credential_interaction_step(
                           &policy, &buffer,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_SUBMIT, '\0',
                           masked, sizeof(masked), &interaction) == 0,
                       "credential interaction submit should wipe and rebuild");
  fails += expect_true(interaction.input_applied == 1 &&
                           interaction.panel_built == 1,
                       "credential interaction submit should finish pipeline");
  fails += expect_true(interaction.submit_attempted == 1 &&
                           interaction.wipe_attempted == 1 &&
                           interaction.wipe_succeeded == 1,
                       "credential interaction submit should expose wipe");
  fails += expect_true(interaction.length == 0 && buffer.wiped == 1,
                       "credential interaction submit should leave buffer wiped");
  fails += expect_true(masked[0] == '\0' && storage[0] == '\0',
                       "credential interaction submit should clear visible and raw storage");
  fails += expect_true(strings_equal(interaction.state, "submit-blocked") &&
                           strings_equal(interaction.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential interaction submit should expose blocked auth");
  fails += expect_true(interaction.submit_allowed == 0 &&
                           interaction.auth_attempt_allowed == 0,
                       "credential interaction submit must not allow GUI auth");
  return fails;
}

static int test_loginwindow_credential_interaction_cancel_wipes_and_rebuilds(void) {
  int fails = 0;
  char storage[8];
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_interaction interaction;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "credential interaction cancel contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "credential interaction cancel policy should build");
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "credential interaction cancel buffer should initialize");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'x') == 1,
                       "credential interaction cancel should prepare secret");
  fails += expect_true(login_window_credential_interaction_step(
                           &policy, &buffer,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, '\0',
                           masked, sizeof(masked), &interaction) == 0,
                       "credential interaction cancel should wipe and rebuild");
  fails += expect_true(interaction.cancel_consumed == 1 &&
                           interaction.wipe_attempted == 1 &&
                           interaction.wipe_succeeded == 1,
                       "credential interaction cancel should expose wipe");
  fails += expect_true(interaction.length == 0 && buffer.wiped == 1,
                       "credential interaction cancel should leave buffer wiped");
  fails += expect_true(strings_equal(interaction.state, "cancelled") &&
                           strings_equal(interaction.blocked_reason,
                                         "cancelled"),
                       "credential interaction cancel should expose cancelled state");
  fails += expect_true(interaction.submit_attempted == 0,
                       "credential interaction cancel must not submit");
  fails += expect_true(interaction.submit_allowed == 0 &&
                           interaction.auth_attempt_allowed == 0,
                       "credential interaction cancel must not allow GUI auth");
  return fails;
}

static int test_loginwindow_credential_interaction_fails_closed_on_missing_policy_and_unknown_action(void) {
  int fails = 0;
  char storage[8];
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_interaction interaction;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "credential interaction blocked contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "credential interaction blocked policy should build");
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "credential interaction blocked buffer should initialize");
  fails += expect_true(login_window_credential_interaction_step(
                           NULL, &buffer,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x',
                           masked, sizeof(masked), &interaction) == 0,
                       "credential interaction should handle missing policy");
  fails += expect_true(interaction.input_applied == 1 &&
                           interaction.panel_built == 1,
                       "credential interaction missing policy should still build audit state");
  fails += expect_true(interaction.input_blocked == 1 &&
                           interaction.buffer_changed == 0,
                       "credential interaction missing policy should block mutation");
  fails += expect_true(strings_equal(interaction.state, "input-blocked") &&
                           strings_equal(interaction.blocked_reason,
                                         "policy-unavailable"),
                       "credential interaction missing policy should explain block");
  fails += expect_true(storage[0] == '\0' && masked[0] == '\0',
                       "credential interaction missing policy should not expose secret");
  fails += expect_true(interaction.submit_allowed == 0 &&
                           interaction.auth_attempt_allowed == 0,
                       "credential interaction missing policy must not allow GUI auth");
  fails += expect_true(login_window_credential_interaction_step(
                           &policy, &buffer, 99, '\0', masked, sizeof(masked),
                           &interaction) == 0,
                       "credential interaction should handle unknown action");
  fails += expect_true(interaction.input_blocked == 1 &&
                           interaction.buffer_changed == 0,
                       "credential interaction unknown action should block mutation");
  fails += expect_true(strings_equal(interaction.state, "input-blocked") &&
                           strings_equal(interaction.blocked_reason,
                                         "input-action-unknown"),
                       "credential interaction unknown action should explain block");
  fails += expect_true(interaction.submit_allowed == 0 &&
                           interaction.auth_attempt_allowed == 0,
                       "credential interaction unknown action must not allow GUI auth");
  return fails;
}

int test_login_runtime_credential_input_view_panel_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_panel_renders_masked_ready_state();
  fails += test_loginwindow_credential_panel_reflects_append_input();
  fails += test_loginwindow_credential_panel_reflects_submit_and_cancel();
  fails += test_loginwindow_credential_panel_fails_closed_on_blocked_field_or_input();
  fails += test_loginwindow_credential_interaction_appends_and_rebuilds_panel();
  fails += test_loginwindow_credential_interaction_submit_wipes_and_blocks_auth();
  fails += test_loginwindow_credential_interaction_cancel_wipes_and_rebuilds();
  fails += test_loginwindow_credential_interaction_fails_closed_on_missing_policy_and_unknown_action();
  return fails;
}
