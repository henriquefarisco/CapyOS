/*
 * src/auth/login_runtime/window_flip_plan.c
 *
 * Credential-screen window-flip plan reset + builder — extracted
 * byte-for-byte from `src/auth/login_runtime.c` during PR C.60 of
 * the Estagio C dedicated plan.  Hosts the per-plan reset helper
 * and the public builder for the window-flip stage of the
 * credential pipeline:
 *
 *   - login_window_credential_screen_window_flip_plan_reset (static)
 *   - login_window_credential_screen_window_flip_plan_build
 *
 * The window-flip-plan converts a fail-closed window-commit-plan
 * into a window-flip contract for the downstream window-vblank
 * stage.  The static `_reset` helper is the canonical "blocked"
 * initializer used when the upstream contract is missing or unsafe.
 * Both helpers are kept file-local so they cannot be reused outside
 * this translation unit; the public builder is the only entry
 * point.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_window_flip_plan_reset(
    struct login_window_credential_screen_window_flip_plan *out,
    int window_commit_plan_available) {
  *out = (struct login_window_credential_screen_window_flip_plan){0};
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_WINDOW_FLIP_PLAN_VERSION;
  out->window_commit_plan_available = window_commit_plan_available ? 1 : 0;
  out->route_blocked = 1;
  out->action_blocked = 1;
  out->compaction_required = 1;
  out->reclaim_required = 1;
  out->release_required = 1;
  out->gui_required = 1;
  out->window_required = 1;
  out->surface_required = 1;
  out->compositor_required = 1;
  out->damage_required = 1;
  out->full_damage_required = 1;
  out->damage_error = 1;
  out->present_required = 1;
  out->full_present_required = 1;
  out->present_error = 1;
  out->schedule_required = 1;
  out->full_schedule_required = 1;
  out->frame_pacing_required = 1;
  out->schedule_error = 1;
  out->vsync_required = 1;
  out->vsync_fence_required = 1;
  out->vsync_error = 1;
  out->scanout_required = 1;
  out->scanout_error = 1;
  out->display_required = 1;
  out->display_error = 1;
  out->output_required = 1;
  out->output_error = 1;
  out->blit_required = 1;
  out->blit_error = 1;
  out->commit_required = 1;
  out->commit_error = 1;
  out->flip_required = 1;
  out->flip_text_login = 1;
  out->flip_text_login_fallback = 1;
  out->flip_error = 1;
  out->recovery_text_session_required = 1;
  out->session_reset_required = 1;
  out->login_screen_rerender_required = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_blocked = 1;
  out->text_login_authoritative = 1;
  out->view = "text-login-fallback";
  out->widget_tree = "text-login-fallback-bindings";
  out->compaction_ticket = "text-login-fallback-compaction-ticket";
  out->reclaim_ticket = "text-login-fallback-reclaim-ticket";
  out->release_ticket = "text-login-fallback-release-ticket";
  out->gui_ticket = "text-login-fallback-gui-ticket";
  out->window_ticket = "text-login-fallback-window-ticket";
  out->surface_ticket = "text-login-fallback-window-surface-ticket";
  out->compositor_ticket = "text-login-fallback-window-compositor-ticket";
  out->damage_ticket = "text-login-fallback-window-damage-ticket";
  out->present_ticket = "text-login-fallback-window-present-ticket";
  out->schedule_ticket = "text-login-fallback-window-schedule-ticket";
  out->vsync_ticket = "text-login-fallback-window-vsync-ticket";
  out->scanout_ticket = "text-login-fallback-window-scanout-ticket";
  out->display_ticket = "text-login-fallback-window-display-ticket";
  out->output_ticket = "text-login-fallback-window-output-ticket";
  out->blit_ticket = "text-login-fallback-window-blit-ticket";
  out->commit_ticket = "text-login-fallback-window-commit-ticket";
  out->flip_ticket = "text-login-fallback-window-flip-ticket";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->compositor_target = "none";
  out->compaction_policy = "compaction-disabled";
  out->reclaim_policy = "reclaim-disabled";
  out->release_policy = "release-disabled";
  out->gui_policy = "gui-disabled";
  out->window_policy = "window-disabled";
  out->surface_policy = "surface-disabled";
  out->compositor_policy = "compositor-disabled";
  out->damage_policy = "damage-disabled";
  out->cache_policy = "window-flip-cache-disabled";
  out->present_policy = "present-disabled";
  out->schedule_policy = "schedule-disabled";
  out->vsync_policy = "vsync-disabled";
  out->scanout_policy = "scanout-disabled";
  out->display_policy = "display-disabled";
  out->output_policy = "output-disabled";
  out->blit_policy = "blit-disabled";
  out->commit_policy = "commit-disabled";
  out->flip_policy = "flip-disabled";
  out->event_type = "credential-screen-window-flip-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "window-commit-plan-unavailable";
}

int login_window_credential_screen_window_flip_plan_build(
    const struct login_window_credential_screen_window_commit_plan *commit_plan,
    struct login_window_credential_screen_window_flip_plan *out) {
  int safe = 0;
  if (!out) return -1;
  login_window_credential_screen_window_flip_plan_reset(
      out, commit_plan ? 1 : 0);
  if (!commit_plan) return 0;
  out->requested_action = commit_plan->requested_action;
  out->window_blit_plan_available =
      commit_plan->window_blit_plan_available ? 1 : 0;
  out->window_output_plan_available =
      commit_plan->window_output_plan_available ? 1 : 0;
  out->window_display_plan_available =
      commit_plan->window_display_plan_available ? 1 : 0;
  out->window_scanout_plan_available =
      commit_plan->window_scanout_plan_available ? 1 : 0;
  out->window_vsync_plan_available =
      commit_plan->window_vsync_plan_available ? 1 : 0;
  out->window_schedule_plan_available =
      commit_plan->window_schedule_plan_available ? 1 : 0;
  out->window_present_plan_available =
      commit_plan->window_present_plan_available ? 1 : 0;
  out->window_damage_plan_available =
      commit_plan->window_damage_plan_available ? 1 : 0;
  out->window_compositor_plan_available =
      commit_plan->window_compositor_plan_available ? 1 : 0;
  out->window_surface_plan_available =
      commit_plan->window_surface_plan_available ? 1 : 0;
  out->window_surface_plan_safe =
      commit_plan->window_surface_plan_safe ? 1 : 0;
  out->window_compositor_plan_safe =
      commit_plan->window_compositor_plan_safe ? 1 : 0;
  out->window_damage_plan_safe =
      commit_plan->window_damage_plan_safe ? 1 : 0;
  out->window_present_plan_safe =
      commit_plan->window_present_plan_safe ? 1 : 0;
  out->window_schedule_plan_safe =
      commit_plan->window_schedule_plan_safe ? 1 : 0;
  out->window_vsync_plan_safe =
      commit_plan->window_vsync_plan_safe ? 1 : 0;
  out->window_scanout_plan_safe =
      commit_plan->window_scanout_plan_safe ? 1 : 0;
  out->window_display_plan_safe =
      commit_plan->window_display_plan_safe ? 1 : 0;
  out->window_output_plan_safe =
      commit_plan->window_output_plan_safe ? 1 : 0;
  out->window_blit_plan_safe =
      commit_plan->window_blit_plan_safe ? 1 : 0;
  out->window_commit_plan_safe =
      commit_plan->window_commit_plan_safe ? 1 : 0;
  safe = commit_plan->window_commit_plan_safe &&
         commit_plan->window_blit_plan_safe &&
         commit_plan->window_output_plan_safe &&
         commit_plan->window_display_plan_safe &&
         commit_plan->window_scanout_plan_safe &&
         commit_plan->window_vsync_plan_safe &&
         commit_plan->window_schedule_plan_safe &&
         commit_plan->window_present_plan_safe &&
         commit_plan->window_damage_plan_safe &&
         commit_plan->window_compositor_plan_safe &&
         commit_plan->window_surface_plan_safe &&
         commit_plan->window_blit_plan_available &&
         commit_plan->window_output_plan_available &&
         commit_plan->window_display_plan_available &&
         commit_plan->window_scanout_plan_available &&
         commit_plan->window_vsync_plan_available &&
         commit_plan->window_schedule_plan_available &&
         commit_plan->window_present_plan_available &&
         commit_plan->window_damage_plan_available &&
         commit_plan->window_compositor_plan_available &&
         commit_plan->window_surface_plan_available &&
         commit_plan->commit_required && commit_plan->commit_allowed &&
         !commit_plan->commit_submitted &&
         commit_plan->commit_ticket_selected &&
         commit_plan->commit_target_selected &&
         !commit_plan->commit_state_attached &&
         !commit_plan->commit_state_submitted &&
         !commit_plan->commit_atomic_allowed &&
         !commit_plan->commit_atomic_submitted &&
         !commit_plan->commit_callback_armed &&
         !commit_plan->commit_callback_submitted &&
         !commit_plan->commit_error &&
         commit_plan->blit_required && commit_plan->blit_allowed &&
         !commit_plan->blit_submitted &&
         commit_plan->blit_ticket_selected &&
         commit_plan->blit_target_selected &&
         !commit_plan->blit_source_buffer_mapped &&
         !commit_plan->blit_destination_buffer_mapped &&
         !commit_plan->blit_pixels_copied &&
         !commit_plan->blit_dma_allowed &&
         !commit_plan->blit_dma_submitted &&
         !commit_plan->blit_error &&
         commit_plan->output_required && commit_plan->output_allowed &&
         !commit_plan->output_submitted &&
         commit_plan->output_ticket_selected &&
         commit_plan->output_target_selected &&
         !commit_plan->output_connector_attached &&
         !commit_plan->output_connector_submitted &&
         !commit_plan->output_mode_attached &&
         !commit_plan->output_mode_submitted &&
         !commit_plan->output_signal_armed &&
         !commit_plan->output_signal_submitted &&
         !commit_plan->output_error &&
         commit_plan->display_required && commit_plan->display_allowed &&
         !commit_plan->display_submitted &&
         commit_plan->display_ticket_selected &&
         commit_plan->display_target_selected &&
         !commit_plan->display_controller_attached &&
         !commit_plan->display_controller_submitted &&
         !commit_plan->display_output_attached &&
         !commit_plan->display_output_submitted &&
         !commit_plan->display_pipeline_attached &&
         !commit_plan->display_pipeline_submitted &&
         !commit_plan->display_error &&
         commit_plan->scanout_required && commit_plan->scanout_allowed &&
         !commit_plan->scanout_submitted &&
         commit_plan->scanout_ticket_selected &&
         commit_plan->scanout_target_selected &&
         !commit_plan->scanout_buffer_attached &&
         !commit_plan->scanout_buffer_submitted &&
         !commit_plan->scanout_display_flip_allowed &&
         !commit_plan->scanout_display_flip_submitted &&
         !commit_plan->scanout_error &&
         commit_plan->vsync_required && commit_plan->vsync_allowed &&
         !commit_plan->vsync_submitted &&
         commit_plan->vsync_ticket_selected &&
         commit_plan->vsync_target_selected &&
         !commit_plan->vsync_wait_allowed &&
         !commit_plan->vsync_wait_submitted &&
         commit_plan->vsync_fence_required &&
         !commit_plan->vsync_fence_armed && !commit_plan->vsync_error &&
         commit_plan->schedule_required && commit_plan->schedule_allowed &&
         !commit_plan->schedule_submitted &&
         commit_plan->schedule_ticket_selected &&
         commit_plan->schedule_target_selected &&
         !commit_plan->schedule_cache_hit &&
         !commit_plan->schedule_error &&
         commit_plan->frame_pacing_required &&
         commit_plan->frame_pacing_allowed &&
         !commit_plan->frame_timer_armed &&
         !commit_plan->compositor_wake_allowed &&
         !commit_plan->compositor_wake_submitted &&
         !commit_plan->page_flip_allowed &&
         !commit_plan->page_flip_submitted &&
         commit_plan->present_required && commit_plan->present_allowed &&
         !commit_plan->present_submitted &&
         commit_plan->present_ticket_selected &&
         commit_plan->present_target_selected &&
         !commit_plan->present_cache_hit &&
         !commit_plan->present_error && commit_plan->damage_required &&
         commit_plan->damage_allowed && !commit_plan->damage_submitted &&
         commit_plan->damage_ticket_selected &&
         commit_plan->damage_target_selected &&
         !commit_plan->damage_cache_hit &&
         !commit_plan->damage_error &&
         commit_plan->compositor_required &&
         commit_plan->compositor_allowed &&
         !commit_plan->compositor_submitted &&
         commit_plan->compositor_ticket_selected &&
         commit_plan->compositor_target_selected &&
         commit_plan->compositor_surface_allowed &&
         !commit_plan->compositor_surface_submitted &&
         commit_plan->compositor_damage_planned &&
         commit_plan->compositor_damage_allowed &&
         !commit_plan->compositor_damage_submitted &&
         commit_plan->surface_required && commit_plan->surface_allowed &&
         !commit_plan->surface_bound &&
         commit_plan->surface_ticket_selected &&
         commit_plan->surface_target_selected &&
         !commit_plan->surface_memory_mapped &&
         !commit_plan->surface_pixels_written &&
         commit_plan->window_required && commit_plan->window_allowed &&
         !commit_plan->window_created &&
         commit_plan->window_ticket_selected &&
         commit_plan->window_target_selected &&
         !commit_plan->window_surface_bound &&
         !commit_plan->window_input_bound &&
         commit_plan->gui_required && commit_plan->gui_allowed &&
         !commit_plan->gui_submitted &&
         commit_plan->gui_ticket_selected &&
         commit_plan->gui_target_selected &&
         commit_plan->release_required && commit_plan->release_allowed &&
         !commit_plan->release_submitted &&
         commit_plan->release_ticket_selected &&
         commit_plan->release_target_selected &&
         commit_plan->reclaim_required && commit_plan->reclaim_allowed &&
         !commit_plan->reclaim_submitted &&
         commit_plan->reclaim_ticket_selected &&
         commit_plan->reclaim_target_selected &&
         commit_plan->compaction_required &&
         commit_plan->compaction_allowed &&
         !commit_plan->compaction_submitted &&
         commit_plan->compaction_ticket_selected &&
         commit_plan->compaction_target_selected &&
         commit_plan->route_selected && !commit_plan->route_blocked &&
         commit_plan->credential_session_safe &&
         commit_plan->credential_storage_wiped &&
         commit_plan->credential_redacted && commit_plan->length_redacted &&
         !commit_plan->raw_secret_exposed &&
         !commit_plan->masked_text_exposed && commit_plan->submit_blocked &&
         !commit_plan->submit_enabled && !commit_plan->auth_attempt_allowed &&
         !commit_plan->submit_callback_bound &&
         !commit_plan->auth_callback_bound &&
         commit_plan->text_login_authoritative;
  if (!safe) {
    out->event_type = "credential-screen-window-flip-plan-unsafe";
    out->blocked_reason = "credential-window-flip-plan-unsafe";
    out->message =
        "Credential screen window flip plan unsafe; use text login.";
    return 0;
  }
  out->window_flip_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = commit_plan->action_allowed ? 1 : 0;
  out->action_blocked = commit_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = commit_plan->input_focus_allowed ? 1 : 0;
  out->compaction_allowed = commit_plan->compaction_allowed ? 1 : 0;
  out->compaction_ticket_selected =
      commit_plan->compaction_ticket_selected ? 1 : 0;
  out->compaction_target_selected =
      commit_plan->compaction_target_selected ? 1 : 0;
  out->reclaim_allowed = commit_plan->reclaim_allowed ? 1 : 0;
  out->reclaim_ticket_selected = commit_plan->reclaim_ticket_selected ? 1 : 0;
  out->reclaim_target_selected = commit_plan->reclaim_target_selected ? 1 : 0;
  out->release_allowed = commit_plan->release_allowed ? 1 : 0;
  out->release_ticket_selected = commit_plan->release_ticket_selected ? 1 : 0;
  out->release_target_selected = commit_plan->release_target_selected ? 1 : 0;
  out->gui_allowed = commit_plan->gui_allowed ? 1 : 0;
  out->gui_ticket_selected = commit_plan->gui_ticket_selected ? 1 : 0;
  out->gui_target_selected = commit_plan->gui_target_selected ? 1 : 0;
  out->window_allowed = commit_plan->window_allowed ? 1 : 0;
  out->window_ticket_selected = commit_plan->window_ticket_selected ? 1 : 0;
  out->window_target_selected = commit_plan->window_target_selected ? 1 : 0;
  out->surface_allowed = commit_plan->surface_allowed ? 1 : 0;
  out->surface_ticket_selected = commit_plan->surface_ticket_selected ? 1 : 0;
  out->surface_target_selected = commit_plan->surface_target_selected ? 1 : 0;
  out->compositor_allowed = commit_plan->compositor_allowed ? 1 : 0;
  out->compositor_ticket_selected =
      commit_plan->compositor_ticket_selected ? 1 : 0;
  out->compositor_target_selected =
      commit_plan->compositor_target_selected ? 1 : 0;
  out->compositor_surface_allowed =
      commit_plan->compositor_surface_allowed ? 1 : 0;
  out->compositor_damage_planned =
      commit_plan->compositor_damage_planned ? 1 : 0;
  out->compositor_damage_allowed =
      commit_plan->compositor_damage_allowed ? 1 : 0;
  out->damage_allowed = commit_plan->damage_allowed ? 1 : 0;
  out->damage_ticket_selected = commit_plan->damage_ticket_selected ? 1 : 0;
  out->damage_target_selected = commit_plan->damage_target_selected ? 1 : 0;
  out->damage_incremental_allowed =
      commit_plan->damage_incremental_allowed ? 1 : 0;
  out->full_damage_required = commit_plan->full_damage_required ? 1 : 0;
  out->damage_cache_allowed = commit_plan->damage_cache_allowed ? 1 : 0;
  out->damage_reuse_allowed = commit_plan->damage_reuse_allowed ? 1 : 0;
  out->present_allowed = commit_plan->present_allowed ? 1 : 0;
  out->present_ticket_selected = commit_plan->present_ticket_selected ? 1 : 0;
  out->present_target_selected = commit_plan->present_target_selected ? 1 : 0;
  out->present_incremental_allowed =
      commit_plan->present_incremental_allowed ? 1 : 0;
  out->full_present_required = commit_plan->full_present_required ? 1 : 0;
  out->present_cache_allowed = commit_plan->present_cache_allowed ? 1 : 0;
  out->present_reuse_allowed = commit_plan->present_reuse_allowed ? 1 : 0;
  out->schedule_allowed = commit_plan->schedule_allowed ? 1 : 0;
  out->schedule_ticket_selected =
      commit_plan->schedule_ticket_selected ? 1 : 0;
  out->schedule_target_selected =
      commit_plan->schedule_target_selected ? 1 : 0;
  out->schedule_incremental_allowed =
      commit_plan->schedule_incremental_allowed ? 1 : 0;
  out->full_schedule_required = commit_plan->full_schedule_required ? 1 : 0;
  out->schedule_cache_allowed = commit_plan->schedule_cache_allowed ? 1 : 0;
  out->schedule_reuse_allowed = commit_plan->schedule_reuse_allowed ? 1 : 0;
  out->frame_pacing_allowed = 1;
  out->vsync_allowed = commit_plan->vsync_allowed ? 1 : 0;
  out->vsync_ticket_selected = commit_plan->vsync_ticket_selected ? 1 : 0;
  out->vsync_target_selected = commit_plan->vsync_target_selected ? 1 : 0;
  out->scanout_allowed = commit_plan->scanout_allowed ? 1 : 0;
  out->scanout_ticket_selected = commit_plan->scanout_ticket_selected ? 1 : 0;
  out->scanout_target_selected = commit_plan->scanout_target_selected ? 1 : 0;
  out->display_allowed = commit_plan->display_allowed ? 1 : 0;
  out->display_ticket_selected = commit_plan->display_ticket_selected ? 1 : 0;
  out->display_target_selected = commit_plan->display_target_selected ? 1 : 0;
  out->output_allowed = commit_plan->output_allowed ? 1 : 0;
  out->output_ticket_selected = commit_plan->output_ticket_selected ? 1 : 0;
  out->output_target_selected = commit_plan->output_target_selected ? 1 : 0;
  out->blit_allowed = commit_plan->blit_allowed ? 1 : 0;
  out->blit_ticket_selected = commit_plan->blit_ticket_selected ? 1 : 0;
  out->blit_target_selected = commit_plan->blit_target_selected ? 1 : 0;
  out->commit_allowed = commit_plan->commit_allowed ? 1 : 0;
  out->commit_ticket_selected = commit_plan->commit_ticket_selected ? 1 : 0;
  out->commit_target_selected = commit_plan->commit_target_selected ? 1 : 0;
  out->flip_allowed = 1;
  out->flip_ticket_selected = 1;
  out->flip_target_selected = 1;
  out->damage_error = 0;
  out->present_error = 0;
  out->schedule_error = 0;
  out->vsync_error = 0;
  out->scanout_error = 0;
  out->display_error = 0;
  out->output_error = 0;
  out->blit_error = 0;
  out->commit_error = 0;
  out->flip_error = 0;
  out->recovery_text_session_required =
      commit_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = commit_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required =
      commit_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = commit_plan->submit_requested ? 1 : 0;
  out->submit_blocked = 1;
  out->text_login_authoritative = 1;
  out->view = commit_plan->view;
  out->widget_tree = commit_plan->widget_tree;
  out->compaction_ticket = commit_plan->compaction_ticket;
  out->reclaim_ticket = commit_plan->reclaim_ticket;
  out->release_ticket = commit_plan->release_ticket;
  out->gui_ticket = commit_plan->gui_ticket;
  out->window_ticket = commit_plan->window_ticket;
  out->surface_ticket = commit_plan->surface_ticket;
  out->compositor_ticket = commit_plan->compositor_ticket;
  out->damage_ticket = commit_plan->damage_ticket;
  out->present_ticket = commit_plan->present_ticket;
  out->schedule_ticket = commit_plan->schedule_ticket;
  out->vsync_ticket = commit_plan->vsync_ticket;
  out->scanout_ticket = commit_plan->scanout_ticket;
  out->display_ticket = commit_plan->display_ticket;
  out->output_ticket = commit_plan->output_ticket;
  out->blit_ticket = commit_plan->blit_ticket;
  out->commit_ticket = commit_plan->commit_ticket;
  out->focus_target = commit_plan->focus_target;
  out->primary_action = commit_plan->primary_action;
  out->route = commit_plan->route;
  out->compositor_target = commit_plan->compositor_target;
  out->compaction_policy = commit_plan->compaction_policy;
  out->reclaim_policy = commit_plan->reclaim_policy;
  out->release_policy = commit_plan->release_policy;
  out->gui_policy = commit_plan->gui_policy;
  out->window_policy = commit_plan->window_policy;
  out->surface_policy = commit_plan->surface_policy;
  out->compositor_policy = commit_plan->compositor_policy;
  out->damage_policy = commit_plan->damage_policy;
  out->cache_policy = commit_plan->cache_policy;
  out->present_policy = commit_plan->present_policy;
  out->schedule_policy = commit_plan->schedule_policy;
  out->vsync_policy = commit_plan->vsync_policy;
  out->scanout_policy = commit_plan->scanout_policy;
  out->display_policy = commit_plan->display_policy;
  out->output_policy = commit_plan->output_policy;
  out->blit_policy = commit_plan->blit_policy;
  out->commit_policy = commit_plan->commit_policy;
  out->flip_policy = out->schedule_incremental_allowed
                          ? "incremental-window-flip-declarative"
                          : "full-window-flip-declarative";
  out->event_type = "credential-screen-window-flip-plan-ready";
  out->state = "window-flip-ready";
  out->message =
      "Credential screen window flip ticket ready; no buffer attached, vblank armed or async flip submitted.";
  out->blocked_reason = commit_plan->blocked_reason;
  if (commit_plan->submit_requested) {
    out->flip_ticket = "text-login-fallback-window-flip-ticket";
    out->compositor_target = "text-login-fallback-window-flip";
    out->flip_policy = "fallback-window-flip-declarative";
    out->flip_text_login = 1;
    out->flip_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "window-flip-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (commit_plan->commit_credential_panel &&
      commit_plan->commit_credential_input &&
      commit_plan->commit_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->flip_ticket = "credential-screen-window-flip-ticket";
    out->compositor_target = "credential-screen-window-flip";
    out->flip_credential_panel = 1;
    out->flip_credential_input = 1;
    out->flip_credential_focus = 1;
    out->flip_text_login = 0;
    out->flip_text_login_fallback = 0;
    out->state = "window-flip-credential-ready";
    out->message =
        "Credential window flip ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (commit_plan->commit_text_recovery &&
      out->recovery_text_session_required) {
    out->flip_ticket = "text-recovery-window-flip-ticket";
    out->compositor_target = "text-recovery-window-flip";
    out->flip_text_recovery = 1;
    out->flip_text_login = 1;
    out->flip_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "window-flip-text-recovery-ready";
    out->message =
        "Text recovery window flip ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (commit_plan->commit_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->flip_ticket = "text-login-resume-window-flip-ticket";
    out->compositor_target = "text-login-resume-window-flip";
    out->flip_policy = "full-window-flip-declarative";
    out->cache_policy = "window-flip-cache-bypassed-for-rerender";
    out->flip_text_login = 1;
    out->flip_text_login_resume = 1;
    out->flip_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "window-flip-resume-ready";
    out->message =
        "Text login resume window flip ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->flip_ticket = "text-login-fallback-window-flip-ticket";
  out->compositor_target = "text-login-fallback-window-flip";
  out->flip_policy = "fallback-window-flip-declarative";
  out->flip_text_login = 1;
  out->flip_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "window-flip-text-login-ready";
  out->message = "Use authoritative text login.";
  out->blocked_reason = "ready";
  return 0;
}
