#include "shell/commands.h"
#include "shell/core.h"

#include "drivers/video/vga.h"
#include "drivers/console/tty.h"
#include "memory/kmem.h"
#include "fs/vfs.h"

static int cmd_print_file(struct shell_context *ctx, int argc, char **argv) {
    if (shell_handle_help(argc, argv, "print-file <arquivo>",
                          "Mostra o conteudo completo de um arquivo de texto.")) {
        return 0;
    }
    if (argc < 2) {
        shell_print_error("informe arquivo");
        shell_suggest_help("print-file");
        return -1;
    }
    char path[SHELL_PATH_BUFFER];
    if (shell_resolve_path(ctx, argv[1], path, sizeof(path)) != 0) {
        shell_print_error("caminho invalido");
        shell_suggest_help("print-file");
        return -1;
    }
    shell_trim_trailing_slash(path);
    struct file *f = shell_open_file_read(path);
    if (!f) {
        shell_print_error("nao foi possivel abrir");
        shell_suggest_help("print-file");
        return -1;
    }
    int res = shell_stream_file(f);
    vfs_close(f);
    return res;
}

static int cmd_page(struct shell_context *ctx, int argc, char **argv) {
    if (shell_handle_help(argc, argv, "page <arquivo>",
                          "Exibe o arquivo de forma paginada.")) {
        return 0;
    }
    return cmd_print_file(ctx, argc, argv);
}

static int cmd_print_file_begin(struct shell_context *ctx, int argc, char **argv) {
    if (shell_handle_help(argc, argv, "print-file-begin <arquivo> [-n <linhas>]",
                          "Mostra as primeiras linhas de um arquivo. Use -n para ajustar a quantidade.")) {
        return 0;
    }
    if (argc < 2) {
        shell_print_error("informe arquivo");
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
        shell_print_error("caminho invalido");
        shell_suggest_help("print-file-begin");
        return -1;
    }
    shell_trim_trailing_slash(path);
    char *buffer = NULL;
    size_t len = 0;
    if (shell_read_file(path, &buffer, &len) != 0) {
        shell_print_error("falha ao ler");
        return -1;
    }
    int count = 0;
    for (size_t i = 0; i < len; ++i) {
        vga_putc(buffer[i]);
        if (buffer[i] == '\n') {
            if (++count >= lines) {
                break;
            }
        }
    }
    vga_newline();
    kfree(buffer);
    return 0;
}

static int cmd_print_file_end(struct shell_context *ctx, int argc, char **argv) {
    if (shell_handle_help(argc, argv, "print-file-end <arquivo> [-n <linhas>]",
                          "Mostra as ultimas linhas de um arquivo. Use -n para ajustar a quantidade.")) {
        return 0;
    }
    if (argc < 2) {
        shell_print_error("informe arquivo");
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
        shell_print_error("caminho invalido");
        shell_suggest_help("print-file-end");
        return -1;
    }
    shell_trim_trailing_slash(path);
    char *buffer = NULL;
    size_t len = 0;
    if (shell_read_file(path, &buffer, &len) != 0) {
        shell_print_error("falha ao ler");
        return -1;
    }
    int newline_count = 0;
    for (size_t i = len; i > 0; --i) {
        if (buffer[i - 1] == '\n') {
            newline_count++;
            if (newline_count > lines) {
                buffer[i - 1] = '\0';
                shell_print(&buffer[i]);
                vga_newline();
                kfree(buffer);
                return 0;
            }
        }
    }
    shell_print(buffer);
    vga_newline();
    kfree(buffer);
    return 0;
}

static int cmd_print_echo(struct shell_context *ctx, int argc, char **argv) {
    (void)ctx;
    if (shell_handle_help(argc, argv, "print-echo [texto]",
                          "Repete os argumentos recebidos.")) {
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

// Editor de linha simples: reescreve o arquivo com linhas digitadas até o usuário enviar ".wq"
static int cmd_open(struct shell_context *ctx, int argc, char **argv) {
    (void)ctx;
    if (shell_handle_help(argc, argv, "open <arquivo>",
                          "Abre arquivo em modo edicao linha-a-linha. Termine com .wq para salvar e sair.")) {
        return 0;
    }
    if (argc < 2) {
        shell_print_error("informe arquivo");
        shell_suggest_help("open");
        return -1;
    }
    char path[SHELL_PATH_BUFFER];
    if (shell_resolve_path(ctx, argv[1], path, sizeof(path)) != 0) {
        shell_print_error("caminho invalido");
        shell_suggest_help("open");
        return -1;
    }
    shell_trim_trailing_slash(path);

    // Mostra conteudo atual (se existir)
    char *buffer = NULL;
    size_t len = 0;
    if (shell_read_file(path, &buffer, &len) == 0 && buffer) {
        shell_print("=== Conteudo atual ===\n");
        vga_write(buffer);
        vga_newline();
        shell_print("======================\n");
        kfree(buffer);
    } else {
        shell_print("Arquivo novo. Conteudo vazio.\n");
    }

    // Recria o arquivo para garantir truncamento
    vfs_unlink(path);
    if (vfs_create(path, VFS_MODE_FILE, NULL) != 0) {
        shell_print_error("nao foi possivel criar arquivo");
        return -1;
    }
    struct file *f = shell_open_file_write(path);
    if (!f) {
        shell_print_error("nao foi possivel abrir para escrita");
        return -1;
    }

    shell_print("Digite linhas; finalize com .wq isolado para salvar.\n");
    char line[TTY_BUFFER_MAX];
    while (1) {
        tty_set_prompt("open> ");
        tty_set_echo(1);
        tty_show_prompt();
        size_t l = tty_readline(line, sizeof(line));
        if (l == 0) {
            // linha vazia -> apenas grava newline
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
    shell_print_ok("arquivo salvo");
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
