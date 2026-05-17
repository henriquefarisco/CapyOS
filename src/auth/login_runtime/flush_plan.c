/*
 * src/auth/login_runtime/flush_plan.c
 *
 * Credential-screen flush plan reset + builder — extracted
 * byte-for-byte from `src/auth/login_runtime.c` during PR C.23 of
 * the Estagio C dedicated plan.  Hosts the per-plan reset helper
 * and the public builder for the flush stage of the credential
 * pipeline:
 *
 *   - login_window_credential_screen_flush_plan_reset (static)
 *   - login_window_credential_screen_flush_plan_build
 *
 * The flush-plan converts a fail-closed framebuffer-plan into a
 * flush contract for the downstream barrier stage.  The static
 * `_reset` helper is the canonical "blocked" initializer shared
 * between the build path and any future internal callers.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_flush_plan_reset(
    struct login_window_credential_screen_flush_plan *out,
    int framebuffer_plan_available) {
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_FLUSH_PLAN_VERSION;
  out->framebuffer_plan_available = framebuffer_plan_available ? 1 : 0;
  out->framebuffer_plan_safe = 0;
  out->flush_plan_safe = 0;
  out->requested_action = 0;
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->compositor_surface_allowed = 0;
  out->compositor_surface_submitted = 0;
  out->compositor_damage_planned = 0;
  out->compositor_damage_allowed = 0;
  out->compositor_damage_submitted = 0;
  out->damage_required = 1;
  out->damage_allowed = 0;
  out->damage_submitted = 0;
  out->damage_incremental_allowed = 0;
  out->full_damage_required = 1;
  out->damage_cache_allowed = 0;
  out->damage_cache_hit = 0;
  out->damage_reuse_allowed = 0;
  out->present_required = 1;
  out->present_allowed = 0;
  out->present_submitted = 0;
  out->present_ticket_selected = 0;
  out->present_incremental_allowed = 0;
  out->full_present_required = 1;
  out->present_cache_allowed = 0;
  out->present_cache_hit = 0;
  out->present_reuse_allowed = 0;
  out->schedule_required = 1;
  out->schedule_allowed = 0;
  out->schedule_submitted = 0;
  out->schedule_ticket_selected = 0;
  out->schedule_incremental_allowed = 0;
  out->full_schedule_required = 1;
  out->schedule_cache_allowed = 0;
  out->schedule_cache_hit = 0;
  out->schedule_reuse_allowed = 0;
  out->frame_pacing_required = 1;
  out->frame_pacing_allowed = 0;
  out->frame_timer_armed = 0;
  out->compositor_wake_allowed = 0;
  out->compositor_wake_submitted = 0;
  out->page_flip_allowed = 0;
  out->page_flip_submitted = 0;
  out->vsync_required = 1;
  out->vsync_allowed = 0;
  out->vsync_submitted = 0;
  out->vsync_ticket_selected = 0;
  out->vsync_wait_allowed = 0;
  out->vsync_wait_submitted = 0;
  out->vsync_fence_required = 1;
  out->vsync_fence_armed = 0;
  out->scanout_required = 1;
  out->scanout_allowed = 0;
  out->scanout_submitted = 0;
  out->scanout_ticket_selected = 0;
  out->scanout_target_selected = 0;
  out->scanout_buffer_attached = 0;
  out->scanout_buffer_submitted = 0;
  out->display_required = 1;
  out->display_allowed = 0;
  out->display_submitted = 0;
  out->display_ticket_selected = 0;
  out->display_target_selected = 0;
  out->display_buffer_attached = 0;
  out->display_buffer_submitted = 0;
  out->display_mode_required = 1;
  out->display_mode_committed = 0;
  out->display_flip_allowed = 0;
  out->display_flip_submitted = 0;
  out->output_required = 1;
  out->output_allowed = 0;
  out->output_submitted = 0;
  out->output_ticket_selected = 0;
  out->output_target_selected = 0;
  out->output_buffer_attached = 0;
  out->output_buffer_submitted = 0;
  out->output_flip_allowed = 0;
  out->output_flip_submitted = 0;
  out->blit_required = 1;
  out->blit_allowed = 0;
  out->blit_submitted = 0;
  out->blit_ticket_selected = 0;
  out->blit_target_selected = 0;
  out->blit_source_buffer_mapped = 0;
  out->blit_destination_buffer_mapped = 0;
  out->blit_pixels_copied = 0;
  out->blit_dma_allowed = 0;
  out->blit_dma_submitted = 0;
  out->framebuffer_required = 1;
  out->framebuffer_allowed = 0;
  out->framebuffer_submitted = 0;
  out->framebuffer_ticket_selected = 0;
  out->framebuffer_target_selected = 0;
  out->framebuffer_mapped = 0;
  out->framebuffer_write_allowed = 0;
  out->framebuffer_written = 0;
  out->framebuffer_flush_required = 1;
  out->framebuffer_flushed = 0;
  out->framebuffer_cache_clean_required = 1;
  out->framebuffer_cache_cleaned = 0;
  out->flush_required = 1;
  out->flush_allowed = 0;
  out->flush_submitted = 0;
  out->flush_ticket_selected = 0;
  out->flush_target_selected = 0;
  out->flush_cache_clean_required = 1;
  out->flush_cache_clean_allowed = 0;
  out->flush_cache_cleaned = 0;
  out->flush_memory_barrier_allowed = 0;
  out->flush_memory_barrier_submitted = 0;
  out->flush_credential_panel = 0;
  out->flush_credential_input = 0;
  out->flush_credential_focus = 0;
  out->flush_text_recovery = 0;
  out->flush_text_login = 1;
  out->flush_text_login_resume = 0;
  out->flush_text_login_fallback = 1;
  out->flush_error = 1;
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
  out->damage_ticket = "text-login-fallback-damage-ticket";
  out->present_ticket = "text-login-fallback-present-ticket";
  out->schedule_ticket = "text-login-fallback-schedule-ticket";
  out->vsync_ticket = "text-login-fallback-vsync-ticket";
  out->scanout_ticket = "text-login-fallback-scanout-ticket";
  out->display_ticket = "text-login-fallback-display-ticket";
  out->output_ticket = "text-login-fallback-output-ticket";
  out->blit_ticket = "text-login-fallback-blit-ticket";
  out->framebuffer_ticket = "text-login-fallback-framebuffer-ticket";
  out->flush_ticket = "text-login-fallback-flush-ticket";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->compositor_target = "none";
  out->damage_policy = "full-safe-fallback";
  out->cache_policy = "cache-disabled";
  out->present_policy = "present-disabled";
  out->schedule_policy = "schedule-disabled";
  out->vsync_policy = "vsync-disabled";
  out->scanout_policy = "scanout-disabled";
  out->display_policy = "display-disabled";
  out->output_policy = "output-disabled";
  out->blit_policy = "blit-disabled";
  out->framebuffer_policy = "framebuffer-disabled";
  out->flush_policy = "flush-disabled";
  out->event_type = "credential-screen-flush-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "framebuffer-plan-unavailable";
}

int login_window_credential_screen_flush_plan_build(
    const struct login_window_credential_screen_framebuffer_plan *framebuffer_plan,
    struct login_window_credential_screen_flush_plan *out) {
  int safe = 0;
  if (!out) return -1;
  login_window_credential_screen_flush_plan_reset(out, framebuffer_plan ? 1 : 0);
  if (!framebuffer_plan) return 0;
  out->requested_action = framebuffer_plan->requested_action;
  out->framebuffer_plan_safe = framebuffer_plan->framebuffer_plan_safe ? 1 : 0;
  safe = framebuffer_plan->framebuffer_plan_safe &&
         framebuffer_plan->framebuffer_required && framebuffer_plan->framebuffer_allowed &&
         !framebuffer_plan->framebuffer_submitted &&
         framebuffer_plan->framebuffer_ticket_selected &&
         framebuffer_plan->framebuffer_target_selected &&
         !framebuffer_plan->framebuffer_mapped &&
         !framebuffer_plan->framebuffer_write_allowed &&
         !framebuffer_plan->framebuffer_written &&
         framebuffer_plan->framebuffer_flush_required &&
         !framebuffer_plan->framebuffer_flushed &&
         framebuffer_plan->framebuffer_cache_clean_required &&
         !framebuffer_plan->framebuffer_cache_cleaned &&
         !framebuffer_plan->framebuffer_error && framebuffer_plan->blit_required &&
         framebuffer_plan->blit_allowed && !framebuffer_plan->blit_submitted &&
         framebuffer_plan->blit_ticket_selected && framebuffer_plan->blit_target_selected &&
         !framebuffer_plan->blit_source_buffer_mapped &&
         !framebuffer_plan->blit_destination_buffer_mapped &&
         !framebuffer_plan->blit_pixels_copied && !framebuffer_plan->blit_dma_allowed &&
         !framebuffer_plan->blit_dma_submitted && framebuffer_plan->output_required &&
         framebuffer_plan->output_allowed && !framebuffer_plan->output_submitted &&
         framebuffer_plan->output_ticket_selected && framebuffer_plan->output_target_selected &&
         !framebuffer_plan->output_buffer_attached &&
         !framebuffer_plan->output_buffer_submitted &&
         !framebuffer_plan->output_flip_allowed && !framebuffer_plan->output_flip_submitted &&
         framebuffer_plan->display_required && framebuffer_plan->display_allowed &&
         !framebuffer_plan->display_submitted && framebuffer_plan->display_ticket_selected &&
         framebuffer_plan->display_target_selected && !framebuffer_plan->display_buffer_attached &&
         !framebuffer_plan->display_buffer_submitted && framebuffer_plan->display_mode_required &&
         !framebuffer_plan->display_mode_committed && !framebuffer_plan->display_flip_allowed &&
         !framebuffer_plan->display_flip_submitted && framebuffer_plan->scanout_required &&
         framebuffer_plan->scanout_allowed && !framebuffer_plan->scanout_submitted &&
         framebuffer_plan->scanout_ticket_selected && framebuffer_plan->scanout_target_selected &&
         !framebuffer_plan->scanout_buffer_attached && !framebuffer_plan->scanout_buffer_submitted &&
         framebuffer_plan->vsync_required && framebuffer_plan->vsync_allowed &&
         !framebuffer_plan->vsync_submitted && framebuffer_plan->vsync_ticket_selected &&
         !framebuffer_plan->vsync_wait_allowed && !framebuffer_plan->vsync_wait_submitted &&
         framebuffer_plan->vsync_fence_required && !framebuffer_plan->vsync_fence_armed &&
         framebuffer_plan->schedule_required && framebuffer_plan->schedule_allowed &&
         !framebuffer_plan->schedule_submitted && framebuffer_plan->schedule_ticket_selected &&
         framebuffer_plan->present_required && framebuffer_plan->present_allowed &&
         !framebuffer_plan->present_submitted && framebuffer_plan->present_ticket_selected &&
         framebuffer_plan->damage_required && framebuffer_plan->damage_allowed &&
         !framebuffer_plan->damage_submitted && framebuffer_plan->compositor_surface_allowed &&
         !framebuffer_plan->compositor_surface_submitted &&
         framebuffer_plan->compositor_damage_planned &&
         framebuffer_plan->compositor_damage_allowed &&
         !framebuffer_plan->compositor_damage_submitted &&
         framebuffer_plan->frame_pacing_required && framebuffer_plan->frame_pacing_allowed &&
         !framebuffer_plan->frame_timer_armed && !framebuffer_plan->compositor_wake_allowed &&
         !framebuffer_plan->compositor_wake_submitted && !framebuffer_plan->page_flip_allowed &&
         !framebuffer_plan->page_flip_submitted && framebuffer_plan->route_selected &&
         !framebuffer_plan->route_blocked && framebuffer_plan->credential_session_safe &&
         framebuffer_plan->credential_storage_wiped && framebuffer_plan->credential_redacted &&
         framebuffer_plan->length_redacted && !framebuffer_plan->raw_secret_exposed &&
         !framebuffer_plan->masked_text_exposed && framebuffer_plan->submit_blocked &&
         !framebuffer_plan->submit_enabled && !framebuffer_plan->auth_attempt_allowed &&
         !framebuffer_plan->submit_callback_bound && !framebuffer_plan->auth_callback_bound &&
         framebuffer_plan->text_login_authoritative;
  if (!safe) {
    out->event_type = "credential-screen-flush-plan-unsafe";
    out->blocked_reason = "credential-flush-plan-unsafe";
    out->message = "Credential screen flush plan unsafe; use text login.";
    return 0;
  }
  out->flush_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = framebuffer_plan->action_allowed ? 1 : 0;
  out->action_blocked = framebuffer_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = framebuffer_plan->input_focus_allowed ? 1 : 0;
  out->compositor_surface_allowed = 1;
  out->compositor_surface_submitted = 0;
  out->compositor_damage_planned = 1;
  out->compositor_damage_allowed = 1;
  out->compositor_damage_submitted = 0;
  out->damage_required = framebuffer_plan->damage_required ? 1 : 0;
  out->damage_allowed = framebuffer_plan->damage_allowed ? 1 : 0;
  out->damage_incremental_allowed = framebuffer_plan->damage_incremental_allowed ? 1 : 0;
  out->full_damage_required = framebuffer_plan->full_damage_required ? 1 : 0;
  out->damage_cache_allowed = framebuffer_plan->damage_cache_allowed ? 1 : 0;
  out->damage_reuse_allowed = framebuffer_plan->damage_reuse_allowed ? 1 : 0;
  out->present_required = 1;
  out->present_allowed = 1;
  out->present_submitted = 0;
  out->present_ticket_selected = 1;
  out->present_incremental_allowed = framebuffer_plan->present_incremental_allowed ? 1 : 0;
  out->full_present_required = framebuffer_plan->full_present_required ? 1 : 0;
  out->present_cache_allowed = framebuffer_plan->present_cache_allowed ? 1 : 0;
  out->present_reuse_allowed = framebuffer_plan->present_reuse_allowed ? 1 : 0;
  out->schedule_required = 1;
  out->schedule_allowed = 1;
  out->schedule_submitted = 0;
  out->schedule_ticket_selected = 1;
  out->schedule_incremental_allowed = framebuffer_plan->schedule_incremental_allowed ? 1 : 0;
  out->full_schedule_required = framebuffer_plan->full_schedule_required ? 1 : 0;
  out->schedule_cache_allowed = framebuffer_plan->schedule_cache_allowed ? 1 : 0;
  out->schedule_reuse_allowed = framebuffer_plan->schedule_reuse_allowed ? 1 : 0;
  out->frame_pacing_allowed = 1;
  out->vsync_allowed = 1;
  out->vsync_submitted = 0;
  out->vsync_ticket_selected = 1;
  out->vsync_wait_allowed = 0;
  out->vsync_wait_submitted = 0;
  out->vsync_fence_required = 1;
  out->vsync_fence_armed = 0;
  out->scanout_allowed = 1;
  out->scanout_submitted = 0;
  out->scanout_ticket_selected = 1;
  out->scanout_target_selected = 1;
  out->scanout_buffer_attached = 0;
  out->scanout_buffer_submitted = 0;
  out->display_allowed = 1;
  out->display_submitted = 0;
  out->display_ticket_selected = 1;
  out->display_target_selected = 1;
  out->display_buffer_attached = 0;
  out->display_buffer_submitted = 0;
  out->display_mode_required = 1;
  out->display_mode_committed = 0;
  out->display_flip_allowed = 0;
  out->display_flip_submitted = 0;
  out->output_allowed = 1;
  out->output_submitted = 0;
  out->output_ticket_selected = 1;
  out->output_target_selected = 1;
  out->output_buffer_attached = 0;
  out->output_buffer_submitted = 0;
  out->output_flip_allowed = 0;
  out->output_flip_submitted = 0;
  out->blit_allowed = 1;
  out->blit_submitted = 0;
  out->blit_ticket_selected = 1;
  out->blit_target_selected = 1;
  out->blit_source_buffer_mapped = 0;
  out->blit_destination_buffer_mapped = 0;
  out->blit_pixels_copied = 0;
  out->blit_dma_allowed = 0;
  out->blit_dma_submitted = 0;
  out->framebuffer_allowed = 1;
  out->framebuffer_submitted = 0;
  out->framebuffer_ticket_selected = 1;
  out->framebuffer_target_selected = 1;
  out->framebuffer_mapped = 0;
  out->framebuffer_write_allowed = 0;
  out->framebuffer_written = 0;
  out->framebuffer_flush_required = 1;
  out->framebuffer_flushed = 0;
  out->framebuffer_cache_clean_required = 1;
  out->framebuffer_cache_cleaned = 0;
  out->flush_allowed = 1;
  out->flush_submitted = 0;
  out->flush_ticket_selected = 1;
  out->flush_target_selected = 1;
  out->flush_cache_clean_required = 1;
  out->flush_cache_clean_allowed = 0;
  out->flush_cache_cleaned = 0;
  out->flush_memory_barrier_allowed = 0;
  out->flush_memory_barrier_submitted = 0;
  out->flush_error = 0;
  out->recovery_text_session_required = framebuffer_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = framebuffer_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = framebuffer_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = framebuffer_plan->submit_requested ? 1 : 0;
  out->text_login_authoritative = 1;
  out->view = framebuffer_plan->view;
  out->widget_tree = framebuffer_plan->widget_tree;
  out->damage_ticket = framebuffer_plan->damage_ticket;
  out->present_ticket = framebuffer_plan->present_ticket;
  out->schedule_ticket = framebuffer_plan->schedule_ticket;
  out->vsync_ticket = framebuffer_plan->vsync_ticket;
  out->scanout_ticket = framebuffer_plan->scanout_ticket;
  out->display_ticket = framebuffer_plan->display_ticket;
  out->output_ticket = framebuffer_plan->output_ticket;
  out->blit_ticket = framebuffer_plan->blit_ticket;
  out->framebuffer_ticket = framebuffer_plan->framebuffer_ticket;
  out->focus_target = framebuffer_plan->focus_target;
  out->primary_action = framebuffer_plan->primary_action;
  out->route = framebuffer_plan->route;
  out->compositor_target = framebuffer_plan->compositor_target;
  out->damage_policy = framebuffer_plan->damage_policy;
  out->cache_policy = framebuffer_plan->cache_policy;
  out->present_policy = framebuffer_plan->present_policy;
  out->schedule_policy = framebuffer_plan->schedule_policy;
  out->vsync_policy = framebuffer_plan->vsync_policy;
  out->scanout_policy = framebuffer_plan->scanout_policy;
  out->display_policy = framebuffer_plan->display_policy;
  out->output_policy = framebuffer_plan->output_policy;
  out->blit_policy = framebuffer_plan->blit_policy;
  out->framebuffer_policy = framebuffer_plan->framebuffer_policy;
  out->flush_policy = out->schedule_incremental_allowed ?
      "incremental-flush-declarative" : "full-flush-declarative";
  out->event_type = "credential-screen-flush-plan-ready";
  out->state = "flush-ready";
  out->message = "Credential screen flush ticket ready; no framebuffer flush performed.";
  out->blocked_reason = framebuffer_plan->blocked_reason;
  if (framebuffer_plan->submit_requested) {
    out->flush_ticket = "text-login-fallback-flush-ticket";
    out->compositor_target = "text-login-fallback-flush";
    out->damage_policy = "fallback-declarative";
    out->present_policy = "fallback-present-declarative";
    out->schedule_policy = "fallback-schedule-declarative";
    out->vsync_policy = "fallback-vsync-declarative";
    out->scanout_policy = "fallback-scanout-declarative";
    out->display_policy = "fallback-display-declarative";
    out->output_policy = "fallback-output-declarative";
    out->blit_policy = "fallback-blit-declarative";
    out->framebuffer_policy = "fallback-framebuffer-declarative";
    out->flush_policy = "fallback-flush-declarative";
    out->flush_text_login = 1;
    out->flush_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "flush-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (framebuffer_plan->framebuffer_credential_panel &&
      framebuffer_plan->framebuffer_credential_input &&
      framebuffer_plan->framebuffer_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->flush_ticket = "credential-screen-flush-ticket";
    out->compositor_target = "credential-screen-flush";
    out->flush_credential_panel = 1;
    out->flush_credential_input = 1;
    out->flush_credential_focus = 1;
    out->flush_text_login = 0;
    out->flush_text_login_fallback = 0;
    out->state = "flush-credential-ready";
    out->message = "Credential flush ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (framebuffer_plan->framebuffer_text_recovery && out->recovery_text_session_required) {
    out->flush_ticket = "text-recovery-flush-ticket";
    out->compositor_target = "text-recovery-flush";
    out->flush_text_recovery = 1;
    out->flush_text_login = 1;
    out->flush_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "flush-text-recovery-ready";
    out->message = "Text recovery flush ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (framebuffer_plan->framebuffer_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->flush_ticket = "text-login-resume-flush-ticket";
    out->compositor_target = "text-login-resume-flush";
    out->damage_policy = "full-rerender-declarative";
    out->cache_policy = "cache-bypassed-for-rerender";
    out->present_policy = "full-present-declarative";
    out->schedule_policy = "full-schedule-declarative";
    out->vsync_policy = "full-vsync-declarative";
    out->scanout_policy = "full-scanout-declarative";
    out->display_policy = "full-display-declarative";
    out->output_policy = "full-output-declarative";
    out->blit_policy = "full-blit-declarative";
    out->framebuffer_policy = "full-framebuffer-declarative";
    out->flush_policy = "full-flush-declarative";
    out->schedule_reuse_allowed = 0;
    out->schedule_cache_allowed = 0;
    out->full_schedule_required = 1;
    out->schedule_incremental_allowed = 0;
    out->flush_text_login = 1;
    out->flush_text_login_resume = 1;
    out->flush_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "flush-resume-ready";
    out->message = "Text login resume flush ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->flush_ticket = "text-login-fallback-flush-ticket";
  out->compositor_target = "text-login-fallback-flush";
  out->damage_policy = "fallback-declarative";
  out->present_policy = "fallback-present-declarative";
  out->schedule_policy = "fallback-schedule-declarative";
  out->vsync_policy = "fallback-vsync-declarative";
  out->scanout_policy = "fallback-scanout-declarative";
  out->display_policy = "fallback-display-declarative";
  out->output_policy = "fallback-output-declarative";
  out->blit_policy = "fallback-blit-declarative";
  out->framebuffer_policy = "fallback-framebuffer-declarative";
  out->flush_policy = "fallback-flush-declarative";
  out->flush_text_login = 1;
  out->flush_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "flush-text-login-ready";
  out->message = "Use authoritative text login.";
  return 0;
}
