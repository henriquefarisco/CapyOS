/*
 * src/auth/login_runtime/window_output_plan.c
 *
 * Credential-screen window-output plan reset + builder — extracted
 * byte-for-byte from `src/auth/login_runtime.c` during PR C.57 of
 * the Estagio C dedicated plan.  Hosts the per-plan reset helper
 * and the public builder for the window-output stage of the
 * credential pipeline:
 *
 *   - login_window_credential_screen_window_output_plan_reset (static)
 *   - login_window_credential_screen_window_output_plan_build
 *
 * The window-output-plan converts a fail-closed window-display-plan
 * into a window-output contract for the downstream window-blit
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

static void login_window_credential_screen_window_output_plan_reset(
    struct login_window_credential_screen_window_output_plan *out,
    int window_display_plan_available) {
  *out = (struct login_window_credential_screen_window_output_plan){0};
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_WINDOW_OUTPUT_PLAN_VERSION;
  out->window_display_plan_available = window_display_plan_available ? 1 : 0;
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
  out->output_text_login = 1;
  out->output_text_login_fallback = 1;
  out->output_error = 1;
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
  out->cache_policy = "window-output-cache-disabled";
  out->present_policy = "present-disabled";
  out->schedule_policy = "schedule-disabled";
  out->vsync_policy = "vsync-disabled";
  out->scanout_policy = "scanout-disabled";
  out->display_policy = "display-disabled";
  out->output_policy = "output-disabled";
  out->event_type = "credential-screen-window-output-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "window-display-plan-unavailable";
}

int login_window_credential_screen_window_output_plan_build(
    const struct login_window_credential_screen_window_display_plan *display_plan,
    struct login_window_credential_screen_window_output_plan *out) {
  int safe = 0;
  if (!out) return -1;
  login_window_credential_screen_window_output_plan_reset(
      out, display_plan ? 1 : 0);
  if (!display_plan) return 0;
  out->requested_action = display_plan->requested_action;
  out->window_scanout_plan_available =
      display_plan->window_scanout_plan_available ? 1 : 0;
  out->window_vsync_plan_available =
      display_plan->window_vsync_plan_available ? 1 : 0;
  out->window_schedule_plan_available =
      display_plan->window_schedule_plan_available ? 1 : 0;
  out->window_present_plan_available =
      display_plan->window_present_plan_available ? 1 : 0;
  out->window_damage_plan_available =
      display_plan->window_damage_plan_available ? 1 : 0;
  out->window_compositor_plan_available =
      display_plan->window_compositor_plan_available ? 1 : 0;
  out->window_surface_plan_available =
      display_plan->window_surface_plan_available ? 1 : 0;
  out->window_surface_plan_safe =
      display_plan->window_surface_plan_safe ? 1 : 0;
  out->window_compositor_plan_safe =
      display_plan->window_compositor_plan_safe ? 1 : 0;
  out->window_damage_plan_safe =
      display_plan->window_damage_plan_safe ? 1 : 0;
  out->window_present_plan_safe =
      display_plan->window_present_plan_safe ? 1 : 0;
  out->window_schedule_plan_safe =
      display_plan->window_schedule_plan_safe ? 1 : 0;
  out->window_vsync_plan_safe =
      display_plan->window_vsync_plan_safe ? 1 : 0;
  out->window_scanout_plan_safe =
      display_plan->window_scanout_plan_safe ? 1 : 0;
  out->window_display_plan_safe =
      display_plan->window_display_plan_safe ? 1 : 0;
  safe = display_plan->window_display_plan_safe &&
         display_plan->window_scanout_plan_safe &&
         display_plan->window_vsync_plan_safe &&
         display_plan->window_schedule_plan_safe &&
         display_plan->window_present_plan_safe &&
         display_plan->window_damage_plan_safe &&
         display_plan->window_compositor_plan_safe &&
         display_plan->window_surface_plan_safe &&
         display_plan->window_scanout_plan_available &&
         display_plan->window_vsync_plan_available &&
         display_plan->window_schedule_plan_available &&
         display_plan->window_present_plan_available &&
         display_plan->window_damage_plan_available &&
         display_plan->window_compositor_plan_available &&
         display_plan->window_surface_plan_available &&
         display_plan->display_required && display_plan->display_allowed &&
         !display_plan->display_submitted &&
         display_plan->display_ticket_selected &&
         display_plan->display_target_selected &&
         !display_plan->display_controller_attached &&
         !display_plan->display_controller_submitted &&
         !display_plan->display_output_attached &&
         !display_plan->display_output_submitted &&
         !display_plan->display_pipeline_attached &&
         !display_plan->display_pipeline_submitted &&
         !display_plan->display_error &&
         display_plan->scanout_required && display_plan->scanout_allowed &&
         !display_plan->scanout_submitted &&
         display_plan->scanout_ticket_selected &&
         display_plan->scanout_target_selected &&
         !display_plan->scanout_buffer_attached &&
         !display_plan->scanout_buffer_submitted &&
         !display_plan->scanout_display_flip_allowed &&
         !display_plan->scanout_display_flip_submitted &&
         !display_plan->scanout_error &&
         display_plan->vsync_required && display_plan->vsync_allowed &&
         !display_plan->vsync_submitted &&
         display_plan->vsync_ticket_selected &&
         display_plan->vsync_target_selected &&
         !display_plan->vsync_wait_allowed &&
         !display_plan->vsync_wait_submitted &&
         display_plan->vsync_fence_required &&
         !display_plan->vsync_fence_armed && !display_plan->vsync_error &&
         display_plan->schedule_required && display_plan->schedule_allowed &&
         !display_plan->schedule_submitted &&
         display_plan->schedule_ticket_selected &&
         display_plan->schedule_target_selected &&
         !display_plan->schedule_cache_hit &&
         !display_plan->schedule_auth_submit_allowed &&
         !display_plan->schedule_auth_attempt_allowed &&
         !display_plan->schedule_error &&
         display_plan->frame_pacing_required &&
         display_plan->frame_pacing_allowed &&
         !display_plan->frame_timer_armed &&
         !display_plan->compositor_wake_allowed &&
         !display_plan->compositor_wake_submitted &&
         !display_plan->page_flip_allowed &&
         !display_plan->page_flip_submitted &&
         display_plan->present_required && display_plan->present_allowed &&
         !display_plan->present_submitted &&
         display_plan->present_ticket_selected &&
         display_plan->present_target_selected &&
         !display_plan->present_cache_hit &&
         !display_plan->present_auth_submit_allowed &&
         !display_plan->present_auth_attempt_allowed &&
         !display_plan->present_error && display_plan->damage_required &&
         display_plan->damage_allowed && !display_plan->damage_submitted &&
         display_plan->damage_ticket_selected &&
         display_plan->damage_target_selected &&
         !display_plan->damage_cache_hit &&
         !display_plan->damage_auth_submit_allowed &&
         !display_plan->damage_auth_attempt_allowed &&
         !display_plan->damage_error &&
         display_plan->compositor_required &&
         display_plan->compositor_allowed &&
         !display_plan->compositor_submitted &&
         display_plan->compositor_ticket_selected &&
         display_plan->compositor_target_selected &&
         display_plan->compositor_surface_allowed &&
         !display_plan->compositor_surface_submitted &&
         display_plan->compositor_damage_planned &&
         display_plan->compositor_damage_allowed &&
         !display_plan->compositor_damage_submitted &&
         !display_plan->compositor_auth_submit_allowed &&
         !display_plan->compositor_auth_attempt_allowed &&
         display_plan->surface_required && display_plan->surface_allowed &&
         !display_plan->surface_bound &&
         display_plan->surface_ticket_selected &&
         display_plan->surface_target_selected &&
         !display_plan->surface_memory_mapped &&
         !display_plan->surface_pixels_written &&
         !display_plan->surface_compositor_submit_allowed &&
         !display_plan->surface_compositor_submitted &&
         !display_plan->surface_auth_submit_allowed &&
         !display_plan->surface_auth_attempt_allowed &&
         display_plan->window_required && display_plan->window_allowed &&
         !display_plan->window_created &&
         display_plan->window_ticket_selected &&
         display_plan->window_target_selected &&
         !display_plan->window_surface_bound &&
         !display_plan->window_input_bound &&
         !display_plan->window_auth_submit_allowed &&
         !display_plan->window_auth_attempt_allowed &&
         display_plan->gui_required && display_plan->gui_allowed &&
         !display_plan->gui_submitted &&
         display_plan->gui_ticket_selected &&
         display_plan->gui_target_selected &&
         !display_plan->gui_pixels_write_allowed &&
         !display_plan->gui_pixels_written &&
         !display_plan->gui_auth_submit_allowed &&
         !display_plan->gui_auth_attempt_allowed &&
         display_plan->release_required && display_plan->release_allowed &&
         !display_plan->release_submitted &&
         display_plan->release_ticket_selected &&
         display_plan->release_target_selected &&
         !display_plan->release_storage_prune_allowed &&
         !display_plan->release_storage_pruned &&
         !display_plan->release_resource_release_allowed &&
         !display_plan->release_resource_released &&
         !display_plan->release_cpu_gpu_sync_allowed &&
         !display_plan->release_cpu_gpu_sync_submitted &&
         display_plan->reclaim_required && display_plan->reclaim_allowed &&
         !display_plan->reclaim_submitted &&
         display_plan->reclaim_ticket_selected &&
         display_plan->reclaim_target_selected &&
         !display_plan->reclaim_storage_prune_allowed &&
         !display_plan->reclaim_storage_pruned &&
         !display_plan->reclaim_resource_release_allowed &&
         !display_plan->reclaim_resource_released &&
         !display_plan->reclaim_cpu_gpu_sync_allowed &&
         !display_plan->reclaim_cpu_gpu_sync_submitted &&
         display_plan->compaction_required &&
         display_plan->compaction_allowed &&
         !display_plan->compaction_submitted &&
         display_plan->compaction_ticket_selected &&
         display_plan->compaction_target_selected &&
         !display_plan->compaction_storage_write_allowed &&
         !display_plan->compaction_storage_written &&
         !display_plan->compaction_resource_release_allowed &&
         !display_plan->compaction_resource_released &&
         !display_plan->compaction_cpu_gpu_sync_allowed &&
         !display_plan->compaction_cpu_gpu_sync_submitted &&
         display_plan->route_selected && !display_plan->route_blocked &&
         display_plan->credential_session_safe &&
         display_plan->credential_storage_wiped &&
         display_plan->credential_redacted && display_plan->length_redacted &&
         !display_plan->raw_secret_exposed &&
         !display_plan->masked_text_exposed && display_plan->submit_blocked &&
         !display_plan->submit_enabled && !display_plan->auth_attempt_allowed &&
         !display_plan->submit_callback_bound &&
         !display_plan->auth_callback_bound &&
         display_plan->text_login_authoritative;
  if (!safe) {
    out->event_type = "credential-screen-window-output-plan-unsafe";
    out->blocked_reason = "credential-window-output-plan-unsafe";
    out->message =
        "Credential screen window output plan unsafe; use text login.";
    return 0;
  }
  out->window_output_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = display_plan->action_allowed ? 1 : 0;
  out->action_blocked = display_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = display_plan->input_focus_allowed ? 1 : 0;
  out->compaction_allowed = display_plan->compaction_allowed ? 1 : 0;
  out->compaction_ticket_selected =
      display_plan->compaction_ticket_selected ? 1 : 0;
  out->compaction_target_selected =
      display_plan->compaction_target_selected ? 1 : 0;
  out->reclaim_allowed = display_plan->reclaim_allowed ? 1 : 0;
  out->reclaim_ticket_selected = display_plan->reclaim_ticket_selected ? 1 : 0;
  out->reclaim_target_selected = display_plan->reclaim_target_selected ? 1 : 0;
  out->release_allowed = display_plan->release_allowed ? 1 : 0;
  out->release_ticket_selected = display_plan->release_ticket_selected ? 1 : 0;
  out->release_target_selected = display_plan->release_target_selected ? 1 : 0;
  out->gui_allowed = display_plan->gui_allowed ? 1 : 0;
  out->gui_ticket_selected = display_plan->gui_ticket_selected ? 1 : 0;
  out->gui_target_selected = display_plan->gui_target_selected ? 1 : 0;
  out->window_allowed = display_plan->window_allowed ? 1 : 0;
  out->window_ticket_selected = display_plan->window_ticket_selected ? 1 : 0;
  out->window_target_selected = display_plan->window_target_selected ? 1 : 0;
  out->surface_allowed = display_plan->surface_allowed ? 1 : 0;
  out->surface_ticket_selected = display_plan->surface_ticket_selected ? 1 : 0;
  out->surface_target_selected = display_plan->surface_target_selected ? 1 : 0;
  out->compositor_allowed = display_plan->compositor_allowed ? 1 : 0;
  out->compositor_ticket_selected =
      display_plan->compositor_ticket_selected ? 1 : 0;
  out->compositor_target_selected =
      display_plan->compositor_target_selected ? 1 : 0;
  out->compositor_surface_allowed =
      display_plan->compositor_surface_allowed ? 1 : 0;
  out->compositor_damage_planned =
      display_plan->compositor_damage_planned ? 1 : 0;
  out->compositor_damage_allowed =
      display_plan->compositor_damage_allowed ? 1 : 0;
  out->damage_allowed = display_plan->damage_allowed ? 1 : 0;
  out->damage_ticket_selected = display_plan->damage_ticket_selected ? 1 : 0;
  out->damage_target_selected = display_plan->damage_target_selected ? 1 : 0;
  out->damage_incremental_allowed =
      display_plan->damage_incremental_allowed ? 1 : 0;
  out->full_damage_required = display_plan->full_damage_required ? 1 : 0;
  out->damage_cache_allowed = display_plan->damage_cache_allowed ? 1 : 0;
  out->damage_reuse_allowed = display_plan->damage_reuse_allowed ? 1 : 0;
  out->present_allowed = display_plan->present_allowed ? 1 : 0;
  out->present_ticket_selected = display_plan->present_ticket_selected ? 1 : 0;
  out->present_target_selected = display_plan->present_target_selected ? 1 : 0;
  out->present_incremental_allowed =
      display_plan->present_incremental_allowed ? 1 : 0;
  out->full_present_required = display_plan->full_present_required ? 1 : 0;
  out->present_cache_allowed = display_plan->present_cache_allowed ? 1 : 0;
  out->present_reuse_allowed = display_plan->present_reuse_allowed ? 1 : 0;
  out->schedule_allowed = display_plan->schedule_allowed ? 1 : 0;
  out->schedule_ticket_selected =
      display_plan->schedule_ticket_selected ? 1 : 0;
  out->schedule_target_selected =
      display_plan->schedule_target_selected ? 1 : 0;
  out->schedule_incremental_allowed =
      display_plan->schedule_incremental_allowed ? 1 : 0;
  out->full_schedule_required = display_plan->full_schedule_required ? 1 : 0;
  out->schedule_cache_allowed = display_plan->schedule_cache_allowed ? 1 : 0;
  out->schedule_reuse_allowed = display_plan->schedule_reuse_allowed ? 1 : 0;
  out->frame_pacing_allowed = 1;
  out->vsync_allowed = display_plan->vsync_allowed ? 1 : 0;
  out->vsync_ticket_selected = display_plan->vsync_ticket_selected ? 1 : 0;
  out->vsync_target_selected = display_plan->vsync_target_selected ? 1 : 0;
  out->scanout_allowed = display_plan->scanout_allowed ? 1 : 0;
  out->scanout_ticket_selected = display_plan->scanout_ticket_selected ? 1 : 0;
  out->scanout_target_selected = display_plan->scanout_target_selected ? 1 : 0;
  out->display_allowed = display_plan->display_allowed ? 1 : 0;
  out->display_ticket_selected = display_plan->display_ticket_selected ? 1 : 0;
  out->display_target_selected = display_plan->display_target_selected ? 1 : 0;
  out->output_allowed = 1;
  out->output_ticket_selected = 1;
  out->output_target_selected = 1;
  out->damage_error = 0;
  out->present_error = 0;
  out->schedule_error = 0;
  out->vsync_error = 0;
  out->scanout_error = 0;
  out->display_error = 0;
  out->output_error = 0;
  out->recovery_text_session_required =
      display_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = display_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required =
      display_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = display_plan->submit_requested ? 1 : 0;
  out->submit_blocked = 1;
  out->text_login_authoritative = 1;
  out->view = display_plan->view;
  out->widget_tree = display_plan->widget_tree;
  out->compaction_ticket = display_plan->compaction_ticket;
  out->reclaim_ticket = display_plan->reclaim_ticket;
  out->release_ticket = display_plan->release_ticket;
  out->gui_ticket = display_plan->gui_ticket;
  out->window_ticket = display_plan->window_ticket;
  out->surface_ticket = display_plan->surface_ticket;
  out->compositor_ticket = display_plan->compositor_ticket;
  out->damage_ticket = display_plan->damage_ticket;
  out->present_ticket = display_plan->present_ticket;
  out->schedule_ticket = display_plan->schedule_ticket;
  out->vsync_ticket = display_plan->vsync_ticket;
  out->scanout_ticket = display_plan->scanout_ticket;
  out->display_ticket = display_plan->display_ticket;
  out->focus_target = display_plan->focus_target;
  out->primary_action = display_plan->primary_action;
  out->route = display_plan->route;
  out->compositor_target = display_plan->compositor_target;
  out->compaction_policy = display_plan->compaction_policy;
  out->reclaim_policy = display_plan->reclaim_policy;
  out->release_policy = display_plan->release_policy;
  out->gui_policy = display_plan->gui_policy;
  out->window_policy = display_plan->window_policy;
  out->surface_policy = display_plan->surface_policy;
  out->compositor_policy = display_plan->compositor_policy;
  out->damage_policy = display_plan->damage_policy;
  out->cache_policy = display_plan->cache_policy;
  out->present_policy = display_plan->present_policy;
  out->schedule_policy = display_plan->schedule_policy;
  out->vsync_policy = display_plan->vsync_policy;
  out->scanout_policy = display_plan->scanout_policy;
  out->display_policy = display_plan->display_policy;
  out->output_policy = out->schedule_incremental_allowed
                          ? "incremental-window-output-declarative"
                          : "full-window-output-declarative";
  out->event_type = "credential-screen-window-output-plan-ready";
  out->state = "window-output-ready";
  out->message =
      "Credential screen window output ticket ready; no connector, mode or signal armed.";
  out->blocked_reason = display_plan->blocked_reason;
  if (display_plan->submit_requested) {
    out->output_ticket = "text-login-fallback-window-output-ticket";
    out->compositor_target = "text-login-fallback-window-output";
    out->output_policy = "fallback-window-output-declarative";
    out->output_text_login = 1;
    out->output_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "window-output-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (display_plan->display_credential_panel &&
      display_plan->display_credential_input &&
      display_plan->display_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->output_ticket = "credential-screen-window-output-ticket";
    out->compositor_target = "credential-screen-window-output";
    out->output_credential_panel = 1;
    out->output_credential_input = 1;
    out->output_credential_focus = 1;
    out->output_text_login = 0;
    out->output_text_login_fallback = 0;
    out->state = "window-output-credential-ready";
    out->message =
        "Credential window output ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (display_plan->display_text_recovery &&
      out->recovery_text_session_required) {
    out->output_ticket = "text-recovery-window-output-ticket";
    out->compositor_target = "text-recovery-window-output";
    out->output_text_recovery = 1;
    out->output_text_login = 1;
    out->output_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "window-output-text-recovery-ready";
    out->message =
        "Text recovery window output ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (display_plan->display_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->output_ticket = "text-login-resume-window-output-ticket";
    out->compositor_target = "text-login-resume-window-output";
    out->output_policy = "full-window-output-declarative";
    out->cache_policy = "window-output-cache-bypassed-for-rerender";
    out->output_text_login = 1;
    out->output_text_login_resume = 1;
    out->output_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "window-output-resume-ready";
    out->message =
        "Text login resume window output ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->output_ticket = "text-login-fallback-window-output-ticket";
  out->compositor_target = "text-login-fallback-window-output";
  out->output_policy = "fallback-window-output-declarative";
  out->output_text_login = 1;
  out->output_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "window-output-text-login-ready";
  out->message = "Use authoritative text login.";
  out->blocked_reason = "ready";
  return 0;
}
