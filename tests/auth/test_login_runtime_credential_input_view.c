/*
 * tests/auth/test_login_runtime_credential_input_view.c
 *
 * Credential input reducer + field view coverage for the
 * `login_runtime` host test. Originally carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-15 monolith
 * refactor (PR D.3 of the Estagio D dedicated plan), and further
 * split at the 2026-05-16 preventive refactor (the 8
 * credential_panel + credential_interaction tests moved to the
 * sibling
 * `tests/auth/test_login_runtime_credential_input_view_panel.c`)
 * so each host-test translation unit stays comfortably below the
 * 900-line layout limit. Tests in this file exercise:
 *
 *   - `login_window_credential_input_apply`: 4 tests covering the
 *     append/backspace path + the submit wipe + the cancel wipe +
 *     the missing-policy/unknown-action fail-closed default.
 *   - `login_window_credential_field_view_build`: 4 tests covering
 *     the filled buffer mask + the empty/truncated mask + the
 *     missing-policy fail-closed default + the unmasked/overflowed
 *     buffer rejection.
 *
 * The companion entry `test_login_runtime_credential_input_view_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`, which also invokes the new
 * sibling entry
 * `test_login_runtime_credential_input_view_panel_cases`. Shared
 * fixture state and helpers come from
 * `tests/auth/test_login_runtime_internal.h`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

static int test_loginwindow_input_reducer_appends_and_backspaces(void) {
  int fails = 0;
  char storage[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_input_result result;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "input reducer contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "input reducer policy should build");
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "input reducer buffer should initialize");
  fails += expect_true(login_window_credential_input_apply(
                           &policy, &buffer,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'a',
                           &result) == 0,
                       "input reducer should append first character");
  fails += expect_true(result.version == LOGIN_WINDOW_CREDENTIAL_INPUT_RESULT_VERSION,
                       "input reducer should expose stable result version");
  fails += expect_true(result.accepted == 1 && result.buffer_changed == 1,
                       "input reducer append should be accepted and mutate buffer");
  fails += expect_true(result.buffer_length == 1 && strings_equal(storage, "a"),
                       "input reducer append should expose updated length");
  fails += expect_true(result.submit_allowed == 0 &&
                           result.auth_attempt_allowed == 0,
                       "input reducer append must not allow GUI auth");
  fails += expect_true(result.masked_text_required == 1 &&
                           result.text_login_authoritative == 1,
                       "input reducer append should keep mask and text login");
  fails += expect_true(login_window_credential_input_apply(
                           &policy, &buffer,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'b',
                           &result) == 0,
                       "input reducer should append second character");
  fails += expect_true(result.buffer_length == 2 && strings_equal(storage, "ab"),
                       "input reducer second append should update storage");
  fails += expect_true(login_window_credential_input_apply(
                           &policy, &buffer,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_BACKSPACE, '\0',
                           &result) == 0,
                       "input reducer should apply backspace");
  fails += expect_true(result.accepted == 1 && result.buffer_changed == 1,
                       "input reducer backspace should be accepted and mutate buffer");
  fails += expect_true(result.buffer_length == 1 && strings_equal(storage, "a"),
                       "input reducer backspace should expose updated length");
  return fails;
}

static int test_loginwindow_input_reducer_submit_wipes_without_auth(void) {
  int fails = 0;
  char storage[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_input_result result;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "input reducer submit contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "input reducer submit policy should build");
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "input reducer submit buffer should initialize");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'p') == 1,
                       "input reducer submit should prepare first byte");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'w') == 1,
                       "input reducer submit should prepare second byte");
  fails += expect_true(login_window_credential_input_apply(
                           &policy, &buffer,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_SUBMIT, '\0',
                           &result) == 0,
                       "input reducer submit should consume attempt");
  fails += expect_true(result.submit_attempted == 1,
                       "input reducer submit should mark attempt");
  fails += expect_true(result.wipe_attempted == 1 && result.wipe_succeeded == 1,
                       "input reducer submit should wipe buffer");
  fails += expect_true(result.submit_allowed == 0 &&
                           result.auth_attempt_allowed == 0,
                       "input reducer submit must not allow GUI auth");
  fails += expect_true(strings_equal(result.blocked_reason,
                                     "gui-submit-disabled"),
                       "input reducer submit should expose gate reason");
  fails += expect_true(result.buffer_length == 0 && buffer.wiped == 1,
                       "input reducer submit should leave buffer wiped");
  fails += expect_true(storage[0] == '\0' && storage[1] == '\0',
                       "input reducer submit should clear storage");
  return fails;
}

static int test_loginwindow_input_reducer_cancel_wipes_without_submit(void) {
  int fails = 0;
  char storage[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_input_result result;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "input reducer cancel contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "input reducer cancel policy should build");
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "input reducer cancel buffer should initialize");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'x') == 1,
                       "input reducer cancel should prepare secret");
  fails += expect_true(login_window_credential_input_apply(
                           &policy, &buffer,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, '\0',
                           &result) == 0,
                       "input reducer cancel should consume cancel");
  fails += expect_true(result.accepted == 1 && result.cancel_consumed == 1,
                       "input reducer cancel should be consumed");
  fails += expect_true(result.submit_attempted == 0,
                       "input reducer cancel must not submit");
  fails += expect_true(result.wipe_attempted == 1 && result.wipe_succeeded == 1,
                       "input reducer cancel should wipe buffer");
  fails += expect_true(strings_equal(result.blocked_reason, "cancelled"),
                       "input reducer cancel should expose cancelled state");
  fails += expect_true(result.buffer_length == 0 && buffer.wiped == 1,
                       "input reducer cancel should leave buffer wiped");
  return fails;
}

static int test_loginwindow_input_reducer_rejects_missing_policy_and_unknown_action(void) {
  int fails = 0;
  char storage[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_input_result result;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "input reducer reject contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "input reducer reject policy should build");
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "input reducer reject buffer should initialize");
  fails += expect_true(login_window_credential_input_apply(
                           NULL, &buffer,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x',
                           &result) == 0,
                       "input reducer should evaluate missing policy");
  fails += expect_true(result.accepted == 0 && result.buffer_changed == 0,
                       "input reducer missing policy should not mutate buffer");
  fails += expect_true(strings_equal(result.blocked_reason,
                                     "policy-unavailable"),
                       "input reducer missing policy should explain block");
  fails += expect_true(buffer.length == 0 && storage[0] == '\0',
                       "input reducer missing policy should keep buffer empty");
  fails += expect_true(login_window_credential_input_apply(
                           &policy, &buffer, 99, '\0', &result) == 0,
                       "input reducer should evaluate unknown action");
  fails += expect_true(result.accepted == 0 && result.buffer_changed == 0,
                       "input reducer unknown action should not mutate buffer");
  fails += expect_true(strings_equal(result.blocked_reason,
                                     "input-action-unknown"),
                       "input reducer unknown action should explain block");
  fails += expect_true(result.submit_allowed == 0 &&
                           result.auth_attempt_allowed == 0,
                       "input reducer unknown action must not allow GUI auth");
  return fails;
}


static int test_loginwindow_field_view_masks_filled_buffer(void) {
  int fails = 0;
  char storage[8];
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_field_view view;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "field view contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "field view policy should build");
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "field view buffer should initialize");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'a') == 1,
                       "field view should append first secret byte");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'b') == 1,
                       "field view should append second secret byte");
  fails += expect_true(login_window_credential_field_view_build(
                           &policy, &buffer, masked, sizeof(masked),
                           &view) == 0,
                       "field view should build for filled buffer");
  fails += expect_true(view.version == LOGIN_WINDOW_CREDENTIAL_FIELD_VIEW_VERSION,
                       "field view should expose stable version");
  fails += expect_true(view.policy_available == 1 && view.field_allowed == 1,
                       "field view should expose available policy and field");
  fails += expect_true(view.buffer_available == 1 && view.buffer_initialized == 1,
                       "field view should expose available initialized buffer");
  fails += expect_true(view.has_secret == 1 && view.length == 2,
                       "field view should expose secret length only");
  fails += expect_true(view.masked_text_available == 1 &&
                           view.masked_text_truncated == 0,
                       "field view should expose complete masked text");
  fails += expect_true(strings_equal(masked, "**"),
                       "field view should expose only masked characters");
  fails += expect_true(strings_equal(storage, "ab"),
                       "field view should not mutate raw storage");
  fails += expect_true(view.submit_allowed == 0 &&
                           view.auth_attempt_allowed == 0,
                       "field view must not allow GUI auth");
  fails += expect_true(strings_equal(view.state, "filled") &&
                           strings_equal(view.blocked_reason, "ready"),
                       "field view should expose filled ready state");
  return fails;
}

static int test_loginwindow_field_view_handles_empty_and_truncated_masks(void) {
  int fails = 0;
  char storage[8];
  char masked[2];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_field_view view;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "empty field view contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "empty field view policy should build");
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "empty field view buffer should initialize");
  fails += expect_true(login_window_credential_field_view_build(
                           &policy, &buffer, masked, sizeof(masked),
                           &view) == 0,
                       "field view should build for empty buffer");
  fails += expect_true(view.has_secret == 0 && view.length == 0,
                       "field view empty should expose no secret");
  fails += expect_true(view.masked_text_available == 1 && masked[0] == '\0',
                       "field view empty should expose empty mask");
  fails += expect_true(strings_equal(view.state, "empty") &&
                           strings_equal(view.blocked_reason, "ready"),
                       "field view empty should expose ready empty state");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'a') == 1,
                       "truncated field view should append first secret byte");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'b') == 1,
                       "truncated field view should append second secret byte");
  fails += expect_true(login_window_credential_field_view_build(
                           &policy, &buffer, masked, sizeof(masked),
                           &view) == 0,
                       "field view should build truncated mask");
  fails += expect_true(view.masked_text_available == 1 &&
                           view.masked_text_truncated == 1,
                       "field view should mark truncated mask");
  fails += expect_true(strings_equal(masked, "*"),
                       "field view truncated output should remain masked");
  fails += expect_true(strings_equal(view.blocked_reason,
                                     "masked-text-truncated"),
                       "field view should explain truncated mask");
  fails += expect_true(view.submit_allowed == 0 &&
                           view.auth_attempt_allowed == 0,
                       "field view truncated must not allow GUI auth");
  return fails;
}

static int test_loginwindow_field_view_fails_closed_without_policy_or_output(void) {
  int fails = 0;
  char masked[4] = {'x', 'x', 'x', '\0'};
  struct login_window_credential_field_view view;

  reset_test_state();
  fails += expect_true(login_window_credential_field_view_build(
                           NULL, NULL, masked, sizeof(masked), &view) == 0,
                       "field view should evaluate missing policy");
  fails += expect_true(view.policy_available == 0 && view.field_allowed == 0,
                       "field view missing policy should expose blocked policy");
  fails += expect_true(view.submit_allowed == 0 &&
                           view.auth_attempt_allowed == 0,
                       "field view missing policy must not allow GUI auth");
  fails += expect_true(masked[0] == '\0',
                       "field view missing policy should clear output");
  fails += expect_true(strings_equal(view.blocked_reason,
                                     "policy-unavailable"),
                       "field view missing policy should explain block");
  return fails;
}

static int test_loginwindow_field_view_rejects_unmasked_or_overflowed_buffer(void) {
  int fails = 0;
  char storage[8];
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_field_view view;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "blocked field view contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "blocked field view policy should build");
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "blocked field view buffer should initialize");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'a') == 1,
                       "blocked field view should append secret byte");
  buffer.masked = 0;
  fails += expect_true(login_window_credential_field_view_build(
                           &policy, &buffer, masked, sizeof(masked),
                           &view) == 0,
                       "field view should evaluate unmasked buffer");
  fails += expect_true(view.masked_text_available == 0 && masked[0] == '\0',
                       "field view unmasked should not expose text");
  fails += expect_true(strings_equal(view.blocked_reason, "buffer-unmasked"),
                       "field view should explain unmasked buffer");
  buffer.masked = 1;
  buffer.overflow_blocked = 1;
  fails += expect_true(login_window_credential_field_view_build(
                           &policy, &buffer, masked, sizeof(masked),
                           &view) == 0,
                       "field view should evaluate overflowed buffer");
  fails += expect_true(view.masked_text_available == 1,
                       "field view overflow should still expose masked text");
  fails += expect_true(strings_equal(view.blocked_reason,
                                     "credential-overflow-blocked"),
                       "field view should explain overflowed buffer");
  fails += expect_true(view.submit_allowed == 0 &&
                           view.auth_attempt_allowed == 0,
                       "field view overflow must not allow GUI auth");
  fails += expect_true(login_window_credential_buffer_wipe(&buffer) == 0,
                       "blocked field view test should wipe buffer");
  return fails;
}


int test_login_runtime_credential_input_view_cases(void) {
  int fails = 0;
  fails += test_loginwindow_input_reducer_appends_and_backspaces();
  fails += test_loginwindow_input_reducer_submit_wipes_without_auth();
  fails += test_loginwindow_input_reducer_cancel_wipes_without_submit();
  fails += test_loginwindow_input_reducer_rejects_missing_policy_and_unknown_action();
  fails += test_loginwindow_field_view_masks_filled_buffer();
  fails += test_loginwindow_field_view_handles_empty_and_truncated_masks();
  fails += test_loginwindow_field_view_fails_closed_without_policy_or_output();
  fails += test_loginwindow_field_view_rejects_unmasked_or_overflowed_buffer();
  return fails;
}
