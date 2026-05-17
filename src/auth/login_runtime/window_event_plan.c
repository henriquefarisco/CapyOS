/*
 * src/auth/login_runtime/window_event_plan.c
 *
 * Credential-screen window-event plan reset + builder — extracted
 * byte-for-byte from `src/auth/login_runtime.c` during PR C.62 of
 * the Estagio C dedicated plan.  Hosts the per-plan reset helper
 * and the public builder for the window-event stage of the
 * credential pipeline:
 *
 *   - login_window_credential_screen_window_event_plan_reset (static)
 *   - login_window_credential_screen_window_event_plan_build
 *
 * The window-event-plan converts a fail-closed window-vblank-plan
 * into a window-event contract for the downstream window-input
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

static void login_window_credential_screen_window_event_plan_reset(
    struct login_window_credential_screen_window_event_plan *out,
    int window_vblank_plan_available) {
  *out = (struct login_window_credential_screen_window_event_plan){0};
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_WINDOW_EVENT_PLAN_VERSION;
  out->window_vblank_plan_available = window_vblank_plan_available ? 1 : 0;
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
  out->flip_error = 1;
  out->vblank_required = 1;
  out->vblank_error = 1;
  out->event_required = 1;
  out->event_text_login = 1;
  out->event_text_login_fallback = 1;
  out->event_error = 1;
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
  out->vblank_ticket = "text-login-fallback-window-vblank-ticket";
  out->event_ticket = "text-login-fallback-window-event-ticket";
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
  out->cache_policy = "window-event-cache-disabled";
  out->present_policy = "present-disabled";
  out->schedule_policy = "schedule-disabled";
  out->vsync_policy = "vsync-disabled";
  out->scanout_policy = "scanout-disabled";
  out->display_policy = "display-disabled";
  out->output_policy = "output-disabled";
  out->blit_policy = "blit-disabled";
  out->commit_policy = "commit-disabled";
  out->flip_policy = "flip-disabled";
  out->vblank_policy = "vblank-disabled";
  out->event_policy = "event-disabled";
  out->event_type = "credential-screen-window-event-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "window-vblank-plan-unavailable";
}

int login_window_credential_screen_window_event_plan_build(
    const struct login_window_credential_screen_window_vblank_plan *vblank_plan,
    struct login_window_credential_screen_window_event_plan *out) {
  int safe = 0;
  if (!out) return -1;
  login_window_credential_screen_window_event_plan_reset(
      out, vblank_plan ? 1 : 0);
  if (!vblank_plan) return 0;
  out->requested_action = vblank_plan->requested_action;
  out->window_flip_plan_available =
      vblank_plan->window_flip_plan_available ? 1 : 0;
  out->window_commit_plan_available =
      vblank_plan->window_commit_plan_available ? 1 : 0;
  out->window_blit_plan_available =
      vblank_plan->window_blit_plan_available ? 1 : 0;
  out->window_output_plan_available =
      vblank_plan->window_output_plan_available ? 1 : 0;
  out->window_display_plan_available =
      vblank_plan->window_display_plan_available ? 1 : 0;
  out->window_scanout_plan_available =
      vblank_plan->window_scanout_plan_available ? 1 : 0;
  out->window_vsync_plan_available =
      vblank_plan->window_vsync_plan_available ? 1 : 0;
  out->window_schedule_plan_available =
      vblank_plan->window_schedule_plan_available ? 1 : 0;
  out->window_present_plan_available =
      vblank_plan->window_present_plan_available ? 1 : 0;
  out->window_damage_plan_available =
      vblank_plan->window_damage_plan_available ? 1 : 0;
  out->window_compositor_plan_available =
      vblank_plan->window_compositor_plan_available ? 1 : 0;
  out->window_surface_plan_available =
      vblank_plan->window_surface_plan_available ? 1 : 0;
  out->window_surface_plan_safe =
      vblank_plan->window_surface_plan_safe ? 1 : 0;
  out->window_compositor_plan_safe =
      vblank_plan->window_compositor_plan_safe ? 1 : 0;
  out->window_damage_plan_safe =
      vblank_plan->window_damage_plan_safe ? 1 : 0;
  out->window_present_plan_safe =
      vblank_plan->window_present_plan_safe ? 1 : 0;
  out->window_schedule_plan_safe =
      vblank_plan->window_schedule_plan_safe ? 1 : 0;
  out->window_vsync_plan_safe =
      vblank_plan->window_vsync_plan_safe ? 1 : 0;
  out->window_scanout_plan_safe =
      vblank_plan->window_scanout_plan_safe ? 1 : 0;
  out->window_display_plan_safe =
      vblank_plan->window_display_plan_safe ? 1 : 0;
  out->window_output_plan_safe =
      vblank_plan->window_output_plan_safe ? 1 : 0;
  out->window_blit_plan_safe =
      vblank_plan->window_blit_plan_safe ? 1 : 0;
  out->window_commit_plan_safe =
      vblank_plan->window_commit_plan_safe ? 1 : 0;
  out->window_flip_plan_safe =
      vblank_plan->window_flip_plan_safe ? 1 : 0;
  out->window_vblank_plan_safe =
      vblank_plan->window_vblank_plan_safe ? 1 : 0;
  safe = vblank_plan->window_vblank_plan_safe &&
         vblank_plan->window_flip_plan_safe &&
         vblank_plan->window_commit_plan_safe &&
         vblank_plan->window_blit_plan_safe &&
         vblank_plan->window_output_plan_safe &&
         vblank_plan->window_display_plan_safe &&
         vblank_plan->window_scanout_plan_safe &&
         vblank_plan->window_vsync_plan_safe &&
         vblank_plan->window_schedule_plan_safe &&
         vblank_plan->window_present_plan_safe &&
         vblank_plan->window_damage_plan_safe &&
         vblank_plan->window_compositor_plan_safe &&
         vblank_plan->window_surface_plan_safe &&
         vblank_plan->window_flip_plan_available &&
         vblank_plan->window_commit_plan_available &&
         vblank_plan->window_blit_plan_available &&
         vblank_plan->window_output_plan_available &&
         vblank_plan->window_display_plan_available &&
         vblank_plan->window_scanout_plan_available &&
         vblank_plan->window_vsync_plan_available &&
         vblank_plan->window_schedule_plan_available &&
         vblank_plan->window_present_plan_available &&
         vblank_plan->window_damage_plan_available &&
         vblank_plan->window_compositor_plan_available &&
         vblank_plan->window_surface_plan_available &&
         vblank_plan->vblank_required && vblank_plan->vblank_allowed &&
         !vblank_plan->vblank_submitted &&
         vblank_plan->vblank_ticket_selected &&
         vblank_plan->vblank_target_selected &&
         !vblank_plan->vblank_event_armed &&
         !vblank_plan->vblank_event_submitted &&
         !vblank_plan->vblank_callback_armed &&
         !vblank_plan->vblank_callback_submitted &&
         !vblank_plan->vblank_timestamp_captured &&
         !vblank_plan->vblank_timestamp_submitted &&
         !vblank_plan->vblank_frame_completed &&
         !vblank_plan->vblank_frame_submitted &&
         !vblank_plan->vblank_error && vblank_plan->flip_required &&
         vblank_plan->flip_allowed && !vblank_plan->flip_submitted &&
         vblank_plan->flip_ticket_selected &&
         vblank_plan->flip_target_selected &&
         !vblank_plan->flip_buffer_attached &&
         !vblank_plan->flip_buffer_submitted &&
         !vblank_plan->flip_vblank_armed &&
         !vblank_plan->flip_vblank_submitted &&
         !vblank_plan->flip_event_armed &&
         !vblank_plan->flip_event_submitted &&
         !vblank_plan->flip_async_allowed &&
         !vblank_plan->flip_async_submitted &&
         !vblank_plan->flip_error &&
         vblank_plan->commit_required && vblank_plan->commit_allowed &&
         !vblank_plan->commit_submitted &&
         vblank_plan->commit_ticket_selected &&
         vblank_plan->commit_target_selected &&
         !vblank_plan->commit_state_attached &&
         !vblank_plan->commit_state_submitted &&
         !vblank_plan->commit_atomic_allowed &&
         !vblank_plan->commit_atomic_submitted &&
         !vblank_plan->commit_callback_armed &&
         !vblank_plan->commit_callback_submitted &&
         !vblank_plan->commit_error &&
         vblank_plan->blit_required && vblank_plan->blit_allowed &&
         !vblank_plan->blit_submitted &&
         vblank_plan->blit_ticket_selected &&
         vblank_plan->blit_target_selected &&
         !vblank_plan->blit_source_buffer_mapped &&
         !vblank_plan->blit_destination_buffer_mapped &&
         !vblank_plan->blit_pixels_copied &&
         !vblank_plan->blit_dma_allowed &&
         !vblank_plan->blit_dma_submitted &&
         !vblank_plan->blit_error &&
         vblank_plan->output_required && vblank_plan->output_allowed &&
         !vblank_plan->output_submitted &&
         vblank_plan->output_ticket_selected &&
         vblank_plan->output_target_selected &&
         !vblank_plan->output_connector_attached &&
         !vblank_plan->output_connector_submitted &&
         !vblank_plan->output_mode_attached &&
         !vblank_plan->output_mode_submitted &&
         !vblank_plan->output_signal_armed &&
         !vblank_plan->output_signal_submitted &&
         !vblank_plan->output_error &&
         vblank_plan->display_required && vblank_plan->display_allowed &&
         !vblank_plan->display_submitted &&
         vblank_plan->display_ticket_selected &&
         vblank_plan->display_target_selected &&
         !vblank_plan->display_controller_attached &&
         !vblank_plan->display_controller_submitted &&
         !vblank_plan->display_output_attached &&
         !vblank_plan->display_output_submitted &&
         !vblank_plan->display_pipeline_attached &&
         !vblank_plan->display_pipeline_submitted &&
         !vblank_plan->display_error &&
         vblank_plan->scanout_required && vblank_plan->scanout_allowed &&
         !vblank_plan->scanout_submitted &&
         vblank_plan->scanout_ticket_selected &&
         vblank_plan->scanout_target_selected &&
         !vblank_plan->scanout_buffer_attached &&
         !vblank_plan->scanout_buffer_submitted &&
         !vblank_plan->scanout_display_flip_allowed &&
         !vblank_plan->scanout_display_flip_submitted &&
         !vblank_plan->scanout_error &&
         vblank_plan->vsync_required && vblank_plan->vsync_allowed &&
         !vblank_plan->vsync_submitted &&
         vblank_plan->vsync_ticket_selected &&
         vblank_plan->vsync_target_selected &&
         !vblank_plan->vsync_wait_allowed &&
         !vblank_plan->vsync_wait_submitted &&
         vblank_plan->vsync_fence_required &&
         !vblank_plan->vsync_fence_armed &&
         !vblank_plan->vsync_error &&
         vblank_plan->schedule_required && vblank_plan->schedule_allowed &&
         !vblank_plan->schedule_submitted &&
         vblank_plan->schedule_ticket_selected &&
         vblank_plan->schedule_target_selected &&
         !vblank_plan->schedule_cache_hit &&
         !vblank_plan->schedule_error &&
         vblank_plan->frame_pacing_required &&
         vblank_plan->frame_pacing_allowed &&
         !vblank_plan->frame_timer_armed &&
         !vblank_plan->compositor_wake_allowed &&
         !vblank_plan->compositor_wake_submitted &&
         !vblank_plan->page_flip_allowed &&
         !vblank_plan->page_flip_submitted &&
         vblank_plan->present_required && vblank_plan->present_allowed &&
         !vblank_plan->present_submitted &&
         vblank_plan->present_ticket_selected &&
         vblank_plan->present_target_selected &&
         !vblank_plan->present_cache_hit &&
         !vblank_plan->present_error &&
         vblank_plan->damage_required && vblank_plan->damage_allowed &&
         !vblank_plan->damage_submitted &&
         vblank_plan->damage_ticket_selected &&
         vblank_plan->damage_target_selected &&
         !vblank_plan->damage_cache_hit && !vblank_plan->damage_error &&
         vblank_plan->compositor_required &&
         vblank_plan->compositor_allowed &&
         !vblank_plan->compositor_submitted &&
         vblank_plan->compositor_ticket_selected &&
         vblank_plan->compositor_target_selected &&
         vblank_plan->compositor_surface_allowed &&
         !vblank_plan->compositor_surface_submitted &&
         vblank_plan->compositor_damage_planned &&
         vblank_plan->compositor_damage_allowed &&
         !vblank_plan->compositor_damage_submitted &&
         vblank_plan->surface_required && vblank_plan->surface_allowed &&
         !vblank_plan->surface_bound &&
         vblank_plan->surface_ticket_selected &&
         vblank_plan->surface_target_selected &&
         !vblank_plan->surface_memory_mapped &&
         !vblank_plan->surface_pixels_written &&
         vblank_plan->window_required && vblank_plan->window_allowed &&
         !vblank_plan->window_created &&
         vblank_plan->window_ticket_selected &&
         vblank_plan->window_target_selected &&
         !vblank_plan->window_surface_bound &&
         !vblank_plan->window_input_bound &&
         vblank_plan->gui_required && vblank_plan->gui_allowed &&
         !vblank_plan->gui_submitted &&
         vblank_plan->gui_ticket_selected &&
         vblank_plan->gui_target_selected &&
         vblank_plan->release_required && vblank_plan->release_allowed &&
         !vblank_plan->release_submitted &&
         vblank_plan->release_ticket_selected &&
         vblank_plan->release_target_selected &&
         vblank_plan->reclaim_required && vblank_plan->reclaim_allowed &&
         !vblank_plan->reclaim_submitted &&
         vblank_plan->reclaim_ticket_selected &&
         vblank_plan->reclaim_target_selected &&
         vblank_plan->compaction_required &&
         vblank_plan->compaction_allowed &&
         !vblank_plan->compaction_submitted &&
         vblank_plan->compaction_ticket_selected &&
         vblank_plan->compaction_target_selected &&
         vblank_plan->route_selected && !vblank_plan->route_blocked &&
         vblank_plan->credential_session_safe &&
         vblank_plan->credential_storage_wiped &&
         vblank_plan->credential_redacted &&
         vblank_plan->length_redacted &&
         !vblank_plan->raw_secret_exposed &&
         !vblank_plan->masked_text_exposed &&
         vblank_plan->submit_blocked && !vblank_plan->submit_enabled &&
         !vblank_plan->auth_attempt_allowed &&
         !vblank_plan->submit_callback_bound &&
         !vblank_plan->auth_callback_bound &&
         vblank_plan->text_login_authoritative;
  if (!safe) {
    out->event_type = "credential-screen-window-event-plan-unsafe";
    out->blocked_reason = "credential-window-event-plan-unsafe";
    out->message =
        "Credential screen window event plan unsafe; use text login.";
    return 0;
  }
  out->window_event_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = vblank_plan->action_allowed ? 1 : 0;
  out->action_blocked = vblank_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = vblank_plan->input_focus_allowed ? 1 : 0;
  out->compaction_allowed = vblank_plan->compaction_allowed ? 1 : 0;
  out->compaction_ticket_selected =
      vblank_plan->compaction_ticket_selected ? 1 : 0;
  out->compaction_target_selected =
      vblank_plan->compaction_target_selected ? 1 : 0;
  out->reclaim_allowed = vblank_plan->reclaim_allowed ? 1 : 0;
  out->reclaim_ticket_selected =
      vblank_plan->reclaim_ticket_selected ? 1 : 0;
  out->reclaim_target_selected =
      vblank_plan->reclaim_target_selected ? 1 : 0;
  out->release_allowed = vblank_plan->release_allowed ? 1 : 0;
  out->release_ticket_selected =
      vblank_plan->release_ticket_selected ? 1 : 0;
  out->release_target_selected =
      vblank_plan->release_target_selected ? 1 : 0;
  out->gui_allowed = vblank_plan->gui_allowed ? 1 : 0;
  out->gui_ticket_selected = vblank_plan->gui_ticket_selected ? 1 : 0;
  out->gui_target_selected = vblank_plan->gui_target_selected ? 1 : 0;
  out->window_allowed = vblank_plan->window_allowed ? 1 : 0;
  out->window_ticket_selected =
      vblank_plan->window_ticket_selected ? 1 : 0;
  out->window_target_selected =
      vblank_plan->window_target_selected ? 1 : 0;
  out->surface_allowed = vblank_plan->surface_allowed ? 1 : 0;
  out->surface_ticket_selected =
      vblank_plan->surface_ticket_selected ? 1 : 0;
  out->surface_target_selected =
      vblank_plan->surface_target_selected ? 1 : 0;
  out->compositor_allowed = vblank_plan->compositor_allowed ? 1 : 0;
  out->compositor_ticket_selected =
      vblank_plan->compositor_ticket_selected ? 1 : 0;
  out->compositor_target_selected =
      vblank_plan->compositor_target_selected ? 1 : 0;
  out->compositor_surface_allowed =
      vblank_plan->compositor_surface_allowed ? 1 : 0;
  out->compositor_damage_planned =
      vblank_plan->compositor_damage_planned ? 1 : 0;
  out->compositor_damage_allowed =
      vblank_plan->compositor_damage_allowed ? 1 : 0;
  out->damage_allowed = vblank_plan->damage_allowed ? 1 : 0;
  out->damage_ticket_selected =
      vblank_plan->damage_ticket_selected ? 1 : 0;
  out->damage_target_selected =
      vblank_plan->damage_target_selected ? 1 : 0;
  out->damage_incremental_allowed =
      vblank_plan->damage_incremental_allowed ? 1 : 0;
  out->full_damage_required = vblank_plan->full_damage_required ? 1 : 0;
  out->damage_cache_allowed = vblank_plan->damage_cache_allowed ? 1 : 0;
  out->damage_reuse_allowed = vblank_plan->damage_reuse_allowed ? 1 : 0;
  out->present_allowed = vblank_plan->present_allowed ? 1 : 0;
  out->present_ticket_selected =
      vblank_plan->present_ticket_selected ? 1 : 0;
  out->present_target_selected =
      vblank_plan->present_target_selected ? 1 : 0;
  out->present_incremental_allowed =
      vblank_plan->present_incremental_allowed ? 1 : 0;
  out->full_present_required = vblank_plan->full_present_required ? 1 : 0;
  out->present_cache_allowed = vblank_plan->present_cache_allowed ? 1 : 0;
  out->present_reuse_allowed = vblank_plan->present_reuse_allowed ? 1 : 0;
  out->schedule_allowed = vblank_plan->schedule_allowed ? 1 : 0;
  out->schedule_ticket_selected =
      vblank_plan->schedule_ticket_selected ? 1 : 0;
  out->schedule_target_selected =
      vblank_plan->schedule_target_selected ? 1 : 0;
  out->schedule_incremental_allowed =
      vblank_plan->schedule_incremental_allowed ? 1 : 0;
  out->full_schedule_required =
      vblank_plan->full_schedule_required ? 1 : 0;
  out->schedule_cache_allowed =
      vblank_plan->schedule_cache_allowed ? 1 : 0;
  out->schedule_reuse_allowed =
      vblank_plan->schedule_reuse_allowed ? 1 : 0;
  out->frame_pacing_allowed = 1;
  out->vsync_allowed = vblank_plan->vsync_allowed ? 1 : 0;
  out->vsync_ticket_selected = vblank_plan->vsync_ticket_selected ? 1 : 0;
  out->vsync_target_selected = vblank_plan->vsync_target_selected ? 1 : 0;
  out->scanout_allowed = vblank_plan->scanout_allowed ? 1 : 0;
  out->scanout_ticket_selected =
      vblank_plan->scanout_ticket_selected ? 1 : 0;
  out->scanout_target_selected =
      vblank_plan->scanout_target_selected ? 1 : 0;
  out->display_allowed = vblank_plan->display_allowed ? 1 : 0;
  out->display_ticket_selected =
      vblank_plan->display_ticket_selected ? 1 : 0;
  out->display_target_selected =
      vblank_plan->display_target_selected ? 1 : 0;
  out->output_allowed = vblank_plan->output_allowed ? 1 : 0;
  out->output_ticket_selected =
      vblank_plan->output_ticket_selected ? 1 : 0;
  out->output_target_selected =
      vblank_plan->output_target_selected ? 1 : 0;
  out->blit_allowed = vblank_plan->blit_allowed ? 1 : 0;
  out->blit_ticket_selected = vblank_plan->blit_ticket_selected ? 1 : 0;
  out->blit_target_selected = vblank_plan->blit_target_selected ? 1 : 0;
  out->commit_allowed = vblank_plan->commit_allowed ? 1 : 0;
  out->commit_ticket_selected =
      vblank_plan->commit_ticket_selected ? 1 : 0;
  out->commit_target_selected =
      vblank_plan->commit_target_selected ? 1 : 0;
  out->flip_allowed = vblank_plan->flip_allowed ? 1 : 0;
  out->flip_ticket_selected = vblank_plan->flip_ticket_selected ? 1 : 0;
  out->flip_target_selected = vblank_plan->flip_target_selected ? 1 : 0;
  out->vblank_allowed = vblank_plan->vblank_allowed ? 1 : 0;
  out->vblank_ticket_selected =
      vblank_plan->vblank_ticket_selected ? 1 : 0;
  out->vblank_target_selected =
      vblank_plan->vblank_target_selected ? 1 : 0;
  out->event_allowed = 1;
  out->event_ticket_selected = 1;
  out->event_target_selected = 1;
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
  out->vblank_error = 0;
  out->event_error = 0;
  out->recovery_text_session_required =
      vblank_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required =
      vblank_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required =
      vblank_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = vblank_plan->submit_requested ? 1 : 0;
  out->submit_blocked = 1;
  out->text_login_authoritative = 1;
  out->view = vblank_plan->view;
  out->widget_tree = vblank_plan->widget_tree;
  out->compaction_ticket = vblank_plan->compaction_ticket;
  out->reclaim_ticket = vblank_plan->reclaim_ticket;
  out->release_ticket = vblank_plan->release_ticket;
  out->gui_ticket = vblank_plan->gui_ticket;
  out->window_ticket = vblank_plan->window_ticket;
  out->surface_ticket = vblank_plan->surface_ticket;
  out->compositor_ticket = vblank_plan->compositor_ticket;
  out->damage_ticket = vblank_plan->damage_ticket;
  out->present_ticket = vblank_plan->present_ticket;
  out->schedule_ticket = vblank_plan->schedule_ticket;
  out->vsync_ticket = vblank_plan->vsync_ticket;
  out->scanout_ticket = vblank_plan->scanout_ticket;
  out->display_ticket = vblank_plan->display_ticket;
  out->output_ticket = vblank_plan->output_ticket;
  out->blit_ticket = vblank_plan->blit_ticket;
  out->commit_ticket = vblank_plan->commit_ticket;
  out->flip_ticket = vblank_plan->flip_ticket;
  out->vblank_ticket = vblank_plan->vblank_ticket;
  out->focus_target = vblank_plan->focus_target;
  out->primary_action = vblank_plan->primary_action;
  out->route = vblank_plan->route;
  out->compositor_target = vblank_plan->compositor_target;
  out->compaction_policy = vblank_plan->compaction_policy;
  out->reclaim_policy = vblank_plan->reclaim_policy;
  out->release_policy = vblank_plan->release_policy;
  out->gui_policy = vblank_plan->gui_policy;
  out->window_policy = vblank_plan->window_policy;
  out->surface_policy = vblank_plan->surface_policy;
  out->compositor_policy = vblank_plan->compositor_policy;
  out->damage_policy = vblank_plan->damage_policy;
  out->cache_policy = vblank_plan->cache_policy;
  out->present_policy = vblank_plan->present_policy;
  out->schedule_policy = vblank_plan->schedule_policy;
  out->vsync_policy = vblank_plan->vsync_policy;
  out->scanout_policy = vblank_plan->scanout_policy;
  out->display_policy = vblank_plan->display_policy;
  out->output_policy = vblank_plan->output_policy;
  out->blit_policy = vblank_plan->blit_policy;
  out->commit_policy = vblank_plan->commit_policy;
  out->flip_policy = vblank_plan->flip_policy;
  out->vblank_policy = vblank_plan->vblank_policy;
  out->event_policy = out->schedule_incremental_allowed
                          ? "incremental-window-event-declarative"
                          : "full-window-event-declarative";
  out->event_type = "credential-screen-window-event-plan-ready";
  out->state = "window-event-ready";
  out->message =
      "Credential screen window event ticket ready; no handler armed, callback dispatched, timestamp captured or frame completion submitted.";
  out->blocked_reason = vblank_plan->blocked_reason;
  if (vblank_plan->submit_requested) {
    out->event_ticket = "text-login-fallback-window-event-ticket";
    out->compositor_target = "text-login-fallback-window-event";
    out->event_policy = "fallback-window-event-declarative";
    out->event_text_login = 1;
    out->event_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "window-event-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (vblank_plan->vblank_credential_panel &&
      vblank_plan->vblank_credential_input &&
      vblank_plan->vblank_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->event_ticket = "credential-screen-window-event-ticket";
    out->compositor_target = "credential-screen-window-event";
    out->event_credential_panel = 1;
    out->event_credential_input = 1;
    out->event_credential_focus = 1;
    out->event_text_login = 0;
    out->event_text_login_fallback = 0;
    out->state = "window-event-credential-ready";
    out->message =
        "Credential window event ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (vblank_plan->vblank_text_recovery &&
      out->recovery_text_session_required) {
    out->event_ticket = "text-recovery-window-event-ticket";
    out->compositor_target = "text-recovery-window-event";
    out->event_text_recovery = 1;
    out->event_text_login = 1;
    out->event_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "window-event-text-recovery-ready";
    out->message =
        "Text recovery window event ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (vblank_plan->vblank_text_login_resume &&
      out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->event_ticket = "text-login-resume-window-event-ticket";
    out->compositor_target = "text-login-resume-window-event";
    out->event_policy = "full-window-event-declarative";
    out->cache_policy = "window-event-cache-bypassed-for-rerender";
    out->event_text_login = 1;
    out->event_text_login_resume = 1;
    out->event_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "window-event-resume-ready";
    out->message =
        "Text login resume window event ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->event_ticket = "text-login-fallback-window-event-ticket";
  out->compositor_target = "text-login-fallback-window-event";
  out->event_policy = "fallback-window-event-declarative";
  out->event_text_login = 1;
  out->event_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "window-event-text-login-ready";
  out->message = "Use authoritative text login.";
  out->blocked_reason = "ready";
  return 0;
}
