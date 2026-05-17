/*
 * src/auth/login_runtime/mount_commit.c
 *
 * Credential-screen mount + commit plan builders — extracted
 * byte-for-byte from `src/auth/login_runtime.c` during PR C.10 of
 * the Estagio C dedicated plan.  Hosts the two contiguous pipeline
 * builders that finalise the screen's transition into a mounted /
 * committed state:
 *
 *   - login_window_credential_screen_mount_plan_build
 *   - login_window_credential_screen_commit_plan_build
 *
 * The mount-plan owns the lifecycle assertion that the binding has
 * been successfully attached to a presenter; the commit-plan owns
 * the submit + auth callback bindings that flow back from the
 * mounted presenter to the credential dispatch.  Both are
 * fail-closed.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

int login_window_credential_screen_mount_plan_build(
    const struct login_window_credential_screen_binding *binding,
    struct login_window_credential_screen_mount_plan *out) {
  int binding_safe = 0;
  if (!out) return -1;
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_MOUNT_PLAN_VERSION;
  out->binding_available = binding ? 1 : 0;
  out->binding_safe = 0;
  out->mount_plan_safe = 0;
  out->requested_action = 0;
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->window_transaction_required = 1;
  out->window_mount_allowed = 0;
  out->widget_tree_selected = 0;
  out->mount_credential_panel = 0;
  out->mount_credential_input = 0;
  out->request_credential_focus = 0;
  out->mount_text_recovery = 0;
  out->mount_text_login = 1;
  out->mount_text_login_resume = 0;
  out->mount_text_login_fallback = 1;
  out->mount_fallback_notice = 1;
  out->mount_text_login_notice = 1;
  out->mount_status = 1;
  out->mount_error = 1;
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
  out->mount_transaction = "text-login-fallback-mount-plan";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->event_type = "credential-screen-mount-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "binding-unavailable";
  if (!binding) return 0;
  out->requested_action = binding->requested_action;
  out->binding_safe = binding->binding_safe ? 1 : 0;
  out->route_selected = binding->route_selected ? 1 : 0;
  out->route_blocked = binding->route_blocked ? 1 : 0;
  out->action_allowed = binding->action_allowed ? 1 : 0;
  out->action_blocked = binding->action_blocked ? 1 : 0;
  out->input_focus_allowed = binding->input_focus_allowed ? 1 : 0;
  out->recovery_text_session_required = binding->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = binding->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = binding->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = binding->credential_session_safe ? 1 : 0;
  out->credential_storage_wiped = binding->credential_storage_wiped ? 1 : 0;
  out->credential_redacted = binding->credential_redacted ? 1 : 0;
  out->length_redacted = binding->length_redacted ? 1 : 0;
  out->raw_secret_exposed = binding->raw_secret_exposed ? 1 : 0;
  out->masked_text_exposed = binding->masked_text_exposed ? 1 : 0;
  out->submit_requested = binding->submit_requested ? 1 : 0;
  out->submit_blocked = 1;
  out->submit_enabled = 0;
  out->auth_attempt_allowed = 0;
  out->text_login_authoritative = binding->text_login_authoritative ? 1 : 0;
  out->view = binding->view ? binding->view : "text-login-fallback";
  out->widget_tree = binding->widget_tree ? binding->widget_tree
                                          : "text-login-fallback-bindings";
  out->focus_target = binding->focus_target ? binding->focus_target : "none";
  out->primary_action = binding->primary_action ? binding->primary_action
                                                : "use-text-login";
  out->route = binding->route ? binding->route : "force-text-login";
  out->event_type = binding->event_type ? binding->event_type
                                        : "credential-screen-binding-blocked";
  out->state = binding->state ? binding->state : "blocked";
  out->message = binding->message ? binding->message
                                  : "Text login remains authoritative.";
  out->blocked_reason = binding->blocked_reason ? binding->blocked_reason
                                                : "blocked";
  binding_safe = out->binding_safe && out->route_selected &&
                 !out->route_blocked && out->credential_session_safe &&
                 out->credential_storage_wiped && out->credential_redacted &&
                 out->length_redacted && !out->raw_secret_exposed &&
                 !out->masked_text_exposed && binding->submit_blocked &&
                 !binding->submit_enabled && !binding->auth_attempt_allowed &&
                 out->text_login_authoritative;
  if (!binding_safe) {
    out->event_type = "credential-screen-mount-plan-unsafe";
    out->state = "blocked";
    out->message = "Credential screen mount plan unsafe; use text login.";
    out->blocked_reason = "credential-mount-plan-unsafe";
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->mount_transaction = "text-login-fallback-mount-plan";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->route_selected = 0;
    out->route_blocked = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->window_mount_allowed = 0;
    out->widget_tree_selected = 0;
    out->mount_credential_panel = 0;
    out->mount_credential_input = 0;
    out->request_credential_focus = 0;
    out->mount_text_recovery = 0;
    out->mount_text_login = 1;
    out->mount_text_login_resume = 0;
    out->mount_text_login_fallback = 1;
    out->mount_fallback_notice = 1;
    out->mount_text_login_notice = 1;
    out->mount_status = 1;
    out->mount_error = 1;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->submit_blocked = 1;
    out->submit_enabled = 0;
    out->auth_attempt_allowed = 0;
    return 0;
  }
  out->mount_plan_safe = 1;
  out->window_mount_allowed = 1;
  out->widget_tree_selected = 1;
  out->mount_error = 0;
  if (binding->submit_requested) {
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->mount_transaction = "text-login-fallback-mount-plan";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->mount_text_login = 1;
    out->mount_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->request_credential_focus = 0;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->state = "mount-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (binding->credential_panel_bound && binding->credential_input_bound &&
      binding->credential_input_focus_requested && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->view = "credential-screen";
    out->widget_tree = "credential-screen-bindings";
    out->mount_transaction = "credential-screen-mount-plan";
    out->focus_target = "credential-input";
    out->primary_action = "edit-credential";
    out->mount_credential_panel = 1;
    out->mount_credential_input = 1;
    out->request_credential_focus = 1;
    out->mount_text_login = 0;
    out->mount_text_login_fallback = 0;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->state = "mount-credential-ready";
    out->message = "Credential widget mount plan ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (binding->text_recovery_bound && out->recovery_text_session_required) {
    out->view = "text-recovery";
    out->widget_tree = "text-recovery-bindings";
    out->mount_transaction = "text-recovery-mount-plan";
    out->focus_target = "none";
    out->primary_action = "open-text-recovery";
    out->mount_text_recovery = 1;
    out->mount_text_login = 1;
    out->mount_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->request_credential_focus = 0;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->state = "mount-text-recovery-ready";
    out->message = "Text recovery mount plan ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (binding->text_login_resume_bound && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->view = "text-login";
    out->widget_tree = "text-login-resume-bindings";
    out->mount_transaction = "text-login-resume-mount-plan";
    out->focus_target = "none";
    out->primary_action = "resume-text-login";
    out->mount_text_login = 1;
    out->mount_text_login_resume = 1;
    out->mount_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->request_credential_focus = 0;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->state = "mount-resume-ready";
    out->message = "Text login resume mount plan ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (binding->text_login_fallback_bound) {
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->mount_transaction = "text-login-fallback-mount-plan";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->mount_text_login = 1;
    out->mount_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->request_credential_focus = 0;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->state = "mount-text-login-ready";
    out->message = "Use authoritative text login.";
    return 0;
  }
  out->event_type = "credential-screen-mount-plan-blocked";
  out->state = "blocked";
  out->message = "Credential screen mount plan blocked; use text login.";
  out->blocked_reason = "credential-mount-plan-blocked";
  out->view = "text-login-fallback";
  out->widget_tree = "text-login-fallback-bindings";
  out->mount_transaction = "text-login-fallback-mount-plan";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->request_credential_focus = 0;
  out->mount_text_login = 1;
  out->mount_text_login_fallback = 1;
  out->mount_error = 1;
  out->submit_callback_bound = 0;
  out->auth_callback_bound = 0;
  return 0;
}


int login_window_credential_screen_commit_plan_build(
    const struct login_window_credential_screen_mount_plan *mount_plan,
    struct login_window_credential_screen_commit_plan *out) {
  int mount_safe = 0;
  if (!out) return -1;
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_COMMIT_PLAN_VERSION;
  out->mount_plan_available = mount_plan ? 1 : 0;
  out->mount_plan_safe = 0;
  out->commit_plan_safe = 0;
  out->requested_action = 0;
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->window_commit_required = 1;
  out->window_commit_allowed = 0;
  out->window_commit_executed = 0;
  out->widget_tree_selected = 0;
  out->commit_credential_panel = 0;
  out->commit_credential_input = 0;
  out->commit_credential_focus = 0;
  out->commit_text_recovery = 0;
  out->commit_text_login = 1;
  out->commit_text_login_resume = 0;
  out->commit_text_login_fallback = 1;
  out->commit_fallback_notice = 1;
  out->commit_text_login_notice = 1;
  out->commit_status = 1;
  out->commit_error = 1;
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
  out->mount_transaction = "text-login-fallback-mount-plan";
  out->commit_transaction = "text-login-fallback-commit-plan";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->event_type = "credential-screen-commit-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "mount-plan-unavailable";
  if (!mount_plan) return 0;
  out->requested_action = mount_plan->requested_action;
  out->mount_plan_safe = mount_plan->mount_plan_safe ? 1 : 0;
  out->route_selected = mount_plan->route_selected ? 1 : 0;
  out->route_blocked = mount_plan->route_blocked ? 1 : 0;
  out->action_allowed = mount_plan->action_allowed ? 1 : 0;
  out->action_blocked = mount_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = mount_plan->input_focus_allowed ? 1 : 0;
  out->widget_tree_selected = mount_plan->widget_tree_selected ? 1 : 0;
  out->recovery_text_session_required = mount_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = mount_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = mount_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = mount_plan->credential_session_safe ? 1 : 0;
  out->credential_storage_wiped = mount_plan->credential_storage_wiped ? 1 : 0;
  out->credential_redacted = mount_plan->credential_redacted ? 1 : 0;
  out->length_redacted = mount_plan->length_redacted ? 1 : 0;
  out->raw_secret_exposed = mount_plan->raw_secret_exposed ? 1 : 0;
  out->masked_text_exposed = mount_plan->masked_text_exposed ? 1 : 0;
  out->submit_requested = mount_plan->submit_requested ? 1 : 0;
  out->submit_blocked = 1;
  out->submit_enabled = 0;
  out->auth_attempt_allowed = 0;
  out->text_login_authoritative = mount_plan->text_login_authoritative ? 1 : 0;
  out->view = mount_plan->view ? mount_plan->view : "text-login-fallback";
  out->widget_tree = mount_plan->widget_tree ? mount_plan->widget_tree
                                             : "text-login-fallback-bindings";
  out->mount_transaction = mount_plan->mount_transaction ? mount_plan->mount_transaction
                                                         : "text-login-fallback-mount-plan";
  out->focus_target = mount_plan->focus_target ? mount_plan->focus_target : "none";
  out->primary_action = mount_plan->primary_action ? mount_plan->primary_action
                                                   : "use-text-login";
  out->route = mount_plan->route ? mount_plan->route : "force-text-login";
  out->event_type = mount_plan->event_type ? mount_plan->event_type
                                           : "credential-screen-mount-plan-blocked";
  out->state = mount_plan->state ? mount_plan->state : "blocked";
  out->message = mount_plan->message ? mount_plan->message
                                     : "Text login remains authoritative.";
  out->blocked_reason = mount_plan->blocked_reason ? mount_plan->blocked_reason
                                                   : "blocked";
  mount_safe = out->mount_plan_safe && mount_plan->window_mount_allowed &&
               out->widget_tree_selected && out->route_selected &&
               !out->route_blocked && out->credential_session_safe &&
               out->credential_storage_wiped && out->credential_redacted &&
               out->length_redacted && !out->raw_secret_exposed &&
               !out->masked_text_exposed && mount_plan->submit_blocked &&
               !mount_plan->submit_enabled && !mount_plan->auth_attempt_allowed &&
               !mount_plan->submit_callback_bound && !mount_plan->auth_callback_bound &&
               out->text_login_authoritative;
  if (!mount_safe) {
    out->event_type = "credential-screen-commit-plan-unsafe";
    out->state = "blocked";
    out->message = "Credential screen commit plan unsafe; use text login.";
    out->blocked_reason = "credential-commit-plan-unsafe";
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->mount_transaction = "text-login-fallback-mount-plan";
    out->commit_transaction = "text-login-fallback-commit-plan";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->route_selected = 0;
    out->route_blocked = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->window_commit_allowed = 0;
    out->window_commit_executed = 0;
    out->widget_tree_selected = 0;
    out->commit_credential_panel = 0;
    out->commit_credential_input = 0;
    out->commit_credential_focus = 0;
    out->commit_text_recovery = 0;
    out->commit_text_login = 1;
    out->commit_text_login_resume = 0;
    out->commit_text_login_fallback = 1;
    out->commit_fallback_notice = 1;
    out->commit_text_login_notice = 1;
    out->commit_status = 1;
    out->commit_error = 1;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->submit_blocked = 1;
    out->submit_enabled = 0;
    out->auth_attempt_allowed = 0;
    return 0;
  }
  out->commit_plan_safe = 1;
  out->window_commit_allowed = 1;
  out->window_commit_executed = 0;
  out->commit_error = 0;
  if (mount_plan->submit_requested) {
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->mount_transaction = "text-login-fallback-mount-plan";
    out->commit_transaction = "text-login-fallback-commit-plan";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->commit_text_login = 1;
    out->commit_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->commit_credential_focus = 0;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->state = "commit-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (mount_plan->mount_credential_panel && mount_plan->mount_credential_input &&
      mount_plan->request_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->view = "credential-screen";
    out->widget_tree = "credential-screen-bindings";
    out->mount_transaction = "credential-screen-mount-plan";
    out->commit_transaction = "credential-screen-commit-plan";
    out->focus_target = "credential-input";
    out->primary_action = "edit-credential";
    out->commit_credential_panel = 1;
    out->commit_credential_input = 1;
    out->commit_credential_focus = 1;
    out->commit_text_login = 0;
    out->commit_text_login_fallback = 0;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->state = "commit-credential-ready";
    out->message = "Credential window commit plan ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (mount_plan->mount_text_recovery && out->recovery_text_session_required) {
    out->view = "text-recovery";
    out->widget_tree = "text-recovery-bindings";
    out->mount_transaction = "text-recovery-mount-plan";
    out->commit_transaction = "text-recovery-commit-plan";
    out->focus_target = "none";
    out->primary_action = "open-text-recovery";
    out->commit_text_recovery = 1;
    out->commit_text_login = 1;
    out->commit_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->commit_credential_focus = 0;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->state = "commit-text-recovery-ready";
    out->message = "Text recovery commit plan ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (mount_plan->mount_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->view = "text-login";
    out->widget_tree = "text-login-resume-bindings";
    out->mount_transaction = "text-login-resume-mount-plan";
    out->commit_transaction = "text-login-resume-commit-plan";
    out->focus_target = "none";
    out->primary_action = "resume-text-login";
    out->commit_text_login = 1;
    out->commit_text_login_resume = 1;
    out->commit_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->commit_credential_focus = 0;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->state = "commit-resume-ready";
    out->message = "Text login resume commit plan ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (mount_plan->mount_text_login_fallback) {
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->mount_transaction = "text-login-fallback-mount-plan";
    out->commit_transaction = "text-login-fallback-commit-plan";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->commit_text_login = 1;
    out->commit_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->commit_credential_focus = 0;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->state = "commit-text-login-ready";
    out->message = "Use authoritative text login.";
    return 0;
  }
  out->event_type = "credential-screen-commit-plan-blocked";
  out->state = "blocked";
  out->message = "Credential screen commit plan blocked; use text login.";
  out->blocked_reason = "credential-commit-plan-blocked";
  out->view = "text-login-fallback";
  out->widget_tree = "text-login-fallback-bindings";
  out->mount_transaction = "text-login-fallback-mount-plan";
  out->commit_transaction = "text-login-fallback-commit-plan";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->commit_credential_focus = 0;
  out->commit_text_login = 1;
  out->commit_text_login_fallback = 1;
  out->commit_error = 1;
  out->submit_callback_bound = 0;
  out->auth_callback_bound = 0;
  return 0;
}
