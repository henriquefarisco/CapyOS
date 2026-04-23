#include "shell/commands.h"
#include "shell/core.h"

#include "fs/vfs.h"
#include "lang/localization.h"
#include "drivers/video/vga.h"

enum fs_manage_text_id {
    FS_MANAGE_HELP_MK_FILE = 0,
    FS_MANAGE_HELP_MK_DIR,
    FS_MANAGE_HELP_KILL_FILE,
    FS_MANAGE_HELP_KILL_DIR,
    FS_MANAGE_HELP_MOVE,
    FS_MANAGE_HELP_CLONE,
    FS_MANAGE_HELP_STATS,
    FS_MANAGE_HELP_TYPE,
    FS_MANAGE_REQUIRE_FILE,
    FS_MANAGE_REQUIRE_DIR,
    FS_MANAGE_REQUIRE_PATH,
    FS_MANAGE_REQUIRE_SRC_DST,
    FS_MANAGE_INVALID_PATH,
    FS_MANAGE_SOURCE_INVALID,
    FS_MANAGE_DEST_INVALID,
    FS_MANAGE_ALREADY_DIR,
    FS_MANAGE_CANNOT_CREATE_FILE,
    FS_MANAGE_CANNOT_CREATE_DIR,
    FS_MANAGE_REMOVE_FAILED,
    FS_MANAGE_REMOVED,
    FS_MANAGE_DIR_REMOVED,
    FS_MANAGE_DEST_TOO_LONG,
    FS_MANAGE_CANNOT_MOVE,
    FS_MANAGE_SOURCE_NOT_FILE,
    FS_MANAGE_DEST_EXISTS,
    FS_MANAGE_CANNOT_CREATE_DEST,
    FS_MANAGE_OPEN_FAILED,
    FS_MANAGE_COPY_FAILED,
    FS_MANAGE_CANNOT_STAT,
    FS_MANAGE_TARGET_LABEL,
    FS_MANAGE_TYPE_LABEL,
    FS_MANAGE_SIZE_LABEL,
    FS_MANAGE_UID_LABEL,
    FS_MANAGE_GID_LABEL,
    FS_MANAGE_PERM_LABEL,
    FS_MANAGE_TARGET_MISSING,
    FS_MANAGE_DIR_TYPE,
    FS_MANAGE_FILE_TYPE,
    FS_MANAGE_UNKNOWN_TYPE,
    FS_MANAGE_BYTES_SUFFIX,
    FS_MANAGE_TYPE_SUFFIX,
    FS_MANAGE_PERM_SUFFIX,
};

