#include "internal/shell_main_internal.h"

static struct shell_command_set g_command_sets[12];
static size_t g_command_set_count = 0;
static int g_command_sets_initialized = 0;
shell_output_write_fn g_shell_output_write = NULL;
shell_output_putc_fn g_shell_output_putc = NULL;
shell_output_clear_fn g_shell_output_clear = NULL;
void shell_hotkey_help_docs(void)
{
    tty_inject_line("help-docs", 1);
}

static void shell_init_command_sets(void)
{
    if (g_command_sets_initialized) {
        return;
    }

    size_t idx = 0;

#define ADD_COMMAND_SET(fetcher)                                                         \
    do {                                                                                 \
        size_t count_ = 0;                                                               \
        const struct shell_command *cmds_ = fetcher(&count_);                            \
        if (cmds_ && count_ && idx < sizeof(g_command_sets) / sizeof(g_command_sets[0])) \
        {                                                                                \
            g_command_sets[idx].commands = cmds_;                                       \
            g_command_sets[idx].count = count_;                                         \
            ++idx;                                                                      \
        }                                                                                \
    } while (0)

    ADD_COMMAND_SET(shell_commands_filesystem_navigation);
    ADD_COMMAND_SET(shell_commands_filesystem_content);
    ADD_COMMAND_SET(shell_commands_filesystem_manage);
    ADD_COMMAND_SET(shell_commands_filesystem_search);
    ADD_COMMAND_SET(shell_commands_help);
    ADD_COMMAND_SET(shell_commands_session);
    ADD_COMMAND_SET(shell_commands_system_info);
    ADD_COMMAND_SET(shell_commands_system_control);
    ADD_COMMAND_SET(shell_commands_network);
    ADD_COMMAND_SET(shell_commands_user_manage);
    ADD_COMMAND_SET(shell_commands_extended);

#undef ADD_COMMAND_SET

    g_command_set_count = idx;
    g_command_sets_initialized = 1;
}

const struct shell_command_set *shell_command_sets(size_t *count)
{
    shell_init_command_sets();
    if (count) {
        *count = g_command_set_count;
    }
    return g_command_sets;
}

void shell_context_init(struct shell_context *ctx,
                        struct session_context *session,
                        const struct system_settings *settings)
{
    if (!ctx) {
        return;
    }
    ctx->session = session;
    ctx->settings = settings;
    ctx->running = 1;
    ctx->logout = 0;
}

struct session_context *shell_context_session(struct shell_context *ctx)
{
    return ctx ? ctx->session : NULL;
}

const struct system_settings *shell_context_settings(const struct shell_context *ctx)
{
    return ctx ? ctx->settings : NULL;
}

int shell_context_running(const struct shell_context *ctx)
{
    return ctx ? ctx->running : 0;
}

int shell_context_should_logout(const struct shell_context *ctx)
{
    return ctx ? ctx->logout : 0;
}

void shell_request_exit(struct shell_context *ctx)
{
    if (ctx) {
        ctx->running = 0;
    }
}

void shell_request_logout(struct shell_context *ctx)
{
    if (ctx) {
        ctx->logout = 1;
        ctx->running = 0;
    }
}

const char *shell_current_language(void)
{
    struct session_context *active = session_active();
    if (active) {
        return session_language(active);
    }
    return "pt-BR";
}

