#include "shell/commands.h"
#include "shell/core.h"

#include "lang/localization.h"
#include "drivers/console/tty.h"
#include "memory/kmem.h"
#include "fs/vfs.h"

enum fs_content_text_id {
    FS_CONTENT_HELP_PRINT_FILE = 0,
    FS_CONTENT_HELP_PAGE,
    FS_CONTENT_HELP_PRINT_FILE_BEGIN,
    FS_CONTENT_HELP_PRINT_FILE_END,
    FS_CONTENT_HELP_PRINT_ECHO,
    FS_CONTENT_HELP_OPEN,
    FS_CONTENT_REQUIRE_FILE,
    FS_CONTENT_INVALID_PATH,
    FS_CONTENT_CANNOT_OPEN,
    FS_CONTENT_READ_FAILED,
    FS_CONTENT_CURRENT_CONTENT_HEADER,
    FS_CONTENT_CURRENT_CONTENT_FOOTER,
    FS_CONTENT_NEW_FILE_EMPTY,
    FS_CONTENT_CANNOT_CREATE_FILE,
    FS_CONTENT_CANNOT_OPEN_WRITE,
    FS_CONTENT_OPEN_INSTRUCTIONS,
    FS_CONTENT_FILE_SAVED,
};

static const char *fs_content_text(const char *language,
                                   enum fs_content_text_id id) {
    switch (id) {
    case FS_CONTENT_HELP_PRINT_FILE:
        return localization_select(
            language,
            "Mostra o conteudo completo de um arquivo de texto.",
            "Shows the full content of a text file.",
            "Muestra el contenido completo de un archivo de texto.");
    case FS_CONTENT_HELP_PAGE:
        return localization_select(
            language,
            "Exibe o arquivo de forma paginada.",
            "Shows the file with pagination.",
            "Muestra el archivo de forma paginada.");
    case FS_CONTENT_HELP_PRINT_FILE_BEGIN:
        return localization_select(
            language,
            "Mostra as primeiras linhas de um arquivo. Use -n para ajustar a quantidade.",
            "Shows the first lines of a file. Use -n to adjust the amount.",
            "Muestra las primeras lineas de un archivo. Usa -n para ajustar la cantidad.");
    case FS_CONTENT_HELP_PRINT_FILE_END:
        return localization_select(
            language,
            "Mostra as ultimas linhas de um arquivo. Use -n para ajustar a quantidade.",
            "Shows the last lines of a file. Use -n to adjust the amount.",
            "Muestra las ultimas lineas de un archivo. Usa -n para ajustar la cantidad.");
    case FS_CONTENT_HELP_PRINT_ECHO:
        return localization_select(language, "Repete os argumentos recebidos.",
                                   "Repeats the received arguments.",
                                   "Repite los argumentos recibidos.");
    case FS_CONTENT_HELP_OPEN:
        return localization_select(
            language,
            "Abre arquivo em modo edicao linha-a-linha. Termine com .wq para salvar e sair.",
            "Opens a file in line-by-line edit mode. Finish with .wq to save and exit.",
            "Abre un archivo en modo de edicion linea por linea. Termina con .wq para guardar y salir.");
    case FS_CONTENT_REQUIRE_FILE:
        return localization_select(language, "informe arquivo", "provide file",
                                   "indica archivo");
    case FS_CONTENT_INVALID_PATH:
        return localization_select(language, "caminho invalido", "invalid path",
                                   "ruta invalida");
    case FS_CONTENT_CANNOT_OPEN:
        return localization_select(language, "nao foi possivel abrir",
                                   "could not open file",
                                   "no fue posible abrir");
    case FS_CONTENT_READ_FAILED:
        return localization_select(language, "falha ao ler", "read failed",
                                   "fallo al leer");
    case FS_CONTENT_CURRENT_CONTENT_HEADER:
        return localization_select(language, "=== Conteudo atual ===\n",
                                   "=== Current content ===\n",
                                   "=== Contenido actual ===\n");
    case FS_CONTENT_CURRENT_CONTENT_FOOTER:
        return localization_select(language, "======================\n",
                                   "=======================\n",
                                   "======================\n");
    case FS_CONTENT_NEW_FILE_EMPTY:
        return localization_select(language, "Arquivo novo. Conteudo vazio.\n",
                                   "New file. Empty content.\n",
                                   "Archivo nuevo. Contenido vacio.\n");
    case FS_CONTENT_CANNOT_CREATE_FILE:
        return localization_select(language, "nao foi possivel criar arquivo",
                                   "could not create file",
                                   "no fue posible crear archivo");
    case FS_CONTENT_CANNOT_OPEN_WRITE:
        return localization_select(language,
                                   "nao foi possivel abrir para escrita",
                                   "could not open for writing",
                                   "no fue posible abrir para escritura");
    case FS_CONTENT_OPEN_INSTRUCTIONS:
        return localization_select(
            language,
            "Digite linhas; finalize com .wq isolado para salvar.\n",
            "Type lines; finish with .wq alone to save.\n",
            "Escribe lineas; finaliza con .wq solo para guardar.\n");
    case FS_CONTENT_FILE_SAVED:
    default:
        return localization_select(language, "arquivo salvo", "file saved",
                                   "archivo guardado");
    }
}

