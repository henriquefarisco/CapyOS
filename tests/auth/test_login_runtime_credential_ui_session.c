/*
 * tests/auth/test_login_runtime_credential_ui_session.c
 *
 * Credential UI step + UI session + recovery view-model coverage
 * for the `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-15 monolith
 * refactor (PR D.5 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_ui_step_build`: 4 tests covering the
 *     append/compose pipeline + the submit wipe + the cancel wipe +
 *     the unknown-action fail-closed default.
 *   - `login_window_credential_ui_session_build`: 4 tests covering
 *     the append/wipe storage path + the empty-submit wipe + the
 *     cancel wipe + the missing-storage fail-closed default.
 *   - `login_window_credential_recovery_view_model_build`: 4 tests
 *     covering the text-recovery-only mode + the resume-ready policy
 *     + the unsafe-session block + the unsafe-policy block.
 *
 * The companion entry `test_login_runtime_credential_ui_session_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`. Shared fixture state and helpers
 * come from `tests/auth/test_login_runtime_internal.h`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

static int test_loginwindow_credential_ui_step_appends_and_composes_view(void) {
  int fails = 0;
  char storage[8];
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_ui_step step;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "credential ui step append contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "credential ui step append policy should build");
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "credential ui step append buffer should initialize");
  fails += expect_true(login_window_credential_ui_step_build(
                           &policy, &buffer,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'a',
                           masked, sizeof(masked), &step) == 0,
                       "credential ui step append should compose pipeline");
  fails += expect_true(step.version == LOGIN_WINDOW_CREDENTIAL_UI_STEP_VERSION,
                       "credential ui step should expose stable version");
  fails += expect_true(step.interaction_built == 1 &&
                           step.readiness_built == 1 &&
                           step.audit_built == 1 &&
                           step.view_built == 1,
                       "credential ui step append should build all stages");
  fails += expect_true(step.input_applied == 1 &&
                           step.input_accepted == 1 &&
                           step.buffer_changed == 1,
                       "credential ui step append should apply accepted input");
  fails += expect_true(step.renderable == 1 && step.input_enabled == 1,
                       "credential ui step append should render enabled field");
  fails += expect_true(step.credential_present == 1 &&
                           step.credential_redacted == 1 &&
                           step.length_redacted == 1,
                       "credential ui step append should redact credential metadata");
  fails += expect_true(step.raw_secret_exposed == 0 &&
                           step.masked_text_exposed == 0,
                       "credential ui step append must not expose secret or mask");
  fails += expect_true(step.submit_blocked == 1 &&
                           step.submit_enabled == 0 &&
                           step.auth_attempt_allowed == 0,
                       "credential ui step append must keep GUI auth blocked");
  fails += expect_true(strings_equal(masked, "*") && strings_equal(storage, "a"),
                       "credential ui step append should keep only mask outside storage");
  fails += expect_true(strings_equal(step.event_type,
                                     "credential-input-accepted") &&
                           strings_equal(step.state, "editing"),
                       "credential ui step append should expose composed editing event");
  return fails;
}

static int test_loginwindow_credential_ui_step_submit_wipes_and_composes_view(void) {
  int fails = 0;
  char storage[8];
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_ui_step step;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "credential ui step submit contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "credential ui step submit policy should build");
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "credential ui step submit buffer should initialize");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'p') == 1,
                       "credential ui step submit should prepare secret");
  fails += expect_true(login_window_credential_ui_step_build(
                           &policy, &buffer,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_SUBMIT, '\0',
                           masked, sizeof(masked), &step) == 0,
                       "credential ui step submit should compose pipeline");
  fails += expect_true(step.interaction_built == 1 &&
                           step.readiness_built == 1 &&
                           step.audit_built == 1 &&
                           step.view_built == 1,
                       "credential ui step submit should build all stages");
  fails += expect_true(step.input_applied == 1 &&
                           step.input_accepted == 0 &&
                           step.input_blocked == 1,
                       "credential ui step submit should expose blocked input result");
  fails += expect_true(step.wipe_attempted == 1 &&
                           step.wipe_succeeded == 1 &&
                           step.credential_present == 0,
                       "credential ui step submit should wipe and redact credential");
  fails += expect_true(step.renderable == 1 && step.input_enabled == 0,
                       "credential ui step submit should render diagnostics only");
  fails += expect_true(step.submit_blocked == 1 &&
                           step.submit_enabled == 0 &&
                           step.auth_attempt_allowed == 0,
                       "credential ui step submit must keep GUI auth blocked");
  fails += expect_true(step.raw_secret_exposed == 0 &&
                           step.masked_text_exposed == 0 &&
                           step.credential_redacted == 1 &&
                           step.length_redacted == 1,
                       "credential ui step submit must remain redacted");
  fails += expect_true(masked[0] == '\0' && storage[0] == '\0',
                       "credential ui step submit should clear mask and storage");
  fails += expect_true(strings_equal(step.event_type,
                                     "credential-submit-blocked") &&
                           strings_equal(step.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential ui step submit should expose blocked submit event");
  return fails;
}

static int test_loginwindow_credential_ui_step_cancel_wipes_and_composes_view(void) {
  int fails = 0;
  char storage[8];
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_ui_step step;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "credential ui step cancel contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "credential ui step cancel policy should build");
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "credential ui step cancel buffer should initialize");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'x') == 1,
                       "credential ui step cancel should prepare secret");
  fails += expect_true(login_window_credential_ui_step_build(
                           &policy, &buffer,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, '\0',
                           masked, sizeof(masked), &step) == 0,
                       "credential ui step cancel should compose pipeline");
  fails += expect_true(step.input_applied == 1 &&
                           step.input_accepted == 1 &&
                           step.buffer_changed == 1,
                       "credential ui step cancel should consume cancel");
  fails += expect_true(step.wipe_attempted == 1 &&
                           step.wipe_succeeded == 1 &&
                           step.credential_present == 0,
                       "credential ui step cancel should wipe credential");
  fails += expect_true(step.renderable == 1 && step.input_enabled == 0,
                       "credential ui step cancel should render disabled state");
  fails += expect_true(strings_equal(step.event_type,
                                     "credential-cancelled") &&
                           strings_equal(step.state, "cancelled"),
                       "credential ui step cancel should expose cancelled event");
  fails += expect_true(strings_equal(step.message,
                                     "Credential input cancelled and wiped."),
                       "credential ui step cancel should expose wipe message");
  fails += expect_true(step.submit_enabled == 0 &&
                           step.auth_attempt_allowed == 0 &&
                           step.raw_secret_exposed == 0 &&
                           step.masked_text_exposed == 0,
                       "credential ui step cancel must stay safe");
  return fails;
}

static int test_loginwindow_credential_ui_step_fails_closed_on_unknown_action(void) {
  int fails = 0;
  char storage[8];
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_ui_step step;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "credential ui step unknown contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "credential ui step unknown policy should build");
  fails += expect_true(login_window_credential_buffer_init(&buffer, storage,
                                                          sizeof(storage),
                                                          &policy) == 0,
                       "credential ui step unknown buffer should initialize");
  fails += expect_true(login_window_credential_ui_step_build(
                           &policy, &buffer, 99, '\0', masked, sizeof(masked),
                           &step) == 0,
                       "credential ui step unknown should compose blocked pipeline");
  fails += expect_true(step.interaction_built == 1 &&
                           step.readiness_built == 1 &&
                           step.audit_built == 1 &&
                           step.view_built == 1,
                       "credential ui step unknown should still build audit state");
  fails += expect_true(step.input_applied == 1 &&
                           step.input_accepted == 0 &&
                           step.input_blocked == 1 &&
                           step.buffer_changed == 0,
                       "credential ui step unknown should block mutation");
  fails += expect_true(step.renderable == 1 && step.input_enabled == 0,
                       "credential ui step unknown should render diagnostic field only");
  fails += expect_true(strings_equal(step.event_type,
                                     "credential-input-blocked") &&
                           strings_equal(step.blocked_reason,
                                         "input-action-unknown"),
                       "credential ui step unknown should explain block");
  fails += expect_true(step.submit_blocked == 1 &&
                           step.submit_enabled == 0 &&
                           step.auth_attempt_allowed == 0,
                       "credential ui step unknown must keep GUI auth blocked");
  fails += expect_true(step.raw_secret_exposed == 0 &&
                           step.masked_text_exposed == 0 &&
                           step.credential_redacted == 1 &&
                           step.length_redacted == 1,
                       "credential ui step unknown must remain redacted");
  fails += expect_true(storage[0] == '\0' && masked[0] == '\0',
                       "credential ui step unknown should not expose storage or mask");
  return fails;
}


static int test_loginwindow_credential_ui_session_appends_and_wipes_storage(void) {
  int fails = 0;
  char storage[8] = "stale";
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_ui_session session;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "credential ui session append contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "credential ui session append policy should build");
  fails += expect_true(login_window_credential_ui_session_build(
                           &policy, storage, sizeof(storage),
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'a',
                           masked, sizeof(masked), &session) == 0,
                       "credential ui session append should build");
  fails += expect_true(session.version == LOGIN_WINDOW_CREDENTIAL_UI_SESSION_VERSION,
                       "credential ui session should expose stable version");
  fails += expect_true(session.policy_available == 1 &&
                           session.storage_available == 1 &&
                           session.storage_cleared == 1 &&
                           session.buffer_initialized == 1,
                       "credential ui session append should prepare storage safely");
  fails += expect_true(session.step_built == 1 &&
                           session.input_applied == 1 &&
                           session.input_accepted == 1 &&
                           session.buffer_changed == 1,
                       "credential ui session append should compose accepted step");
  fails += expect_true(session.renderable == 1 && session.input_enabled == 1,
                       "credential ui session append should expose renderable state");
  fails += expect_true(session.storage_wiped == 1 &&
                           session.wipe_attempted == 1 &&
                           session.wipe_succeeded == 1 &&
                           session.credential_present == 0,
                       "credential ui session append should wipe before return");
  fails += expect_true(session.credential_redacted == 1 &&
                           session.length_redacted == 1 &&
                           session.raw_secret_exposed == 0 &&
                           session.masked_text_exposed == 0,
                       "credential ui session append should expose only redacted flags");
  fails += expect_true(session.submit_blocked == 1 &&
                           session.submit_enabled == 0 &&
                           session.auth_attempt_allowed == 0,
                       "credential ui session append must keep GUI auth blocked");
  fails += expect_true(storage[0] == '\0' && masked[0] == '\0',
                       "credential ui session append should clear storage and scratch mask");
  fails += expect_true(strings_equal(session.event_type,
                                     "credential-input-accepted") &&
                           strings_equal(session.state, "editing"),
                       "credential ui session append should preserve redacted event state");
  return fails;
}

static int test_loginwindow_credential_ui_session_submit_blocks_and_wipes_empty_storage(void) {
  int fails = 0;
  char storage[8];
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_ui_session session;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "credential ui session submit contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "credential ui session submit policy should build");
  fails += expect_true(login_window_credential_ui_session_build(
                           &policy, storage, sizeof(storage),
                           LOGIN_WINDOW_CREDENTIAL_INPUT_SUBMIT, '\0',
                           masked, sizeof(masked), &session) == 0,
                       "credential ui session submit should build");
  fails += expect_true(session.step_built == 1 &&
                           session.input_applied == 1 &&
                           session.input_accepted == 0 &&
                           session.input_blocked == 1,
                       "credential ui session submit should expose blocked submit");
  fails += expect_true(session.storage_wiped == 1 &&
                           session.wipe_attempted == 1 &&
                           session.wipe_succeeded == 1 &&
                           session.credential_present == 0,
                       "credential ui session submit should wipe empty storage");
  fails += expect_true(strings_equal(session.event_type,
                                     "credential-submit-blocked") &&
                           strings_equal(session.blocked_reason,
                                         "credential-empty"),
                       "credential ui session submit should report empty credential block");
  fails += expect_true(session.submit_blocked == 1 &&
                           session.submit_enabled == 0 &&
                           session.auth_attempt_allowed == 0,
                       "credential ui session submit must keep GUI auth blocked");
  fails += expect_true(session.raw_secret_exposed == 0 &&
                           session.masked_text_exposed == 0 &&
                           session.credential_redacted == 1 &&
                           session.length_redacted == 1,
                       "credential ui session submit must remain redacted");
  fails += expect_true(storage[0] == '\0' && masked[0] == '\0',
                       "credential ui session submit should clear storage and scratch");
  return fails;
}

static int test_loginwindow_credential_ui_session_cancel_wipes_and_reports_cancelled(void) {
  int fails = 0;
  char storage[8] = "old";
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_ui_session session;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "credential ui session cancel contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "credential ui session cancel policy should build");
  fails += expect_true(login_window_credential_ui_session_build(
                           &policy, storage, sizeof(storage),
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, '\0',
                           masked, sizeof(masked), &session) == 0,
                       "credential ui session cancel should build");
  fails += expect_true(session.step_built == 1 &&
                           session.input_applied == 1 &&
                           session.input_accepted == 1 &&
                           session.buffer_changed == 1,
                       "credential ui session cancel should consume input");
  fails += expect_true(session.storage_wiped == 1 &&
                           session.wipe_attempted == 1 &&
                           session.wipe_succeeded == 1 &&
                           session.credential_present == 0,
                       "credential ui session cancel should wipe before return");
  fails += expect_true(strings_equal(session.event_type,
                                     "credential-cancelled") &&
                           strings_equal(session.state, "cancelled") &&
                           strings_equal(session.message,
                                         "Credential input cancelled and wiped."),
                       "credential ui session cancel should expose cancelled state");
  fails += expect_true(session.submit_enabled == 0 &&
                           session.auth_attempt_allowed == 0 &&
                           session.raw_secret_exposed == 0 &&
                           session.masked_text_exposed == 0,
                       "credential ui session cancel must stay safe");
  fails += expect_true(storage[0] == '\0' && masked[0] == '\0',
                       "credential ui session cancel should clear storage and scratch");
  return fails;
}

static int test_loginwindow_credential_ui_session_fails_closed_without_storage(void) {
  int fails = 0;
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_ui_session session;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "credential ui session no storage contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "credential ui session no storage policy should build");
  fails += expect_true(login_window_credential_ui_session_build(
                           &policy, NULL, 0,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x',
                           masked, sizeof(masked), &session) == 0,
                       "credential ui session no storage should build fail-closed state");
  fails += expect_true(session.storage_available == 0 &&
                           session.storage_cleared == 0 &&
                           session.buffer_initialized == 0 &&
                           session.step_built == 1,
                       "credential ui session no storage should expose unavailable storage");
  fails += expect_true(session.input_applied == 1 &&
                           session.input_accepted == 0 &&
                           session.input_blocked == 1 &&
                           session.buffer_changed == 0,
                       "credential ui session no storage should block mutation");
  fails += expect_true(session.renderable == 0 &&
                           session.input_enabled == 0 &&
                           session.storage_wiped == 0,
                       "credential ui session no storage should not render or claim wipe");
  fails += expect_true(strings_equal(session.event_type,
                                     "credential-input-blocked") &&
                           strings_equal(session.blocked_reason,
                                         "buffer-unavailable"),
                       "credential ui session no storage should explain block");
  fails += expect_true(session.submit_blocked == 1 &&
                           session.submit_enabled == 0 &&
                           session.auth_attempt_allowed == 0 &&
                           session.raw_secret_exposed == 0 &&
                           session.masked_text_exposed == 0,
                       "credential ui session no storage must keep GUI auth blocked");
  fails += expect_true(masked[0] == '\0',
                       "credential ui session no storage should clear scratch mask");
  return fails;
}


static int test_loginwindow_credential_recovery_view_model_allows_text_recovery_only(void) {
  int fails = 0;
  char storage[8];
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_ui_session credential_session;
  struct login_window_credential_recovery_view_model view;

  reset_test_state();
  ops = build_ops();
  ops.maintenance_mode = 1;
  g_runtime_maintenance_active = 1;
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "credential recovery text contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "credential recovery text policy should build");
  fails += expect_true(login_window_credential_ui_session_build(
                           &policy, storage, sizeof(storage),
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, '\0',
                           masked, sizeof(masked), &credential_session) == 0,
                       "credential recovery text session should build");
  fails += expect_true(login_window_credential_recovery_view_model_build(
                           &contract, &policy, &credential_session, NULL,
                           &view) == 0,
                       "credential recovery text view should build");
  fails += expect_true(view.version == LOGIN_WINDOW_CREDENTIAL_RECOVERY_VIEW_MODEL_VERSION,
                       "credential recovery view should expose stable version");
  fails += expect_true(view.recovery_visible == 1 &&
                           view.recovery_enabled == 1 &&
                           view.recovery_text_session_required == 1,
                       "credential recovery view should expose text recovery only");
  fails += expect_true(view.resume_visible == 0 && view.resume_enabled == 0,
                       "credential recovery view should not expose resume without policy");
  fails += expect_true(view.credential_session_safe == 1 &&
                           view.credential_storage_wiped == 1 &&
                           view.credential_redacted == 1 &&
                           view.length_redacted == 1,
                       "credential recovery view should require safe wiped session");
  fails += expect_true(view.raw_secret_exposed == 0 &&
                           view.masked_text_exposed == 0 &&
                           view.submit_blocked == 1 &&
                           view.submit_enabled == 0 &&
                           view.auth_attempt_allowed == 0,
                       "credential recovery view must keep GUI auth disabled");
  fails += expect_true(strings_equal(view.state, "recovery-ready") &&
                           strings_equal(view.blocked_reason,
                                         "text-recovery-only"),
                       "credential recovery view should expose text recovery state");
  return fails;
}

static int test_loginwindow_credential_recovery_view_model_reports_resume_ready(void) {
  int fails = 0;
  char storage[8];
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_ui_session credential_session;
  struct login_recovery_resume_policy resume_policy;
  struct login_window_credential_recovery_view_model view;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "credential recovery resume contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "credential recovery resume policy should build");
  fails += expect_true(login_window_credential_ui_session_build(
                           &policy, storage, sizeof(storage),
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r',
                           masked, sizeof(masked), &credential_session) == 0,
                       "credential recovery resume session should build");
  fails += expect_true(login_recovery_resume_policy_evaluate(&ops, 1, 1,
                                                            &resume_policy) == 0,
                       "credential recovery resume policy should evaluate");
  fails += expect_true(login_window_credential_recovery_view_model_build(
                           &contract, &policy, &credential_session,
                           &resume_policy, &view) == 0,
                       "credential recovery resume view should build");
  fails += expect_true(view.resume_visible == 1 && view.resume_enabled == 1,
                       "credential recovery resume should be visible and enabled");
  fails += expect_true(view.recovery_visible == 0 && view.recovery_enabled == 0,
                       "credential recovery resume should not expose recovery action");
  fails += expect_true(view.session_reset_required == 1 &&
                           view.login_screen_rerender_required == 1,
                       "credential recovery resume should require reset and rerender");
  fails += expect_true(view.credential_session_safe == 1 &&
                           view.credential_storage_wiped == 1 &&
                           view.raw_secret_exposed == 0 &&
                           view.masked_text_exposed == 0,
                       "credential recovery resume should require safe credential session");
  fails += expect_true(view.submit_blocked == 1 &&
                           view.submit_enabled == 0 &&
                           view.auth_attempt_allowed == 0,
                       "credential recovery resume must keep GUI auth disabled");
  fails += expect_true(strings_equal(view.state, "resume-ready") &&
                           strings_equal(view.blocked_reason, "ready"),
                       "credential recovery resume should expose ready state");
  return fails;
}

static int test_loginwindow_credential_recovery_view_model_blocks_unsafe_session(void) {
  int fails = 0;
  char storage[8];
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_ui_session credential_session;
  struct login_window_credential_recovery_view_model view;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "credential recovery unsafe contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "credential recovery unsafe policy should build");
  fails += expect_true(login_window_credential_ui_session_build(
                           &policy, storage, sizeof(storage),
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u',
                           masked, sizeof(masked), &credential_session) == 0,
                       "credential recovery unsafe session should build");
  credential_session.raw_secret_exposed = 1;
  credential_session.storage_wiped = 0;
  fails += expect_true(login_window_credential_recovery_view_model_build(
                           &contract, &policy, &credential_session, NULL,
                           &view) == 0,
                       "credential recovery unsafe view should build");
  fails += expect_true(view.credential_session_safe == 0 &&
                           view.recovery_visible == 0 &&
                           view.resume_visible == 0,
                       "credential recovery unsafe should hide actions");
  fails += expect_true(view.raw_secret_exposed == 1 &&
                           view.submit_blocked == 1 &&
                           view.submit_enabled == 0 &&
                           view.auth_attempt_allowed == 0,
                       "credential recovery unsafe must keep GUI auth disabled");
  fails += expect_true(strings_equal(view.state, "blocked") &&
                           strings_equal(view.blocked_reason,
                                         "credential-session-unsafe"),
                       "credential recovery unsafe should explain block");
  return fails;
}

static int test_loginwindow_credential_recovery_view_model_blocks_unsafe_policy(void) {
  int fails = 0;
  char storage[8];
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_ui_session credential_session;
  struct login_window_credential_recovery_view_model view;

  reset_test_state();
  ops = build_ops();
  ops.maintenance_mode = 1;
  g_runtime_maintenance_active = 1;
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "credential recovery unsafe policy contract should evaluate");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "credential recovery unsafe policy should build");
  fails += expect_true(login_window_credential_ui_session_build(
                           &policy, storage, sizeof(storage),
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, '\0',
                           masked, sizeof(masked), &credential_session) == 0,
                       "credential recovery unsafe policy session should build");
  policy.recovery_requires_text_session = 0;
  fails += expect_true(login_window_credential_recovery_view_model_build(
                           &contract, &policy, &credential_session, NULL,
                           &view) == 0,
                       "credential recovery unsafe policy view should build");
  fails += expect_true(view.recovery_visible == 0 &&
                           view.recovery_enabled == 0 &&
                           view.credential_session_safe == 1,
                       "credential recovery unsafe policy should keep actions hidden");
  fails += expect_true(view.submit_blocked == 1 &&
                           view.submit_enabled == 0 &&
                           view.auth_attempt_allowed == 0 &&
                           view.raw_secret_exposed == 0 &&
                           view.masked_text_exposed == 0,
                       "credential recovery unsafe policy must stay redacted");
  fails += expect_true(strings_equal(view.state, "blocked") &&
                           strings_equal(view.blocked_reason,
                                         "recovery-policy-unsafe"),
                       "credential recovery unsafe policy should explain block");
  return fails;
}

int test_login_runtime_credential_ui_session_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_ui_step_appends_and_composes_view();
  fails += test_loginwindow_credential_ui_step_submit_wipes_and_composes_view();
  fails += test_loginwindow_credential_ui_step_cancel_wipes_and_composes_view();
  fails += test_loginwindow_credential_ui_step_fails_closed_on_unknown_action();
  fails += test_loginwindow_credential_ui_session_appends_and_wipes_storage();
  fails += test_loginwindow_credential_ui_session_submit_blocks_and_wipes_empty_storage();
  fails += test_loginwindow_credential_ui_session_cancel_wipes_and_reports_cancelled();
  fails += test_loginwindow_credential_ui_session_fails_closed_without_storage();
  fails += test_loginwindow_credential_recovery_view_model_allows_text_recovery_only();
  fails += test_loginwindow_credential_recovery_view_model_reports_resume_ready();
  fails += test_loginwindow_credential_recovery_view_model_blocks_unsafe_session();
  fails += test_loginwindow_credential_recovery_view_model_blocks_unsafe_policy();
  return fails;
}
