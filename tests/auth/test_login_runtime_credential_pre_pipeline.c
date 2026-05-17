/*
 * tests/auth/test_login_runtime_credential_pre_pipeline.c
 *
 * Credential pre-pipeline coverage for the `login_runtime` host
 * test. Carved out of `tests/auth/test_login_runtime.c` at the
 * 2026-05-15 monolith refactor (PR D.2 of the Estagio D dedicated
 * plan) so each host-test translation unit stays under the 900-line
 * layout limit. Tests in this file exercise:
 *
 *   - `login_window_credential_policy_from_contract`: 4 tests
 *     covering the missing-contract fail-closed default + the ready
 *     contract + the maintenance contract + the incomplete runtime
 *     downgrade.
 *   - `login_window_credential_buffer_*`: 3 tests covering the
 *     missing-policy fail-closed default + the ready masked buffer
 *     append/backspace/wipe path + the bounded overflow rejection.
 *   - `login_window_credential_submit_gate_*`: 3 tests covering the
 *     missing-policy fail-closed default + the ready filled buffer
 *     gate evaluation + the empty/unmasked/overflowed buffer
 *     blockers.
 *   - `login_window_credential_submit_attempt_consume`: 5 tests
 *     covering the ready filled wipe path + the missing-policy
 *     wipe + the overflowed buffer wipe + the unsafe policy wipe +
 *     the missing buffer fail-closed default.
 *
 * The companion entry `test_login_runtime_credential_pre_pipeline_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`. Shared fixture state and helpers
 * come from `tests/auth/test_login_runtime_internal.h`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

static int test_loginwindow_policy_fail_closed_without_contract(void) {
  int fails = 0;
  struct login_window_credential_policy policy;

  reset_test_state();
  fails += expect_true(login_window_credential_policy_from_contract(NULL,
                                                                    &policy) == 0,
                       "credential policy should accept missing contract");
  fails += expect_true(policy.version == LOGIN_WINDOW_CREDENTIAL_POLICY_VERSION,
                       "credential policy should expose stable version");
  fails += expect_true(policy.max_password_chars == LOGIN_WINDOW_PASSWORD_MAX_CHARS,
                       "credential policy should cap password length");
  fails += expect_true(policy.mask_char == LOGIN_WINDOW_PASSWORD_MASK_CHAR,
                       "credential policy should expose mask character");
  fails += expect_true(policy.password_field_allowed == 0,
                       "missing contract must not allow password field");
  fails += expect_true(policy.password_submit_allowed == 0,
                       "missing contract must not allow graphical submit");
  fails += expect_true(policy.password_mask_required == 1,
                       "password masking should be required by default");
  fails += expect_true(policy.password_wipe_required == 1,
                       "credential wipe should be required by default");
  fails += expect_true(policy.recovery_requires_text_session == 1,
                       "recovery should default to text-session-only");
  fails += expect_true(policy.text_login_authoritative == 1,
                       "text login should remain authoritative");
  return fails;
}

static int test_loginwindow_ready_policy_keeps_gui_submit_disabled(void) {
  int fails = 0;
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_view_model model;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "ready contract should evaluate");
  fails += expect_true(contract.ready == 1,
                       "complete runtime should make loginwindow contract ready");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "ready contract should build credential policy");
  fails += expect_true(policy.password_field_allowed == 1,
                       "ready contract may render password field");
  fails += expect_true(policy.password_submit_allowed == 0,
                       "GUI password submit must remain disabled");
  fails += expect_true(policy.password_mask_required == 1,
                       "ready password field should be masked");
  fails += expect_true(policy.password_wipe_required == 1,
                       "ready password buffer should require wipe");
  fails += expect_true(policy.text_login_authoritative == 1,
                       "ready policy should keep text login authoritative");
  fails += expect_true(login_window_view_model_build(&contract, "en",
                                                     &model) == 0,
                       "ready view model should build");
  fails += expect_true(model.password_enabled == 1,
                       "ready view model should show password field");
  fails += expect_true(model.password_submit_enabled == 0,
                       "ready view model must keep graphical submit disabled");
  fails += expect_true(model.password_masked == 1,
                       "ready view model should expose masked password field");
  fails += expect_true(model.credential_wipe_required == 1,
                       "ready view model should require credential wipe");
  fails += expect_true(model.text_login_authoritative == 1,
                       "ready view model should keep text login authoritative");
  fails += expect_true(model.fallback_required == 1,
                       "ready view model should still require text login while GUI submit is disabled");
  return fails;
}

static int test_loginwindow_recovery_policy_stays_text_only(void) {
  int fails = 0;
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_view_model model;

  reset_test_state();
  ops = build_ops();
  ops.maintenance_mode = 1;
  g_runtime_maintenance_active = 1;
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "maintenance contract should evaluate");
  fails += expect_true(contract.ready == 0,
                       "maintenance contract should not be login-ready");
  fails += expect_true(contract.maintenance_mode == 1,
                       "maintenance contract should report maintenance mode");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "maintenance contract should build credential policy");
  fails += expect_true(policy.password_field_allowed == 0,
                       "maintenance policy must not show normal password field");
  fails += expect_true(policy.password_submit_allowed == 0,
                       "maintenance policy must not allow graphical submit");
  fails += expect_true(policy.recovery_allowed == 1,
                       "maintenance policy should allow text recovery action");
  fails += expect_true(policy.recovery_requires_text_session == 1,
                       "maintenance recovery should stay text-session-only");
  fails += expect_true(login_window_view_model_build(&contract, "en",
                                                     &model) == 0,
                       "maintenance view model should build");
  fails += expect_true(model.recovery_enabled == 1,
                       "maintenance view model should expose recovery action");
  fails += expect_true(model.maintenance_notice == 1,
                       "maintenance view model should expose notice");
  fails += expect_true(model.password_enabled == 0,
                       "maintenance view model must not show normal password field");
  fails += expect_true(model.password_submit_enabled == 0,
                       "maintenance view model must not allow graphical submit");
  return fails;
}

static int test_loginwindow_recovery_policy_requires_complete_runtime(void) {
  int fails = 0;
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_view_model model;

  reset_test_state();
  ops = build_ops();
  ops.maintenance_mode = 1;
  ops.putc = NULL;
  g_runtime_maintenance_active = 1;
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "incomplete maintenance contract should evaluate");
  fails += expect_true(contract.ui_callbacks_ready == 0,
                       "incomplete maintenance contract should expose missing UI callback");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "incomplete maintenance contract should build credential policy");
  fails += expect_true(policy.recovery_allowed == 0,
                       "incomplete runtime must not expose recovery action");
  fails += expect_true(policy.password_submit_allowed == 0,
                       "incomplete runtime must not allow graphical submit");
  fails += expect_true(login_window_view_model_build(&contract, "en",
                                                     &model) == 0,
                       "incomplete maintenance view model should build");
  fails += expect_true(model.recovery_enabled == 0,
                       "incomplete maintenance view model must not expose recovery");
  fails += expect_true(model.renderable == 0,
                       "incomplete maintenance view model should remain non-renderable");
  return fails;
}


static int test_loginwindow_credential_buffer_fail_closed_without_policy(void) {
  int fails = 0;
  char storage[8] = {'s', 'e', 'c', 'r', 'e', 't', '!', '\0'};
  char masked[8];
  struct login_window_credential_buffer buffer;

  reset_test_state();
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          NULL) == 0,
                       "credential buffer should accept missing policy");
  fails += expect_true(buffer.version == LOGIN_WINDOW_CREDENTIAL_BUFFER_VERSION,
                       "credential buffer should expose stable version");
  fails += expect_true(buffer.initialized == 0,
                       "credential buffer without policy must not initialize");
  fails += expect_true(buffer.submit_allowed == 0,
                       "credential buffer without policy must not allow submit");
  fails += expect_true(buffer.wipe_required == 1,
                       "credential buffer should require wipe by default");
  fails += expect_true(storage[0] == '\0' && storage[1] == '\0' &&
                           storage[2] == '\0',
                       "credential buffer init should clear stale storage");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'x') == 0,
                       "credential buffer without policy must reject append");
  fails += expect_true(login_window_credential_buffer_masked_text(&buffer,
                                                                  masked,
                                                                  sizeof(masked)) == 0,
                       "credential buffer should mask missing policy safely");
  fails += expect_true(masked[0] == '\0',
                       "credential buffer without policy should render empty mask");
  return fails;
}

static int test_loginwindow_credential_buffer_masks_backspaces_and_wipes(void) {
  int fails = 0;
  char storage[8];
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "ready contract should evaluate for buffer");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "ready policy should build for buffer");
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "ready policy should initialize credential buffer");
  fails += expect_true(buffer.initialized == 1,
                       "ready credential buffer should initialize");
  fails += expect_true(buffer.masked == 1 && buffer.wipe_required == 1,
                       "ready credential buffer should require mask and wipe");
  fails += expect_true(buffer.wiped == 1,
                       "ready credential buffer should start wiped after init");
  fails += expect_true(buffer.submit_allowed == 0,
                       "ready credential buffer must not allow GUI submit");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'a') == 1,
                       "credential buffer should append first character");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'b') == 1,
                       "credential buffer should append second character");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'c') == 1,
                       "credential buffer should append third character");
  fails += expect_true(login_window_credential_buffer_masked_text(&buffer,
                                                                  masked,
                                                                  sizeof(masked)) == 0,
                       "credential buffer should build masked text");
  fails += expect_true(strings_equal(masked, "***"),
                       "credential buffer should mask every stored character");
  fails += expect_true(strings_equal(storage, "abc"),
                       "credential buffer should keep raw storage internal only");
  fails += expect_true(login_window_credential_buffer_backspace(&buffer) == 1,
                       "credential buffer should backspace one character");
  fails += expect_true(strings_equal(storage, "ab"),
                       "credential buffer should clear removed character");
  fails += expect_true(login_window_credential_buffer_wipe(&buffer) == 0,
                       "credential buffer wipe should succeed");
  fails += expect_true(buffer.length == 0 && buffer.wiped == 1,
                       "credential buffer wipe should reset length and mark wiped");
  fails += expect_true(storage[0] == '\0' && storage[1] == '\0' &&
                           storage[2] == '\0',
                       "credential buffer wipe should clear stored secret bytes");
  return fails;
}

static int test_loginwindow_credential_buffer_blocks_overflow(void) {
  int fails = 0;
  char storage[8];
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "ready contract should evaluate for overflow");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "ready policy should build for overflow");
  policy.max_password_chars = 3;
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "overflow policy should initialize buffer");
  fails += expect_true(buffer.max_chars == 3,
                       "credential buffer should honor policy max length");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'a') == 1,
                       "credential buffer should append first bounded char");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'b') == 1,
                       "credential buffer should append second bounded char");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'c') == 1,
                       "credential buffer should append third bounded char");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'd') == 0,
                       "credential buffer should block overflow char");
  fails += expect_true(buffer.length == 3 && buffer.overflow_blocked == 1,
                       "credential buffer should keep length stable on overflow");
  fails += expect_true(strings_equal(storage, "abc"),
                       "credential buffer overflow should not mutate storage");
  fails += expect_true(login_window_credential_buffer_masked_text(&buffer,
                                                                  masked,
                                                                  sizeof(masked)) == 0,
                       "credential buffer should mask bounded text");
  fails += expect_true(strings_equal(masked, "***"),
                       "credential buffer should keep bounded mask length");
  fails += expect_true(strings_equal(buffer.blocked_reason,
                                     "max-password-chars"),
                       "credential buffer should expose overflow reason");
  return fails;
}


static int test_loginwindow_submit_gate_fails_closed_without_policy(void) {
  int fails = 0;
  struct login_window_credential_submit_gate gate;

  reset_test_state();
  fails += expect_true(login_window_credential_submit_gate_evaluate(NULL, NULL,
                                                                   &gate) == 0,
                       "submit gate should evaluate missing policy");
  fails += expect_true(gate.version == LOGIN_WINDOW_CREDENTIAL_SUBMIT_GATE_VERSION,
                       "submit gate should expose stable version");
  fails += expect_true(gate.policy_available == 0,
                       "submit gate should expose missing policy");
  fails += expect_true(gate.submit_allowed == 0,
                       "submit gate must reject missing policy");
  fails += expect_true(gate.auth_attempt_allowed == 0,
                       "submit gate must not allow auth without policy");
  fails += expect_true(gate.wipe_required == 1,
                       "submit gate should require wipe by default");
  fails += expect_true(strings_equal(gate.blocked_reason, "policy-unavailable"),
                       "submit gate should explain missing policy");
  return fails;
}

static int test_loginwindow_submit_gate_rejects_filled_buffer(void) {
  int fails = 0;
  char storage[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_submit_gate gate;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "ready contract should evaluate for submit gate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "ready policy should build for submit gate");
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "ready buffer should initialize for submit gate");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'p') == 1,
                       "submit gate buffer should append first secret byte");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'w') == 1,
                       "submit gate buffer should append second secret byte");
  fails += expect_true(login_window_credential_submit_gate_evaluate(&policy,
                                                                    &buffer,
                                                                    &gate) == 0,
                       "submit gate should evaluate filled buffer");
  fails += expect_true(gate.policy_available == 1 && gate.buffer_available == 1,
                       "submit gate should expose available policy and buffer");
  fails += expect_true(gate.buffer_initialized == 1 && gate.buffer_has_secret == 1,
                       "submit gate should expose filled initialized buffer");
  fails += expect_true(gate.buffer_masked == 1 && gate.wipe_required == 1,
                       "submit gate should require masked wipeable buffer");
  fails += expect_true(gate.policy_submit_allowed == 0 &&
                           gate.buffer_submit_allowed == 0,
                       "submit gate should expose submit disabled by policy and buffer");
  fails += expect_true(gate.submit_allowed == 0 &&
                           gate.auth_attempt_allowed == 0,
                       "submit gate must reject GUI authentication attempt");
  fails += expect_true(gate.text_login_authoritative == 1,
                       "submit gate should keep text login authoritative");
  fails += expect_true(strings_equal(gate.blocked_reason,
                                     "gui-submit-disabled"),
                       "submit gate should explain disabled GUI submit");
  fails += expect_true(login_window_credential_buffer_wipe(&buffer) == 0,
                       "submit gate test should wipe filled buffer");
  return fails;
}

static int test_loginwindow_submit_gate_rejects_empty_overflow_and_unmasked(void) {
  int fails = 0;
  char storage[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_submit_gate gate;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "ready contract should evaluate for submit gate blockers");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "ready policy should build for submit gate blockers");
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "ready buffer should initialize for submit gate blockers");
  fails += expect_true(login_window_credential_submit_gate_evaluate(&policy,
                                                                    &buffer,
                                                                    &gate) == 0,
                       "submit gate should evaluate empty buffer");
  fails += expect_true(strings_equal(gate.blocked_reason, "credential-empty"),
                       "submit gate should reject empty credential");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'a') == 1,
                       "submit gate blocker buffer should append first char");
  buffer.masked = 0;
  fails += expect_true(login_window_credential_submit_gate_evaluate(&policy,
                                                                    &buffer,
                                                                    &gate) == 0,
                       "submit gate should evaluate unmasked buffer");
  fails += expect_true(strings_equal(gate.blocked_reason, "buffer-unmasked"),
                       "submit gate should reject unmasked buffer");
  buffer.masked = 1;
  buffer.overflow_blocked = 1;
  fails += expect_true(login_window_credential_submit_gate_evaluate(&policy,
                                                                    &buffer,
                                                                    &gate) == 0,
                       "submit gate should evaluate overflowed buffer");
  fails += expect_true(strings_equal(gate.blocked_reason,
                                     "credential-overflow-blocked"),
                       "submit gate should reject overflowed buffer");
  fails += expect_true(gate.submit_allowed == 0 &&
                           gate.auth_attempt_allowed == 0,
                       "submit gate blockers must not allow auth");
  fails += expect_true(login_window_credential_buffer_wipe(&buffer) == 0,
                       "submit gate blocker test should wipe buffer");
  return fails;
}


static int test_loginwindow_submit_attempt_wipes_filled_buffer(void) {
  int fails = 0;
  char storage[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_submit_attempt attempt;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "ready contract should evaluate for submit attempt");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "ready policy should build for submit attempt");
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "ready buffer should initialize for submit attempt");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'p') == 1,
                       "submit attempt buffer should append first secret byte");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'w') == 1,
                       "submit attempt buffer should append second secret byte");
  fails += expect_true(login_window_credential_submit_attempt_consume(
                           &policy, &buffer, &attempt) == 0,
                       "submit attempt should consume filled buffer");
  fails += expect_true(attempt.version ==
                           LOGIN_WINDOW_CREDENTIAL_SUBMIT_ATTEMPT_VERSION,
                       "submit attempt should expose stable version");
  fails += expect_true(attempt.attempted == 1 && attempt.gate_evaluated == 1,
                       "submit attempt should evaluate gate");
  fails += expect_true(attempt.buffer_had_secret == 1,
                       "submit attempt should remember that buffer had secret");
  fails += expect_true(attempt.submit_allowed == 0 &&
                           attempt.auth_attempt_allowed == 0,
                       "submit attempt must not allow GUI authentication");
  fails += expect_true(attempt.wipe_attempted == 1 &&
                           attempt.wipe_succeeded == 1,
                       "submit attempt should wipe filled buffer");
  fails += expect_true(strings_equal(attempt.blocked_reason,
                                     "gui-submit-disabled"),
                       "submit attempt should explain GUI submit block");
  fails += expect_true(buffer.length == 0 && buffer.wiped == 1,
                       "submit attempt should leave buffer wiped");
  fails += expect_true(storage[0] == '\0' && storage[1] == '\0',
                       "submit attempt should clear secret storage");
  return fails;
}

static int test_loginwindow_submit_attempt_wipes_without_policy(void) {
  int fails = 0;
  char storage[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_submit_attempt attempt;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "ready contract should evaluate for missing policy attempt");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "ready policy should build before missing policy attempt");
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "ready buffer should initialize before missing policy attempt");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'x') == 1,
                       "missing policy attempt buffer should append secret");
  fails += expect_true(login_window_credential_submit_attempt_consume(
                           NULL, &buffer, &attempt) == 0,
                       "submit attempt should consume missing policy with buffer");
  fails += expect_true(attempt.gate_evaluated == 1,
                       "missing policy attempt should evaluate gate");
  fails += expect_true(strings_equal(attempt.blocked_reason,
                                     "policy-unavailable"),
                       "missing policy attempt should explain block");
  fails += expect_true(attempt.wipe_attempted == 1 &&
                           attempt.wipe_succeeded == 1,
                       "missing policy attempt should still wipe buffer");
  fails += expect_true(buffer.length == 0 && buffer.wiped == 1,
                       "missing policy attempt should leave buffer wiped");
  return fails;
}

static int test_loginwindow_submit_attempt_wipes_overflowed_buffer(void) {
  int fails = 0;
  char storage[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_submit_attempt attempt;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "ready contract should evaluate for overflow submit attempt");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "ready policy should build for overflow submit attempt");
  policy.max_password_chars = 2;
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "overflow attempt buffer should initialize");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'a') == 1,
                       "overflow attempt should append first char");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'b') == 1,
                       "overflow attempt should append second char");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'c') == 0,
                       "overflow attempt should block third char");
  fails += expect_true(login_window_credential_submit_attempt_consume(
                           &policy, &buffer, &attempt) == 0,
                       "submit attempt should consume overflowed buffer");
  fails += expect_true(strings_equal(attempt.blocked_reason,
                                     "credential-overflow-blocked"),
                       "overflow submit attempt should explain block");
  fails += expect_true(attempt.wipe_attempted == 1 &&
                           attempt.wipe_succeeded == 1,
                       "overflow submit attempt should wipe buffer");
  fails += expect_true(buffer.length == 0 && buffer.overflow_blocked == 0 &&
                           buffer.wiped == 1,
                       "overflow submit attempt should reset buffer state");
  fails += expect_true(storage[0] == '\0' && storage[1] == '\0',
                       "overflow submit attempt should clear storage");
  return fails;
}

static int test_loginwindow_submit_attempt_wipes_even_with_unsafe_policy(void) {
  int fails = 0;
  char storage[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_submit_attempt attempt;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "ready contract should evaluate for unsafe policy attempt");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "ready policy should build before unsafe policy attempt");
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "ready buffer should initialize before unsafe policy attempt");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'z') == 1,
                       "unsafe policy attempt buffer should append secret");
  policy.password_wipe_required = 0;
  fails += expect_true(login_window_credential_submit_attempt_consume(
                           &policy, &buffer, &attempt) == 0,
                       "submit attempt should consume unsafe policy with buffer");
  fails += expect_true(strings_equal(attempt.blocked_reason,
                                     "credential-wipe-required"),
                       "unsafe policy attempt should explain missing wipe policy");
  fails += expect_true(attempt.wipe_required == 1,
                       "unsafe policy attempt should still require wipe");
  fails += expect_true(attempt.wipe_attempted == 1 &&
                           attempt.wipe_succeeded == 1,
                       "unsafe policy attempt should still wipe buffer");
  fails += expect_true(buffer.length == 0 && buffer.wiped == 1,
                       "unsafe policy attempt should leave buffer wiped");
  return fails;
}

static int test_loginwindow_submit_attempt_handles_missing_buffer(void) {
  int fails = 0;
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_submit_attempt attempt;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "ready contract should evaluate for missing buffer attempt");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "ready policy should build for missing buffer attempt");
  fails += expect_true(login_window_credential_submit_attempt_consume(
                           &policy, NULL, &attempt) == 0,
                       "submit attempt should handle missing buffer");
  fails += expect_true(attempt.gate_evaluated == 1,
                       "missing buffer attempt should evaluate gate");
  fails += expect_true(attempt.buffer_had_secret == 0,
                       "missing buffer attempt should expose no secret");
  fails += expect_true(attempt.wipe_required == 1,
                       "missing buffer attempt should preserve wipe requirement");
  fails += expect_true(attempt.wipe_attempted == 0 &&
                           attempt.wipe_succeeded == 0,
                       "missing buffer attempt should not claim wipe success");
  fails += expect_true(attempt.submit_allowed == 0 &&
                           attempt.auth_attempt_allowed == 0,
                       "missing buffer attempt must not allow GUI auth");
  fails += expect_true(strings_equal(attempt.blocked_reason,
                                     "buffer-unavailable"),
                       "missing buffer attempt should explain block");
  return fails;
}

int test_login_runtime_credential_pre_pipeline_cases(void) {
  int fails = 0;
  fails += test_loginwindow_policy_fail_closed_without_contract();
  fails += test_loginwindow_ready_policy_keeps_gui_submit_disabled();
  fails += test_loginwindow_recovery_policy_stays_text_only();
  fails += test_loginwindow_recovery_policy_requires_complete_runtime();
  fails += test_loginwindow_credential_buffer_fail_closed_without_policy();
  fails += test_loginwindow_credential_buffer_masks_backspaces_and_wipes();
  fails += test_loginwindow_credential_buffer_blocks_overflow();
  fails += test_loginwindow_submit_gate_fails_closed_without_policy();
  fails += test_loginwindow_submit_gate_rejects_filled_buffer();
  fails += test_loginwindow_submit_gate_rejects_empty_overflow_and_unmasked();
  fails += test_loginwindow_submit_attempt_wipes_filled_buffer();
  fails += test_loginwindow_submit_attempt_wipes_without_policy();
  fails += test_loginwindow_submit_attempt_wipes_overflowed_buffer();
  fails += test_loginwindow_submit_attempt_wipes_even_with_unsafe_policy();
  fails += test_loginwindow_submit_attempt_handles_missing_buffer();
  return fails;
}
