#include <stdio.h>
#include <string.h>

#include "core/login_runtime.h"

static struct shell_context g_shell_ctx;
static struct session_context g_session_ctx;
static struct system_settings g_settings;
static int g_prepare_calls;
static int g_maintenance_calls;
static int g_login_calls;
static int g_init_user_calls;
static int g_dispatch_calls;
static int g_banner_calls;
static int g_clear_calls;
static int g_splash_calls;
static int g_should_logout;
static char g_printed[2048];
static size_t g_printed_len;

static void reset_test_state(void) {
  memset(&g_shell_ctx, 0, sizeof(g_shell_ctx));
  memset(&g_session_ctx, 0, sizeof(g_session_ctx));
  memset(&g_settings, 0, sizeof(g_settings));
  memset(g_printed, 0, sizeof(g_printed));
  g_prepare_calls = 0;
  g_maintenance_calls = 0;
  g_login_calls = 0;
  g_init_user_calls = 0;
  g_dispatch_calls = 0;
  g_banner_calls = 0;
  g_clear_calls = 0;
  g_splash_calls = 0;
  g_should_logout = 0;
  g_printed_len = 0;
  memcpy(g_settings.language, "en", 3);
  memcpy(g_settings.hostname, "capyos", 7);
}

static void append_printed(const char *text) {
  size_t idx = 0;

  if (!text) {
    return;
  }
  while (text[idx] && g_printed_len + 1 < sizeof(g_printed)) {
    g_printed[g_printed_len++] = text[idx++];
  }
  g_printed[g_printed_len] = '\0';
}

static int strings_equal(const char *a, const char *b) {
  if (!a || !b) {
    return 0;
  }
  while (*a && *b) {
    if (*a++ != *b++) {
      return 0;
    }
  }
  return *a == *b;
}

static void copy_text(char *dst, size_t dst_size, const char *src) {
  size_t idx = 0;
  if (!dst || dst_size == 0) {
    return;
  }
  if (src) {
    while (src[idx] && idx + 1 < dst_size) {
      dst[idx] = src[idx];
      ++idx;
    }
  }
  dst[idx] = '\0';
}

void shell_build_prompt(const struct user_record *user,
                        const struct system_settings *settings,
                        const char *cwd, char *out, size_t out_len) {
  const char *username = (user && user->username[0]) ? user->username : "user";
  const char *hostname =
      (settings && settings->hostname[0]) ? settings->hostname : "capyos";
  const char *path = (cwd && cwd[0]) ? cwd : "/";
  char prompt[128];

  (void)out_len;
  snprintf(prompt, sizeof(prompt), "%s@%s>%s> ", username, hostname, path);
  copy_text(out, out_len, prompt);
}

static int prepare_shell_runtime_stub(void) {
  ++g_prepare_calls;
  return 0;
}

static void session_reset_stub(struct session_context *ctx) {
  if (!ctx) {
    return;
  }
  memset(ctx, 0, sizeof(*ctx));
  copy_text(ctx->cwd, sizeof(ctx->cwd), "/");
  copy_text(ctx->prefs.language, sizeof(ctx->prefs.language), "en");
}

static void session_set_active_stub(struct session_context *ctx) {
  (void)ctx;
}

static void shell_context_init_stub(struct shell_context *ctx,
                                    struct session_context *session,
                                    const struct system_settings *settings) {
  if (!ctx) {
    return;
  }
  ctx->session = session;
  ctx->settings = settings;
  ctx->running = 1;
  ctx->logout = 0;
  g_should_logout = 0;
}

static int maintenance_session_start_stub(struct session_context *session,
                                          const struct system_settings *settings) {
  (void)settings;
  ++g_maintenance_calls;
  if (g_maintenance_calls > 1) {
    return -1;
  }
  session_reset_stub(session);
  copy_text(session->user.username, sizeof(session->user.username),
            "maintenance");
  copy_text(session->user.role, sizeof(session->user.role), "recovery");
  copy_text(session->user.home, sizeof(session->user.home), "/system");
  session->user.uid = 0u;
  session->user.gid = 0u;
  copy_text(session->cwd, sizeof(session->cwd), "/system");
  return 0;
}

static int system_login_stub(struct session_context *session,
                             const struct system_settings *settings) {
  (void)session;
  (void)settings;
  ++g_login_calls;
  if (g_login_calls > 1) {
    return -1;
  }
  session_reset_stub(session);
  copy_text(session->user.username, sizeof(session->user.username), "admin");
  copy_text(session->user.role, sizeof(session->user.role), "admin");
  copy_text(session->user.home, sizeof(session->user.home), "/home/admin");
  session->user.uid = 1000u;
  session->user.gid = 1000u;
  copy_text(session->cwd, sizeof(session->cwd), "/home/admin");
  return 0;
}

static int init_shell_context_user_stub(const struct user_record *user) {
  ++g_init_user_calls;
  return (user && user->username[0]) ? 0 : -1;
}

static int dispatch_shell_command_stub(char *line) {
  ++g_dispatch_calls;
  if (strings_equal(line, "logout")) {
    g_should_logout = 1;
    return 1;
  }
  return 0;
}

static int run_shell_alias_stub(const char *alias_line) {
  (void)alias_line;
  return 0;
}

