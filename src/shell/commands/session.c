#include "shell/commands.h"
#include "shell/core.h"

#include "core/localization.h"
#include "drivers/video/vga.h"

static int cmd_mess(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    (void)ctx;
    if (shell_help_requested(argc, argv)) {
        shell_print(localization_select(
            language, "Uso: mess\nLimpa a tela do terminal.\n",
            "Usage: mess\nClears the terminal screen.\n",
            "Uso: mess\nLimpia la pantalla del terminal.\n"));
        return 0;
    }
    vga_clear();
    return 0;
}

static int cmd_bye(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    if (shell_help_requested(argc, argv)) {
        if (shell_string_equal(language, "en")) {
            shell_print("Usage: bye\nEnds the current session and returns to the login screen.\n");
        } else if (shell_string_equal(language, "es")) {
            shell_print("Uso: bye\nCierra la sesion actual y vuelve a la pantalla de inicio de sesion.\n");
        } else {
            shell_print("Uso: bye\nEncerra a sessao atual e retorna a tela de login.\n");
        }
        return 0;
    }
    (void)argc;
    (void)argv;
    shell_request_logout(ctx);
    shell_print(localization_text_for(language, LOC_TEXT_LOGOUT_MESSAGE));
    return 0;
}

static struct shell_command g_session_commands[2];
static int g_session_commands_initialized = 0;

static void init_session_commands(void) {
    if (g_session_commands_initialized) {
        return;
    }
    g_session_commands[0].name = "mess";
    g_session_commands[0].handler = cmd_mess;
    g_session_commands[1].name = "bye";
    g_session_commands[1].handler = cmd_bye;
    g_session_commands_initialized = 1;
}

const struct shell_command *shell_commands_session(size_t *count) {
    init_session_commands();
    if (count) {
        *count = 2;
    }
    return g_session_commands;
}
