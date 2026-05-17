/*
 * src/auth/login_runtime/window_blit_plan.c
 *
 * Credential-screen window-blit plan reset + builder — extracted
 * byte-for-byte from `src/auth/login_runtime.c` during PR C.58 of
 * the Estagio C dedicated plan.  Hosts the per-plan reset helper
 * and the public builder for the window-blit stage of the
 * credential pipeline:
 *
 *   - login_window_credential_screen_window_blit_plan_reset (static)
 *   - login_window_credential_screen_window_blit_plan_build
 *
 * The window-blit-plan converts a fail-closed window-output-plan
 * into a window-blit contract for the downstream window-commit
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

static void login_window_credential_screen_window_blit_plan_reset(
    struct login_window_credential_screen_window_blit_plan *out,
    int window_output_plan_available) {
  *out = (struct login_window_credential_screen_window_blit_plan){0};
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_WINDOW_BLIT_PLAN_VERSION;
  out->window_output_plan_available = window_output_plan_available ? 1 : 0;
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
  out->blit_text_login = 1;
  out->blit_text_login_fallback = 1;
  out->blit_error = 1;
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
  out->cache_policy = "window-blit-cache-disabled";
  out->present_policy = "present-disabled";
  out->schedule_policy = "schedule-disabled";
  out->vsync_policy = "vsync-disabled";
  out->scanout_policy = "scanout-disabled";
  out->display_policy = "display-disabled";
  out->output_policy = "output-disabled";
  out->blit_policy = "blit-disabled";
  out->event_type = "credential-screen-window-blit-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "window-output-plan-unavailable";
}

int login_window_credential_screen_window_blit_plan_build(
    const struct login_window_credential_screen_window_output_plan *output_plan,
    struct login_window_credential_screen_window_blit_plan *out) {
  int safe = 0;
  if (!out) return -1;
  login_window_credential_screen_window_blit_plan_reset(
      out, output_plan ? 1 : 0);
  if (!output_plan) return 0;
  out->requested_action = output_plan->requested_action;
  out->window_display_plan_available =
      output_plan->window_display_plan_available ? 1 : 0;
  out->window_scanout_plan_available =
      output_plan->window_scanout_plan_available ? 1 : 0;
  out->window_vsync_plan_available =
      output_plan->window_vsync_plan_available ? 1 : 0;
  out->window_schedule_plan_available =
      output_plan->window_schedule_plan_available ? 1 : 0;
  out->window_present_plan_available =
      output_plan->window_present_plan_available ? 1 : 0;
  out->window_damage_plan_available =
      output_plan->window_damage_plan_available ? 1 : 0;
  out->window_compositor_plan_available =
      output_plan->window_compositor_plan_available ? 1 : 0;
  out->window_surface_plan_available =
      output_plan->window_surface_plan_available ? 1 : 0;
  out->window_surface_plan_safe =
      output_plan->window_surface_plan_safe ? 1 : 0;
  out->window_compositor_plan_safe =
      output_plan->window_compositor_plan_safe ? 1 : 0;
  out->window_damage_plan_safe =
      output_plan->window_damage_plan_safe ? 1 : 0;
  out->window_present_plan_safe =
      output_plan->window_present_plan_safe ? 1 : 0;
  out->window_schedule_plan_safe =
      output_plan->window_schedule_plan_safe ? 1 : 0;
  out->window_vsync_plan_safe =
      output_plan->window_vsync_plan_safe ? 1 : 0;
  out->window_scanout_plan_safe =
      output_plan->window_scanout_plan_safe ? 1 : 0;
  out->window_display_plan_safe =
      output_plan->window_display_plan_safe ? 1 : 0;
  out->window_output_plan_safe =
      output_plan->window_output_plan_safe ? 1 : 0;
  safe = output_plan->window_output_plan_safe &&
         output_plan->window_display_plan_safe &&
         output_plan->window_scanout_plan_safe &&
         output_plan->window_vsync_plan_safe &&
         output_plan->window_schedule_plan_safe &&
         output_plan->window_present_plan_safe &&
         output_plan->window_damage_plan_safe &&
         output_plan->window_compositor_plan_safe &&
         output_plan->window_surface_plan_safe &&
         output_plan->window_display_plan_available &&
         output_plan->window_scanout_plan_available &&
         output_plan->window_vsync_plan_available &&
         output_plan->window_schedule_plan_available &&
         output_plan->window_present_plan_available &&
         output_plan->window_damage_plan_available &&
         output_plan->window_compositor_plan_available &&
         output_plan->window_surface_plan_available &&
         output_plan->output_required && output_plan->output_allowed &&
         !output_plan->output_submitted &&
         output_plan->output_ticket_selected &&
         output_plan->output_target_selected &&
         !output_plan->output_connector_attached &&
         !output_plan->output_connector_submitted &&
         !output_plan->output_mode_attached &&
         !output_plan->output_mode_submitted &&
         !output_plan->output_signal_armed &&
         !output_plan->output_signal_submitted &&
         !output_plan->output_error &&
         output_plan->display_required && output_plan->display_allowed &&
         !output_plan->display_submitted &&
         output_plan->display_ticket_selected &&
         output_plan->display_target_selected &&
         !output_plan->display_controller_attached &&
         !output_plan->display_controller_submitted &&
         !output_plan->display_output_attached &&
         !output_plan->display_output_submitted &&
         !output_plan->display_pipeline_attached &&
         !output_plan->display_pipeline_submitted &&
         !output_plan->display_error &&
         output_plan->scanout_required && output_plan->scanout_allowed &&
         !output_plan->scanout_submitted &&
         output_plan->scanout_ticket_selected &&
         output_plan->scanout_target_selected &&
         !output_plan->scanout_buffer_attached &&
         !output_plan->scanout_buffer_submitted &&
         !output_plan->scanout_display_flip_allowed &&
         !output_plan->scanout_display_flip_submitted &&
         !output_plan->scanout_error &&
         output_plan->vsync_required && output_plan->vsync_allowed &&
         !output_plan->vsync_submitted &&
         output_plan->vsync_ticket_selected &&
         output_plan->vsync_target_selected &&
         !output_plan->vsync_wait_allowed &&
         !output_plan->vsync_wait_submitted &&
         output_plan->vsync_fence_required &&
         !output_plan->vsync_fence_armed && !output_plan->vsync_error &&
         output_plan->schedule_required && output_plan->schedule_allowed &&
         !output_plan->schedule_submitted &&
         output_plan->schedule_ticket_selected &&
         output_plan->schedule_target_selected &&
         !output_plan->schedule_cache_hit &&
         !output_plan->schedule_error &&
         output_plan->frame_pacing_required &&
         output_plan->frame_pacing_allowed &&
         !output_plan->frame_timer_armed &&
         !output_plan->compositor_wake_allowed &&
         !output_plan->compositor_wake_submitted &&
         !output_plan->page_flip_allowed &&
         !output_plan->page_flip_submitted &&
         output_plan->present_required && output_plan->present_allowed &&
         !output_plan->present_submitted &&
         output_plan->present_ticket_selected &&
         output_plan->present_target_selected &&
         !output_plan->present_cache_hit &&
         !output_plan->present_error && output_plan->damage_required &&
         output_plan->damage_allowed && !output_plan->damage_submitted &&
         output_plan->damage_ticket_selected &&
         output_plan->damage_target_selected &&
         !output_plan->damage_cache_hit &&
         !output_plan->damage_error &&
         output_plan->compositor_required &&
         output_plan->compositor_allowed &&
         !output_plan->compositor_submitted &&
         output_plan->compositor_ticket_selected &&
         output_plan->compositor_target_selected &&
         output_plan->compositor_surface_allowed &&
         !output_plan->compositor_surface_submitted &&
         output_plan->compositor_damage_planned &&
         output_plan->compositor_damage_allowed &&
         !output_plan->compositor_damage_submitted &&
         output_plan->surface_required && output_plan->surface_allowed &&
         !output_plan->surface_bound &&
         output_plan->surface_ticket_selected &&
         output_plan->surface_target_selected &&
         !output_plan->surface_memory_mapped &&
         !output_plan->surface_pixels_written &&
         output_plan->window_required && output_plan->window_allowed &&
         !output_plan->window_created &&
         output_plan->window_ticket_selected &&
         output_plan->window_target_selected &&
         !output_plan->window_surface_bound &&
         !output_plan->window_input_bound &&
         output_plan->gui_required && output_plan->gui_allowed &&
         !output_plan->gui_submitted &&
         output_plan->gui_ticket_selected &&
         output_plan->gui_target_selected &&
         !output_plan->gui_pixels_write_allowed &&
         !output_plan->gui_pixels_written &&
         output_plan->release_required && output_plan->release_allowed &&
         !output_plan->release_submitted &&
         output_plan->release_ticket_selected &&
         output_plan->release_target_selected &&
         !output_plan->release_storage_prune_allowed &&
         !output_plan->release_storage_pruned &&
         !output_plan->release_resource_release_allowed &&
         !output_plan->release_resource_released &&
         !output_plan->release_cpu_gpu_sync_allowed &&
         !output_plan->release_cpu_gpu_sync_submitted &&
         output_plan->reclaim_required && output_plan->reclaim_allowed &&
         !output_plan->reclaim_submitted &&
         output_plan->reclaim_ticket_selected &&
         output_plan->reclaim_target_selected &&
         !output_plan->reclaim_storage_prune_allowed &&
         !output_plan->reclaim_storage_pruned &&
         !output_plan->reclaim_resource_release_allowed &&
         !output_plan->reclaim_resource_released &&
         !output_plan->reclaim_cpu_gpu_sync_allowed &&
         !output_plan->reclaim_cpu_gpu_sync_submitted &&
         output_plan->compaction_required &&
         output_plan->compaction_allowed &&
         !output_plan->compaction_submitted &&
         output_plan->compaction_ticket_selected &&
         output_plan->compaction_target_selected &&
         output_plan->route_selected && !output_plan->route_blocked &&
         output_plan->credential_session_safe &&
         output_plan->credential_storage_wiped &&
         output_plan->credential_redacted && output_plan->length_redacted &&
         !output_plan->raw_secret_exposed &&
         !output_plan->masked_text_exposed && output_plan->submit_blocked &&
         !output_plan->submit_enabled && !output_plan->auth_attempt_allowed &&
         !output_plan->submit_callback_bound &&
         !output_plan->auth_callback_bound &&
         output_plan->text_login_authoritative;
  if (!safe) {
    out->event_type = "credential-screen-window-blit-plan-unsafe";
    out->blocked_reason = "credential-window-blit-plan-unsafe";
    out->message =
        "Credential screen window blit plan unsafe; use text login.";
    return 0;
  }
  out->window_blit_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = output_plan->action_allowed ? 1 : 0;
  out->action_blocked = output_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = output_plan->input_focus_allowed ? 1 : 0;
  out->compaction_allowed = output_plan->compaction_allowed ? 1 : 0;
  out->compaction_ticket_selected =
      output_plan->compaction_ticket_selected ? 1 : 0;
  out->compaction_target_selected =
      output_plan->compaction_target_selected ? 1 : 0;
  out->reclaim_allowed = output_plan->reclaim_allowed ? 1 : 0;
  out->reclaim_ticket_selected = output_plan->reclaim_ticket_selected ? 1 : 0;
  out->reclaim_target_selected = output_plan->reclaim_target_selected ? 1 : 0;
  out->release_allowed = output_plan->release_allowed ? 1 : 0;
  out->release_ticket_selected = output_plan->release_ticket_selected ? 1 : 0;
  out->release_target_selected = output_plan->release_target_selected ? 1 : 0;
  out->gui_allowed = output_plan->gui_allowed ? 1 : 0;
  out->gui_ticket_selected = output_plan->gui_ticket_selected ? 1 : 0;
  out->gui_target_selected = output_plan->gui_target_selected ? 1 : 0;
  out->window_allowed = output_plan->window_allowed ? 1 : 0;
  out->window_ticket_selected = output_plan->window_ticket_selected ? 1 : 0;
  out->window_target_selected = output_plan->window_target_selected ? 1 : 0;
  out->surface_allowed = output_plan->surface_allowed ? 1 : 0;
  out->surface_ticket_selected = output_plan->surface_ticket_selected ? 1 : 0;
  out->surface_target_selected = output_plan->surface_target_selected ? 1 : 0;
  out->compositor_allowed = output_plan->compositor_allowed ? 1 : 0;
  out->compositor_ticket_selected =
      output_plan->compositor_ticket_selected ? 1 : 0;
  out->compositor_target_selected =
      output_plan->compositor_target_selected ? 1 : 0;
  out->compositor_surface_allowed =
      output_plan->compositor_surface_allowed ? 1 : 0;
  out->compositor_damage_planned =
      output_plan->compositor_damage_planned ? 1 : 0;
  out->compositor_damage_allowed =
      output_plan->compositor_damage_allowed ? 1 : 0;
  out->damage_allowed = output_plan->damage_allowed ? 1 : 0;
  out->damage_ticket_selected = output_plan->damage_ticket_selected ? 1 : 0;
  out->damage_target_selected = output_plan->damage_target_selected ? 1 : 0;
  out->damage_incremental_allowed =
      output_plan->damage_incremental_allowed ? 1 : 0;
  out->full_damage_required = output_plan->full_damage_required ? 1 : 0;
  out->damage_cache_allowed = output_plan->damage_cache_allowed ? 1 : 0;
  out->damage_reuse_allowed = output_plan->damage_reuse_allowed ? 1 : 0;
  out->present_allowed = output_plan->present_allowed ? 1 : 0;
  out->present_ticket_selected = output_plan->present_ticket_selected ? 1 : 0;
  out->present_target_selected = output_plan->present_target_selected ? 1 : 0;
  out->present_incremental_allowed =
      output_plan->present_incremental_allowed ? 1 : 0;
  out->full_present_required = output_plan->full_present_required ? 1 : 0;
  out->present_cache_allowed = output_plan->present_cache_allowed ? 1 : 0;
  out->present_reuse_allowed = output_plan->present_reuse_allowed ? 1 : 0;
  out->schedule_allowed = output_plan->schedule_allowed ? 1 : 0;
  out->schedule_ticket_selected =
      output_plan->schedule_ticket_selected ? 1 : 0;
  out->schedule_target_selected =
      output_plan->schedule_target_selected ? 1 : 0;
  out->schedule_incremental_allowed =
      output_plan->schedule_incremental_allowed ? 1 : 0;
  out->full_schedule_required = output_plan->full_schedule_required ? 1 : 0;
  out->schedule_cache_allowed = output_plan->schedule_cache_allowed ? 1 : 0;
  out->schedule_reuse_allowed = output_plan->schedule_reuse_allowed ? 1 : 0;
  out->frame_pacing_allowed = 1;
  out->vsync_allowed = output_plan->vsync_allowed ? 1 : 0;
  out->vsync_ticket_selected = output_plan->vsync_ticket_selected ? 1 : 0;
  out->vsync_target_selected = output_plan->vsync_target_selected ? 1 : 0;
  out->scanout_allowed = output_plan->scanout_allowed ? 1 : 0;
  out->scanout_ticket_selected = output_plan->scanout_ticket_selected ? 1 : 0;
  out->scanout_target_selected = output_plan->scanout_target_selected ? 1 : 0;
  out->display_allowed = output_plan->display_allowed ? 1 : 0;
  out->display_ticket_selected = output_plan->display_ticket_selected ? 1 : 0;
  out->display_target_selected = output_plan->display_target_selected ? 1 : 0;
  out->output_allowed = output_plan->output_allowed ? 1 : 0;
  out->output_ticket_selected = output_plan->output_ticket_selected ? 1 : 0;
  out->output_target_selected = output_plan->output_target_selected ? 1 : 0;
  out->blit_allowed = 1;
  out->blit_ticket_selected = 1;
  out->blit_target_selected = 1;
  out->damage_error = 0;
  out->present_error = 0;
  out->schedule_error = 0;
  out->vsync_error = 0;
  out->scanout_error = 0;
  out->display_error = 0;
  out->output_error = 0;
  out->blit_error = 0;
  out->recovery_text_session_required =
      output_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = output_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required =
      output_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = output_plan->submit_requested ? 1 : 0;
  out->submit_blocked = 1;
  out->text_login_authoritative = 1;
  out->view = output_plan->view;
  out->widget_tree = output_plan->widget_tree;
  out->compaction_ticket = output_plan->compaction_ticket;
  out->reclaim_ticket = output_plan->reclaim_ticket;
  out->release_ticket = output_plan->release_ticket;
  out->gui_ticket = output_plan->gui_ticket;
  out->window_ticket = output_plan->window_ticket;
  out->surface_ticket = output_plan->surface_ticket;
  out->compositor_ticket = output_plan->compositor_ticket;
  out->damage_ticket = output_plan->damage_ticket;
  out->present_ticket = output_plan->present_ticket;
  out->schedule_ticket = output_plan->schedule_ticket;
  out->vsync_ticket = output_plan->vsync_ticket;
  out->scanout_ticket = output_plan->scanout_ticket;
  out->display_ticket = output_plan->display_ticket;
  out->output_ticket = output_plan->output_ticket;
  out->focus_target = output_plan->focus_target;
  out->primary_action = output_plan->primary_action;
  out->route = output_plan->route;
  out->compositor_target = output_plan->compositor_target;
  out->compaction_policy = output_plan->compaction_policy;
  out->reclaim_policy = output_plan->reclaim_policy;
  out->release_policy = output_plan->release_policy;
  out->gui_policy = output_plan->gui_policy;
  out->window_policy = output_plan->window_policy;
  out->surface_policy = output_plan->surface_policy;
  out->compositor_policy = output_plan->compositor_policy;
  out->damage_policy = output_plan->damage_policy;
  out->cache_policy = output_plan->cache_policy;
  out->present_policy = output_plan->present_policy;
  out->schedule_policy = output_plan->schedule_policy;
  out->vsync_policy = output_plan->vsync_policy;
  out->scanout_policy = output_plan->scanout_policy;
  out->display_policy = output_plan->display_policy;
  out->output_policy = output_plan->output_policy;
  out->blit_policy = out->schedule_incremental_allowed
                         ? "incremental-window-blit-declarative"
                         : "full-window-blit-declarative";
  out->event_type = "credential-screen-window-blit-plan-ready";
  out->state = "window-blit-ready";
  out->message =
      "Credential screen window blit ticket ready; no buffers mapped, pixels copied or DMA armed.";
  out->blocked_reason = output_plan->blocked_reason;
  if (output_plan->submit_requested) {
    out->blit_ticket = "text-login-fallback-window-blit-ticket";
    out->compositor_target = "text-login-fallback-window-blit";
    out->blit_policy = "fallback-window-blit-declarative";
    out->blit_text_login = 1;
    out->blit_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "window-blit-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (output_plan->output_credential_panel &&
      output_plan->output_credential_input &&
      output_plan->output_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->blit_ticket = "credential-screen-window-blit-ticket";
    out->compositor_target = "credential-screen-window-blit";
    out->blit_credential_panel = 1;
    out->blit_credential_input = 1;
    out->blit_credential_focus = 1;
    out->blit_text_login = 0;
    out->blit_text_login_fallback = 0;
    out->state = "window-blit-credential-ready";
    out->message =
        "Credential window blit ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (output_plan->output_text_recovery &&
      out->recovery_text_session_required) {
    out->blit_ticket = "text-recovery-window-blit-ticket";
    out->compositor_target = "text-recovery-window-blit";
    out->blit_text_recovery = 1;
    out->blit_text_login = 1;
    out->blit_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "window-blit-text-recovery-ready";
    out->message =
        "Text recovery window blit ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (output_plan->output_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->blit_ticket = "text-login-resume-window-blit-ticket";
    out->compositor_target = "text-login-resume-window-blit";
    out->blit_policy = "full-window-blit-declarative";
    out->cache_policy = "window-blit-cache-bypassed-for-rerender";
    out->blit_text_login = 1;
    out->blit_text_login_resume = 1;
    out->blit_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "window-blit-resume-ready";
    out->message =
        "Text login resume window blit ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->blit_ticket = "text-login-fallback-window-blit-ticket";
  out->compositor_target = "text-login-fallback-window-blit";
  out->blit_policy = "fallback-window-blit-declarative";
  out->blit_text_login = 1;
  out->blit_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "window-blit-text-login-ready";
  out->message = "Use authoritative text login.";
  out->blocked_reason = "ready";
  return 0;
}
