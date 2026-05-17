/*
 * src/auth/login_runtime/route_controller.c
 *
 * Credential-screen route + controller plan builders — extracted
 * byte-for-byte from `src/auth/login_runtime.c` during PR C.8 of
 * the Estagio C dedicated plan.  Hosts the two contiguous pipeline
 * builders that map a UI event into the route-plan + controller-plan
 * pair consumed by the presenter/binding stages:
 *
 *   - login_window_credential_screen_route_plan_build
 *   - login_window_credential_screen_controller_build
 *
 * The route-plan owns the safe transition decision (which next-stage
 * action is authorised given the UI event + readiness + audit
 * envelope); the controller-plan owns the actor that will dispatch
 * that decision.  Both are fail-closed: any missing input collapses
 * the plan into a blocked state.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

int login_window_credential_screen_route_plan_build(
    const struct login_window_credential_screen_ui_event *ui_event,
    struct login_window_credential_screen_route_plan *out) {
  int event_safe = 0;
  if (!out) return -1;
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_ROUTE_PLAN_VERSION;
  out->ui_event_available = ui_event ? 1 : 0;
  out->ui_event_safe = 0;
  out->route_plan_safe = 0;
  out->requested_action = 0;
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->stay_on_credential_screen = 0;
  out->open_text_recovery_route = 0;
  out->resume_text_login_route = 0;
  out->force_text_login_required = 1;
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
  out->route = "force-text-login";
  out->event_type = "credential-screen-route-unavailable";
  out->result_action = "use-text-login";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "ui-event-unavailable";
  if (!ui_event) return 0;
  out->requested_action = ui_event->requested_action;
  out->ui_event_safe = ui_event->ui_event_safe ? 1 : 0;
  out->action_allowed = ui_event->action_allowed ? 1 : 0;
  out->action_blocked = ui_event->action_blocked ? 1 : 0;
  out->input_focus_allowed = ui_event->input_focus_allowed ? 1 : 0;
  out->recovery_text_session_required = ui_event->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = ui_event->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = ui_event->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = ui_event->credential_session_safe ? 1 : 0;
  out->credential_storage_wiped = ui_event->credential_storage_wiped ? 1 : 0;
  out->credential_redacted = ui_event->credential_redacted ? 1 : 0;
  out->length_redacted = ui_event->length_redacted ? 1 : 0;
  out->raw_secret_exposed = ui_event->raw_secret_exposed ? 1 : 0;
  out->masked_text_exposed = ui_event->masked_text_exposed ? 1 : 0;
  out->submit_requested = ui_event->submit_requested ? 1 : 0;
  out->submit_blocked = 1;
  out->submit_enabled = 0;
  out->auth_attempt_allowed = 0;
  out->text_login_authoritative = ui_event->text_login_authoritative ? 1 : 0;
  out->event_type = ui_event->event_type ? ui_event->event_type
                                         : "credential-screen-action-blocked";
  out->result_action = ui_event->result_action ? ui_event->result_action
                                               : "use-text-login";
  out->state = ui_event->state ? ui_event->state : "blocked";
  out->message = ui_event->message ? ui_event->message
                                   : "Text login remains authoritative.";
  out->blocked_reason = ui_event->blocked_reason ? ui_event->blocked_reason
                                                 : "blocked";
  event_safe = out->ui_event_safe && out->credential_session_safe &&
               out->credential_storage_wiped && out->credential_redacted &&
               out->length_redacted && !out->raw_secret_exposed &&
               !out->masked_text_exposed && ui_event->submit_blocked &&
               !ui_event->submit_enabled && !ui_event->auth_attempt_allowed &&
               out->text_login_authoritative;
  if (!event_safe) {
    out->event_type = "credential-screen-route-unsafe";
    out->result_action = "use-text-login";
    out->state = "blocked";
    out->message = "Credential route plan unsafe; use text login.";
    out->blocked_reason = "credential-route-unsafe";
    out->route = "force-text-login";
    out->route_selected = 0;
    out->route_blocked = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->force_text_login_required = 1;
    out->submit_blocked = 1;
    out->submit_enabled = 0;
    out->auth_attempt_allowed = 0;
    return 0;
  }
  out->route_plan_safe = 1;
  if (ui_event->submit_requested) {
    out->route = "force-text-login";
    out->route_selected = 1;
    out->route_blocked = 0;
    out->force_text_login_required = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "text-login-route-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (ui_event->edit_credential_requested && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->route = "stay-on-credential-screen";
    out->route_selected = 1;
    out->route_blocked = 0;
    out->stay_on_credential_screen = 1;
    out->force_text_login_required = 0;
    out->state = "credential-route-ready";
    out->message = "Stay on credential screen; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (ui_event->open_text_recovery_requested && out->action_allowed &&
      !out->action_blocked && out->recovery_text_session_required) {
    out->route = "open-text-recovery";
    out->route_selected = 1;
    out->route_blocked = 0;
    out->open_text_recovery_route = 1;
    out->force_text_login_required = 1;
    out->state = "text-recovery-route-ready";
    out->message = "Open text recovery route; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (ui_event->resume_text_login_requested && out->action_allowed &&
      !out->action_blocked && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->route = "resume-text-login";
    out->route_selected = 1;
    out->route_blocked = 0;
    out->resume_text_login_route = 1;
    out->force_text_login_required = 1;
    out->state = "resume-route-ready";
    out->message = "Resume text login route; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (ui_event->use_text_login_required || ui_event->action_blocked) {
    out->route = "force-text-login";
    out->route_selected = 1;
    out->route_blocked = 0;
    out->force_text_login_required = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "text-login-route-ready";
    out->message = "Use authoritative text login route.";
    return 0;
  }
  out->route = "force-text-login";
  out->event_type = "credential-screen-route-blocked";
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->force_text_login_required = 1;
  return 0;
}


int login_window_credential_screen_controller_build(
    const struct login_window_credential_screen_route_plan *route_plan,
    struct login_window_credential_screen_controller *out) {
  int route_safe = 0;
  if (!out) return -1;
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_CONTROLLER_VERSION;
  out->route_plan_available = route_plan ? 1 : 0;
  out->route_plan_safe = 0;
  out->controller_safe = 0;
  out->requested_action = 0;
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->credential_screen_visible = 0;
  out->credential_input_focus = 0;
  out->text_recovery_open = 0;
  out->text_login_resume = 0;
  out->text_login_forced = 1;
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
  out->route = "force-text-login";
  out->event_type = "credential-screen-controller-unavailable";
  out->result_action = "use-text-login";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "route-plan-unavailable";
  if (!route_plan) return 0;
  out->requested_action = route_plan->requested_action;
  out->route_plan_safe = route_plan->route_plan_safe ? 1 : 0;
  out->route_selected = route_plan->route_selected ? 1 : 0;
  out->route_blocked = route_plan->route_blocked ? 1 : 0;
  out->action_allowed = route_plan->action_allowed ? 1 : 0;
  out->action_blocked = route_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = route_plan->input_focus_allowed ? 1 : 0;
  out->recovery_text_session_required = route_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = route_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = route_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = route_plan->credential_session_safe ? 1 : 0;
  out->credential_storage_wiped = route_plan->credential_storage_wiped ? 1 : 0;
  out->credential_redacted = route_plan->credential_redacted ? 1 : 0;
  out->length_redacted = route_plan->length_redacted ? 1 : 0;
  out->raw_secret_exposed = route_plan->raw_secret_exposed ? 1 : 0;
  out->masked_text_exposed = route_plan->masked_text_exposed ? 1 : 0;
  out->submit_requested = route_plan->submit_requested ? 1 : 0;
  out->submit_blocked = 1;
  out->submit_enabled = 0;
  out->auth_attempt_allowed = 0;
  out->text_login_authoritative = route_plan->text_login_authoritative ? 1 : 0;
  out->route = route_plan->route ? route_plan->route : "force-text-login";
  out->event_type = route_plan->event_type ? route_plan->event_type
                                           : "credential-screen-route-blocked";
  out->result_action = route_plan->result_action ? route_plan->result_action
                                                 : "use-text-login";
  out->state = route_plan->state ? route_plan->state : "blocked";
  out->message = route_plan->message ? route_plan->message
                                     : "Text login remains authoritative.";
  out->blocked_reason = route_plan->blocked_reason ? route_plan->blocked_reason
                                                   : "blocked";
  route_safe = out->route_plan_safe && out->route_selected &&
               !out->route_blocked && out->credential_session_safe &&
               out->credential_storage_wiped && out->credential_redacted &&
               out->length_redacted && !out->raw_secret_exposed &&
               !out->masked_text_exposed && route_plan->submit_blocked &&
               !route_plan->submit_enabled && !route_plan->auth_attempt_allowed &&
               out->text_login_authoritative;
  if (!route_safe) {
    out->event_type = "credential-screen-controller-unsafe";
    out->result_action = "use-text-login";
    out->state = "blocked";
    out->message = "Credential screen controller unsafe; use text login.";
    out->blocked_reason = "credential-controller-unsafe";
    out->route = "force-text-login";
    out->route_selected = 0;
    out->route_blocked = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->credential_screen_visible = 0;
    out->credential_input_focus = 0;
    out->text_recovery_open = 0;
    out->text_login_resume = 0;
    out->text_login_forced = 1;
    out->submit_blocked = 1;
    out->submit_enabled = 0;
    out->auth_attempt_allowed = 0;
    return 0;
  }
  out->controller_safe = 1;
  if (route_plan->submit_requested) {
    out->route = "force-text-login";
    out->text_login_forced = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->credential_input_focus = 0;
    out->state = "controller-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (route_plan->stay_on_credential_screen && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->credential_screen_visible = 1;
    out->credential_input_focus = 1;
    out->text_login_forced = 0;
    out->state = "controller-credential-ready";
    out->message = "Credential screen may keep focus; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (route_plan->open_text_recovery_route && out->action_allowed &&
      !out->action_blocked && out->recovery_text_session_required) {
    out->text_recovery_open = 1;
    out->text_login_forced = 1;
    out->input_focus_allowed = 0;
    out->credential_input_focus = 0;
    out->state = "controller-text-recovery-ready";
    out->message = "Open text recovery; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (route_plan->resume_text_login_route && out->action_allowed &&
      !out->action_blocked && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->text_login_resume = 1;
    out->text_login_forced = 1;
    out->input_focus_allowed = 0;
    out->credential_input_focus = 0;
    out->state = "controller-resume-ready";
    out->message = "Resume text login; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (route_plan->force_text_login_required) {
    out->route = "force-text-login";
    out->text_login_forced = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->credential_input_focus = 0;
    out->state = "controller-text-login-ready";
    out->message = "Use authoritative text login.";
    return 0;
  }
  out->route = "force-text-login";
  out->event_type = "credential-screen-controller-blocked";
  out->state = "blocked";
  out->message = "Credential screen controller blocked; use text login.";
  out->blocked_reason = "credential-controller-blocked";
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->credential_input_focus = 0;
  out->text_login_forced = 1;
  return 0;
}
