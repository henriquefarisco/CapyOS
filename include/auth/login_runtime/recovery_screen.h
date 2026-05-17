#ifndef AUTH_LOGIN_RUNTIME_RECOVERY_SCREEN_H
#define AUTH_LOGIN_RUNTIME_RECOVERY_SCREEN_H

/*
 * include/auth/login_runtime/recovery_screen.h
 *
 * Internal partial header of the `login_runtime` module: it carries
 * the four recovery/screen view-model structs that combine the
 * audit_view layer with the resume-policy state into a single
 * renderable plan before the action_route pipeline kicks in.
 *
 *   - struct login_window_credential_recovery_view_model
 *   - struct login_window_credential_screen_view_model
 *   - struct login_window_credential_screen_session
 *   - struct login_window_credential_screen_render_plan
 *
 * INCLUSION CONTRACT
 * ------------------
 * Fully standalone. All four structs hold only primitives and
 * `const char *` strings, so just `<stddef.h>` is needed (no other
 * partial header).
 *
 * PR B+C+D #4 of the dedicated plan extracts these definitions
 * unchanged byte-for-byte from `auth/login_runtime.h`.
 */

#include <stddef.h>

struct login_window_credential_recovery_view_model {
  int version;
  int contract_available;
  int policy_available;
  int credential_session_available;
  int resume_policy_available;
  int recovery_visible;
  int recovery_enabled;
  int recovery_text_session_required;
  int resume_visible;
  int resume_enabled;
  int session_reset_required;
  int login_screen_rerender_required;
  int fallback_required;
  int credential_session_safe;
  int credential_storage_wiped;
  int credential_redacted;
  int length_redacted;
  int raw_secret_exposed;
  int masked_text_exposed;
  int submit_blocked;
  int submit_enabled;
  int auth_attempt_allowed;
  int text_login_authoritative;
  const char *state;
  const char *message;
  const char *blocked_reason;
};

struct login_window_credential_screen_view_model {
  int version;
  int contract_available;
  int login_view_available;
  int credential_session_available;
  int recovery_view_available;
  int renderable;
  int password_panel_visible;
  int password_input_enabled;
  int recovery_visible;
  int recovery_enabled;
  int resume_visible;
  int resume_enabled;
  int recovery_text_session_required;
  int session_reset_required;
  int login_screen_rerender_required;
  int fallback_required;
  int maintenance_notice;
  int credential_session_safe;
  int credential_storage_wiped;
  int credential_redacted;
  int length_redacted;
  int raw_secret_exposed;
  int masked_text_exposed;
  int submit_visible;
  int submit_blocked;
  int submit_enabled;
  int auth_attempt_allowed;
  int text_login_authoritative;
  const char *title;
  const char *state;
  const char *message;
  const char *blocked_reason;
};

struct login_window_credential_screen_session {
  int version;
  int ops_available;
  int contract_built;
  int login_view_built;
  int policy_built;
  int credential_session_built;
  int resume_policy_built;
  int recovery_view_built;
  int screen_built;
  int renderable;
  int password_panel_visible;
  int password_input_enabled;
  int recovery_visible;
  int recovery_enabled;
  int resume_visible;
  int resume_enabled;
  int recovery_text_session_required;
  int session_reset_required;
  int login_screen_rerender_required;
  int fallback_required;
  int maintenance_notice;
  int credential_session_safe;
  int credential_storage_wiped;
  int credential_redacted;
  int length_redacted;
  int raw_secret_exposed;
  int masked_text_exposed;
  int storage_cleared;
  int scratch_cleared;
  int submit_visible;
  int submit_blocked;
  int submit_enabled;
  int auth_attempt_allowed;
  int text_login_authoritative;
  const char *title;
  const char *state;
  const char *message;
  const char *blocked_reason;
};

struct login_window_credential_screen_render_plan {
  int version;
  int session_available;
  int screen_built;
  int screen_session_safe;
  int layout_visible;
  int header_visible;
  int status_visible;
  int error_visible;
  int renderable;
  int password_panel_visible;
  int password_input_visible;
  int password_input_enabled;
  int password_input_focus;
  int recovery_panel_visible;
  int recovery_button_visible;
  int recovery_button_enabled;
  int resume_button_visible;
  int resume_button_enabled;
  int maintenance_notice_visible;
  int fallback_notice_visible;
  int text_login_notice_visible;
  int recovery_text_session_required;
  int session_reset_required;
  int login_screen_rerender_required;
  int credential_session_safe;
  int credential_storage_wiped;
  int credential_redacted;
  int length_redacted;
  int raw_secret_exposed;
  int masked_text_exposed;
  int storage_cleared;
  int scratch_cleared;
  int submit_button_visible;
  int submit_button_enabled;
  int submit_blocked;
  int auth_attempt_allowed;
  int text_login_authoritative;
  const char *primary_action;
  const char *title;
  const char *state;
  const char *message;
  const char *blocked_reason;
};

#endif /* AUTH_LOGIN_RUNTIME_RECOVERY_SCREEN_H */
