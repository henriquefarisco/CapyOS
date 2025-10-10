#include "shell/commands.h"
#include "shell/core.h"

#include "drivers/input/keyboard.h"
#include "fs/buffer.h"

static int cmd_config_keyboard(struct shell_context *ctx, int argc, char **argv) {
    (void)ctx;
    if (shell_help_requested(argc, argv)) {
        shell_print("Uso: config-keyboard [layout]\n"
                    "Sem argumentos mostra os layouts disponiveis.\n"
                    "Com um argumento (ex.: config-keyboard br-abnt2) altera o layout ativo.\n");
        return 0;
    }
    if (argc < 2) {
        shell_print("Layout atual: ");
        shell_print(keyboard_current_layout());
        shell_newline();
        shell_print("Layouts disponiveis:\n");
        for (size_t i = 0; i < keyboard_layout_count(); ++i) {
            shell_print(" - ");
            shell_print(keyboard_layout_name(i));
            shell_print(" : ");
            shell_print(keyboard_layout_description(i));
            shell_newline();
        }
        return 0;
    }
    if (keyboard_set_layout_by_name(argv[1]) != 0) {
        shell_print_error("layout desconhecido");
        shell_suggest_help("config-keyboard");
        return -1;
    }
    shell_print_ok("layout atualizado");
    return 0;
}

static int cmd_do_sync(struct shell_context *ctx, int argc, char **argv) {
    (void)ctx;
    if (shell_help_requested(argc, argv)) {
        shell_print("Uso: do-sync\nForca a gravacao do buffer de disco.");
        shell_newline();
        return 0;
    }
    (void)argc;
    (void)argv;
    struct super_block *root = vfs_root();
    if (!root || !root->bdev) {
        shell_print_error("sem dispositivo");
        return -1;
    }
    buffer_cache_sync(root->bdev);
    shell_print_ok("buffers sincronizados");
    return 0;
}

static const struct shell_command g_system_control_commands[] = {
    { "config-keyboard", cmd_config_keyboard },
    { "do-sync", cmd_do_sync },
};

const struct shell_command *shell_commands_system_control(size_t *count) {
    if (count) {
        *count = sizeof(g_system_control_commands) / sizeof(g_system_control_commands[0]);
    }
    return g_system_control_commands;
}
