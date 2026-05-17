/*
 * src/auth/login_runtime/credential_interaction.c
 *
 * Higher-level credential interaction: per-step state machine,
 * submit-readiness predicate, and audit-event construction —
 * extracted byte-for-byte from `src/auth/login_runtime.c` during
 * PR C.4 of the Estagio C dedicated plan.  Hosts three public
 * functions:
 *
 *   - login_window_credential_interaction_step
 *   - login_window_credential_readiness_evaluate
 *   - login_window_credential_audit_event_build
 *
 * `_interaction_step` orchestrates buffer transitions for each
 * keystroke; `_readiness_evaluate` snapshots whether the buffer +
 * policy combination is safe to submit; `_audit_event_build`
 * stamps the structured audit envelope the screen pipeline forwards
 * to the audit/ledger stages.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

int login_window_credential_interaction_step(
    const struct login_window_credential_policy *policy,
    struct login_window_credential_buffer *buffer, int action, char ch,
    char *masked_out, size_t masked_out_size,
    struct login_window_credential_interaction *out) {
  if (masked_out && masked_out_size > 0) {
    masked_out[0] = '\0';
  }
  if (!out) return -1;
  out->version = LOGIN_WINDOW_CREDENTIAL_INTERACTION_VERSION;
  out->action = action;
  out->input_applied = 0;
  out->panel_built = 0;
  out->input_accepted = 0;
  out->input_blocked = 0;
  out->buffer_changed = 0;
  out->submit_attempted = 0;
  out->cancel_consumed = 0;
  out->wipe_required = 1;
  out->wipe_attempted = 0;
  out->wipe_succeeded = 0;
  out->masked_text_available = 0;
  out->masked_text_truncated = 0;
  out->submit_allowed = 0;
  out->auth_attempt_allowed = 0;
  out->text_login_authoritative =
      policy && policy->text_login_authoritative ? 1 : 0;
  out->length = buffer ? buffer->length : 0;
  out->state = "blocked";
  out->blocked_reason = "input-unhandled";
  if (login_window_credential_input_apply(policy, buffer, action, ch,
                                          &out->input_result) != 0) {
    out->blocked_reason = "input-apply-failed";
    return -1;
  }
  out->input_applied = 1;
  if (login_window_credential_panel_build(policy, buffer, &out->input_result,
                                          masked_out, masked_out_size,
                                          &out->panel) != 0) {
    out->blocked_reason = "panel-build-failed";
    return -1;
  }
  out->panel_built = 1;
  out->input_accepted = out->input_result.accepted ? 1 : 0;
  out->input_blocked = out->panel.input_blocked ? 1 : 0;
  out->buffer_changed = out->input_result.buffer_changed ? 1 : 0;
  out->submit_attempted = out->input_result.submit_attempted ? 1 : 0;
  out->cancel_consumed = out->input_result.cancel_consumed ? 1 : 0;
  out->wipe_required = out->panel.wipe_required ? 1 : 0;
  out->wipe_attempted = out->input_result.wipe_attempted ? 1 : 0;
  out->wipe_succeeded = out->input_result.wipe_succeeded ? 1 : 0;
  out->masked_text_available = out->panel.masked_text_available ? 1 : 0;
  out->masked_text_truncated = out->panel.masked_text_truncated ? 1 : 0;
  out->text_login_authoritative =
      out->panel.text_login_authoritative ? 1 : 0;
  out->length = out->panel.length;
  out->state = out->panel.state ? out->panel.state : "blocked";
  out->blocked_reason = out->panel.blocked_reason
                            ? out->panel.blocked_reason
                            : "panel-unavailable";
  out->submit_allowed = 0;
  out->auth_attempt_allowed = 0;
  return 0;
}


int login_window_credential_readiness_evaluate(
    const struct login_window_credential_policy *policy,
    const struct login_window_credential_buffer *buffer,
    const struct login_window_credential_panel *panel,
    const struct login_window_credential_interaction *interaction,
    struct login_window_credential_readiness *out) {
  if (!out) return -1;
  out->version = LOGIN_WINDOW_CREDENTIAL_READINESS_VERSION;
  out->policy_available = 0;
  out->field_allowed = 0;
  out->buffer_available = 0;
  out->buffer_initialized = 0;
  out->panel_available = 0;
  out->interaction_available = 0;
  out->ready_for_render = 0;
  out->ready_for_input = 0;
  out->ready_for_masked_text = 0;
  out->has_secret = 0;
  out->input_blocked = 0;
  out->submit_blocked = 1;
  out->cancel_consumed = 0;
  out->wipe_required = 1;
  out->wipe_attempted = 0;
  out->wipe_succeeded = 0;
  out->overflow_blocked = 0;
  out->masked_text_required = 1;
  out->masked_text_available = 0;
  out->masked_text_truncated = 0;
  out->submit_allowed = 0;
  out->auth_attempt_allowed = 0;
  out->text_login_authoritative = 1;
  out->length = 0;
  out->max_chars = 0;
  out->state = "blocked";
  out->blocked_reason = "policy-unavailable";
  out->panel_available = panel ? 1 : 0;
  out->interaction_available = interaction ? 1 : 0;
  if (!policy) return 0;
  out->policy_available = 1;
  out->field_allowed = policy->password_field_allowed ? 1 : 0;
  out->masked_text_required = policy->password_mask_required ? 1 : 0;
  out->wipe_required = policy->password_wipe_required ? 1 : 0;
  out->text_login_authoritative = policy->text_login_authoritative ? 1 : 0;
  out->max_chars = policy->max_password_chars;
  if (!out->text_login_authoritative) {
    out->blocked_reason = "text-login-not-authoritative";
    return 0;
  }
  if (!policy->password_field_allowed) {
    out->state = "disabled";
    out->blocked_reason = "password-field-disabled";
    return 0;
  }
  if (!policy->password_mask_required) {
    out->blocked_reason = "password-mask-required";
    return 0;
  }
  if (!policy->password_wipe_required) {
    out->blocked_reason = "credential-wipe-required";
    return 0;
  }
  if (!buffer) {
    out->blocked_reason = "buffer-unavailable";
    return 0;
  }
  out->buffer_available = 1;
  out->buffer_initialized = buffer->initialized ? 1 : 0;
  out->has_secret = buffer->length > 0 ? 1 : 0;
  out->length = buffer->length;
  out->max_chars = buffer->max_chars;
  out->wipe_required = buffer->wipe_required ? 1 : 0;
  out->overflow_blocked = buffer->overflow_blocked ? 1 : 0;
  if (!buffer->initialized || !buffer->storage) {
    out->blocked_reason = "buffer-unavailable";
    return 0;
  }
  if (!buffer->masked) {
    out->blocked_reason = "buffer-unmasked";
    return 0;
  }
  if (!buffer->wipe_required) {
    out->blocked_reason = "buffer-wipe-not-required";
    return 0;
  }
  if (buffer->overflow_blocked) {
    out->blocked_reason = "credential-overflow-blocked";
    return 0;
  }
  if (!panel) {
    out->blocked_reason = "panel-unavailable";
    return 0;
  }
  out->panel_available = 1;
  out->input_blocked = panel->input_blocked ? 1 : 0;
  out->cancel_consumed = panel->cancel_consumed ? 1 : 0;
  out->wipe_attempted = panel->wipe_attempted ? 1 : 0;
  out->wipe_succeeded = panel->wipe_succeeded ? 1 : 0;
  out->masked_text_available = panel->masked_text_available ? 1 : 0;
  out->masked_text_truncated = panel->masked_text_truncated ? 1 : 0;
  out->masked_text_required = panel->masked_text_required ? 1 : 0;
  out->has_secret = panel->has_secret ? 1 : 0;
  out->length = panel->length;
  out->max_chars = panel->max_chars;
  out->state = panel->state ? panel->state : "blocked";
  out->blocked_reason = panel->blocked_reason ? panel->blocked_reason
                                               : "panel-unavailable";
  if (interaction) {
    out->interaction_available = 1;
    out->input_blocked = interaction->input_blocked ? 1 : out->input_blocked;
    out->cancel_consumed = interaction->cancel_consumed ? 1 : out->cancel_consumed;
    out->wipe_attempted = interaction->wipe_attempted ? 1 : out->wipe_attempted;
    out->wipe_succeeded = interaction->wipe_succeeded ? 1 : out->wipe_succeeded;
    out->masked_text_available = interaction->masked_text_available ? 1 :
                                 out->masked_text_available;
    out->masked_text_truncated = interaction->masked_text_truncated ? 1 :
                                 out->masked_text_truncated;
    out->length = interaction->length;
    out->state = interaction->state ? interaction->state : out->state;
    out->blocked_reason = interaction->blocked_reason
                              ? interaction->blocked_reason
                              : out->blocked_reason;
  }
  out->ready_for_masked_text =
      out->masked_text_available && !out->masked_text_truncated ? 1 : 0;
  if (!out->ready_for_masked_text) {
    out->state = "blocked";
    if (out->masked_text_truncated) {
      out->blocked_reason = "masked-text-truncated";
    }
    return 0;
  }
  out->ready_for_render = panel->panel_renderable ? 1 : 0;
  if (!out->ready_for_render) {
    out->state = "blocked";
    return 0;
  }
  if (interaction && interaction->submit_attempted) {
    out->ready_for_input = 0;
    out->state = "submit-blocked";
    out->blocked_reason = interaction->blocked_reason
                              ? interaction->blocked_reason
                              : "gui-submit-disabled";
    out->submit_allowed = 0;
    out->auth_attempt_allowed = 0;
    return 0;
  }
  if (out->input_blocked) {
    out->ready_for_input = 0;
    out->state = "input-blocked";
    return 0;
  }
  out->ready_for_input = 1;
  if (out->cancel_consumed) {
    out->state = "cancelled";
    out->blocked_reason = "cancelled";
  } else {
    out->state = interaction && interaction->state ? interaction->state : "ready";
    out->blocked_reason = "gui-submit-disabled";
  }
  out->submit_allowed = 0;
  out->auth_attempt_allowed = 0;
  return 0;
}


int login_window_credential_audit_event_build(
    const struct login_window_credential_readiness *readiness,
    const struct login_window_credential_interaction *interaction,
    struct login_window_credential_audit_event *out) {
  if (!out) return -1;
  out->version = LOGIN_WINDOW_CREDENTIAL_AUDIT_EVENT_VERSION;
  out->action = 0;
  out->readiness_available = 0;
  out->interaction_available = 0;
  out->input_applied = 0;
  out->input_accepted = 0;
  out->input_blocked = 0;
  out->submit_attempted = 0;
  out->submit_blocked = 1;
  out->cancel_consumed = 0;
  out->wipe_required = 1;
  out->wipe_attempted = 0;
  out->wipe_succeeded = 0;
  out->ready_for_render = 0;
  out->ready_for_input = 0;
  out->ready_for_masked_text = 0;
  out->credential_present = 0;
  out->raw_secret_exposed = 0;
  out->masked_text_exposed = 0;
  out->secret_redacted = 1;
  out->length_redacted = 1;
  out->submit_allowed = 0;
  out->auth_attempt_allowed = 0;
  out->text_login_authoritative = 1;
  out->event_type = "credential-audit-unavailable";
  out->state = "blocked";
  out->blocked_reason = "readiness-unavailable";
  if (!readiness) return 0;
  out->readiness_available = 1;
  out->ready_for_render = readiness->ready_for_render ? 1 : 0;
  out->ready_for_input = readiness->ready_for_input ? 1 : 0;
  out->ready_for_masked_text = readiness->ready_for_masked_text ? 1 : 0;
  out->credential_present = readiness->has_secret ? 1 : 0;
  out->input_blocked = readiness->input_blocked ? 1 : 0;
  out->submit_blocked = 1;
  out->cancel_consumed = readiness->cancel_consumed ? 1 : 0;
  out->wipe_required = readiness->wipe_required ? 1 : 0;
  out->wipe_attempted = readiness->wipe_attempted ? 1 : 0;
  out->wipe_succeeded = readiness->wipe_succeeded ? 1 : 0;
  out->text_login_authoritative =
      readiness->text_login_authoritative ? 1 : 0;
  out->state = readiness->state ? readiness->state : "blocked";
  out->blocked_reason = readiness->blocked_reason
                            ? readiness->blocked_reason
                            : "blocked";
  if (interaction) {
    out->interaction_available = 1;
    out->action = interaction->action;
    out->input_applied = interaction->input_applied ? 1 : 0;
    out->input_accepted = interaction->input_accepted ? 1 : 0;
    out->input_blocked = interaction->input_blocked ? 1 : out->input_blocked;
    out->submit_attempted = interaction->submit_attempted ? 1 : 0;
    out->cancel_consumed = interaction->cancel_consumed ? 1 : out->cancel_consumed;
    out->wipe_required = interaction->wipe_required ? 1 : out->wipe_required;
    out->wipe_attempted = interaction->wipe_attempted ? 1 : out->wipe_attempted;
    out->wipe_succeeded = interaction->wipe_succeeded ? 1 : out->wipe_succeeded;
    out->state = interaction->state ? interaction->state : out->state;
    out->blocked_reason = interaction->blocked_reason
                              ? interaction->blocked_reason
                              : out->blocked_reason;
  }
  if (out->submit_attempted) {
    out->event_type = "credential-submit-blocked";
  } else if (out->cancel_consumed) {
    out->event_type = "credential-cancelled";
  } else if (out->input_blocked) {
    out->event_type = "credential-input-blocked";
  } else if (out->interaction_available && out->input_accepted) {
    out->event_type = "credential-input-accepted";
  } else if (out->ready_for_render && out->ready_for_input &&
             out->ready_for_masked_text) {
    out->event_type = "credential-ready";
  } else {
    out->event_type = "credential-blocked";
  }
  out->raw_secret_exposed = 0;
  out->masked_text_exposed = 0;
  out->secret_redacted = 1;
  out->length_redacted = 1;
  out->submit_allowed = 0;
  out->auth_attempt_allowed = 0;
  out->submit_blocked = 1;
  return 0;
}
