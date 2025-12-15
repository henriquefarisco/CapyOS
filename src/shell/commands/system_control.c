#include "shell/commands.h"
#include "shell/core.h"

#include "drivers/input/keyboard.h"
#include "fs/buffer.h"
#include "fs/vfs.h"
#include "arch/x86/hw/io.h"

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

static void sync_and_flush(void) {
    struct super_block *root = vfs_root();
    if (root && root->bdev) {
        buffer_cache_sync(root->bdev);
    }
}

static void do_hard_reboot(void) {
    sync_and_flush();
    shell_print("Reiniciando...\n");
    __asm__ volatile("cli");
    outb(0x64, 0xFE); // comando de reset via controlador de teclado
    while (1) { __asm__ volatile("hlt"); }
}

static void do_power_off(void) {
    sync_and_flush();
    shell_print("Desligando...\n");
    __asm__ volatile("cli");
    while (1) { __asm__ volatile("hlt"); }
}

static int cmd_shutdown_reboot(struct shell_context *ctx, int argc, char **argv) {
    (void)ctx;
    (void)argv;
    if (shell_help_requested(argc, argv)) {
        shell_print("Uso: shutdown-reboot\nReinicia o sistema de forma controlada (sincroniza buffers).");
        shell_newline();
        return 0;
    }
    do_hard_reboot();
    return 0;
}

static int cmd_shutdown_off(struct shell_context *ctx, int argc, char **argv) {
    (void)ctx;
    (void)argv;
    if (shell_help_requested(argc, argv)) {
        shell_print("Uso: shutdown-off\nDesliga o sistema (halt) apos sincronizar buffers.");
        shell_newline();
        return 0;
    }
    do_power_off();
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
    { "shutdown-reboot", cmd_shutdown_reboot },
    { "shutdown-off", cmd_shutdown_off },
    { "do-sync", cmd_do_sync },
};

const struct shell_command *shell_commands_system_control(size_t *count) {
    if (count) {
        *count = sizeof(g_system_control_commands) / sizeof(g_system_control_commands[0]);
    }
    return g_system_control_commands;
}
