#include "shell/commands.h"
#include "shell/core.h"

#include "drivers/video/vga.h"
#include "memory/kmem.h"

enum hunt_target {
    HUNT_FILES,
    HUNT_DIRS,
    HUNT_ANY
};

struct hunt_ctx {
    struct shell_context *shell;
    const char *pattern;
    enum hunt_target target;
    int depth;
    int *matches;
    char current[SHELL_PATH_BUFFER];
    int error;
};

struct search_ctx {
    struct shell_context *shell;
    const char *pattern;
    int depth;
    int *matches;
    char current[SHELL_PATH_BUFFER];
    int error;
};

static int hunt_callback(const char *name, uint16_t mode, void *userdata) {
    struct hunt_ctx *ctx = (struct hunt_ctx *)userdata;
    if (!ctx || !name || name[0] == '\0') {
        return 0;
    }
    char path[SHELL_PATH_BUFFER];
    if (shell_join_path(ctx->current, name, path, sizeof(path)) != 0) {
        ctx->error = 1;
        return -1;
    }
    if (mode & VFS_MODE_DIR) {
        if (ctx->target == HUNT_DIRS || ctx->target == HUNT_ANY) {
            if (shell_string_contains(shell_basename(path), ctx->pattern)) {
                shell_print(path);
                shell_newline();
                if (ctx->matches) {
                    (*ctx->matches)++;
                }
            }
        }
        if (ctx->depth < SHELL_HUNT_MAX_DEPTH) {
            struct hunt_ctx child = *ctx;
            child.depth = ctx->depth + 1;
            shell_copy(child.current, sizeof(child.current), path);
            if (vfs_listdir(path, hunt_callback, &child) != 0) {
                ctx->error = 1;
                return -1;
            }
        }
    } else if (mode & VFS_MODE_FILE) {
        if (ctx->target == HUNT_FILES || ctx->target == HUNT_ANY) {
            if (shell_string_contains(shell_basename(path), ctx->pattern)) {
                shell_print(path);
                shell_newline();
                if (ctx->matches) {
                    (*ctx->matches)++;
                }
            }
        }
    }
    return 0;
}

static int shell_hunt_walk(struct shell_context *ctx, const char *path, const char *pattern,
                           enum hunt_target target, int depth, int *matches) {
    struct hunt_ctx state;
    state.shell = ctx;
    state.pattern = pattern;
    state.target = target;
    state.depth = depth;
    state.matches = matches;
    state.error = 0;
    shell_copy(state.current, sizeof(state.current), path);
    if (vfs_listdir(path, hunt_callback, &state) != 0 || state.error) {
        return -1;
    }
    return 0;
}

static int search_callback(const char *name, uint16_t mode, void *userdata) {
    struct search_ctx *ctx = (struct search_ctx *)userdata;
    if (!ctx || !name || name[0] == '\0') {
        return 0;
    }
    char path[SHELL_PATH_BUFFER];
    if (shell_join_path(ctx->current, name, path, sizeof(path)) != 0) {
        ctx->error = 1;
        return -1;
    }
    if (mode & VFS_MODE_DIR) {
        if (ctx->depth < SHELL_HUNT_MAX_DEPTH) {
            struct search_ctx child = *ctx;
            child.depth = ctx->depth + 1;
            shell_copy(child.current, sizeof(child.current), path);
            if (vfs_listdir(path, search_callback, &child) != 0 || child.error) {
                ctx->error = 1;
                return -1;
            }
        }
    } else if (mode & VFS_MODE_FILE) {
        char *buffer = NULL;
        size_t len = 0;
        if (shell_read_file(path, &buffer, &len) != 0) {
            ctx->error = 1;
            return -1;
        }
        if (shell_string_contains(buffer, ctx->pattern)) {
            shell_print(path);
            shell_newline();
            if (ctx->matches) {
                (*ctx->matches)++;
            }
        }
        kfree(buffer);
    }
    return 0;
}

static int shell_search_text(struct shell_context *ctx, const char *path, const char *pattern,
                             int depth, int *matches) {
    struct search_ctx state;
    state.shell = ctx;
    state.pattern = pattern;
    state.depth = depth;
    state.matches = matches;
    state.error = 0;
    shell_copy(state.current, sizeof(state.current), path);
    if (vfs_listdir(path, search_callback, &state) != 0 || state.error) {
        return -1;
    }
    return 0;
}

