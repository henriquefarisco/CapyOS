#include "shell/commands.h"
#include "shell/core.h"

#include "lang/localization.h"

struct list_ctx {
    int count;
};

enum fs_nav_text_id {
    FS_NAV_HELP_LIST = 0,
    FS_NAV_HELP_GO,
    FS_NAV_HELP_MYPATH,
    FS_NAV_INVALID_PATH,
    FS_NAV_CANNOT_LIST,
    FS_NAV_EMPTY,
    FS_NAV_REQUIRE_DESTINATION,
    FS_NAV_TARGET_IS_FILE,
    FS_NAV_CANNOT_CHANGE,
};

static const char *fs_nav_text(const char *language, enum fs_nav_text_id id) {
    switch (id) {
    case FS_NAV_HELP_LIST:
        return localization_select(
            language,
            "Lista os itens do diretorio atual ou do caminho informado.",
            "Lists items in the current directory or in the provided path.",
            "Lista los elementos del directorio actual o de la ruta indicada.");
    case FS_NAV_HELP_GO:
        return localization_select(
            language,
            "Altera o diretorio atual da sessao.",
            "Changes the current directory for the session.",
            "Cambia el directorio actual de la sesion.");
    case FS_NAV_HELP_MYPATH:
        return localization_select(
            language,
            "Exibe o caminho atual da sessao.",
            "Shows the current path for the session.",
            "Muestra la ruta actual de la sesion.");
    case FS_NAV_INVALID_PATH:
        return localization_select(language, "caminho invalido", "invalid path",
                                   "ruta invalida");
    case FS_NAV_CANNOT_LIST:
        return localization_select(language, "nao foi possivel listar",
                                   "could not list directory",
                                   "no fue posible listar");
    case FS_NAV_EMPTY:
        return localization_select(language, "(vazio)\n", "(empty)\n",
                                   "(vacio)\n");
    case FS_NAV_REQUIRE_DESTINATION:
        return localization_select(language, "informe destino",
                                   "provide destination",
                                   "indica destino");
    case FS_NAV_TARGET_IS_FILE:
        return localization_select(language, "destino eh um arquivo",
                                   "destination is a file",
                                   "el destino es un archivo");
    case FS_NAV_CANNOT_CHANGE:
    default:
        return localization_select(language, "nao foi possivel mudar",
                                   "could not change directory",
                                   "no fue posible cambiar");
    }
}

static int list_callback(const char *name, uint16_t mode, void *userdata) {
    struct list_ctx *ctx = (struct list_ctx *)userdata;
    if (!name || name[0] == '\0') {
        return 0;
    }
    shell_print(" - ");
    shell_print(name);
    if (mode & VFS_MODE_DIR) {
        shell_print("/");
    }
    shell_newline();
    ctx->count++;
    return 0;
}

static int cmd_list(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    if (shell_handle_help(argc, argv, "list [path]",
                          fs_nav_text(language, FS_NAV_HELP_LIST))) {
        return 0;
    }
    const char *target = (argc > 1) ? argv[1] : ".";
    char path[SHELL_PATH_BUFFER];
    if (shell_resolve_path(ctx, target, path, sizeof(path)) != 0) {
        shell_print_error(fs_nav_text(language, FS_NAV_INVALID_PATH));
        shell_suggest_help("list");
        return -1;
    }
    struct list_ctx lctx;
    lctx.count = 0;
    if (vfs_listdir(path, list_callback, &lctx) != 0) {
        shell_print_error(fs_nav_text(language, FS_NAV_CANNOT_LIST));
        shell_suggest_help("list");
        return -1;
    }
    if (lctx.count == 0) {
        shell_print(fs_nav_text(language, FS_NAV_EMPTY));
    }
    return 0;
}

static int cmd_go(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    if (shell_handle_help(argc, argv, "go <path>",
                          fs_nav_text(language, FS_NAV_HELP_GO))) {
        return 0;
    }
    if (argc < 2) {
        shell_print_error(fs_nav_text(language, FS_NAV_REQUIRE_DESTINATION));
        shell_suggest_help("go");
        return -1;
    }
    char path[SHELL_PATH_BUFFER];
    if (session_resolve_path(ctx->session, argv[1], path, sizeof(path)) != 0) {
        shell_print_error(fs_nav_text(language, FS_NAV_INVALID_PATH));
        shell_suggest_help("go");
        return -1;
    }
    shell_trim_trailing_slash(path);
    if (shell_path_is_file(path)) {
        shell_print_error(fs_nav_text(language, FS_NAV_TARGET_IS_FILE));
        return -1;
    }
    if (session_set_cwd(ctx->session, path) != 0) {
        shell_print_error(fs_nav_text(language, FS_NAV_CANNOT_CHANGE));
        shell_suggest_help("go");
        return -1;
    }
    shell_print_ok(path);
    return 0;
}

static int cmd_mypath(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    if (shell_handle_help(argc, argv, "mypath",
                          fs_nav_text(language, FS_NAV_HELP_MYPATH))) {
        return 0;
    }
    (void)argc;
    (void)argv;
    shell_print(session_cwd(ctx->session));
    shell_newline();
    return 0;
}

static struct shell_command g_fs_nav_commands[3];
static int g_fs_nav_commands_initialized = 0;

static void init_fs_nav_commands(void) {
    if (g_fs_nav_commands_initialized) {
        return;
    }
    g_fs_nav_commands[0].name = "list";
    g_fs_nav_commands[0].handler = cmd_list;
    g_fs_nav_commands[1].name = "go";
    g_fs_nav_commands[1].handler = cmd_go;
    g_fs_nav_commands[2].name = "mypath";
    g_fs_nav_commands[2].handler = cmd_mypath;
    g_fs_nav_commands_initialized = 1;
}

const struct shell_command *shell_commands_filesystem_navigation(size_t *count) {
    init_fs_nav_commands();
    if (count) {
        *count = 3;
    }
    return g_fs_nav_commands;
}