static size_t readline_stub(char *buf, size_t maxlen, int mask) {
  const char *text = "logout";
  size_t idx = 0;

  (void)mask;
  if (!buf || maxlen == 0) {
    return 0;
  }
  while (text[idx] && idx + 1 < maxlen) {
    buf[idx] = text[idx];
    ++idx;
  }
  buf[idx] = '\0';
  return idx;
}

static const struct user_record *
session_user_stub(const struct session_context *ctx) {
  return ctx ? &ctx->user : NULL;
}

const char *session_language(const struct session_context *ctx) {
  if (!ctx || !ctx->prefs.language[0]) {
    return "en";
  }
  return ctx->prefs.language;
}

static const char *session_cwd_stub(const struct session_context *ctx) {
  if (!ctx || !ctx->cwd[0]) {
    return "/";
  }
  return ctx->cwd;
}

static int shell_context_should_logout_stub(const struct shell_context *ctx) {
  (void)ctx;
  return g_should_logout;
}

static void print_stub(const char *text) { append_printed(text); }

static void putc_stub(char c) {
  char text[2];
  text[0] = c;
  text[1] = '\0';
  append_printed(text);
}

static void clear_view_stub(void) { ++g_clear_calls; }

static void show_splash_stub(const struct system_settings *settings) {
  (void)settings;
  ++g_splash_calls;
}

static void ui_banner_stub(void) { ++g_banner_calls; }

static void cmd_info_stub(void) {}

static int expect_true(int cond, const char *msg) {
  if (!cond) {
    fprintf(stderr, "[login_runtime] %s\n", msg);
    return 1;
  }
  return 0;
}

static struct login_runtime_ops build_ops(void) {
  struct login_runtime_ops ops;
  memset(&ops, 0, sizeof(ops));
  ops.has_any_input = 1;
  ops.maintenance_mode = 0;
  ops.shell_ctx = &g_shell_ctx;
  ops.session_ctx = &g_session_ctx;
  ops.settings = &g_settings;
  ops.prepare_shell_runtime = prepare_shell_runtime_stub;
  ops.maintenance_session_start = maintenance_session_start_stub;
  ops.init_shell_context_user = init_shell_context_user_stub;
  ops.dispatch_shell_command = dispatch_shell_command_stub;
  ops.run_shell_alias = run_shell_alias_stub;
  ops.is_equal = strings_equal;
  ops.readline = readline_stub;
  ops.session_reset = session_reset_stub;
  ops.session_set_active = session_set_active_stub;
  ops.shell_context_init = shell_context_init_stub;
  ops.system_login = system_login_stub;
  ops.session_user = session_user_stub;
  ops.session_cwd = session_cwd_stub;
  ops.shell_context_should_logout = shell_context_should_logout_stub;
  ops.print = print_stub;
  ops.putc = putc_stub;
  ops.clear_view = clear_view_stub;
  ops.show_splash = show_splash_stub;
  ops.ui_banner = ui_banner_stub;
  ops.cmd_info = cmd_info_stub;
  ops.service_poll = NULL;
  return ops;
}

static int test_maintenance_mode_bypasses_login(void) {
  int fails = 0;
  struct login_runtime_ops ops;
  int rc = 0;

  reset_test_state();
  ops = build_ops();
  ops.maintenance_mode = 1;
  ops.maintenance_reason =
      "Boot policy forced maintenance because the storage runtime is unavailable";

  rc = login_runtime_run(&ops);
  fails += expect_true(rc == -1,
                       "maintenance mode should stop when recovery session fails");
  fails += expect_true(g_prepare_calls == 1,
                       "prepare shell runtime should run once");
  fails += expect_true(g_login_calls == 0,
                       "maintenance mode must bypass normal login");
  fails += expect_true(g_maintenance_calls == 2,
                       "maintenance session should restart after logout");
  fails += expect_true(g_init_user_calls == 1,
                       "maintenance user should initialize shell session");
  fails += expect_true(g_dispatch_calls == 1,
                       "maintenance shell should dispatch one command");
  fails += expect_true(g_banner_calls >= 2 && g_clear_calls >= 2,
                       "maintenance screen should be redrawn after logout");
  fails += expect_true(strstr(g_printed, "recovery mode") != NULL,
                       "maintenance notice should be printed");
  fails += expect_true(strstr(g_printed, "storage runtime is unavailable") != NULL,
                       "maintenance reason should be visible");
  return fails;
}

static int test_normal_login_path_still_runs(void) {
  int fails = 0;
  struct login_runtime_ops ops;
  int rc = 0;

  reset_test_state();
  ops = build_ops();

  rc = login_runtime_run(&ops);
  fails += expect_true(rc == -1,
                       "normal login path should stop when login later fails");
  fails += expect_true(g_prepare_calls == 1,
                       "prepare shell runtime should run once in normal mode");
  fails += expect_true(g_login_calls == 2,
                       "normal login should run again after logout");
  fails += expect_true(g_maintenance_calls == 0,
                       "normal login must not start maintenance session");
  fails += expect_true(strstr(g_printed, "recovery mode") == NULL,
                       "normal login should not show maintenance notice");
  return fails;
}

int run_login_runtime_tests(void) {
  int fails = 0;
  fails += test_maintenance_mode_bypasses_login();
  fails += test_normal_login_path_still_runs();
  if (fails == 0) {
    printf("[tests] login_runtime OK\n");
  }
  return fails;
}
