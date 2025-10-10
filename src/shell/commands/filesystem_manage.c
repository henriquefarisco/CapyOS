#include "shell/commands.h"
#include "shell/core.h"

#include "drivers/video/vga.h"

static int shell_dir_recursive(struct shell_context *ctx, const char *input) {
    char abs_path[SHELL_PATH_BUFFER];
    if (session_resolve_path(ctx->session, input, abs_path, sizeof(abs_path)) != 0) {
        return -1;
    }
    shell_trim_trailing_slash(abs_path);
    if (shell_cstring_length(abs_path) == 0) {
        shell_copy(abs_path, sizeof(abs_path), "/");
    }
    const struct user_record *user = session_user(ctx->session);
    uint32_t uid = user ? user->uid : 0;
    uint32_t gid = user ? user->gid : 0;
    char build[SHELL_PATH_BUFFER];
    size_t build_len = 0;
    build[build_len++] = '/';
    build[build_len] = '\0';
    const char *p = abs_path;
    while (*p == '/') {
        ++p;
    }
    while (*p) {
        const char *start = p;
        size_t len = 0;
        while (start[len] && start[len] != '/') {
            ++len;
        }
        if (len > 0) {
            if (build_len > 1) {
                if (build_len + 1 >= sizeof(build)) {
                    return -1;
                }
                build[build_len++] = '/';
            }
            if (build_len + len >= sizeof(build)) {
                return -1;
            }
            for (size_t i = 0; i < len; ++i) {
                build[build_len++] = start[i];
            }
            build[build_len] = '\0';

            struct dentry *d = NULL;
            if (vfs_lookup(build, &d) != 0) {
                struct vfs_metadata meta = { uid, gid, 0755 };
                if (vfs_create(build, VFS_MODE_DIR, &meta) != 0) {
                    return -1;
                }
            } else {
                if (d->refcount) {
                    d->refcount--;
                }
                struct vfs_stat st;
                if (vfs_stat_path(build, &st) == 0 && (st.mode & VFS_MODE_DIR) == 0) {
                    return -2;
                }
            }
        }
        p += len;
        while (*p == '/') {
            ++p;
        }
    }
    return 0;
}

static int cmd_mk_file(struct shell_context *ctx, int argc, char **argv) {
    if (shell_handle_help(argc, argv, "mk-file <arquivo>",
                          "Cria um arquivo vazio no diretorio atual ou indicado.")) {
        return 0;
    }
    if (argc < 2) {
        shell_print_error("informe arquivo");
        shell_suggest_help("mk-file");
        return -1;
    }
    char path[SHELL_PATH_BUFFER];
    if (shell_resolve_path(ctx, argv[1], path, sizeof(path)) != 0) {
        shell_print_error("caminho invalido");
        shell_suggest_help("mk-file");
        return -1;
    }
    shell_trim_trailing_slash(path);
    if (shell_path_is_dir(path)) {
        shell_print_error("ja existe diretorio neste caminho");
        shell_suggest_help("mk-file");
        return -1;
    }
    struct vfs_metadata meta;
    shell_fill_metadata(ctx, VFS_MODE_FILE, &meta);
    if (vfs_create(path, VFS_MODE_FILE, &meta) != 0) {
        shell_print_error("nao foi possivel criar arquivo");
        shell_suggest_help("mk-file");
        return -1;
    }
    shell_print_ok(path);
    return 0;
}

static int cmd_mk_dir(struct shell_context *ctx, int argc, char **argv) {
    if (shell_handle_help(argc, argv, "mk-dir <caminho>",
                          "Cria um diretorio (e pais necessários) no caminho indicado.")) {
        return 0;
    }
    if (argc < 2) {
        shell_print_error("informe diretorio");
        shell_suggest_help("mk-dir");
        return -1;
    }
    if (shell_dir_recursive(ctx, argv[1]) != 0) {
        shell_print_error("nao foi possivel criar diretorio");
        shell_suggest_help("mk-dir");
        return -1;
    }
    shell_print_ok(argv[1]);
    return 0;
}

static int cmd_kill_file(struct shell_context *ctx, int argc, char **argv) {
    (void)ctx;
    if (shell_handle_help(argc, argv, "kill-file <arquivo>", "Remove o arquivo informado.")) {
        return 0;
    }
    if (argc < 2) {
        shell_print_error("informe arquivo");
        shell_suggest_help("kill-file");
        return -1;
    }
    char path[SHELL_PATH_BUFFER];
    if (shell_resolve_path(ctx, argv[1], path, sizeof(path)) != 0) {
        shell_print_error("caminho invalido");
        shell_suggest_help("kill-file");
        return -1;
    }
    shell_trim_trailing_slash(path);
    if (vfs_unlink(path) != 0) {
        shell_print_error("falha ao remover");
        shell_suggest_help("kill-file");
        return -1;
    }
    shell_print_ok("removido");
    return 0;
}

