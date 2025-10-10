#include "shell/commands.h"
#include "shell/core.h"

#include "drivers/video/vga.h"

static int cmd_mess(struct shell_context *ctx, int argc, char **argv) {
    (void)ctx;
    if (shell_help_requested(argc, argv)) {
        shell_print("Uso: mess\nLimpa a tela do terminal.\n");
        return 0;
    }
    vga_clear();
    return 0;
}

static int cmd_bye(struct shell_context *ctx, int argc, char **argv) {
    if (shell_handle_help(argc, argv, "bye",
                          "Encerra a sessao atual e retorna a tela de login.")) {
        return 0;
    }
    (void)argc;
    (void)argv;
    shell_request_logout(ctx);
    shell_print("Encerrando sessao...\n");
    return 0;
}

static const struct shell_command g_session_commands[] = {
    { "mess", cmd_mess },
    { "bye", cmd_bye },
};

const struct shell_command *shell_commands_session(size_t *count) {
    if (count) {
        *count = sizeof(g_session_commands) / sizeof(g_session_commands[0]);
    }
    return g_session_commands;
}
