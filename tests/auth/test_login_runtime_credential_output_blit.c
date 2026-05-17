/*
 * tests/auth/test_login_runtime_credential_output_blit.c
 *
 * Credential screen output plan + blit plan coverage for the
 * `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-15 monolith
 * refactor (PR D.18 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_output_plan_build`: 4 tests
 *     covering the credential widgets output + the text-route
 *     output (recovery + resume) + the submit/unknown fallback
 *     output + the missing-or-unsafe display plan fail-closed
 *     default.
 *   - `login_window_credential_screen_blit_plan_build`: 4 tests
 *     covering the credential widgets blit + the text-route blit
 *     (recovery + resume) + the submit/unknown fallback blit + the
 *     missing-or-unsafe output plan fail-closed default.
 *
 * Also exposes shared helpers
 * `build_loginwindow_credential_screen_output_plan_for_action` and
 * `build_loginwindow_credential_screen_blit_plan_for_action`, used
 * by later companion files that chain on top of the output/blit
 * stages (framebuffer, flush, ...).
 *
 * The companion entry `test_login_runtime_credential_output_blit_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_output_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_output_plan *output_plan) {
  struct login_window_credential_screen_display_plan display_plan;

  if (build_loginwindow_credential_screen_display_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &display_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_output_plan_build(&display_plan,
                                                          output_plan);
}

static int test_loginwindow_credential_screen_output_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_output_plan output_plan;

  fails += expect_true(build_loginwindow_credential_screen_output_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'p', 0, 0, 0,
                           1, &output_plan) == 0,
                       "credential output plan edit should build");
  fails += expect_true(output_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_OUTPUT_PLAN_VERSION,
                       "credential output plan should expose stable version");
  fails += expect_true(output_plan.display_plan_available == 1 &&
                           output_plan.display_plan_safe == 1 &&
                           output_plan.output_plan_safe == 1,
                       "credential output plan should require safe display plan");
  fails += expect_true(output_plan.output_allowed == 1 &&
                           output_plan.output_submitted == 0 &&
                           output_plan.output_ticket_selected == 1 &&
                           output_plan.output_target_selected == 1 &&
                           output_plan.output_buffer_attached == 0 &&
                           output_plan.output_buffer_submitted == 0 &&
                           output_plan.output_flip_allowed == 0 &&
                           output_plan.output_flip_submitted == 0,
                       "credential output plan should remain declarative");
  fails += expect_true(output_plan.display_submitted == 0 &&
                           output_plan.display_buffer_attached == 0 &&
                           output_plan.display_buffer_submitted == 0 &&
                           output_plan.display_mode_committed == 0 &&
                           output_plan.display_flip_submitted == 0 &&
                           output_plan.scanout_submitted == 0 &&
                           output_plan.vsync_submitted == 0 &&
                           output_plan.schedule_submitted == 0 &&
                           output_plan.present_submitted == 0 &&
                           output_plan.damage_submitted == 0 &&
                           output_plan.page_flip_submitted == 0,
                       "credential output plan must not submit upstream GUI work");
  fails += expect_true(output_plan.schedule_incremental_allowed == 1 &&
                           output_plan.full_schedule_required == 0 &&
                           output_plan.schedule_cache_allowed == 1 &&
                           output_plan.schedule_reuse_allowed == 1 &&
                           output_plan.schedule_cache_hit == 0,
                       "credential output plan should preserve scalable planning");
  fails += expect_true(output_plan.output_credential_panel == 1 &&
                           output_plan.output_credential_input == 1 &&
                           output_plan.output_credential_focus == 1,
                       "credential output plan should mark credential widgets");
  fails += expect_true(output_plan.submit_callback_bound == 0 &&
                           output_plan.auth_callback_bound == 0 &&
                           output_plan.submit_enabled == 0 &&
                           output_plan.auth_attempt_allowed == 0 &&
                           output_plan.raw_secret_exposed == 0 &&
                           output_plan.masked_text_exposed == 0 &&
                           output_plan.length_redacted == 1,
                       "credential output plan must stay redacted and auth-disabled");
  fails += expect_true(strings_equal(output_plan.output_ticket,
                                     "credential-screen-output-ticket") &&
                           strings_equal(output_plan.display_ticket,
                                         "credential-screen-display-ticket") &&
                           strings_equal(output_plan.output_policy,
                                         "incremental-output-declarative") &&
                           strings_equal(output_plan.state,
                                         "output-credential-ready"),
                       "credential output plan should report output ticket");
  return fails;
}

static int test_loginwindow_credential_screen_output_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_output_plan output_plan;

  fails += expect_true(build_loginwindow_credential_screen_output_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &output_plan) == 0,
                       "credential output plan recovery should build");
  fails += expect_true(output_plan.output_plan_safe == 1 &&
                           output_plan.output_allowed == 1 &&
                           output_plan.output_submitted == 0 &&
                           output_plan.output_text_recovery == 1 &&
                           output_plan.output_text_login == 1 &&
                           output_plan.output_credential_focus == 0,
                       "credential output plan recovery should mark text recovery");
  fails += expect_true(output_plan.output_buffer_attached == 0 &&
                           output_plan.output_flip_submitted == 0 &&
                           output_plan.display_buffer_attached == 0 &&
                           output_plan.display_mode_committed == 0 &&
                           output_plan.display_flip_submitted == 0 &&
                           output_plan.page_flip_submitted == 0 &&
                           output_plan.submit_enabled == 0 &&
                           output_plan.auth_attempt_allowed == 0,
                       "credential output plan recovery must not submit real visual output");
  fails += expect_true(strings_equal(output_plan.output_ticket,
                                     "text-recovery-output-ticket") &&
                           strings_equal(output_plan.compositor_target,
                                         "text-recovery-output") &&
                           strings_equal(output_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential output plan recovery should report recovery ticket");

  fails += expect_true(build_loginwindow_credential_screen_output_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &output_plan) == 0,
                       "credential output plan resume should build");
  fails += expect_true(output_plan.output_plan_safe == 1 &&
                           output_plan.output_text_login_resume == 1 &&
                           output_plan.session_reset_required == 1 &&
                           output_plan.login_screen_rerender_required == 1 &&
                           output_plan.schedule_reuse_allowed == 0 &&
                           output_plan.schedule_cache_allowed == 0 &&
                           output_plan.full_schedule_required == 1 &&
                           output_plan.schedule_incremental_allowed == 0,
                       "credential output plan resume should require full planning");
  fails += expect_true(output_plan.output_submitted == 0 &&
                           output_plan.output_buffer_submitted == 0 &&
                           output_plan.output_flip_submitted == 0 &&
                           output_plan.display_submitted == 0 &&
                           output_plan.display_mode_committed == 0 &&
                           output_plan.scanout_submitted == 0 &&
                           output_plan.vsync_submitted == 0 &&
                           output_plan.submit_enabled == 0 &&
                           output_plan.auth_attempt_allowed == 0,
                       "credential output plan resume must keep GUI auth disabled");
  fails += expect_true(strings_equal(output_plan.output_ticket,
                                     "text-login-resume-output-ticket") &&
                           strings_equal(output_plan.output_policy,
                                         "full-output-declarative") &&
                           strings_equal(output_plan.state,
                                         "output-resume-ready"),
                       "credential output plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_output_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_output_plan output_plan;

  fails += expect_true(build_loginwindow_credential_screen_output_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &output_plan) == 0,
                       "credential output plan submit should build");
  fails += expect_true(output_plan.output_plan_safe == 1 &&
                           output_plan.submit_requested == 1 &&
                           output_plan.output_text_login_fallback == 1 &&
                           output_plan.action_allowed == 0 &&
                           output_plan.action_blocked == 1 &&
                           output_plan.input_focus_allowed == 0,
                       "credential output plan submit should force text login fallback");
  fails += expect_true(output_plan.output_allowed == 1 &&
                           output_plan.output_submitted == 0 &&
                           output_plan.output_buffer_attached == 0 &&
                           output_plan.output_buffer_submitted == 0 &&
                           output_plan.output_flip_submitted == 0 &&
                           output_plan.display_submitted == 0 &&
                           output_plan.display_buffer_attached == 0 &&
                           output_plan.display_mode_committed == 0 &&
                           output_plan.page_flip_submitted == 0 &&
                           output_plan.submit_callback_bound == 0 &&
                           output_plan.auth_callback_bound == 0 &&
                           output_plan.submit_enabled == 0 &&
                           output_plan.auth_attempt_allowed == 0,
                       "credential output plan submit must stay declarative");
  fails += expect_true(strings_equal(output_plan.output_ticket,
                                     "text-login-fallback-output-ticket") &&
                           strings_equal(output_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(output_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential output plan submit should explain fallback");

  fails += expect_true(build_loginwindow_credential_screen_output_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &output_plan) == 0,
                       "credential output plan unknown should build");
  fails += expect_true(output_plan.output_plan_safe == 1 &&
                           output_plan.output_text_login_fallback == 1 &&
                           output_plan.action_allowed == 0 &&
                           output_plan.action_blocked == 1,
                       "credential output plan unknown should force text login fallback");
  fails += expect_true(strings_equal(output_plan.output_ticket,
                                     "text-login-fallback-output-ticket") &&
                           strings_equal(output_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential output plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_output_plan_fails_closed_for_unsafe_or_missing_display_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_display_plan display_plan;
  struct login_window_credential_screen_output_plan output_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_output_plan_build(
                           NULL, &output_plan) == 0,
                       "credential output plan missing display plan should build fail-closed state");
  fails += expect_true(output_plan.display_plan_available == 0 &&
                           output_plan.display_plan_safe == 0 &&
                           output_plan.output_plan_safe == 0 &&
                           output_plan.route_selected == 0 &&
                           output_plan.route_blocked == 1,
                       "credential output plan missing display plan should block output plan");
  fails += expect_true(output_plan.output_allowed == 0 &&
                           output_plan.output_submitted == 0 &&
                           output_plan.output_buffer_attached == 0 &&
                           output_plan.output_flip_submitted == 0 &&
                           output_plan.display_submitted == 0 &&
                           output_plan.display_buffer_attached == 0 &&
                           output_plan.display_mode_committed == 0 &&
                           output_plan.scanout_submitted == 0 &&
                           output_plan.vsync_submitted == 0 &&
                           output_plan.page_flip_submitted == 0 &&
                           output_plan.output_text_login_fallback == 1 &&
                           output_plan.submit_enabled == 0 &&
                           output_plan.auth_attempt_allowed == 0,
                       "credential output plan missing display plan must stay redacted");
  fails += expect_true(strings_equal(output_plan.output_ticket,
                                     "text-login-fallback-output-ticket") &&
                           strings_equal(output_plan.event_type,
                                         "credential-screen-output-plan-unavailable") &&
                           strings_equal(output_plan.blocked_reason,
                                         "display-plan-unavailable"),
                       "credential output plan missing display plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_display_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &display_plan) == 0,
                       "credential output plan unsafe display source should build");
  fails += expect_true(login_window_credential_screen_output_plan_build(
                           &display_plan, &output_plan) == 0,
                       "credential output plan unsafe display plan should build blocked state");
  fails += expect_true(output_plan.display_plan_available == 1 &&
                           output_plan.display_plan_safe == 0 &&
                           output_plan.output_plan_safe == 0 &&
                           output_plan.route_selected == 0 &&
                           output_plan.route_blocked == 1,
                       "credential output plan unsafe display plan should block output plan");
  fails += expect_true(output_plan.output_allowed == 0 &&
                           output_plan.output_submitted == 0 &&
                           output_plan.output_credential_focus == 0 &&
                           output_plan.output_text_login_fallback == 1 &&
                           output_plan.submit_enabled == 0 &&
                           output_plan.auth_attempt_allowed == 0,
                       "credential output plan unsafe display plan must force text login fallback");
  fails += expect_true(strings_equal(output_plan.output_ticket,
                                     "text-login-fallback-output-ticket") &&
                           strings_equal(output_plan.event_type,
                                         "credential-screen-output-plan-unsafe") &&
                           strings_equal(output_plan.blocked_reason,
                                         "credential-output-plan-unsafe"),
                       "credential output plan unsafe display plan should force text login");

  fails += expect_true(build_loginwindow_credential_screen_display_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'y', 0, 0, 0,
                           1, &display_plan) == 0,
                       "credential output plan submitted display source should build");
  display_plan.display_submitted = 1;
  display_plan.display_buffer_attached = 1;
  display_plan.display_flip_allowed = 1;
  display_plan.display_flip_submitted = 1;
  display_plan.page_flip_allowed = 1;
  fails += expect_true(login_window_credential_screen_output_plan_build(
                           &display_plan, &output_plan) == 0,
                       "credential output plan submitted display should fail closed");
  fails += expect_true(output_plan.output_plan_safe == 0 &&
                           output_plan.output_allowed == 0 &&
                           output_plan.output_submitted == 0 &&
                           output_plan.output_buffer_attached == 0 &&
                           output_plan.output_flip_allowed == 0 &&
                           output_plan.output_flip_submitted == 0 &&
                           output_plan.display_submitted == 0 &&
                           output_plan.display_buffer_attached == 0 &&
                           output_plan.display_flip_allowed == 0 &&
                           output_plan.page_flip_allowed == 0 &&
                           output_plan.page_flip_submitted == 0 &&
                           output_plan.submit_enabled == 0 &&
                           output_plan.auth_attempt_allowed == 0,
                       "credential output plan must not copy unsafe submitted state");
  return fails;
}


int build_loginwindow_credential_screen_blit_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_blit_plan *blit_plan) {
  struct login_window_credential_screen_output_plan output_plan;

  if (build_loginwindow_credential_screen_output_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &output_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_blit_plan_build(&output_plan,
                                                        blit_plan);
}

static int test_loginwindow_credential_screen_blit_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_blit_plan blit_plan;

  fails += expect_true(build_loginwindow_credential_screen_blit_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'p', 0, 0, 0,
                           1, &blit_plan) == 0,
                       "credential blit plan edit should build");
  fails += expect_true(blit_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_BLIT_PLAN_VERSION,
                       "credential blit plan should expose stable version");
  fails += expect_true(blit_plan.output_plan_available == 1 &&
                           blit_plan.output_plan_safe == 1 &&
                           blit_plan.blit_plan_safe == 1,
                       "credential blit plan should require safe output plan");
  fails += expect_true(blit_plan.blit_allowed == 1 &&
                           blit_plan.blit_submitted == 0 &&
                           blit_plan.blit_ticket_selected == 1 &&
                           blit_plan.blit_target_selected == 1 &&
                           blit_plan.blit_source_buffer_mapped == 0 &&
                           blit_plan.blit_destination_buffer_mapped == 0 &&
                           blit_plan.blit_pixels_copied == 0 &&
                           blit_plan.blit_dma_allowed == 0 &&
                           blit_plan.blit_dma_submitted == 0,
                       "credential blit plan should remain declarative");
  fails += expect_true(blit_plan.output_submitted == 0 &&
                           blit_plan.output_buffer_attached == 0 &&
                           blit_plan.output_buffer_submitted == 0 &&
                           blit_plan.output_flip_submitted == 0 &&
                           blit_plan.display_submitted == 0 &&
                           blit_plan.display_mode_committed == 0 &&
                           blit_plan.scanout_submitted == 0 &&
                           blit_plan.vsync_submitted == 0 &&
                           blit_plan.schedule_submitted == 0 &&
                           blit_plan.present_submitted == 0 &&
                           blit_plan.damage_submitted == 0 &&
                           blit_plan.page_flip_submitted == 0,
                       "credential blit plan must not submit upstream GUI work");
  fails += expect_true(blit_plan.schedule_incremental_allowed == 1 &&
                           blit_plan.full_schedule_required == 0 &&
                           blit_plan.schedule_cache_allowed == 1 &&
                           blit_plan.schedule_reuse_allowed == 1 &&
                           blit_plan.schedule_cache_hit == 0,
                       "credential blit plan should preserve scalable planning");
  fails += expect_true(blit_plan.blit_credential_panel == 1 &&
                           blit_plan.blit_credential_input == 1 &&
                           blit_plan.blit_credential_focus == 1,
                       "credential blit plan should mark credential widgets");
  fails += expect_true(blit_plan.submit_callback_bound == 0 &&
                           blit_plan.auth_callback_bound == 0 &&
                           blit_plan.submit_enabled == 0 &&
                           blit_plan.auth_attempt_allowed == 0 &&
                           blit_plan.raw_secret_exposed == 0 &&
                           blit_plan.masked_text_exposed == 0 &&
                           blit_plan.length_redacted == 1,
                       "credential blit plan must stay redacted and auth-disabled");
  fails += expect_true(strings_equal(blit_plan.blit_ticket,
                                     "credential-screen-blit-ticket") &&
                           strings_equal(blit_plan.output_ticket,
                                         "credential-screen-output-ticket") &&
                           strings_equal(blit_plan.blit_policy,
                                         "incremental-blit-declarative") &&
                           strings_equal(blit_plan.state,
                                         "blit-credential-ready"),
                       "credential blit plan should report blit ticket");
  return fails;
}

static int test_loginwindow_credential_screen_blit_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_blit_plan blit_plan;

  fails += expect_true(build_loginwindow_credential_screen_blit_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &blit_plan) == 0,
                       "credential blit plan recovery should build");
  fails += expect_true(blit_plan.blit_plan_safe == 1 &&
                           blit_plan.blit_allowed == 1 &&
                           blit_plan.blit_submitted == 0 &&
                           blit_plan.blit_text_recovery == 1 &&
                           blit_plan.blit_text_login == 1 &&
                           blit_plan.blit_credential_focus == 0,
                       "credential blit plan recovery should mark text recovery");
  fails += expect_true(blit_plan.blit_source_buffer_mapped == 0 &&
                           blit_plan.blit_destination_buffer_mapped == 0 &&
                           blit_plan.blit_pixels_copied == 0 &&
                           blit_plan.blit_dma_submitted == 0 &&
                           blit_plan.output_buffer_attached == 0 &&
                           blit_plan.display_mode_committed == 0 &&
                           blit_plan.output_flip_submitted == 0 &&
                           blit_plan.page_flip_submitted == 0 &&
                           blit_plan.submit_enabled == 0 &&
                           blit_plan.auth_attempt_allowed == 0,
                       "credential blit plan recovery must not copy pixels or submit output");
  fails += expect_true(strings_equal(blit_plan.blit_ticket,
                                     "text-recovery-blit-ticket") &&
                           strings_equal(blit_plan.compositor_target,
                                         "text-recovery-blit") &&
                           strings_equal(blit_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential blit plan recovery should report recovery ticket");

  fails += expect_true(build_loginwindow_credential_screen_blit_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &blit_plan) == 0,
                       "credential blit plan resume should build");
  fails += expect_true(blit_plan.blit_plan_safe == 1 &&
                           blit_plan.blit_text_login_resume == 1 &&
                           blit_plan.session_reset_required == 1 &&
                           blit_plan.login_screen_rerender_required == 1 &&
                           blit_plan.schedule_reuse_allowed == 0 &&
                           blit_plan.schedule_cache_allowed == 0 &&
                           blit_plan.full_schedule_required == 1 &&
                           blit_plan.schedule_incremental_allowed == 0,
                       "credential blit plan resume should require full planning");
  fails += expect_true(blit_plan.blit_submitted == 0 &&
                           blit_plan.blit_pixels_copied == 0 &&
                           blit_plan.output_submitted == 0 &&
                           blit_plan.display_submitted == 0 &&
                           blit_plan.display_mode_committed == 0 &&
                           blit_plan.scanout_submitted == 0 &&
                           blit_plan.vsync_submitted == 0 &&
                           blit_plan.submit_enabled == 0 &&
                           blit_plan.auth_attempt_allowed == 0,
                       "credential blit plan resume must keep GUI auth disabled");
  fails += expect_true(strings_equal(blit_plan.blit_ticket,
                                     "text-login-resume-blit-ticket") &&
                           strings_equal(blit_plan.blit_policy,
                                         "full-blit-declarative") &&
                           strings_equal(blit_plan.state,
                                         "blit-resume-ready"),
                       "credential blit plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_blit_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_blit_plan blit_plan;

  fails += expect_true(build_loginwindow_credential_screen_blit_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &blit_plan) == 0,
                       "credential blit plan submit should build");
  fails += expect_true(blit_plan.blit_plan_safe == 1 &&
                           blit_plan.submit_requested == 1 &&
                           blit_plan.blit_text_login_fallback == 1 &&
                           blit_plan.action_allowed == 0 &&
                           blit_plan.action_blocked == 1 &&
                           blit_plan.input_focus_allowed == 0,
                       "credential blit plan submit should force text login fallback");
  fails += expect_true(blit_plan.blit_allowed == 1 &&
                           blit_plan.blit_submitted == 0 &&
                           blit_plan.blit_source_buffer_mapped == 0 &&
                           blit_plan.blit_destination_buffer_mapped == 0 &&
                           blit_plan.blit_pixels_copied == 0 &&
                           blit_plan.blit_dma_submitted == 0 &&
                           blit_plan.output_submitted == 0 &&
                           blit_plan.display_submitted == 0 &&
                           blit_plan.page_flip_submitted == 0 &&
                           blit_plan.submit_callback_bound == 0 &&
                           blit_plan.auth_callback_bound == 0 &&
                           blit_plan.submit_enabled == 0 &&
                           blit_plan.auth_attempt_allowed == 0,
                       "credential blit plan submit must stay declarative");
  fails += expect_true(strings_equal(blit_plan.blit_ticket,
                                     "text-login-fallback-blit-ticket") &&
                           strings_equal(blit_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(blit_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential blit plan submit should explain fallback");

  fails += expect_true(build_loginwindow_credential_screen_blit_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &blit_plan) == 0,
                       "credential blit plan unknown should build");
  fails += expect_true(blit_plan.blit_plan_safe == 1 &&
                           blit_plan.blit_text_login_fallback == 1 &&
                           blit_plan.action_allowed == 0 &&
                           blit_plan.action_blocked == 1,
                       "credential blit plan unknown should force text login fallback");
  fails += expect_true(strings_equal(blit_plan.blit_ticket,
                                     "text-login-fallback-blit-ticket") &&
                           strings_equal(blit_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential blit plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_blit_plan_fails_closed_for_unsafe_or_missing_output_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_output_plan output_plan;
  struct login_window_credential_screen_blit_plan blit_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_blit_plan_build(
                           NULL, &blit_plan) == 0,
                       "credential blit plan missing output plan should build fail-closed state");
  fails += expect_true(blit_plan.output_plan_available == 0 &&
                           blit_plan.output_plan_safe == 0 &&
                           blit_plan.blit_plan_safe == 0 &&
                           blit_plan.route_selected == 0 &&
                           blit_plan.route_blocked == 1,
                       "credential blit plan missing output plan should block blit plan");
  fails += expect_true(blit_plan.blit_allowed == 0 &&
                           blit_plan.blit_submitted == 0 &&
                           blit_plan.blit_source_buffer_mapped == 0 &&
                           blit_plan.blit_destination_buffer_mapped == 0 &&
                           blit_plan.blit_pixels_copied == 0 &&
                           blit_plan.blit_dma_submitted == 0 &&
                           blit_plan.output_submitted == 0 &&
                           blit_plan.display_submitted == 0 &&
                           blit_plan.page_flip_submitted == 0 &&
                           blit_plan.blit_text_login_fallback == 1 &&
                           blit_plan.submit_enabled == 0 &&
                           blit_plan.auth_attempt_allowed == 0,
                       "credential blit plan missing output plan must stay redacted");
  fails += expect_true(strings_equal(blit_plan.blit_ticket,
                                     "text-login-fallback-blit-ticket") &&
                           strings_equal(blit_plan.event_type,
                                         "credential-screen-blit-plan-unavailable") &&
                           strings_equal(blit_plan.blocked_reason,
                                         "output-plan-unavailable"),
                       "credential blit plan missing output plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_output_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &output_plan) == 0,
                       "credential blit plan unsafe output source should build");
  fails += expect_true(login_window_credential_screen_blit_plan_build(
                           &output_plan, &blit_plan) == 0,
                       "credential blit plan unsafe output plan should build blocked state");
  fails += expect_true(blit_plan.output_plan_available == 1 &&
                           blit_plan.output_plan_safe == 0 &&
                           blit_plan.blit_plan_safe == 0 &&
                           blit_plan.route_selected == 0 &&
                           blit_plan.route_blocked == 1,
                       "credential blit plan unsafe output plan should block blit plan");
  fails += expect_true(blit_plan.blit_allowed == 0 &&
                           blit_plan.blit_submitted == 0 &&
                           blit_plan.blit_credential_focus == 0 &&
                           blit_plan.blit_text_login_fallback == 1 &&
                           blit_plan.submit_enabled == 0 &&
                           blit_plan.auth_attempt_allowed == 0,
                       "credential blit plan unsafe output plan must force text login fallback");
  fails += expect_true(strings_equal(blit_plan.blit_ticket,
                                     "text-login-fallback-blit-ticket") &&
                           strings_equal(blit_plan.event_type,
                                         "credential-screen-blit-plan-unsafe") &&
                           strings_equal(blit_plan.blocked_reason,
                                         "credential-blit-plan-unsafe"),
                       "credential blit plan unsafe output plan should force text login");

  fails += expect_true(build_loginwindow_credential_screen_output_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'y', 0, 0, 0,
                           1, &output_plan) == 0,
                       "credential blit plan submitted output source should build");
  output_plan.output_submitted = 1;
  output_plan.output_buffer_attached = 1;
  output_plan.output_flip_allowed = 1;
  output_plan.output_flip_submitted = 1;
  output_plan.page_flip_allowed = 1;
  fails += expect_true(login_window_credential_screen_blit_plan_build(
                           &output_plan, &blit_plan) == 0,
                       "credential blit plan submitted output should fail closed");
  fails += expect_true(blit_plan.blit_plan_safe == 0 &&
                           blit_plan.blit_allowed == 0 &&
                           blit_plan.blit_submitted == 0 &&
                           blit_plan.blit_source_buffer_mapped == 0 &&
                           blit_plan.blit_destination_buffer_mapped == 0 &&
                           blit_plan.blit_pixels_copied == 0 &&
                           blit_plan.blit_dma_allowed == 0 &&
                           blit_plan.output_submitted == 0 &&
                           blit_plan.output_buffer_attached == 0 &&
                           blit_plan.output_flip_allowed == 0 &&
                           blit_plan.page_flip_allowed == 0 &&
                           blit_plan.page_flip_submitted == 0 &&
                           blit_plan.submit_enabled == 0 &&
                           blit_plan.auth_attempt_allowed == 0,
                       "credential blit plan must not copy unsafe submitted state");
  return fails;
}

int test_login_runtime_credential_output_blit_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_output_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_output_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_output_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_output_plan_fails_closed_for_unsafe_or_missing_display_plan();
  fails += test_loginwindow_credential_screen_blit_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_blit_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_blit_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_blit_plan_fails_closed_for_unsafe_or_missing_output_plan();
  return fails;
}
