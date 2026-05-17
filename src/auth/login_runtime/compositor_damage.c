/*
 * src/auth/login_runtime/compositor_damage.c
 *
 * Credential-screen compositor + damage plan builders — extracted
 * byte-for-byte from `src/auth/login_runtime.c` during PR C.14 of
 * the Estagio C dedicated plan.  Hosts the two contiguous pipeline
 * builders that compose surfaces and compute the damage regions
 * needed before presenting a frame:
 *
 *   - login_window_credential_screen_compositor_plan_build
 *   - login_window_credential_screen_damage_plan_build
 *
 * The compositor-plan owns the actor responsible for composing all
 * the per-screen surfaces; the damage-plan owns the bounded set of
 * dirty regions that must be redrawn in the upcoming present cycle.
 * Both are fail-closed.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

int login_window_credential_screen_compositor_plan_build(
    const struct login_window_credential_screen_surface_plan *surface_plan,
    struct login_window_credential_screen_compositor_plan *out) {
  int surface_safe = 0;
  if (!out) return -1;
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_COMPOSITOR_PLAN_VERSION;
  out->surface_plan_available = surface_plan ? 1 : 0;
  out->surface_plan_safe = 0;
  out->compositor_plan_safe = 0;
  out->requested_action = 0;
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->window_surface_allowed = 0;
  out->window_surface_submitted = 0;
  out->compositor_surface_required = 1;
  out->compositor_surface_allowed = 0;
  out->compositor_surface_submitted = 0;
  out->compositor_damage_planned = 0;
  out->compositor_damage_allowed = 0;
  out->compositor_damage_submitted = 0;
  out->compositor_ticket_selected = 0;
  out->compositor_credential_panel = 0;
  out->compositor_credential_input = 0;
  out->compositor_credential_focus = 0;
  out->compositor_text_recovery = 0;
  out->compositor_text_login = 1;
  out->compositor_text_login_resume = 0;
  out->compositor_text_login_fallback = 1;
  out->compositor_error = 1;
  out->compositor_reuse_allowed = 0;
  out->compositor_cache_allowed = 0;
  out->compositor_cache_hit = 0;
  out->full_damage_required = 1;
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
  out->surface_ticket = "text-login-fallback-surface-ticket";
  out->compositor_ticket = "text-login-fallback-compositor-ticket";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->compositor_target = "none";
  out->damage_policy = "full-safe-fallback";
  out->cache_policy = "cache-disabled";
  out->event_type = "credential-screen-compositor-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "surface-plan-unavailable";
  if (!surface_plan) return 0;
  out->requested_action = surface_plan->requested_action;
  out->surface_plan_safe = surface_plan->surface_plan_safe ? 1 : 0;
  out->route_selected = surface_plan->route_selected ? 1 : 0;
  out->route_blocked = surface_plan->route_blocked ? 1 : 0;
  out->action_allowed = surface_plan->action_allowed ? 1 : 0;
  out->action_blocked = surface_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = surface_plan->input_focus_allowed ? 1 : 0;
  out->window_surface_allowed = surface_plan->window_surface_allowed ? 1 : 0;
  out->window_surface_submitted = surface_plan->window_surface_submitted ? 1 : 0;
  out->compositor_damage_planned = surface_plan->compositor_damage_planned ? 1 : 0;
  out->compositor_damage_submitted = surface_plan->compositor_damage_submitted ? 1 : 0;
  out->compositor_ticket_selected = surface_plan->surface_ticket_selected ? 1 : 0;
  out->recovery_text_session_required = surface_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = surface_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = surface_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = surface_plan->credential_session_safe ? 1 : 0;
  out->credential_storage_wiped = surface_plan->credential_storage_wiped ? 1 : 0;
  out->credential_redacted = surface_plan->credential_redacted ? 1 : 0;
  out->length_redacted = surface_plan->length_redacted ? 1 : 0;
  out->raw_secret_exposed = surface_plan->raw_secret_exposed ? 1 : 0;
  out->masked_text_exposed = surface_plan->masked_text_exposed ? 1 : 0;
  out->submit_requested = surface_plan->submit_requested ? 1 : 0;
  out->text_login_authoritative = surface_plan->text_login_authoritative ? 1 : 0;
  out->view = surface_plan->view ? surface_plan->view : "text-login-fallback";
  out->widget_tree = surface_plan->widget_tree ? surface_plan->widget_tree
                                               : "text-login-fallback-bindings";
  out->surface_ticket = surface_plan->surface_ticket ? surface_plan->surface_ticket
                                                     : "text-login-fallback-surface-ticket";
  out->focus_target = surface_plan->focus_target ? surface_plan->focus_target : "none";
  out->primary_action = surface_plan->primary_action ? surface_plan->primary_action
                                                     : "use-text-login";
  out->route = surface_plan->route ? surface_plan->route : "force-text-login";
  out->compositor_target = surface_plan->compositor_target ? surface_plan->compositor_target
                                                           : "none";
  out->damage_policy = surface_plan->damage_policy ? surface_plan->damage_policy
                                                   : "full-safe-fallback";
  out->cache_policy = surface_plan->cache_policy ? surface_plan->cache_policy
                                                 : "cache-disabled";
  out->event_type = surface_plan->event_type ? surface_plan->event_type
                                             : "credential-screen-surface-plan-blocked";
  out->state = surface_plan->state ? surface_plan->state : "blocked";
  out->message = surface_plan->message ? surface_plan->message
                                       : "Text login remains authoritative.";
  out->blocked_reason = surface_plan->blocked_reason ? surface_plan->blocked_reason
                                                     : "blocked";
  surface_safe = out->surface_plan_safe && surface_plan->window_surface_allowed &&
                 !surface_plan->window_surface_submitted &&
                 surface_plan->compositor_damage_planned &&
                 !surface_plan->compositor_damage_submitted &&
                 out->compositor_ticket_selected && out->route_selected &&
                 !out->route_blocked && out->credential_session_safe &&
                 out->credential_storage_wiped && out->credential_redacted &&
                 out->length_redacted && !out->raw_secret_exposed &&
                 !out->masked_text_exposed && surface_plan->submit_blocked &&
                 !surface_plan->submit_enabled && !surface_plan->auth_attempt_allowed &&
                 !surface_plan->submit_callback_bound &&
                 !surface_plan->auth_callback_bound && out->text_login_authoritative;
  if (!surface_safe) {
    out->event_type = "credential-screen-compositor-plan-unsafe";
    out->state = "blocked";
    out->message = "Credential screen compositor plan unsafe; use text login.";
    out->blocked_reason = "credential-compositor-plan-unsafe";
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->surface_ticket = "text-login-fallback-surface-ticket";
    out->compositor_ticket = "text-login-fallback-compositor-ticket";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->compositor_target = "none";
    out->damage_policy = "full-safe-fallback";
    out->cache_policy = "cache-disabled";
    out->route_selected = 0;
    out->route_blocked = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->compositor_surface_allowed = 0;
    out->compositor_surface_submitted = 0;
    out->compositor_damage_allowed = 0;
    out->compositor_damage_submitted = 0;
    out->compositor_ticket_selected = 0;
    out->compositor_credential_panel = 0;
    out->compositor_credential_input = 0;
    out->compositor_credential_focus = 0;
    out->compositor_text_recovery = 0;
    out->compositor_text_login = 1;
    out->compositor_text_login_resume = 0;
    out->compositor_text_login_fallback = 1;
    out->compositor_error = 1;
    out->compositor_reuse_allowed = 0;
    out->compositor_cache_allowed = 0;
    out->compositor_cache_hit = 0;
    out->full_damage_required = 1;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->submit_blocked = 1;
    out->submit_enabled = 0;
    out->auth_attempt_allowed = 0;
    return 0;
  }
  out->compositor_plan_safe = 1;
  out->compositor_surface_allowed = 1;
  out->compositor_surface_submitted = 0;
  out->compositor_damage_allowed = 1;
  out->compositor_damage_submitted = 0;
  out->compositor_error = 0;
  out->compositor_reuse_allowed = surface_plan->surface_reuse_allowed ? 1 : 0;
  out->compositor_cache_allowed = surface_plan->surface_cache_allowed ? 1 : 0;
  out->compositor_cache_hit = 0;
  out->full_damage_required = surface_plan->full_damage_required ? 1 : 0;
  if (surface_plan->submit_requested) {
    out->compositor_ticket = "text-login-fallback-compositor-ticket";
    out->compositor_target = "text-login-fallback-compositor";
    out->damage_policy = "fallback-declarative";
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->surface_ticket = "text-login-fallback-surface-ticket";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->compositor_text_login = 1;
    out->compositor_text_login_fallback = 1;
    out->full_damage_required = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->compositor_credential_focus = 0;
    out->state = "compositor-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (surface_plan->surface_credential_panel && surface_plan->surface_credential_input &&
      surface_plan->surface_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->compositor_ticket = "credential-screen-compositor-ticket";
    out->compositor_target = "credential-screen-compositor";
    out->view = "credential-screen";
    out->widget_tree = "credential-screen-bindings";
    out->surface_ticket = "credential-screen-surface-ticket";
    out->focus_target = "credential-input";
    out->primary_action = "edit-credential";
    out->compositor_credential_panel = 1;
    out->compositor_credential_input = 1;
    out->compositor_credential_focus = 1;
    out->compositor_text_login = 0;
    out->compositor_text_login_fallback = 0;
    out->state = "compositor-credential-ready";
    out->message = "Credential compositor ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (surface_plan->surface_text_recovery && out->recovery_text_session_required) {
    out->compositor_ticket = "text-recovery-compositor-ticket";
    out->compositor_target = "text-recovery-compositor";
    out->view = "text-recovery";
    out->widget_tree = "text-recovery-bindings";
    out->surface_ticket = "text-recovery-surface-ticket";
    out->focus_target = "none";
    out->primary_action = "open-text-recovery";
    out->compositor_text_recovery = 1;
    out->compositor_text_login = 1;
    out->compositor_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->compositor_credential_focus = 0;
    out->state = "compositor-text-recovery-ready";
    out->message = "Text recovery compositor ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (surface_plan->surface_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->compositor_ticket = "text-login-resume-compositor-ticket";
    out->compositor_target = "text-login-resume-compositor";
    out->damage_policy = "full-rerender-declarative";
    out->cache_policy = "cache-bypassed-for-rerender";
    out->compositor_reuse_allowed = 0;
    out->full_damage_required = 1;
    out->view = "text-login";
    out->widget_tree = "text-login-resume-bindings";
    out->surface_ticket = "text-login-resume-surface-ticket";
    out->focus_target = "none";
    out->primary_action = "resume-text-login";
    out->compositor_text_login = 1;
    out->compositor_text_login_resume = 1;
    out->compositor_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->compositor_credential_focus = 0;
    out->state = "compositor-resume-ready";
    out->message = "Text login resume compositor ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (surface_plan->surface_text_login_fallback) {
    out->compositor_ticket = "text-login-fallback-compositor-ticket";
    out->compositor_target = "text-login-fallback-compositor";
    out->damage_policy = "fallback-declarative";
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->surface_ticket = "text-login-fallback-surface-ticket";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->compositor_text_login = 1;
    out->compositor_text_login_fallback = 1;
    out->full_damage_required = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->compositor_credential_focus = 0;
    out->state = "compositor-text-login-ready";
    out->message = "Use authoritative text login.";
    return 0;
  }
  out->event_type = "credential-screen-compositor-plan-blocked";
  out->state = "blocked";
  out->message = "Credential screen compositor plan blocked; use text login.";
  out->blocked_reason = "credential-compositor-plan-blocked";
  out->view = "text-login-fallback";
  out->widget_tree = "text-login-fallback-bindings";
  out->surface_ticket = "text-login-fallback-surface-ticket";
  out->compositor_ticket = "text-login-fallback-compositor-ticket";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->compositor_target = "none";
  out->damage_policy = "full-safe-fallback";
  out->cache_policy = "cache-disabled";
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->compositor_credential_focus = 0;
  out->compositor_text_login = 1;
  out->compositor_text_login_fallback = 1;
  out->compositor_error = 1;
  out->compositor_reuse_allowed = 0;
  out->compositor_cache_allowed = 0;
  out->compositor_cache_hit = 0;
  out->compositor_damage_allowed = 0;
  out->full_damage_required = 1;
  return 0;
}

int login_window_credential_screen_damage_plan_build(
    const struct login_window_credential_screen_compositor_plan *compositor_plan,
    struct login_window_credential_screen_damage_plan *out) {
  int compositor_safe = 0;
  if (!out) return -1;
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_DAMAGE_PLAN_VERSION;
  out->compositor_plan_available = compositor_plan ? 1 : 0;
  out->compositor_plan_safe = 0;
  out->damage_plan_safe = 0;
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
  out->damage_ticket_selected = 0;
  out->damage_incremental_allowed = 0;
  out->full_damage_required = 1;
  out->damage_cache_allowed = 0;
  out->damage_cache_hit = 0;
  out->damage_reuse_allowed = 0;
  out->damage_credential_panel = 0;
  out->damage_credential_input = 0;
  out->damage_credential_focus = 0;
  out->damage_text_recovery = 0;
  out->damage_text_login = 1;
  out->damage_text_login_resume = 0;
  out->damage_text_login_fallback = 1;
  out->damage_error = 1;
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
  out->compositor_ticket = "text-login-fallback-compositor-ticket";
  out->damage_ticket = "text-login-fallback-damage-ticket";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->compositor_target = "none";
  out->damage_policy = "full-safe-fallback";
  out->cache_policy = "cache-disabled";
  out->event_type = "credential-screen-damage-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "compositor-plan-unavailable";
  if (!compositor_plan) return 0;
  out->requested_action = compositor_plan->requested_action;
  out->compositor_plan_safe = compositor_plan->compositor_plan_safe ? 1 : 0;
  out->route_selected = compositor_plan->route_selected ? 1 : 0;
  out->route_blocked = compositor_plan->route_blocked ? 1 : 0;
  out->action_allowed = compositor_plan->action_allowed ? 1 : 0;
  out->action_blocked = compositor_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = compositor_plan->input_focus_allowed ? 1 : 0;
  out->compositor_surface_allowed = compositor_plan->compositor_surface_allowed ? 1 : 0;
  out->compositor_surface_submitted = compositor_plan->compositor_surface_submitted ? 1 : 0;
  out->compositor_damage_planned = compositor_plan->compositor_damage_planned ? 1 : 0;
  out->compositor_damage_allowed = compositor_plan->compositor_damage_allowed ? 1 : 0;
  out->compositor_damage_submitted = compositor_plan->compositor_damage_submitted ? 1 : 0;
  out->damage_ticket_selected = compositor_plan->compositor_ticket_selected ? 1 : 0;
  out->recovery_text_session_required = compositor_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = compositor_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = compositor_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = compositor_plan->credential_session_safe ? 1 : 0;
  out->credential_storage_wiped = compositor_plan->credential_storage_wiped ? 1 : 0;
  out->credential_redacted = compositor_plan->credential_redacted ? 1 : 0;
  out->length_redacted = compositor_plan->length_redacted ? 1 : 0;
  out->raw_secret_exposed = compositor_plan->raw_secret_exposed ? 1 : 0;
  out->masked_text_exposed = compositor_plan->masked_text_exposed ? 1 : 0;
  out->submit_requested = compositor_plan->submit_requested ? 1 : 0;
  out->text_login_authoritative = compositor_plan->text_login_authoritative ? 1 : 0;
  out->view = compositor_plan->view ? compositor_plan->view : "text-login-fallback";
  out->widget_tree = compositor_plan->widget_tree ? compositor_plan->widget_tree
                                                  : "text-login-fallback-bindings";
  out->compositor_ticket = compositor_plan->compositor_ticket ? compositor_plan->compositor_ticket
                                                              : "text-login-fallback-compositor-ticket";
  out->focus_target = compositor_plan->focus_target ? compositor_plan->focus_target : "none";
  out->primary_action = compositor_plan->primary_action ? compositor_plan->primary_action
                                                        : "use-text-login";
  out->route = compositor_plan->route ? compositor_plan->route : "force-text-login";
  out->compositor_target = compositor_plan->compositor_target ? compositor_plan->compositor_target
                                                              : "none";
  out->damage_policy = compositor_plan->damage_policy ? compositor_plan->damage_policy
                                                      : "full-safe-fallback";
  out->cache_policy = compositor_plan->cache_policy ? compositor_plan->cache_policy
                                                    : "cache-disabled";
  out->event_type = compositor_plan->event_type ? compositor_plan->event_type
                                                : "credential-screen-compositor-plan-blocked";
  out->state = compositor_plan->state ? compositor_plan->state : "blocked";
  out->message = compositor_plan->message ? compositor_plan->message
                                          : "Text login remains authoritative.";
  out->blocked_reason = compositor_plan->blocked_reason ? compositor_plan->blocked_reason
                                                        : "blocked";
  compositor_safe = out->compositor_plan_safe &&
                    compositor_plan->compositor_surface_allowed &&
                    !compositor_plan->compositor_surface_submitted &&
                    compositor_plan->compositor_damage_planned &&
                    compositor_plan->compositor_damage_allowed &&
                    !compositor_plan->compositor_damage_submitted &&
                    out->damage_ticket_selected && out->route_selected &&
                    !out->route_blocked && out->credential_session_safe &&
                    out->credential_storage_wiped && out->credential_redacted &&
                    out->length_redacted && !out->raw_secret_exposed &&
                    !out->masked_text_exposed && compositor_plan->submit_blocked &&
                    !compositor_plan->submit_enabled &&
                    !compositor_plan->auth_attempt_allowed &&
                    !compositor_plan->submit_callback_bound &&
                    !compositor_plan->auth_callback_bound && out->text_login_authoritative;
  if (!compositor_safe) {
    out->event_type = "credential-screen-damage-plan-unsafe";
    out->state = "blocked";
    out->message = "Credential screen damage plan unsafe; use text login.";
    out->blocked_reason = "credential-damage-plan-unsafe";
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->compositor_ticket = "text-login-fallback-compositor-ticket";
    out->damage_ticket = "text-login-fallback-damage-ticket";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->compositor_target = "none";
    out->damage_policy = "full-safe-fallback";
    out->cache_policy = "cache-disabled";
    out->route_selected = 0;
    out->route_blocked = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->damage_allowed = 0;
    out->damage_submitted = 0;
    out->damage_ticket_selected = 0;
    out->damage_incremental_allowed = 0;
    out->full_damage_required = 1;
    out->damage_cache_allowed = 0;
    out->damage_cache_hit = 0;
    out->damage_reuse_allowed = 0;
    out->damage_credential_panel = 0;
    out->damage_credential_input = 0;
    out->damage_credential_focus = 0;
    out->damage_text_recovery = 0;
    out->damage_text_login = 1;
    out->damage_text_login_resume = 0;
    out->damage_text_login_fallback = 1;
    out->damage_error = 1;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->submit_blocked = 1;
    out->submit_enabled = 0;
    out->auth_attempt_allowed = 0;
    return 0;
  }
  out->damage_plan_safe = 1;
  out->damage_allowed = 1;
  out->damage_submitted = 0;
  out->damage_error = 0;
  out->damage_cache_allowed = compositor_plan->compositor_cache_allowed ? 1 : 0;
  out->damage_cache_hit = 0;
  out->damage_reuse_allowed = compositor_plan->compositor_reuse_allowed ? 1 : 0;
  out->full_damage_required = compositor_plan->full_damage_required ? 1 : 0;
  out->damage_incremental_allowed = out->full_damage_required ? 0 : 1;
  if (compositor_plan->submit_requested) {
    out->damage_ticket = "text-login-fallback-damage-ticket";
    out->compositor_target = "text-login-fallback-damage";
    out->damage_policy = "fallback-declarative";
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->compositor_ticket = "text-login-fallback-compositor-ticket";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->damage_text_login = 1;
    out->damage_text_login_fallback = 1;
    out->full_damage_required = 1;
    out->damage_incremental_allowed = 0;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->damage_credential_focus = 0;
    out->state = "damage-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (compositor_plan->compositor_credential_panel &&
      compositor_plan->compositor_credential_input &&
      compositor_plan->compositor_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->damage_ticket = "credential-screen-damage-ticket";
    out->compositor_target = "credential-screen-damage";
    out->view = "credential-screen";
    out->widget_tree = "credential-screen-bindings";
    out->compositor_ticket = "credential-screen-compositor-ticket";
    out->focus_target = "credential-input";
    out->primary_action = "edit-credential";
    out->damage_credential_panel = 1;
    out->damage_credential_input = 1;
    out->damage_credential_focus = 1;
    out->damage_text_login = 0;
    out->damage_text_login_fallback = 0;
    out->state = "damage-credential-ready";
    out->message = "Credential damage ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (compositor_plan->compositor_text_recovery && out->recovery_text_session_required) {
    out->damage_ticket = "text-recovery-damage-ticket";
    out->compositor_target = "text-recovery-damage";
    out->view = "text-recovery";
    out->widget_tree = "text-recovery-bindings";
    out->compositor_ticket = "text-recovery-compositor-ticket";
    out->focus_target = "none";
    out->primary_action = "open-text-recovery";
    out->damage_text_recovery = 1;
    out->damage_text_login = 1;
    out->damage_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->damage_credential_focus = 0;
    out->state = "damage-text-recovery-ready";
    out->message = "Text recovery damage ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (compositor_plan->compositor_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->damage_ticket = "text-login-resume-damage-ticket";
    out->compositor_target = "text-login-resume-damage";
    out->damage_policy = "full-rerender-declarative";
    out->cache_policy = "cache-bypassed-for-rerender";
    out->damage_reuse_allowed = 0;
    out->damage_cache_allowed = 0;
    out->full_damage_required = 1;
    out->damage_incremental_allowed = 0;
    out->view = "text-login";
    out->widget_tree = "text-login-resume-bindings";
    out->compositor_ticket = "text-login-resume-compositor-ticket";
    out->focus_target = "none";
    out->primary_action = "resume-text-login";
    out->damage_text_login = 1;
    out->damage_text_login_resume = 1;
    out->damage_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->damage_credential_focus = 0;
    out->state = "damage-resume-ready";
    out->message = "Text login resume damage ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (compositor_plan->compositor_text_login_fallback) {
    out->damage_ticket = "text-login-fallback-damage-ticket";
    out->compositor_target = "text-login-fallback-damage";
    out->damage_policy = "fallback-declarative";
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->compositor_ticket = "text-login-fallback-compositor-ticket";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->damage_text_login = 1;
    out->damage_text_login_fallback = 1;
    out->full_damage_required = 1;
    out->damage_incremental_allowed = 0;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->damage_credential_focus = 0;
    out->state = "damage-text-login-ready";
    out->message = "Use authoritative text login.";
    return 0;
  }
  out->event_type = "credential-screen-damage-plan-blocked";
  out->state = "blocked";
  out->message = "Credential screen damage plan blocked; use text login.";
  out->blocked_reason = "credential-damage-plan-blocked";
  out->view = "text-login-fallback";
  out->widget_tree = "text-login-fallback-bindings";
  out->compositor_ticket = "text-login-fallback-compositor-ticket";
  out->damage_ticket = "text-login-fallback-damage-ticket";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->compositor_target = "none";
  out->damage_policy = "full-safe-fallback";
  out->cache_policy = "cache-disabled";
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->damage_credential_focus = 0;
  out->damage_text_login = 1;
  out->damage_text_login_fallback = 1;
  out->damage_error = 1;
  out->damage_reuse_allowed = 0;
  out->damage_cache_allowed = 0;
  out->damage_cache_hit = 0;
  out->damage_incremental_allowed = 0;
  out->full_damage_required = 1;
  return 0;
}
