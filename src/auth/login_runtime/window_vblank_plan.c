/*
 * src/auth/login_runtime/window_vblank_plan.c
 *
 * Credential-screen window-vblank plan reset + builder — extracted
 * byte-for-byte from `src/auth/login_runtime.c` during PR C.61 of
 * the Estagio C dedicated plan.  Hosts the per-plan reset helper
 * and the public builder for the window-vblank stage of the
 * credential pipeline:
 *
 *   - login_window_credential_screen_window_vblank_plan_reset (static)
 *   - login_window_credential_screen_window_vblank_plan_build
 *
 * The window-vblank-plan converts a fail-closed window-flip-plan
 * into a window-vblank contract for the downstream window-event
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

static void login_window_credential_screen_window_vblank_plan_reset(
    struct login_window_credential_screen_window_vblank_plan *out,
    int window_flip_plan_available) {
  *out = (struct login_window_credential_screen_window_vblank_plan){0};
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_WINDOW_VBLANK_PLAN_VERSION;
  out->window_flip_plan_available = window_flip_plan_available ? 1 : 0;
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
  out->vblank_text_login = 1;
  out->vblank_text_login_fallback = 1;
  out->vblank_error = 1;
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
  out->cache_policy = "window-vblank-cache-disabled";
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
  out->event_type = "credential-screen-window-vblank-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "window-flip-plan-unavailable";
}

int login_window_credential_screen_window_vblank_plan_build(
    const struct login_window_credential_screen_window_flip_plan *flip_plan,
    struct login_window_credential_screen_window_vblank_plan *out) {
  int safe = 0;
  if (!out) return -1;
  login_window_credential_screen_window_vblank_plan_reset(
      out, flip_plan ? 1 : 0);
  if (!flip_plan) return 0;
  out->requested_action = flip_plan->requested_action;
  out->window_commit_plan_available =
      flip_plan->window_commit_plan_available ? 1 : 0;
  out->window_blit_plan_available =
      flip_plan->window_blit_plan_available ? 1 : 0;
  out->window_output_plan_available =
      flip_plan->window_output_plan_available ? 1 : 0;
  out->window_display_plan_available =
      flip_plan->window_display_plan_available ? 1 : 0;
  out->window_scanout_plan_available =
      flip_plan->window_scanout_plan_available ? 1 : 0;
  out->window_vsync_plan_available =
      flip_plan->window_vsync_plan_available ? 1 : 0;
  out->window_schedule_plan_available =
      flip_plan->window_schedule_plan_available ? 1 : 0;
  out->window_present_plan_available =
      flip_plan->window_present_plan_available ? 1 : 0;
  out->window_damage_plan_available =
      flip_plan->window_damage_plan_available ? 1 : 0;
  out->window_compositor_plan_available =
      flip_plan->window_compositor_plan_available ? 1 : 0;
  out->window_surface_plan_available =
      flip_plan->window_surface_plan_available ? 1 : 0;
  out->window_surface_plan_safe =
      flip_plan->window_surface_plan_safe ? 1 : 0;
  out->window_compositor_plan_safe =
      flip_plan->window_compositor_plan_safe ? 1 : 0;
  out->window_damage_plan_safe =
      flip_plan->window_damage_plan_safe ? 1 : 0;
  out->window_present_plan_safe =
      flip_plan->window_present_plan_safe ? 1 : 0;
  out->window_schedule_plan_safe =
      flip_plan->window_schedule_plan_safe ? 1 : 0;
  out->window_vsync_plan_safe =
      flip_plan->window_vsync_plan_safe ? 1 : 0;
  out->window_scanout_plan_safe =
      flip_plan->window_scanout_plan_safe ? 1 : 0;
  out->window_display_plan_safe =
      flip_plan->window_display_plan_safe ? 1 : 0;
  out->window_output_plan_safe =
      flip_plan->window_output_plan_safe ? 1 : 0;
  out->window_blit_plan_safe =
      flip_plan->window_blit_plan_safe ? 1 : 0;
  out->window_commit_plan_safe =
      flip_plan->window_commit_plan_safe ? 1 : 0;
  out->window_flip_plan_safe =
      flip_plan->window_flip_plan_safe ? 1 : 0;
  safe = flip_plan->window_flip_plan_safe &&
         flip_plan->window_commit_plan_safe &&
         flip_plan->window_blit_plan_safe &&
         flip_plan->window_output_plan_safe &&
         flip_plan->window_display_plan_safe &&
         flip_plan->window_scanout_plan_safe &&
         flip_plan->window_vsync_plan_safe &&
         flip_plan->window_schedule_plan_safe &&
         flip_plan->window_present_plan_safe &&
         flip_plan->window_damage_plan_safe &&
         flip_plan->window_compositor_plan_safe &&
         flip_plan->window_surface_plan_safe &&
         flip_plan->window_commit_plan_available &&
         flip_plan->window_blit_plan_available &&
         flip_plan->window_output_plan_available &&
         flip_plan->window_display_plan_available &&
         flip_plan->window_scanout_plan_available &&
         flip_plan->window_vsync_plan_available &&
         flip_plan->window_schedule_plan_available &&
         flip_plan->window_present_plan_available &&
         flip_plan->window_damage_plan_available &&
         flip_plan->window_compositor_plan_available &&
         flip_plan->window_surface_plan_available &&
         flip_plan->flip_required && flip_plan->flip_allowed &&
         !flip_plan->flip_submitted &&
         flip_plan->flip_ticket_selected &&
         flip_plan->flip_target_selected &&
         !flip_plan->flip_buffer_attached &&
         !flip_plan->flip_buffer_submitted &&
         !flip_plan->flip_vblank_armed &&
         !flip_plan->flip_vblank_submitted &&
         !flip_plan->flip_event_armed &&
         !flip_plan->flip_event_submitted &&
         !flip_plan->flip_async_allowed &&
         !flip_plan->flip_async_submitted &&
         !flip_plan->flip_error &&
         flip_plan->commit_required && flip_plan->commit_allowed &&
         !flip_plan->commit_submitted &&
         flip_plan->commit_ticket_selected &&
         flip_plan->commit_target_selected &&
         !flip_plan->commit_state_attached &&
         !flip_plan->commit_state_submitted &&
         !flip_plan->commit_atomic_allowed &&
         !flip_plan->commit_atomic_submitted &&
         !flip_plan->commit_callback_armed &&
         !flip_plan->commit_callback_submitted &&
         !flip_plan->commit_error && flip_plan->blit_required &&
         flip_plan->blit_allowed && !flip_plan->blit_submitted &&
         flip_plan->blit_ticket_selected &&
         flip_plan->blit_target_selected &&
         !flip_plan->blit_source_buffer_mapped &&
         !flip_plan->blit_destination_buffer_mapped &&
         !flip_plan->blit_pixels_copied && !flip_plan->blit_dma_allowed &&
         !flip_plan->blit_dma_submitted && !flip_plan->blit_error &&
         flip_plan->output_required && flip_plan->output_allowed &&
         !flip_plan->output_submitted &&
         flip_plan->output_ticket_selected &&
         flip_plan->output_target_selected &&
         !flip_plan->output_connector_attached &&
         !flip_plan->output_connector_submitted &&
         !flip_plan->output_mode_attached &&
         !flip_plan->output_mode_submitted &&
         !flip_plan->output_signal_armed &&
         !flip_plan->output_signal_submitted &&
         !flip_plan->output_error && flip_plan->display_required &&
         flip_plan->display_allowed && !flip_plan->display_submitted &&
         flip_plan->display_ticket_selected &&
         flip_plan->display_target_selected &&
         !flip_plan->display_controller_attached &&
         !flip_plan->display_controller_submitted &&
         !flip_plan->display_output_attached &&
         !flip_plan->display_output_submitted &&
         !flip_plan->display_pipeline_attached &&
         !flip_plan->display_pipeline_submitted &&
         !flip_plan->display_error && flip_plan->scanout_required &&
         flip_plan->scanout_allowed && !flip_plan->scanout_submitted &&
         flip_plan->scanout_ticket_selected &&
         flip_plan->scanout_target_selected &&
         !flip_plan->scanout_buffer_attached &&
         !flip_plan->scanout_buffer_submitted &&
         !flip_plan->scanout_display_flip_allowed &&
         !flip_plan->scanout_display_flip_submitted &&
         !flip_plan->scanout_error && flip_plan->vsync_required &&
         flip_plan->vsync_allowed && !flip_plan->vsync_submitted &&
         flip_plan->vsync_ticket_selected &&
         flip_plan->vsync_target_selected &&
         !flip_plan->vsync_wait_allowed &&
         !flip_plan->vsync_wait_submitted &&
         flip_plan->vsync_fence_required &&
         !flip_plan->vsync_fence_armed && !flip_plan->vsync_error &&
         flip_plan->schedule_required && flip_plan->schedule_allowed &&
         !flip_plan->schedule_submitted &&
         flip_plan->schedule_ticket_selected &&
         flip_plan->schedule_target_selected &&
         !flip_plan->schedule_cache_hit &&
         !flip_plan->schedule_error && flip_plan->frame_pacing_required &&
         flip_plan->frame_pacing_allowed &&
         !flip_plan->frame_timer_armed &&
         !flip_plan->compositor_wake_allowed &&
         !flip_plan->compositor_wake_submitted &&
         !flip_plan->page_flip_allowed &&
         !flip_plan->page_flip_submitted &&
         flip_plan->present_required && flip_plan->present_allowed &&
         !flip_plan->present_submitted &&
         flip_plan->present_ticket_selected &&
         flip_plan->present_target_selected &&
         !flip_plan->present_cache_hit && !flip_plan->present_error &&
         flip_plan->damage_required && flip_plan->damage_allowed &&
         !flip_plan->damage_submitted &&
         flip_plan->damage_ticket_selected &&
         flip_plan->damage_target_selected &&
         !flip_plan->damage_cache_hit && !flip_plan->damage_error &&
         flip_plan->compositor_required &&
         flip_plan->compositor_allowed &&
         !flip_plan->compositor_submitted &&
         flip_plan->compositor_ticket_selected &&
         flip_plan->compositor_target_selected &&
         flip_plan->compositor_surface_allowed &&
         !flip_plan->compositor_surface_submitted &&
         flip_plan->compositor_damage_planned &&
         flip_plan->compositor_damage_allowed &&
         !flip_plan->compositor_damage_submitted &&
         flip_plan->surface_required && flip_plan->surface_allowed &&
         !flip_plan->surface_bound &&
         flip_plan->surface_ticket_selected &&
         flip_plan->surface_target_selected &&
         !flip_plan->surface_memory_mapped &&
         !flip_plan->surface_pixels_written &&
         flip_plan->window_required && flip_plan->window_allowed &&
         !flip_plan->window_created &&
         flip_plan->window_ticket_selected &&
         flip_plan->window_target_selected &&
         !flip_plan->window_surface_bound &&
         !flip_plan->window_input_bound && flip_plan->gui_required &&
         flip_plan->gui_allowed && !flip_plan->gui_submitted &&
         flip_plan->gui_ticket_selected &&
         flip_plan->gui_target_selected && flip_plan->release_required &&
         flip_plan->release_allowed && !flip_plan->release_submitted &&
         flip_plan->release_ticket_selected &&
         flip_plan->release_target_selected &&
         flip_plan->reclaim_required && flip_plan->reclaim_allowed &&
         !flip_plan->reclaim_submitted &&
         flip_plan->reclaim_ticket_selected &&
         flip_plan->reclaim_target_selected &&
         flip_plan->compaction_required &&
         flip_plan->compaction_allowed &&
         !flip_plan->compaction_submitted &&
         flip_plan->compaction_ticket_selected &&
         flip_plan->compaction_target_selected &&
         flip_plan->route_selected && !flip_plan->route_blocked &&
         flip_plan->credential_session_safe &&
         flip_plan->credential_storage_wiped &&
         flip_plan->credential_redacted && flip_plan->length_redacted &&
         !flip_plan->raw_secret_exposed &&
         !flip_plan->masked_text_exposed && flip_plan->submit_blocked &&
         !flip_plan->submit_enabled && !flip_plan->auth_attempt_allowed &&
         !flip_plan->submit_callback_bound &&
         !flip_plan->auth_callback_bound &&
         flip_plan->text_login_authoritative;
  if (!safe) {
    out->event_type = "credential-screen-window-vblank-plan-unsafe";
    out->blocked_reason = "credential-window-vblank-plan-unsafe";
    out->message =
        "Credential screen window vblank plan unsafe; use text login.";
    return 0;
  }
  out->window_vblank_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = flip_plan->action_allowed ? 1 : 0;
  out->action_blocked = flip_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = flip_plan->input_focus_allowed ? 1 : 0;
  out->compaction_allowed = flip_plan->compaction_allowed ? 1 : 0;
  out->compaction_ticket_selected =
      flip_plan->compaction_ticket_selected ? 1 : 0;
  out->compaction_target_selected =
      flip_plan->compaction_target_selected ? 1 : 0;
  out->reclaim_allowed = flip_plan->reclaim_allowed ? 1 : 0;
  out->reclaim_ticket_selected = flip_plan->reclaim_ticket_selected ? 1 : 0;
  out->reclaim_target_selected = flip_plan->reclaim_target_selected ? 1 : 0;
  out->release_allowed = flip_plan->release_allowed ? 1 : 0;
  out->release_ticket_selected = flip_plan->release_ticket_selected ? 1 : 0;
  out->release_target_selected = flip_plan->release_target_selected ? 1 : 0;
  out->gui_allowed = flip_plan->gui_allowed ? 1 : 0;
  out->gui_ticket_selected = flip_plan->gui_ticket_selected ? 1 : 0;
  out->gui_target_selected = flip_plan->gui_target_selected ? 1 : 0;
  out->window_allowed = flip_plan->window_allowed ? 1 : 0;
  out->window_ticket_selected = flip_plan->window_ticket_selected ? 1 : 0;
  out->window_target_selected = flip_plan->window_target_selected ? 1 : 0;
  out->surface_allowed = flip_plan->surface_allowed ? 1 : 0;
  out->surface_ticket_selected = flip_plan->surface_ticket_selected ? 1 : 0;
  out->surface_target_selected = flip_plan->surface_target_selected ? 1 : 0;
  out->compositor_allowed = flip_plan->compositor_allowed ? 1 : 0;
  out->compositor_ticket_selected =
      flip_plan->compositor_ticket_selected ? 1 : 0;
  out->compositor_target_selected =
      flip_plan->compositor_target_selected ? 1 : 0;
  out->compositor_surface_allowed =
      flip_plan->compositor_surface_allowed ? 1 : 0;
  out->compositor_damage_planned =
      flip_plan->compositor_damage_planned ? 1 : 0;
  out->compositor_damage_allowed =
      flip_plan->compositor_damage_allowed ? 1 : 0;
  out->damage_allowed = flip_plan->damage_allowed ? 1 : 0;
  out->damage_ticket_selected = flip_plan->damage_ticket_selected ? 1 : 0;
  out->damage_target_selected = flip_plan->damage_target_selected ? 1 : 0;
  out->damage_incremental_allowed =
      flip_plan->damage_incremental_allowed ? 1 : 0;
  out->full_damage_required = flip_plan->full_damage_required ? 1 : 0;
  out->damage_cache_allowed = flip_plan->damage_cache_allowed ? 1 : 0;
  out->damage_reuse_allowed = flip_plan->damage_reuse_allowed ? 1 : 0;
  out->present_allowed = flip_plan->present_allowed ? 1 : 0;
  out->present_ticket_selected = flip_plan->present_ticket_selected ? 1 : 0;
  out->present_target_selected = flip_plan->present_target_selected ? 1 : 0;
  out->present_incremental_allowed =
      flip_plan->present_incremental_allowed ? 1 : 0;
  out->full_present_required = flip_plan->full_present_required ? 1 : 0;
  out->present_cache_allowed = flip_plan->present_cache_allowed ? 1 : 0;
  out->present_reuse_allowed = flip_plan->present_reuse_allowed ? 1 : 0;
  out->schedule_allowed = flip_plan->schedule_allowed ? 1 : 0;
  out->schedule_ticket_selected =
      flip_plan->schedule_ticket_selected ? 1 : 0;
  out->schedule_target_selected =
      flip_plan->schedule_target_selected ? 1 : 0;
  out->schedule_incremental_allowed =
      flip_plan->schedule_incremental_allowed ? 1 : 0;
  out->full_schedule_required = flip_plan->full_schedule_required ? 1 : 0;
  out->schedule_cache_allowed = flip_plan->schedule_cache_allowed ? 1 : 0;
  out->schedule_reuse_allowed = flip_plan->schedule_reuse_allowed ? 1 : 0;
  out->frame_pacing_allowed = 1;
  out->vsync_allowed = flip_plan->vsync_allowed ? 1 : 0;
  out->vsync_ticket_selected = flip_plan->vsync_ticket_selected ? 1 : 0;
  out->vsync_target_selected = flip_plan->vsync_target_selected ? 1 : 0;
  out->scanout_allowed = flip_plan->scanout_allowed ? 1 : 0;
  out->scanout_ticket_selected = flip_plan->scanout_ticket_selected ? 1 : 0;
  out->scanout_target_selected = flip_plan->scanout_target_selected ? 1 : 0;
  out->display_allowed = flip_plan->display_allowed ? 1 : 0;
  out->display_ticket_selected = flip_plan->display_ticket_selected ? 1 : 0;
  out->display_target_selected = flip_plan->display_target_selected ? 1 : 0;
  out->output_allowed = flip_plan->output_allowed ? 1 : 0;
  out->output_ticket_selected = flip_plan->output_ticket_selected ? 1 : 0;
  out->output_target_selected = flip_plan->output_target_selected ? 1 : 0;
  out->blit_allowed = flip_plan->blit_allowed ? 1 : 0;
  out->blit_ticket_selected = flip_plan->blit_ticket_selected ? 1 : 0;
  out->blit_target_selected = flip_plan->blit_target_selected ? 1 : 0;
  out->commit_allowed = flip_plan->commit_allowed ? 1 : 0;
  out->commit_ticket_selected = flip_plan->commit_ticket_selected ? 1 : 0;
  out->commit_target_selected = flip_plan->commit_target_selected ? 1 : 0;
  out->flip_allowed = flip_plan->flip_allowed ? 1 : 0;
  out->flip_ticket_selected = flip_plan->flip_ticket_selected ? 1 : 0;
  out->flip_target_selected = flip_plan->flip_target_selected ? 1 : 0;
  out->vblank_allowed = 1;
  out->vblank_ticket_selected = 1;
  out->vblank_target_selected = 1;
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
  out->recovery_text_session_required =
      flip_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = flip_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required =
      flip_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = flip_plan->submit_requested ? 1 : 0;
  out->submit_blocked = 1;
  out->text_login_authoritative = 1;
  out->view = flip_plan->view;
  out->widget_tree = flip_plan->widget_tree;
  out->compaction_ticket = flip_plan->compaction_ticket;
  out->reclaim_ticket = flip_plan->reclaim_ticket;
  out->release_ticket = flip_plan->release_ticket;
  out->gui_ticket = flip_plan->gui_ticket;
  out->window_ticket = flip_plan->window_ticket;
  out->surface_ticket = flip_plan->surface_ticket;
  out->compositor_ticket = flip_plan->compositor_ticket;
  out->damage_ticket = flip_plan->damage_ticket;
  out->present_ticket = flip_plan->present_ticket;
  out->schedule_ticket = flip_plan->schedule_ticket;
  out->vsync_ticket = flip_plan->vsync_ticket;
  out->scanout_ticket = flip_plan->scanout_ticket;
  out->display_ticket = flip_plan->display_ticket;
  out->output_ticket = flip_plan->output_ticket;
  out->blit_ticket = flip_plan->blit_ticket;
  out->commit_ticket = flip_plan->commit_ticket;
  out->flip_ticket = flip_plan->flip_ticket;
  out->focus_target = flip_plan->focus_target;
  out->primary_action = flip_plan->primary_action;
  out->route = flip_plan->route;
  out->compositor_target = flip_plan->compositor_target;
  out->compaction_policy = flip_plan->compaction_policy;
  out->reclaim_policy = flip_plan->reclaim_policy;
  out->release_policy = flip_plan->release_policy;
  out->gui_policy = flip_plan->gui_policy;
  out->window_policy = flip_plan->window_policy;
  out->surface_policy = flip_plan->surface_policy;
  out->compositor_policy = flip_plan->compositor_policy;
  out->damage_policy = flip_plan->damage_policy;
  out->cache_policy = flip_plan->cache_policy;
  out->present_policy = flip_plan->present_policy;
  out->schedule_policy = flip_plan->schedule_policy;
  out->vsync_policy = flip_plan->vsync_policy;
  out->scanout_policy = flip_plan->scanout_policy;
  out->display_policy = flip_plan->display_policy;
  out->output_policy = flip_plan->output_policy;
  out->blit_policy = flip_plan->blit_policy;
  out->commit_policy = flip_plan->commit_policy;
  out->flip_policy = flip_plan->flip_policy;
  out->vblank_policy = out->schedule_incremental_allowed
                           ? "incremental-window-vblank-declarative"
                           : "full-window-vblank-declarative";
  out->event_type = "credential-screen-window-vblank-plan-ready";
  out->state = "window-vblank-ready";
  out->message =
      "Credential screen window vblank ticket ready; no event armed, callback submitted, timestamp captured or frame completion submitted.";
  out->blocked_reason = flip_plan->blocked_reason;
  if (flip_plan->submit_requested) {
    out->vblank_ticket = "text-login-fallback-window-vblank-ticket";
    out->compositor_target = "text-login-fallback-window-vblank";
    out->vblank_policy = "fallback-window-vblank-declarative";
    out->vblank_text_login = 1;
    out->vblank_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "window-vblank-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (flip_plan->flip_credential_panel &&
      flip_plan->flip_credential_input &&
      flip_plan->flip_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->vblank_ticket = "credential-screen-window-vblank-ticket";
    out->compositor_target = "credential-screen-window-vblank";
    out->vblank_credential_panel = 1;
    out->vblank_credential_input = 1;
    out->vblank_credential_focus = 1;
    out->vblank_text_login = 0;
    out->vblank_text_login_fallback = 0;
    out->state = "window-vblank-credential-ready";
    out->message =
        "Credential window vblank ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (flip_plan->flip_text_recovery &&
      out->recovery_text_session_required) {
    out->vblank_ticket = "text-recovery-window-vblank-ticket";
    out->compositor_target = "text-recovery-window-vblank";
    out->vblank_text_recovery = 1;
    out->vblank_text_login = 1;
    out->vblank_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "window-vblank-text-recovery-ready";
    out->message =
        "Text recovery window vblank ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (flip_plan->flip_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->vblank_ticket = "text-login-resume-window-vblank-ticket";
    out->compositor_target = "text-login-resume-window-vblank";
    out->vblank_policy = "full-window-vblank-declarative";
    out->cache_policy = "window-vblank-cache-bypassed-for-rerender";
    out->vblank_text_login = 1;
    out->vblank_text_login_resume = 1;
    out->vblank_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "window-vblank-resume-ready";
    out->message =
        "Text login resume window vblank ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->vblank_ticket = "text-login-fallback-window-vblank-ticket";
  out->compositor_target = "text-login-fallback-window-vblank";
  out->vblank_policy = "fallback-window-vblank-declarative";
  out->vblank_text_login = 1;
  out->vblank_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "window-vblank-text-login-ready";
  out->message = "Use authoritative text login.";
  out->blocked_reason = "ready";
  return 0;
}
