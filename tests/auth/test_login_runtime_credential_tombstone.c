/*
 * tests/auth/test_login_runtime_credential_tombstone.c
 *
 * Credential screen tombstone plan coverage for the
 * `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-16 monolith
 * refactor (PR D.30 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_tombstone_plan_build`: 4 tests
 *     covering the credential widgets tombstone + the text-route
 *     tombstone (recovery + resume) + the submit/unknown fallback
 *     tombstone + the missing-or-unsafe purge plan fail-closed
 *     default.
 *
 * Also exposes shared helper
 * `build_loginwindow_credential_screen_tombstone_plan_for_action`,
 * used by later companion files that chain on top of the tombstone
 * stage (compaction, ...).
 *
 * Split independently from `purge` (PR D.29) because the combined
 * block exceeded the 900-line layout limit. This is the largest
 * single-plan companion in the chain, anchored by the comprehensive
 * fail-closed test that exercises the full pipeline state mask.
 *
 * The companion entry `test_login_runtime_credential_tombstone_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_tombstone_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_tombstone_plan *tombstone_plan) {
  struct login_window_credential_screen_purge_plan purge_plan;

  if (build_loginwindow_credential_screen_purge_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &purge_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_tombstone_plan_build(&purge_plan,
                                                            tombstone_plan);
}

static int test_loginwindow_credential_screen_tombstone_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_tombstone_plan tombstone_plan;

  fails += expect_true(build_loginwindow_credential_screen_tombstone_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'a', 0, 0, 0,
                           1, &tombstone_plan) == 0,
                       "credential tombstone plan edit should build");
  fails += expect_true(tombstone_plan.version == LOGIN_WINDOW_CREDENTIAL_SCREEN_TOMBSTONE_PLAN_VERSION,
                       "credential tombstone plan should expose stable version");
  fails += expect_true(tombstone_plan.purge_plan_available == 1 &&
                           tombstone_plan.purge_plan_safe == 1 &&
                           tombstone_plan.tombstone_plan_safe == 1,
                       "credential tombstone plan should require safe purge plan");
  fails += expect_true(tombstone_plan.tombstone_required == 1 &&
                           tombstone_plan.tombstone_allowed == 1 &&
                           tombstone_plan.tombstone_submitted == 0 &&
                           tombstone_plan.tombstone_ticket_selected == 1 &&
                           tombstone_plan.tombstone_target_selected == 1 &&
                           tombstone_plan.tombstone_persist_allowed == 0 &&
                           tombstone_plan.tombstone_persisted == 0 &&
                           tombstone_plan.tombstone_cpu_gpu_sync_allowed == 0 &&
                           tombstone_plan.tombstone_cpu_gpu_sync_submitted == 0,
                       "credential tombstone plan should remain declarative");
  fails += expect_true(tombstone_plan.purge_submitted == 0 &&
                           tombstone_plan.purge_persisted == 0 &&
                           tombstone_plan.purge_deleted == 0 &&
                           tombstone_plan.expiry_submitted == 0 &&
                           tombstone_plan.expiry_persisted == 0 &&
                           tombstone_plan.retention_submitted == 0 &&
                           tombstone_plan.retention_persisted == 0 &&
                           tombstone_plan.archive_submitted == 0 &&
                           tombstone_plan.archive_persisted == 0 &&
                           tombstone_plan.journal_submitted == 0 &&
                           tombstone_plan.journal_persisted == 0 &&
                           tombstone_plan.ledger_submitted == 0 &&
                           tombstone_plan.ledger_persisted == 0 &&
                           tombstone_plan.receipt_submitted == 0 &&
                           tombstone_plan.receipt_persisted == 0 &&
                           tombstone_plan.record_submitted == 0 &&
                           tombstone_plan.record_persisted == 0 &&
                           tombstone_plan.audit_submitted == 0 &&
                           tombstone_plan.audit_log_appended == 0 &&
                           tombstone_plan.seal_submitted == 0 &&
                           tombstone_plan.seal_state_written == 0 &&
                           tombstone_plan.cleanup_submitted == 0 &&
                           tombstone_plan.retire_submitted == 0 &&
                           tombstone_plan.ack_submitted == 0 &&
                           tombstone_plan.completion_reported == 0 &&
                           tombstone_plan.deadline_armed == 0 &&
                           tombstone_plan.sync_submitted == 0 &&
                           tombstone_plan.timeline_submitted == 0 &&
                           tombstone_plan.fence_submitted == 0 &&
                           tombstone_plan.barrier_submitted == 0 &&
                           tombstone_plan.flush_submitted == 0 &&
                           tombstone_plan.framebuffer_written == 0 &&
                           tombstone_plan.blit_pixels_copied == 0 &&
                           tombstone_plan.output_submitted == 0 &&
                           tombstone_plan.display_mode_committed == 0 &&
                           tombstone_plan.scanout_submitted == 0 &&
                           tombstone_plan.vsync_submitted == 0 &&
                           tombstone_plan.schedule_submitted == 0 &&
                           tombstone_plan.present_submitted == 0 &&
                           tombstone_plan.damage_submitted == 0 &&
                           tombstone_plan.page_flip_submitted == 0,
                       "credential tombstone plan must not execute GUI work");
  fails += expect_true(tombstone_plan.tombstone_credential_panel == 1 &&
                           tombstone_plan.tombstone_credential_input == 1 &&
                           tombstone_plan.tombstone_credential_focus == 1 &&
                           tombstone_plan.tombstone_text_login == 0 &&
                           tombstone_plan.tombstone_text_login_fallback == 0,
                       "credential tombstone plan should mark credential widgets");
  fails += expect_true(tombstone_plan.submit_callback_bound == 0 &&
                           tombstone_plan.auth_callback_bound == 0 &&
                           tombstone_plan.submit_enabled == 0 &&
                           tombstone_plan.auth_attempt_allowed == 0 &&
                           tombstone_plan.raw_secret_exposed == 0 &&
                           tombstone_plan.masked_text_exposed == 0 &&
                           tombstone_plan.length_redacted == 1,
                       "credential tombstone plan must stay redacted and auth-disabled");
  fails += expect_true(strings_equal(tombstone_plan.tombstone_ticket,
                                     "credential-screen-tombstone-ticket") &&
                           strings_equal(tombstone_plan.purge_ticket,
                                         "credential-screen-purge-ticket") &&
                           strings_equal(tombstone_plan.tombstone_policy,
                                         "declarative-tombstone-no-write") &&
                           strings_equal(tombstone_plan.state,
                                         "tombstone-credential-ready"),
                       "credential tombstone plan should report tombstone ticket");
  return fails;
}

static int test_loginwindow_credential_screen_tombstone_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_tombstone_plan tombstone_plan;

  fails += expect_true(build_loginwindow_credential_screen_tombstone_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1,
                           1, &tombstone_plan) == 0,
                       "credential tombstone plan recovery should build");
  fails += expect_true(tombstone_plan.tombstone_plan_safe == 1 &&
                           tombstone_plan.tombstone_allowed == 1 &&
                           tombstone_plan.tombstone_submitted == 0 &&
                           tombstone_plan.tombstone_persisted == 0 &&
                           tombstone_plan.tombstone_text_recovery == 1 &&
                           tombstone_plan.tombstone_text_login == 1 &&
                           tombstone_plan.tombstone_credential_focus == 0,
                       "credential tombstone plan recovery should mark text recovery");
  fails += expect_true(tombstone_plan.purge_submitted == 0 &&
                           tombstone_plan.expiry_submitted == 0 &&
                           tombstone_plan.retention_submitted == 0 &&
                           tombstone_plan.archive_submitted == 0 &&
                           tombstone_plan.journal_submitted == 0 &&
                           tombstone_plan.ledger_submitted == 0 &&
                           tombstone_plan.receipt_submitted == 0 &&
                           tombstone_plan.record_submitted == 0 &&
                           tombstone_plan.audit_submitted == 0 &&
                           tombstone_plan.audit_log_appended == 0 &&
                           tombstone_plan.seal_submitted == 0 &&
                           tombstone_plan.cleanup_submitted == 0 &&
                           tombstone_plan.retire_submitted == 0 &&
                           tombstone_plan.ack_submitted == 0 &&
                           tombstone_plan.completion_reported == 0 &&
                           tombstone_plan.deadline_armed == 0 &&
                           tombstone_plan.sync_submitted == 0 &&
                           tombstone_plan.timeline_submitted == 0 &&
                           tombstone_plan.fence_submitted == 0 &&
                           tombstone_plan.barrier_submitted == 0 &&
                           tombstone_plan.flush_submitted == 0 &&
                           tombstone_plan.framebuffer_written == 0 &&
                           tombstone_plan.output_submitted == 0 &&
                           tombstone_plan.display_mode_committed == 0 &&
                           tombstone_plan.page_flip_submitted == 0 &&
                           tombstone_plan.submit_enabled == 0 &&
                           tombstone_plan.auth_attempt_allowed == 0,
                       "credential tombstone plan recovery must not persist or output");
  fails += expect_true(strings_equal(tombstone_plan.tombstone_ticket,
                                     "text-recovery-tombstone-ticket") &&
                           strings_equal(tombstone_plan.compositor_target,
                                         "text-recovery-tombstone") &&
                           strings_equal(tombstone_plan.blocked_reason,
                                         "text-recovery-only"),
                       "credential tombstone plan recovery should report recovery ticket");

  fails += expect_true(build_loginwindow_credential_screen_tombstone_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0,
                           1, &tombstone_plan) == 0,
                       "credential tombstone plan resume should build");
  fails += expect_true(tombstone_plan.tombstone_plan_safe == 1 &&
                           tombstone_plan.tombstone_text_login_resume == 1 &&
                           tombstone_plan.session_reset_required == 1 &&
                           tombstone_plan.login_screen_rerender_required == 1 &&
                           tombstone_plan.tombstone_submitted == 0 &&
                           tombstone_plan.tombstone_persisted == 0 &&
                           tombstone_plan.purge_submitted == 0 &&
                           tombstone_plan.expiry_submitted == 0 &&
                           tombstone_plan.submit_enabled == 0 &&
                           tombstone_plan.auth_attempt_allowed == 0,
                       "credential tombstone plan resume should keep GUI auth disabled");
  fails += expect_true(strings_equal(tombstone_plan.tombstone_ticket,
                                     "text-login-resume-tombstone-ticket") &&
                           strings_equal(tombstone_plan.tombstone_policy,
                                         "full-tombstone-declarative") &&
                           strings_equal(tombstone_plan.state,
                                         "tombstone-resume-ready"),
                       "credential tombstone plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_tombstone_plan_forces_text_login_for_submit_and_unknown(void) {
  int fails = 0;
  struct login_window_credential_screen_tombstone_plan tombstone_plan;

  fails += expect_true(build_loginwindow_credential_screen_tombstone_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0,
                           1, &tombstone_plan) == 0,
                       "credential tombstone plan submit should build");
  fails += expect_true(tombstone_plan.tombstone_plan_safe == 1 &&
                           tombstone_plan.submit_requested == 1 &&
                           tombstone_plan.tombstone_text_login_fallback == 1 &&
                           tombstone_plan.action_allowed == 0 &&
                           tombstone_plan.action_blocked == 1 &&
                           tombstone_plan.input_focus_allowed == 0,
                       "credential tombstone plan submit should force text login fallback");
  fails += expect_true(tombstone_plan.tombstone_submitted == 0 &&
                           tombstone_plan.tombstone_persist_allowed == 0 &&
                           tombstone_plan.tombstone_persisted == 0 &&
                           tombstone_plan.tombstone_cpu_gpu_sync_allowed == 0 &&
                           tombstone_plan.tombstone_cpu_gpu_sync_submitted == 0 &&
                           tombstone_plan.purge_submitted == 0 &&
                           tombstone_plan.purge_persisted == 0 &&
                           tombstone_plan.purge_deleted == 0 &&
                           tombstone_plan.expiry_submitted == 0 &&
                           tombstone_plan.expiry_persisted == 0 &&
                           tombstone_plan.retention_submitted == 0 &&
                           tombstone_plan.retention_persisted == 0 &&
                           tombstone_plan.archive_submitted == 0 &&
                           tombstone_plan.archive_persisted == 0 &&
                           tombstone_plan.journal_submitted == 0 &&
                           tombstone_plan.journal_persisted == 0 &&
                           tombstone_plan.ledger_submitted == 0 &&
                           tombstone_plan.ledger_persisted == 0 &&
                           tombstone_plan.receipt_submitted == 0 &&
                           tombstone_plan.receipt_persisted == 0 &&
                           tombstone_plan.record_submitted == 0 &&
                           tombstone_plan.audit_submitted == 0 &&
                           tombstone_plan.audit_log_appended == 0 &&
                           tombstone_plan.seal_submitted == 0 &&
                           tombstone_plan.cleanup_submitted == 0 &&
                           tombstone_plan.retire_submitted == 0 &&
                           tombstone_plan.ack_submitted == 0 &&
                           tombstone_plan.completion_reported == 0 &&
                           tombstone_plan.deadline_armed == 0 &&
                           tombstone_plan.sync_submitted == 0 &&
                           tombstone_plan.timeline_submitted == 0 &&
                           tombstone_plan.fence_submitted == 0 &&
                           tombstone_plan.barrier_submitted == 0 &&
                           tombstone_plan.flush_submitted == 0 &&
                           tombstone_plan.framebuffer_written == 0 &&
                           tombstone_plan.blit_pixels_copied == 0 &&
                           tombstone_plan.output_submitted == 0 &&
                           tombstone_plan.display_submitted == 0 &&
                           tombstone_plan.display_mode_committed == 0 &&
                           tombstone_plan.page_flip_submitted == 0 &&
                           tombstone_plan.submit_callback_bound == 0 &&
                           tombstone_plan.auth_callback_bound == 0 &&
                           tombstone_plan.submit_enabled == 0 &&
                           tombstone_plan.auth_attempt_allowed == 0,
                       "credential tombstone plan submit must stay declarative");
  fails += expect_true(strings_equal(tombstone_plan.tombstone_ticket,
                                     "text-login-fallback-tombstone-ticket") &&
                           strings_equal(tombstone_plan.primary_action,
                                         "use-text-login") &&
                           strings_equal(tombstone_plan.blocked_reason,
                                         "gui-submit-disabled"),
                       "credential tombstone plan submit should explain fallback");

  fails += expect_true(build_loginwindow_credential_screen_tombstone_plan_for_action(
                           99, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0,
                           0, 1, &tombstone_plan) == 0,
                       "credential tombstone plan unknown should build");
  fails += expect_true(tombstone_plan.tombstone_plan_safe == 1 &&
                           tombstone_plan.tombstone_text_login_fallback == 1 &&
                           tombstone_plan.action_allowed == 0 &&
                           tombstone_plan.action_blocked == 1,
                       "credential tombstone plan unknown should force text login fallback");
  fails += expect_true(strings_equal(tombstone_plan.tombstone_ticket,
                                     "text-login-fallback-tombstone-ticket") &&
                           strings_equal(tombstone_plan.blocked_reason,
                                         "credential-action-unknown"),
                       "credential tombstone plan unknown should preserve block reason");
  return fails;
}

static int test_loginwindow_credential_screen_tombstone_plan_fails_closed_for_unsafe_or_missing_purge_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_purge_plan purge_plan;
  struct login_window_credential_screen_tombstone_plan tombstone_plan;

  reset_test_state();
  fails += expect_true(login_window_credential_screen_tombstone_plan_build(
                           NULL, &tombstone_plan) == 0,
                       "credential tombstone plan missing purge plan should build fail-closed state");
  fails += expect_true(tombstone_plan.purge_plan_available == 0 &&
                           tombstone_plan.purge_plan_safe == 0 &&
                           tombstone_plan.tombstone_plan_safe == 0 &&
                           tombstone_plan.route_selected == 0 &&
                           tombstone_plan.route_blocked == 1,
                       "credential tombstone plan missing purge plan should block tombstone plan");
  fails += expect_true(tombstone_plan.tombstone_allowed == 0 &&
                           tombstone_plan.tombstone_submitted == 0 &&
                           tombstone_plan.tombstone_persisted == 0 &&
                           tombstone_plan.tombstone_cpu_gpu_sync_submitted == 0 &&
                           tombstone_plan.tombstone_text_login_fallback == 1 &&
                           tombstone_plan.submit_enabled == 0 &&
                           tombstone_plan.auth_attempt_allowed == 0,
                       "credential tombstone plan missing purge plan must stay redacted");
  fails += expect_true(strings_equal(tombstone_plan.tombstone_ticket,
                                     "text-login-fallback-tombstone-ticket") &&
                           strings_equal(tombstone_plan.event_type,
                                         "credential-screen-tombstone-plan-unavailable") &&
                           strings_equal(tombstone_plan.blocked_reason,
                                         "purge-plan-unavailable"),
                       "credential tombstone plan missing purge plan should explain block");

  fails += expect_true(build_loginwindow_credential_screen_purge_plan_for_action(
                           LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
                           LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'y', 0, 0, 0,
                           1, &purge_plan) == 0,
                       "credential tombstone plan submitted purge source should build");
  purge_plan.purge_submitted = 1;
  purge_plan.purge_persist_allowed = 1;
  purge_plan.purge_persisted = 1;
  purge_plan.purge_cpu_gpu_sync_allowed = 1;
  purge_plan.purge_cpu_gpu_sync_submitted = 1;
  purge_plan.purge_delete_allowed = 1;
  purge_plan.purge_deleted = 1;
  purge_plan.expiry_submitted = 1;
  purge_plan.expiry_persist_allowed = 1;
  purge_plan.expiry_persisted = 1;
  purge_plan.expiry_cpu_gpu_sync_allowed = 1;
  purge_plan.expiry_cpu_gpu_sync_submitted = 1;
  purge_plan.expiry_timer_allowed = 1;
  purge_plan.expiry_timer_armed = 1;
  purge_plan.expiry_delete_allowed = 1;
  purge_plan.expiry_deleted = 1;
  purge_plan.retention_submitted = 1;
  purge_plan.retention_persist_allowed = 1;
  purge_plan.retention_persisted = 1;
  purge_plan.archive_submitted = 1;
  purge_plan.archive_persist_allowed = 1;
  purge_plan.archive_persisted = 1;
  purge_plan.journal_submitted = 1;
  purge_plan.journal_persist_allowed = 1;
  purge_plan.journal_persisted = 1;
  purge_plan.ledger_submitted = 1;
  purge_plan.ledger_persist_allowed = 1;
  purge_plan.ledger_persisted = 1;
  purge_plan.receipt_submitted = 1;
  purge_plan.receipt_persist_allowed = 1;
  purge_plan.receipt_persisted = 1;
  purge_plan.record_submitted = 1;
  purge_plan.record_persisted = 1;
  purge_plan.audit_submitted = 1;
  purge_plan.audit_log_append_allowed = 1;
  purge_plan.audit_log_appended = 1;
  purge_plan.seal_submitted = 1;
  purge_plan.seal_state_write_allowed = 1;
  purge_plan.seal_state_written = 1;
  purge_plan.cleanup_submitted = 1;
  purge_plan.retire_submitted = 1;
  purge_plan.ack_submitted = 1;
  purge_plan.completion_reported = 1;
  purge_plan.deadline_armed = 1;
  purge_plan.deadline_timer_armed = 1;
  purge_plan.deadline_expired = 1;
  purge_plan.sync_submitted = 1;
  purge_plan.sync_wait_allowed = 1;
  purge_plan.sync_wait_submitted = 1;
  purge_plan.sync_signal_allowed = 1;
  purge_plan.sync_signal_submitted = 1;
  purge_plan.sync_deadline_armed = 1;
  purge_plan.sync_completion_reported = 1;
  purge_plan.sync_cpu_gpu_sync_allowed = 1;
  purge_plan.sync_cpu_gpu_sync_submitted = 1;
  purge_plan.timeline_submitted = 1;
  purge_plan.timeline_wait_allowed = 1;
  purge_plan.timeline_wait_submitted = 1;
  purge_plan.timeline_signal_allowed = 1;
  purge_plan.timeline_signal_submitted = 1;
  purge_plan.timeline_semaphore_allowed = 1;
  purge_plan.timeline_semaphore_submitted = 1;
  purge_plan.timeline_value_allocated = 1;
  purge_plan.timeline_value_published = 1;
  purge_plan.timeline_cpu_gpu_sync_allowed = 1;
  purge_plan.timeline_cpu_gpu_sync_submitted = 1;
  purge_plan.fence_submitted = 1;
  purge_plan.fence_wait_allowed = 1;
  purge_plan.fence_wait_submitted = 1;
  purge_plan.fence_signal_allowed = 1;
  purge_plan.fence_signal_submitted = 1;
  purge_plan.fence_fd_export_allowed = 1;
  purge_plan.fence_fd_exported = 1;
  purge_plan.fence_cpu_gpu_sync_allowed = 1;
  purge_plan.fence_cpu_gpu_sync_submitted = 1;
  purge_plan.barrier_submitted = 1;
  purge_plan.barrier_memory_visibility_established = 1;
  purge_plan.barrier_cache_visibility_established = 1;
  purge_plan.barrier_cpu_gpu_sync_allowed = 1;
  purge_plan.barrier_cpu_gpu_sync_submitted = 1;
  purge_plan.flush_submitted = 1;
  purge_plan.flush_cache_clean_allowed = 1;
  purge_plan.flush_cache_cleaned = 1;
  purge_plan.flush_memory_barrier_allowed = 1;
  purge_plan.flush_memory_barrier_submitted = 1;
  purge_plan.framebuffer_submitted = 1;
  purge_plan.framebuffer_mapped = 1;
  purge_plan.framebuffer_write_allowed = 1;
  purge_plan.framebuffer_written = 1;
  purge_plan.blit_submitted = 1;
  purge_plan.blit_pixels_copied = 1;
  purge_plan.output_submitted = 1;
  purge_plan.output_buffer_attached = 1;
  purge_plan.output_buffer_submitted = 1;
  purge_plan.output_flip_allowed = 1;
  purge_plan.output_flip_submitted = 1;
  purge_plan.display_submitted = 1;
  purge_plan.display_buffer_attached = 1;
  purge_plan.display_buffer_submitted = 1;
  purge_plan.display_mode_committed = 1;
  purge_plan.display_flip_allowed = 1;
  purge_plan.display_flip_submitted = 1;
  purge_plan.scanout_submitted = 1;
  purge_plan.scanout_buffer_attached = 1;
  purge_plan.scanout_buffer_submitted = 1;
  purge_plan.vsync_submitted = 1;
  purge_plan.vsync_fence_armed = 1;
  purge_plan.schedule_submitted = 1;
  purge_plan.present_submitted = 1;
  purge_plan.damage_submitted = 1;
  purge_plan.compositor_damage_submitted = 1;
  purge_plan.frame_timer_armed = 1;
  purge_plan.compositor_wake_allowed = 1;
  purge_plan.compositor_wake_submitted = 1;
  purge_plan.page_flip_allowed = 1;
  fails += expect_true(login_window_credential_screen_tombstone_plan_build(
                           &purge_plan, &tombstone_plan) == 0,
                       "credential tombstone plan submitted purge should fail closed");
  fails += expect_true(tombstone_plan.tombstone_plan_safe == 0 &&
                           tombstone_plan.tombstone_allowed == 0 &&
                           tombstone_plan.tombstone_submitted == 0 &&
                           tombstone_plan.tombstone_persist_allowed == 0 &&
                           tombstone_plan.tombstone_persisted == 0 &&
                           tombstone_plan.tombstone_cpu_gpu_sync_allowed == 0 &&
                           tombstone_plan.tombstone_cpu_gpu_sync_submitted == 0 &&
                           tombstone_plan.purge_submitted == 0 &&
                           tombstone_plan.purge_persist_allowed == 0 &&
                           tombstone_plan.purge_persisted == 0 &&
                           tombstone_plan.purge_cpu_gpu_sync_allowed == 0 &&
                           tombstone_plan.purge_cpu_gpu_sync_submitted == 0 &&
                           tombstone_plan.purge_deleted == 0 &&
                           tombstone_plan.expiry_submitted == 0 &&
                           tombstone_plan.expiry_persisted == 0 &&
                           tombstone_plan.expiry_timer_armed == 0 &&
                           tombstone_plan.retention_submitted == 0 &&
                           tombstone_plan.archive_submitted == 0 &&
                           tombstone_plan.journal_submitted == 0 &&
                           tombstone_plan.ledger_submitted == 0 &&
                           tombstone_plan.receipt_submitted == 0 &&
                           tombstone_plan.record_submitted == 0 &&
                           tombstone_plan.audit_submitted == 0 &&
                           tombstone_plan.audit_log_appended == 0 &&
                           tombstone_plan.seal_submitted == 0 &&
                           tombstone_plan.seal_state_written == 0 &&
                           tombstone_plan.cleanup_submitted == 0 &&
                           tombstone_plan.retire_submitted == 0 &&
                           tombstone_plan.ack_submitted == 0 &&
                           tombstone_plan.completion_reported == 0 &&
                           tombstone_plan.deadline_armed == 0 &&
                           tombstone_plan.deadline_timer_armed == 0 &&
                           tombstone_plan.deadline_expired == 0 &&
                           tombstone_plan.sync_submitted == 0 &&
                           tombstone_plan.sync_wait_allowed == 0 &&
                           tombstone_plan.sync_wait_submitted == 0 &&
                           tombstone_plan.sync_signal_allowed == 0 &&
                           tombstone_plan.sync_signal_submitted == 0 &&
                           tombstone_plan.sync_deadline_armed == 0 &&
                           tombstone_plan.sync_completion_reported == 0 &&
                           tombstone_plan.sync_cpu_gpu_sync_allowed == 0 &&
                           tombstone_plan.sync_cpu_gpu_sync_submitted == 0 &&
                           tombstone_plan.timeline_submitted == 0 &&
                           tombstone_plan.timeline_wait_allowed == 0 &&
                           tombstone_plan.timeline_wait_submitted == 0 &&
                           tombstone_plan.timeline_signal_allowed == 0 &&
                           tombstone_plan.timeline_signal_submitted == 0 &&
                           tombstone_plan.timeline_semaphore_allowed == 0 &&
                           tombstone_plan.timeline_semaphore_submitted == 0 &&
                           tombstone_plan.timeline_value_allocated == 0 &&
                           tombstone_plan.timeline_value_published == 0 &&
                           tombstone_plan.timeline_cpu_gpu_sync_allowed == 0 &&
                           tombstone_plan.timeline_cpu_gpu_sync_submitted == 0 &&
                           tombstone_plan.fence_submitted == 0 &&
                           tombstone_plan.fence_wait_allowed == 0 &&
                           tombstone_plan.fence_wait_submitted == 0 &&
                           tombstone_plan.fence_signal_allowed == 0 &&
                           tombstone_plan.fence_signal_submitted == 0 &&
                           tombstone_plan.fence_fd_export_allowed == 0 &&
                           tombstone_plan.fence_fd_exported == 0 &&
                           tombstone_plan.fence_cpu_gpu_sync_allowed == 0 &&
                           tombstone_plan.fence_cpu_gpu_sync_submitted == 0 &&
                           tombstone_plan.barrier_submitted == 0 &&
                           tombstone_plan.barrier_memory_visibility_established == 0 &&
                           tombstone_plan.barrier_cache_visibility_established == 0 &&
                           tombstone_plan.barrier_cpu_gpu_sync_allowed == 0 &&
                           tombstone_plan.barrier_cpu_gpu_sync_submitted == 0 &&
                           tombstone_plan.flush_submitted == 0 &&
                           tombstone_plan.flush_cache_clean_allowed == 0 &&
                           tombstone_plan.flush_cache_cleaned == 0 &&
                           tombstone_plan.flush_memory_barrier_allowed == 0 &&
                           tombstone_plan.flush_memory_barrier_submitted == 0 &&
                           tombstone_plan.framebuffer_submitted == 0 &&
                           tombstone_plan.framebuffer_written == 0 &&
                           tombstone_plan.blit_pixels_copied == 0 &&
                           tombstone_plan.output_submitted == 0 &&
                           tombstone_plan.display_submitted == 0 &&
                           tombstone_plan.display_mode_committed == 0 &&
                           tombstone_plan.scanout_submitted == 0 &&
                           tombstone_plan.vsync_submitted == 0 &&
                           tombstone_plan.page_flip_allowed == 0 &&
                           tombstone_plan.page_flip_submitted == 0 &&
                           tombstone_plan.submit_enabled == 0 &&
                           tombstone_plan.auth_attempt_allowed == 0,
                       "credential tombstone plan must not copy unsafe submitted purge state");
  fails += expect_true(strings_equal(tombstone_plan.tombstone_ticket,
                                     "text-login-fallback-tombstone-ticket") &&
                           strings_equal(tombstone_plan.event_type,
                                         "credential-screen-tombstone-plan-unsafe") &&
                           strings_equal(tombstone_plan.blocked_reason,
                                         "credential-tombstone-plan-unsafe"),
                       "credential tombstone plan unsafe purge should force text login");
  return fails;
}

int test_login_runtime_credential_tombstone_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_tombstone_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_tombstone_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_tombstone_plan_forces_text_login_for_submit_and_unknown();
  fails += test_loginwindow_credential_screen_tombstone_plan_fails_closed_for_unsafe_or_missing_purge_plan();
  return fails;
}