static const char *fs_manage_text(const char *language,
                                  enum fs_manage_text_id id) {
    switch (id) {
    case FS_MANAGE_HELP_MK_FILE:
        return localization_select(
            language,
            "Cria um arquivo vazio no diretorio atual ou indicado.",
            "Creates an empty file in the current or provided directory.",
            "Crea un archivo vacio en el directorio actual o indicado.");
    case FS_MANAGE_HELP_MK_DIR:
        return localization_select(
            language,
            "Cria um diretorio (e pais necessarios) no caminho indicado.",
            "Creates a directory (and needed parents) in the given path.",
            "Crea un directorio (y padres necesarios) en la ruta indicada.");
    case FS_MANAGE_HELP_KILL_FILE:
        return localization_select(language, "Remove o arquivo informado.",
                                   "Removes the provided file.",
                                   "Elimina el archivo indicado.");
    case FS_MANAGE_HELP_KILL_DIR:
        return localization_select(language, "Remove um diretorio vazio.",
                                   "Removes an empty directory.",
                                   "Elimina un directorio vacio.");
    case FS_MANAGE_HELP_MOVE:
        return localization_select(
            language,
            "Move ou renomeia um arquivo/diretorio.",
            "Moves or renames a file/directory.",
            "Mueve o renombra un archivo/directorio.");
    case FS_MANAGE_HELP_CLONE:
        return localization_select(
            language,
            "Copia o conteudo de um arquivo para outro arquivo.",
            "Copies the content of one file into another file.",
            "Copia el contenido de un archivo a otro archivo.");
    case FS_MANAGE_HELP_STATS:
        return localization_select(
            language,
            "Mostra estatisticas de permissao, tamanho e donos.",
            "Shows permission, size and ownership statistics.",
            "Muestra estadisticas de permisos, tamano y propietarios.");
    case FS_MANAGE_HELP_TYPE:
        return localization_select(language, "Informa o tipo basico do arquivo.",
                                   "Shows the basic file type.",
                                   "Informa el tipo basico del archivo.");
    case FS_MANAGE_REQUIRE_FILE:
        return localization_select(language, "informe arquivo", "provide file",
                                   "indica archivo");
    case FS_MANAGE_REQUIRE_DIR:
        return localization_select(language, "informe diretorio",
                                   "provide directory",
                                   "indica directorio");
    case FS_MANAGE_REQUIRE_PATH:
        return localization_select(language, "informe caminho", "provide path",
                                   "indica ruta");
    case FS_MANAGE_REQUIRE_SRC_DST:
        return localization_select(language, "informe origem e destino",
                                   "provide source and destination",
                                   "indica origen y destino");
    case FS_MANAGE_INVALID_PATH:
        return localization_select(language, "caminho invalido", "invalid path",
                                   "ruta invalida");
    case FS_MANAGE_SOURCE_INVALID:
        return localization_select(language, "origem invalida",
                                   "invalid source",
                                   "origen invalido");
    case FS_MANAGE_DEST_INVALID:
        return localization_select(language, "destino invalido",
                                   "invalid destination",
                                   "destino invalido");
    case FS_MANAGE_ALREADY_DIR:
        return localization_select(language, "ja existe diretorio neste caminho",
                                   "a directory already exists at this path",
                                   "ya existe un directorio en esta ruta");
    case FS_MANAGE_CANNOT_CREATE_FILE:
        return localization_select(language, "nao foi possivel criar arquivo",
                                   "could not create file",
                                   "no fue posible crear archivo");
    case FS_MANAGE_CANNOT_CREATE_DIR:
        return localization_select(language,
                                   "nao foi possivel criar diretorio",
                                   "could not create directory",
                                   "no fue posible crear directorio");
    case FS_MANAGE_REMOVE_FAILED:
        return localization_select(language, "falha ao remover",
                                   "remove failed",
                                   "fallo al eliminar");
    case FS_MANAGE_REMOVED:
        return localization_select(language, "removido", "removed", "eliminado");
    case FS_MANAGE_DIR_REMOVED:
        return localization_select(language, "diretorio removido",
                                   "directory removed",
                                   "directorio eliminado");
    case FS_MANAGE_DEST_TOO_LONG:
        return localization_select(language, "destino muito longo",
                                   "destination path too long",
                                   "destino demasiado largo");
    case FS_MANAGE_CANNOT_MOVE:
        return localization_select(language, "nao foi possivel mover",
                                   "could not move",
                                   "no fue posible mover");
    case FS_MANAGE_SOURCE_NOT_FILE:
        return localization_select(language, "origem nao e arquivo",
                                   "source is not a file",
                                   "el origen no es un archivo");
    case FS_MANAGE_DEST_EXISTS:
        return localization_select(language, "destino ja existe",
                                   "destination already exists",
                                   "el destino ya existe");
    case FS_MANAGE_CANNOT_CREATE_DEST:
        return localization_select(language, "nao foi possivel criar destino",
                                   "could not create destination",
                                   "no fue posible crear destino");
    case FS_MANAGE_OPEN_FAILED:
        return localization_select(language, "falha ao abrir arquivos",
                                   "failed to open files",
                                   "fallo al abrir archivos");
    case FS_MANAGE_COPY_FAILED:
        return localization_select(language, "falha na copia",
                                   "copy failed",
                                   "fallo en la copia");
    case FS_MANAGE_CANNOT_STAT:
        return localization_select(language, "nao foi possivel coletar dados",
                                   "could not collect data",
                                   "no fue posible recopilar datos");
    case FS_MANAGE_TARGET_LABEL:
        return localization_select(language, "alvo: ", "target: ",
                                   "objetivo: ");
    case FS_MANAGE_TYPE_LABEL:
        return localization_select(language, "tipo: ", "type: ", "tipo: ");
    case FS_MANAGE_SIZE_LABEL:
        return localization_select(language, "tamanho: ", "size: ",
                                   "tamano: ");
    case FS_MANAGE_UID_LABEL:
        return "uid: ";
    case FS_MANAGE_GID_LABEL:
        return " gid: ";
    case FS_MANAGE_PERM_LABEL:
        return localization_select(language, "permissoes: 0",
                                   "permissions: 0",
                                   "permisos: 0");
    case FS_MANAGE_TARGET_MISSING:
        return localization_select(language, "alvo inexistente",
                                   "target does not exist",
                                   "objetivo inexistente");
    case FS_MANAGE_DIR_TYPE:
        return localization_select(language, "diretorio", "directory",
                                   "directorio");
    case FS_MANAGE_FILE_TYPE:
        return localization_select(language, "arquivo", "file", "archivo");
    case FS_MANAGE_UNKNOWN_TYPE:
        return localization_select(language, "desconhecido", "unknown",
                                   "desconocido");
    case FS_MANAGE_BYTES_SUFFIX:
        return localization_select(language, " bytes\n", " bytes\n",
                                   " bytes\n");
    case FS_MANAGE_TYPE_SUFFIX:
        return localization_select(language, ": ", ": ", ": ");
    case FS_MANAGE_PERM_SUFFIX:
    default:
        return " perm=0";
    }
}

