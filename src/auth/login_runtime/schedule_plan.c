/*
 * src/auth/login_runtime/schedule_plan.c
 *
 * Credential-screen schedule plan reset + builder — extracted
 * byte-for-byte from `src/auth/login_runtime.c` during PR C.16 of
 * the Estagio C dedicated plan.  Hosts the per-plan reset helper
 * and the public builder for the schedule stage of the credential
 * pipeline:
 *
 *   - login_window_credential_screen_schedule_plan_reset (static)
 *   - login_window_credential_screen_schedule_plan_build
 *
 * The schedule-plan converts a fail-closed present-plan into a
 * scheduling contract for the downstream vsync/scanout stages.  The
 * static `_reset` helper is the canonical "blocked" initializer
 * shared between the build path and any future internal callers.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_schedule_plan_reset(
    struct login_window_credential_screen_schedule_plan *out,
    int present_plan_available) {
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_SCHEDULE_PLAN_VERSION;
  out->present_plan_available = present_plan_available ? 1 : 0;
  out->present_plan_safe = 0;
  out->schedule_plan_safe = 0;
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
  out->schedule_credential_panel = 0;
  out->schedule_credential_input = 0;
  out->schedule_credential_focus = 0;
  out->schedule_text_recovery = 0;
  out->schedule_text_login = 1;
  out->schedule_text_login_resume = 0;
  out->schedule_text_login_fallback = 1;
  out->schedule_error = 1;
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
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->compositor_target = "none";
  out->damage_policy = "full-safe-fallback";
  out->cache_policy = "cache-disabled";
  out->present_policy = "present-disabled";
  out->schedule_policy = "schedule-disabled";
  out->event_type = "credential-screen-schedule-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "present-plan-unavailable";
}

int login_window_credential_screen_schedule_plan_build(
    const struct login_window_credential_screen_present_plan *present_plan,
    struct login_window_credential_screen_schedule_plan *out) {
  int safe = 0;
  if (!out) return -1;
  login_window_credential_screen_schedule_plan_reset(out, present_plan ? 1 : 0);
  if (!present_plan) return 0;
  out->requested_action = present_plan->requested_action;
  out->present_plan_safe = present_plan->present_plan_safe ? 1 : 0;
  safe = present_plan->present_plan_safe && present_plan->present_required &&
         present_plan->present_allowed && !present_plan->present_submitted &&
         present_plan->present_ticket_selected && !present_plan->present_error &&
         present_plan->damage_required && present_plan->damage_allowed &&
         !present_plan->damage_submitted && present_plan->compositor_surface_allowed &&
         !present_plan->compositor_surface_submitted &&
         present_plan->compositor_damage_planned &&
         present_plan->compositor_damage_allowed &&
         !present_plan->compositor_damage_submitted && present_plan->route_selected &&
         !present_plan->route_blocked && present_plan->credential_session_safe &&
         present_plan->credential_storage_wiped && present_plan->credential_redacted &&
         present_plan->length_redacted && !present_plan->raw_secret_exposed &&
         !present_plan->masked_text_exposed && present_plan->submit_blocked &&
         !present_plan->submit_enabled && !present_plan->auth_attempt_allowed &&
         !present_plan->submit_callback_bound && !present_plan->auth_callback_bound &&
         present_plan->text_login_authoritative;
  if (!safe) {
    out->event_type = "credential-screen-schedule-plan-unsafe";
    out->blocked_reason = "credential-schedule-plan-unsafe";
    out->message = "Credential screen schedule plan unsafe; use text login.";
    return 0;
  }
  out->schedule_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = present_plan->action_allowed ? 1 : 0;
  out->action_blocked = present_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = present_plan->input_focus_allowed ? 1 : 0;
  out->compositor_surface_allowed = 1;
  out->compositor_surface_submitted = 0;
  out->compositor_damage_planned = 1;
  out->compositor_damage_allowed = 1;
  out->compositor_damage_submitted = 0;
  out->damage_required = present_plan->damage_required ? 1 : 0;
  out->damage_allowed = present_plan->damage_allowed ? 1 : 0;
  out->damage_incremental_allowed = present_plan->damage_incremental_allowed ? 1 : 0;
  out->full_damage_required = present_plan->full_damage_required ? 1 : 0;
  out->damage_cache_allowed = present_plan->damage_cache_allowed ? 1 : 0;
  out->damage_reuse_allowed = present_plan->damage_reuse_allowed ? 1 : 0;
  out->present_required = 1;
  out->present_allowed = 1;
  out->present_submitted = 0;
  out->present_ticket_selected = 1;
  out->present_incremental_allowed = present_plan->present_incremental_allowed ? 1 : 0;
  out->full_present_required = present_plan->full_present_required ? 1 : 0;
  out->present_cache_allowed = present_plan->present_cache_allowed ? 1 : 0;
  out->present_reuse_allowed = present_plan->present_reuse_allowed ? 1 : 0;
  out->schedule_allowed = 1;
  out->schedule_submitted = 0;
  out->schedule_ticket_selected = 1;
  out->schedule_incremental_allowed = present_plan->present_incremental_allowed ? 1 : 0;
  out->full_schedule_required = present_plan->full_present_required ? 1 : 0;
  out->schedule_cache_allowed = present_plan->present_cache_allowed ? 1 : 0;
  out->schedule_cache_hit = 0;
  out->schedule_reuse_allowed = present_plan->present_reuse_allowed ? 1 : 0;
  out->frame_pacing_allowed = 1;
  out->schedule_error = 0;
  out->recovery_text_session_required = present_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = present_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = present_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = present_plan->submit_requested ? 1 : 0;
  out->text_login_authoritative = 1;
  out->view = present_plan->view;
  out->widget_tree = present_plan->widget_tree;
  out->damage_ticket = present_plan->damage_ticket;
  out->present_ticket = present_plan->present_ticket;
  out->focus_target = present_plan->focus_target;
  out->primary_action = present_plan->primary_action;
  out->route = present_plan->route;
  out->compositor_target = present_plan->compositor_target;
  out->damage_policy = present_plan->damage_policy;
  out->cache_policy = present_plan->cache_policy;
  out->present_policy = present_plan->present_policy;
  out->schedule_policy = out->schedule_incremental_allowed ?
      "incremental-schedule-declarative" : "full-schedule-declarative";
  out->event_type = "credential-screen-schedule-plan-ready";
  out->state = "schedule-ready";
  out->message = "Credential screen schedule ticket ready; no frame scheduled.";
  out->blocked_reason = present_plan->blocked_reason;
  if (present_plan->submit_requested) {
    out->schedule_ticket = "text-login-fallback-schedule-ticket";
    out->compositor_target = "text-login-fallback-schedule";
    out->damage_policy = "fallback-declarative";
    out->present_policy = "fallback-present-declarative";
    out->schedule_policy = "fallback-schedule-declarative";
    out->schedule_text_login = 1;
    out->schedule_text_login_fallback = 1;
    out->full_schedule_required = 1;
    out->schedule_incremental_allowed = 0;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "schedule-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (present_plan->present_credential_panel && present_plan->present_credential_input &&
      present_plan->present_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->schedule_ticket = "credential-screen-schedule-ticket";
    out->compositor_target = "credential-screen-schedule";
    out->schedule_credential_panel = 1;
    out->schedule_credential_input = 1;
    out->schedule_credential_focus = 1;
    out->schedule_text_login = 0;
    out->schedule_text_login_fallback = 0;
    out->state = "schedule-credential-ready";
    out->message = "Credential schedule ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (present_plan->present_text_recovery && out->recovery_text_session_required) {
    out->schedule_ticket = "text-recovery-schedule-ticket";
    out->compositor_target = "text-recovery-schedule";
    out->schedule_text_recovery = 1;
    out->schedule_text_login = 1;
    out->schedule_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "schedule-text-recovery-ready";
    out->message = "Text recovery schedule ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (present_plan->present_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->schedule_ticket = "text-login-resume-schedule-ticket";
    out->compositor_target = "text-login-resume-schedule";
    out->damage_policy = "full-rerender-declarative";
    out->cache_policy = "cache-bypassed-for-rerender";
    out->present_policy = "full-present-declarative";
    out->schedule_policy = "full-schedule-declarative";
    out->schedule_reuse_allowed = 0;
    out->schedule_cache_allowed = 0;
    out->full_schedule_required = 1;
    out->schedule_incremental_allowed = 0;
    out->schedule_text_login = 1;
    out->schedule_text_login_resume = 1;
    out->schedule_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "schedule-resume-ready";
    out->message = "Text login resume schedule ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->schedule_ticket = "text-login-fallback-schedule-ticket";
  out->compositor_target = "text-login-fallback-schedule";
  out->damage_policy = "fallback-declarative";
  out->present_policy = "fallback-present-declarative";
  out->schedule_policy = "fallback-schedule-declarative";
  out->schedule_text_login = 1;
  out->schedule_text_login_fallback = 1;
  out->full_schedule_required = 1;
  out->schedule_incremental_allowed = 0;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "schedule-text-login-ready";
  out->message = "Use authoritative text login.";
  return 0;
}
