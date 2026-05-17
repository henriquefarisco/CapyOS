/*
 * src/auth/login_runtime/present_plan.c
 *
 * Credential-screen present plan builder — extracted byte-for-byte
 * from `src/auth/login_runtime.c` during PR C.15 of the Estagio C
 * dedicated plan.  Hosts the solo pipeline builder that turns the
 * damage-plan into the present-plan (the contract that drives the
 * downstream schedule/vsync/scanout chain):
 *
 *   - login_window_credential_screen_present_plan_build
 *
 * The present-plan owns the decision to advance the credential
 * screen to the next visible frame.  It is fail-closed: if the
 * upstream damage-plan is missing or unsafe the present-plan is
 * marked blocked with an explicit reason.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

int login_window_credential_screen_present_plan_build(
    const struct login_window_credential_screen_damage_plan *damage_plan,
    struct login_window_credential_screen_present_plan *out) {
  int damage_safe = 0;
  if (!out) return -1;
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_PRESENT_PLAN_VERSION;
  out->damage_plan_available = damage_plan ? 1 : 0;
  out->damage_plan_safe = 0;
  out->present_plan_safe = 0;
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
  out->present_credential_panel = 0;
  out->present_credential_input = 0;
  out->present_credential_focus = 0;
  out->present_text_recovery = 0;
  out->present_text_login = 1;
  out->present_text_login_resume = 0;
  out->present_text_login_fallback = 1;
  out->present_error = 1;
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
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->compositor_target = "none";
  out->damage_policy = "full-safe-fallback";
  out->cache_policy = "cache-disabled";
  out->present_policy = "present-disabled";
  out->event_type = "credential-screen-present-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "damage-plan-unavailable";
  if (!damage_plan) return 0;
  out->requested_action = damage_plan->requested_action;
  out->damage_plan_safe = damage_plan->damage_plan_safe ? 1 : 0;
  out->route_selected = damage_plan->route_selected ? 1 : 0;
  out->route_blocked = damage_plan->route_blocked ? 1 : 0;
  out->action_allowed = damage_plan->action_allowed ? 1 : 0;
  out->action_blocked = damage_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = damage_plan->input_focus_allowed ? 1 : 0;
  out->compositor_surface_allowed = damage_plan->compositor_surface_allowed ? 1 : 0;
  out->compositor_surface_submitted = damage_plan->compositor_surface_submitted ? 1 : 0;
  out->compositor_damage_planned = damage_plan->compositor_damage_planned ? 1 : 0;
  out->compositor_damage_allowed = damage_plan->compositor_damage_allowed ? 1 : 0;
  out->compositor_damage_submitted = damage_plan->compositor_damage_submitted ? 1 : 0;
  out->damage_required = damage_plan->damage_required ? 1 : 0;
  out->damage_allowed = damage_plan->damage_allowed ? 1 : 0;
  out->damage_submitted = damage_plan->damage_submitted ? 1 : 0;
  out->damage_incremental_allowed = damage_plan->damage_incremental_allowed ? 1 : 0;
  out->full_damage_required = damage_plan->full_damage_required ? 1 : 0;
  out->damage_cache_allowed = damage_plan->damage_cache_allowed ? 1 : 0;
  out->damage_cache_hit = damage_plan->damage_cache_hit ? 1 : 0;
  out->damage_reuse_allowed = damage_plan->damage_reuse_allowed ? 1 : 0;
  out->present_ticket_selected = damage_plan->damage_ticket_selected ? 1 : 0;
  out->recovery_text_session_required = damage_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = damage_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = damage_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = damage_plan->credential_session_safe ? 1 : 0;
  out->credential_storage_wiped = damage_plan->credential_storage_wiped ? 1 : 0;
  out->credential_redacted = damage_plan->credential_redacted ? 1 : 0;
  out->length_redacted = damage_plan->length_redacted ? 1 : 0;
  out->raw_secret_exposed = damage_plan->raw_secret_exposed ? 1 : 0;
  out->masked_text_exposed = damage_plan->masked_text_exposed ? 1 : 0;
  out->submit_requested = damage_plan->submit_requested ? 1 : 0;
  out->text_login_authoritative = damage_plan->text_login_authoritative ? 1 : 0;
  out->view = damage_plan->view ? damage_plan->view : "text-login-fallback";
  out->widget_tree = damage_plan->widget_tree ? damage_plan->widget_tree
                                              : "text-login-fallback-bindings";
  out->damage_ticket = damage_plan->damage_ticket ? damage_plan->damage_ticket
                                                  : "text-login-fallback-damage-ticket";
  out->focus_target = damage_plan->focus_target ? damage_plan->focus_target : "none";
  out->primary_action = damage_plan->primary_action ? damage_plan->primary_action
                                                    : "use-text-login";
  out->route = damage_plan->route ? damage_plan->route : "force-text-login";
  out->compositor_target = damage_plan->compositor_target ? damage_plan->compositor_target
                                                          : "none";
  out->damage_policy = damage_plan->damage_policy ? damage_plan->damage_policy
                                                  : "full-safe-fallback";
  out->cache_policy = damage_plan->cache_policy ? damage_plan->cache_policy
                                                : "cache-disabled";
  out->event_type = damage_plan->event_type ? damage_plan->event_type
                                            : "credential-screen-damage-plan-blocked";
  out->state = damage_plan->state ? damage_plan->state : "blocked";
  out->message = damage_plan->message ? damage_plan->message
                                      : "Text login remains authoritative.";
  out->blocked_reason = damage_plan->blocked_reason ? damage_plan->blocked_reason
                                                    : "blocked";
  damage_safe = out->damage_plan_safe && damage_plan->compositor_surface_allowed &&
                !damage_plan->compositor_surface_submitted &&
                damage_plan->compositor_damage_planned &&
                damage_plan->compositor_damage_allowed &&
                !damage_plan->compositor_damage_submitted &&
                damage_plan->damage_required && damage_plan->damage_allowed &&
                !damage_plan->damage_submitted && out->present_ticket_selected &&
                out->route_selected && !out->route_blocked &&
                out->credential_session_safe && out->credential_storage_wiped &&
                out->credential_redacted && out->length_redacted &&
                !out->raw_secret_exposed && !out->masked_text_exposed &&
                damage_plan->submit_blocked && !damage_plan->submit_enabled &&
                !damage_plan->auth_attempt_allowed &&
                !damage_plan->submit_callback_bound &&
                !damage_plan->auth_callback_bound && out->text_login_authoritative;
  if (!damage_safe) {
    out->event_type = "credential-screen-present-plan-unsafe";
    out->state = "blocked";
    out->message = "Credential screen present plan unsafe; use text login.";
    out->blocked_reason = "credential-present-plan-unsafe";
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->damage_ticket = "text-login-fallback-damage-ticket";
    out->present_ticket = "text-login-fallback-present-ticket";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->compositor_target = "none";
    out->damage_policy = "full-safe-fallback";
    out->cache_policy = "cache-disabled";
    out->present_policy = "present-disabled";
    out->route_selected = 0;
    out->route_blocked = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->present_allowed = 0;
    out->present_submitted = 0;
    out->present_ticket_selected = 0;
    out->present_incremental_allowed = 0;
    out->full_present_required = 1;
    out->present_cache_allowed = 0;
    out->present_cache_hit = 0;
    out->present_reuse_allowed = 0;
    out->present_credential_panel = 0;
    out->present_credential_input = 0;
    out->present_credential_focus = 0;
    out->present_text_recovery = 0;
    out->present_text_login = 1;
    out->present_text_login_resume = 0;
    out->present_text_login_fallback = 1;
    out->present_error = 1;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->submit_blocked = 1;
    out->submit_enabled = 0;
    out->auth_attempt_allowed = 0;
    return 0;
  }
  out->present_plan_safe = 1;
  out->present_allowed = 1;
  out->present_submitted = 0;
  out->present_error = 0;
  out->present_cache_allowed = damage_plan->damage_cache_allowed ? 1 : 0;
  out->present_cache_hit = 0;
  out->present_reuse_allowed = damage_plan->damage_reuse_allowed ? 1 : 0;
  out->full_present_required = damage_plan->full_damage_required ? 1 : 0;
  out->present_incremental_allowed = damage_plan->damage_incremental_allowed ? 1 : 0;
  out->present_policy = out->present_incremental_allowed ? "incremental-present-declarative"
                                                         : "full-present-declarative";
  if (damage_plan->submit_requested) {
    out->present_ticket = "text-login-fallback-present-ticket";
    out->compositor_target = "text-login-fallback-present";
    out->damage_policy = "fallback-declarative";
    out->present_policy = "fallback-present-declarative";
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->damage_ticket = "text-login-fallback-damage-ticket";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->present_text_login = 1;
    out->present_text_login_fallback = 1;
    out->full_present_required = 1;
    out->present_incremental_allowed = 0;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->present_credential_focus = 0;
    out->state = "present-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (damage_plan->damage_credential_panel && damage_plan->damage_credential_input &&
      damage_plan->damage_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->present_ticket = "credential-screen-present-ticket";
    out->compositor_target = "credential-screen-present";
    out->view = "credential-screen";
    out->widget_tree = "credential-screen-bindings";
    out->damage_ticket = "credential-screen-damage-ticket";
    out->focus_target = "credential-input";
    out->primary_action = "edit-credential";
    out->present_credential_panel = 1;
    out->present_credential_input = 1;
    out->present_credential_focus = 1;
    out->present_text_login = 0;
    out->present_text_login_fallback = 0;
    out->state = "present-credential-ready";
    out->message = "Credential present ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (damage_plan->damage_text_recovery && out->recovery_text_session_required) {
    out->present_ticket = "text-recovery-present-ticket";
    out->compositor_target = "text-recovery-present";
    out->view = "text-recovery";
    out->widget_tree = "text-recovery-bindings";
    out->damage_ticket = "text-recovery-damage-ticket";
    out->focus_target = "none";
    out->primary_action = "open-text-recovery";
    out->present_text_recovery = 1;
    out->present_text_login = 1;
    out->present_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->present_credential_focus = 0;
    out->state = "present-text-recovery-ready";
    out->message = "Text recovery present ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (damage_plan->damage_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->present_ticket = "text-login-resume-present-ticket";
    out->compositor_target = "text-login-resume-present";
    out->damage_policy = "full-rerender-declarative";
    out->cache_policy = "cache-bypassed-for-rerender";
    out->present_policy = "full-present-declarative";
    out->present_reuse_allowed = 0;
    out->present_cache_allowed = 0;
    out->full_present_required = 1;
    out->present_incremental_allowed = 0;
    out->view = "text-login";
    out->widget_tree = "text-login-resume-bindings";
    out->damage_ticket = "text-login-resume-damage-ticket";
    out->focus_target = "none";
    out->primary_action = "resume-text-login";
    out->present_text_login = 1;
    out->present_text_login_resume = 1;
    out->present_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->present_credential_focus = 0;
    out->state = "present-resume-ready";
    out->message = "Text login resume present ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (damage_plan->damage_text_login_fallback) {
    out->present_ticket = "text-login-fallback-present-ticket";
    out->compositor_target = "text-login-fallback-present";
    out->damage_policy = "fallback-declarative";
    out->present_policy = "fallback-present-declarative";
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->damage_ticket = "text-login-fallback-damage-ticket";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->present_text_login = 1;
    out->present_text_login_fallback = 1;
    out->full_present_required = 1;
    out->present_incremental_allowed = 0;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->present_credential_focus = 0;
    out->state = "present-text-login-ready";
    out->message = "Use authoritative text login.";
    return 0;
  }
  out->event_type = "credential-screen-present-plan-blocked";
  out->state = "blocked";
  out->message = "Credential screen present plan blocked; use text login.";
  out->blocked_reason = "credential-present-plan-blocked";
  out->view = "text-login-fallback";
  out->widget_tree = "text-login-fallback-bindings";
  out->damage_ticket = "text-login-fallback-damage-ticket";
  out->present_ticket = "text-login-fallback-present-ticket";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->compositor_target = "none";
  out->damage_policy = "full-safe-fallback";
  out->cache_policy = "cache-disabled";
  out->present_policy = "present-disabled";
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->present_credential_focus = 0;
  out->present_text_login = 1;
  out->present_text_login_fallback = 1;
  out->present_error = 1;
  out->present_reuse_allowed = 0;
  out->present_cache_allowed = 0;
  out->present_cache_hit = 0;
  out->present_incremental_allowed = 0;
  out->full_present_required = 1;
  return 0;
}
