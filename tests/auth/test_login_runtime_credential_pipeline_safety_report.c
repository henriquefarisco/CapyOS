/*
 * tests/auth/test_login_runtime_credential_pipeline_safety_report.c
 *
 * Credential screen pipeline safety report coverage for the
 * `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-16 monolith
 * refactor (PR D.47 of the Estagio D dedicated plan, the final
 * companion file in the credential screen pipeline chain) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_pipeline_safety_report_build`:
 *     3 tests covering the safe pipeline report for credential
 *     route + the safe pipeline report for text routes + the
 *     missing-or-unsafe input plan fail-closed default plus the
 *     dispatched-events, exposed-credentials, unblocked-auth and
 *     inconsistent-route rejections.
 *
 * This file caps the multi-stage credential screen pipeline chain
 * that started with `controller` and traversed 58 distinct stages
 * to reach the final `pipeline_safety_report`. The helper
 * `build_loginwindow_credential_screen_pipeline_safety_report_for_action`
 * is kept file-local because no downstream stage exists.
 *
 * The companion entry `test_login_runtime_credential_pipeline_safety_report_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

static int build_loginwindow_credential_screen_pipeline_safety_report_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage,
    struct login_window_credential_screen_pipeline_safety_report *report) {
  struct login_window_credential_screen_window_input_plan input_plan;

  if (build_loginwindow_credential_screen_window_input_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &input_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_pipeline_safety_report_build(
      &input_plan, report);
}

static int test_loginwindow_credential_screen_pipeline_safety_report_reports_safe_pipeline_for_credential_route(void) {
  int fails = 0;
  struct login_window_credential_screen_pipeline_safety_report report;

  fails += expect_true(
      build_loginwindow_credential_screen_pipeline_safety_report_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'p', 0, 0, 0, 1,
          &report) == 0,
      "credential pipeline safety report edit should build");
  fails += expect_true(
      report.version ==
          LOGIN_WINDOW_CREDENTIAL_SCREEN_PIPELINE_SAFETY_REPORT_VERSION,
      "credential pipeline safety report should expose stable version");
  fails += expect_true(
      report.window_input_plan_available == 1 &&
          report.window_surface_plan_safe == 1 &&
          report.window_compositor_plan_safe == 1 &&
          report.window_damage_plan_safe == 1 &&
          report.window_present_plan_safe == 1 &&
          report.window_schedule_plan_safe == 1 &&
          report.window_vsync_plan_safe == 1 &&
          report.window_scanout_plan_safe == 1 &&
          report.window_display_plan_safe == 1 &&
          report.window_output_plan_safe == 1 &&
          report.window_blit_plan_safe == 1 &&
          report.window_commit_plan_safe == 1 &&
          report.window_flip_plan_safe == 1 &&
          report.window_vblank_plan_safe == 1 &&
          report.window_event_plan_safe == 1 &&
          report.window_input_plan_safe == 1,
      "credential pipeline safety report should mark every window layer safe");
  fails += expect_true(report.total_layers == 15 && report.layers_safe == 15 &&
                           report.layers_unsafe == 0,
                       "credential pipeline safety report should count 15 safe layers");
  fails += expect_true(report.gui_submit_blocked == 1 &&
                           report.auth_attempt_blocked == 1 &&
                           report.credentials_redacted == 1 &&
                           report.credentials_storage_wiped == 1 &&
                           report.text_login_authoritative == 1 &&
                           report.route_consistent == 1 &&
                           report.no_real_event_dispatched == 1 &&
                           report.no_real_input_dispatched == 1 &&
                           report.pipeline_safe == 1,
                       "credential pipeline safety report should keep every fail-closed invariant");
  fails += expect_true(
      strings_equal(report.deepest_safe_layer, "window_input_plan") &&
          strings_equal(report.first_unsafe_layer, "none") &&
          strings_equal(report.event_type,
                        "credential-screen-pipeline-safety-report-ready") &&
          strings_equal(report.state, "pipeline-safe") &&
          strings_equal(report.blocked_reason, "ready"),
      "credential pipeline safety report should report ready pipeline");
  return fails;
}

static int test_loginwindow_credential_screen_pipeline_safety_report_reports_safe_for_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_pipeline_safety_report report;

  fails += expect_true(
      build_loginwindow_credential_screen_pipeline_safety_report_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
          LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1, 1,
          &report) == 0,
      "credential pipeline safety report recovery should build");
  fails += expect_true(report.window_input_plan_safe == 1 &&
                           report.layers_safe == 15 &&
                           report.gui_submit_blocked == 1 &&
                           report.auth_attempt_blocked == 1 &&
                           report.no_real_event_dispatched == 1 &&
                           report.no_real_input_dispatched == 1 &&
                           report.pipeline_safe == 1,
                       "credential pipeline safety report recovery should remain safe");
  fails += expect_true(
      strings_equal(report.state, "pipeline-safe") &&
          strings_equal(report.event_type,
                        "credential-screen-pipeline-safety-report-ready"),
      "credential pipeline safety report recovery should report ready pipeline");

  fails += expect_true(
      build_loginwindow_credential_screen_pipeline_safety_report_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
          LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 1, 1, 0, 1,
          &report) == 0,
      "credential pipeline safety report resume should build");
  fails += expect_true(report.window_input_plan_safe == 1 &&
                           report.layers_safe == 15 &&
                           report.pipeline_safe == 1,
                       "credential pipeline safety report resume should remain safe");

  fails += expect_true(
      build_loginwindow_credential_screen_pipeline_safety_report_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_SUBMIT, 0, 0, 0, 0, 1,
          &report) == 0,
      "credential pipeline safety report submit fallback should build");
  fails += expect_true(report.window_input_plan_safe == 1 &&
                           report.layers_safe == 15 &&
                           report.gui_submit_blocked == 1 &&
                           report.auth_attempt_blocked == 1 &&
                           report.no_real_event_dispatched == 1 &&
                           report.no_real_input_dispatched == 1 &&
                           report.pipeline_safe == 1,
                       "credential pipeline safety report submit fallback should remain safe");

  fails += expect_true(
      build_loginwindow_credential_screen_pipeline_safety_report_for_action(
          9999, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0, 1,
          &report) == 0,
      "credential pipeline safety report unknown action should build");
  fails += expect_true(report.window_input_plan_safe == 1 &&
                           report.layers_safe == 15 &&
                           report.pipeline_safe == 1,
                       "credential pipeline safety report unknown action should remain safe");
  return fails;
}

static int test_loginwindow_credential_screen_pipeline_safety_report_fails_closed_for_unsafe_or_missing_input_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_window_input_plan input_plan;
  struct login_window_credential_screen_pipeline_safety_report report;

  fails += expect_true(
      login_window_credential_screen_pipeline_safety_report_build(
          NULL, &report) == 0,
      "credential pipeline safety report missing input plan should build fallback");
  fails += expect_true(report.window_input_plan_available == 0 &&
                           report.layers_safe == 0 &&
                           report.layers_unsafe == 15 &&
                           report.pipeline_safe == 0 &&
                           report.gui_submit_blocked == 0 &&
                           report.auth_attempt_blocked == 0 &&
                           report.credentials_redacted == 0 &&
                           report.text_login_authoritative == 0,
                       "credential pipeline safety report missing input plan should fail closed");
  fails += expect_true(
      strings_equal(report.deepest_safe_layer, "none") &&
          strings_equal(report.first_unsafe_layer, "window_surface_plan") &&
          strings_equal(report.event_type,
                        "credential-screen-pipeline-safety-report-unavailable") &&
          strings_equal(report.blocked_reason,
                        "window-input-plan-unavailable"),
      "credential pipeline safety report missing input plan should report missing upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_input_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0, 0, 1,
          &input_plan) == 0,
      "credential pipeline safety report unsafe input fixture should build");
  input_plan.window_input_plan_safe = 0;
  fails += expect_true(
      login_window_credential_screen_pipeline_safety_report_build(
          &input_plan, &report) == 0,
      "credential pipeline safety report unsafe input should build fallback");
  fails += expect_true(report.window_input_plan_safe == 0 &&
                           report.layers_safe == 14 &&
                           report.layers_unsafe == 1 &&
                           report.pipeline_safe == 0,
                       "credential pipeline safety report unsafe input should fail closed");
  fails += expect_true(
      strings_equal(report.deepest_safe_layer, "window_event_plan") &&
          strings_equal(report.first_unsafe_layer, "window_input_plan") &&
          strings_equal(report.event_type,
                        "credential-screen-pipeline-safety-report-unsafe") &&
          strings_equal(report.blocked_reason, "pipeline-safety-violated"),
      "credential pipeline safety report unsafe input should report deepest safe layer");

  fails += expect_true(
      build_loginwindow_credential_screen_window_input_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'd', 0, 0, 0, 1,
          &input_plan) == 0,
      "credential pipeline safety report dispatched fixture should build");
  input_plan.event_submitted = 1;
  input_plan.event_handler_armed = 1;
  input_plan.event_callback_submitted = 1;
  input_plan.input_keyboard_armed = 1;
  input_plan.input_callback_submitted = 1;
  fails += expect_true(
      login_window_credential_screen_pipeline_safety_report_build(
          &input_plan, &report) == 0,
      "credential pipeline safety report dispatched events/input should build fallback");
  fails += expect_true(report.no_real_event_dispatched == 0 &&
                           report.no_real_input_dispatched == 0 &&
                           report.pipeline_safe == 0,
                       "credential pipeline safety report should reject dispatched events/input");
  fails += expect_true(
      strings_equal(report.event_type,
                    "credential-screen-pipeline-safety-report-unsafe") &&
          strings_equal(report.blocked_reason, "pipeline-safety-violated"),
      "credential pipeline safety report dispatched events/input should report violated safety");

  fails += expect_true(
      build_loginwindow_credential_screen_window_input_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'e', 0, 0, 0, 1,
          &input_plan) == 0,
      "credential pipeline safety report exposed credentials fixture should build");
  input_plan.raw_secret_exposed = 1;
  input_plan.masked_text_exposed = 1;
  input_plan.credential_redacted = 0;
  fails += expect_true(
      login_window_credential_screen_pipeline_safety_report_build(
          &input_plan, &report) == 0,
      "credential pipeline safety report exposed credentials should build fallback");
  fails += expect_true(report.credentials_redacted == 0 &&
                           report.pipeline_safe == 0,
                       "credential pipeline safety report should reject exposed credentials");

  fails += expect_true(
      build_loginwindow_credential_screen_window_input_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'a', 0, 0, 0, 1,
          &input_plan) == 0,
      "credential pipeline safety report unblocked auth fixture should build");
  input_plan.auth_attempt_allowed = 1;
  input_plan.auth_callback_bound = 1;
  input_plan.submit_enabled = 1;
  input_plan.submit_blocked = 0;
  input_plan.submit_callback_bound = 1;
  fails += expect_true(
      login_window_credential_screen_pipeline_safety_report_build(
          &input_plan, &report) == 0,
      "credential pipeline safety report unblocked auth should build fallback");
  fails += expect_true(report.gui_submit_blocked == 0 &&
                           report.auth_attempt_blocked == 0 &&
                           report.pipeline_safe == 0,
                       "credential pipeline safety report should reject unblocked submit/auth");

  fails += expect_true(
      build_loginwindow_credential_screen_window_input_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 0, 0, 0, 1,
          &input_plan) == 0,
      "credential pipeline safety report inconsistent route fixture should build");
  input_plan.route_selected = 1;
  input_plan.route_blocked = 1;
  fails += expect_true(
      login_window_credential_screen_pipeline_safety_report_build(
          &input_plan, &report) == 0,
      "credential pipeline safety report inconsistent route should build fallback");
  fails += expect_true(report.route_consistent == 0 &&
                           report.pipeline_safe == 0,
                       "credential pipeline safety report should reject inconsistent routes");
  return fails;
}

int test_login_runtime_credential_pipeline_safety_report_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_pipeline_safety_report_reports_safe_pipeline_for_credential_route();
  fails += test_loginwindow_credential_screen_pipeline_safety_report_reports_safe_for_text_routes();
  fails += test_loginwindow_credential_screen_pipeline_safety_report_fails_closed_for_unsafe_or_missing_input_plan();
  return fails;
}
