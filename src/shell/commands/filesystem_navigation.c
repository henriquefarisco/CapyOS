#include "shell/commands.h"
#include "shell/core.h"

#include "drivers/video/vga.h"

struct list_ctx {
    int count;
};

static int list_callback(const char *name, uint16_t mode, void *userdata) {
    struct list_ctx *ctx = (struct list_ctx *)userdata;
    if (!name || name[0] == '\0') {
        return 0;
    }
    vga_write(" - ");
    vga_write(name);
    if (mode & VFS_MODE_DIR) {
        vga_write("/");
    }
    vga_newline();
    ctx->count++;
    return 0;
}

static int cmd_list(struct shell_context *ctx, int argc, char **argv) {
    if (shell_handle_help(argc, argv, "list [caminho]",
                          "Lista os itens do diretorio atual ou do caminho informado.")) {
        return 0;
    }
    const char *target = (argc > 1) ? argv[1] : ".";
    char path[SHELL_PATH_BUFFER];
    if (shell_resolve_path(ctx, target, path, sizeof(path)) != 0) {
        shell_print_error("caminho invalido");
        shell_suggest_help("list");
        return -1;
    }
    struct list_ctx lctx;
    lctx.count = 0;
    if (vfs_listdir(path, list_callback, &lctx) != 0) {
        shell_print_error("nao foi possivel listar");
        shell_suggest_help("list");
        return -1;
    }
    if (lctx.count == 0) {
        shell_print("(vazio)\n");
    }
    return 0;
}

static int cmd_go(struct shell_context *ctx, int argc, char **argv) {
    if (shell_handle_help(argc, argv, "go <caminho>",
                          "Altera o diretorio atual da sessao.")) {
        return 0;
    }
    if (argc < 2) {
        shell_print_error("informe destino");
        shell_suggest_help("go");
        return -1;
    }
    char path[SHELL_PATH_BUFFER];
    if (session_resolve_path(ctx->session, argv[1], path, sizeof(path)) != 0) {
        shell_print_error("caminho invalido");
        shell_suggest_help("go");
        return -1;
    }
    shell_trim_trailing_slash(path);
    if (shell_path_is_file(path)) {
        shell_print_error("destino eh um arquivo");
        return -1;
    }
    if (session_set_cwd(ctx->session, path) != 0) {
        shell_print_error("nao foi possivel mudar");
        shell_suggest_help("go");
        return -1;
    }
    shell_print_ok(path);
    return 0;
}

static int cmd_mypath(struct shell_context *ctx, int argc, char **argv) {
    if (shell_handle_help(argc, argv, "mypath", "Exibe o caminho atual da sessao.")) {
        return 0;
    }
    (void)argc;
    (void)argv;
    shell_print(session_cwd(ctx->session));
    shell_newline();
    return 0;
}

static const struct shell_command g_fs_nav_commands[] = {
    { "list", cmd_list },
    { "go", cmd_go },
    { "mypath", cmd_mypath },
};

const struct shell_command *shell_commands_filesystem_navigation(size_t *count) {
    if (count) {
        *count = sizeof(g_fs_nav_commands) / sizeof(g_fs_nav_commands[0]);
    }
    return g_fs_nav_commands;
}
