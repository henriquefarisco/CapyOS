/*
 * tests/auth/test_login_runtime_credential_framebuffer_flush.c
 *
 * Credential screen framebuffer plan + flush plan coverage for the
 * `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-16 monolith
 * refactor (PR D.19 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_framebuffer_plan_build`: 4 tests
 *     covering the credential widgets framebuffer + the text-route
 *     framebuffer (recovery + resume) + the submit/unknown fallback
 *     framebuffer + the missing-or-unsafe blit plan fail-closed
 *     default.
 *   - `login_window_credential_screen_flush_plan_build`: 4 tests
 *     covering the credential widgets flush + the text-route flush
 *     (recovery + resume) + the submit/unknown fallback flush + the
 *     missing-or-unsafe framebuffer plan fail-closed default.
 *
 * Also exposes shared helpers
 * `build_loginwindow_credential_screen_framebuffer_plan_for_action`
 * and `build_loginwindow_credential_screen_flush_plan_for_action`,
 * used by later companion files that chain on top of the
 * framebuffer/flush stages (barrier, ...).
 *
 * The companion entry `test_login_runtime_credential_framebuffer_flush_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_framebuffer_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_framebuffer_plan *framebuffer_plan) {
  struct login_window_credential_screen_blit_plan blit_plan;

  if (build_loginwindow_credential_screen_blit_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &blit_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_framebuffer_plan_build(&blit_plan,
                                                               framebuffer_plan);
}

static int test_loginwindow_credential_screen_framebuffer_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_framebuffer_plan framebuffer_plan;

  fails += expect_true(build_loginwindow_credential_screen_framebuffer_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'p', 0, 0, 0,
                           1, &framebuffer_plan) == 0,
                       "credential framebuffer plan edit should build");
  fails += expect_true(framebuffer_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_FRAMEBUFFER_PLAN_VERSION,
                       "credential framebuffer plan should expose stable version");
  fails += expect_true(framebuffer_plan.blit_plan_available == 1 &&
                           framebuffer_plan.blit_plan_safe == 1 &&
                           framebuffer_plan.framebuffer_plan_safe == 1,
                       "credential framebuffer plan should require safe blit plan");
  fails += expect_true(framebuffer_plan.framebuffer_allowed == 1 &&
                           framebuffer_plan.framebuffer_submitted == 0 &&
                           framebuffer_plan.framebuffer_ticket_selected == 1 &&
                           framebuffer_plan.framebuffer_target_selected == 1 &&
                           framebuffer_plan.framebuffer_mapped == 0 &&
                           framebuffer_plan.framebuffer_write_allowed == 0 &&
                           framebuffer_plan.framebuffer_written == 0 &&
                           framebuffer_plan.framebuffer_flushed == 0 &&
                           framebuffer_plan.framebuffer_cache_cleaned == 0,
                       "credential framebuffer plan should remain declarative");
  fails += expect_true(framebuffer_plan.blit_submitted == 0 &&
                           framebuffer_plan.blit_source_buffer_mapped == 0 &&
                           framebuffer_plan.blit_destination_buffer_mapped == 0 &&
                           framebuffer_plan.blit_pixels_copied == 0 &&
                           framebuffer_plan.blit_dma_submitted == 0 &&
                           framebuffer_plan.output_submitted == 0 &&
                           framebuffer_plan.display_submitted == 0 &&
                           framebuffer_plan.display_mode_committed == 0 &&
                           framebuffer_plan.scanout_submitted == 0 &&
                           framebuffer_plan.vsync_submitted == 0 &&
                           framebuffer_plan.schedule_submitted == 0 &&
                           framebuffer_plan.present_submitted == 0 &&
                           framebuffer_plan.damage_submitted == 0 &&
                           framebuffer_plan.page_flip_submitted == 0,
                       "credential framebuffer plan must not submit upstream GUI work");
  fails += expect_true(framebuffer_plan.schedule_incremental_allowed == 1 &&
                           framebuffer_plan.full_schedule_required == 0 &&
                           framebuffer_plan.schedule_cache_allowed == 1 &&
                           framebuffer_plan.schedule_reuse_allowed == 1 &&
                           framebuffer_plan.schedule_cache_hit == 0,
                       "credential framebuffer plan should preserve scalable planning");
  fails += expect_true(framebuffer_plan.framebuffer_credential_panel == 1 &&
                           framebuffer_plan.framebuffer_credential_input == 1 &&
                           framebuffer_plan.framebuffer_credential_focus == 1,
                       "credential framebuffer plan should mark credential widgets");
  fails += expect_true(framebuffer_plan.submit_callback_bound == 0 &&
                           framebuffer_plan.auth_callback_bound == 0 &&
                           framebuffer_plan.submit_enabled == 0 &&
                           framebuffer_plan.auth_attempt_allowed == 0 &&
                           framebuffer_plan.raw_secret_exposed == 0 &&
                           framebuffer_plan.masked_text_exposed == 0 &&
                           framebuffer_plan.length_redacted == 1,
                       "credential framebuffer plan must stay redacted and auth-disabled");
  fails += expect_true(strings_equal(framebuffer_plan.framebuffer_ticket,
                                     "credential-screen-framebuffer-ticket") &&
                           strings_equal(framebuffer_plan.blit_ticket,
                                         "credential-screen-blit-ticket") &&
                           strings_equal(framebuffer_plan.framebuffer_policy,
                                         "incremental-framebuffer-declarative") &&
                           strings_equal(framebuffer_plan.state,
                                         "framebuffer-credential-ready"),
                       "credential framebuffer plan should report framebuffer ticket");
  return fails;
}

static int test_loginwindow_credential_screen_framebuffer_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_framebuffer_plan framebuffer_plan;

  fails += expect_true(build_loginwindow_credential_screen_framebuffer_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &framebuffer_plan) == 0,
                       "credential framebuffer plan recovery should build");
  fails += expect_true(framebuffer_plan.framebuffer_plan_safe == 1 &&
                           framebuffer_plan.framebuffer_allowed == 1 &&
                           framebuffer_plan.framebuffer_submitted == 0 &&
                           framebuffer_plan.framebuffer_text_recovery == 1 &&
                           framebuffer_plan.framebuffer_text_login == 1 &&
                           framebuffer_plan.framebuffer_credential_focus == 0,
                       "credential framebuffer plan recovery should mark text recovery");
  fails += expect_true(framebuffer_plan.framebuffer_mapped == 0 &&
                           framebuffer_plan.framebuffer_write_allowed == 0 &&
                           framebuffer_plan.framebuffer_written == 0 &&
                           framebuffer_plan.framebuffer_flushed == 0 &&
                           framebuffer_plan.framebuffer_cache_cleaned == 0 &&
                           framebuffer_plan.blit_pixels_copied == 0 &&
                           framebuffer_plan.output_submitted == 0 &&
                           framebuffer_plan.display_mode_committed == 0 &&
                           framebuffer_plan.page_flip_submitted == 0 &&
                           framebuffer_plan.submit_enabled == 0 &&
                           framebuffer_plan.auth_attempt_allowed == 0,
                       "credential framebuffer plan recovery must not map or write framebuffer");
  fails += expect_true(strings_equal(framebuffer_plan.framebuffer_ticket,
                                     "text-recovery-framebuffer-ticket") &&
                           strings_equal(framebuffer_plan.compositor_target,
                                         "text-recovery-framebuffer") &&
                           strings_equal(framebuffer_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential framebuffer plan recovery should report recovery ticket");

  fails += expect_true(build_loginwindow_credential_screen_framebuffer_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &framebuffer_plan) == 0,
                       "credential framebuffer plan resume should build");
  fails += expect_true(framebuffer_plan.framebuffer_plan_safe == 1 &&
                           framebuffer_plan.framebuffer_text_login_resume == 1 &&
                           framebuffer_plan.session_reset_required == 1 &&
                           framebuffer_plan.login_screen_rerender_required == 1 &&
                           framebuffer_plan.schedule_reuse_allowed == 0 &&
                           framebuffer_plan.schedule_cache_allowed == 0 &&
                           framebuffer_plan.full_schedule_required == 1 &&
                           framebuffer_plan.schedule_incremental_allowed == 0,
                       "credential framebuffer plan resume should require full planning");
  fails += expect_true(framebuffer_plan.framebuffer_submitted == 0 &&
                           framebuffer_plan.framebuffer_mapped == 0 &&
                           framebuffer_plan.framebuffer_written == 0 &&
                           framebuffer_plan.blit_submitted == 0 &&
                           framebuffer_plan.output_submitted == 0 &&
                           framebuffer_plan.display_submitted == 0 &&
                           framebuffer_plan.scanout_submitted == 0 &&
                           framebuffer_plan.submit_enabled == 0 &&
                           framebuffer_plan.auth_attempt_allowed == 0,
                       "credential framebuffer plan resume must keep GUI auth disabled");
  fails += expect_true(strings_equal(framebuffer_plan.framebuffer_ticket,
                                     "text-login-resume-framebuffer-ticket") &&
                           strings_equal(framebuffer_plan.framebuffer_policy,
                                         "full-framebuffer-declarative") &&
                           strings_equal(framebuffer_plan.state,
                                         "framebuffer-resume-ready"),
                       "credential framebuffer plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_framebuffer_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_framebuffer_plan framebuffer_plan;

  fails += expect_true(build_loginwindow_credential_screen_framebuffer_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &framebuffer_plan) == 0,
                       "credential framebuffer plan submit should build");
  fails += expect_true(framebuffer_plan.framebuffer_plan_safe == 1 &&
                           framebuffer_plan.submit_requested == 1 &&
                           framebuffer_plan.framebuffer_text_login_fallback == 1 &&
                           framebuffer_plan.action_allowed == 0 &&
                           framebuffer_plan.action_blocked == 1 &&
                           framebuffer_plan.input_focus_allowed == 0,
                       "credential framebuffer plan submit should force text login fallback");
  fails += expect_true(framebuffer_plan.framebuffer_allowed == 1 &&
                           framebuffer_plan.framebuffer_submitted == 0 &&
                           framebuffer_plan.framebuffer_mapped == 0 &&
                           framebuffer_plan.framebuffer_write_allowed == 0 &&
                           framebuffer_plan.framebuffer_written == 0 &&
                           framebuffer_plan.framebuffer_flushed == 0 &&
                           framebuffer_plan.blit_pixels_copied == 0 &&
                           framebuffer_plan.output_submitted == 0 &&
                           framebuffer_plan.display_submitted == 0 &&
                           framebuffer_plan.page_flip_submitted == 0 &&
                           framebuffer_plan.submit_callback_bound == 0 &&
                           framebuffer_plan.auth_callback_bound == 0 &&
                           framebuffer_plan.submit_enabled == 0 &&
                           framebuffer_plan.auth_attempt_allowed == 0,
                       "credential framebuffer plan submit must stay declarative");
  fails += expect_true(strings_equal(framebuffer_plan.framebuffer_ticket,
                                     "text-login-fallback-framebuffer-ticket") &&
                           strings_equal(framebuffer_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(framebuffer_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential framebuffer plan submit should explain fallback");

  fails += expect_true(build_loginwindow_credential_screen_framebuffer_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &framebuffer_plan) == 0,
                       "credential framebuffer plan unknown should build");
  fails += expect_true(framebuffer_plan.framebuffer_plan_safe == 1 &&
                           framebuffer_plan.framebuffer_text_login_fallback == 1 &&
                           framebuffer_plan.action_allowed == 0 &&
                           framebuffer_plan.action_blocked == 1,
                       "credential framebuffer plan unknown should force text login fallback");
  fails += expect_true(strings_equal(framebuffer_plan.framebuffer_ticket,
                                     "text-login-fallback-framebuffer-ticket") &&
                           strings_equal(framebuffer_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential framebuffer plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_framebuffer_plan_fails_closed_for_unsafe_or_missing_blit_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_blit_plan blit_plan;
  struct login_window_credential_screen_framebuffer_plan framebuffer_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_framebuffer_plan_build(
                           NULL, &framebuffer_plan) == 0,
                       "credential framebuffer plan missing blit plan should build fail-closed state");
  fails += expect_true(framebuffer_plan.blit_plan_available == 0 &&
                           framebuffer_plan.blit_plan_safe == 0 &&
                           framebuffer_plan.framebuffer_plan_safe == 0 &&
                           framebuffer_plan.route_selected == 0 &&
                           framebuffer_plan.route_blocked == 1,
                       "credential framebuffer plan missing blit plan should block framebuffer plan");
  fails += expect_true(framebuffer_plan.framebuffer_allowed == 0 &&
                           framebuffer_plan.framebuffer_submitted == 0 &&
                           framebuffer_plan.framebuffer_mapped == 0 &&
                           framebuffer_plan.framebuffer_write_allowed == 0 &&
                           framebuffer_plan.framebuffer_written == 0 &&
                           framebuffer_plan.blit_submitted == 0 &&
                           framebuffer_plan.output_submitted == 0 &&
                           framebuffer_plan.display_submitted == 0 &&
                           framebuffer_plan.page_flip_submitted == 0 &&
                           framebuffer_plan.framebuffer_text_login_fallback == 1 &&
                           framebuffer_plan.submit_enabled == 0 &&
                           framebuffer_plan.auth_attempt_allowed == 0,
                       "credential framebuffer plan missing blit plan must stay redacted");
  fails += expect_true(strings_equal(framebuffer_plan.framebuffer_ticket,
                                     "text-login-fallback-framebuffer-ticket") &&
                           strings_equal(framebuffer_plan.event_type,
                                         "credential-screen-framebuffer-plan-unavailable") &&
                           strings_equal(framebuffer_plan.blocked_reason,
                                         "blit-plan-unavailable"),
                       "credential framebuffer plan missing blit plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_blit_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &blit_plan) == 0,
                       "credential framebuffer plan unsafe blit source should build");
  fails += expect_true(login_window_credential_screen_framebuffer_plan_build(
                           &blit_plan, &framebuffer_plan) == 0,
                       "credential framebuffer plan unsafe blit plan should build blocked state");
  fails += expect_true(framebuffer_plan.blit_plan_available == 1 &&
                           framebuffer_plan.blit_plan_safe == 0 &&
                           framebuffer_plan.framebuffer_plan_safe == 0 &&
                           framebuffer_plan.route_selected == 0 &&
                           framebuffer_plan.route_blocked == 1,
                       "credential framebuffer plan unsafe blit plan should block framebuffer plan");
  fails += expect_true(framebuffer_plan.framebuffer_allowed == 0 &&
                           framebuffer_plan.framebuffer_submitted == 0 &&
                           framebuffer_plan.framebuffer_credential_focus == 0 &&
                           framebuffer_plan.framebuffer_text_login_fallback == 1 &&
                           framebuffer_plan.submit_enabled == 0 &&
                           framebuffer_plan.auth_attempt_allowed == 0,
                       "credential framebuffer plan unsafe blit plan must force text login fallback");
  fails += expect_true(strings_equal(framebuffer_plan.framebuffer_ticket,
                                     "text-login-fallback-framebuffer-ticket") &&
                           strings_equal(framebuffer_plan.event_type,
                                         "credential-screen-framebuffer-plan-unsafe") &&
                           strings_equal(framebuffer_plan.blocked_reason,
                                         "credential-framebuffer-plan-unsafe"),
                       "credential framebuffer plan unsafe blit plan should force text login");

  fails += expect_true(build_loginwindow_credential_screen_blit_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'y', 0, 0, 0,
                           1, &blit_plan) == 0,
                       "credential framebuffer plan submitted blit source should build");
  blit_plan.blit_submitted = 1;
  blit_plan.blit_source_buffer_mapped = 1;
  blit_plan.blit_destination_buffer_mapped = 1;
  blit_plan.blit_pixels_copied = 1;
  blit_plan.blit_dma_allowed = 1;
  blit_plan.page_flip_allowed = 1;
  fails += expect_true(login_window_credential_screen_framebuffer_plan_build(
                           &blit_plan, &framebuffer_plan) == 0,
                       "credential framebuffer plan submitted blit should fail closed");
  fails += expect_true(framebuffer_plan.framebuffer_plan_safe == 0 &&
                           framebuffer_plan.framebuffer_allowed == 0 &&
                           framebuffer_plan.framebuffer_submitted == 0 &&
                           framebuffer_plan.framebuffer_mapped == 0 &&
                           framebuffer_plan.framebuffer_write_allowed == 0 &&
                           framebuffer_plan.framebuffer_written == 0 &&
                           framebuffer_plan.framebuffer_flushed == 0 &&
                           framebuffer_plan.blit_submitted == 0 &&
                           framebuffer_plan.blit_source_buffer_mapped == 0 &&
                           framebuffer_plan.blit_destination_buffer_mapped == 0 &&
                           framebuffer_plan.blit_pixels_copied == 0 &&
                           framebuffer_plan.blit_dma_allowed == 0 &&
                           framebuffer_plan.page_flip_allowed == 0 &&
                           framebuffer_plan.page_flip_submitted == 0 &&
                           framebuffer_plan.submit_enabled == 0 &&
                           framebuffer_plan.auth_attempt_allowed == 0,
                       "credential framebuffer plan must not copy unsafe submitted state");
  return fails;
}


int build_loginwindow_credential_screen_flush_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_flush_plan *flush_plan) {
  struct login_window_credential_screen_framebuffer_plan framebuffer_plan;

  if (build_loginwindow_credential_screen_framebuffer_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &framebuffer_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_flush_plan_build(&framebuffer_plan,
                                                         flush_plan);
}

static int test_loginwindow_credential_screen_flush_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_flush_plan flush_plan;

  fails += expect_true(build_loginwindow_credential_screen_flush_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'p', 0, 0, 0,
                           1, &flush_plan) == 0,
                       "credential flush plan edit should build");
  fails += expect_true(flush_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_FLUSH_PLAN_VERSION,
                       "credential flush plan should expose stable version");
  fails += expect_true(flush_plan.framebuffer_plan_available == 1 &&
                           flush_plan.framebuffer_plan_safe == 1 &&
                           flush_plan.flush_plan_safe == 1,
                       "credential flush plan should require safe framebuffer plan");
  fails += expect_true(flush_plan.flush_allowed == 1 &&
                           flush_plan.flush_submitted == 0 &&
                           flush_plan.flush_ticket_selected == 1 &&
                           flush_plan.flush_target_selected == 1 &&
                           flush_plan.flush_cache_clean_required == 1 &&
                           flush_plan.flush_cache_clean_allowed == 0 &&
                           flush_plan.flush_cache_cleaned == 0 &&
                           flush_plan.flush_memory_barrier_allowed == 0 &&
                           flush_plan.flush_memory_barrier_submitted == 0,
                       "credential flush plan should remain declarative");
  fails += expect_true(flush_plan.framebuffer_submitted == 0 &&
                           flush_plan.framebuffer_mapped == 0 &&
                           flush_plan.framebuffer_write_allowed == 0 &&
                           flush_plan.framebuffer_written == 0 &&
                           flush_plan.framebuffer_flushed == 0 &&
                           flush_plan.framebuffer_cache_cleaned == 0 &&
                           flush_plan.blit_submitted == 0 &&
                           flush_plan.blit_pixels_copied == 0 &&
                           flush_plan.output_submitted == 0 &&
                           flush_plan.display_mode_committed == 0 &&
                           flush_plan.scanout_submitted == 0 &&
                           flush_plan.vsync_submitted == 0 &&
                           flush_plan.schedule_submitted == 0 &&
                           flush_plan.present_submitted == 0 &&
                           flush_plan.damage_submitted == 0 &&
                           flush_plan.page_flip_submitted == 0,
                       "credential flush plan must not execute upstream GUI work");
  fails += expect_true(flush_plan.schedule_incremental_allowed == 1 &&
                           flush_plan.full_schedule_required == 0 &&
                           flush_plan.schedule_cache_allowed == 1 &&
                           flush_plan.schedule_reuse_allowed == 1 &&
                           flush_plan.schedule_cache_hit == 0,
                       "credential flush plan should preserve scalable planning");
  fails += expect_true(flush_plan.flush_credential_panel == 1 &&
                           flush_plan.flush_credential_input == 1 &&
                           flush_plan.flush_credential_focus == 1,
                       "credential flush plan should mark credential widgets");
  fails += expect_true(flush_plan.submit_callback_bound == 0 &&
                           flush_plan.auth_callback_bound == 0 &&
                           flush_plan.submit_enabled == 0 &&
                           flush_plan.auth_attempt_allowed == 0 &&
                           flush_plan.raw_secret_exposed == 0 &&
                           flush_plan.masked_text_exposed == 0 &&
                           flush_plan.length_redacted == 1,
                       "credential flush plan must stay redacted and auth-disabled");
  fails += expect_true(strings_equal(flush_plan.flush_ticket,
                                     "credential-screen-flush-ticket") &&
                           strings_equal(flush_plan.framebuffer_ticket,
                                         "credential-screen-framebuffer-ticket") &&
                           strings_equal(flush_plan.flush_policy,
                                         "incremental-flush-declarative") &&
                           strings_equal(flush_plan.state,
                                         "flush-credential-ready"),
                       "credential flush plan should report flush ticket");
  return fails;
}

static int test_loginwindow_credential_screen_flush_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_flush_plan flush_plan;

  fails += expect_true(build_loginwindow_credential_screen_flush_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &flush_plan) == 0,
                       "credential flush plan recovery should build");
  fails += expect_true(flush_plan.flush_plan_safe == 1 &&
                           flush_plan.flush_allowed == 1 &&
                           flush_plan.flush_submitted == 0 &&
                           flush_plan.flush_text_recovery == 1 &&
                           flush_plan.flush_text_login == 1 &&
                           flush_plan.flush_credential_focus == 0,
                       "credential flush plan recovery should mark text recovery");
  fails += expect_true(flush_plan.flush_cache_clean_allowed == 0 &&
                           flush_plan.flush_cache_cleaned == 0 &&
                           flush_plan.flush_memory_barrier_allowed == 0 &&
                           flush_plan.flush_memory_barrier_submitted == 0 &&
                           flush_plan.framebuffer_flushed == 0 &&
                           flush_plan.framebuffer_cache_cleaned == 0 &&
                           flush_plan.framebuffer_mapped == 0 &&
                           flush_plan.framebuffer_written == 0 &&
                           flush_plan.blit_pixels_copied == 0 &&
                           flush_plan.output_submitted == 0 &&
                           flush_plan.display_mode_committed == 0 &&
                           flush_plan.page_flip_submitted == 0 &&
                           flush_plan.submit_enabled == 0 &&
                           flush_plan.auth_attempt_allowed == 0,
                       "credential flush plan recovery must not flush or submit output");
  fails += expect_true(strings_equal(flush_plan.flush_ticket,
                                     "text-recovery-flush-ticket") &&
                           strings_equal(flush_plan.compositor_target,
                                         "text-recovery-flush") &&
                           strings_equal(flush_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential flush plan recovery should report recovery ticket");

  fails += expect_true(build_loginwindow_credential_screen_flush_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &flush_plan) == 0,
                       "credential flush plan resume should build");
  fails += expect_true(flush_plan.flush_plan_safe == 1 &&
                           flush_plan.flush_text_login_resume == 1 &&
                           flush_plan.session_reset_required == 1 &&
                           flush_plan.login_screen_rerender_required == 1 &&
                           flush_plan.schedule_reuse_allowed == 0 &&
                           flush_plan.schedule_cache_allowed == 0 &&
                           flush_plan.full_schedule_required == 1 &&
                           flush_plan.schedule_incremental_allowed == 0,
                       "credential flush plan resume should require full planning");
  fails += expect_true(flush_plan.flush_submitted == 0 &&
                           flush_plan.flush_cache_cleaned == 0 &&
                           flush_plan.flush_memory_barrier_submitted == 0 &&
                           flush_plan.framebuffer_flushed == 0 &&
                           flush_plan.framebuffer_cache_cleaned == 0 &&
                           flush_plan.framebuffer_submitted == 0 &&
                           flush_plan.blit_submitted == 0 &&
                           flush_plan.output_submitted == 0 &&
                           flush_plan.display_submitted == 0 &&
                           flush_plan.scanout_submitted == 0 &&
                           flush_plan.submit_enabled == 0 &&
                           flush_plan.auth_attempt_allowed == 0,
                       "credential flush plan resume must keep GUI auth disabled");
  fails += expect_true(strings_equal(flush_plan.flush_ticket,
                                     "text-login-resume-flush-ticket") &&
                           strings_equal(flush_plan.flush_policy,
                                         "full-flush-declarative") &&
                           strings_equal(flush_plan.state,
                                         "flush-resume-ready"),
                       "credential flush plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_flush_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_flush_plan flush_plan;

  fails += expect_true(build_loginwindow_credential_screen_flush_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &flush_plan) == 0,
                       "credential flush plan submit should build");
  fails += expect_true(flush_plan.flush_plan_safe == 1 &&
                           flush_plan.submit_requested == 1 &&
                           flush_plan.flush_text_login_fallback == 1 &&
                           flush_plan.action_allowed == 0 &&
                           flush_plan.action_blocked == 1 &&
                           flush_plan.input_focus_allowed == 0,
                       "credential flush plan submit should force text login fallback");
  fails += expect_true(flush_plan.flush_allowed == 1 &&
                           flush_plan.flush_submitted == 0 &&
                           flush_plan.flush_cache_clean_allowed == 0 &&
                           flush_plan.flush_cache_cleaned == 0 &&
                           flush_plan.flush_memory_barrier_submitted == 0 &&
                           flush_plan.framebuffer_flushed == 0 &&
                           flush_plan.framebuffer_cache_cleaned == 0 &&
                           flush_plan.framebuffer_written == 0 &&
                           flush_plan.blit_pixels_copied == 0 &&
                           flush_plan.output_submitted == 0 &&
                           flush_plan.display_submitted == 0 &&
                           flush_plan.page_flip_submitted == 0 &&
                           flush_plan.submit_callback_bound == 0 &&
                           flush_plan.auth_callback_bound == 0 &&
                           flush_plan.submit_enabled == 0 &&
                           flush_plan.auth_attempt_allowed == 0,
                       "credential flush plan submit must stay declarative");
  fails += expect_true(strings_equal(flush_plan.flush_ticket,
                                     "text-login-fallback-flush-ticket") &&
                           strings_equal(flush_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(flush_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential flush plan submit should explain fallback");

  fails += expect_true(build_loginwindow_credential_screen_flush_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &flush_plan) == 0,
                       "credential flush plan unknown should build");
  fails += expect_true(flush_plan.flush_plan_safe == 1 &&
                           flush_plan.flush_text_login_fallback == 1 &&
                           flush_plan.action_allowed == 0 &&
                           flush_plan.action_blocked == 1,
                       "credential flush plan unknown should force text login fallback");
  fails += expect_true(strings_equal(flush_plan.flush_ticket,
                                     "text-login-fallback-flush-ticket") &&
                           strings_equal(flush_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential flush plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_flush_plan_fails_closed_for_unsafe_or_missing_framebuffer_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_framebuffer_plan framebuffer_plan;
  struct login_window_credential_screen_flush_plan flush_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_flush_plan_build(
                           NULL, &flush_plan) == 0,
                       "credential flush plan missing framebuffer plan should build fail-closed state");
  fails += expect_true(flush_plan.framebuffer_plan_available == 0 &&
                           flush_plan.framebuffer_plan_safe == 0 &&
                           flush_plan.flush_plan_safe == 0 &&
                           flush_plan.route_selected == 0 &&
                           flush_plan.route_blocked == 1,
                       "credential flush plan missing framebuffer plan should block flush plan");
  fails += expect_true(flush_plan.flush_allowed == 0 &&
                           flush_plan.flush_submitted == 0 &&
                           flush_plan.flush_cache_clean_allowed == 0 &&
                           flush_plan.flush_cache_cleaned == 0 &&
                           flush_plan.flush_memory_barrier_submitted == 0 &&
                           flush_plan.framebuffer_flushed == 0 &&
                           flush_plan.framebuffer_submitted == 0 &&
                           flush_plan.blit_submitted == 0 &&
                           flush_plan.output_submitted == 0 &&
                           flush_plan.display_submitted == 0 &&
                           flush_plan.page_flip_submitted == 0 &&
                           flush_plan.flush_text_login_fallback == 1 &&
                           flush_plan.submit_enabled == 0 &&
                           flush_plan.auth_attempt_allowed == 0,
                       "credential flush plan missing framebuffer plan must stay redacted");
  fails += expect_true(strings_equal(flush_plan.flush_ticket,
                                     "text-login-fallback-flush-ticket") &&
                           strings_equal(flush_plan.event_type,
                                         "credential-screen-flush-plan-unavailable") &&
                           strings_equal(flush_plan.blocked_reason,
                                         "framebuffer-plan-unavailable"),
                       "credential flush plan missing framebuffer plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_framebuffer_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &framebuffer_plan) == 0,
                       "credential flush plan unsafe framebuffer source should build");
  fails += expect_true(login_window_credential_screen_flush_plan_build(
                           &framebuffer_plan, &flush_plan) == 0,
                       "credential flush plan unsafe framebuffer plan should build blocked state");
  fails += expect_true(flush_plan.framebuffer_plan_available == 1 &&
                           flush_plan.framebuffer_plan_safe == 0 &&
                           flush_plan.flush_plan_safe == 0 &&
                           flush_plan.route_selected == 0 &&
                           flush_plan.route_blocked == 1,
                       "credential flush plan unsafe framebuffer plan should block flush plan");
  fails += expect_true(flush_plan.flush_allowed == 0 &&
                           flush_plan.flush_submitted == 0 &&
                           flush_plan.flush_credential_focus == 0 &&
                           flush_plan.flush_text_login_fallback == 1 &&
                           flush_plan.submit_enabled == 0 &&
                           flush_plan.auth_attempt_allowed == 0,
                       "credential flush plan unsafe framebuffer plan must force text login fallback");
  fails += expect_true(strings_equal(flush_plan.flush_ticket,
                                     "text-login-fallback-flush-ticket") &&
                           strings_equal(flush_plan.event_type,
                                         "credential-screen-flush-plan-unsafe") &&
                           strings_equal(flush_plan.blocked_reason,
                                         "credential-flush-plan-unsafe"),
                       "credential flush plan unsafe framebuffer plan should force text login");

  fails += expect_true(build_loginwindow_credential_screen_framebuffer_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'y', 0, 0, 0,
                           1, &framebuffer_plan) == 0,
                       "credential flush plan submitted framebuffer source should build");
  framebuffer_plan.framebuffer_submitted = 1;
  framebuffer_plan.framebuffer_mapped = 1;
  framebuffer_plan.framebuffer_write_allowed = 1;
  framebuffer_plan.framebuffer_written = 1;
  framebuffer_plan.framebuffer_flushed = 1;
  framebuffer_plan.framebuffer_cache_cleaned = 1;
  framebuffer_plan.page_flip_allowed = 1;
  fails += expect_true(login_window_credential_screen_flush_plan_build(
                           &framebuffer_plan, &flush_plan) == 0,
                       "credential flush plan submitted framebuffer should fail closed");
  fails += expect_true(flush_plan.flush_plan_safe == 0 &&
                           flush_plan.flush_allowed == 0 &&
                           flush_plan.flush_submitted == 0 &&
                           flush_plan.flush_cache_clean_allowed == 0 &&
                           flush_plan.flush_cache_cleaned == 0 &&
                           flush_plan.flush_memory_barrier_allowed == 0 &&
                           flush_plan.flush_memory_barrier_submitted == 0 &&
                           flush_plan.framebuffer_submitted == 0 &&
                           flush_plan.framebuffer_mapped == 0 &&
                           flush_plan.framebuffer_write_allowed == 0 &&
                           flush_plan.framebuffer_written == 0 &&
                           flush_plan.framebuffer_flushed == 0 &&
                           flush_plan.framebuffer_cache_cleaned == 0 &&
                           flush_plan.page_flip_allowed == 0 &&
                           flush_plan.page_flip_submitted == 0 &&
                           flush_plan.submit_enabled == 0 &&
                           flush_plan.auth_attempt_allowed == 0,
                       "credential flush plan must not copy unsafe submitted state");
  return fails;
}

int test_login_runtime_credential_framebuffer_flush_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_framebuffer_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_framebuffer_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_framebuffer_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_framebuffer_plan_fails_closed_for_unsafe_or_missing_blit_plan();
  fails += test_loginwindow_credential_screen_flush_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_flush_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_flush_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_flush_plan_fails_closed_for_unsafe_or_missing_framebuffer_plan();
  return fails;
}
