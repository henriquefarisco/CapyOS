/*
 * tests/auth/test_login_runtime_credential_screen_view_model.c
 *
 * Credential screen view-model coverage for the `login_runtime`
 * host test. Carved out of
 * `tests/auth/test_login_runtime_credential_screen.c` at the
 * 2026-05-16 preventive refactor so each host-test translation
 * unit stays comfortably below the 900-line layout limit. Tests
 * in this file exercise `login_window_credential_screen_view_model_build`:
 *
 *   - the ready safe login screen,
 *   - the text recovery screen,
 *   - the resume-ready policy,
 *   - the unsafe-session block,
 *   - the unsafe-recovery block,
 *   - the enabled-GUI-submit block.
 *
 * The companion entry
 * `test_login_runtime_credential_screen_view_model_cases` is invoked
 * by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`. Shared fixture state and
 * helpers come from `tests/auth/test_login_runtime_internal.h`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

static int test_loginwindow_credential_screen_view_model_renders_safe_login_screen(void) {
  int fails = 0;
  char storage[8];
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_view_model login_view;
  struct login_window_credential_policy policy;
  struct login_window_credential_ui_session credential_session;
  struct login_window_credential_screen_view_model screen;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "credential screen ready contract should evaluate");
  fails += expect_true(login_window_view_model_build(&contract, "en",
                                                     &login_view) == 0,
                       "credential screen ready login view should build");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "credential screen ready policy should build");
  fails += expect_true(login_window_credential_ui_session_build(
                           &policy, storage, sizeof(storage),
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's',
                           masked, sizeof(masked), &credential_session) == 0,
                       "credential screen ready session should build");
  fails += expect_true(login_window_credential_screen_view_model_build(
                           &contract, &login_view, &credential_session, NULL,
                           &screen) == 0,
                       "credential screen ready should build");
  fails += expect_true(screen.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_VIEW_MODEL_VERSION,
                       "credential screen should expose stable version");
  fails += expect_true(screen.renderable == 1 &&
                           screen.password_panel_visible == 1 &&
                           screen.password_input_enabled == 1,
                       "credential screen ready should render password panel");
  fails += expect_true(screen.recovery_visible == 0 &&
                           screen.resume_visible == 0,
                       "credential screen ready should not expose recovery actions");
  fails += expect_true(screen.credential_session_safe == 1 &&
                           screen.credential_storage_wiped == 1 &&
                           screen.credential_redacted == 1 &&
                           screen.length_redacted == 1,
                       "credential screen ready should require safe wiped session");
  fails += expect_true(screen.raw_secret_exposed == 0 &&
                           screen.masked_text_exposed == 0 &&
                           screen.submit_visible == 0 &&
                           screen.submit_blocked == 1 &&
                           screen.submit_enabled == 0 &&
                           screen.auth_attempt_allowed == 0,
                       "credential screen ready must keep GUI auth disabled");
  fails += expect_true(strings_equal(screen.state,
                                     "credential-screen-ready") &&
                           strings_equal(screen.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential screen ready should explain GUI submit block");
  return fails;
}

static int test_loginwindow_credential_screen_view_model_exposes_text_recovery(void) {
  int fails = 0;
  char storage[8];
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_view_model login_view;
  struct login_window_credential_policy policy;
  struct login_window_credential_ui_session credential_session;
  struct login_window_credential_recovery_view_model recovery_view;
  struct login_window_credential_screen_view_model screen;

  reset_test_state();
  ops = build_ops();
  ops.maintenance_mode = 1;
  g_runtime_maintenance_active = 1;
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "credential screen recovery contract should evaluate");
  fails += expect_true(login_window_view_model_build(&contract, "en",
                                                     &login_view) == 0,
                       "credential screen recovery login view should build");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "credential screen recovery policy should build");
  fails += expect_true(login_window_credential_ui_session_build(
                           &policy, storage, sizeof(storage),
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, '\0',
                           masked, sizeof(masked), &credential_session) == 0,
                       "credential screen recovery session should build");
  fails += expect_true(login_window_credential_recovery_view_model_build(
                           &contract, &policy, &credential_session, NULL,
                           &recovery_view) == 0,
                       "credential screen recovery view should build");
  fails += expect_true(login_window_credential_screen_view_model_build(
                           &contract, &login_view, &credential_session,
                           &recovery_view, &screen) == 0,
                       "credential screen recovery should build");
  fails += expect_true(screen.renderable == 1 &&
                           screen.maintenance_notice == 1,
                       "credential screen recovery should render maintenance view");
  fails += expect_true(screen.password_panel_visible == 0 &&
                           screen.password_input_enabled == 0,
                       "credential screen recovery should hide password panel");
  fails += expect_true(screen.recovery_visible == 1 &&
                           screen.recovery_enabled == 1 &&
                           screen.recovery_text_session_required == 1,
                       "credential screen recovery should expose text recovery");
  fails += expect_true(screen.resume_visible == 0 && screen.resume_enabled == 0,
                       "credential screen recovery should not expose resume");
  fails += expect_true(screen.credential_session_safe == 1 &&
                           screen.credential_storage_wiped == 1 &&
                           screen.raw_secret_exposed == 0 &&
                           screen.masked_text_exposed == 0,
                       "credential screen recovery should require redacted session");
  fails += expect_true(screen.submit_visible == 0 &&
                           screen.submit_blocked == 1 &&
                           screen.submit_enabled == 0 &&
                           screen.auth_attempt_allowed == 0,
                       "credential screen recovery must keep GUI auth disabled");
  fails += expect_true(strings_equal(screen.state, "recovery-ready") &&
                           strings_equal(screen.blocked_reason,
                                         "text-recovery-only"),
                       "credential screen recovery should expose text recovery state");
  return fails;
}

static int test_loginwindow_credential_screen_view_model_exposes_resume_ready(void) {
  int fails = 0;
  char storage[8];
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_view_model login_view;
  struct login_window_credential_policy policy;
  struct login_window_credential_ui_session credential_session;
  struct login_recovery_resume_policy resume_policy;
  struct login_window_credential_recovery_view_model recovery_view;
  struct login_window_credential_screen_view_model screen;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "credential screen resume contract should evaluate");
  fails += expect_true(login_window_view_model_build(&contract, "en",
                                                     &login_view) == 0,
                       "credential screen resume login view should build");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "credential screen resume policy should build");
  fails += expect_true(login_window_credential_ui_session_build(
                           &policy, storage, sizeof(storage),
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r',
                           masked, sizeof(masked), &credential_session) == 0,
                       "credential screen resume session should build");
  fails += expect_true(login_recovery_resume_policy_evaluate(&ops, 1, 1,
                                                            &resume_policy) == 0,
                       "credential screen resume policy should evaluate");
  fails += expect_true(login_window_credential_recovery_view_model_build(
                           &contract, &policy, &credential_session,
                           &resume_policy, &recovery_view) == 0,
                       "credential screen resume recovery view should build");
  fails += expect_true(login_window_credential_screen_view_model_build(
                           &contract, &login_view, &credential_session,
                           &recovery_view, &screen) == 0,
                       "credential screen resume should build");
  fails += expect_true(screen.renderable == 1 &&
                           screen.resume_visible == 1 &&
                           screen.resume_enabled == 1,
                       "credential screen resume should expose resume action");
  fails += expect_true(screen.recovery_visible == 0 &&
                           screen.recovery_enabled == 0,
                       "credential screen resume should not expose recovery action");
  fails += expect_true(screen.session_reset_required == 1 &&
                           screen.login_screen_rerender_required == 1,
                       "credential screen resume should require reset and rerender");
  fails += expect_true(screen.credential_session_safe == 1 &&
                           screen.credential_storage_wiped == 1 &&
                           screen.raw_secret_exposed == 0 &&
                           screen.masked_text_exposed == 0,
                       "credential screen resume should require safe session");
  fails += expect_true(screen.submit_blocked == 1 &&
                           screen.submit_enabled == 0 &&
                           screen.auth_attempt_allowed == 0,
                       "credential screen resume must keep GUI auth disabled");
  fails += expect_true(strings_equal(screen.state, "resume-ready") &&
                           strings_equal(screen.blocked_reason, "ready"),
                       "credential screen resume should expose ready state");
  return fails;
}

static int test_loginwindow_credential_screen_view_model_blocks_unsafe_session(void) {
  int fails = 0;
  char storage[8];
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_view_model login_view;
  struct login_window_credential_policy policy;
  struct login_window_credential_ui_session credential_session;
  struct login_window_credential_screen_view_model screen;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "credential screen unsafe session contract should evaluate");
  fails += expect_true(login_window_view_model_build(&contract, "en",
                                                     &login_view) == 0,
                       "credential screen unsafe session login view should build");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "credential screen unsafe session policy should build");
  fails += expect_true(login_window_credential_ui_session_build(
                           &policy, storage, sizeof(storage),
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x',
                           masked, sizeof(masked), &credential_session) == 0,
                       "credential screen unsafe session should build");
  credential_session.storage_wiped = 0;
  fails += expect_true(login_window_credential_screen_view_model_build(
                           &contract, &login_view, &credential_session, NULL,
                           &screen) == 0,
                       "credential screen unsafe session should build blocked state");
  fails += expect_true(screen.renderable == 0 &&
                           screen.password_panel_visible == 0 &&
                           screen.password_input_enabled == 0,
                       "credential screen unsafe session should not render password panel");
  fails += expect_true(screen.credential_session_safe == 0 &&
                           screen.credential_storage_wiped == 0,
                       "credential screen unsafe session should expose unsafe wipe state");
  fails += expect_true(screen.submit_blocked == 1 &&
                           screen.submit_enabled == 0 &&
                           screen.auth_attempt_allowed == 0,
                       "credential screen unsafe session must keep GUI auth disabled");
  fails += expect_true(strings_equal(screen.state, "blocked") &&
                           strings_equal(screen.blocked_reason,
                                         "credential-session-unsafe"),
                       "credential screen unsafe session should explain block");
  return fails;
}

static int test_loginwindow_credential_screen_view_model_blocks_unsafe_recovery(void) {
  int fails = 0;
  char storage[8];
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_view_model login_view;
  struct login_window_credential_policy policy;
  struct login_window_credential_ui_session credential_session;
  struct login_window_credential_recovery_view_model recovery_view;
  struct login_window_credential_screen_view_model screen;

  reset_test_state();
  ops = build_ops();
  ops.maintenance_mode = 1;
  g_runtime_maintenance_active = 1;
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "credential screen unsafe recovery contract should evaluate");
  fails += expect_true(login_window_view_model_build(&contract, "en",
                                                     &login_view) == 0,
                       "credential screen unsafe recovery login view should build");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "credential screen unsafe recovery policy should build");
  fails += expect_true(login_window_credential_ui_session_build(
                           &policy, storage, sizeof(storage),
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, '\0',
                           masked, sizeof(masked), &credential_session) == 0,
                       "credential screen unsafe recovery session should build");
  fails += expect_true(login_window_credential_recovery_view_model_build(
                           &contract, &policy, &credential_session, NULL,
                           &recovery_view) == 0,
                       "credential screen unsafe recovery view should build");
  recovery_view.raw_secret_exposed = 1;
  recovery_view.recovery_text_session_required = 0;
  fails += expect_true(login_window_credential_screen_view_model_build(
                           &contract, &login_view, &credential_session,
                           &recovery_view, &screen) == 0,
                       "credential screen unsafe recovery should build blocked state");
  fails += expect_true(screen.renderable == 0 &&
                           screen.recovery_visible == 0 &&
                           screen.recovery_enabled == 0 &&
                           screen.resume_visible == 0 &&
                           screen.resume_enabled == 0,
                       "credential screen unsafe recovery should hide actions");
  fails += expect_true(screen.raw_secret_exposed == 1 &&
                           screen.submit_blocked == 1 &&
                           screen.submit_enabled == 0 &&
                           screen.auth_attempt_allowed == 0,
                       "credential screen unsafe recovery must keep GUI auth disabled");
  fails += expect_true(strings_equal(screen.state, "blocked") &&
                           strings_equal(screen.blocked_reason,
                                         "recovery-view-unsafe"),
                       "credential screen unsafe recovery should explain unsafe recovery view");
  return fails;
}

static int test_loginwindow_credential_screen_view_model_blocks_enabled_gui_submit(void) {
  int fails = 0;
  char storage[8];
  char masked[8];
  struct login_runtime_ops ops;
  struct login_window_contract contract;
  struct login_window_view_model login_view;
  struct login_window_credential_policy policy;
  struct login_window_credential_ui_session credential_session;
  struct login_window_credential_screen_view_model screen;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_contract_evaluate(&ops, &contract) == 0,
                       "credential screen submit contract should evaluate");
  fails += expect_true(login_window_view_model_build(&contract, "en",
                                                     &login_view) == 0,
                       "credential screen submit login view should build");
  fails += expect_true(login_window_credential_policy_from_contract(&contract,
                                                                    &policy) == 0,
                       "credential screen submit policy should build");
  fails += expect_true(login_window_credential_ui_session_build(
                           &policy, storage, sizeof(storage),
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's',
                           masked, sizeof(masked), &credential_session) == 0,
                       "credential screen submit session should build");
  login_view.password_submit_enabled = 1;
  fails += expect_true(login_window_credential_screen_view_model_build(
                           &contract, &login_view, &credential_session, NULL,
                           &screen) == 0,
                       "credential screen submit should build blocked state");
  fails += expect_true(screen.renderable == 0 &&
                           screen.password_panel_visible == 0 &&
                           screen.submit_visible == 0 &&
                           screen.submit_enabled == 0 &&
                           screen.submit_blocked == 1,
                       "credential screen submit should hide enabled GUI submit");
  fails += expect_true(screen.auth_attempt_allowed == 0 &&
                           screen.raw_secret_exposed == 0 &&
                           screen.masked_text_exposed == 0,
                       "credential screen submit must remain redacted");
  fails += expect_true(strings_equal(screen.state, "blocked") &&
                           strings_equal(screen.blocked_reason,
                                         "gui-submit-enabled"),
                       "credential screen submit should explain unsafe GUI submit");
  return fails;
}

int test_login_runtime_credential_screen_view_model_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_view_model_renders_safe_login_screen();
  fails += test_loginwindow_credential_screen_view_model_exposes_text_recovery();
  fails += test_loginwindow_credential_screen_view_model_exposes_resume_ready();
  fails += test_loginwindow_credential_screen_view_model_blocks_unsafe_session();
  fails += test_loginwindow_credential_screen_view_model_blocks_unsafe_recovery();
  fails += test_loginwindow_credential_screen_view_model_blocks_enabled_gui_submit();
  return fails;
}