static int cmd_print_file(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    if (shell_handle_help(argc, argv, "print-file <file>",
                          fs_content_text(language, FS_CONTENT_HELP_PRINT_FILE))) {
        return 0;
    }
    if (argc < 2) {
        shell_print_error(fs_content_text(language, FS_CONTENT_REQUIRE_FILE));
        shell_suggest_help("print-file");
        return -1;
    }
    char path[SHELL_PATH_BUFFER];
    if (shell_resolve_path(ctx, argv[1], path, sizeof(path)) != 0) {
        shell_print_error(fs_content_text(language, FS_CONTENT_INVALID_PATH));
        shell_suggest_help("print-file");
        return -1;
    }
    shell_trim_trailing_slash(path);
    struct file *f = shell_open_file_read(path);
    if (!f) {
        shell_print_error(fs_content_text(language, FS_CONTENT_CANNOT_OPEN));
        shell_suggest_help("print-file");
        return -1;
    }
    int res = shell_stream_file(f);
    vfs_close(f);
    return res;
}

static int cmd_page(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    if (shell_handle_help(argc, argv, "page <file>",
                          fs_content_text(language, FS_CONTENT_HELP_PAGE))) {
        return 0;
    }
    return cmd_print_file(ctx, argc, argv);
}

static int cmd_print_file_begin(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    if (shell_handle_help(argc, argv, "print-file-begin <file> [-n <lines>]",
                          fs_content_text(language,
                                          FS_CONTENT_HELP_PRINT_FILE_BEGIN))) {
        return 0;
    }
    if (argc < 2) {
        shell_print_error(fs_content_text(language, FS_CONTENT_REQUIRE_FILE));
        shell_suggest_help("print-file-begin");
        return -1;
    }
    int lines = 10;
    if (argc > 2) {
        int idx = 2;
        if (shell_string_equal(argv[2], "-n")) {
            idx = 3;
        }
        if (idx < argc) {
            const char *opt = argv[idx];
            int value = 0;
            int valid = 1;
            for (size_t i = 0; opt[i]; ++i) {
                if (opt[i] < '0' || opt[i] > '9') {
                    valid = 0;
                    break;
                }
                value = value * 10 + (opt[i] - '0');
            }
            if (valid && value > 0) {
                lines = value;
            }
        }
    }
    char path[SHELL_PATH_BUFFER];
    if (shell_resolve_path(ctx, argv[1], path, sizeof(path)) != 0) {
        shell_print_error(fs_content_text(language, FS_CONTENT_INVALID_PATH));
        shell_suggest_help("print-file-begin");
        return -1;
    }
    shell_trim_trailing_slash(path);
    char *buffer = NULL;
    size_t len = 0;
    if (shell_read_file(path, &buffer, &len) != 0) {
        shell_print_error(fs_content_text(language, FS_CONTENT_READ_FAILED));
        return -1;
    }
    int count = 0;
    for (size_t i = 0; i < len; ++i) {
        char ch[2];
        ch[0] = buffer[i];
        ch[1] = '\0';
        shell_print(ch);
        if (buffer[i] == '\n') {
            if (++count >= lines) {
                break;
            }
        }
    }
    shell_newline();
    kfree(buffer);
    return 0;
}

static int cmd_print_file_end(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    if (shell_handle_help(argc, argv, "print-file-end <file> [-n <lines>]",
                          fs_content_text(language,
                                          FS_CONTENT_HELP_PRINT_FILE_END))) {
        return 0;
    }
    if (argc < 2) {
        shell_print_error(fs_content_text(language, FS_CONTENT_REQUIRE_FILE));
        shell_suggest_help("print-file-end");
        return -1;
    }
    int lines = 10;
    if (argc > 2) {
        int idx = 2;
        if (shell_string_equal(argv[2], "-n")) {
            idx = 3;
        }
        if (idx < argc) {
            const char *opt = argv[idx];
            int value = 0;
            int valid = 1;
            for (size_t i = 0; opt[i]; ++i) {
                if (opt[i] < '0' || opt[i] > '9') {
                    valid = 0;
                    break;
                }
                value = value * 10 + (opt[i] - '0');
            }
            if (valid && value > 0) {
                lines = value;
            }
        }
    }
    char path[SHELL_PATH_BUFFER];
    if (shell_resolve_path(ctx, argv[1], path, sizeof(path)) != 0) {
        shell_print_error(fs_content_text(language, FS_CONTENT_INVALID_PATH));
        shell_suggest_help("print-file-end");
        return -1;
    }
    shell_trim_trailing_slash(path);
    char *buffer = NULL;
    size_t len = 0;
    if (shell_read_file(path, &buffer, &len) != 0) {
        shell_print_error(fs_content_text(language, FS_CONTENT_READ_FAILED));
        return -1;
    }
    int newline_count = 0;
    for (size_t i = len; i > 0; --i) {
        if (buffer[i - 1] == '\n') {
            newline_count++;
            if (newline_count > lines) {
                buffer[i - 1] = '\0';
                shell_print(&buffer[i]);
                shell_newline();
                kfree(buffer);
                return 0;
            }
        }
    }
    shell_print(buffer);
    shell_newline();
    kfree(buffer);
    return 0;
}

