/*
 * tests/auth/test_login_runtime_credential_screen.c
 *
 * Credential screen session + screen render plan coverage for the
 * `login_runtime` host test. Originally carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-15 monolith
 * refactor (PR D.6 of the Estagio D dedicated plan), and further
 * split at the 2026-05-16 preventive refactor (the 6 view_model
 * tests moved to the sibling
 * `tests/auth/test_login_runtime_credential_screen_view_model.c`)
 * so each host-test translation unit stays comfortably below the
 * 900-line layout limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_session_build`: 5 tests
 *     covering the ready safe screen + the text recovery + the
 *     resume-ready + the missing-storage block + the missing-ops
 *     fail-closed default.
 *   - `login_window_credential_screen_render_plan_build`: 5 tests
 *     covering the ready password-focus layout + the text-recovery
 *     action + the resume action + the unsafe-session fallback +
 *     the missing-session fail-closed default.
 *
 * The companion entry `test_login_runtime_credential_screen_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`, which also invokes the new
 * sibling entry
 * `test_login_runtime_credential_screen_view_model_cases`. Shared
 * fixture state and helpers come from
 * `tests/auth/test_login_runtime_internal.h`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

static int test_loginwindow_credential_screen_session_builds_safe_login_screen(void) {
  int fails = 0;
  char storage[8] = "stale";
  char masked[8] = "mask";
  struct login_runtime_ops ops;
  struct login_window_credential_screen_session session;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_credential_screen_session_build(
                           &ops, "en", storage, sizeof(storage),
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'a',
                           masked, sizeof(masked), 0, 0, &session) == 0,
                       "credential screen session ready should build");
  fails += expect_true(session.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_SESSION_VERSION,
                       "credential screen session should expose stable version");
  fails += expect_true(session.ops_available == 1 &&
                           session.contract_built == 1 &&
                           session.login_view_built == 1 &&
                           session.policy_built == 1 &&
                           session.credential_session_built == 1 &&
                           session.resume_policy_built == 1 &&
                           session.recovery_view_built == 1 &&
                           session.screen_built == 1,
                       "credential screen session should compose all safe stages");
  fails += expect_true(session.renderable == 1 &&
                           session.password_panel_visible == 1 &&
                           session.password_input_enabled == 1,
                       "credential screen session ready should render password panel");
  fails += expect_true(session.recovery_visible == 0 &&
                           session.recovery_enabled == 0 &&
                           session.resume_visible == 0 &&
                           session.resume_enabled == 0,
                       "credential screen session ready should hide recovery actions");
  fails += expect_true(session.credential_session_safe == 1 &&
                           session.credential_storage_wiped == 1 &&
                           session.credential_redacted == 1 &&
                           session.length_redacted == 1,
                       "credential screen session ready should require safe wiped credentials");
  fails += expect_true(session.raw_secret_exposed == 0 &&
                           session.masked_text_exposed == 0 &&
                           session.storage_cleared == 1 &&
                           session.scratch_cleared == 1,
                       "credential screen session ready should clear IO and stay redacted");
  fails += expect_true(session.submit_visible == 0 &&
                           session.submit_blocked == 1 &&
                           session.submit_enabled == 0 &&
                           session.auth_attempt_allowed == 0,
                       "credential screen session ready must keep GUI auth disabled");
  fails += expect_true(storage[0] == '\0' && masked[0] == '\0',
                       "credential screen session ready should clear storage and scratch");
  fails += expect_true(strings_equal(session.state,
                                     "credential-screen-ready") &&
                           strings_equal(session.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential screen session ready should report GUI submit block");
  return fails;
}

static int test_loginwindow_credential_screen_session_exposes_text_recovery(void) {
  int fails = 0;
  char storage[8] = "old";
  char masked[8] = "mask";
  struct login_runtime_ops ops;
  struct login_window_credential_screen_session session;

  reset_test_state();
  ops = build_ops();
  ops.maintenance_mode = 1;
  g_runtime_maintenance_active = 1;
  fails += expect_true(login_window_credential_screen_session_build(
                           &ops, "en", storage, sizeof(storage),
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, '\0',
                           masked, sizeof(masked), 0, 0, &session) == 0,
                       "credential screen session recovery should build");
  fails += expect_true(session.renderable == 1 &&
                           session.maintenance_notice == 1,
                       "credential screen session recovery should render maintenance screen");
  fails += expect_true(session.password_panel_visible == 0 &&
                           session.password_input_enabled == 0,
                       "credential screen session recovery should hide password input");
  fails += expect_true(session.recovery_visible == 1 &&
                           session.recovery_enabled == 1 &&
                           session.recovery_text_session_required == 1,
                       "credential screen session recovery should expose text recovery");
  fails += expect_true(session.resume_visible == 0 && session.resume_enabled == 0,
                       "credential screen session recovery should not expose resume");
  fails += expect_true(session.credential_session_safe == 1 &&
                           session.credential_storage_wiped == 1 &&
                           session.raw_secret_exposed == 0 &&
                           session.masked_text_exposed == 0,
                       "credential screen session recovery should require safe credentials");
  fails += expect_true(session.submit_visible == 0 &&
                           session.submit_blocked == 1 &&
                           session.submit_enabled == 0 &&
                           session.auth_attempt_allowed == 0,
                       "credential screen session recovery must keep GUI auth disabled");
  fails += expect_true(storage[0] == '\0' && masked[0] == '\0',
                       "credential screen session recovery should clear IO");
  fails += expect_true(strings_equal(session.state, "recovery-ready") &&
                           strings_equal(session.blocked_reason,
                                         "text-recovery-only"),
                       "credential screen session recovery should report text recovery state");
  return fails;
}

static int test_loginwindow_credential_screen_session_exposes_resume_ready(void) {
  int fails = 0;
  char storage[8] = "old";
  char masked[8] = "mask";
  struct login_runtime_ops ops;
  struct login_window_credential_screen_session session;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_credential_screen_session_build(
                           &ops, "en", storage, sizeof(storage),
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r',
                           masked, sizeof(masked), 1, 1, &session) == 0,
                       "credential screen session resume should build");
  fails += expect_true(session.renderable == 1 &&
                           session.resume_visible == 1 &&
                           session.resume_enabled == 1,
                       "credential screen session resume should expose resume action");
  fails += expect_true(session.recovery_visible == 0 &&
                           session.recovery_enabled == 0,
                       "credential screen session resume should hide recovery action");
  fails += expect_true(session.session_reset_required == 1 &&
                           session.login_screen_rerender_required == 1,
                       "credential screen session resume should require reset and rerender");
  fails += expect_true(session.credential_session_safe == 1 &&
                           session.credential_storage_wiped == 1 &&
                           session.raw_secret_exposed == 0 &&
                           session.masked_text_exposed == 0,
                       "credential screen session resume should require safe credentials");
  fails += expect_true(session.submit_blocked == 1 &&
                           session.submit_enabled == 0 &&
                           session.auth_attempt_allowed == 0,
                       "credential screen session resume must keep GUI auth disabled");
  fails += expect_true(storage[0] == '\0' && masked[0] == '\0',
                       "credential screen session resume should clear IO");
  fails += expect_true(strings_equal(session.state, "resume-ready") &&
                           strings_equal(session.blocked_reason, "ready"),
                       "credential screen session resume should report ready state");
  return fails;
}

static int test_loginwindow_credential_screen_session_blocks_missing_storage(void) {
  int fails = 0;
  char masked[8] = "mask";
  struct login_runtime_ops ops;
  struct login_window_credential_screen_session session;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_credential_screen_session_build(
                           &ops, "en", NULL, 0,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x',
                           masked, sizeof(masked), 0, 0, &session) == 0,
                       "credential screen session no storage should build blocked state");
  fails += expect_true(session.ops_available == 1 &&
                           session.contract_built == 1 &&
                           session.credential_session_built == 1 &&
                           session.screen_built == 1,
                       "credential screen session no storage should compose fail-closed stages");
  fails += expect_true(session.renderable == 0 &&
                           session.password_panel_visible == 0 &&
                           session.password_input_enabled == 0,
                       "credential screen session no storage should not render input");
  fails += expect_true(session.credential_session_safe == 0 &&
                           session.credential_storage_wiped == 0 &&
                           session.storage_cleared == 0 &&
                           session.scratch_cleared == 1,
                       "credential screen session no storage should expose safe IO flags");
  fails += expect_true(session.submit_blocked == 1 &&
                           session.submit_enabled == 0 &&
                           session.auth_attempt_allowed == 0 &&
                           session.raw_secret_exposed == 0 &&
                           session.masked_text_exposed == 0,
                       "credential screen session no storage must keep GUI auth disabled");
  fails += expect_true(masked[0] == '\0',
                       "credential screen session no storage should clear scratch");
  fails += expect_true(strings_equal(session.state, "blocked") &&
                           strings_equal(session.blocked_reason,
                                         "credential-session-unsafe"),
                       "credential screen session no storage should explain block");
  return fails;
}

static int test_loginwindow_credential_screen_session_fails_closed_without_ops(void) {
  int fails = 0;
  char storage[8] = "secret";
  char masked[8] = "mask";
  struct login_window_credential_screen_session session;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_session_build(
                           NULL, "en", storage, sizeof(storage),
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x',
                           masked, sizeof(masked), 0, 0, &session) == 0,
                       "credential screen session no ops should build fail-closed state");
  fails += expect_true(session.ops_available == 0 &&
                           session.contract_built == 0 &&
                           session.login_view_built == 0 &&
                           session.screen_built == 0,
                       "credential screen session no ops should not build stages");
  fails += expect_true(session.renderable == 0 &&
                           session.password_panel_visible == 0 &&
                           session.recovery_visible == 0 &&
                           session.resume_visible == 0,
                       "credential screen session no ops should hide all UI actions");
  fails += expect_true(session.storage_cleared == 1 &&
                           session.scratch_cleared == 1 &&
                           storage[0] == '\0' && masked[0] == '\0',
                       "credential screen session no ops should clear provided IO");
  fails += expect_true(session.submit_blocked == 1 &&
                           session.submit_enabled == 0 &&
                           session.auth_attempt_allowed == 0 &&
                           session.raw_secret_exposed == 0 &&
                           session.masked_text_exposed == 0,
                       "credential screen session no ops must keep GUI auth disabled");
  fails += expect_true(strings_equal(session.state, "blocked") &&
                           strings_equal(session.blocked_reason,
                                         "ops-unavailable"),
                       "credential screen session no ops should explain block");
  return fails;
}


static int test_loginwindow_credential_screen_render_plan_focuses_safe_password_input(void) {
  int fails = 0;
  char storage[8] = "stale";
  char masked[8] = "mask";
  struct login_runtime_ops ops;
  struct login_window_credential_screen_session session;
  struct login_window_credential_screen_render_plan plan;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_credential_screen_session_build(
                           &ops, "en", storage, sizeof(storage),
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'p',
                           masked, sizeof(masked), 0, 0, &session) == 0,
                       "credential render plan ready session should build");
  fails += expect_true(login_window_credential_screen_render_plan_build(
                           &session, &plan) == 0,
                       "credential render plan ready should build");
  fails += expect_true(plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_RENDER_PLAN_VERSION,
                       "credential render plan should expose stable version");
  fails += expect_true(plan.session_available == 1 &&
                           plan.screen_built == 1 &&
                           plan.screen_session_safe == 1,
                       "credential render plan ready should require safe screen session");
  fails += expect_true(plan.layout_visible == 1 &&
                           plan.header_visible == 1 &&
                           plan.status_visible == 1 &&
                           plan.error_visible == 0 &&
                           plan.renderable == 1,
                       "credential render plan ready should render main layout");
  fails += expect_true(plan.password_panel_visible == 1 &&
                           plan.password_input_visible == 1 &&
                           plan.password_input_enabled == 1 &&
                           plan.password_input_focus == 1,
                       "credential render plan ready should focus password input");
  fails += expect_true(plan.recovery_panel_visible == 0 &&
                           plan.recovery_button_visible == 0 &&
                           plan.resume_button_visible == 0,
                       "credential render plan ready should hide recovery actions");
  fails += expect_true(plan.credential_session_safe == 1 &&
                           plan.credential_storage_wiped == 1 &&
                           plan.credential_redacted == 1 &&
                           plan.length_redacted == 1,
                       "credential render plan ready should carry safe credential flags");
  fails += expect_true(plan.raw_secret_exposed == 0 &&
                           plan.masked_text_exposed == 0 &&
                           plan.submit_button_visible == 0 &&
                           plan.submit_button_enabled == 0 &&
                           plan.submit_blocked == 1 &&
                           plan.auth_attempt_allowed == 0,
                       "credential render plan ready must keep GUI auth disabled");
  fails += expect_true(strings_equal(plan.primary_action,
                                     "edit-credential") &&
                           strings_equal(plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential render plan ready should expose edit action only");
  return fails;
}

static int test_loginwindow_credential_screen_render_plan_exposes_text_recovery_action(void) {
  int fails = 0;
  char storage[8] = "old";
  char masked[8] = "mask";
  struct login_runtime_ops ops;
  struct login_window_credential_screen_session session;
  struct login_window_credential_screen_render_plan plan;

  reset_test_state();
  ops = build_ops();
  ops.maintenance_mode = 1;
  g_runtime_maintenance_active = 1;
  fails += expect_true(login_window_credential_screen_session_build(
                           &ops, "en", storage, sizeof(storage),
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, '\0',
                           masked, sizeof(masked), 0, 0, &session) == 0,
                       "credential render plan recovery session should build");
  fails += expect_true(login_window_credential_screen_render_plan_build(
                           &session, &plan) == 0,
                       "credential render plan recovery should build");
  fails += expect_true(plan.screen_session_safe == 1 && plan.renderable == 1,
                       "credential render plan recovery should be safe and renderable");
  fails += expect_true(plan.password_panel_visible == 0 &&
                           plan.password_input_visible == 0 &&
                           plan.password_input_focus == 0,
                       "credential render plan recovery should not focus password input");
  fails += expect_true(plan.recovery_panel_visible == 1 &&
                           plan.recovery_button_visible == 1 &&
                           plan.recovery_button_enabled == 1,
                       "credential render plan recovery should expose recovery button");
  fails += expect_true(plan.resume_button_visible == 0 &&
                           plan.resume_button_enabled == 0,
                       "credential render plan recovery should hide resume button");
  fails += expect_true(plan.maintenance_notice_visible == 1 &&
                           plan.text_login_notice_visible == 1 &&
                           plan.recovery_text_session_required == 1,
                       "credential render plan recovery should show text-session notices");
  fails += expect_true(plan.raw_secret_exposed == 0 &&
                           plan.masked_text_exposed == 0 &&
                           plan.submit_button_visible == 0 &&
                           plan.auth_attempt_allowed == 0,
                       "credential render plan recovery must stay redacted");
  fails += expect_true(strings_equal(plan.primary_action,
                                     "open-text-recovery") &&
                           strings_equal(plan.state, "recovery-ready"),
                       "credential render plan recovery should expose text recovery action");
  return fails;
}

static int test_loginwindow_credential_screen_render_plan_exposes_resume_action(void) {
  int fails = 0;
  char storage[8] = "old";
  char masked[8] = "mask";
  struct login_runtime_ops ops;
  struct login_window_credential_screen_session session;
  struct login_window_credential_screen_render_plan plan;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_credential_screen_session_build(
                           &ops, "en", storage, sizeof(storage),
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r',
                           masked, sizeof(masked), 1, 1, &session) == 0,
                       "credential render plan resume session should build");
  fails += expect_true(login_window_credential_screen_render_plan_build(
                           &session, &plan) == 0,
                       "credential render plan resume should build");
  fails += expect_true(plan.screen_session_safe == 1 &&
                           plan.recovery_panel_visible == 1,
                       "credential render plan resume should expose recovery panel");
  fails += expect_true(plan.resume_button_visible == 1 &&
                           plan.resume_button_enabled == 1 &&
                           plan.recovery_button_visible == 0,
                       "credential render plan resume should expose resume only");
  fails += expect_true(plan.session_reset_required == 1 &&
                           plan.login_screen_rerender_required == 1,
                       "credential render plan resume should require reset and rerender");
  fails += expect_true(plan.password_input_focus == 0 &&
                           plan.submit_button_visible == 0 &&
                           plan.submit_button_enabled == 0 &&
                           plan.auth_attempt_allowed == 0,
                       "credential render plan resume must not focus auth input");
  fails += expect_true(plan.raw_secret_exposed == 0 &&
                           plan.masked_text_exposed == 0,
                       "credential render plan resume must stay redacted");
  fails += expect_true(strings_equal(plan.primary_action,
                                     "resume-text-login") &&
                           strings_equal(plan.state, "resume-ready"),
                       "credential render plan resume should expose resume action");
  return fails;
}

static int test_loginwindow_credential_screen_render_plan_blocks_unsafe_session(void) {
  int fails = 0;
  char masked[8] = "mask";
  struct login_runtime_ops ops;
  struct login_window_credential_screen_session session;
  struct login_window_credential_screen_render_plan plan;

  reset_test_state();
  ops = build_ops();
  fails += expect_true(login_window_credential_screen_session_build(
                           &ops, "en", NULL, 0,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x',
                           masked, sizeof(masked), 0, 0, &session) == 0,
                       "credential render plan unsafe session should build");
  fails += expect_true(login_window_credential_screen_render_plan_build(
                           &session, &plan) == 0,
                       "credential render plan unsafe should build fail-closed state");
  fails += expect_true(plan.session_available == 1 &&
                           plan.screen_built == 1 &&
                           plan.screen_session_safe == 0,
                       "credential render plan unsafe should reject unsafe session");
  fails += expect_true(plan.layout_visible == 1 &&
                           plan.header_visible == 1 &&
                           plan.status_visible == 1 &&
                           plan.error_visible == 1,
                       "credential render plan unsafe should show blocked layout");
  fails += expect_true(plan.password_input_visible == 0 &&
                           plan.password_input_enabled == 0 &&
                           plan.password_input_focus == 0 &&
                           plan.recovery_button_enabled == 0 &&
                           plan.resume_button_enabled == 0,
                       "credential render plan unsafe should disable all actions");
  fails += expect_true(plan.submit_button_visible == 0 &&
                           plan.submit_button_enabled == 0 &&
                           plan.submit_blocked == 1 &&
                           plan.auth_attempt_allowed == 0,
                       "credential render plan unsafe must keep GUI auth disabled");
  fails += expect_true(strings_equal(plan.primary_action,
                                     "use-text-login") &&
                           strings_equal(plan.blocked_reason,
                                         "credential-render-unsafe"),
                       "credential render plan unsafe should force text login");
  return fails;
}

static int test_loginwindow_credential_screen_render_plan_fails_closed_without_session(void) {
  int fails = 0;
  struct login_window_credential_screen_render_plan plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_render_plan_build(
                           NULL, &plan) == 0,
                       "credential render plan no session should build fail-closed state");
  fails += expect_true(plan.session_available == 0 &&
                           plan.screen_built == 0 &&
                           plan.screen_session_safe == 0,
                       "credential render plan no session should expose unavailable state");
  fails += expect_true(plan.renderable == 0 &&
                           plan.password_panel_visible == 0 &&
                           plan.recovery_panel_visible == 0 &&
                           plan.resume_button_visible == 0,
                       "credential render plan no session should hide UI actions");
  fails += expect_true(plan.error_visible == 1 &&
                           plan.fallback_notice_visible == 1 &&
                           plan.text_login_notice_visible == 1,
                       "credential render plan no session should show fallback notices");
  fails += expect_true(plan.submit_button_visible == 0 &&
                           plan.submit_button_enabled == 0 &&
                           plan.submit_blocked == 1 &&
                           plan.auth_attempt_allowed == 0 &&
                           plan.raw_secret_exposed == 0 &&
                           plan.masked_text_exposed == 0,
                       "credential render plan no session must stay redacted");
  fails += expect_true(strings_equal(plan.primary_action,
                                     "use-text-login") &&
                           strings_equal(plan.blocked_reason,
                                         "screen-session-unavailable"),
                       "credential render plan no session should force text login");
  return fails;
}

int test_login_runtime_credential_screen_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_session_builds_safe_login_screen();
  fails += test_loginwindow_credential_screen_session_exposes_text_recovery();
  fails += test_loginwindow_credential_screen_session_exposes_resume_ready();
  fails += test_loginwindow_credential_screen_session_blocks_missing_storage();
  fails += test_loginwindow_credential_screen_session_fails_closed_without_ops();
  fails += test_loginwindow_credential_screen_render_plan_focuses_safe_password_input();
  fails += test_loginwindow_credential_screen_render_plan_exposes_text_recovery_action();
  fails += test_loginwindow_credential_screen_render_plan_exposes_resume_action();
  fails += test_loginwindow_credential_screen_render_plan_blocks_unsafe_session();
  fails += test_loginwindow_credential_screen_render_plan_fails_closed_without_session();
  return fails;
}
