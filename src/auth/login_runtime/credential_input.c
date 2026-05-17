/*
 * src/auth/login_runtime/credential_input.c
 *
 * Credential input event handling and per-field rendering —
 * extracted byte-for-byte from `src/auth/login_runtime.c` during
 * PR C.3 of the Estagio C dedicated plan.  Hosts the four
 * functions that connect raw keyboard input to the credential
 * buffer + field-view layer:
 *
 *   - login_window_credential_input_result_init   (static helper)
 *   - login_window_credential_input_apply
 *   - login_window_credential_field_view_build
 *   - login_window_credential_panel_build
 *
 * `_input_result_init` keeps its `static` storage class because
 * it is only called from `_input_apply` within the same module.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_input_result_init(
    struct login_window_credential_input_result *out, int action,
    const struct login_window_credential_policy *policy,
    const struct login_window_credential_buffer *buffer) {
  out->version = LOGIN_WINDOW_CREDENTIAL_INPUT_RESULT_VERSION;
  out->action = action;
  out->accepted = 0;
  out->buffer_changed = 0;
  out->submit_attempted = 0;
  out->cancel_consumed = 0;
  out->wipe_attempted = 0;
  out->wipe_succeeded = 0;
  out->submit_allowed = 0;
  out->auth_attempt_allowed = 0;
  out->text_login_authoritative =
      policy && policy->text_login_authoritative ? 1 : 0;
  out->masked_text_required = 1;
  out->buffer_length = buffer ? buffer->length : 0;
  out->submit_attempt.version = LOGIN_WINDOW_CREDENTIAL_SUBMIT_ATTEMPT_VERSION;
  out->submit_attempt.attempted = 0;
  out->submit_attempt.gate_evaluated = 0;
  out->submit_attempt.buffer_had_secret = 0;
  out->submit_attempt.wipe_required = 1;
  out->submit_attempt.wipe_attempted = 0;
  out->submit_attempt.wipe_succeeded = 0;
  out->submit_attempt.submit_allowed = 0;
  out->submit_attempt.auth_attempt_allowed = 0;
  out->submit_attempt.text_login_authoritative =
      policy && policy->text_login_authoritative ? 1 : 0;
  out->submit_attempt.gate.version = LOGIN_WINDOW_CREDENTIAL_SUBMIT_GATE_VERSION;
  out->submit_attempt.gate.policy_available = policy ? 1 : 0;
  out->submit_attempt.gate.policy_submit_allowed =
      policy && policy->password_submit_allowed ? 1 : 0;
  out->submit_attempt.gate.buffer_available = buffer ? 1 : 0;
  out->submit_attempt.gate.buffer_initialized =
      buffer && buffer->initialized ? 1 : 0;
  out->submit_attempt.gate.buffer_has_secret =
      buffer && buffer->length > 0 ? 1 : 0;
  out->submit_attempt.gate.buffer_masked =
      buffer ? (buffer->masked ? 1 : 0) : 1;
  out->submit_attempt.gate.buffer_submit_allowed =
      buffer && buffer->submit_allowed ? 1 : 0;
  out->submit_attempt.gate.wipe_required = 1;
  out->submit_attempt.gate.submit_allowed = 0;
  out->submit_attempt.gate.auth_attempt_allowed = 0;
  out->submit_attempt.gate.text_login_authoritative =
      policy && policy->text_login_authoritative ? 1 : 0;
  out->submit_attempt.gate.blocked_reason = "not-attempted";
  out->submit_attempt.blocked_reason = "not-attempted";
  out->blocked_reason = "input-unhandled";
}

int login_window_credential_input_apply(
    const struct login_window_credential_policy *policy,
    struct login_window_credential_buffer *buffer, int action, char ch,
    struct login_window_credential_input_result *out) {
  int rc = 0;
  if (!out) return -1;
  login_window_credential_input_result_init(out, action, policy, buffer);
  if (action == LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL) {
    out->cancel_consumed = 1;
    out->accepted = 1;
    out->blocked_reason = "cancelled";
    if (buffer) {
      out->wipe_attempted = 1;
      rc = login_window_credential_buffer_wipe(buffer);
      out->wipe_succeeded = rc == 0 ? 1 : 0;
      out->buffer_changed = rc == 0 ? 1 : 0;
      if (rc != 0) {
        out->blocked_reason = "credential-wipe-failed";
        return 0;
      }
    }
    out->buffer_length = buffer ? buffer->length : 0;
    return 0;
  }
  if (action == LOGIN_WINDOW_CREDENTIAL_INPUT_SUBMIT) {
    out->submit_attempted = 1;
    if (login_window_credential_submit_attempt_consume(policy, buffer,
                                                       &out->submit_attempt) != 0) {
      out->blocked_reason = "submit-attempt-failed";
      return -1;
    }
    out->wipe_attempted = out->submit_attempt.wipe_attempted;
    out->wipe_succeeded = out->submit_attempt.wipe_succeeded;
    out->submit_allowed = 0;
    out->auth_attempt_allowed = 0;
    out->text_login_authoritative =
        out->submit_attempt.text_login_authoritative ? 1 : 0;
    out->buffer_changed = out->wipe_succeeded;
    out->buffer_length = buffer ? buffer->length : 0;
    out->blocked_reason = out->submit_attempt.blocked_reason
                              ? out->submit_attempt.blocked_reason
                              : "gui-submit-disabled";
    return 0;
  }
  if (!policy) {
    out->blocked_reason = "policy-unavailable";
    return 0;
  }
  if (!policy->password_field_allowed) {
    out->blocked_reason = "password-field-disabled";
    return 0;
  }
  if (!policy->password_mask_required || !policy->password_wipe_required) {
    out->blocked_reason = "policy-unsafe";
    return 0;
  }
  if (!buffer || !buffer->initialized || !buffer->storage) {
    out->blocked_reason = "buffer-unavailable";
    return 0;
  }
  if (action == LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND) {
    rc = login_window_credential_buffer_append(buffer, ch);
    out->accepted = rc == 1 ? 1 : 0;
    out->buffer_changed = rc == 1 ? 1 : 0;
    out->buffer_length = buffer->length;
    out->blocked_reason = buffer->blocked_reason;
    return 0;
  }
  if (action == LOGIN_WINDOW_CREDENTIAL_INPUT_BACKSPACE) {
    rc = login_window_credential_buffer_backspace(buffer);
    out->accepted = rc == 1 ? 1 : 0;
    out->buffer_changed = rc == 1 ? 1 : 0;
    out->buffer_length = buffer->length;
    out->blocked_reason = buffer->blocked_reason;
    return 0;
  }
  out->blocked_reason = "input-action-unknown";
  out->buffer_length = buffer->length;
  return 0;
}


int login_window_credential_field_view_build(
    const struct login_window_credential_policy *policy,
    const struct login_window_credential_buffer *buffer, char *masked_out,
    size_t masked_out_size, struct login_window_credential_field_view *out) {
  if (masked_out && masked_out_size > 0) {
    masked_out[0] = '\0';
  }
  if (!out) return -1;
  out->version = LOGIN_WINDOW_CREDENTIAL_FIELD_VIEW_VERSION;
  out->policy_available = 0;
  out->field_allowed = 0;
  out->buffer_available = 0;
  out->buffer_initialized = 0;
  out->has_secret = 0;
  out->masked_text_available = 0;
  out->masked_text_truncated = 0;
  out->masked_text_required = 1;
  out->wipe_required = 1;
  out->submit_allowed = 0;
  out->auth_attempt_allowed = 0;
  out->text_login_authoritative = 1;
  out->length = 0;
  out->max_chars = 0;
  out->state = "blocked";
  out->blocked_reason = "policy-unavailable";
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
  if (!masked_out || masked_out_size == 0) {
    out->blocked_reason = "masked-output-unavailable";
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
  if (login_window_credential_buffer_masked_text(buffer, masked_out,
                                                 masked_out_size) != 0) {
    out->blocked_reason = "masked-text-failed";
    return 0;
  }
  out->masked_text_available = 1;
  if (buffer->length + 1 > masked_out_size) {
    out->masked_text_truncated = 1;
    out->blocked_reason = "masked-text-truncated";
    return 0;
  }
  if (buffer->overflow_blocked) {
    out->blocked_reason = "credential-overflow-blocked";
    return 0;
  }
  out->state = buffer->length > 0 ? "filled" : "empty";
  out->blocked_reason = "ready";
  return 0;
}


int login_window_credential_panel_build(
    const struct login_window_credential_policy *policy,
    const struct login_window_credential_buffer *buffer,
    const struct login_window_credential_input_result *last_input,
    char *masked_out, size_t masked_out_size,
    struct login_window_credential_panel *out) {
  if (masked_out && masked_out_size > 0) {
    masked_out[0] = '\0';
  }
  if (!out) return -1;
  out->version = LOGIN_WINDOW_CREDENTIAL_PANEL_VERSION;
  out->panel_renderable = 0;
  out->field_renderable = 0;
  out->input_result_available = 0;
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
  out->masked_text_required = 1;
  out->has_secret = 0;
  out->submit_allowed = 0;
  out->auth_attempt_allowed = 0;
  out->text_login_authoritative = 1;
  out->length = 0;
  out->max_chars = 0;
  out->state = "blocked";
  out->blocked_reason = "field-view-unavailable";
  if (login_window_credential_field_view_build(policy, buffer, masked_out,
                                               masked_out_size,
                                               &out->field_view) != 0) {
    out->blocked_reason = "field-view-failed";
    return -1;
  }
  out->field_renderable =
      out->field_view.masked_text_available &&
              !out->field_view.masked_text_truncated
          ? 1
          : 0;
  out->panel_renderable =
      out->field_renderable && out->field_view.field_allowed &&
              out->field_view.text_login_authoritative &&
              out->field_view.wipe_required
          ? 1
          : 0;
  out->wipe_required = out->field_view.wipe_required ? 1 : 0;
  out->masked_text_available = out->field_view.masked_text_available ? 1 : 0;
  out->masked_text_truncated = out->field_view.masked_text_truncated ? 1 : 0;
  out->masked_text_required = out->field_view.masked_text_required ? 1 : 0;
  out->has_secret = out->field_view.has_secret ? 1 : 0;
  out->text_login_authoritative =
      out->field_view.text_login_authoritative ? 1 : 0;
  out->length = out->field_view.length;
  out->max_chars = out->field_view.max_chars;
  out->state = out->field_view.state ? out->field_view.state : "blocked";
  out->blocked_reason = out->field_view.blocked_reason
                            ? out->field_view.blocked_reason
                            : "field-view-unavailable";
  if (last_input) {
    out->input_result_available = 1;
    out->input_accepted = last_input->accepted ? 1 : 0;
    out->input_blocked = last_input->accepted ? 0 : 1;
    out->buffer_changed = last_input->buffer_changed ? 1 : 0;
    out->submit_attempted = last_input->submit_attempted ? 1 : 0;
    out->cancel_consumed = last_input->cancel_consumed ? 1 : 0;
    out->wipe_attempted = last_input->wipe_attempted ? 1 : 0;
    out->wipe_succeeded = last_input->wipe_succeeded ? 1 : 0;
    if (last_input->submit_attempted) {
      out->state = "submit-blocked";
      out->blocked_reason = last_input->blocked_reason
                                ? last_input->blocked_reason
                                : "gui-submit-disabled";
    } else if (last_input->cancel_consumed) {
      out->state = "cancelled";
      out->blocked_reason = last_input->blocked_reason
                                ? last_input->blocked_reason
                                : "cancelled";
    } else if (!last_input->accepted && last_input->blocked_reason) {
      out->state = "input-blocked";
      out->blocked_reason = last_input->blocked_reason;
    } else if (last_input->accepted && last_input->buffer_changed) {
      out->state = "editing";
      out->blocked_reason = last_input->blocked_reason
                                ? last_input->blocked_reason
                                : "ready";
    }
  }
  out->submit_allowed = 0;
  out->auth_attempt_allowed = 0;
  return 0;
}
