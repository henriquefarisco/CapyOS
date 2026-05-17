/*
 * tests/auth/test_login_runtime_credential_barrier_fence.c
 *
 * Credential screen barrier plan + fence plan coverage for the
 * `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-16 monolith
 * refactor (PR D.20 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_barrier_plan_build`: 4 tests
 *     covering the credential widgets barrier + the text-route
 *     barrier (recovery + resume) + the submit/unknown fallback
 *     barrier + the missing-or-unsafe flush plan fail-closed
 *     default.
 *   - `login_window_credential_screen_fence_plan_build`: 4 tests
 *     covering the credential widgets fence + the text-route fence
 *     (recovery + resume) + the submit/unknown fallback fence + the
 *     missing-or-unsafe barrier plan fail-closed default.
 *
 * Also exposes shared helpers
 * `build_loginwindow_credential_screen_barrier_plan_for_action` and
 * `build_loginwindow_credential_screen_fence_plan_for_action`, used
 * by later companion files that chain on top of the barrier/fence
 * stages (timeline, sync, ...).
 *
 * The companion entry `test_login_runtime_credential_barrier_fence_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_barrier_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_barrier_plan *barrier_plan) {
  struct login_window_credential_screen_flush_plan flush_plan;

  if (build_loginwindow_credential_screen_flush_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &flush_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_barrier_plan_build(&flush_plan,
                                                           barrier_plan);
}

static int test_loginwindow_credential_screen_barrier_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_barrier_plan barrier_plan;

  fails += expect_true(build_loginwindow_credential_screen_barrier_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'b', 0, 0, 0,
                           1, &barrier_plan) == 0,
                       "credential barrier plan edit should build");
  fails += expect_true(barrier_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_BARRIER_PLAN_VERSION,
                       "credential barrier plan should expose stable version");
  fails += expect_true(barrier_plan.flush_plan_available == 1 &&
                           barrier_plan.flush_plan_safe == 1 &&
                           barrier_plan.barrier_plan_safe == 1,
                       "credential barrier plan should require safe flush plan");
  fails += expect_true(barrier_plan.barrier_required == 1 &&
                           barrier_plan.barrier_allowed == 1 &&
                           barrier_plan.barrier_submitted == 0 &&
                           barrier_plan.barrier_ticket_selected == 1 &&
                           barrier_plan.barrier_target_selected == 1 &&
                           barrier_plan.barrier_memory_visibility_required == 1 &&
                           barrier_plan.barrier_memory_visibility_established == 0 &&
                           barrier_plan.barrier_cache_visibility_required == 1 &&
                           barrier_plan.barrier_cache_visibility_established == 0 &&
                           barrier_plan.barrier_cpu_gpu_sync_allowed == 0 &&
                           barrier_plan.barrier_cpu_gpu_sync_submitted == 0,
                       "credential barrier plan should remain declarative");
  fails += expect_true(barrier_plan.flush_submitted == 0 &&
                           barrier_plan.flush_cache_clean_allowed == 0 &&
                           barrier_plan.flush_cache_cleaned == 0 &&
                           barrier_plan.flush_memory_barrier_allowed == 0 &&
                           barrier_plan.flush_memory_barrier_submitted == 0 &&
                           barrier_plan.framebuffer_submitted == 0 &&
                           barrier_plan.framebuffer_mapped == 0 &&
                           barrier_plan.framebuffer_write_allowed == 0 &&
                           barrier_plan.framebuffer_written == 0 &&
                           barrier_plan.framebuffer_flushed == 0 &&
                           barrier_plan.framebuffer_cache_cleaned == 0 &&
                           barrier_plan.blit_submitted == 0 &&
                           barrier_plan.blit_pixels_copied == 0 &&
                           barrier_plan.output_submitted == 0 &&
                           barrier_plan.display_mode_committed == 0 &&
                           barrier_plan.scanout_submitted == 0 &&
                           barrier_plan.vsync_submitted == 0 &&
                           barrier_plan.schedule_submitted == 0 &&
                           barrier_plan.present_submitted == 0 &&
                           barrier_plan.damage_submitted == 0 &&
                           barrier_plan.page_flip_submitted == 0,
                       "credential barrier plan must not execute GUI work");
  fails += expect_true(barrier_plan.barrier_credential_panel == 1 &&
                           barrier_plan.barrier_credential_input == 1 &&
                           barrier_plan.barrier_credential_focus == 1,
                       "credential barrier plan should mark credential widgets");
  fails += expect_true(barrier_plan.submit_callback_bound == 0 &&
                           barrier_plan.auth_callback_bound == 0 &&
                           barrier_plan.submit_enabled == 0 &&
                           barrier_plan.auth_attempt_allowed == 0 &&
                           barrier_plan.raw_secret_exposed == 0 &&
                           barrier_plan.masked_text_exposed == 0 &&
                           barrier_plan.length_redacted == 1,
                       "credential barrier plan must stay redacted and auth-disabled");
  fails += expect_true(strings_equal(barrier_plan.barrier_ticket,
                                     "credential-screen-barrier-ticket") &&
                           strings_equal(barrier_plan.flush_ticket,
                                         "credential-screen-flush-ticket") &&
                           strings_equal(barrier_plan.barrier_policy,
                                         "incremental-barrier-declarative") &&
                           strings_equal(barrier_plan.state,
                                         "barrier-credential-ready"),
                       "credential barrier plan should report barrier ticket");
  return fails;
}

static int test_loginwindow_credential_screen_barrier_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_barrier_plan barrier_plan;

  fails += expect_true(build_loginwindow_credential_screen_barrier_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &barrier_plan) == 0,
                       "credential barrier plan recovery should build");
  fails += expect_true(barrier_plan.barrier_plan_safe == 1 &&
                           barrier_plan.barrier_allowed == 1 &&
                           barrier_plan.barrier_submitted == 0 &&
                           barrier_plan.barrier_text_recovery == 1 &&
                           barrier_plan.barrier_text_login == 1 &&
                           barrier_plan.barrier_credential_focus == 0,
                       "credential barrier plan recovery should mark text recovery");
  fails += expect_true(barrier_plan.barrier_memory_visibility_established == 0 &&
                           barrier_plan.barrier_cache_visibility_established == 0 &&
                           barrier_plan.barrier_cpu_gpu_sync_submitted == 0 &&
                           barrier_plan.flush_memory_barrier_submitted == 0 &&
                           barrier_plan.flush_cache_cleaned == 0 &&
                           barrier_plan.framebuffer_flushed == 0 &&
                           barrier_plan.framebuffer_written == 0 &&
                           barrier_plan.blit_pixels_copied == 0 &&
                           barrier_plan.output_submitted == 0 &&
                           barrier_plan.display_mode_committed == 0 &&
                           barrier_plan.page_flip_submitted == 0 &&
                           barrier_plan.submit_enabled == 0 &&
                           barrier_plan.auth_attempt_allowed == 0,
                       "credential barrier plan recovery must not submit barrier or output");
  fails += expect_true(strings_equal(barrier_plan.barrier_ticket,
                                     "text-recovery-barrier-ticket") &&
                           strings_equal(barrier_plan.compositor_target,
                                         "text-recovery-barrier") &&
                           strings_equal(barrier_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential barrier plan recovery should report recovery ticket");

  fails += expect_true(build_loginwindow_credential_screen_barrier_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &barrier_plan) == 0,
                       "credential barrier plan resume should build");
  fails += expect_true(barrier_plan.barrier_plan_safe == 1 &&
                           barrier_plan.barrier_text_login_resume == 1 &&
                           barrier_plan.session_reset_required == 1 &&
                           barrier_plan.login_screen_rerender_required == 1 &&
                           barrier_plan.barrier_submitted == 0 &&
                           barrier_plan.barrier_cpu_gpu_sync_submitted == 0 &&
                           barrier_plan.submit_enabled == 0 &&
                           barrier_plan.auth_attempt_allowed == 0,
                       "credential barrier plan resume should keep GUI auth disabled");
  fails += expect_true(strings_equal(barrier_plan.barrier_ticket,
                                     "text-login-resume-barrier-ticket") &&
                           strings_equal(barrier_plan.barrier_policy,
                                         "full-barrier-declarative") &&
                           strings_equal(barrier_plan.state,
                                         "barrier-resume-ready"),
                       "credential barrier plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_barrier_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_barrier_plan barrier_plan;

  fails += expect_true(build_loginwindow_credential_screen_barrier_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &barrier_plan) == 0,
                       "credential barrier plan submit should build");
  fails += expect_true(barrier_plan.barrier_plan_safe == 1 &&
                           barrier_plan.submit_requested == 1 &&
                           barrier_plan.barrier_text_login_fallback == 1 &&
                           barrier_plan.action_allowed == 0 &&
                           barrier_plan.action_blocked == 1 &&
                           barrier_plan.input_focus_allowed == 0,
                       "credential barrier plan submit should force text login fallback");
  fails += expect_true(barrier_plan.barrier_submitted == 0 &&
                           barrier_plan.barrier_memory_visibility_established == 0 &&
                           barrier_plan.barrier_cache_visibility_established == 0 &&
                           barrier_plan.barrier_cpu_gpu_sync_allowed == 0 &&
                           barrier_plan.barrier_cpu_gpu_sync_submitted == 0 &&
                           barrier_plan.flush_submitted == 0 &&
                           barrier_plan.flush_cache_cleaned == 0 &&
                           barrier_plan.flush_memory_barrier_submitted == 0 &&
                           barrier_plan.framebuffer_written == 0 &&
                           barrier_plan.blit_pixels_copied == 0 &&
                           barrier_plan.output_submitted == 0 &&
                           barrier_plan.display_submitted == 0 &&
                           barrier_plan.page_flip_submitted == 0 &&
                           barrier_plan.submit_callback_bound == 0 &&
                           barrier_plan.auth_callback_bound == 0 &&
                           barrier_plan.submit_enabled == 0 &&
                           barrier_plan.auth_attempt_allowed == 0,
                       "credential barrier plan submit must stay declarative");
  fails += expect_true(strings_equal(barrier_plan.barrier_ticket,
                                     "text-login-fallback-barrier-ticket") &&
                           strings_equal(barrier_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(barrier_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential barrier plan submit should explain fallback");

  fails += expect_true(build_loginwindow_credential_screen_barrier_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &barrier_plan) == 0,
                       "credential barrier plan unknown should build");
  fails += expect_true(barrier_plan.barrier_plan_safe == 1 &&
                           barrier_plan.barrier_text_login_fallback == 1 &&
                           barrier_plan.action_allowed == 0 &&
                           barrier_plan.action_blocked == 1,
                       "credential barrier plan unknown should force text login fallback");
  fails += expect_true(strings_equal(barrier_plan.barrier_ticket,
                                     "text-login-fallback-barrier-ticket") &&
                           strings_equal(barrier_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential barrier plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_barrier_plan_fails_closed_for_unsafe_or_missing_flush_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_flush_plan flush_plan;
  struct login_window_credential_screen_barrier_plan barrier_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_barrier_plan_build(
                           NULL, &barrier_plan) == 0,
                       "credential barrier plan missing flush plan should build fail-closed state");
  fails += expect_true(barrier_plan.flush_plan_available == 0 &&
                           barrier_plan.flush_plan_safe == 0 &&
                           barrier_plan.barrier_plan_safe == 0 &&
                           barrier_plan.route_selected == 0 &&
                           barrier_plan.route_blocked == 1,
                       "credential barrier plan missing flush plan should block barrier plan");
  fails += expect_true(barrier_plan.barrier_allowed == 0 &&
                           barrier_plan.barrier_submitted == 0 &&
                           barrier_plan.barrier_cpu_gpu_sync_allowed == 0 &&
                           barrier_plan.barrier_cpu_gpu_sync_submitted == 0 &&
                           barrier_plan.flush_submitted == 0 &&
                           barrier_plan.flush_cache_cleaned == 0 &&
                           barrier_plan.flush_memory_barrier_submitted == 0 &&
                           barrier_plan.framebuffer_flushed == 0 &&
                           barrier_plan.output_submitted == 0 &&
                           barrier_plan.display_submitted == 0 &&
                           barrier_plan.page_flip_submitted == 0 &&
                           barrier_plan.barrier_text_login_fallback == 1 &&
                           barrier_plan.submit_enabled == 0 &&
                           barrier_plan.auth_attempt_allowed == 0,
                       "credential barrier plan missing flush plan must stay redacted");
  fails += expect_true(strings_equal(barrier_plan.barrier_ticket,
                                     "text-login-fallback-barrier-ticket") &&
                           strings_equal(barrier_plan.event_type,
                                         "credential-screen-barrier-plan-unavailable") &&
                           strings_equal(barrier_plan.blocked_reason,
                                         "flush-plan-unavailable"),
                       "credential barrier plan missing flush plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_flush_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &flush_plan) == 0,
                       "credential barrier plan unsafe flush source should build");
  fails += expect_true(login_window_credential_screen_barrier_plan_build(
                           &flush_plan, &barrier_plan) == 0,
                       "credential barrier plan unsafe flush plan should build blocked state");
  fails += expect_true(barrier_plan.flush_plan_available == 1 &&
                           barrier_plan.flush_plan_safe == 0 &&
                           barrier_plan.barrier_plan_safe == 0 &&
                           barrier_plan.route_selected == 0 &&
                           barrier_plan.route_blocked == 1,
                       "credential barrier plan unsafe flush plan should block barrier plan");
  fails += expect_true(barrier_plan.barrier_allowed == 0 &&
                           barrier_plan.barrier_submitted == 0 &&
                           barrier_plan.barrier_text_login_fallback == 1 &&
                           barrier_plan.submit_enabled == 0 &&
                           barrier_plan.auth_attempt_allowed == 0,
                       "credential barrier plan unsafe flush plan must force text login fallback");
  fails += expect_true(strings_equal(barrier_plan.barrier_ticket,
                                     "text-login-fallback-barrier-ticket") &&
                           strings_equal(barrier_plan.event_type,
                                         "credential-screen-barrier-plan-unsafe") &&
                           strings_equal(barrier_plan.blocked_reason,
                                         "credential-barrier-plan-unsafe"),
                       "credential barrier plan unsafe flush plan should force text login");

  fails += expect_true(build_loginwindow_credential_screen_flush_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'y', 0, 0, 0,
                           1, &flush_plan) == 0,
                       "credential barrier plan submitted flush source should build");
  flush_plan.flush_submitted = 1;
  flush_plan.flush_cache_clean_allowed = 1;
  flush_plan.flush_cache_cleaned = 1;
  flush_plan.flush_memory_barrier_allowed = 1;
  flush_plan.flush_memory_barrier_submitted = 1;
  flush_plan.framebuffer_flushed = 1;
  flush_plan.page_flip_allowed = 1;
  fails += expect_true(login_window_credential_screen_barrier_plan_build(
                           &flush_plan, &barrier_plan) == 0,
                       "credential barrier plan submitted flush should fail closed");
  fails += expect_true(barrier_plan.barrier_plan_safe == 0 &&
                           barrier_plan.barrier_allowed == 0 &&
                           barrier_plan.barrier_submitted == 0 &&
                           barrier_plan.barrier_memory_visibility_established == 0 &&
                           barrier_plan.barrier_cache_visibility_established == 0 &&
                           barrier_plan.barrier_cpu_gpu_sync_allowed == 0 &&
                           barrier_plan.barrier_cpu_gpu_sync_submitted == 0 &&
                           barrier_plan.flush_submitted == 0 &&
                           barrier_plan.flush_cache_clean_allowed == 0 &&
                           barrier_plan.flush_cache_cleaned == 0 &&
                           barrier_plan.flush_memory_barrier_allowed == 0 &&
                           barrier_plan.flush_memory_barrier_submitted == 0 &&
                           barrier_plan.framebuffer_flushed == 0 &&
                           barrier_plan.page_flip_allowed == 0 &&
                           barrier_plan.page_flip_submitted == 0 &&
                           barrier_plan.submit_enabled == 0 &&
                           barrier_plan.auth_attempt_allowed == 0,
                       "credential barrier plan must not copy unsafe submitted state");
  return fails;
}


int build_loginwindow_credential_screen_fence_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_fence_plan *fence_plan) {
  struct login_window_credential_screen_barrier_plan barrier_plan;

  if (build_loginwindow_credential_screen_barrier_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &barrier_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_fence_plan_build(&barrier_plan,
                                                         fence_plan);
}

static int test_loginwindow_credential_screen_fence_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_fence_plan fence_plan;

  fails += expect_true(build_loginwindow_credential_screen_fence_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'f', 0, 0, 0,
                           1, &fence_plan) == 0,
                       "credential fence plan edit should build");
  fails += expect_true(fence_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_FENCE_PLAN_VERSION,
                       "credential fence plan should expose stable version");
  fails += expect_true(fence_plan.barrier_plan_available == 1 &&
                           fence_plan.barrier_plan_safe == 1 &&
                           fence_plan.fence_plan_safe == 1,
                       "credential fence plan should require safe barrier plan");
  fails += expect_true(fence_plan.fence_required == 1 &&
                           fence_plan.fence_allowed == 1 &&
                           fence_plan.fence_submitted == 0 &&
                           fence_plan.fence_ticket_selected == 1 &&
                           fence_plan.fence_target_selected == 1 &&
                           fence_plan.fence_wait_required == 1 &&
                           fence_plan.fence_wait_allowed == 0 &&
                           fence_plan.fence_wait_submitted == 0 &&
                           fence_plan.fence_signal_allowed == 0 &&
                           fence_plan.fence_signal_submitted == 0 &&
                           fence_plan.fence_fd_export_allowed == 0 &&
                           fence_plan.fence_fd_exported == 0 &&
                           fence_plan.fence_cpu_gpu_sync_allowed == 0 &&
                           fence_plan.fence_cpu_gpu_sync_submitted == 0,
                       "credential fence plan should remain declarative");
  fails += expect_true(fence_plan.barrier_submitted == 0 &&
                           fence_plan.barrier_memory_visibility_established == 0 &&
                           fence_plan.barrier_cache_visibility_established == 0 &&
                           fence_plan.barrier_cpu_gpu_sync_submitted == 0 &&
                           fence_plan.flush_submitted == 0 &&
                           fence_plan.flush_cache_cleaned == 0 &&
                           fence_plan.flush_memory_barrier_submitted == 0 &&
                           fence_plan.framebuffer_submitted == 0 &&
                           fence_plan.framebuffer_mapped == 0 &&
                           fence_plan.framebuffer_write_allowed == 0 &&
                           fence_plan.framebuffer_written == 0 &&
                           fence_plan.framebuffer_flushed == 0 &&
                           fence_plan.framebuffer_cache_cleaned == 0 &&
                           fence_plan.blit_submitted == 0 &&
                           fence_plan.blit_pixels_copied == 0 &&
                           fence_plan.output_submitted == 0 &&
                           fence_plan.display_mode_committed == 0 &&
                           fence_plan.scanout_submitted == 0 &&
                           fence_plan.vsync_submitted == 0 &&
                           fence_plan.schedule_submitted == 0 &&
                           fence_plan.present_submitted == 0 &&
                           fence_plan.damage_submitted == 0 &&
                           fence_plan.page_flip_submitted == 0,
                       "credential fence plan must not execute GUI work");
  fails += expect_true(fence_plan.fence_credential_panel == 1 &&
                           fence_plan.fence_credential_input == 1 &&
                           fence_plan.fence_credential_focus == 1,
                       "credential fence plan should mark credential widgets");
  fails += expect_true(fence_plan.submit_callback_bound == 0 &&
                           fence_plan.auth_callback_bound == 0 &&
                           fence_plan.submit_enabled == 0 &&
                           fence_plan.auth_attempt_allowed == 0 &&
                           fence_plan.raw_secret_exposed == 0 &&
                           fence_plan.masked_text_exposed == 0 &&
                           fence_plan.length_redacted == 1,
                       "credential fence plan must stay redacted and auth-disabled");
  fails += expect_true(strings_equal(fence_plan.fence_ticket,
                                     "credential-screen-fence-ticket") &&
                           strings_equal(fence_plan.barrier_ticket,
                                         "credential-screen-barrier-ticket") &&
                           strings_equal(fence_plan.fence_policy,
                                         "declarative-fence-no-wait") &&
                           strings_equal(fence_plan.state,
                                         "fence-credential-ready"),
                       "credential fence plan should report fence ticket");
  return fails;
}

static int test_loginwindow_credential_screen_fence_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_fence_plan fence_plan;

  fails += expect_true(build_loginwindow_credential_screen_fence_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &fence_plan) == 0,
                       "credential fence plan recovery should build");
  fails += expect_true(fence_plan.fence_plan_safe == 1 &&
                           fence_plan.fence_allowed == 1 &&
                           fence_plan.fence_submitted == 0 &&
                           fence_plan.fence_text_recovery == 1 &&
                           fence_plan.fence_text_login == 1 &&
                           fence_plan.fence_credential_focus == 0,
                       "credential fence plan recovery should mark text recovery");
  fails += expect_true(fence_plan.fence_wait_allowed == 0 &&
                           fence_plan.fence_wait_submitted == 0 &&
                           fence_plan.fence_signal_allowed == 0 &&
                           fence_plan.fence_signal_submitted == 0 &&
                           fence_plan.fence_fd_exported == 0 &&
                           fence_plan.fence_cpu_gpu_sync_submitted == 0 &&
                           fence_plan.barrier_submitted == 0 &&
                           fence_plan.barrier_memory_visibility_established == 0 &&
                           fence_plan.flush_memory_barrier_submitted == 0 &&
                           fence_plan.framebuffer_flushed == 0 &&
                           fence_plan.framebuffer_written == 0 &&
                           fence_plan.blit_pixels_copied == 0 &&
                           fence_plan.output_submitted == 0 &&
                           fence_plan.display_mode_committed == 0 &&
                           fence_plan.page_flip_submitted == 0 &&
                           fence_plan.submit_enabled == 0 &&
                           fence_plan.auth_attempt_allowed == 0,
                       "credential fence plan recovery must not arm or wait fence");
  fails += expect_true(strings_equal(fence_plan.fence_ticket,
                                     "text-recovery-fence-ticket") &&
                           strings_equal(fence_plan.compositor_target,
                                         "text-recovery-fence") &&
                           strings_equal(fence_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential fence plan recovery should report recovery ticket");

  fails += expect_true(build_loginwindow_credential_screen_fence_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &fence_plan) == 0,
                       "credential fence plan resume should build");
  fails += expect_true(fence_plan.fence_plan_safe == 1 &&
                           fence_plan.fence_text_login_resume == 1 &&
                           fence_plan.session_reset_required == 1 &&
                           fence_plan.login_screen_rerender_required == 1 &&
                           fence_plan.fence_submitted == 0 &&
                           fence_plan.fence_wait_submitted == 0 &&
                           fence_plan.fence_signal_submitted == 0 &&
                           fence_plan.fence_cpu_gpu_sync_submitted == 0 &&
                           fence_plan.submit_enabled == 0 &&
                           fence_plan.auth_attempt_allowed == 0,
                       "credential fence plan resume should keep GUI auth disabled");
  fails += expect_true(strings_equal(fence_plan.fence_ticket,
                                     "text-login-resume-fence-ticket") &&
                           strings_equal(fence_plan.fence_policy,
                                         "full-fence-declarative") &&
                           strings_equal(fence_plan.state,
                                         "fence-resume-ready"),
                       "credential fence plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_fence_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_fence_plan fence_plan;

  fails += expect_true(build_loginwindow_credential_screen_fence_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &fence_plan) == 0,
                       "credential fence plan submit should build");
  fails += expect_true(fence_plan.fence_plan_safe == 1 &&
                           fence_plan.submit_requested == 1 &&
                           fence_plan.fence_text_login_fallback == 1 &&
                           fence_plan.action_allowed == 0 &&
                           fence_plan.action_blocked == 1 &&
                           fence_plan.input_focus_allowed == 0,
                       "credential fence plan submit should force text login fallback");
  fails += expect_true(fence_plan.fence_submitted == 0 &&
                           fence_plan.fence_wait_allowed == 0 &&
                           fence_plan.fence_wait_submitted == 0 &&
                           fence_plan.fence_signal_allowed == 0 &&
                           fence_plan.fence_signal_submitted == 0 &&
                           fence_plan.fence_fd_export_allowed == 0 &&
                           fence_plan.fence_fd_exported == 0 &&
                           fence_plan.fence_cpu_gpu_sync_allowed == 0 &&
                           fence_plan.fence_cpu_gpu_sync_submitted == 0 &&
                           fence_plan.barrier_submitted == 0 &&
                           fence_plan.flush_submitted == 0 &&
                           fence_plan.framebuffer_written == 0 &&
                           fence_plan.blit_pixels_copied == 0 &&
                           fence_plan.output_submitted == 0 &&
                           fence_plan.display_submitted == 0 &&
                           fence_plan.page_flip_submitted == 0 &&
                           fence_plan.submit_callback_bound == 0 &&
                           fence_plan.auth_callback_bound == 0 &&
                           fence_plan.submit_enabled == 0 &&
                           fence_plan.auth_attempt_allowed == 0,
                       "credential fence plan submit must stay declarative");
  fails += expect_true(strings_equal(fence_plan.fence_ticket,
                                     "text-login-fallback-fence-ticket") &&
                           strings_equal(fence_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(fence_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential fence plan submit should explain fallback");

  fails += expect_true(build_loginwindow_credential_screen_fence_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &fence_plan) == 0,
                       "credential fence plan unknown should build");
  fails += expect_true(fence_plan.fence_plan_safe == 1 &&
                           fence_plan.fence_text_login_fallback == 1 &&
                           fence_plan.action_allowed == 0 &&
                           fence_plan.action_blocked == 1,
                       "credential fence plan unknown should force text login fallback");
  fails += expect_true(strings_equal(fence_plan.fence_ticket,
                                     "text-login-fallback-fence-ticket") &&
                           strings_equal(fence_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential fence plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_fence_plan_fails_closed_for_unsafe_or_missing_barrier_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_barrier_plan barrier_plan;
  struct login_window_credential_screen_fence_plan fence_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_fence_plan_build(
                           NULL, &fence_plan) == 0,
                       "credential fence plan missing barrier plan should build fail-closed state");
  fails += expect_true(fence_plan.barrier_plan_available == 0 &&
                           fence_plan.barrier_plan_safe == 0 &&
                           fence_plan.fence_plan_safe == 0 &&
                           fence_plan.route_selected == 0 &&
                           fence_plan.route_blocked == 1,
                       "credential fence plan missing barrier plan should block fence plan");
  fails += expect_true(fence_plan.fence_allowed == 0 &&
                           fence_plan.fence_submitted == 0 &&
                           fence_plan.fence_wait_allowed == 0 &&
                           fence_plan.fence_wait_submitted == 0 &&
                           fence_plan.fence_signal_allowed == 0 &&
                           fence_plan.fence_signal_submitted == 0 &&
                           fence_plan.fence_fd_exported == 0 &&
                           fence_plan.fence_text_login_fallback == 1 &&
                           fence_plan.submit_enabled == 0 &&
                           fence_plan.auth_attempt_allowed == 0,
                       "credential fence plan missing barrier plan must stay redacted");
  fails += expect_true(strings_equal(fence_plan.fence_ticket,
                                     "text-login-fallback-fence-ticket") &&
                           strings_equal(fence_plan.event_type,
                                         "credential-screen-fence-plan-unavailable") &&
                           strings_equal(fence_plan.blocked_reason,
                                         "barrier-plan-unavailable"),
                       "credential fence plan missing barrier plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_barrier_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0,
                           0, &barrier_plan) == 0,
                       "credential fence plan unsafe barrier source should build");
  fails += expect_true(login_window_credential_screen_fence_plan_build(
                           &barrier_plan, &fence_plan) == 0,
                       "credential fence plan unsafe barrier plan should build blocked state");
  fails += expect_true(fence_plan.barrier_plan_available == 1 &&
                           fence_plan.barrier_plan_safe == 0 &&
                           fence_plan.fence_plan_safe == 0 &&
                           fence_plan.route_selected == 0 &&
                           fence_plan.route_blocked == 1,
                       "credential fence plan unsafe barrier plan should block fence plan");
  fails += expect_true(fence_plan.fence_allowed == 0 &&
                           fence_plan.fence_submitted == 0 &&
                           fence_plan.fence_text_login_fallback == 1 &&
                           fence_plan.submit_enabled == 0 &&
                           fence_plan.auth_attempt_allowed == 0,
                       "credential fence plan unsafe barrier plan must force text login fallback");
  fails += expect_true(strings_equal(fence_plan.fence_ticket,
                                     "text-login-fallback-fence-ticket") &&
                           strings_equal(fence_plan.event_type,
                                         "credential-screen-fence-plan-unsafe") &&
                           strings_equal(fence_plan.blocked_reason,
                                         "credential-fence-plan-unsafe"),
                       "credential fence plan unsafe barrier plan should force text login");

  fails += expect_true(build_loginwindow_credential_screen_barrier_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'y', 0, 0, 0,
                           1, &barrier_plan) == 0,
                       "credential fence plan submitted barrier source should build");
  barrier_plan.barrier_submitted = 1;
  barrier_plan.barrier_memory_visibility_established = 1;
  barrier_plan.barrier_cache_visibility_established = 1;
  barrier_plan.barrier_cpu_gpu_sync_allowed = 1;
  barrier_plan.barrier_cpu_gpu_sync_submitted = 1;
  barrier_plan.page_flip_allowed = 1;
  fails += expect_true(login_window_credential_screen_fence_plan_build(
                           &barrier_plan, &fence_plan) == 0,
                       "credential fence plan submitted barrier should fail closed");
  fails += expect_true(fence_plan.fence_plan_safe == 0 &&
                           fence_plan.fence_allowed == 0 &&
                           fence_plan.fence_submitted == 0 &&
                           fence_plan.fence_wait_allowed == 0 &&
                           fence_plan.fence_wait_submitted == 0 &&
                           fence_plan.fence_signal_allowed == 0 &&
                           fence_plan.fence_signal_submitted == 0 &&
                           fence_plan.fence_fd_export_allowed == 0 &&
                           fence_plan.fence_fd_exported == 0 &&
                           fence_plan.fence_cpu_gpu_sync_allowed == 0 &&
                           fence_plan.fence_cpu_gpu_sync_submitted == 0 &&
                           fence_plan.barrier_submitted == 0 &&
                           fence_plan.barrier_memory_visibility_established == 0 &&
                           fence_plan.barrier_cache_visibility_established == 0 &&
                           fence_plan.barrier_cpu_gpu_sync_allowed == 0 &&
                           fence_plan.barrier_cpu_gpu_sync_submitted == 0 &&
                           fence_plan.page_flip_allowed == 0 &&
                           fence_plan.page_flip_submitted == 0 &&
                           fence_plan.submit_enabled == 0 &&
                           fence_plan.auth_attempt_allowed == 0,
                       "credential fence plan must not copy unsafe submitted state");
  return fails;
}

int test_login_runtime_credential_barrier_fence_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_barrier_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_barrier_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_barrier_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_barrier_plan_fails_closed_for_unsafe_or_missing_flush_plan();
  fails += test_loginwindow_credential_screen_fence_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_fence_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_fence_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_fence_plan_fails_closed_for_unsafe_or_missing_barrier_plan();
  return fails;
}