static const char *fs_manage_vfs_reason(const char *language, int rc) {
    switch (rc < 0 ? -rc : rc) {
    case VFS_ERR_ALREADY_EXISTS:
        return localization_select(language, "ja existe", "already exists",
                                   "ya existe");
    case VFS_ERR_PERMISSION_DENIED:
        return localization_select(language, "permissao negada",
                                   "permission denied",
                                   "permiso denegado");
    case VFS_ERR_NOT_FOUND:
        return localization_select(language, "alvo inexistente",
                                   "target does not exist",
                                   "objetivo inexistente");
    case VFS_ERR_NOT_DIRECTORY:
        return localization_select(language, "nao e diretorio",
                                   "not a directory",
                                   "no es un directorio");
    case VFS_ERR_IS_DIRECTORY:
        return localization_select(language, "e um diretorio",
                                   "is a directory",
                                   "es un directorio");
    case VFS_ERR_DIR_NOT_EMPTY:
        return localization_select(language, "diretorio nao esta vazio",
                                   "directory is not empty",
                                   "el directorio no esta vacio");
    case VFS_ERR_NAME_TOO_LONG:
        return localization_select(language, "nome muito longo",
                                   "name too long",
                                   "nombre demasiado largo");
    case VFS_ERR_UNSUPPORTED:
        return localization_select(language, "operacao nao suportada",
                                   "operation not supported",
                                   "operacion no soportada");
    case VFS_ERR_INVALID_PATH:
        return localization_select(language, "caminho invalido",
                                   "invalid path",
                                   "ruta invalida");
    case VFS_ERR_INVALID_ARGUMENT:
        return localization_select(language, "argumento invalido",
                                   "invalid argument",
                                   "argumento invalido");
    case VFS_ERR_NO_MEMORY:
        return localization_select(language, "memoria insuficiente",
                                   "out of memory",
                                   "memoria insuficiente");
    case VFS_ERR_IO:
        return localization_select(language, "falha de entrada/saida",
                                   "input/output failure",
                                   "fallo de entrada/salida");
    default:
        return vfs_error_string(rc);
    }
}

static void fs_manage_print_vfs_error(const char *language, const char *prefix,
                                      int rc) {
    shell_print("[erro] ");
    if (prefix && prefix[0]) {
        shell_print(prefix);
        shell_print(": ");
    }
    shell_print(fs_manage_vfs_reason(language, rc));
    shell_newline();
}

