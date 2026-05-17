#ifndef AUTH_LOGIN_RUNTIME_PIPELINE_CONTRACT_H
#define AUTH_LOGIN_RUNTIME_PIPELINE_CONTRACT_H

/*
 * include/auth/login_runtime/pipeline_contract.h
 *
 * Internal partial header of the `login_runtime` module: it carries
 * the four "top-of-pipeline" structures that surface results to
 * runtime callers and inject ops:
 *
 *   - struct login_window_credential_screen_pipeline_safety_report
 *   - struct login_window_contract
 *   - struct login_window_view_model
 *   - struct login_runtime_ops
 *
 * INCLUSION CONTRACT
 * ------------------
 * This header is standalone-includable since PR B+C+D #2 of the
 * dedicated plan introduced `auth/login_runtime/auth_core.h` and
 * wired it in below. The auth_core partial brings in the
 * `struct login_window_credential_policy` definition that
 * `struct login_window_view_model` carries by value.
 *
 * The function pointer field types inside `struct login_runtime_ops`
 * (struct shell_context, struct session_context, struct
 * system_settings) still come from the headers the facade
 * `auth/login_runtime.h` already pulls in (`shell/core.h`,
 * `auth/session.h`, `core/system_init.h`). Direct inclusion of this
 * partial header therefore requires those upstream headers to be
 * visible too; they will move to dedicated partials in later PRs of
 * Estagio B+C+D.
 *
 * PR B+C+D #1 of the dedicated plan extracted these four definitions
 * unchanged byte-for-byte from `auth/login_runtime.h`. PR B+C+D #2
 * resolved the standalone-include limitation by routing through
 * `auth_core.h`. No behavioural change is intended; only the
 * lexical home of the declarations moves.
 */

#include <stddef.h>

#include "auth/login_runtime/auth_core.h"

struct login_window_credential_screen_pipeline_safety_report {
  int version;
  int window_input_plan_available;
  int window_surface_plan_safe;
  int window_compositor_plan_safe;
  int window_damage_plan_safe;
  int window_present_plan_safe;
  int window_schedule_plan_safe;
  int window_vsync_plan_safe;
  int window_scanout_plan_safe;
  int window_display_plan_safe;
  int window_output_plan_safe;
  int window_blit_plan_safe;
  int window_commit_plan_safe;
  int window_flip_plan_safe;
  int window_vblank_plan_safe;
  int window_event_plan_safe;
  int window_input_plan_safe;
  int total_layers;
  int layers_safe;
  int layers_unsafe;
  int gui_submit_blocked;
  int auth_attempt_blocked;
  int credentials_redacted;
  int credentials_storage_wiped;
  int text_login_authoritative;
  int route_consistent;
  int no_real_event_dispatched;
  int no_real_input_dispatched;
  int pipeline_safe;
  const char *deepest_safe_layer;
  const char *first_unsafe_layer;
  const char *event_type;
  const char *state;
  const char *blocked_reason;
  const char *message;
};

struct login_window_contract {
  int ready;
  int has_input;
  int maintenance_mode;
  int session_available;
  int settings_available;
  int recovery_available;
  int shell_callbacks_ready;
  int auth_callbacks_ready;
  int ui_callbacks_ready;
  const char *blocked_reason;
};

struct login_window_view_model {
  int renderable;
  int password_enabled;
  int recovery_enabled;
  int fallback_required;
  int maintenance_notice;
  int password_submit_enabled;
  int password_masked;
  int credential_wipe_required;
  int text_login_authoritative;
  struct login_window_credential_policy credential_policy;
  const char *state;
  const char *title;
  const char *message;
  const char *blocked_reason;
};

struct login_runtime_ops {
  int has_any_input;
  int maintenance_mode;
  struct shell_context *shell_ctx;
  struct session_context *session_ctx;
  struct system_settings *settings;
  const char *maintenance_reason;

  int (*prepare_shell_runtime)(void);
  int (*maintenance_session_start)(struct session_context *session,
                                   const struct system_settings *settings);
  int (*init_shell_context_user)(const struct user_record *user);
  int (*dispatch_shell_command)(char *line);
  int (*run_shell_alias)(const char *alias_line);
  int (*is_equal)(const char *a, const char *b);
  size_t (*readline)(char *buf, size_t maxlen, int mask);

  void (*session_reset)(struct session_context *ctx);
  void (*session_set_active)(struct session_context *ctx);
  void (*shell_context_init)(struct shell_context *ctx,
                             struct session_context *session,
                             const struct system_settings *settings);
  int (*system_login)(struct session_context *session,
                      const struct system_settings *settings);
  const struct user_record *(*session_user)(const struct session_context *ctx);
  const char *(*session_cwd)(const struct session_context *ctx);
  int (*shell_context_should_logout)(const struct shell_context *ctx);

  void (*print)(const char *text);
  void (*putc)(char c);
  void (*clear_view)(void);
  void (*show_splash)(const struct system_settings *settings);
  void (*ui_banner)(void);
  void (*render_window_layout)(void);
  void (*cmd_info)(void);
  void (*service_poll)(void);
  int (*maintenance_mode_active)(void);
  int (*consume_recovery_login_request)(void);
};

#endif /* AUTH_LOGIN_RUNTIME_PIPELINE_CONTRACT_H */
