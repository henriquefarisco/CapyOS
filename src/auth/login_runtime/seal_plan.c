/*
 * src/auth/login_runtime/seal_plan.c
 *
 * Credential-screen seal plan reset + safety helper + builder —
 * extracted byte-for-byte from `src/auth/login_runtime.c` during PR
 * C.33 of the Estagio C dedicated plan.  Hosts the per-plan reset
 * helper, the upstream-safety predicate and the public builder for
 * the seal stage of the credential pipeline:
 *
 *   - login_window_credential_screen_seal_plan_reset (static)
 *   - login_window_credential_screen_seal_plan_cleanup_is_safe (static)
 *   - login_window_credential_screen_seal_plan_build
 *
 * The seal-plan converts a fail-closed cleanup-plan into a seal
 * contract for the downstream audit stage.  The static `_reset`
 * helper is the canonical "blocked" initializer and the static
 * `_cleanup_is_safe` predicate consolidates the upstream-safety
 * checks shared with future internal callers.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_seal_plan_reset(
    struct login_window_credential_screen_seal_plan *out,
    int cleanup_plan_available) {
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_SEAL_PLAN_VERSION;
  out->cleanup_plan_available = cleanup_plan_available ? 1 : 0;
  out->cleanup_plan_safe = 0;
  out->seal_plan_safe = 0;
  out->requested_action = 0;
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->cleanup_required = 1;
  out->cleanup_allowed = 0;
  out->cleanup_submitted = 0;
  out->cleanup_ticket_selected = 0;
  out->cleanup_target_selected = 0;
  out->cleanup_resource_release_allowed = 0;
  out->cleanup_resource_released = 0;
  out->cleanup_cpu_gpu_sync_allowed = 0;
  out->cleanup_cpu_gpu_sync_submitted = 0;
  out->retire_required = 1;
  out->retire_allowed = 0;
  out->retire_submitted = 0;
  out->retire_ticket_selected = 0;
  out->retire_target_selected = 0;
  out->retire_resource_release_allowed = 0;
  out->retire_resource_released = 0;
  out->retire_cpu_gpu_sync_allowed = 0;
  out->retire_cpu_gpu_sync_submitted = 0;
  out->ack_required = 1;
  out->ack_allowed = 0;
  out->ack_submitted = 0;
  out->ack_ticket_selected = 0;
  out->ack_target_selected = 0;
  out->ack_cpu_gpu_sync_allowed = 0;
  out->ack_cpu_gpu_sync_submitted = 0;
  out->completion_required = 1;
  out->completion_allowed = 0;
  out->completion_report_required = 1;
  out->completion_reported = 0;
  out->completion_ack_required = 1;
  out->completion_acknowledged = 0;
  out->completion_ticket_selected = 0;
  out->completion_target_selected = 0;
  out->completion_cpu_gpu_sync_allowed = 0;
  out->completion_cpu_gpu_sync_submitted = 0;
  out->deadline_required = 1;
  out->deadline_allowed = 0;
  out->deadline_armed = 0;
  out->deadline_timer_required = 1;
  out->deadline_timer_armed = 0;
  out->deadline_expired = 0;
  out->deadline_completion_required = 1;
  out->deadline_completion_reported = 0;
  out->deadline_cpu_gpu_sync_allowed = 0;
  out->deadline_cpu_gpu_sync_submitted = 0;
  out->sync_submitted = 0;
  out->sync_wait_allowed = 0;
  out->sync_wait_submitted = 0;
  out->sync_signal_allowed = 0;
  out->sync_signal_submitted = 0;
  out->sync_deadline_armed = 0;
  out->sync_completion_reported = 0;
  out->sync_cpu_gpu_sync_allowed = 0;
  out->sync_cpu_gpu_sync_submitted = 0;
  out->timeline_submitted = 0;
  out->timeline_wait_allowed = 0;
  out->timeline_wait_submitted = 0;
  out->timeline_signal_allowed = 0;
  out->timeline_signal_submitted = 0;
  out->timeline_semaphore_allowed = 0;
  out->timeline_semaphore_submitted = 0;
  out->timeline_value_allocated = 0;
  out->timeline_value_published = 0;
  out->timeline_cpu_gpu_sync_allowed = 0;
  out->timeline_cpu_gpu_sync_submitted = 0;
  out->fence_submitted = 0;
  out->fence_wait_allowed = 0;
  out->fence_wait_submitted = 0;
  out->fence_signal_allowed = 0;
  out->fence_signal_submitted = 0;
  out->fence_fd_export_allowed = 0;
  out->fence_fd_exported = 0;
  out->fence_cpu_gpu_sync_allowed = 0;
  out->fence_cpu_gpu_sync_submitted = 0;
  out->barrier_submitted = 0;
  out->barrier_memory_visibility_established = 0;
  out->barrier_cache_visibility_established = 0;
  out->barrier_cpu_gpu_sync_allowed = 0;
  out->barrier_cpu_gpu_sync_submitted = 0;
  out->flush_submitted = 0;
  out->flush_cache_clean_allowed = 0;
  out->flush_cache_cleaned = 0;
  out->flush_memory_barrier_allowed = 0;
  out->flush_memory_barrier_submitted = 0;
  out->framebuffer_submitted = 0;
  out->framebuffer_mapped = 0;
  out->framebuffer_write_allowed = 0;
  out->framebuffer_written = 0;
  out->framebuffer_flushed = 0;
  out->framebuffer_cache_cleaned = 0;
  out->blit_submitted = 0;
  out->blit_source_buffer_mapped = 0;
  out->blit_destination_buffer_mapped = 0;
  out->blit_pixels_copied = 0;
  out->blit_dma_allowed = 0;
  out->blit_dma_submitted = 0;
  out->output_submitted = 0;
  out->output_buffer_attached = 0;
  out->output_buffer_submitted = 0;
  out->output_flip_allowed = 0;
  out->output_flip_submitted = 0;
  out->display_submitted = 0;
  out->display_buffer_attached = 0;
  out->display_buffer_submitted = 0;
  out->display_mode_committed = 0;
  out->display_flip_allowed = 0;
  out->display_flip_submitted = 0;
  out->scanout_submitted = 0;
  out->scanout_buffer_attached = 0;
  out->scanout_buffer_submitted = 0;
  out->vsync_submitted = 0;
  out->vsync_wait_submitted = 0;
  out->vsync_fence_armed = 0;
  out->schedule_submitted = 0;
  out->present_submitted = 0;
  out->damage_submitted = 0;
  out->compositor_damage_submitted = 0;
  out->frame_timer_armed = 0;
  out->compositor_wake_allowed = 0;
  out->compositor_wake_submitted = 0;
  out->page_flip_allowed = 0;
  out->page_flip_submitted = 0;
  out->seal_required = 1;
  out->seal_allowed = 0;
  out->seal_submitted = 0;
  out->seal_ticket_selected = 0;
  out->seal_target_selected = 0;
  out->seal_state_write_allowed = 0;
  out->seal_state_written = 0;
  out->seal_cpu_gpu_sync_allowed = 0;
  out->seal_cpu_gpu_sync_submitted = 0;
  out->seal_credential_panel = 0;
  out->seal_credential_input = 0;
  out->seal_credential_focus = 0;
  out->seal_text_recovery = 0;
  out->seal_text_login = 1;
  out->seal_text_login_resume = 0;
  out->seal_text_login_fallback = 1;
  out->seal_error = 1;
  out->submit_callback_bound = 0;
  out->auth_callback_bound = 0;
  out->recovery_text_session_required = 1;
  out->session_reset_required = 1;
  out->login_screen_rerender_required = 1;
  out->credential_session_safe = 0;
  out->credential_storage_wiped = 0;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->raw_secret_exposed = 0;
  out->masked_text_exposed = 0;
  out->submit_requested = 0;
  out->submit_blocked = 1;
  out->submit_enabled = 0;
  out->auth_attempt_allowed = 0;
  out->text_login_authoritative = 1;
  out->view = "text-login-fallback";
  out->widget_tree = "text-login-fallback-bindings";
  out->cleanup_ticket = "text-login-fallback-cleanup-ticket";
  out->seal_ticket = "text-login-fallback-seal-ticket";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->compositor_target = "none";
  out->cleanup_policy = "cleanup-disabled";
  out->seal_policy = "seal-disabled";
  out->event_type = "credential-screen-seal-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "cleanup-plan-unavailable";
}

static int login_window_credential_screen_seal_plan_cleanup_is_safe(
    const struct login_window_credential_screen_cleanup_plan *cleanup_plan) {
  return cleanup_plan->cleanup_plan_safe && cleanup_plan->cleanup_required &&
         cleanup_plan->cleanup_allowed && !cleanup_plan->cleanup_submitted &&
         cleanup_plan->cleanup_ticket_selected && cleanup_plan->cleanup_target_selected &&
         !cleanup_plan->cleanup_resource_release_allowed &&
         !cleanup_plan->cleanup_resource_released && !cleanup_plan->cleanup_cpu_gpu_sync_allowed &&
         !cleanup_plan->cleanup_cpu_gpu_sync_submitted && !cleanup_plan->cleanup_error &&
         cleanup_plan->retire_required && cleanup_plan->retire_allowed &&
         !cleanup_plan->retire_submitted && cleanup_plan->retire_ticket_selected &&
         cleanup_plan->retire_target_selected && !cleanup_plan->retire_resource_release_allowed &&
         !cleanup_plan->retire_resource_released && !cleanup_plan->retire_cpu_gpu_sync_allowed &&
         !cleanup_plan->retire_cpu_gpu_sync_submitted && cleanup_plan->ack_required &&
         cleanup_plan->ack_allowed && !cleanup_plan->ack_submitted &&
         cleanup_plan->ack_ticket_selected && cleanup_plan->ack_target_selected &&
         !cleanup_plan->ack_cpu_gpu_sync_allowed && !cleanup_plan->ack_cpu_gpu_sync_submitted &&
         cleanup_plan->completion_required && cleanup_plan->completion_allowed &&
         cleanup_plan->completion_report_required && !cleanup_plan->completion_reported &&
         cleanup_plan->completion_ack_required && !cleanup_plan->completion_acknowledged &&
         cleanup_plan->completion_ticket_selected && cleanup_plan->completion_target_selected &&
         !cleanup_plan->completion_cpu_gpu_sync_allowed &&
         !cleanup_plan->completion_cpu_gpu_sync_submitted && cleanup_plan->deadline_required &&
         cleanup_plan->deadline_allowed && !cleanup_plan->deadline_armed &&
         cleanup_plan->deadline_timer_required && !cleanup_plan->deadline_timer_armed &&
         !cleanup_plan->deadline_expired && cleanup_plan->deadline_completion_required &&
         !cleanup_plan->deadline_completion_reported &&
         !cleanup_plan->deadline_cpu_gpu_sync_allowed &&
         !cleanup_plan->deadline_cpu_gpu_sync_submitted && !cleanup_plan->sync_submitted &&
         !cleanup_plan->sync_wait_allowed && !cleanup_plan->sync_wait_submitted &&
         !cleanup_plan->sync_signal_allowed && !cleanup_plan->sync_signal_submitted &&
         !cleanup_plan->sync_deadline_armed && !cleanup_plan->sync_completion_reported &&
         !cleanup_plan->sync_cpu_gpu_sync_allowed && !cleanup_plan->sync_cpu_gpu_sync_submitted &&
         !cleanup_plan->timeline_submitted && !cleanup_plan->timeline_wait_allowed &&
         !cleanup_plan->timeline_wait_submitted && !cleanup_plan->timeline_signal_allowed &&
         !cleanup_plan->timeline_signal_submitted && !cleanup_plan->timeline_semaphore_allowed &&
         !cleanup_plan->timeline_semaphore_submitted && !cleanup_plan->timeline_value_allocated &&
         !cleanup_plan->timeline_value_published && !cleanup_plan->timeline_cpu_gpu_sync_allowed &&
         !cleanup_plan->timeline_cpu_gpu_sync_submitted && !cleanup_plan->fence_submitted &&
         !cleanup_plan->fence_wait_allowed && !cleanup_plan->fence_wait_submitted &&
         !cleanup_plan->fence_signal_allowed && !cleanup_plan->fence_signal_submitted &&
         !cleanup_plan->fence_fd_export_allowed && !cleanup_plan->fence_fd_exported &&
         !cleanup_plan->fence_cpu_gpu_sync_allowed && !cleanup_plan->fence_cpu_gpu_sync_submitted &&
         !cleanup_plan->barrier_submitted && !cleanup_plan->barrier_memory_visibility_established &&
         !cleanup_plan->barrier_cache_visibility_established &&
         !cleanup_plan->barrier_cpu_gpu_sync_allowed && !cleanup_plan->barrier_cpu_gpu_sync_submitted &&
         !cleanup_plan->flush_submitted && !cleanup_plan->flush_cache_clean_allowed &&
         !cleanup_plan->flush_cache_cleaned && !cleanup_plan->flush_memory_barrier_allowed &&
         !cleanup_plan->flush_memory_barrier_submitted && !cleanup_plan->framebuffer_submitted &&
         !cleanup_plan->framebuffer_mapped && !cleanup_plan->framebuffer_write_allowed &&
         !cleanup_plan->framebuffer_written && !cleanup_plan->framebuffer_flushed &&
         !cleanup_plan->framebuffer_cache_cleaned && !cleanup_plan->blit_submitted &&
         !cleanup_plan->blit_source_buffer_mapped && !cleanup_plan->blit_destination_buffer_mapped &&
         !cleanup_plan->blit_pixels_copied && !cleanup_plan->blit_dma_allowed &&
         !cleanup_plan->blit_dma_submitted && !cleanup_plan->output_submitted &&
         !cleanup_plan->output_buffer_attached && !cleanup_plan->output_buffer_submitted &&
         !cleanup_plan->output_flip_allowed && !cleanup_plan->output_flip_submitted &&
         !cleanup_plan->display_submitted && !cleanup_plan->display_buffer_attached &&
         !cleanup_plan->display_buffer_submitted && !cleanup_plan->display_mode_committed &&
         !cleanup_plan->display_flip_allowed && !cleanup_plan->display_flip_submitted &&
         !cleanup_plan->scanout_submitted && !cleanup_plan->scanout_buffer_attached &&
         !cleanup_plan->scanout_buffer_submitted && !cleanup_plan->vsync_submitted &&
         !cleanup_plan->vsync_wait_submitted && !cleanup_plan->vsync_fence_armed &&
         !cleanup_plan->schedule_submitted && !cleanup_plan->present_submitted &&
         !cleanup_plan->damage_submitted && !cleanup_plan->compositor_damage_submitted &&
         !cleanup_plan->frame_timer_armed && !cleanup_plan->compositor_wake_allowed &&
         !cleanup_plan->compositor_wake_submitted && !cleanup_plan->page_flip_allowed &&
         !cleanup_plan->page_flip_submitted && cleanup_plan->route_selected &&
         !cleanup_plan->route_blocked && cleanup_plan->credential_session_safe &&
         cleanup_plan->credential_storage_wiped && cleanup_plan->credential_redacted &&
         cleanup_plan->length_redacted && !cleanup_plan->raw_secret_exposed &&
         !cleanup_plan->masked_text_exposed && cleanup_plan->submit_blocked &&
         !cleanup_plan->submit_enabled && !cleanup_plan->auth_attempt_allowed &&
         !cleanup_plan->submit_callback_bound && !cleanup_plan->auth_callback_bound &&
         cleanup_plan->text_login_authoritative;
}

int login_window_credential_screen_seal_plan_build(
    const struct login_window_credential_screen_cleanup_plan *cleanup_plan,
    struct login_window_credential_screen_seal_plan *out) {
  if (!out) return -1;
  login_window_credential_screen_seal_plan_reset(out, cleanup_plan ? 1 : 0);
  if (!cleanup_plan) return 0;
  out->requested_action = cleanup_plan->requested_action;
  out->cleanup_plan_safe = cleanup_plan->cleanup_plan_safe ? 1 : 0;
  if (!login_window_credential_screen_seal_plan_cleanup_is_safe(cleanup_plan)) {
    out->event_type = "credential-screen-seal-plan-unsafe";
    out->blocked_reason = "credential-seal-plan-unsafe";
    out->message = "Credential screen seal plan unsafe; use text login.";
    return 0;
  }
  out->seal_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = cleanup_plan->action_allowed ? 1 : 0;
  out->action_blocked = cleanup_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = cleanup_plan->input_focus_allowed ? 1 : 0;
  out->cleanup_required = cleanup_plan->cleanup_required ? 1 : 0;
  out->cleanup_allowed = cleanup_plan->cleanup_allowed ? 1 : 0;
  out->cleanup_ticket_selected = cleanup_plan->cleanup_ticket_selected ? 1 : 0;
  out->cleanup_target_selected = cleanup_plan->cleanup_target_selected ? 1 : 0;
  out->retire_required = cleanup_plan->retire_required ? 1 : 0;
  out->retire_allowed = cleanup_plan->retire_allowed ? 1 : 0;
  out->retire_ticket_selected = cleanup_plan->retire_ticket_selected ? 1 : 0;
  out->retire_target_selected = cleanup_plan->retire_target_selected ? 1 : 0;
  out->ack_required = cleanup_plan->ack_required ? 1 : 0;
  out->ack_allowed = cleanup_plan->ack_allowed ? 1 : 0;
  out->ack_ticket_selected = cleanup_plan->ack_ticket_selected ? 1 : 0;
  out->ack_target_selected = cleanup_plan->ack_target_selected ? 1 : 0;
  out->completion_required = cleanup_plan->completion_required ? 1 : 0;
  out->completion_allowed = cleanup_plan->completion_allowed ? 1 : 0;
  out->completion_report_required = cleanup_plan->completion_report_required ? 1 : 0;
  out->completion_ack_required = cleanup_plan->completion_ack_required ? 1 : 0;
  out->completion_ticket_selected = cleanup_plan->completion_ticket_selected ? 1 : 0;
  out->completion_target_selected = cleanup_plan->completion_target_selected ? 1 : 0;
  out->deadline_required = cleanup_plan->deadline_required ? 1 : 0;
  out->deadline_allowed = cleanup_plan->deadline_allowed ? 1 : 0;
  out->seal_allowed = 1;
  out->seal_ticket_selected = 1;
  out->seal_target_selected = 1;
  out->seal_error = 0;
  out->recovery_text_session_required = cleanup_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = cleanup_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = cleanup_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = cleanup_plan->submit_requested ? 1 : 0;
  out->text_login_authoritative = 1;
  out->view = cleanup_plan->view;
  out->widget_tree = cleanup_plan->widget_tree;
  out->cleanup_ticket = cleanup_plan->cleanup_ticket;
  out->focus_target = cleanup_plan->focus_target;
  out->primary_action = cleanup_plan->primary_action;
  out->route = cleanup_plan->route;
  out->compositor_target = cleanup_plan->compositor_target;
  out->cleanup_policy = cleanup_plan->cleanup_policy;
  out->seal_policy = "declarative-seal-no-write";
  out->event_type = "credential-screen-seal-plan-ready";
  out->state = "seal-ready";
  out->message = "Credential screen seal ticket ready; no state written.";
  out->blocked_reason = cleanup_plan->blocked_reason;
  if (cleanup_plan->submit_requested) {
    out->seal_ticket = "text-login-fallback-seal-ticket";
    out->compositor_target = "text-login-fallback-seal";
    out->seal_policy = "fallback-seal-declarative";
    out->seal_text_login = 1;
    out->seal_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "seal-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (cleanup_plan->cleanup_credential_panel && cleanup_plan->cleanup_credential_input &&
      cleanup_plan->cleanup_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->seal_ticket = "credential-screen-seal-ticket";
    out->compositor_target = "credential-screen-seal";
    out->seal_credential_panel = 1;
    out->seal_credential_input = 1;
    out->seal_credential_focus = 1;
    out->seal_text_login = 0;
    out->seal_text_login_fallback = 0;
    out->state = "seal-credential-ready";
    out->message = "Credential seal ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (cleanup_plan->cleanup_text_recovery && out->recovery_text_session_required) {
    out->seal_ticket = "text-recovery-seal-ticket";
    out->compositor_target = "text-recovery-seal";
    out->seal_text_recovery = 1;
    out->seal_text_login = 1;
    out->seal_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "seal-text-recovery-ready";
    out->message = "Text recovery seal ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (cleanup_plan->cleanup_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->seal_ticket = "text-login-resume-seal-ticket";
    out->compositor_target = "text-login-resume-seal";
    out->seal_policy = "full-seal-declarative";
    out->seal_text_login = 1;
    out->seal_text_login_resume = 1;
    out->seal_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "seal-resume-ready";
    out->message = "Text login resume seal ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->seal_ticket = "text-login-fallback-seal-ticket";
  out->compositor_target = "text-login-fallback-seal";
  out->seal_policy = "fallback-seal-declarative";
  out->seal_text_login = 1;
  out->seal_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "seal-text-login-ready";
  out->message = "Use authoritative text login.";
  return 0;
}
