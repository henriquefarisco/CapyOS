/*
 * src/auth/login_runtime/presenter_binding.c
 *
 * Credential-screen presenter + binding plan builders — extracted
 * byte-for-byte from `src/auth/login_runtime.c` during PR C.9 of
 * the Estagio C dedicated plan.  Hosts the two contiguous pipeline
 * builders that connect the controller plan to the screen's actual
 * presenter actor and its window binding:
 *
 *   - login_window_credential_screen_presenter_build
 *   - login_window_credential_screen_binding_build
 *
 * The presenter-plan owns the actor responsible for visual updates;
 * the binding-plan owns the window-system hook surface that will
 * receive the presenter's render commands.  Both are fail-closed:
 * any missing input collapses the plan into a blocked state.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

int login_window_credential_screen_presenter_build(
    const struct login_window_credential_screen_controller *controller,
    struct login_window_credential_screen_presenter *out) {
  int controller_safe = 0;
  if (!out) return -1;
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_PRESENTER_VERSION;
  out->controller_available = controller ? 1 : 0;
  out->controller_safe = 0;
  out->presenter_safe = 0;
  out->requested_action = 0;
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->credential_screen_visible = 0;
  out->credential_panel_visible = 0;
  out->credential_input_visible = 0;
  out->credential_input_focus = 0;
  out->text_recovery_visible = 0;
  out->text_recovery_open = 0;
  out->text_login_visible = 1;
  out->text_login_resume = 0;
  out->text_login_forced = 1;
  out->fallback_notice_visible = 1;
  out->text_login_notice_visible = 1;
  out->status_visible = 1;
  out->error_visible = 1;
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
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->event_type = "credential-screen-presenter-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "controller-unavailable";
  if (!controller) return 0;
  out->requested_action = controller->requested_action;
  out->controller_safe = controller->controller_safe ? 1 : 0;
  out->route_selected = controller->route_selected ? 1 : 0;
  out->route_blocked = controller->route_blocked ? 1 : 0;
  out->action_allowed = controller->action_allowed ? 1 : 0;
  out->action_blocked = controller->action_blocked ? 1 : 0;
  out->input_focus_allowed = controller->input_focus_allowed ? 1 : 0;
  out->credential_screen_visible = controller->credential_screen_visible ? 1 : 0;
  out->credential_input_focus = controller->credential_input_focus ? 1 : 0;
  out->text_recovery_open = controller->text_recovery_open ? 1 : 0;
  out->text_login_resume = controller->text_login_resume ? 1 : 0;
  out->text_login_forced = controller->text_login_forced ? 1 : 0;
  out->recovery_text_session_required = controller->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = controller->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = controller->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = controller->credential_session_safe ? 1 : 0;
  out->credential_storage_wiped = controller->credential_storage_wiped ? 1 : 0;
  out->credential_redacted = controller->credential_redacted ? 1 : 0;
  out->length_redacted = controller->length_redacted ? 1 : 0;
  out->raw_secret_exposed = controller->raw_secret_exposed ? 1 : 0;
  out->masked_text_exposed = controller->masked_text_exposed ? 1 : 0;
  out->submit_requested = controller->submit_requested ? 1 : 0;
  out->submit_blocked = 1;
  out->submit_enabled = 0;
  out->auth_attempt_allowed = 0;
  out->text_login_authoritative = controller->text_login_authoritative ? 1 : 0;
  out->route = controller->route ? controller->route : "force-text-login";
  out->event_type = controller->event_type ? controller->event_type
                                           : "credential-screen-controller-blocked";
  out->state = controller->state ? controller->state : "blocked";
  out->message = controller->message ? controller->message
                                     : "Text login remains authoritative.";
  out->blocked_reason = controller->blocked_reason ? controller->blocked_reason
                                                   : "blocked";
  controller_safe = out->controller_safe && out->route_selected &&
                    !out->route_blocked && out->credential_session_safe &&
                    out->credential_storage_wiped && out->credential_redacted &&
                    out->length_redacted && !out->raw_secret_exposed &&
                    !out->masked_text_exposed && controller->submit_blocked &&
                    !controller->submit_enabled && !controller->auth_attempt_allowed &&
                    out->text_login_authoritative;
  if (!controller_safe) {
    out->event_type = "credential-screen-presenter-unsafe";
    out->state = "blocked";
    out->message = "Credential screen presenter unsafe; use text login.";
    out->blocked_reason = "credential-presenter-unsafe";
    out->view = "text-login-fallback";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->route_selected = 0;
    out->route_blocked = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->credential_screen_visible = 0;
    out->credential_panel_visible = 0;
    out->credential_input_visible = 0;
    out->credential_input_focus = 0;
    out->text_recovery_visible = 0;
    out->text_recovery_open = 0;
    out->text_login_visible = 1;
    out->text_login_resume = 0;
    out->text_login_forced = 1;
    out->fallback_notice_visible = 1;
    out->text_login_notice_visible = 1;
    out->status_visible = 1;
    out->error_visible = 1;
    out->submit_blocked = 1;
    out->submit_enabled = 0;
    out->auth_attempt_allowed = 0;
    return 0;
  }
  out->presenter_safe = 1;
  out->error_visible = 0;
  if (controller->submit_requested) {
    out->view = "text-login-fallback";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->text_login_visible = 1;
    out->text_login_forced = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->credential_input_focus = 0;
    out->state = "presenter-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (controller->credential_screen_visible && controller->credential_input_focus &&
      out->action_allowed && !out->action_blocked && out->input_focus_allowed) {
    out->view = "credential-screen";
    out->focus_target = "credential-input";
    out->primary_action = "edit-credential";
    out->credential_panel_visible = 1;
    out->credential_input_visible = 1;
    out->credential_input_focus = 1;
    out->text_login_visible = 0;
    out->text_login_forced = 0;
    out->fallback_notice_visible = 1;
    out->text_login_notice_visible = 1;
    out->state = "presenter-credential-ready";
    out->message = "Credential screen ready for focus; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (controller->text_recovery_open && out->recovery_text_session_required) {
    out->view = "text-recovery";
    out->focus_target = "none";
    out->primary_action = "open-text-recovery";
    out->text_recovery_visible = 1;
    out->text_login_visible = 1;
    out->text_login_forced = 1;
    out->input_focus_allowed = 0;
    out->credential_input_focus = 0;
    out->state = "presenter-text-recovery-ready";
    out->message = "Text recovery presentation is ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (controller->text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->view = "text-login";
    out->focus_target = "none";
    out->primary_action = "resume-text-login";
    out->text_login_visible = 1;
    out->text_login_forced = 1;
    out->input_focus_allowed = 0;
    out->credential_input_focus = 0;
    out->state = "presenter-resume-ready";
    out->message = "Text login resume presentation is ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (controller->text_login_forced) {
    out->view = "text-login-fallback";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->text_login_visible = 1;
    out->text_login_forced = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->credential_input_focus = 0;
    out->state = "presenter-text-login-ready";
    out->message = "Use authoritative text login.";
    return 0;
  }
  out->event_type = "credential-screen-presenter-blocked";
  out->state = "blocked";
  out->message = "Credential screen presenter blocked; use text login.";
  out->blocked_reason = "credential-presenter-blocked";
  out->view = "text-login-fallback";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->credential_input_focus = 0;
  out->text_login_forced = 1;
  out->error_visible = 1;
  return 0;
}


int login_window_credential_screen_binding_build(
    const struct login_window_credential_screen_presenter *presenter,
    struct login_window_credential_screen_binding *out) {
  int presenter_safe = 0;
  if (!out) return -1;
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_BINDING_VERSION;
  out->presenter_available = presenter ? 1 : 0;
  out->presenter_safe = 0;
  out->binding_safe = 0;
  out->requested_action = 0;
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->window_mount_required = 1;
  out->credential_panel_bound = 0;
  out->credential_input_bound = 0;
  out->credential_input_focus_requested = 0;
  out->text_recovery_bound = 0;
  out->text_login_bound = 1;
  out->text_login_resume_bound = 0;
  out->text_login_fallback_bound = 1;
  out->fallback_notice_bound = 1;
  out->text_login_notice_bound = 1;
  out->status_bound = 1;
  out->error_bound = 1;
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
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->event_type = "credential-screen-binding-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "presenter-unavailable";
  if (!presenter) return 0;
  out->requested_action = presenter->requested_action;
  out->presenter_safe = presenter->presenter_safe ? 1 : 0;
  out->route_selected = presenter->route_selected ? 1 : 0;
  out->route_blocked = presenter->route_blocked ? 1 : 0;
  out->action_allowed = presenter->action_allowed ? 1 : 0;
  out->action_blocked = presenter->action_blocked ? 1 : 0;
  out->input_focus_allowed = presenter->input_focus_allowed ? 1 : 0;
  out->recovery_text_session_required = presenter->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = presenter->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = presenter->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = presenter->credential_session_safe ? 1 : 0;
  out->credential_storage_wiped = presenter->credential_storage_wiped ? 1 : 0;
  out->credential_redacted = presenter->credential_redacted ? 1 : 0;
  out->length_redacted = presenter->length_redacted ? 1 : 0;
  out->raw_secret_exposed = presenter->raw_secret_exposed ? 1 : 0;
  out->masked_text_exposed = presenter->masked_text_exposed ? 1 : 0;
  out->submit_requested = presenter->submit_requested ? 1 : 0;
  out->submit_blocked = 1;
  out->submit_enabled = 0;
  out->auth_attempt_allowed = 0;
  out->text_login_authoritative = presenter->text_login_authoritative ? 1 : 0;
  out->view = presenter->view ? presenter->view : "text-login-fallback";
  out->focus_target = presenter->focus_target ? presenter->focus_target : "none";
  out->primary_action = presenter->primary_action ? presenter->primary_action
                                                   : "use-text-login";
  out->route = presenter->route ? presenter->route : "force-text-login";
  out->event_type = presenter->event_type ? presenter->event_type
                                          : "credential-screen-presenter-blocked";
  out->state = presenter->state ? presenter->state : "blocked";
  out->message = presenter->message ? presenter->message
                                    : "Text login remains authoritative.";
  out->blocked_reason = presenter->blocked_reason ? presenter->blocked_reason
                                                  : "blocked";
  presenter_safe = out->presenter_safe && out->route_selected &&
                   !out->route_blocked && out->credential_session_safe &&
                   out->credential_storage_wiped && out->credential_redacted &&
                   out->length_redacted && !out->raw_secret_exposed &&
                   !out->masked_text_exposed && presenter->submit_blocked &&
                   !presenter->submit_enabled && !presenter->auth_attempt_allowed &&
                   out->text_login_authoritative;
  if (!presenter_safe) {
    out->event_type = "credential-screen-binding-unsafe";
    out->state = "blocked";
    out->message = "Credential screen binding unsafe; use text login.";
    out->blocked_reason = "credential-binding-unsafe";
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->route_selected = 0;
    out->route_blocked = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->credential_panel_bound = 0;
    out->credential_input_bound = 0;
    out->credential_input_focus_requested = 0;
    out->text_recovery_bound = 0;
    out->text_login_bound = 1;
    out->text_login_resume_bound = 0;
    out->text_login_fallback_bound = 1;
    out->fallback_notice_bound = 1;
    out->text_login_notice_bound = 1;
    out->status_bound = 1;
    out->error_bound = 1;
    out->submit_blocked = 1;
    out->submit_enabled = 0;
    out->auth_attempt_allowed = 0;
    return 0;
  }
  out->binding_safe = 1;
  out->error_bound = 0;
  if (presenter->submit_requested) {
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->text_login_bound = 1;
    out->text_login_fallback_bound = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->credential_input_focus_requested = 0;
    out->state = "binding-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (presenter->credential_screen_visible && presenter->credential_panel_visible &&
      presenter->credential_input_visible && presenter->credential_input_focus &&
      out->action_allowed && !out->action_blocked && out->input_focus_allowed) {
    out->view = "credential-screen";
    out->widget_tree = "credential-screen-bindings";
    out->focus_target = "credential-input";
    out->primary_action = "edit-credential";
    out->credential_panel_bound = 1;
    out->credential_input_bound = 1;
    out->credential_input_focus_requested = 1;
    out->text_login_bound = 0;
    out->text_login_fallback_bound = 0;
    out->state = "binding-credential-ready";
    out->message = "Credential widgets may be mounted; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (presenter->text_recovery_visible && presenter->text_recovery_open &&
      out->recovery_text_session_required) {
    out->view = "text-recovery";
    out->widget_tree = "text-recovery-bindings";
    out->focus_target = "none";
    out->primary_action = "open-text-recovery";
    out->text_recovery_bound = 1;
    out->text_login_bound = 1;
    out->text_login_fallback_bound = 0;
    out->input_focus_allowed = 0;
    out->credential_input_focus_requested = 0;
    out->state = "binding-text-recovery-ready";
    out->message = "Text recovery widgets may be mounted; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (presenter->text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->view = "text-login";
    out->widget_tree = "text-login-resume-bindings";
    out->focus_target = "none";
    out->primary_action = "resume-text-login";
    out->text_login_bound = 1;
    out->text_login_resume_bound = 1;
    out->text_login_fallback_bound = 0;
    out->input_focus_allowed = 0;
    out->credential_input_focus_requested = 0;
    out->state = "binding-resume-ready";
    out->message = "Text login resume widgets may be mounted; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (presenter->text_login_forced) {
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->text_login_bound = 1;
    out->text_login_fallback_bound = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->credential_input_focus_requested = 0;
    out->state = "binding-text-login-ready";
    out->message = "Use authoritative text login.";
    return 0;
  }
  out->event_type = "credential-screen-binding-blocked";
  out->state = "blocked";
  out->message = "Credential screen binding blocked; use text login.";
  out->blocked_reason = "credential-binding-blocked";
  out->view = "text-login-fallback";
  out->widget_tree = "text-login-fallback-bindings";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->credential_input_focus_requested = 0;
  out->text_login_bound = 1;
  out->text_login_fallback_bound = 1;
  out->error_bound = 1;
  return 0;
}
