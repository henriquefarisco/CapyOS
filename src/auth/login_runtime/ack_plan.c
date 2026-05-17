/*
 * src/auth/login_runtime/ack_plan.c
 *
 * Credential-screen ack plan reset + safety helper + builder —
 * extracted byte-for-byte from `src/auth/login_runtime.c` during PR
 * C.30 of the Estagio C dedicated plan.  Hosts the per-plan reset
 * helper, the upstream-safety predicate and the public builder for
 * the ack stage of the credential pipeline:
 *
 *   - login_window_credential_screen_ack_plan_reset (static)
 *   - login_window_credential_screen_ack_plan_completion_is_safe (static)
 *   - login_window_credential_screen_ack_plan_build
 *
 * The ack-plan converts a fail-closed completion-plan into an ack
 * contract for the downstream retire stage.  The static `_reset`
 * helper is the canonical "blocked" initializer and the static
 * `_completion_is_safe` predicate consolidates the upstream-safety
 * checks shared with future internal callers.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_ack_plan_reset(
    struct login_window_credential_screen_ack_plan *out,
    int completion_plan_available) {
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_ACK_PLAN_VERSION;
  out->completion_plan_available = completion_plan_available ? 1 : 0;
  out->completion_plan_safe = 0;
  out->ack_plan_safe = 0;
  out->requested_action = 0;
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
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
  out->ack_required = 1;
  out->ack_allowed = 0;
  out->ack_submitted = 0;
  out->ack_ticket_selected = 0;
  out->ack_target_selected = 0;
  out->ack_cpu_gpu_sync_allowed = 0;
  out->ack_cpu_gpu_sync_submitted = 0;
  out->ack_credential_panel = 0;
  out->ack_credential_input = 0;
  out->ack_credential_focus = 0;
  out->ack_text_recovery = 0;
  out->ack_text_login = 1;
  out->ack_text_login_resume = 0;
  out->ack_text_login_fallback = 1;
  out->ack_error = 1;
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
  out->completion_ticket = "text-login-fallback-completion-ticket";
  out->ack_ticket = "text-login-fallback-ack-ticket";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->compositor_target = "none";
  out->completion_policy = "completion-disabled";
  out->ack_policy = "ack-disabled";
  out->event_type = "credential-screen-ack-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "completion-plan-unavailable";
}

static int login_window_credential_screen_ack_plan_completion_is_safe(
    const struct login_window_credential_screen_completion_plan *completion_plan) {
  return completion_plan->completion_plan_safe && completion_plan->completion_required &&
         completion_plan->completion_allowed && completion_plan->completion_report_required &&
         !completion_plan->completion_reported && completion_plan->completion_ack_required &&
         !completion_plan->completion_acknowledged && completion_plan->completion_ticket_selected &&
         completion_plan->completion_target_selected &&
         !completion_plan->completion_cpu_gpu_sync_allowed &&
         !completion_plan->completion_cpu_gpu_sync_submitted && !completion_plan->completion_error &&
         completion_plan->deadline_required && completion_plan->deadline_allowed &&
         !completion_plan->deadline_armed && completion_plan->deadline_timer_required &&
         !completion_plan->deadline_timer_armed && !completion_plan->deadline_expired &&
         completion_plan->deadline_completion_required &&
         !completion_plan->deadline_completion_reported &&
         !completion_plan->deadline_cpu_gpu_sync_allowed &&
         !completion_plan->deadline_cpu_gpu_sync_submitted && !completion_plan->sync_submitted &&
         !completion_plan->sync_wait_allowed && !completion_plan->sync_wait_submitted &&
         !completion_plan->sync_signal_allowed && !completion_plan->sync_signal_submitted &&
         !completion_plan->sync_deadline_armed && !completion_plan->sync_completion_reported &&
         !completion_plan->sync_cpu_gpu_sync_allowed &&
         !completion_plan->sync_cpu_gpu_sync_submitted && !completion_plan->timeline_submitted &&
         !completion_plan->timeline_wait_allowed && !completion_plan->timeline_wait_submitted &&
         !completion_plan->timeline_signal_allowed && !completion_plan->timeline_signal_submitted &&
         !completion_plan->timeline_semaphore_allowed && !completion_plan->timeline_semaphore_submitted &&
         !completion_plan->timeline_value_allocated && !completion_plan->timeline_value_published &&
         !completion_plan->timeline_cpu_gpu_sync_allowed &&
         !completion_plan->timeline_cpu_gpu_sync_submitted && !completion_plan->fence_submitted &&
         !completion_plan->fence_wait_allowed && !completion_plan->fence_wait_submitted &&
         !completion_plan->fence_signal_allowed && !completion_plan->fence_signal_submitted &&
         !completion_plan->fence_fd_export_allowed && !completion_plan->fence_fd_exported &&
         !completion_plan->fence_cpu_gpu_sync_allowed &&
         !completion_plan->fence_cpu_gpu_sync_submitted && !completion_plan->barrier_submitted &&
         !completion_plan->barrier_memory_visibility_established &&
         !completion_plan->barrier_cache_visibility_established &&
         !completion_plan->barrier_cpu_gpu_sync_allowed &&
         !completion_plan->barrier_cpu_gpu_sync_submitted && !completion_plan->flush_submitted &&
         !completion_plan->flush_cache_clean_allowed && !completion_plan->flush_cache_cleaned &&
         !completion_plan->flush_memory_barrier_allowed &&
         !completion_plan->flush_memory_barrier_submitted && !completion_plan->framebuffer_submitted &&
         !completion_plan->framebuffer_mapped && !completion_plan->framebuffer_write_allowed &&
         !completion_plan->framebuffer_written && !completion_plan->framebuffer_flushed &&
         !completion_plan->framebuffer_cache_cleaned && !completion_plan->blit_submitted &&
         !completion_plan->blit_source_buffer_mapped && !completion_plan->blit_destination_buffer_mapped &&
         !completion_plan->blit_pixels_copied && !completion_plan->blit_dma_allowed &&
         !completion_plan->blit_dma_submitted && !completion_plan->output_submitted &&
         !completion_plan->output_buffer_attached && !completion_plan->output_buffer_submitted &&
         !completion_plan->output_flip_allowed && !completion_plan->output_flip_submitted &&
         !completion_plan->display_submitted && !completion_plan->display_buffer_attached &&
         !completion_plan->display_buffer_submitted && !completion_plan->display_mode_committed &&
         !completion_plan->display_flip_allowed && !completion_plan->display_flip_submitted &&
         !completion_plan->scanout_submitted && !completion_plan->scanout_buffer_attached &&
         !completion_plan->scanout_buffer_submitted && !completion_plan->vsync_submitted &&
         !completion_plan->vsync_wait_submitted && !completion_plan->vsync_fence_armed &&
         !completion_plan->schedule_submitted && !completion_plan->present_submitted &&
         !completion_plan->damage_submitted && !completion_plan->compositor_damage_submitted &&
         !completion_plan->frame_timer_armed && !completion_plan->compositor_wake_allowed &&
         !completion_plan->compositor_wake_submitted && !completion_plan->page_flip_allowed &&
         !completion_plan->page_flip_submitted && completion_plan->route_selected &&
         !completion_plan->route_blocked && completion_plan->credential_session_safe &&
         completion_plan->credential_storage_wiped && completion_plan->credential_redacted &&
         completion_plan->length_redacted && !completion_plan->raw_secret_exposed &&
         !completion_plan->masked_text_exposed && completion_plan->submit_blocked &&
         !completion_plan->submit_enabled && !completion_plan->auth_attempt_allowed &&
         !completion_plan->submit_callback_bound && !completion_plan->auth_callback_bound &&
         completion_plan->text_login_authoritative;
}

int login_window_credential_screen_ack_plan_build(
    const struct login_window_credential_screen_completion_plan *completion_plan,
    struct login_window_credential_screen_ack_plan *out) {
  if (!out) return -1;
  login_window_credential_screen_ack_plan_reset(out, completion_plan ? 1 : 0);
  if (!completion_plan) return 0;
  out->requested_action = completion_plan->requested_action;
  out->completion_plan_safe = completion_plan->completion_plan_safe ? 1 : 0;
  if (!login_window_credential_screen_ack_plan_completion_is_safe(completion_plan)) {
    out->event_type = "credential-screen-ack-plan-unsafe";
    out->blocked_reason = "credential-ack-plan-unsafe";
    out->message = "Credential screen ack plan unsafe; use text login.";
    return 0;
  }
  out->ack_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = completion_plan->action_allowed ? 1 : 0;
  out->action_blocked = completion_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = completion_plan->input_focus_allowed ? 1 : 0;
  out->completion_required = completion_plan->completion_required ? 1 : 0;
  out->completion_allowed = completion_plan->completion_allowed ? 1 : 0;
  out->completion_report_required = completion_plan->completion_report_required ? 1 : 0;
  out->completion_ack_required = completion_plan->completion_ack_required ? 1 : 0;
  out->completion_ticket_selected = completion_plan->completion_ticket_selected ? 1 : 0;
  out->completion_target_selected = completion_plan->completion_target_selected ? 1 : 0;
  out->deadline_required = completion_plan->deadline_required ? 1 : 0;
  out->deadline_allowed = completion_plan->deadline_allowed ? 1 : 0;
  out->ack_allowed = 1;
  out->ack_ticket_selected = 1;
  out->ack_target_selected = 1;
  out->ack_error = 0;
  out->recovery_text_session_required = completion_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = completion_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = completion_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = completion_plan->submit_requested ? 1 : 0;
  out->text_login_authoritative = 1;
  out->view = completion_plan->view;
  out->widget_tree = completion_plan->widget_tree;
  out->completion_ticket = completion_plan->completion_ticket;
  out->focus_target = completion_plan->focus_target;
  out->primary_action = completion_plan->primary_action;
  out->route = completion_plan->route;
  out->compositor_target = completion_plan->compositor_target;
  out->completion_policy = completion_plan->completion_policy;
  out->ack_policy = "declarative-ack-no-submit";
  out->event_type = "credential-screen-ack-plan-ready";
  out->state = "ack-ready";
  out->message = "Credential screen ack ticket ready; no ack submitted.";
  out->blocked_reason = completion_plan->blocked_reason;
  if (completion_plan->submit_requested) {
    out->ack_ticket = "text-login-fallback-ack-ticket";
    out->compositor_target = "text-login-fallback-ack";
    out->ack_policy = "fallback-ack-declarative";
    out->ack_text_login = 1;
    out->ack_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "ack-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (completion_plan->completion_credential_panel && completion_plan->completion_credential_input &&
      completion_plan->completion_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->ack_ticket = "credential-screen-ack-ticket";
    out->compositor_target = "credential-screen-ack";
    out->ack_credential_panel = 1;
    out->ack_credential_input = 1;
    out->ack_credential_focus = 1;
    out->ack_text_login = 0;
    out->ack_text_login_fallback = 0;
    out->state = "ack-credential-ready";
    out->message = "Credential ack ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (completion_plan->completion_text_recovery && out->recovery_text_session_required) {
    out->ack_ticket = "text-recovery-ack-ticket";
    out->compositor_target = "text-recovery-ack";
    out->ack_text_recovery = 1;
    out->ack_text_login = 1;
    out->ack_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "ack-text-recovery-ready";
    out->message = "Text recovery ack ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (completion_plan->completion_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->ack_ticket = "text-login-resume-ack-ticket";
    out->compositor_target = "text-login-resume-ack";
    out->ack_policy = "full-ack-declarative";
    out->ack_text_login = 1;
    out->ack_text_login_resume = 1;
    out->ack_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "ack-resume-ready";
    out->message = "Text login resume ack ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->ack_ticket = "text-login-fallback-ack-ticket";
  out->compositor_target = "text-login-fallback-ack";
  out->ack_policy = "fallback-ack-declarative";
  out->ack_text_login = 1;
  out->ack_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "ack-text-login-ready";
  out->message = "Use authoritative text login.";
  return 0;
}