static int shell_dir_recursive(struct shell_context *ctx, const char *input) {
    char abs_path[SHELL_PATH_BUFFER];
    if (session_resolve_path(ctx->session, input, abs_path, sizeof(abs_path)) != 0) {
        return -VFS_ERR_INVALID_PATH;
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
                    return -VFS_ERR_NAME_TOO_LONG;
                }
                build[build_len++] = '/';
            }
            if (build_len + len >= sizeof(build)) {
                return -VFS_ERR_NAME_TOO_LONG;
            }
            for (size_t i = 0; i < len; ++i) {
                build[build_len++] = start[i];
            }
            build[build_len] = '\0';

            struct dentry *d = NULL;
            if (vfs_lookup(build, &d) != 0) {
                struct vfs_metadata meta = { uid, gid, 0755 };
                int rc = vfs_create(build, VFS_MODE_DIR, &meta);
                if (rc != 0) {
                    return rc;
                }
            } else {
                if (d->refcount) {
                    d->refcount--;
                }
                struct vfs_stat st;
                if (vfs_stat_path(build, &st) == 0 && (st.mode & VFS_MODE_DIR) == 0) {
                    return -VFS_ERR_NOT_DIRECTORY;
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
    const char *language = shell_current_language();
    if (shell_handle_help(argc, argv, "mk-file <file>",
                          fs_manage_text(language, FS_MANAGE_HELP_MK_FILE))) {
        return 0;
    }
    if (argc < 2) {
        shell_print_error(fs_manage_text(language, FS_MANAGE_REQUIRE_FILE));
        shell_suggest_help("mk-file");
        return -1;
    }
    char path[SHELL_PATH_BUFFER];
    if (shell_resolve_path(ctx, argv[1], path, sizeof(path)) != 0) {
        shell_print_error(fs_manage_text(language, FS_MANAGE_INVALID_PATH));
        shell_suggest_help("mk-file");
        return -1;
    }
    shell_trim_trailing_slash(path);
    if (shell_path_is_dir(path)) {
        shell_print_error(fs_manage_text(language, FS_MANAGE_ALREADY_DIR));
        shell_suggest_help("mk-file");
        return -1;
    }
    struct vfs_metadata meta;
    shell_fill_metadata(ctx, VFS_MODE_FILE, &meta);
    {
        int rc = vfs_create(path, VFS_MODE_FILE, &meta);
        if (rc != 0) {
        fs_manage_print_vfs_error(
            language, fs_manage_text(language, FS_MANAGE_CANNOT_CREATE_FILE), rc);
        shell_suggest_help("mk-file");
        return -1;
        }
    }
    shell_print_ok(path);
    return 0;
}

static int cmd_mk_dir(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    if (shell_handle_help(argc, argv, "mk-dir <path>",
                          fs_manage_text(language, FS_MANAGE_HELP_MK_DIR))) {
        return 0;
    }
    if (argc < 2) {
        shell_print_error(fs_manage_text(language, FS_MANAGE_REQUIRE_DIR));
        shell_suggest_help("mk-dir");
        return -1;
    }
    {
        int rc = shell_dir_recursive(ctx, argv[1]);
        if (rc != 0) {
        fs_manage_print_vfs_error(language,
                                  fs_manage_text(language, FS_MANAGE_CANNOT_CREATE_DIR),
                                  rc);
        shell_suggest_help("mk-dir");
        return -1;
        }
    }
    shell_print_ok(argv[1]);
    return 0;
}

static int cmd_kill_file(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    (void)ctx;
    if (shell_handle_help(argc, argv, "kill-file <file>",
                          fs_manage_text(language, FS_MANAGE_HELP_KILL_FILE))) {
        return 0;
    }
    if (argc < 2) {
        shell_print_error(fs_manage_text(language, FS_MANAGE_REQUIRE_FILE));
        shell_suggest_help("kill-file");
        return -1;
    }
    char path[SHELL_PATH_BUFFER];
    if (shell_resolve_path(ctx, argv[1], path, sizeof(path)) != 0) {
        shell_print_error(fs_manage_text(language, FS_MANAGE_INVALID_PATH));
        shell_suggest_help("kill-file");
        return -1;
    }
    shell_trim_trailing_slash(path);
    {
        int rc = vfs_unlink(path);
        if (rc != 0) {
        fs_manage_print_vfs_error(language,
                                  fs_manage_text(language, FS_MANAGE_REMOVE_FAILED), rc);
        shell_suggest_help("kill-file");
        return -1;
        }
    }
    shell_print_ok(fs_manage_text(language, FS_MANAGE_REMOVED));
    return 0;
}

static int cmd_kill_dir(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    (void)ctx;
    if (shell_handle_help(argc, argv, "kill-dir <directory>",
                          fs_manage_text(language, FS_MANAGE_HELP_KILL_DIR))) {
        return 0;
    }
    if (argc < 2) {
        shell_print_error(fs_manage_text(language, FS_MANAGE_REQUIRE_DIR));
        shell_suggest_help("kill-dir");
        return -1;
    }
    char path[SHELL_PATH_BUFFER];
    if (shell_resolve_path(ctx, argv[1], path, sizeof(path)) != 0) {
        shell_print_error(fs_manage_text(language, FS_MANAGE_INVALID_PATH));
        shell_suggest_help("kill-dir");
        return -1;
    }
    shell_trim_trailing_slash(path);
    {
        int rc = vfs_rmdir(path);
        if (rc != 0) {
        fs_manage_print_vfs_error(language,
                                  fs_manage_text(language, FS_MANAGE_REMOVE_FAILED), rc);
        shell_suggest_help("kill-dir");
        return -1;
        }
    }
    shell_print_ok(fs_manage_text(language, FS_MANAGE_DIR_REMOVED));
    return 0;
}

static int cmd_move(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    (void)ctx;
    if (shell_handle_help(argc, argv, "move <source> <destination>",
                          fs_manage_text(language, FS_MANAGE_HELP_MOVE))) {
        return 0;
    }
    if (argc < 3) {
        shell_print_error(fs_manage_text(language, FS_MANAGE_REQUIRE_SRC_DST));
        shell_suggest_help("move");
        return -1;
    }
    char src_path[SHELL_PATH_BUFFER];
    char dst_path[SHELL_PATH_BUFFER];
    if (shell_resolve_path(ctx, argv[1], src_path, sizeof(src_path)) != 0) {
        shell_print_error(fs_manage_text(language, FS_MANAGE_SOURCE_INVALID));
        shell_suggest_help("move");
        return -1;
    }
    if (shell_resolve_path(ctx, argv[2], dst_path, sizeof(dst_path)) != 0) {
        shell_print_error(fs_manage_text(language, FS_MANAGE_DEST_INVALID));
        shell_suggest_help("move");
        return -1;
    }
    shell_trim_trailing_slash(src_path);
    shell_trim_trailing_slash(dst_path);
    if (shell_path_is_dir(dst_path)) {
        char combined[SHELL_PATH_BUFFER];
        if (shell_join_path(dst_path, shell_basename(src_path), combined, sizeof(combined)) != 0) {
            shell_print_error(fs_manage_text(language, FS_MANAGE_DEST_TOO_LONG));
            shell_suggest_help("move");
            return -1;
        }
        shell_copy(dst_path, sizeof(dst_path), combined);
    }
    {
        int rc = vfs_rename(src_path, dst_path);
        if (rc != 0) {
        fs_manage_print_vfs_error(language,
                                  fs_manage_text(language, FS_MANAGE_CANNOT_MOVE), rc);
        shell_suggest_help("move");
        return -1;
        }
    }
    shell_print_ok(dst_path);
    return 0;
}

static int cmd_clone(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    (void)ctx;
    if (shell_handle_help(argc, argv, "clone <source> <destination>",
                          fs_manage_text(language, FS_MANAGE_HELP_CLONE))) {
        return 0;
    }
    if (argc < 3) {
        shell_print_error(fs_manage_text(language, FS_MANAGE_REQUIRE_SRC_DST));
        shell_suggest_help("clone");
        return -1;
    }
    char src_path[SHELL_PATH_BUFFER];
    char dst_path[SHELL_PATH_BUFFER];
    if (shell_resolve_path(ctx, argv[1], src_path, sizeof(src_path)) != 0) {
        shell_print_error(fs_manage_text(language, FS_MANAGE_SOURCE_INVALID));
        shell_suggest_help("clone");
        return -1;
    }
    shell_trim_trailing_slash(src_path);
    if (!shell_path_is_file(src_path)) {
        shell_print_error(fs_manage_text(language, FS_MANAGE_SOURCE_NOT_FILE));
        shell_suggest_help("clone");
        return -1;
    }
    if (shell_resolve_path(ctx, argv[2], dst_path, sizeof(dst_path)) != 0) {
        shell_print_error(fs_manage_text(language, FS_MANAGE_DEST_INVALID));
        shell_suggest_help("clone");
        return -1;
    }
    shell_trim_trailing_slash(dst_path);
    if (shell_path_is_dir(dst_path)) {
        char combined[SHELL_PATH_BUFFER];
        if (shell_join_path(dst_path, shell_basename(src_path), combined, sizeof(combined)) != 0) {
            shell_print_error(fs_manage_text(language, FS_MANAGE_DEST_TOO_LONG));
            shell_suggest_help("clone");
            return -1;
        }
        shell_copy(dst_path, sizeof(dst_path), combined);
    }
    if (shell_path_is_file(dst_path)) {
        shell_print_error(fs_manage_text(language, FS_MANAGE_DEST_EXISTS));
        shell_suggest_help("clone");
        return -1;
    }

    struct vfs_metadata meta;
    shell_fill_metadata(ctx, VFS_MODE_FILE, &meta);
    {
        int rc = vfs_create(dst_path, VFS_MODE_FILE, &meta);
        if (rc != 0) {
        fs_manage_print_vfs_error(
            language, fs_manage_text(language, FS_MANAGE_CANNOT_CREATE_DEST), rc);
        shell_suggest_help("clone");
        return -1;
        }
    }

    struct file *src = shell_open_file_read(src_path);
    struct file *dst = shell_open_file_write(dst_path);
    if (!src || !dst) {
        if (src) vfs_close(src);
        if (dst) vfs_close(dst);
        vfs_unlink(dst_path);
        fs_manage_print_vfs_error(language,
                                  fs_manage_text(language, FS_MANAGE_OPEN_FAILED),
                                  vfs_last_error());
        shell_suggest_help("clone");
        return -1;
    }

    int copy_res = shell_copy_stream(src, dst);
    vfs_close(src);
    vfs_close(dst);
    if (copy_res != 0) {
        vfs_unlink(dst_path);
        shell_print_error(fs_manage_text(language, FS_MANAGE_COPY_FAILED));
        shell_suggest_help("clone");
        return -1;
    }
    shell_print_ok(dst_path);
    return 0;
}

static int cmd_stats_file(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    (void)ctx;
    if (shell_handle_help(argc, argv, "stats-file <path>",
                          fs_manage_text(language, FS_MANAGE_HELP_STATS))) {
        return 0;
    }
    if (argc < 2) {
        shell_print_error(fs_manage_text(language, FS_MANAGE_REQUIRE_PATH));
        shell_suggest_help("stats-file");
        return -1;
    }
    char path[SHELL_PATH_BUFFER];
    if (shell_resolve_path(ctx, argv[1], path, sizeof(path)) != 0) {
        shell_print_error(fs_manage_text(language, FS_MANAGE_INVALID_PATH));
        shell_suggest_help("stats-file");
        return -1;
    }
    shell_trim_trailing_slash(path);
    struct vfs_stat st;
    {
        int rc = vfs_stat_path(path, &st);
        if (rc != 0) {
        fs_manage_print_vfs_error(language,
                                  fs_manage_text(language, FS_MANAGE_CANNOT_STAT), rc);
        shell_suggest_help("stats-file");
        return -1;
        }
    }
    shell_print(fs_manage_text(language, FS_MANAGE_TARGET_LABEL));
    shell_print(path);
    shell_newline();
    shell_print(fs_manage_text(language, FS_MANAGE_TYPE_LABEL));
    shell_print((st.mode & VFS_MODE_DIR)
                    ? fs_manage_text(language, FS_MANAGE_DIR_TYPE)
                    : fs_manage_text(language, FS_MANAGE_FILE_TYPE));
    shell_newline();
    shell_print(fs_manage_text(language, FS_MANAGE_SIZE_LABEL));
    shell_print_number(st.size);
    shell_print(fs_manage_text(language, FS_MANAGE_BYTES_SUFFIX));
    shell_print(fs_manage_text(language, FS_MANAGE_UID_LABEL));
    shell_print_number(st.uid);
    shell_print(fs_manage_text(language, FS_MANAGE_GID_LABEL));
    shell_print_number(st.gid);
    shell_newline();
    char perm_buf[5];
    shell_format_perm(st.perm, perm_buf);
    shell_print(fs_manage_text(language, FS_MANAGE_PERM_LABEL));
    shell_print(perm_buf);
    shell_newline();
    return 0;
}

static int cmd_type(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    (void)ctx;
    if (shell_handle_help(argc, argv, "type <path>",
                          fs_manage_text(language, FS_MANAGE_HELP_TYPE))) {
        return 0;
    }
    if (argc < 2) {
        shell_print_error(fs_manage_text(language, FS_MANAGE_REQUIRE_PATH));
        shell_suggest_help("type");
        return -1;
    }
    char path[SHELL_PATH_BUFFER];
    if (shell_resolve_path(ctx, argv[1], path, sizeof(path)) != 0) {
        shell_print_error(fs_manage_text(language, FS_MANAGE_INVALID_PATH));
        shell_suggest_help("type");
        return -1;
    }
    shell_trim_trailing_slash(path);
    struct vfs_stat st;
    {
        int rc = vfs_stat_path(path, &st);
        if (rc != 0) {
        fs_manage_print_vfs_error(language,
                                  fs_manage_text(language, FS_MANAGE_TARGET_MISSING), rc);
        return -1;
        }
    }
    shell_print(path);
    shell_print(fs_manage_text(language, FS_MANAGE_TYPE_SUFFIX));
    if (st.mode & VFS_MODE_DIR) {
        shell_print(fs_manage_text(language, FS_MANAGE_DIR_TYPE));
    } else if (st.mode & VFS_MODE_FILE) {
        shell_print(fs_manage_text(language, FS_MANAGE_FILE_TYPE));
    } else {
        shell_print(fs_manage_text(language, FS_MANAGE_UNKNOWN_TYPE));
    }
    shell_print(fs_manage_text(language, FS_MANAGE_PERM_SUFFIX));
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

static struct shell_command g_fs_manage_commands[10];
static int g_fs_manage_commands_initialized = 0;

static void init_fs_manage_commands(void) {
    if (g_fs_manage_commands_initialized) {
        return;
    }
    g_fs_manage_commands[0].name = "mk-file";
    g_fs_manage_commands[0].handler = cmd_mk_file;
    g_fs_manage_commands[1].name = "mk-dir";
    g_fs_manage_commands[1].handler = cmd_mk_dir;
    g_fs_manage_commands[2].name = "kill-file";
    g_fs_manage_commands[2].handler = cmd_kill_file;
    g_fs_manage_commands[3].name = "kill-dir";
    g_fs_manage_commands[3].handler = cmd_kill_dir;
    g_fs_manage_commands[4].name = "move";
    g_fs_manage_commands[4].handler = cmd_move;
    g_fs_manage_commands[5].name = "clone";
    g_fs_manage_commands[5].handler = cmd_clone;
    g_fs_manage_commands[6].name = "stats-file";
    g_fs_manage_commands[6].handler = cmd_stats_file;
    g_fs_manage_commands[7].name = "type";
    g_fs_manage_commands[7].handler = cmd_type;
    g_fs_manage_commands[8].name = "touch";
    g_fs_manage_commands[8].handler = cmd_mk_file;
    g_fs_manage_commands[9].name = "mkdir";
    g_fs_manage_commands[9].handler = cmd_mk_dir;
    g_fs_manage_commands_initialized = 1;
}

const struct shell_command *shell_commands_filesystem_manage(size_t *count) {
    init_fs_manage_commands();
    if (count) {
        *count = 10;
    }
    return g_fs_manage_commands;
}