static int shell_run_hunt(struct shell_context *ctx, enum hunt_target target, int argc, char **argv) {
    const char *pattern = (argc > 1) ? argv[1] : "";
    const char *start = (argc > 2) ? argv[2] : ".";
    char base[SHELL_PATH_BUFFER];
    if (shell_resolve_path(ctx, start, base, sizeof(base)) != 0) {
        shell_print_error("caminho invalido");
        shell_suggest_help(target == HUNT_FILES ? "hunt-file" :
                           target == HUNT_DIRS ? "hunt-dir" : "hunt-any");
        return -1;
    }
    shell_trim_trailing_slash(base);
    int matches = 0;
    int is_dir = shell_path_is_dir(base);
    int is_file = shell_path_is_file(base);
    if (!is_dir && !is_file) {
        shell_print_error("alvo inexistente");
        shell_suggest_help(target == HUNT_FILES ? "hunt-file" :
                           target == HUNT_DIRS ? "hunt-dir" : "hunt-any");
        return -1;
    }
    if (is_file && (target == HUNT_FILES || target == HUNT_ANY)) {
        if (shell_string_contains(shell_basename(base), pattern)) {
            shell_print(base);
            shell_newline();
            matches++;
        } else {
            shell_print("(nenhum resultado)\n");
        }
        if (matches == 0) {
            shell_print("(nenhum resultado)\n");
        }
        return 0;
    }
    if (is_dir) {
        if (target == HUNT_DIRS || target == HUNT_ANY) {
            if (shell_string_contains(shell_basename(base), pattern)) {
                shell_print(base);
                shell_newline();
                matches++;
            }
        }
        if (shell_hunt_walk(ctx, base, pattern, target, 0, &matches) != 0) {
            shell_print_error("falha na varredura");
            return -1;
        }
    }
    if (matches == 0) {
        shell_print("(nenhum resultado)\n");
    }
    return 0;
}

static int cmd_hunt_file(struct shell_context *ctx, int argc, char **argv) {
    if (shell_handle_help(argc, argv, "hunt-file <padrao> [caminho]",
                          "Busca arquivos cujo nome corresponde ao padrao.")) {
        return 0;
    }
    return shell_run_hunt(ctx, HUNT_FILES, argc, argv);
}

static int cmd_hunt_dir(struct shell_context *ctx, int argc, char **argv) {
    if (shell_handle_help(argc, argv, "hunt-dir <padrao> [caminho]",
                          "Busca diretorios cujo nome corresponde ao padrao.")) {
        return 0;
    }
    return shell_run_hunt(ctx, HUNT_DIRS, argc, argv);
}

static int cmd_hunt_any(struct shell_context *ctx, int argc, char **argv) {
    if (shell_handle_help(argc, argv, "hunt-any <padrao> [caminho]",
                          "Busca arquivos ou diretorios cujo nome corresponde ao padrao.")) {
        return 0;
    }
    return shell_run_hunt(ctx, HUNT_ANY, argc, argv);
}

static int cmd_find(struct shell_context *ctx, int argc, char **argv) {
    if (shell_handle_help(argc, argv, "find \"texto\" [caminho]",
                          "Procura por texto dentro de arquivos iniciando do caminho informado.")) {
        return 0;
    }
    if (argc < 2) {
        shell_print_error("informe termo");
        shell_suggest_help("find");
        return -1;
    }
    const char *pattern = argv[1];
    const char *start = (argc > 2) ? argv[2] : ".";
    char base[SHELL_PATH_BUFFER];
    if (shell_resolve_path(ctx, start, base, sizeof(base)) != 0) {
        shell_print_error("caminho invalido");
        shell_suggest_help("find");
        return -1;
    }
    shell_trim_trailing_slash(base);
    if (!shell_path_is_dir(base)) {
        shell_print_error("alvo inexistente");
        shell_suggest_help("find");
        return -1;
    }
    int matches = 0;
    if (shell_search_text(ctx, base, pattern, 0, &matches) != 0) {
        shell_print_error("falha na busca");
        return -1;
    }
    if (matches == 0) {
        shell_print("(nenhum resultado)\n");
    }
    return 0;
}

static const struct shell_command g_fs_search_commands[] = {
    { "hunt-file", cmd_hunt_file },
    { "hunt-dir", cmd_hunt_dir },
    { "hunt-any", cmd_hunt_any },
    { "find", cmd_find },
};

const struct shell_command *shell_commands_filesystem_search(size_t *count) {
    if (count) {
        *count = sizeof(g_fs_search_commands) / sizeof(g_fs_search_commands[0]);
    }
    return g_fs_search_commands;
}