static int cmd_print_echo(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    (void)ctx;
    if (shell_handle_help(argc, argv, "print-echo [text]",
                          fs_content_text(language, FS_CONTENT_HELP_PRINT_ECHO))) {
        return 0;
    }
    for (int i = 1; i < argc; ++i) {
        shell_print(argv[i]);
        if (i + 1 < argc) {
            shell_print(" ");
        }
    }
    shell_newline();
    return 0;
}

static int cmd_open(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    (void)ctx;
    if (shell_handle_help(argc, argv, "open <file>",
                          fs_content_text(language, FS_CONTENT_HELP_OPEN))) {
        return 0;
    }
    if (argc < 2) {
        shell_print_error(fs_content_text(language, FS_CONTENT_REQUIRE_FILE));
        shell_suggest_help("open");
        return -1;
    }
    char path[SHELL_PATH_BUFFER];
    if (shell_resolve_path(ctx, argv[1], path, sizeof(path)) != 0) {
        shell_print_error(fs_content_text(language, FS_CONTENT_INVALID_PATH));
        shell_suggest_help("open");
        return -1;
    }
    shell_trim_trailing_slash(path);

    char *buffer = NULL;
    size_t len = 0;
    if (shell_read_file(path, &buffer, &len) == 0 && buffer) {
        shell_print(fs_content_text(language, FS_CONTENT_CURRENT_CONTENT_HEADER));
        shell_print(buffer);
        shell_newline();
        shell_print(fs_content_text(language, FS_CONTENT_CURRENT_CONTENT_FOOTER));
        kfree(buffer);
    } else {
        shell_print(fs_content_text(language, FS_CONTENT_NEW_FILE_EMPTY));
    }

    vfs_unlink(path);
    if (vfs_create(path, VFS_MODE_FILE, NULL) != 0) {
        shell_print_error(
            fs_content_text(language, FS_CONTENT_CANNOT_CREATE_FILE));
        return -1;
    }
    struct file *f = shell_open_file_write(path);
    if (!f) {
        shell_print_error(fs_content_text(language, FS_CONTENT_CANNOT_OPEN_WRITE));
        return -1;
    }

    shell_print(fs_content_text(language, FS_CONTENT_OPEN_INSTRUCTIONS));
    char line[TTY_BUFFER_MAX];
    while (1) {
        tty_set_prompt("open> ");
        tty_set_echo(1);
        tty_show_prompt();
        size_t l = tty_readline(line, sizeof(line));
        if (l == 0) {
            const char nl = '\n';
            vfs_write(f, &nl, 1);
            continue;
        }
        if (l == 3 && line[0] == '.' && line[1] == 'w' && line[2] == 'q') {
            break;
        }
        vfs_write(f, line, l);
        const char nl = '\n';
        vfs_write(f, &nl, 1);
    }
    vfs_close(f);
    shell_print_ok(fs_content_text(language, FS_CONTENT_FILE_SAVED));
    return 0;
}

static struct shell_command g_fs_content_commands[6];
static int g_fs_content_commands_initialized = 0;

static void init_fs_content_commands(void) {
    if (g_fs_content_commands_initialized) {
        return;
    }
    g_fs_content_commands[0].name = "print-file";
    g_fs_content_commands[0].handler = cmd_print_file;
    g_fs_content_commands[1].name = "page";
    g_fs_content_commands[1].handler = cmd_page;
    g_fs_content_commands[2].name = "print-file-begin";
    g_fs_content_commands[2].handler = cmd_print_file_begin;
    g_fs_content_commands[3].name = "print-file-end";
    g_fs_content_commands[3].handler = cmd_print_file_end;
    g_fs_content_commands[4].name = "open";
    g_fs_content_commands[4].handler = cmd_open;
    g_fs_content_commands[5].name = "print-echo";
    g_fs_content_commands[5].handler = cmd_print_echo;
    g_fs_content_commands_initialized = 1;
}

const struct shell_command *shell_commands_filesystem_content(size_t *count) {
    init_fs_content_commands();
    if (count) {
        *count = 6;
    }
    return g_fs_content_commands;
}