static int cmd_kill_dir(struct shell_context *ctx, int argc, char **argv) {
    (void)ctx;
    if (shell_handle_help(argc, argv, "kill-dir <diretorio>", "Remove um diretorio vazio.")) {
        return 0;
    }
    if (argc < 2) {
        shell_print_error("informe diretorio");
        shell_suggest_help("kill-dir");
        return -1;
    }
    char path[SHELL_PATH_BUFFER];
    if (shell_resolve_path(ctx, argv[1], path, sizeof(path)) != 0) {
        shell_print_error("caminho invalido");
        shell_suggest_help("kill-dir");
        return -1;
    }
    shell_trim_trailing_slash(path);
    if (vfs_rmdir(path) != 0) {
        shell_print_error("falha ao remover");
        shell_suggest_help("kill-dir");
        return -1;
    }
    shell_print_ok("diretorio removido");
    return 0;
}

static int cmd_move(struct shell_context *ctx, int argc, char **argv) {
    (void)ctx;
    if (shell_handle_help(argc, argv, "move <origem> <destino>",
                          "Move ou renomeia um arquivo/diretorio.")) {
        return 0;
    }
    if (argc < 3) {
        shell_print_error("informe origem e destino");
        shell_suggest_help("move");
        return -1;
    }
    char src_path[SHELL_PATH_BUFFER];
    char dst_path[SHELL_PATH_BUFFER];
    if (shell_resolve_path(ctx, argv[1], src_path, sizeof(src_path)) != 0) {
        shell_print_error("origem invalida");
        shell_suggest_help("move");
        return -1;
    }
    if (shell_resolve_path(ctx, argv[2], dst_path, sizeof(dst_path)) != 0) {
        shell_print_error("destino invalido");
        shell_suggest_help("move");
        return -1;
    }
    shell_trim_trailing_slash(src_path);
    shell_trim_trailing_slash(dst_path);
    if (shell_path_is_dir(dst_path)) {
        char combined[SHELL_PATH_BUFFER];
        if (shell_join_path(dst_path, shell_basename(src_path), combined, sizeof(combined)) != 0) {
            shell_print_error("destino muito longo");
            shell_suggest_help("move");
            return -1;
        }
        shell_copy(dst_path, sizeof(dst_path), combined);
    }
    if (vfs_rename(src_path, dst_path) != 0) {
        shell_print_error("nao foi possivel mover");
        shell_suggest_help("move");
        return -1;
    }
    shell_print_ok(dst_path);
    return 0;
}

static int cmd_clone(struct shell_context *ctx, int argc, char **argv) {
    (void)ctx;
    if (shell_handle_help(argc, argv, "clone <origem> <destino>",
                          "Copia o conteudo de um arquivo para outro arquivo.")) {
        return 0;
    }
    if (argc < 3) {
        shell_print_error("informe origem e destino");
        shell_suggest_help("clone");
        return -1;
    }
    char src_path[SHELL_PATH_BUFFER];
    char dst_path[SHELL_PATH_BUFFER];
    if (shell_resolve_path(ctx, argv[1], src_path, sizeof(src_path)) != 0) {
        shell_print_error("origem invalida");
        shell_suggest_help("clone");
        return -1;
    }
    shell_trim_trailing_slash(src_path);
    if (!shell_path_is_file(src_path)) {
        shell_print_error("origem nao e arquivo");
        shell_suggest_help("clone");
        return -1;
    }
    if (shell_resolve_path(ctx, argv[2], dst_path, sizeof(dst_path)) != 0) {
        shell_print_error("destino invalido");
        shell_suggest_help("clone");
        return -1;
    }
    shell_trim_trailing_slash(dst_path);
    if (shell_path_is_dir(dst_path)) {
        char combined[SHELL_PATH_BUFFER];
        if (shell_join_path(dst_path, shell_basename(src_path), combined, sizeof(combined)) != 0) {
            shell_print_error("destino muito longo");
            shell_suggest_help("clone");
            return -1;
        }
        shell_copy(dst_path, sizeof(dst_path), combined);
    }
    if (shell_path_is_file(dst_path)) {
        shell_print_error("destino ja existe");
        shell_suggest_help("clone");
        return -1;
    }

    struct vfs_metadata meta;
    shell_fill_metadata(ctx, VFS_MODE_FILE, &meta);
    if (vfs_create(dst_path, VFS_MODE_FILE, &meta) != 0) {
        shell_print_error("nao foi possivel criar destino");
        shell_suggest_help("clone");
        return -1;
    }

    struct file *src = shell_open_file_read(src_path);
    struct file *dst = shell_open_file_write(dst_path);
    if (!src || !dst) {
        if (src) vfs_close(src);
        if (dst) vfs_close(dst);
        vfs_unlink(dst_path);
        shell_print_error("falha ao abrir arquivos");
        shell_suggest_help("clone");
        return -1;
    }

    int copy_res = shell_copy_stream(src, dst);
    vfs_close(src);
    vfs_close(dst);
    if (copy_res != 0) {
        vfs_unlink(dst_path);
        shell_print_error("falha na copia");
        shell_suggest_help("clone");
        return -1;
    }
    shell_print_ok(dst_path);
    return 0;
}

