/* Regression for the desktop freeze caused by `capyai` with no arguments.
 * The graphical path must launch/focus the chat and return without touching
 * tty_readline; the text-console path keeps the legacy interactive loop. */
#include "shell/commands_extended.h"
#include "shell/commands.h"
#include "shell/core.h"
#include "services/capyai.h"
#include "gui/desktop_runtime.h"
#include "drivers/console/tty.h"

#include <stdio.h>
#include <string.h>

static int failures;
static int desktop_active;
static int launch_rc;
static int launch_calls;
static int tty_calls;

#define CHECK(c, m) do { \
    if (c) printf("  ok   %s\n", m); \
    else { printf("  FAIL %s\n", m); failures++; } \
} while (0)

int desktop_is_active(void) { return desktop_active; }
int desktop_launch_capyai(void) { launch_calls++; return launch_rc; }
size_t tty_readline(char *out, size_t max_len) {
    static const char exit_line[] = "sair";
    size_t n = sizeof(exit_line) - 1u;
    tty_calls++;
    if (max_len == 0u) return 0u;
    if (n >= max_len) n = max_len - 1u;
    memcpy(out, exit_line, n);
    out[n] = '\0';
    return n;
}

int shell_help_requested(int argc, char **argv) { (void)argc; (void)argv; return 0; }
void shell_print(const char *s) { (void)s; }
void shell_print_error(const char *s) { (void)s; }
void shell_newline(void) {}
int shell_string_equal(const char *a, const char *b) {
    return a && b && strcmp(a, b) == 0;
}
int shell_parse_line(char *line, char **argv, int max_args) {
    (void)line; (void)argv; (void)max_args; return 0;
}
const struct shell_command *shell_find_command(const char *name) {
    (void)name; return NULL;
}

int capyai_plan(const char *model_text, size_t model_len, const char *intent,
                const char *platform, const char *shell,
                const struct capyai_perms *perms,
                const struct capyai_session *session,
                struct capyai_plan *plan) {
    (void)model_text; (void)model_len; (void)intent; (void)platform;
    (void)shell; (void)perms; (void)session;
    if (plan) memset(plan, 0, sizeof(*plan));
    return 0;
}
int capyai_execute_intent_v2(
    const char *model_text, size_t model_len,
    const char *intent, const char *platform, const char *shell,
    const struct capyai_perms *perms, struct capyai_session *session,
    capyai_dispatch_fn dispatch, void *dispatch_ctx,
    capyai_typed_dispatch_fn typed_dispatch, void *typed_dispatch_ctx,
    struct capyai_plan *plan, struct capyai_exec_result *result) {
    (void)model_text; (void)model_len; (void)intent; (void)platform;
    (void)shell; (void)perms; (void)session; (void)dispatch;
    (void)dispatch_ctx; (void)typed_dispatch; (void)typed_dispatch_ctx;
    (void)plan; (void)result;
    return 0;
}
int capyai_native_file_dispatch(void *ctx,
                                const struct capy_ai_output *tool_call,
                                char *detail, size_t detail_size) {
    (void)ctx; (void)tool_call; (void)detail; (void)detail_size;
    return 0;
}
int capyai_native_system_dispatch(void *ctx,
                                  const struct capy_ai_output *tool_call,
                                  char *detail, size_t detail_size) {
    (void)ctx; (void)tool_call; (void)detail; (void)detail_size;
    return 0;
}
void capyai_summary(const struct capyai_plan *plan,
                    const struct capyai_exec_result *result,
                    char *buf, size_t buf_size) {
    (void)plan; (void)result;
    if (buf && buf_size) buf[0] = '\0';
}

int main(void) {
    struct shell_command command;
    struct shell_context ctx;
    char name[] = "capyai";
    char *argv[] = {name};
    memset(&command, 0, sizeof(command));
    memset(&ctx, 0, sizeof(ctx));
    CHECK(capyai_command_register(&command) == 0 && command.handler != NULL,
          "capyai command registers");

    desktop_active = 1;
    launch_rc = 0;
    launch_calls = 0;
    tty_calls = 0;
    CHECK(command.handler(&ctx, 1, argv) == 0,
          "graphical no-arg command returns");
    CHECK(launch_calls == 1, "graphical no-arg command launches chat");
    CHECK(tty_calls == 0, "graphical no-arg command never calls tty_readline");

    launch_rc = -1;
    CHECK(command.handler(&ctx, 1, argv) != 0,
          "graphical launch allocation error is visible to caller");
    CHECK(tty_calls == 0, "graphical allocation failure still avoids readline");

    desktop_active = 0;
    launch_calls = 0;
    CHECK(command.handler(&ctx, 1, argv) == 0,
          "text console interactive mode exits normally");
    CHECK(launch_calls == 0 && tty_calls == 1,
          "text console alone uses tty_readline");

    return failures ? 1 : 0;
}
