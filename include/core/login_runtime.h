#ifndef CORE_LOGIN_RUNTIME_H
#define CORE_LOGIN_RUNTIME_H

#include <stddef.h>

#include "core/session.h"
#include "core/system_init.h"
#include "core/user.h"
#include "shell/core.h"

struct login_runtime_ops {
  int has_any_input;
  struct shell_context *shell_ctx;
  struct session_context *session_ctx;
  struct system_settings *settings;

  int (*prepare_shell_runtime)(void);
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
  void (*ui_banner)(void);
  void (*cmd_info)(void);
};

int login_runtime_run(struct login_runtime_ops *ops);

#endif /* CORE_LOGIN_RUNTIME_H */