static int cmd_stats_file(struct shell_context *ctx, int argc, char **argv) {
    (void)ctx;
    if (shell_handle_help(argc, argv, "stats-file <caminho>",
                          "Mostra estatisticas de permissao, tamanho e donos.")) {
        return 0;
    }
    if (argc < 2) {
        shell_print_error("informe caminho");
        shell_suggest_help("stats-file");
        return -1;
    }
    char path[SHELL_PATH_BUFFER];
    if (shell_resolve_path(ctx, argv[1], path, sizeof(path)) != 0) {
        shell_print_error("caminho invalido");
        shell_suggest_help("stats-file");
        return -1;
    }
    shell_trim_trailing_slash(path);
    struct vfs_stat st;
    if (vfs_stat_path(path, &st) != 0) {
        shell_print_error("nao foi possivel coletar dados");
        shell_suggest_help("stats-file");
        return -1;
    }
    shell_print("alvo: ");
    shell_print(path);
    shell_newline();
    shell_print("tipo: ");
    shell_print((st.mode & VFS_MODE_DIR) ? "diretorio" : "arquivo");
    shell_newline();
    shell_print("tamanho: ");
    shell_print_number(st.size);
    shell_print(" bytes\n");
    shell_print("uid: ");
    shell_print_number(st.uid);
    shell_print(" gid: ");
    shell_print_number(st.gid);
    shell_newline();
    char perm_buf[5];
    shell_format_perm(st.perm, perm_buf);
    shell_print("permissoes: 0");
    shell_print(perm_buf);
    shell_newline();
    return 0;
}

static int cmd_type(struct shell_context *ctx, int argc, char **argv) {
    (void)ctx;
    if (shell_handle_help(argc, argv, "type <caminho>", "Informa o tipo basico do arquivo.")) {
        return 0;
    }
    if (argc < 2) {
        shell_print_error("informe caminho");
        shell_suggest_help("type");
        return -1;
    }
    char path[SHELL_PATH_BUFFER];
    if (shell_resolve_path(ctx, argv[1], path, sizeof(path)) != 0) {
        shell_print_error("caminho invalido");
        shell_suggest_help("type");
        return -1;
    }
    shell_trim_trailing_slash(path);
    struct vfs_stat st;
    if (vfs_stat_path(path, &st) != 0) {
        shell_print_error("alvo inexistente");
        return -1;
    }
    shell_print(path);
    shell_print(": ");
    if (st.mode & VFS_MODE_DIR) {
        shell_print("diretorio");
    } else if (st.mode & VFS_MODE_FILE) {
        shell_print("arquivo");
    } else {
        shell_print("desconhecido");
    }
    shell_print(" perm=0");
    char perm_buf[5];
    shell_format_perm(st.perm, perm_buf);
    shell_print(perm_buf);
    shell_print(" uid=");
    shell_print_number(st.uid);
    shell_print(" gid=");
    shell_print_number(st.gid);
    shell_newline();
    return 0;
}

static const struct shell_command g_fs_manage_commands[] = {
    { "mk-file", cmd_mk_file },
    { "mk-dir", cmd_mk_dir },
    { "kill-file", cmd_kill_file },
    { "kill-dir", cmd_kill_dir },
    { "move", cmd_move },
    { "clone", cmd_clone },
    { "stats-file", cmd_stats_file },
    { "type", cmd_type },
};

const struct shell_command *shell_commands_filesystem_manage(size_t *count) {
    if (count) {
        *count = sizeof(g_fs_manage_commands) / sizeof(g_fs_manage_commands[0]);
    }
    return g_fs_manage_commands;
}
