/*
 * src/auth/login_runtime/credential_view_model.c
 *
 * Credential view-model construction layer — extracted byte-for-byte
 * from `src/auth/login_runtime.c` during PR C.5 of the Estagio C
 * dedicated plan.  Hosts the five view-model builders that project
 * the credential + readiness + audit state into the structures the
 * screen pipeline consumes:
 *
 *   - login_window_credential_view_model_build
 *   - login_window_credential_ui_step_build
 *   - login_window_credential_ui_session_build
 *   - login_window_credential_recovery_view_model_build
 *   - login_window_credential_screen_view_model_build
 *
 * All five functions are pure transformers: no I/O, no global state,
 * no allocations.  They consume snapshots of the credential pipeline
 * (policy/buffer/readiness/audit) and emit immutable view-model
 * structs documented in `include/auth/login_runtime/`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

int login_window_credential_view_model_build(
    const struct login_window_credential_readiness *readiness,
    const struct login_window_credential_audit_event *audit,
    struct login_window_credential_view_model *out) {
  int audit_redacted = 0;
  if (!out) return -1;
  out->version = LOGIN_WINDOW_CREDENTIAL_VIEW_MODEL_VERSION;
  out->readiness_available = 0;
  out->audit_available = 0;
  out->renderable = 0;
  out->input_enabled = 0;
  out->password_field_visible = 0;
  out->masked_text_ready = 0;
  out->masked_text_redacted = 1;
  out->credential_present = 0;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_visible = 0;
  out->submit_enabled = 0;
  out->submit_blocked = 1;
  out->cancel_visible = 0;
  out->cancel_enabled = 0;
  out->wipe_required = 1;
  out->wipe_attempted = 0;
  out->wipe_succeeded = 0;
  out->status_visible = 1;
  out->error_visible = 1;
  out->fallback_required = 1;
  out->raw_secret_exposed = 0;
  out->masked_text_exposed = 0;
  out->auth_attempt_allowed = 0;
  out->text_login_authoritative = 1;
  out->event_type = "credential-view-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "audit-unavailable";
  if (!audit) return 0;
  out->audit_available = 1;
  out->event_type = audit->event_type ? audit->event_type : "credential-blocked";
  out->state = audit->state ? audit->state : "blocked";
  out->blocked_reason = audit->blocked_reason ? audit->blocked_reason
                                               : "blocked";
  out->readiness_available = readiness ? 1 : 0;
  if (!readiness || !audit->readiness_available) {
    out->blocked_reason = "readiness-unavailable";
    return 0;
  }
  out->readiness_available = 1;
  out->raw_secret_exposed = audit->raw_secret_exposed ? 1 : 0;
  out->masked_text_exposed = audit->masked_text_exposed ? 1 : 0;
  audit_redacted = !out->raw_secret_exposed && !out->masked_text_exposed &&
                   audit->secret_redacted && audit->length_redacted;
  out->credential_redacted = audit->secret_redacted ? 1 : 0;
  out->length_redacted = audit->length_redacted ? 1 : 0;
  out->masked_text_redacted = audit->masked_text_exposed ? 0 : 1;
  if (!audit_redacted) {
    out->event_type = "credential-view-blocked";
    out->state = "blocked";
    out->blocked_reason = "credential-audit-unsafe";
    return 0;
  }
  out->credential_present = audit->credential_present ? 1 : 0;
  out->masked_text_ready =
      readiness->ready_for_masked_text && audit->ready_for_masked_text ? 1 : 0;
  out->wipe_required = audit->wipe_required ? 1 : 0;
  out->wipe_attempted = audit->wipe_attempted ? 1 : 0;
  out->wipe_succeeded = audit->wipe_succeeded ? 1 : 0;
  out->text_login_authoritative = audit->text_login_authoritative ? 1 : 0;
  out->renderable = readiness->ready_for_render && audit->ready_for_render &&
                    out->masked_text_ready && out->text_login_authoritative
                        ? 1
                        : 0;
  out->password_field_visible = out->renderable ? 1 : 0;
  out->input_enabled = out->renderable && readiness->ready_for_input &&
                       audit->ready_for_input && !audit->input_blocked &&
                       !audit->submit_attempted && !audit->cancel_consumed
                           ? 1
                           : 0;
  out->cancel_visible = out->renderable ? 1 : 0;
  out->cancel_enabled = out->input_enabled || out->credential_present ? 1 : 0;
  if (!out->renderable) {
    out->message = "Credential field unavailable; use text login.";
  } else if (audit->submit_attempted) {
    out->message = "Submit blocked; use text login.";
  } else if (audit->cancel_consumed) {
    out->message = "Credential input cancelled and wiped.";
  } else if (audit->input_blocked) {
    out->message = "Credential input blocked; use text login.";
  } else {
    out->message = "Credential field ready; text login remains authoritative.";
    out->error_visible = 0;
  }
  out->submit_visible = 0;
  out->submit_enabled = 0;
  out->submit_blocked = 1;
  out->auth_attempt_allowed = 0;
  out->fallback_required = 1;
  return 0;
}



int login_window_credential_ui_step_build(
    const struct login_window_credential_policy *policy,
    struct login_window_credential_buffer *buffer, int action, char ch,
    char *masked_out, size_t masked_out_size,
    struct login_window_credential_ui_step *out) {
  struct login_window_credential_interaction interaction;
  struct login_window_credential_readiness readiness;
  struct login_window_credential_audit_event audit;
  struct login_window_credential_view_model view;
  if (masked_out && masked_out_size > 0) {
    masked_out[0] = '\0';
  }
  if (!out) return -1;
  out->version = LOGIN_WINDOW_CREDENTIAL_UI_STEP_VERSION;
  out->action = action;
  out->interaction_built = 0;
  out->readiness_built = 0;
  out->audit_built = 0;
  out->view_built = 0;
  out->input_applied = 0;
  out->input_accepted = 0;
  out->input_blocked = 0;
  out->buffer_changed = 0;
  out->renderable = 0;
  out->input_enabled = 0;
  out->credential_present = 0;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_blocked = 1;
  out->submit_enabled = 0;
  out->auth_attempt_allowed = 0;
  out->raw_secret_exposed = 0;
  out->masked_text_exposed = 0;
  out->wipe_required = 1;
  out->wipe_attempted = 0;
  out->wipe_succeeded = 0;
  out->event_type = "credential-ui-step-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "interaction-unavailable";
  if (login_window_credential_interaction_step(policy, buffer, action, ch,
                                               masked_out, masked_out_size,
                                               &interaction) != 0) {
    out->blocked_reason = "interaction-step-failed";
    return -1;
  }
  out->interaction_built = 1;
  out->input_applied = interaction.input_applied ? 1 : 0;
  out->input_accepted = interaction.input_accepted ? 1 : 0;
  out->input_blocked = interaction.input_blocked ? 1 : 0;
  out->buffer_changed = interaction.buffer_changed ? 1 : 0;
  if (login_window_credential_readiness_evaluate(policy, buffer,
                                                 &interaction.panel,
                                                 &interaction,
                                                 &readiness) != 0) {
    out->blocked_reason = "readiness-evaluate-failed";
    return -1;
  }
  out->readiness_built = 1;
  if (login_window_credential_audit_event_build(&readiness, &interaction,
                                                &audit) != 0) {
    out->blocked_reason = "audit-event-build-failed";
    return -1;
  }
  out->audit_built = 1;
  if (login_window_credential_view_model_build(&readiness, &audit,
                                               &view) != 0) {
    out->blocked_reason = "view-model-build-failed";
    return -1;
  }
  out->view_built = 1;
  out->renderable = view.renderable ? 1 : 0;
  out->input_enabled = view.input_enabled ? 1 : 0;
  out->credential_present = view.credential_present ? 1 : 0;
  out->credential_redacted = view.credential_redacted ? 1 : 0;
  out->length_redacted = view.length_redacted ? 1 : 0;
  out->submit_blocked = 1;
  out->submit_enabled = 0;
  out->auth_attempt_allowed = 0;
  out->raw_secret_exposed = 0;
  out->masked_text_exposed = 0;
  out->wipe_required = view.wipe_required ? 1 : 0;
  out->wipe_attempted = view.wipe_attempted ? 1 : 0;
  out->wipe_succeeded = view.wipe_succeeded ? 1 : 0;
  out->event_type = view.event_type ? view.event_type
                                    : "credential-blocked";
  out->state = view.state ? view.state : "blocked";
  out->message = view.message ? view.message
                              : "Text login remains authoritative.";
  out->blocked_reason = view.blocked_reason ? view.blocked_reason
                                            : "blocked";
  return 0;
}



int login_window_credential_ui_session_build(
    const struct login_window_credential_policy *policy,
    char *storage, size_t storage_size, int action, char ch,
    char *masked_scratch, size_t masked_scratch_size,
    struct login_window_credential_ui_session *out) {
  int wipe_rc = 0;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_ui_step step;
  if (masked_scratch && masked_scratch_size > 0) {
    masked_scratch[0] = '\0';
  }
  if (!out) return -1;
  out->version = LOGIN_WINDOW_CREDENTIAL_UI_SESSION_VERSION;
  out->action = action;
  out->policy_available = policy ? 1 : 0;
  out->storage_available = storage && storage_size > 0 ? 1 : 0;
  out->storage_cleared = 0;
  out->buffer_initialized = 0;
  out->step_built = 0;
  out->input_applied = 0;
  out->input_accepted = 0;
  out->input_blocked = 0;
  out->buffer_changed = 0;
  out->renderable = 0;
  out->input_enabled = 0;
  out->credential_present = 0;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_blocked = 1;
  out->submit_enabled = 0;
  out->auth_attempt_allowed = 0;
  out->raw_secret_exposed = 0;
  out->masked_text_exposed = 0;
  out->wipe_required = 1;
  out->wipe_attempted = 0;
  out->wipe_succeeded = 0;
  out->storage_wiped = 0;
  out->event_type = "credential-ui-session-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "storage-unavailable";
  if (login_window_credential_buffer_init(&buffer, storage, storage_size,
                                          policy) != 0) {
    out->blocked_reason = "credential-buffer-init-failed";
    return -1;
  }
  out->storage_cleared = out->storage_available ? 1 : 0;
  out->buffer_initialized = buffer.initialized ? 1 : 0;
  if (login_window_credential_ui_step_build(policy, &buffer, action, ch,
                                            masked_scratch,
                                            masked_scratch_size,
                                            &step) != 0) {
    out->blocked_reason = "credential-ui-step-failed";
    (void)login_window_credential_buffer_wipe(&buffer);
    if (masked_scratch && masked_scratch_size > 0) {
      masked_scratch[0] = '\0';
    }
    return -1;
  }
  out->step_built = 1;
  out->input_applied = step.input_applied ? 1 : 0;
  out->input_accepted = step.input_accepted ? 1 : 0;
  out->input_blocked = step.input_blocked ? 1 : 0;
  out->buffer_changed = step.buffer_changed ? 1 : 0;
  out->renderable = step.renderable ? 1 : 0;
  out->input_enabled = step.input_enabled ? 1 : 0;
  out->credential_present = step.credential_present ? 1 : 0;
  out->credential_redacted = step.credential_redacted ? 1 : 0;
  out->length_redacted = step.length_redacted ? 1 : 0;
  out->wipe_required = step.wipe_required ? 1 : 0;
  out->wipe_attempted = step.wipe_attempted ? 1 : 0;
  out->wipe_succeeded = step.wipe_succeeded ? 1 : 0;
  out->event_type = step.event_type ? step.event_type
                                    : "credential-blocked";
  out->state = step.state ? step.state : "blocked";
  out->message = step.message ? step.message
                              : "Text login remains authoritative.";
  out->blocked_reason = step.blocked_reason ? step.blocked_reason
                                            : "blocked";
  out->submit_blocked = 1;
  out->submit_enabled = 0;
  out->auth_attempt_allowed = 0;
  out->raw_secret_exposed = 0;
  out->masked_text_exposed = 0;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->wipe_attempted = 1;
  wipe_rc = login_window_credential_buffer_wipe(&buffer);
  out->storage_wiped = wipe_rc == 0 && out->storage_available ? 1 : 0;
  out->wipe_succeeded = wipe_rc == 0 ? 1 : 0;
  out->credential_present = 0;
  if (wipe_rc != 0) {
    out->event_type = "credential-ui-session-blocked";
    out->state = "blocked";
    out->message = "Credential storage wipe failed; use text login.";
    out->blocked_reason = "credential-wipe-failed";
  }
  if (masked_scratch && masked_scratch_size > 0) {
    masked_scratch[0] = '\0';
  }
  return 0;
}


int login_window_credential_recovery_view_model_build(
    const struct login_window_contract *contract,
    const struct login_window_credential_policy *policy,
    const struct login_window_credential_ui_session *credential_session,
    const struct login_recovery_resume_policy *resume_policy,
    struct login_window_credential_recovery_view_model *out) {
  int session_safe = 0;
  if (!out) return -1;
  out->version = LOGIN_WINDOW_CREDENTIAL_RECOVERY_VIEW_MODEL_VERSION;
  out->contract_available = contract ? 1 : 0;
  out->policy_available = policy ? 1 : 0;
  out->credential_session_available = credential_session ? 1 : 0;
  out->resume_policy_available = resume_policy ? 1 : 0;
  out->recovery_visible = 0;
  out->recovery_enabled = 0;
  out->recovery_text_session_required = 1;
  out->resume_visible = 0;
  out->resume_enabled = 0;
  out->session_reset_required = 1;
  out->login_screen_rerender_required = 1;
  out->fallback_required = 1;
  out->credential_session_safe = 0;
  out->credential_storage_wiped = 0;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->raw_secret_exposed = 0;
  out->masked_text_exposed = 0;
  out->submit_blocked = 1;
  out->submit_enabled = 0;
  out->auth_attempt_allowed = 0;
  out->text_login_authoritative = 1;
  out->state = "blocked";
  out->message = "Text recovery remains authoritative.";
  out->blocked_reason = "policy-unavailable";
  if (!policy) return 0;
  out->recovery_text_session_required = policy->recovery_requires_text_session ? 1 : 0;
  out->text_login_authoritative = policy->text_login_authoritative ? 1 : 0;
  if (!out->text_login_authoritative) {
    out->blocked_reason = "text-login-not-authoritative";
    return 0;
  }
  if (!credential_session) {
    out->blocked_reason = "credential-session-unavailable";
    return 0;
  }
  out->credential_storage_wiped = credential_session->storage_wiped ? 1 : 0;
  out->credential_redacted = credential_session->credential_redacted ? 1 : 0;
  out->length_redacted = credential_session->length_redacted ? 1 : 0;
  out->raw_secret_exposed = credential_session->raw_secret_exposed ? 1 : 0;
  out->masked_text_exposed = credential_session->masked_text_exposed ? 1 : 0;
  session_safe = !credential_session->credential_present &&
                 credential_session->storage_wiped &&
                 out->credential_redacted && out->length_redacted &&
                 !out->raw_secret_exposed && !out->masked_text_exposed &&
                 credential_session->submit_blocked &&
                 !credential_session->submit_enabled &&
                 !credential_session->auth_attempt_allowed;
  if (!session_safe) {
    out->state = "blocked";
    out->message = "Credential session unsafe; use text login.";
    out->blocked_reason = "credential-session-unsafe";
    return 0;
  }
  out->credential_session_safe = 1;
  if (resume_policy) {
    out->resume_visible = resume_policy->recovery_session_active ||
                          resume_policy->resume_requested ? 1 : 0;
    out->session_reset_required = resume_policy->session_reset_required ? 1 : 0;
    out->login_screen_rerender_required =
        resume_policy->login_screen_rerender_required ? 1 : 0;
    if (resume_policy->can_resume_normal_login) {
      out->resume_enabled = 1;
      out->state = "resume-ready";
      out->message = "Text recovery can return to normal login.";
      out->blocked_reason = "ready";
      return 0;
    }
    if (out->resume_visible) {
      out->state = "resume-blocked";
      out->message = "Recovery return is blocked; stay in text recovery.";
      out->blocked_reason = resume_policy->blocked_reason
                                ? resume_policy->blocked_reason
                                : "resume-blocked";
    }
  }
  if (policy->recovery_allowed) {
    if (!policy->recovery_requires_text_session) {
      out->state = "blocked";
      out->message = "Recovery policy unsafe; use text login.";
      out->blocked_reason = "recovery-policy-unsafe";
      return 0;
    }
    out->recovery_visible = 1;
    out->recovery_enabled = 1;
    out->state = "recovery-ready";
    out->message = "Text recovery is available; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (!out->resume_visible) {
    out->state = contract && contract->ready ? "login-ready" : "recovery-blocked";
    out->message = "Use text login; recovery remains text-session-only.";
    out->blocked_reason = "recovery-unavailable";
  }
  out->submit_blocked = 1;
  out->submit_enabled = 0;
  out->auth_attempt_allowed = 0;
  return 0;
}


int login_window_credential_screen_view_model_build(
    const struct login_window_contract *contract,
    const struct login_window_view_model *login_view,
    const struct login_window_credential_ui_session *credential_session,
    const struct login_window_credential_recovery_view_model *recovery_view,
    struct login_window_credential_screen_view_model *out) {
  int credential_safe = 0;
  int recovery_safe = 1;
  if (!out) return -1;
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_VIEW_MODEL_VERSION;
  out->contract_available = contract ? 1 : 0;
  out->login_view_available = login_view ? 1 : 0;
  out->credential_session_available = credential_session ? 1 : 0;
  out->recovery_view_available = recovery_view ? 1 : 0;
  out->renderable = 0;
  out->password_panel_visible = 0;
  out->password_input_enabled = 0;
  out->recovery_visible = 0;
  out->recovery_enabled = 0;
  out->resume_visible = 0;
  out->resume_enabled = 0;
  out->recovery_text_session_required = 1;
  out->session_reset_required = 1;
  out->login_screen_rerender_required = 1;
  out->fallback_required = 1;
  out->maintenance_notice = 0;
  out->credential_session_safe = 0;
  out->credential_storage_wiped = 0;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->raw_secret_exposed = 0;
  out->masked_text_exposed = 0;
  out->submit_visible = 0;
  out->submit_blocked = 1;
  out->submit_enabled = 0;
  out->auth_attempt_allowed = 0;
  out->text_login_authoritative = 1;
  out->title = "CapyOS";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "login-view-unavailable";
  if (!login_view) return 0;
  out->maintenance_notice = login_view->maintenance_notice ? 1 : 0;
  out->text_login_authoritative = login_view->text_login_authoritative ? 1 : 0;
  out->title = login_view->title ? login_view->title : "CapyOS";
  out->state = login_view->state ? login_view->state : "blocked";
  out->message = login_view->message ? login_view->message
                                     : "Text login remains authoritative.";
  out->blocked_reason = login_view->blocked_reason ? login_view->blocked_reason
                                                   : "blocked";
  if (!out->text_login_authoritative) {
    out->blocked_reason = "text-login-not-authoritative";
    return 0;
  }
  if (login_view->password_submit_enabled) {
    out->state = "blocked";
    out->message = "Graphical submit is unsafe; use text login.";
    out->blocked_reason = "gui-submit-enabled";
    return 0;
  }
  if (!credential_session) {
    out->blocked_reason = "credential-session-unavailable";
    return 0;
  }
  out->credential_storage_wiped = credential_session->storage_wiped ? 1 : 0;
  out->credential_redacted = credential_session->credential_redacted ? 1 : 0;
  out->length_redacted = credential_session->length_redacted ? 1 : 0;
  out->raw_secret_exposed = credential_session->raw_secret_exposed ? 1 : 0;
  out->masked_text_exposed = credential_session->masked_text_exposed ? 1 : 0;
  credential_safe = !credential_session->credential_present &&
                    credential_session->storage_wiped &&
                    out->credential_redacted && out->length_redacted &&
                    !out->raw_secret_exposed && !out->masked_text_exposed &&
                    credential_session->submit_blocked &&
                    !credential_session->submit_enabled &&
                    !credential_session->auth_attempt_allowed;
  if (!credential_safe) {
    out->state = "blocked";
    out->message = "Credential screen unsafe; use text login.";
    out->blocked_reason = "credential-session-unsafe";
    return 0;
  }
  out->credential_session_safe = 1;
  if (recovery_view) {
    out->recovery_text_session_required = recovery_view->recovery_text_session_required ? 1 : 0;
    out->session_reset_required = recovery_view->session_reset_required ? 1 : 0;
    out->login_screen_rerender_required =
        recovery_view->login_screen_rerender_required ? 1 : 0;
    out->recovery_visible = recovery_view->recovery_visible ? 1 : 0;
    out->recovery_enabled = recovery_view->recovery_enabled ? 1 : 0;
    out->resume_visible = recovery_view->resume_visible ? 1 : 0;
    out->resume_enabled = recovery_view->resume_enabled ? 1 : 0;
    out->raw_secret_exposed = recovery_view->raw_secret_exposed ? 1 : out->raw_secret_exposed;
    out->masked_text_exposed = recovery_view->masked_text_exposed ? 1 : out->masked_text_exposed;
    out->credential_redacted = recovery_view->credential_redacted ? out->credential_redacted : 0;
    out->length_redacted = recovery_view->length_redacted ? out->length_redacted : 0;
    recovery_safe = recovery_view->credential_session_safe &&
                    recovery_view->credential_storage_wiped &&
                    recovery_view->credential_redacted &&
                    recovery_view->length_redacted &&
                    !recovery_view->raw_secret_exposed &&
                    !recovery_view->masked_text_exposed &&
                    recovery_view->submit_blocked &&
                    !recovery_view->submit_enabled &&
                    !recovery_view->auth_attempt_allowed &&
                    recovery_view->text_login_authoritative &&
                    ((!recovery_view->recovery_visible &&
                      !recovery_view->recovery_enabled &&
                      !recovery_view->resume_visible &&
                      !recovery_view->resume_enabled) ||
                     recovery_view->recovery_text_session_required);
    if (!recovery_safe) {
      out->recovery_visible = 0;
      out->recovery_enabled = 0;
      out->resume_visible = 0;
      out->resume_enabled = 0;
      out->state = "blocked";
      out->message = "Recovery screen unsafe; use text login.";
      out->blocked_reason = "recovery-view-unsafe";
      return 0;
    }
  }
  out->renderable = login_view->renderable && credential_safe && recovery_safe ? 1 : 0;
  out->password_panel_visible = out->renderable && credential_session->renderable ? 1 : 0;
  out->password_input_enabled = out->password_panel_visible &&
                                credential_session->input_enabled ? 1 : 0;
  out->submit_visible = 0;
  out->submit_blocked = 1;
  out->submit_enabled = 0;
  out->auth_attempt_allowed = 0;
  out->fallback_required = 1;
  if (recovery_view && recovery_view->resume_enabled) {
    out->state = "resume-ready";
    out->message = recovery_view->message ? recovery_view->message
                                          : "Text recovery can return to normal login.";
    out->blocked_reason = recovery_view->blocked_reason ? recovery_view->blocked_reason
                                                        : "ready";
  } else if (recovery_view && recovery_view->recovery_enabled) {
    out->state = "recovery-ready";
    out->message = recovery_view->message ? recovery_view->message
                                          : "Text recovery is available.";
    out->blocked_reason = recovery_view->blocked_reason ? recovery_view->blocked_reason
                                                        : "text-recovery-only";
  } else if (out->password_panel_visible) {
    out->state = "credential-screen-ready";
    out->message = "Credential screen ready; text login remains authoritative.";
    out->blocked_reason = "gui-submit-disabled";
  }
  return 0;
}
