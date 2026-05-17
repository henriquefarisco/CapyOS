/*
 * src/auth/login_runtime/credential_buffer.c
 *
 * Credential-input buffer lifecycle and submit-gate evaluation —
 * extracted byte-for-byte from `src/auth/login_runtime.c` during
 * PR C.2 of the Estagio C dedicated plan.  Hosts seven public
 * functions:
 *
 *   - login_window_credential_buffer_wipe
 *   - login_window_credential_buffer_init
 *   - login_window_credential_buffer_append
 *   - login_window_credential_buffer_backspace
 *   - login_window_credential_buffer_masked_text
 *   - login_window_credential_submit_gate_evaluate
 *   - login_window_credential_submit_attempt_consume
 *
 * The volatile-safe wipe routine `..._buffer_wipe` is the canonical
 * scrub path used everywhere a credential buffer is reset, so it is
 * grouped with the buffer init/append/backspace primitives that
 * own it. The submit-gate functions are co-located because they
 * read directly from the buffer to enforce credential length /
 * lockout policy.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

int login_window_credential_buffer_wipe(
    struct login_window_credential_buffer *buffer) {
  size_t idx = 0;
  if (!buffer) return -1;
  if (buffer->storage && buffer->capacity > 0) {
    for (idx = 0; idx < buffer->capacity; ++idx) {
      buffer->storage[idx] = '\0';
    }
  }
  buffer->length = 0;
  buffer->overflow_blocked = 0;
  buffer->wiped = 1;
  buffer->blocked_reason = "wiped";
  return 0;
}

int login_window_credential_buffer_init(
    struct login_window_credential_buffer *buffer, char *storage,
    size_t storage_size, const struct login_window_credential_policy *policy) {
  size_t effective_max = LOGIN_WINDOW_PASSWORD_MAX_CHARS;
  size_t idx = 0;
  if (!buffer) return -1;
  buffer->version = LOGIN_WINDOW_CREDENTIAL_BUFFER_VERSION;
  buffer->storage = storage;
  buffer->capacity = storage_size;
  buffer->length = 0;
  buffer->max_chars = 0;
  buffer->mask_char = LOGIN_WINDOW_PASSWORD_MASK_CHAR;
  buffer->initialized = 0;
  buffer->masked = 1;
  buffer->wipe_required = 1;
  buffer->submit_allowed = 0;
  buffer->overflow_blocked = 0;
  buffer->wiped = 1;
  buffer->blocked_reason = "policy-unavailable";
  if (storage && storage_size > 0) {
    for (idx = 0; idx < storage_size; ++idx) {
      storage[idx] = '\0';
    }
  }
  if (!policy) return 0;
  buffer->mask_char = policy->mask_char ? policy->mask_char
                                        : LOGIN_WINDOW_PASSWORD_MASK_CHAR;
  buffer->max_chars = policy->max_password_chars;
  if (!policy->password_field_allowed) {
    buffer->blocked_reason = "password-field-disabled";
    return 0;
  }
  if (!policy->password_mask_required || !policy->password_wipe_required) {
    buffer->blocked_reason = "policy-unsafe";
    return 0;
  }
  if (!storage || storage_size < 2) {
    buffer->blocked_reason = "storage-unavailable";
    return 0;
  }
  if (effective_max > policy->max_password_chars) {
    effective_max = policy->max_password_chars;
  }
  if (effective_max > storage_size - 1) {
    effective_max = storage_size - 1;
  }
  if (effective_max == 0) {
    buffer->blocked_reason = "max-password-chars-unavailable";
    return 0;
  }
  buffer->max_chars = effective_max;
  buffer->initialized = 1;
  buffer->submit_allowed = policy->password_submit_allowed ? 1 : 0;
  buffer->wiped = 1;
  buffer->blocked_reason = "ready";
  return 0;
}

int login_window_credential_buffer_append(
    struct login_window_credential_buffer *buffer, char ch) {
  if (!buffer) return -1;
  if (!buffer->initialized || !buffer->storage) {
    buffer->blocked_reason = "buffer-unavailable";
    return 0;
  }
  if (!ch) {
    buffer->blocked_reason = "invalid-character";
    return 0;
  }
  if (buffer->length >= buffer->max_chars ||
      buffer->length + 1 >= buffer->capacity) {
    buffer->overflow_blocked = 1;
    buffer->blocked_reason = "max-password-chars";
    return 0;
  }
  buffer->storage[buffer->length++] = ch;
  buffer->storage[buffer->length] = '\0';
  buffer->wiped = 0;
  buffer->blocked_reason = "ready";
  return 1;
}

int login_window_credential_buffer_backspace(
    struct login_window_credential_buffer *buffer) {
  if (!buffer) return -1;
  if (!buffer->initialized || !buffer->storage) {
    buffer->blocked_reason = "buffer-unavailable";
    return 0;
  }
  if (buffer->length == 0) {
    buffer->blocked_reason = "empty";
    return 0;
  }
  buffer->storage[--buffer->length] = '\0';
  buffer->wiped = buffer->length == 0 ? 1 : 0;
  buffer->blocked_reason = "ready";
  return 1;
}

int login_window_credential_buffer_masked_text(
    const struct login_window_credential_buffer *buffer, char *out,
    size_t out_size) {
  size_t idx = 0;
  size_t visible = 0;
  char mask = LOGIN_WINDOW_PASSWORD_MASK_CHAR;
  if (!buffer || !out || out_size == 0) return -1;
  out[0] = '\0';
  if (!buffer->initialized) return 0;
  visible = buffer->length;
  if (visible + 1 > out_size) {
    visible = out_size - 1;
  }
  if (buffer->mask_char) {
    mask = buffer->mask_char;
  }
  for (idx = 0; idx < visible; ++idx) {
    out[idx] = mask;
  }
  out[visible] = '\0';
  return 0;
}


int login_window_credential_submit_gate_evaluate(
    const struct login_window_credential_policy *policy,
    const struct login_window_credential_buffer *buffer,
    struct login_window_credential_submit_gate *out) {
  if (!out) return -1;
  out->version = LOGIN_WINDOW_CREDENTIAL_SUBMIT_GATE_VERSION;
  out->policy_available = 0;
  out->policy_submit_allowed = 0;
  out->buffer_available = 0;
  out->buffer_initialized = 0;
  out->buffer_has_secret = 0;
  out->buffer_masked = 1;
  out->buffer_submit_allowed = 0;
  out->wipe_required = 1;
  out->submit_allowed = 0;
  out->auth_attempt_allowed = 0;
  out->text_login_authoritative = 1;
  out->blocked_reason = "policy-unavailable";
  if (!policy) return 0;
  out->policy_available = 1;
  out->policy_submit_allowed = policy->password_submit_allowed ? 1 : 0;
  out->text_login_authoritative = policy->text_login_authoritative ? 1 : 0;
  out->wipe_required = policy->password_wipe_required ? 1 : 0;
  out->buffer_masked = policy->password_mask_required ? 1 : 0;
  if (!out->text_login_authoritative) {
    out->blocked_reason = "text-login-not-authoritative";
    return 0;
  }
  if (!policy->password_field_allowed) {
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
  out->buffer_has_secret = buffer->length > 0 ? 1 : 0;
  out->buffer_masked = buffer->masked ? 1 : 0;
  out->buffer_submit_allowed = buffer->submit_allowed ? 1 : 0;
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
  if (buffer->overflow_blocked) {
    out->blocked_reason = "credential-overflow-blocked";
    return 0;
  }
  if (buffer->length == 0) {
    out->blocked_reason = "credential-empty";
    return 0;
  }
  if (buffer->wiped) {
    out->blocked_reason = "credential-wiped";
    return 0;
  }
  if (!policy->password_submit_allowed) {
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (!buffer->submit_allowed) {
    out->blocked_reason = "buffer-submit-disabled";
    return 0;
  }
  out->submit_allowed = 1;
  out->auth_attempt_allowed = 1;
  out->blocked_reason = "ready";
  return 0;
}


int login_window_credential_submit_attempt_consume(
    const struct login_window_credential_policy *policy,
    struct login_window_credential_buffer *buffer,
    struct login_window_credential_submit_attempt *out) {
  int wipe_rc = 0;
  if (!out) return -1;
  out->version = LOGIN_WINDOW_CREDENTIAL_SUBMIT_ATTEMPT_VERSION;
  out->attempted = 1;
  out->gate_evaluated = 0;
  out->buffer_had_secret = (buffer && buffer->length > 0) ? 1 : 0;
  out->wipe_required = 1;
  out->wipe_attempted = 0;
  out->wipe_succeeded = 0;
  out->submit_allowed = 0;
  out->auth_attempt_allowed = 0;
  out->text_login_authoritative = 1;
  out->blocked_reason = "gate-unavailable";
  if (login_window_credential_submit_gate_evaluate(policy, buffer,
                                                   &out->gate) != 0) {
    return -1;
  }
  out->gate_evaluated = 1;
  out->wipe_required = 1;
  out->text_login_authoritative = out->gate.text_login_authoritative ? 1 : 0;
  out->blocked_reason = out->gate.blocked_reason ? out->gate.blocked_reason
                                                 : "gui-submit-disabled";
  if (buffer) {
    out->wipe_attempted = 1;
    wipe_rc = login_window_credential_buffer_wipe(buffer);
    out->wipe_succeeded = wipe_rc == 0 ? 1 : 0;
    if (wipe_rc != 0) {
      out->blocked_reason = "credential-wipe-failed";
      return 0;
    }
  }
  return 0;
}
