/*
 * src/auth/login_runtime/window_display_plan.c
 *
 * Credential-screen window-display plan reset + builder —
 * extracted byte-for-byte from `src/auth/login_runtime.c` during PR
 * C.56 of the Estagio C dedicated plan.  Hosts the per-plan reset
 * helper and the public builder for the window-display stage of
 * the credential pipeline:
 *
 *   - login_window_credential_screen_window_display_plan_reset (static)
 *   - login_window_credential_screen_window_display_plan_build
 *
 * The window-display-plan converts a fail-closed window-scanout-
 * plan into a window-display contract for the downstream window-
 * output stage.  The static `_reset` helper is the canonical
 * "blocked" initializer used when the upstream contract is missing
 * or unsafe.  Both helpers are kept file-local so they cannot be
 * reused outside this translation unit; the public builder is the
 * only entry point.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_window_display_plan_reset(
    struct login_window_credential_screen_window_display_plan *out,
    int window_scanout_plan_available) {
  *out = (struct login_window_credential_screen_window_display_plan){0};
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_WINDOW_DISPLAY_PLAN_VERSION;
  out->window_scanout_plan_available = window_scanout_plan_available ? 1 : 0;
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
  out->display_text_login = 1;
  out->display_text_login_fallback = 1;
  out->display_error = 1;
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
  out->cache_policy = "window-display-cache-disabled";
  out->present_policy = "present-disabled";
  out->schedule_policy = "schedule-disabled";
  out->vsync_policy = "vsync-disabled";
  out->scanout_policy = "scanout-disabled";
  out->display_policy = "display-disabled";
  out->event_type = "credential-screen-window-display-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "window-scanout-plan-unavailable";
}

int login_window_credential_screen_window_display_plan_build(
    const struct login_window_credential_screen_window_scanout_plan *scanout_plan,
    struct login_window_credential_screen_window_display_plan *out) {
  int safe = 0;
  if (!out) return -1;
  login_window_credential_screen_window_display_plan_reset(
      out, scanout_plan ? 1 : 0);
  if (!scanout_plan) return 0;
  out->requested_action = scanout_plan->requested_action;
  out->window_vsync_plan_available =
      scanout_plan->window_vsync_plan_available ? 1 : 0;
  out->window_schedule_plan_available =
      scanout_plan->window_schedule_plan_available ? 1 : 0;
  out->window_present_plan_available =
      scanout_plan->window_present_plan_available ? 1 : 0;
  out->window_damage_plan_available =
      scanout_plan->window_damage_plan_available ? 1 : 0;
  out->window_compositor_plan_available =
      scanout_plan->window_compositor_plan_available ? 1 : 0;
  out->window_surface_plan_available =
      scanout_plan->window_surface_plan_available ? 1 : 0;
  out->window_surface_plan_safe =
      scanout_plan->window_surface_plan_safe ? 1 : 0;
  out->window_compositor_plan_safe =
      scanout_plan->window_compositor_plan_safe ? 1 : 0;
  out->window_damage_plan_safe =
      scanout_plan->window_damage_plan_safe ? 1 : 0;
  out->window_present_plan_safe =
      scanout_plan->window_present_plan_safe ? 1 : 0;
  out->window_schedule_plan_safe =
      scanout_plan->window_schedule_plan_safe ? 1 : 0;
  out->window_vsync_plan_safe =
      scanout_plan->window_vsync_plan_safe ? 1 : 0;
  out->window_scanout_plan_safe =
      scanout_plan->window_scanout_plan_safe ? 1 : 0;
  safe = scanout_plan->window_scanout_plan_safe &&
         scanout_plan->window_vsync_plan_safe &&
         scanout_plan->window_schedule_plan_safe &&
         scanout_plan->window_present_plan_safe &&
         scanout_plan->window_damage_plan_safe &&
         scanout_plan->window_compositor_plan_safe &&
         scanout_plan->window_surface_plan_safe &&
         scanout_plan->window_vsync_plan_available &&
         scanout_plan->window_schedule_plan_available &&
         scanout_plan->window_present_plan_available &&
         scanout_plan->window_damage_plan_available &&
         scanout_plan->window_compositor_plan_available &&
         scanout_plan->window_surface_plan_available &&
         scanout_plan->scanout_required && scanout_plan->scanout_allowed &&
         !scanout_plan->scanout_submitted &&
         scanout_plan->scanout_ticket_selected &&
         scanout_plan->scanout_target_selected &&
         !scanout_plan->scanout_buffer_attached &&
         !scanout_plan->scanout_buffer_submitted &&
         !scanout_plan->scanout_display_flip_allowed &&
         !scanout_plan->scanout_display_flip_submitted &&
         !scanout_plan->scanout_error &&
         scanout_plan->vsync_required && scanout_plan->vsync_allowed &&
         !scanout_plan->vsync_submitted &&
         scanout_plan->vsync_ticket_selected &&
         scanout_plan->vsync_target_selected &&
         !scanout_plan->vsync_wait_allowed &&
         !scanout_plan->vsync_wait_submitted &&
         scanout_plan->vsync_fence_required &&
         !scanout_plan->vsync_fence_armed && !scanout_plan->vsync_error &&
         scanout_plan->schedule_required && scanout_plan->schedule_allowed &&
         !scanout_plan->schedule_submitted &&
         scanout_plan->schedule_ticket_selected &&
         scanout_plan->schedule_target_selected &&
         !scanout_plan->schedule_cache_hit &&
         !scanout_plan->schedule_auth_submit_allowed &&
         !scanout_plan->schedule_auth_attempt_allowed &&
         !scanout_plan->schedule_error &&
         scanout_plan->frame_pacing_required &&
         scanout_plan->frame_pacing_allowed &&
         !scanout_plan->frame_timer_armed &&
         !scanout_plan->compositor_wake_allowed &&
         !scanout_plan->compositor_wake_submitted &&
         !scanout_plan->page_flip_allowed &&
         !scanout_plan->page_flip_submitted &&
         scanout_plan->present_required && scanout_plan->present_allowed &&
         !scanout_plan->present_submitted &&
         scanout_plan->present_ticket_selected &&
         scanout_plan->present_target_selected &&
         !scanout_plan->present_cache_hit &&
         !scanout_plan->present_auth_submit_allowed &&
         !scanout_plan->present_auth_attempt_allowed &&
         !scanout_plan->present_error && scanout_plan->damage_required &&
         scanout_plan->damage_allowed && !scanout_plan->damage_submitted &&
         scanout_plan->damage_ticket_selected &&
         scanout_plan->damage_target_selected &&
         !scanout_plan->damage_cache_hit &&
         !scanout_plan->damage_auth_submit_allowed &&
         !scanout_plan->damage_auth_attempt_allowed &&
         !scanout_plan->damage_error &&
         scanout_plan->compositor_required &&
         scanout_plan->compositor_allowed &&
         !scanout_plan->compositor_submitted &&
         scanout_plan->compositor_ticket_selected &&
         scanout_plan->compositor_target_selected &&
         scanout_plan->compositor_surface_allowed &&
         !scanout_plan->compositor_surface_submitted &&
         scanout_plan->compositor_damage_planned &&
         scanout_plan->compositor_damage_allowed &&
         !scanout_plan->compositor_damage_submitted &&
         !scanout_plan->compositor_auth_submit_allowed &&
         !scanout_plan->compositor_auth_attempt_allowed &&
         scanout_plan->surface_required && scanout_plan->surface_allowed &&
         !scanout_plan->surface_bound &&
         scanout_plan->surface_ticket_selected &&
         scanout_plan->surface_target_selected &&
         !scanout_plan->surface_memory_mapped &&
         !scanout_plan->surface_pixels_written &&
         !scanout_plan->surface_compositor_submit_allowed &&
         !scanout_plan->surface_compositor_submitted &&
         !scanout_plan->surface_auth_submit_allowed &&
         !scanout_plan->surface_auth_attempt_allowed &&
         scanout_plan->window_required && scanout_plan->window_allowed &&
         !scanout_plan->window_created &&
         scanout_plan->window_ticket_selected &&
         scanout_plan->window_target_selected &&
         !scanout_plan->window_surface_bound &&
         !scanout_plan->window_input_bound &&
         !scanout_plan->window_auth_submit_allowed &&
         !scanout_plan->window_auth_attempt_allowed &&
         scanout_plan->gui_required && scanout_plan->gui_allowed &&
         !scanout_plan->gui_submitted &&
         scanout_plan->gui_ticket_selected &&
         scanout_plan->gui_target_selected &&
         !scanout_plan->gui_pixels_write_allowed &&
         !scanout_plan->gui_pixels_written &&
         !scanout_plan->gui_auth_submit_allowed &&
         !scanout_plan->gui_auth_attempt_allowed &&
         scanout_plan->release_required && scanout_plan->release_allowed &&
         !scanout_plan->release_submitted &&
         scanout_plan->release_ticket_selected &&
         scanout_plan->release_target_selected &&
         !scanout_plan->release_storage_prune_allowed &&
         !scanout_plan->release_storage_pruned &&
         !scanout_plan->release_resource_release_allowed &&
         !scanout_plan->release_resource_released &&
         !scanout_plan->release_cpu_gpu_sync_allowed &&
         !scanout_plan->release_cpu_gpu_sync_submitted &&
         scanout_plan->reclaim_required && scanout_plan->reclaim_allowed &&
         !scanout_plan->reclaim_submitted &&
         scanout_plan->reclaim_ticket_selected &&
         scanout_plan->reclaim_target_selected &&
         !scanout_plan->reclaim_storage_prune_allowed &&
         !scanout_plan->reclaim_storage_pruned &&
         !scanout_plan->reclaim_resource_release_allowed &&
         !scanout_plan->reclaim_resource_released &&
         !scanout_plan->reclaim_cpu_gpu_sync_allowed &&
         !scanout_plan->reclaim_cpu_gpu_sync_submitted &&
         scanout_plan->compaction_required &&
         scanout_plan->compaction_allowed &&
         !scanout_plan->compaction_submitted &&
         scanout_plan->compaction_ticket_selected &&
         scanout_plan->compaction_target_selected &&
         !scanout_plan->compaction_storage_write_allowed &&
         !scanout_plan->compaction_storage_written &&
         !scanout_plan->compaction_resource_release_allowed &&
         !scanout_plan->compaction_resource_released &&
         !scanout_plan->compaction_cpu_gpu_sync_allowed &&
         !scanout_plan->compaction_cpu_gpu_sync_submitted &&
         scanout_plan->route_selected && !scanout_plan->route_blocked &&
         scanout_plan->credential_session_safe &&
         scanout_plan->credential_storage_wiped &&
         scanout_plan->credential_redacted && scanout_plan->length_redacted &&
         !scanout_plan->raw_secret_exposed &&
         !scanout_plan->masked_text_exposed && scanout_plan->submit_blocked &&
         !scanout_plan->submit_enabled && !scanout_plan->auth_attempt_allowed &&
         !scanout_plan->submit_callback_bound &&
         !scanout_plan->auth_callback_bound &&
         scanout_plan->text_login_authoritative;
  if (!safe) {
    out->event_type = "credential-screen-window-display-plan-unsafe";
    out->blocked_reason = "credential-window-display-plan-unsafe";
    out->message =
        "Credential screen window display plan unsafe; use text login.";
    return 0;
  }
  out->window_display_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = scanout_plan->action_allowed ? 1 : 0;
  out->action_blocked = scanout_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = scanout_plan->input_focus_allowed ? 1 : 0;
  out->compaction_allowed = scanout_plan->compaction_allowed ? 1 : 0;
  out->compaction_ticket_selected =
      scanout_plan->compaction_ticket_selected ? 1 : 0;
  out->compaction_target_selected =
      scanout_plan->compaction_target_selected ? 1 : 0;
  out->reclaim_allowed = scanout_plan->reclaim_allowed ? 1 : 0;
  out->reclaim_ticket_selected = scanout_plan->reclaim_ticket_selected ? 1 : 0;
  out->reclaim_target_selected = scanout_plan->reclaim_target_selected ? 1 : 0;
  out->release_allowed = scanout_plan->release_allowed ? 1 : 0;
  out->release_ticket_selected = scanout_plan->release_ticket_selected ? 1 : 0;
  out->release_target_selected = scanout_plan->release_target_selected ? 1 : 0;
  out->gui_allowed = scanout_plan->gui_allowed ? 1 : 0;
  out->gui_ticket_selected = scanout_plan->gui_ticket_selected ? 1 : 0;
  out->gui_target_selected = scanout_plan->gui_target_selected ? 1 : 0;
  out->window_allowed = scanout_plan->window_allowed ? 1 : 0;
  out->window_ticket_selected = scanout_plan->window_ticket_selected ? 1 : 0;
  out->window_target_selected = scanout_plan->window_target_selected ? 1 : 0;
  out->surface_allowed = scanout_plan->surface_allowed ? 1 : 0;
  out->surface_ticket_selected = scanout_plan->surface_ticket_selected ? 1 : 0;
  out->surface_target_selected = scanout_plan->surface_target_selected ? 1 : 0;
  out->compositor_allowed = scanout_plan->compositor_allowed ? 1 : 0;
  out->compositor_ticket_selected =
      scanout_plan->compositor_ticket_selected ? 1 : 0;
  out->compositor_target_selected =
      scanout_plan->compositor_target_selected ? 1 : 0;
  out->compositor_surface_allowed =
      scanout_plan->compositor_surface_allowed ? 1 : 0;
  out->compositor_damage_planned =
      scanout_plan->compositor_damage_planned ? 1 : 0;
  out->compositor_damage_allowed =
      scanout_plan->compositor_damage_allowed ? 1 : 0;
  out->damage_allowed = scanout_plan->damage_allowed ? 1 : 0;
  out->damage_ticket_selected = scanout_plan->damage_ticket_selected ? 1 : 0;
  out->damage_target_selected = scanout_plan->damage_target_selected ? 1 : 0;
  out->damage_incremental_allowed =
      scanout_plan->damage_incremental_allowed ? 1 : 0;
  out->full_damage_required = scanout_plan->full_damage_required ? 1 : 0;
  out->damage_cache_allowed = scanout_plan->damage_cache_allowed ? 1 : 0;
  out->damage_reuse_allowed = scanout_plan->damage_reuse_allowed ? 1 : 0;
  out->present_allowed = scanout_plan->present_allowed ? 1 : 0;
  out->present_ticket_selected = scanout_plan->present_ticket_selected ? 1 : 0;
  out->present_target_selected = scanout_plan->present_target_selected ? 1 : 0;
  out->present_incremental_allowed =
      scanout_plan->present_incremental_allowed ? 1 : 0;
  out->full_present_required = scanout_plan->full_present_required ? 1 : 0;
  out->present_cache_allowed = scanout_plan->present_cache_allowed ? 1 : 0;
  out->present_reuse_allowed = scanout_plan->present_reuse_allowed ? 1 : 0;
  out->schedule_allowed = scanout_plan->schedule_allowed ? 1 : 0;
  out->schedule_ticket_selected =
      scanout_plan->schedule_ticket_selected ? 1 : 0;
  out->schedule_target_selected =
      scanout_plan->schedule_target_selected ? 1 : 0;
  out->schedule_incremental_allowed =
      scanout_plan->schedule_incremental_allowed ? 1 : 0;
  out->full_schedule_required = scanout_plan->full_schedule_required ? 1 : 0;
  out->schedule_cache_allowed = scanout_plan->schedule_cache_allowed ? 1 : 0;
  out->schedule_reuse_allowed = scanout_plan->schedule_reuse_allowed ? 1 : 0;
  out->frame_pacing_allowed = 1;
  out->vsync_allowed = scanout_plan->vsync_allowed ? 1 : 0;
  out->vsync_ticket_selected = scanout_plan->vsync_ticket_selected ? 1 : 0;
  out->vsync_target_selected = scanout_plan->vsync_target_selected ? 1 : 0;
  out->scanout_allowed = scanout_plan->scanout_allowed ? 1 : 0;
  out->scanout_ticket_selected = scanout_plan->scanout_ticket_selected ? 1 : 0;
  out->scanout_target_selected = scanout_plan->scanout_target_selected ? 1 : 0;
  out->display_allowed = 1;
  out->display_ticket_selected = 1;
  out->display_target_selected = 1;
  out->damage_error = 0;
  out->present_error = 0;
  out->schedule_error = 0;
  out->vsync_error = 0;
  out->scanout_error = 0;
  out->display_error = 0;
  out->recovery_text_session_required =
      scanout_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = scanout_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required =
      scanout_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = scanout_plan->submit_requested ? 1 : 0;
  out->submit_blocked = 1;
  out->text_login_authoritative = 1;
  out->view = scanout_plan->view;
  out->widget_tree = scanout_plan->widget_tree;
  out->compaction_ticket = scanout_plan->compaction_ticket;
  out->reclaim_ticket = scanout_plan->reclaim_ticket;
  out->release_ticket = scanout_plan->release_ticket;
  out->gui_ticket = scanout_plan->gui_ticket;
  out->window_ticket = scanout_plan->window_ticket;
  out->surface_ticket = scanout_plan->surface_ticket;
  out->compositor_ticket = scanout_plan->compositor_ticket;
  out->damage_ticket = scanout_plan->damage_ticket;
  out->present_ticket = scanout_plan->present_ticket;
  out->schedule_ticket = scanout_plan->schedule_ticket;
  out->vsync_ticket = scanout_plan->vsync_ticket;
  out->scanout_ticket = scanout_plan->scanout_ticket;
  out->focus_target = scanout_plan->focus_target;
  out->primary_action = scanout_plan->primary_action;
  out->route = scanout_plan->route;
  out->compositor_target = scanout_plan->compositor_target;
  out->compaction_policy = scanout_plan->compaction_policy;
  out->reclaim_policy = scanout_plan->reclaim_policy;
  out->release_policy = scanout_plan->release_policy;
  out->gui_policy = scanout_plan->gui_policy;
  out->window_policy = scanout_plan->window_policy;
  out->surface_policy = scanout_plan->surface_policy;
  out->compositor_policy = scanout_plan->compositor_policy;
  out->damage_policy = scanout_plan->damage_policy;
  out->cache_policy = scanout_plan->cache_policy;
  out->present_policy = scanout_plan->present_policy;
  out->schedule_policy = scanout_plan->schedule_policy;
  out->vsync_policy = scanout_plan->vsync_policy;
  out->scanout_policy = scanout_plan->scanout_policy;
  out->display_policy = out->schedule_incremental_allowed
                            ? "incremental-window-display-declarative"
                            : "full-window-display-declarative";
  out->event_type = "credential-screen-window-display-plan-ready";
  out->state = "window-display-ready";
  out->message =
      "Credential screen window display ticket ready; no controller, output or pipeline attached.";
  out->blocked_reason = scanout_plan->blocked_reason;
  if (scanout_plan->submit_requested) {
    out->display_ticket = "text-login-fallback-window-display-ticket";
    out->compositor_target = "text-login-fallback-window-display";
    out->display_policy = "fallback-window-display-declarative";
    out->display_text_login = 1;
    out->display_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "window-display-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (scanout_plan->scanout_credential_panel &&
      scanout_plan->scanout_credential_input &&
      scanout_plan->scanout_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->display_ticket = "credential-screen-window-display-ticket";
    out->compositor_target = "credential-screen-window-display";
    out->display_credential_panel = 1;
    out->display_credential_input = 1;
    out->display_credential_focus = 1;
    out->display_text_login = 0;
    out->display_text_login_fallback = 0;
    out->state = "window-display-credential-ready";
    out->message =
        "Credential window display ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (scanout_plan->scanout_text_recovery &&
      out->recovery_text_session_required) {
    out->display_ticket = "text-recovery-window-display-ticket";
    out->compositor_target = "text-recovery-window-display";
    out->display_text_recovery = 1;
    out->display_text_login = 1;
    out->display_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "window-display-text-recovery-ready";
    out->message =
        "Text recovery window display ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (scanout_plan->scanout_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->display_ticket = "text-login-resume-window-display-ticket";
    out->compositor_target = "text-login-resume-window-display";
    out->display_policy = "full-window-display-declarative";
    out->cache_policy = "window-display-cache-bypassed-for-rerender";
    out->display_text_login = 1;
    out->display_text_login_resume = 1;
    out->display_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "window-display-resume-ready";
    out->message =
        "Text login resume window display ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->display_ticket = "text-login-fallback-window-display-ticket";
  out->compositor_target = "text-login-fallback-window-display";
  out->display_policy = "fallback-window-display-declarative";
  out->display_text_login = 1;
  out->display_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "window-display-text-login-ready";
  out->message = "Use authoritative text login.";
  out->blocked_reason = "ready";
  return 0;
}
