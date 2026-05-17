/*
 * src/auth/login_runtime/framebuffer_plan.c
 *
 * Credential-screen framebuffer plan reset + builder — extracted
 * byte-for-byte from `src/auth/login_runtime.c` during PR C.22 of
 * the Estagio C dedicated plan.  Hosts the per-plan reset helper
 * and the public builder for the framebuffer stage of the
 * credential pipeline:
 *
 *   - login_window_credential_screen_framebuffer_plan_reset (static)
 *   - login_window_credential_screen_framebuffer_plan_build
 *
 * The framebuffer-plan converts a fail-closed blit-plan into a
 * framebuffer contract for the downstream flush stage.  The static
 * `_reset` helper is the canonical "blocked" initializer shared
 * between the build path and any future internal callers.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_framebuffer_plan_reset(
    struct login_window_credential_screen_framebuffer_plan *out,
    int blit_plan_available) {
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_FRAMEBUFFER_PLAN_VERSION;
  out->blit_plan_available = blit_plan_available ? 1 : 0;
  out->blit_plan_safe = 0;
  out->framebuffer_plan_safe = 0;
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
  out->framebuffer_credential_panel = 0;
  out->framebuffer_credential_input = 0;
  out->framebuffer_credential_focus = 0;
  out->framebuffer_text_recovery = 0;
  out->framebuffer_text_login = 1;
  out->framebuffer_text_login_resume = 0;
  out->framebuffer_text_login_fallback = 1;
  out->framebuffer_error = 1;
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
  out->event_type = "credential-screen-framebuffer-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "blit-plan-unavailable";
}

int login_window_credential_screen_framebuffer_plan_build(
    const struct login_window_credential_screen_blit_plan *blit_plan,
    struct login_window_credential_screen_framebuffer_plan *out) {
  int safe = 0;
  if (!out) return -1;
  login_window_credential_screen_framebuffer_plan_reset(out, blit_plan ? 1 : 0);
  if (!blit_plan) return 0;
  out->requested_action = blit_plan->requested_action;
  out->blit_plan_safe = blit_plan->blit_plan_safe ? 1 : 0;
  safe = blit_plan->blit_plan_safe && blit_plan->blit_required &&
         blit_plan->blit_allowed && !blit_plan->blit_submitted &&
         blit_plan->blit_ticket_selected && blit_plan->blit_target_selected &&
         !blit_plan->blit_source_buffer_mapped &&
         !blit_plan->blit_destination_buffer_mapped &&
         !blit_plan->blit_pixels_copied && !blit_plan->blit_dma_allowed &&
         !blit_plan->blit_dma_submitted && !blit_plan->blit_error &&
         blit_plan->output_required && blit_plan->output_allowed &&
         !blit_plan->output_submitted && blit_plan->output_ticket_selected &&
         blit_plan->output_target_selected && !blit_plan->output_buffer_attached &&
         !blit_plan->output_buffer_submitted && !blit_plan->output_flip_allowed &&
         !blit_plan->output_flip_submitted && blit_plan->display_required &&
         blit_plan->display_allowed && !blit_plan->display_submitted &&
         blit_plan->display_ticket_selected && blit_plan->display_target_selected &&
         !blit_plan->display_buffer_attached && !blit_plan->display_buffer_submitted &&
         blit_plan->display_mode_required && !blit_plan->display_mode_committed &&
         !blit_plan->display_flip_allowed && !blit_plan->display_flip_submitted &&
         blit_plan->scanout_required && blit_plan->scanout_allowed &&
         !blit_plan->scanout_submitted && blit_plan->scanout_ticket_selected &&
         blit_plan->scanout_target_selected && !blit_plan->scanout_buffer_attached &&
         !blit_plan->scanout_buffer_submitted && blit_plan->vsync_required &&
         blit_plan->vsync_allowed && !blit_plan->vsync_submitted &&
         blit_plan->vsync_ticket_selected && !blit_plan->vsync_wait_allowed &&
         !blit_plan->vsync_wait_submitted && blit_plan->vsync_fence_required &&
         !blit_plan->vsync_fence_armed && blit_plan->schedule_required &&
         blit_plan->schedule_allowed && !blit_plan->schedule_submitted &&
         blit_plan->schedule_ticket_selected && blit_plan->present_required &&
         blit_plan->present_allowed && !blit_plan->present_submitted &&
         blit_plan->present_ticket_selected && blit_plan->damage_required &&
         blit_plan->damage_allowed && !blit_plan->damage_submitted &&
         blit_plan->compositor_surface_allowed &&
         !blit_plan->compositor_surface_submitted &&
         blit_plan->compositor_damage_planned &&
         blit_plan->compositor_damage_allowed &&
         !blit_plan->compositor_damage_submitted &&
         blit_plan->frame_pacing_required && blit_plan->frame_pacing_allowed &&
         !blit_plan->frame_timer_armed && !blit_plan->compositor_wake_allowed &&
         !blit_plan->compositor_wake_submitted && !blit_plan->page_flip_allowed &&
         !blit_plan->page_flip_submitted && blit_plan->route_selected &&
         !blit_plan->route_blocked && blit_plan->credential_session_safe &&
         blit_plan->credential_storage_wiped && blit_plan->credential_redacted &&
         blit_plan->length_redacted && !blit_plan->raw_secret_exposed &&
         !blit_plan->masked_text_exposed && blit_plan->submit_blocked &&
         !blit_plan->submit_enabled && !blit_plan->auth_attempt_allowed &&
         !blit_plan->submit_callback_bound && !blit_plan->auth_callback_bound &&
         blit_plan->text_login_authoritative;
  if (!safe) {
    out->event_type = "credential-screen-framebuffer-plan-unsafe";
    out->blocked_reason = "credential-framebuffer-plan-unsafe";
    out->message = "Credential screen framebuffer plan unsafe; use text login.";
    return 0;
  }
  out->framebuffer_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = blit_plan->action_allowed ? 1 : 0;
  out->action_blocked = blit_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = blit_plan->input_focus_allowed ? 1 : 0;
  out->compositor_surface_allowed = 1;
  out->compositor_surface_submitted = 0;
  out->compositor_damage_planned = 1;
  out->compositor_damage_allowed = 1;
  out->compositor_damage_submitted = 0;
  out->damage_required = blit_plan->damage_required ? 1 : 0;
  out->damage_allowed = blit_plan->damage_allowed ? 1 : 0;
  out->damage_incremental_allowed = blit_plan->damage_incremental_allowed ? 1 : 0;
  out->full_damage_required = blit_plan->full_damage_required ? 1 : 0;
  out->damage_cache_allowed = blit_plan->damage_cache_allowed ? 1 : 0;
  out->damage_reuse_allowed = blit_plan->damage_reuse_allowed ? 1 : 0;
  out->present_required = 1;
  out->present_allowed = 1;
  out->present_submitted = 0;
  out->present_ticket_selected = 1;
  out->present_incremental_allowed = blit_plan->present_incremental_allowed ? 1 : 0;
  out->full_present_required = blit_plan->full_present_required ? 1 : 0;
  out->present_cache_allowed = blit_plan->present_cache_allowed ? 1 : 0;
  out->present_reuse_allowed = blit_plan->present_reuse_allowed ? 1 : 0;
  out->schedule_required = 1;
  out->schedule_allowed = 1;
  out->schedule_submitted = 0;
  out->schedule_ticket_selected = 1;
  out->schedule_incremental_allowed = blit_plan->schedule_incremental_allowed ? 1 : 0;
  out->full_schedule_required = blit_plan->full_schedule_required ? 1 : 0;
  out->schedule_cache_allowed = blit_plan->schedule_cache_allowed ? 1 : 0;
  out->schedule_reuse_allowed = blit_plan->schedule_reuse_allowed ? 1 : 0;
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
  out->framebuffer_error = 0;
  out->recovery_text_session_required = blit_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = blit_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = blit_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = blit_plan->submit_requested ? 1 : 0;
  out->text_login_authoritative = 1;
  out->view = blit_plan->view;
  out->widget_tree = blit_plan->widget_tree;
  out->damage_ticket = blit_plan->damage_ticket;
  out->present_ticket = blit_plan->present_ticket;
  out->schedule_ticket = blit_plan->schedule_ticket;
  out->vsync_ticket = blit_plan->vsync_ticket;
  out->scanout_ticket = blit_plan->scanout_ticket;
  out->display_ticket = blit_plan->display_ticket;
  out->output_ticket = blit_plan->output_ticket;
  out->blit_ticket = blit_plan->blit_ticket;
  out->focus_target = blit_plan->focus_target;
  out->primary_action = blit_plan->primary_action;
  out->route = blit_plan->route;
  out->compositor_target = blit_plan->compositor_target;
  out->damage_policy = blit_plan->damage_policy;
  out->cache_policy = blit_plan->cache_policy;
  out->present_policy = blit_plan->present_policy;
  out->schedule_policy = blit_plan->schedule_policy;
  out->vsync_policy = blit_plan->vsync_policy;
  out->scanout_policy = blit_plan->scanout_policy;
  out->display_policy = blit_plan->display_policy;
  out->output_policy = blit_plan->output_policy;
  out->blit_policy = blit_plan->blit_policy;
  out->framebuffer_policy = out->schedule_incremental_allowed ?
      "incremental-framebuffer-declarative" : "full-framebuffer-declarative";
  out->event_type = "credential-screen-framebuffer-plan-ready";
  out->state = "framebuffer-ready";
  out->message = "Credential screen framebuffer ticket ready; no framebuffer mapped.";
  out->blocked_reason = blit_plan->blocked_reason;
  if (blit_plan->submit_requested) {
    out->framebuffer_ticket = "text-login-fallback-framebuffer-ticket";
    out->compositor_target = "text-login-fallback-framebuffer";
    out->damage_policy = "fallback-declarative";
    out->present_policy = "fallback-present-declarative";
    out->schedule_policy = "fallback-schedule-declarative";
    out->vsync_policy = "fallback-vsync-declarative";
    out->scanout_policy = "fallback-scanout-declarative";
    out->display_policy = "fallback-display-declarative";
    out->output_policy = "fallback-output-declarative";
    out->blit_policy = "fallback-blit-declarative";
    out->framebuffer_policy = "fallback-framebuffer-declarative";
    out->framebuffer_text_login = 1;
    out->framebuffer_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "framebuffer-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (blit_plan->blit_credential_panel && blit_plan->blit_credential_input &&
      blit_plan->blit_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->framebuffer_ticket = "credential-screen-framebuffer-ticket";
    out->compositor_target = "credential-screen-framebuffer";
    out->framebuffer_credential_panel = 1;
    out->framebuffer_credential_input = 1;
    out->framebuffer_credential_focus = 1;
    out->framebuffer_text_login = 0;
    out->framebuffer_text_login_fallback = 0;
    out->state = "framebuffer-credential-ready";
    out->message = "Credential framebuffer ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (blit_plan->blit_text_recovery && out->recovery_text_session_required) {
    out->framebuffer_ticket = "text-recovery-framebuffer-ticket";
    out->compositor_target = "text-recovery-framebuffer";
    out->framebuffer_text_recovery = 1;
    out->framebuffer_text_login = 1;
    out->framebuffer_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "framebuffer-text-recovery-ready";
    out->message = "Text recovery framebuffer ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (blit_plan->blit_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->framebuffer_ticket = "text-login-resume-framebuffer-ticket";
    out->compositor_target = "text-login-resume-framebuffer";
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
    out->schedule_reuse_allowed = 0;
    out->schedule_cache_allowed = 0;
    out->full_schedule_required = 1;
    out->schedule_incremental_allowed = 0;
    out->framebuffer_text_login = 1;
    out->framebuffer_text_login_resume = 1;
    out->framebuffer_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "framebuffer-resume-ready";
    out->message = "Text login resume framebuffer ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->framebuffer_ticket = "text-login-fallback-framebuffer-ticket";
  out->compositor_target = "text-login-fallback-framebuffer";
  out->damage_policy = "fallback-declarative";
  out->present_policy = "fallback-present-declarative";
  out->schedule_policy = "fallback-schedule-declarative";
  out->vsync_policy = "fallback-vsync-declarative";
  out->scanout_policy = "fallback-scanout-declarative";
  out->display_policy = "fallback-display-declarative";
  out->output_policy = "fallback-output-declarative";
  out->blit_policy = "fallback-blit-declarative";
  out->framebuffer_policy = "fallback-framebuffer-declarative";
  out->framebuffer_text_login = 1;
  out->framebuffer_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "framebuffer-text-login-ready";
  out->message = "Use authoritative text login.";
  return 0;
}
