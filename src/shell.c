#include "shell.h"

#include "buffer.h"
#include "kmem.h"
#include "pit.h"
#include "tty.h"
#include "user.h"
#include "vfs.h"
#include "vga.h"
#include "keyboard.h"

#include <stdint.h>

#define SHELL_MAX_ARGS 16
#define SHELL_PATH_BUFFER 128
#define SHELL_READ_CHUNK 128
#define SHELL_HUNT_MAX_DEPTH 16

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

static int hunt_callback(const char *name, uint16_t mode, void *userdata);
static int shell_hunt_walk(struct shell_context *ctx, const char *path, const char *pattern, enum hunt_target target, int depth, int *matches);
static int shell_search_text(struct shell_context *ctx, const char *path, const char *pattern, int depth, int *matches);
static int read_file_into_memory(const char *path, char **out_buf, size_t *out_len);
static void print_number(uint32_t value);

struct search_ctx {
    struct shell_context *shell;
    const char *pattern;
    int depth;
    int *matches;
    char current[SHELL_PATH_BUFFER];
    int error;
};

static int search_callback(const char *name, uint16_t mode, void *userdata);

struct shell_context {
    struct session_context *session;
    const struct system_settings *settings;
    int running;
    int logout;
};

struct shell_cmd {
    const char *name;
    int (*handler)(struct shell_context *ctx, int argc, char **argv);
};

static void shell_print(const char *text);
static void shell_newline(void);

static int strings_equal(const char *a, const char *b) {
    if (!a || !b) {
        return 0;
    }
    size_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return 0;
        }
        ++i;
    }
    return a[i] == b[i];
}

static size_t cstring_length(const char *s) {
    size_t len = 0;
    if (!s) {
        return 0;
    }
    while (s[len]) {
        ++len;
    }
    return len;
}

static void shell_fill_metadata(struct shell_context *ctx, uint16_t mode, struct vfs_metadata *meta) {
    const struct user_record *user = ctx && ctx->session ? session_user(ctx->session) : NULL;
    if (user) {
        meta->uid = user->uid;
        meta->gid = user->gid;
    } else {
        meta->uid = 0;
        meta->gid = 0;
    }
    meta->perm = (mode & VFS_MODE_DIR) ? 0755 : 0644;
}

static int command_help_requested(int argc, char **argv) {
    return (argc == 2 && strings_equal(argv[1], "-help"));
}

static int handle_help(int argc, char **argv, const char *usage, const char *details) {
    if (!command_help_requested(argc, argv)) {
        return 0;
    }
    shell_print("Uso: ");
    shell_print(usage);
    shell_newline();
    if (details) {
        shell_print(details);
        shell_newline();
    }
    return 1;
}

static void suggest_help(const char *cmd) {
    shell_print("Use ");
    shell_print(cmd);
    shell_print(" -help para detalhes.\n");
}

static int shell_get_stat(const char *path, struct vfs_stat *st) {
    return vfs_stat_path(path, st);
}

static void shell_copy(char *dst, size_t dst_len, const char *src) {
    if (!dst || dst_len == 0) {
        return;
    }
    size_t i = 0;
    if (src) {
        while (src[i] && i < dst_len - 1) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = '\0';
}

static int shell_path_is_dir(const char *path) {
    struct vfs_stat st;
    if (shell_get_stat(path, &st) != 0) {
        return 0;
    }
    return (st.mode & VFS_MODE_DIR) != 0;
}

static int shell_path_is_file(const char *path) {
    struct vfs_stat st;
    if (shell_get_stat(path, &st) != 0) {
        return 0;
    }
    return (st.mode & VFS_MODE_FILE) != 0;
}

static void trim_trailing_slash(char *path) {
    size_t len = cstring_length(path);
    while (len > 1 && path[len - 1] == '/') {
        path[--len] = '\0';
    }
}

static int shell_dir_recursive(struct shell_context *ctx, const char *input) {
    char abs_path[SHELL_PATH_BUFFER];
    if (session_resolve_path(ctx->session, input, abs_path, sizeof(abs_path)) != 0) {
        return -1;
    }
    trim_trailing_slash(abs_path);
    if (cstring_length(abs_path) == 0) {
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

static void shell_format_perm(uint16_t perm, char out[5]) {
    out[0] = (char)('0' + ((perm >> 6) & 0x7));
    out[1] = (char)('0' + ((perm >> 3) & 0x7));
    out[2] = (char)('0' + (perm & 0x7));
    out[3] = '\0';
    out[4] = '\0';
}

static const char *shell_basename(const char *path) {
    if (!path) {
        return "";
    }
    size_t len = cstring_length(path);
    while (len > 0 && path[len - 1] == '/') {
        len--;
    }
    const char *last = path;
    for (size_t i = 0; i < len; ++i) {
        if (path[i] == '/' && i + 1 < len) {
            last = &path[i + 1];
        }
    }
    return last;
}

static int shell_join_path(const char *dir, const char *name, char *out, size_t out_len) {
    size_t pos = 0;
    if (!dir || dir[0] == '\0') {
        dir = "/";
    }
    size_t dir_len = cstring_length(dir);
    if (dir_len >= out_len) {
        return -1;
    }
    for (size_t i = 0; i < dir_len; ++i) {
        out[pos++] = dir[i];
    }
    if (pos > 1 && out[pos - 1] != '/') {
        if (pos >= out_len) {
            return -1;
        }
        out[pos++] = '/';
    }
    const char *base = name ? name : "";
    size_t name_len = cstring_length(base);
    if (pos + name_len >= out_len) {
        return -1;
    }
    for (size_t i = 0; i < name_len; ++i) {
        out[pos++] = base[i];
    }
    if (pos == 0) {
        out[pos++] = '/';
    }
    out[pos] = '\0';
    return 0;
}

static int shell_str_contains(const char *haystack, const char *needle) {
    if (!needle || !needle[0]) {
        return 1;
    }
    if (!haystack) {
        return 0;
    }
    size_t hlen = cstring_length(haystack);
    size_t nlen = cstring_length(needle);
    if (nlen > hlen) {
        return 0;
    }
    for (size_t i = 0; i + nlen <= hlen; ++i) {
        size_t j = 0;
        while (j < nlen && haystack[i + j] == needle[j]) {
            ++j;
        }
        if (j == nlen) {
            return 1;
        }
    }
    return 0;
}

static void shell_update_prompt(struct shell_context *ctx) {
    const struct user_record *user = session_user(ctx->session);
    const char *username = user ? user->username : "user";
    const char *hostname = ctx->settings ? ctx->settings->hostname : "noiros";
    char prompt[TTY_PROMPT_MAX];
    size_t idx = 0;
    const char *parts[] = { username, "@", hostname, "> " };
    for (size_t i = 0; i < sizeof(parts)/sizeof(parts[0]); ++i) {
        const char *p = parts[i];
        size_t plen = cstring_length(p);
        for (size_t j = 0; j < plen && idx < sizeof(prompt) - 1; ++j) {
            prompt[idx++] = p[j];
        }
    }
    prompt[idx] = '\0';
    tty_set_prompt(prompt);
}

static int shell_parse(char *line, char **argv, int max_args) {
    int argc = 0;
    char *p = line;
    while (*p) {
        while (*p == ' ' || *p == '\t') {
            ++p;
        }
        if (*p == '\0') {
            break;
        }
        if (argc >= max_args) {
            break;
        }

        char quote = 0;
        if (*p == '"' || *p == '\'') {
            quote = *p;
            ++p;
        }
        char *start = p;
        char *dest = p;
        while (*p) {
            char c = *p;
            if (quote) {
                if (c == '\\' && quote == '"' && p[1] != '\0') {
                    ++p;
                    c = *p;
                } else if (c == quote) {
                    ++p;
                    break;
                }
            } else {
                if (c == ' ' || c == '\t') {
                    ++p;
                    break;
                }
            }
            *dest++ = c;
            ++p;
        }
        *dest = '\0';
        argv[argc++] = start;
    }
    return argc;
}

static void shell_print(const char *text) {
    if (text) {
        vga_write(text);
    }
}

static void shell_newline(void) {
    vga_newline();
}

static int resolve_path(struct shell_context *ctx, const char *input, char *out, size_t out_len) {
    if (!input || !input[0]) {
        return session_resolve_path(ctx->session, ".", out, out_len);
    }
    return session_resolve_path(ctx->session, input, out, out_len);
}

static int cmd_list(struct shell_context *ctx, int argc, char **argv);
static int cmd_go(struct shell_context *ctx, int argc, char **argv);
static int cmd_mypath(struct shell_context *ctx, int argc, char **argv);
static int cmd_print_file(struct shell_context *ctx, int argc, char **argv);
static int cmd_page(struct shell_context *ctx, int argc, char **argv);
static int cmd_print_file_begin(struct shell_context *ctx, int argc, char **argv);
static int cmd_print_file_end(struct shell_context *ctx, int argc, char **argv);
static int cmd_mk_file(struct shell_context *ctx, int argc, char **argv);
static int cmd_mk_dir(struct shell_context *ctx, int argc, char **argv);
static int cmd_print_echo(struct shell_context *ctx, int argc, char **argv);
static int cmd_help_any(struct shell_context *ctx, int argc, char **argv);
static int cmd_help_docs(struct shell_context *ctx, int argc, char **argv);
static int cmd_mess(struct shell_context *ctx, int argc, char **argv);
static int cmd_bye(struct shell_context *ctx, int argc, char **argv);
static int cmd_config_keyboard(struct shell_context *ctx, int argc, char **argv);
static int cmd_print_me(struct shell_context *ctx, int argc, char **argv);
static int cmd_print_id(struct shell_context *ctx, int argc, char **argv);
static int cmd_print_host(struct shell_context *ctx, int argc, char **argv);
static int cmd_print_version(struct shell_context *ctx, int argc, char **argv);
static int cmd_print_time(struct shell_context *ctx, int argc, char **argv);
static int cmd_print_envs(struct shell_context *ctx, int argc, char **argv);
static int cmd_do_sync(struct shell_context *ctx, int argc, char **argv);
static int cmd_print_insomnia(struct shell_context *ctx, int argc, char **argv);
static int cmd_kill_file(struct shell_context *ctx, int argc, char **argv);
static int cmd_kill_dir(struct shell_context *ctx, int argc, char **argv);
static int cmd_move(struct shell_context *ctx, int argc, char **argv);
static int cmd_clone(struct shell_context *ctx, int argc, char **argv);
static int cmd_hunt_file(struct shell_context *ctx, int argc, char **argv);
static int cmd_hunt_dir(struct shell_context *ctx, int argc, char **argv);
static int cmd_hunt_any(struct shell_context *ctx, int argc, char **argv);
static int cmd_find(struct shell_context *ctx, int argc, char **argv);
static int cmd_stats_file(struct shell_context *ctx, int argc, char **argv);
static int cmd_type(struct shell_context *ctx, int argc, char **argv);

static const struct shell_cmd g_commands[] = {
    { "list", cmd_list },
    { "go", cmd_go },
    { "mypath", cmd_mypath },
    { "print-file", cmd_print_file },
    { "page", cmd_page },
    { "print-file-begin", cmd_print_file_begin },
    { "print-file-end", cmd_print_file_end },
    { "mk-file", cmd_mk_file },
    { "mk-dir", cmd_mk_dir },
    { "print-echo", cmd_print_echo },
    { "config-keyboard", cmd_config_keyboard },
    { "help-any", cmd_help_any },
    { "help-docs", cmd_help_docs },
    { "mess", cmd_mess },
    { "bye", cmd_bye },
    { "print-me", cmd_print_me },
    { "print-id", cmd_print_id },
    { "print-host", cmd_print_host },
    { "print-version", cmd_print_version },
    { "print-time", cmd_print_time },
    { "print-envs", cmd_print_envs },
    { "do-sync", cmd_do_sync },
    { "print-insomnia", cmd_print_insomnia },
    { "kill-file", cmd_kill_file },
    { "kill-dir", cmd_kill_dir },
    { "move", cmd_move },
    { "clone", cmd_clone },
    { "hunt-file", cmd_hunt_file },
    { "hunt-dir", cmd_hunt_dir },
    { "hunt-any", cmd_hunt_any },
    { "find", cmd_find },
    { "stats-file", cmd_stats_file },
    { "type", cmd_type },
};

static int diag_log_append(const char *msg);
static void buffer_append_local(char *dst, size_t dst_size, const char *src);
static void cli_log_process_begin(const char *name);
static void cli_log_process_begin_success(const char *name);
static void cli_log_process_progress(const char *name);
static void cli_log_process_conclude(const char *name);
static void cli_log_process_finalize(const char *name);
static void cli_log_process_finalize_success(const char *name);
static void cli_log_process_error(const char *name, const char *reason);
static void cli_log_dependency_wait(const char *dependency, const char *target);

static void print_error(const char *msg) {
    vga_write("[erro] ");
    if (msg) {
        vga_write(msg);
    }
    vga_newline();
}

static void print_ok(const char *msg) {
    vga_write("[ok] ");
    if (msg) {
        vga_write(msg);
    }
    vga_newline();
}

static void buffer_append_local(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0 || !src) {
        return;
    }
    size_t idx = cstring_length(dst);
    size_t sidx = 0;
    while (src[sidx] && idx < dst_size - 1) {
        dst[idx++] = src[sidx++];
    }
    dst[idx] = '\0';
}

static int path_exists(const char *path) {
    struct dentry *d = NULL;
    if (vfs_lookup(path, &d) != 0) {
        return 0;
    }
    if (d && d->refcount) {
        d->refcount--;
    }
    return 1;
}

static int ensure_log_dir(void) {
    static int prepared = 0;
    if (prepared) {
        return 0;
    }
    if (!path_exists("/var")) {
        if (vfs_create("/var", VFS_MODE_DIR, NULL) != 0) {
            return -1;
        }
    }
    if (!path_exists("/var/log")) {
        if (vfs_create("/var/log", VFS_MODE_DIR, NULL) != 0) {
            return -1;
        }
    }
    prepared = 1;
    return 0;
}

static int diag_log_append(const char *msg) {
    if (!msg) {
        return -1;
    }
    if (ensure_log_dir() != 0) {
        return -1;
    }
    struct file *logf = vfs_open("/var/log/cli-selftest.log", VFS_OPEN_WRITE);
    if (!logf) {
        if (vfs_create("/var/log/cli-selftest.log", VFS_MODE_FILE, NULL) != 0) {
            return -1;
        }
        logf = vfs_open("/var/log/cli-selftest.log", VFS_OPEN_WRITE);
        if (!logf) {
            return -1;
        }
    }
    if (logf->dentry && logf->dentry->inode) {
        logf->position = logf->dentry->inode->size;
    }
    size_t len = cstring_length(msg);
    if (len > 0) {
        if (vfs_write(logf, msg, len) < 0) {
            vfs_close(logf);
            return -1;
        }
    }
    const char newline = '\n';
    vfs_write(logf, &newline, 1);
    vfs_close(logf);
    return 0;
}

static void cli_log_process_begin(const char *name) {
    vga_write("Iniciando processo de ");
    vga_write(name);
    vga_write("...");
    vga_newline();
}

static void cli_log_process_begin_success(const char *name) {
    vga_write("Processo ");
    vga_write(name);
    vga_write(" iniciado com sucesso...");
    vga_newline();
}

static void cli_log_process_progress(const char *name) {
    vga_write("Processo ");
    vga_write(name);
    vga_write(" em andamento.");
    vga_newline();
}

static void cli_log_process_conclude(const char *name) {
    vga_write("Processo ");
    vga_write(name);
    vga_write(" concluido.");
    vga_newline();
}

static void cli_log_process_finalize(const char *name) {
    vga_write("Finalizando processo ");
    vga_write(name);
    vga_write("...");
    vga_newline();
}

static void cli_log_process_finalize_success(const char *name) {
    vga_write("Processo ");
    vga_write(name);
    vga_write(" finalizado com sucesso.");
    vga_newline();
}

static void cli_log_process_error(const char *name, const char *reason) {
    vga_write("[erro] Processo ");
    vga_write(name);
    vga_write(" falhou");
    if (reason) {
        vga_write(": ");
        vga_write(reason);
    }
    vga_write(".");
    vga_newline();
}

static void cli_log_dependency_wait(const char *dependency, const char *target) {
    vga_write("Aguardando processo ");
    vga_write(dependency);
    vga_write(" para iniciar o processo ");
    vga_write(target);
    vga_write("...");
    vga_newline();
}

static int shell_self_test(struct shell_context *ctx) {
    const char *proc = "auto teste do NoirCLI";
    cli_log_process_begin(proc);
    cli_log_process_begin_success(proc);
    if (!ctx || !ctx->session) {
        cli_log_process_error(proc, "sessao CLI invalida.");
        cli_log_process_finalize(proc);
        return -1;
    }

    vga_write("Iniciando CLI...\n");

    const struct user_record *user = session_user(ctx->session);
    if (!user) {
        cli_log_process_error(proc, "nenhum usuario autenticado.");
        cli_log_process_finalize(proc);
        return -1;
    }

    cli_log_process_progress(proc);
    struct vfs_stat st;
    if (vfs_stat_path("/", &st) != 0 || (st.mode & VFS_MODE_DIR) == 0) {
        cli_log_process_error(proc, "sistema de arquivos raiz indisponivel.");
        cli_log_process_finalize(proc);
        return -1;
    }

    const char *home = user->home[0] ? user->home : "/";
    if (vfs_stat_path(home, &st) != 0 || (st.mode & VFS_MODE_DIR) == 0) {
        cli_log_process_error(proc, "diretorio pessoal inacessivel.");
        cli_log_process_finalize(proc);
        return -1;
    }

    vga_write("CLI iniciado com sucesso.\n");
    vga_write("Hello ");
    if (user->username[0]) {
        vga_write(user->username);
    } else {
        vga_write("usuario");
    }
    vga_newline();
    cli_log_process_conclude(proc);
    cli_log_process_finalize(proc);
    cli_log_process_finalize_success(proc);
    return 0;
}

static int run_command_by_name(struct shell_context *ctx, const char *name, int argc, char **argv) {
    for (size_t i = 0; i < sizeof(g_commands)/sizeof(g_commands[0]); ++i) {
        if (strings_equal(name, g_commands[i].name)) {
            return g_commands[i].handler(ctx, argc, argv);
        }
    }
    return -1;
}

static int shell_run_diagnostics(struct shell_context *ctx) {
    if (!ctx) {
        return -1;
    }
    const char *proc = "diagnostico de comandos basicos do NoirCLI";
    cli_log_process_begin(proc);
    cli_log_process_begin_success(proc);
    diag_log_append("[diag] CLI self-test iniciado");

    char cmd_list[] = "list";
    char path_root[] = "/";
    char cmd_print_host[] = "print-host";
    char cmd_print_me[] = "print-me";
    char cmd_print_id[] = "print-id";
    char cmd_print_time[] = "print-time";
    char cmd_help_any[] = "help-any";

    struct diag_case {
        const char *cmd;
        int argc;
        char *argv[3];
    } tests[] = {
        { "list", 2, { cmd_list, path_root, NULL } },
        { "print-host", 1, { cmd_print_host, NULL, NULL } },
        { "print-me", 1, { cmd_print_me, NULL, NULL } },
        { "print-id", 1, { cmd_print_id, NULL, NULL } },
        { "print-time", 1, { cmd_print_time, NULL, NULL } },
        { "help-any", 1, { cmd_help_any, NULL, NULL } },
    };

    cli_log_process_progress(proc);
    for (size_t i = 0; i < sizeof(tests)/sizeof(tests[0]); ++i) {
        if (run_command_by_name(ctx, tests[i].cmd, tests[i].argc, tests[i].argv) != 0) {
            char fail_msg[128];
            fail_msg[0] = '\0';
            buffer_append_local(fail_msg, sizeof(fail_msg), "[diag] falha ao executar ");
            buffer_append_local(fail_msg, sizeof(fail_msg), tests[i].cmd);
            diag_log_append(fail_msg);
            cli_log_process_error(proc, "falha ao executar comando diagnostico.");
            cli_log_process_finalize(proc);
            return -1;
        }
    }

    diag_log_append("[diag] CLI self-test concluido com sucesso");
    cli_log_process_conclude(proc);
    cli_log_process_finalize(proc);
    cli_log_process_finalize_success(proc);
    return 0;
}

enum shell_result shell_run(struct session_context *session, const struct system_settings *settings) {
    if (!session) {
        return SHELL_RESULT_EXIT;
    }
    struct shell_context ctx;
    ctx.session = session;
    ctx.settings = settings;
    ctx.running = 1;
    ctx.logout = 0;

    char line[TTY_BUFFER_MAX];
    char *argv[SHELL_MAX_ARGS];

    session_set_active(session);

    if (shell_self_test(&ctx) != 0) {
        session_set_active(NULL);
        return SHELL_RESULT_EXIT;
    }

    cli_log_dependency_wait("auto teste do NoirCLI", "diagnostico de comandos basicos do NoirCLI");
    if (shell_run_diagnostics(&ctx) != 0) {
        print_error("Processo diagnostico de comandos basicos do NoirCLI finalizado com erro (veja /var/log/cli-selftest.log)");
    } else {
        print_ok("Processo diagnostico de comandos basicos do NoirCLI finalizado com sucesso.");
    }

    while (ctx.running) {
        shell_update_prompt(&ctx);
        tty_set_echo(1);
        tty_set_echo_mask('\0');
        tty_show_prompt();
        size_t len = tty_readline(line, sizeof(line));
        if (len == 0) {
            continue;
        }
        int argc = shell_parse(line, argv, SHELL_MAX_ARGS);
        if (argc == 0) {
            continue;
        }
        int handled = 0;
        for (size_t i = 0; i < sizeof(g_commands)/sizeof(g_commands[0]); ++i) {
            if (cstring_length(argv[0]) == cstring_length(g_commands[i].name)) {
                const char *cmd_name = g_commands[i].name;
                const char *input = argv[0];
                size_t idx = 0;
                while (cmd_name[idx] && input[idx] && cmd_name[idx] == input[idx]) {
                    ++idx;
                }
                if (cmd_name[idx] == '\0' && input[idx] == '\0') {
                    handled = 1;
                    if (g_commands[i].handler(&ctx, argc, argv) != 0) {
                        print_error("comando falhou");
                        suggest_help(cmd_name);
                    }
                    break;
                }
            }
        }
        if (!handled) {
            print_error("comando desconhecido");
            shell_print("Use help-any para listar comandos.\n");
        }
    }
    enum shell_result result = ctx.logout ? SHELL_RESULT_LOGOUT : SHELL_RESULT_EXIT;
    session_set_active(NULL);
    return result;
}

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
    if (handle_help(argc, argv, "list [caminho]",
                    "Lista os itens do diretorio atual ou do caminho informado.")) {
        return 0;
    }
    const char *target = (argc > 1) ? argv[1] : ".";
    char path[SHELL_PATH_BUFFER];
    if (resolve_path(ctx, target, path, sizeof(path)) != 0) {
        print_error("caminho invalido");
        suggest_help("list");
        return -1;
    }
    struct list_ctx lctx;
    lctx.count = 0;
    if (vfs_listdir(path, list_callback, &lctx) != 0) {
        print_error("nao foi possivel listar");
        suggest_help("list");
        return -1;
    }
    if (lctx.count == 0) {
        shell_print("(vazio)\n");
    }
    return 0;
}

static int cmd_go(struct shell_context *ctx, int argc, char **argv) {
    if (handle_help(argc, argv, "go <caminho>",
                    "Altera o diretorio atual da sessao.")) {
        return 0;
    }
    if (argc < 2) {
        print_error("informe destino");
        suggest_help("go");
        return -1;
    }
    char path[SHELL_PATH_BUFFER];
    if (session_resolve_path(ctx->session, argv[1], path, sizeof(path)) != 0) {
        print_error("caminho invalido");
        suggest_help("go");
        return -1;
    }
    trim_trailing_slash(path);
    if (shell_path_is_file(path)) {
        print_error("destino eh um arquivo");
        return -1;
    }
    if (session_set_cwd(ctx->session, path) != 0) {
        print_error("nao foi possivel mudar");
        suggest_help("go");
        return -1;
    }
    print_ok(path);
    return 0;
}

static int cmd_mypath(struct shell_context *ctx, int argc, char **argv) {
    if (command_help_requested(argc, argv)) {
        shell_print("Uso: mypath\nExibe o caminho atual da sessao.\n");
        return 0;
    }
    (void)argc; (void)argv;
    shell_print(session_cwd(ctx->session));
    shell_newline();
    return 0;
}

static struct file *open_file_read(const char *path) {
    return vfs_open(path, 0);
}

static struct file *open_file_write(const char *path) {
    return vfs_open(path, VFS_OPEN_WRITE);
}

static int stream_file(struct file *file) {
    if (!file) {
        return -1;
    }
    char buffer[SHELL_READ_CHUNK + 1];
    long read;
    while ((read = vfs_read(file, buffer, SHELL_READ_CHUNK)) > 0) {
        buffer[read] = '\0';
        vga_write(buffer);
    }
    vga_newline();
    return 0;
}

static int shell_copy_stream(struct file *src, struct file *dst) {
    if (!src || !dst) {
        return -1;
    }
    char buffer[SHELL_READ_CHUNK];
    long read_bytes;
    while ((read_bytes = vfs_read(src, buffer, sizeof(buffer))) > 0) {
        const char *ptr = buffer;
        long remaining = read_bytes;
        while (remaining > 0) {
            long written = vfs_write(dst, ptr, (size_t)remaining);
            if (written <= 0) {
                return -1;
            }
            ptr += written;
            remaining -= written;
        }
    }
    return (read_bytes < 0) ? -1 : 0;
}

static int hunt_callback(const char *name, uint16_t mode, void *userdata) {
    struct hunt_ctx *state = (struct hunt_ctx *)userdata;
    if (!state || !name) {
        return 0;
    }
    if ((name[0] == '.' && name[1] == '\0') || (name[0] == '.' && name[1] == '.' && name[2] == '\0')) {
        return 0;
    }
    char child[SHELL_PATH_BUFFER];
    if (shell_join_path(state->current, name, child, sizeof(child)) != 0) {
        state->error = 1;
        return -1;
    }
    int is_dir = (mode & VFS_MODE_DIR) != 0;
    int matches_type = 0;
    switch (state->target) {
        case HUNT_FILES:
            matches_type = !is_dir;
            break;
        case HUNT_DIRS:
            matches_type = is_dir;
            break;
        case HUNT_ANY:
            matches_type = 1;
            break;
    }
    if (matches_type && shell_str_contains(name, state->pattern)) {
        shell_print(child);
        shell_newline();
        if (state->matches) {
            (*state->matches)++;
        }
    }
    if (is_dir && state->depth < SHELL_HUNT_MAX_DEPTH) {
        if (shell_hunt_walk(state->shell, child, state->pattern, state->target, state->depth + 1, state->matches) != 0) {
            state->error = 1;
            return -1;
        }
    }
    return 0;
}

static int shell_hunt_walk(struct shell_context *ctx, const char *path, const char *pattern, enum hunt_target target, int depth, int *matches) {
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
    struct search_ctx *state = (struct search_ctx *)userdata;
    if (!state || !name) {
        return 0;
    }
    if ((name[0] == '.' && name[1] == '\0') || (name[0] == '.' && name[1] == '.' && name[2] == '\0')) {
        return 0;
    }
    char child[SHELL_PATH_BUFFER];
    if (shell_join_path(state->current, name, child, sizeof(child)) != 0) {
        state->error = 1;
        return -1;
    }
    if ((mode & VFS_MODE_DIR) != 0) {
        if (state->depth < SHELL_HUNT_MAX_DEPTH) {
            if (shell_search_text(state->shell, child, state->pattern, state->depth + 1, state->matches) != 0) {
                state->error = 1;
                return -1;
            }
        }
    } else {
        if (shell_search_text(state->shell, child, state->pattern, state->depth, state->matches) != 0) {
            state->error = 1;
            return -1;
        }
    }
    return 0;
}

static int shell_search_text(struct shell_context *ctx, const char *path, const char *pattern, int depth, int *matches) {
    if (shell_path_is_dir(path)) {
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

    char *buffer = NULL;
    size_t len = 0;
    if (read_file_into_memory(path, &buffer, &len) != 0) {
        return -1;
    }
    int found = shell_str_contains(buffer, pattern);
    kfree(buffer);
    if (found) {
        shell_print(path);
        shell_newline();
        if (matches) {
            (*matches)++;
        }
    }
    return 0;
}

static int cmd_print_file(struct shell_context *ctx, int argc, char **argv) {
    if (handle_help(argc, argv, "print-file <arquivo>",
                    "Mostra o conteudo completo de um arquivo de texto.")) {
        return 0;
    }
    if (argc < 2) {
        print_error("informe arquivo");
        suggest_help("print-file");
        return -1;
    }
    char path[SHELL_PATH_BUFFER];
    if (resolve_path(ctx, argv[1], path, sizeof(path)) != 0) {
        print_error("caminho invalido");
        suggest_help("print-file");
        return -1;
    }
    trim_trailing_slash(path);
    struct file *f = open_file_read(path);
    if (!f) {
        print_error("nao foi possivel abrir");
        suggest_help("print-file");
        return -1;
    }
    int res = stream_file(f);
    vfs_close(f);
    return res;
}

static int cmd_page(struct shell_context *ctx, int argc, char **argv) {
    if (handle_help(argc, argv, "page <arquivo>",
                    "Exibe o arquivo de forma paginada.")) {
        return 0;
    }
    return cmd_print_file(ctx, argc, argv);
}

static int read_file_into_memory(const char *path, char **out_buf, size_t *out_len) {
    struct file *f = vfs_open(path, 0);
    if (!f) {
        return -1;
    }
    size_t size = 0;
    if (f->dentry && f->dentry->inode) {
        size = f->dentry->inode->size;
    }
    char *buffer = (char *)kalloc(size + 1);
    if (!buffer) {
        vfs_close(f);
        return -1;
    }
    long read = vfs_read(f, buffer, size);
    vfs_close(f);
    if (read < 0) {
        kfree(buffer);
        return -1;
    }
    buffer[read] = '\0';
    if (out_buf) {
        *out_buf = buffer;
    }
    if (out_len) {
        *out_len = (size_t)read;
    }
    return 0;
}

static int cmd_print_file_begin(struct shell_context *ctx, int argc, char **argv) {
    if (handle_help(argc, argv, "print-file-begin <arquivo> [-n <linhas>]",
                    "Mostra as primeiras linhas de um arquivo. Use -n para ajustar a quantidade.")) {
        return 0;
    }
    if (argc < 2) {
        print_error("informe arquivo");
        suggest_help("print-file-begin");
        return -1;
    }
    int lines = 10;
    if (argc > 2) {
        int idx = 2;
        if (strings_equal(argv[2], "-n")) {
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
    if (resolve_path(ctx, argv[1], path, sizeof(path)) != 0) {
        print_error("caminho invalido");
        suggest_help("print-file-begin");
        return -1;
    }
    trim_trailing_slash(path);
    char *buffer = NULL;
    size_t len = 0;
    if (read_file_into_memory(path, &buffer, &len) != 0) {
        print_error("falha ao ler");
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
    if (handle_help(argc, argv, "print-file-end <arquivo> [-n <linhas>]",
                    "Mostra as ultimas linhas de um arquivo. Use -n para ajustar a quantidade.")) {
        return 0;
    }
    if (argc < 2) {
        print_error("informe arquivo");
        suggest_help("print-file-end");
        return -1;
    }
    int lines = 10;
    if (argc > 2) {
        int idx = 2;
        if (strings_equal(argv[2], "-n")) {
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
    if (resolve_path(ctx, argv[1], path, sizeof(path)) != 0) {
        print_error("caminho invalido");
        suggest_help("print-file-end");
        return -1;
    }
    trim_trailing_slash(path);
    char *buffer = NULL;
    size_t len = 0;
    if (read_file_into_memory(path, &buffer, &len) != 0) {
        print_error("falha ao ler");
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

static int cmd_mk_file(struct shell_context *ctx, int argc, char **argv) {
    if (handle_help(argc, argv, "mk-file <arquivo>",
                    "Cria um arquivo vazio no diretorio atual ou indicado.")) {
        return 0;
    }
    if (argc < 2) {
        print_error("informe arquivo");
        suggest_help("mk-file");
        return -1;
    }
    char path[SHELL_PATH_BUFFER];
    if (resolve_path(ctx, argv[1], path, sizeof(path)) != 0) {
        print_error("caminho invalido");
        suggest_help("mk-file");
        return -1;
    }
    trim_trailing_slash(path);
    struct vfs_metadata meta;
    shell_fill_metadata(ctx, VFS_MODE_FILE, &meta);
    if (vfs_create(path, VFS_MODE_FILE, &meta) != 0) {
        print_error("nao foi possivel criar");
        suggest_help("mk-file");
        return -1;
    }
    print_ok(path);
    return 0;
}

static int cmd_mk_dir(struct shell_context *ctx, int argc, char **argv) {
    if (handle_help(argc, argv, "mk-dir <caminho>",
                    "Cria um novo diretorio. Caminhos com '/' criam a arvore completa.")) {
        return 0;
    }
    if (argc < 2) {
        print_error("informe diretorio");
        suggest_help("mk-dir");
        return -1;
    }
    int res = shell_dir_recursive(ctx, argv[1]);
    if (res == -2) {
        print_error("ja existe arquivo neste caminho");
        return -1;
    }
    if (res != 0) {
        print_error("nao foi possivel criar diretorio");
        suggest_help("mk-dir");
        return -1;
    }
    char path[SHELL_PATH_BUFFER];
    if (session_resolve_path(ctx->session, argv[1], path, sizeof(path)) == 0) {
        trim_trailing_slash(path);
        print_ok(path);
    } else {
        print_ok(argv[1]);
    }
    return 0;
}

static int cmd_config_keyboard(struct shell_context *ctx, int argc, char **argv) {
    (void)ctx;
    if (handle_help(argc, argv,
                    "config-keyboard [list|show|<layout>]",
                    "Ajusta o layout do teclado. Use 'list' para ver opcoes ou informe um layout como 'us' ou 'br-abnt2'.")) {
        return 0;
    }
    if (argc == 1) {
        shell_print("Layout atual: ");
        shell_print(keyboard_current_layout());
        shell_newline();
        shell_print("Use config-keyboard list para ver opcoes.\n");
        return 0;
    }
    if (strings_equal(argv[1], "list")) {
        size_t count = keyboard_layout_count();
        for (size_t i = 0; i < count; ++i) {
            const char *name = keyboard_layout_name(i);
            const char *desc = keyboard_layout_description(i);
            shell_print(" - ");
            shell_print(name ? name : "?");
            if (desc && desc[0]) {
                shell_print(" : ");
                shell_print(desc);
            }
            shell_newline();
        }
        return 0;
    }
    if (strings_equal(argv[1], "show")) {
        shell_print("Layout atual: ");
        shell_print(keyboard_current_layout());
        shell_newline();
        return 0;
    }
    if (keyboard_set_layout_by_name(argv[1]) == 0) {
        print_ok(argv[1]);
        return 0;
    }
    print_error("layout desconhecido");
    shell_print("Use config-keyboard list para ver layouts disponiveis.\n");
    return -1;
}

static int cmd_kill_file(struct shell_context *ctx, int argc, char **argv) {
    if (handle_help(argc, argv, "kill-file <arquivo>", "Remove o arquivo informado.")) {
        return 0;
    }
    if (argc < 2) {
        print_error("informe arquivo");
        suggest_help("kill-file");
        return -1;
    }
    char path[SHELL_PATH_BUFFER];
    if (resolve_path(ctx, argv[1], path, sizeof(path)) != 0) {
        print_error("caminho invalido");
        suggest_help("kill-file");
        return -1;
    }
    trim_trailing_slash(path);
    if (vfs_unlink(path) != 0) {
        print_error("falha ao remover");
        suggest_help("kill-file");
        return -1;
    }
    print_ok("removido");
    return 0;
}

static int cmd_kill_dir(struct shell_context *ctx, int argc, char **argv) {
    if (handle_help(argc, argv, "kill-dir <diretorio>", "Remove um diretorio vazio.")) {
        return 0;
    }
    if (argc < 2) {
        print_error("informe diretorio");
        suggest_help("kill-dir");
        return -1;
    }
    char path[SHELL_PATH_BUFFER];
    if (resolve_path(ctx, argv[1], path, sizeof(path)) != 0) {
        print_error("caminho invalido");
        suggest_help("kill-dir");
        return -1;
    }
    trim_trailing_slash(path);
    if (vfs_rmdir(path) != 0) {
        print_error("falha ao remover");
        suggest_help("kill-dir");
        return -1;
    }
    print_ok("diretorio removido");
    return 0;
}

static int cmd_move(struct shell_context *ctx, int argc, char **argv) {
    if (handle_help(argc, argv, "move <origem> <destino>",
                    "Move ou renomeia arquivos e diretorios.")) {
        return 0;
    }
    if (argc < 3) {
        print_error("informe origem e destino");
        suggest_help("move");
        return -1;
    }
    char src_path[SHELL_PATH_BUFFER];
    char dst_path[SHELL_PATH_BUFFER];
    if (resolve_path(ctx, argv[1], src_path, sizeof(src_path)) != 0) {
        print_error("origem invalida");
        suggest_help("move");
        return -1;
    }
    trim_trailing_slash(src_path);
    if (resolve_path(ctx, argv[2], dst_path, sizeof(dst_path)) != 0) {
        print_error("destino invalido");
        suggest_help("move");
        return -1;
    }
    trim_trailing_slash(dst_path);
    if (shell_path_is_dir(dst_path)) {
        char combined[SHELL_PATH_BUFFER];
        if (shell_join_path(dst_path, shell_basename(src_path), combined, sizeof(combined)) != 0) {
            print_error("destino muito longo");
            return -1;
        }
        shell_copy(dst_path, sizeof(dst_path), combined);
    }
    if (vfs_rename(src_path, dst_path) != 0) {
        print_error("nao foi possivel mover");
        suggest_help("move");
        return -1;
    }
    print_ok(dst_path);
    return 0;
}

static int cmd_clone(struct shell_context *ctx, int argc, char **argv) {
    if (handle_help(argc, argv, "clone <origem> <destino>",
                    "Copia um arquivo para o destino informado.")) {
        return 0;
    }
    if (argc < 3) {
        print_error("informe origem e destino");
        suggest_help("clone");
        return -1;
    }
    char src_path[SHELL_PATH_BUFFER];
    char dst_path[SHELL_PATH_BUFFER];
    if (resolve_path(ctx, argv[1], src_path, sizeof(src_path)) != 0) {
        print_error("origem invalida");
        suggest_help("clone");
        return -1;
    }
    trim_trailing_slash(src_path);
    if (!shell_path_is_file(src_path)) {
        print_error("origem nao e arquivo");
        suggest_help("clone");
        return -1;
    }
    if (resolve_path(ctx, argv[2], dst_path, sizeof(dst_path)) != 0) {
        print_error("destino invalido");
        suggest_help("clone");
        return -1;
    }
    trim_trailing_slash(dst_path);
    if (shell_path_is_dir(dst_path)) {
        char combined[SHELL_PATH_BUFFER];
        if (shell_join_path(dst_path, shell_basename(src_path), combined, sizeof(combined)) != 0) {
            print_error("destino muito longo");
            suggest_help("clone");
            return -1;
        }
        shell_copy(dst_path, sizeof(dst_path), combined);
    }
    if (shell_path_is_file(dst_path)) {
        print_error("destino ja existe");
        suggest_help("clone");
        return -1;
    }

    struct vfs_metadata meta;
    shell_fill_metadata(ctx, VFS_MODE_FILE, &meta);
    if (vfs_create(dst_path, VFS_MODE_FILE, &meta) != 0) {
        print_error("nao foi possivel criar destino");
        suggest_help("clone");
        return -1;
    }

    struct file *src = open_file_read(src_path);
    struct file *dst = open_file_write(dst_path);
    if (!src || !dst) {
        if (src) vfs_close(src);
        if (dst) vfs_close(dst);
        vfs_unlink(dst_path);
        print_error("falha ao abrir arquivos");
        suggest_help("clone");
        return -1;
    }

    int copy_res = shell_copy_stream(src, dst);
    vfs_close(src);
    vfs_close(dst);
    if (copy_res != 0) {
        vfs_unlink(dst_path);
        print_error("falha na copia");
        suggest_help("clone");
        return -1;
    }
    print_ok(dst_path);
    return 0;
}

static int shell_run_hunt(struct shell_context *ctx, enum hunt_target target, int argc, char **argv) {
    const char *pattern = (argc > 1) ? argv[1] : "";
    const char *start = (argc > 2) ? argv[2] : ".";
    char base[SHELL_PATH_BUFFER];
    if (resolve_path(ctx, start, base, sizeof(base)) != 0) {
        print_error("caminho invalido");
        suggest_help(target == HUNT_FILES ? "hunt-file" :
                     target == HUNT_DIRS ? "hunt-dir" : "hunt-any");
        return -1;
    }
    trim_trailing_slash(base);
    int matches = 0;
    int is_dir = shell_path_is_dir(base);
    int is_file = shell_path_is_file(base);
    if (!is_dir && !is_file) {
        print_error("alvo inexistente");
        suggest_help(target == HUNT_FILES ? "hunt-file" :
                     target == HUNT_DIRS ? "hunt-dir" : "hunt-any");
        return -1;
    }
    if (is_file && (target == HUNT_FILES || target == HUNT_ANY)) {
        if (shell_str_contains(shell_basename(base), pattern)) {
            shell_print(base);
            shell_newline();
            matches++;
        } else {
            shell_print("(nenhum resultado)\n");
        }
        return 0;
    }
    if (is_dir) {
        if (target == HUNT_DIRS || target == HUNT_ANY) {
            if (shell_str_contains(shell_basename(base), pattern)) {
                shell_print(base);
                shell_newline();
                matches++;
            }
        }
        if (shell_hunt_walk(ctx, base, pattern, target, 0, &matches) != 0) {
            print_error("falha na varredura");
            return -1;
        }
    }
    if (matches == 0) {
        shell_print("(nenhum resultado)\n");
    }
    return 0;
}

static int cmd_hunt_file(struct shell_context *ctx, int argc, char **argv) {
    if (handle_help(argc, argv, "hunt-file <padrao> [caminho]",
                    "Busca arquivos cujo nome corresponde ao padrao.")) {
        return 0;
    }
    return shell_run_hunt(ctx, HUNT_FILES, argc, argv);
}

static int cmd_hunt_dir(struct shell_context *ctx, int argc, char **argv) {
    if (handle_help(argc, argv, "hunt-dir <padrao> [caminho]",
                    "Busca diretorios cujo nome corresponde ao padrao.")) {
        return 0;
    }
    return shell_run_hunt(ctx, HUNT_DIRS, argc, argv);
}

static int cmd_hunt_any(struct shell_context *ctx, int argc, char **argv) {
    if (handle_help(argc, argv, "hunt-any <padrao> [caminho]",
                    "Busca arquivos e diretorios ao mesmo tempo.")) {
        return 0;
    }
    return shell_run_hunt(ctx, HUNT_ANY, argc, argv);
}

static int cmd_find(struct shell_context *ctx, int argc, char **argv) {
    if (handle_help(argc, argv, "find \"texto\" [caminho]",
                    "Procura texto em arquivos ou diretorios.")) {
        return 0;
    }
    if (argc < 2) {
        print_error("informe termo");
        suggest_help("find");
        return -1;
    }
    const char *pattern = argv[1];
    const char *start = (argc > 2) ? argv[2] : ".";
    char base[SHELL_PATH_BUFFER];
    if (resolve_path(ctx, start, base, sizeof(base)) != 0) {
        print_error("caminho invalido");
        suggest_help("find");
        return -1;
    }
    trim_trailing_slash(base);
    int matches = 0;
    if (shell_search_text(ctx, base, pattern, 0, &matches) != 0) {
        print_error("falha na busca");
        suggest_help("find");
        return -1;
    }
    if (matches == 0) {
        shell_print("(nenhum resultado)\n");
    }
    return 0;
}

static int cmd_stats_file(struct shell_context *ctx, int argc, char **argv) {
    if (handle_help(argc, argv, "stats-file <caminho>",
                    "Mostra estatisticas de permissao, tamanho e donos.")) {
        return 0;
    }
    if (argc < 2) {
        print_error("informe caminho");
        suggest_help("stats-file");
        return -1;
    }
    char path[SHELL_PATH_BUFFER];
    if (resolve_path(ctx, argv[1], path, sizeof(path)) != 0) {
        print_error("caminho invalido");
        suggest_help("stats-file");
        return -1;
    }
    trim_trailing_slash(path);
    struct vfs_stat st;
    if (vfs_stat_path(path, &st) != 0) {
        print_error("nao foi possivel coletar dados");
        suggest_help("stats-file");
        return -1;
    }
    shell_print("alvo: ");
    shell_print(path);
    shell_newline();
    shell_print("tipo: ");
    shell_print((st.mode & VFS_MODE_DIR) ? "diretorio" : "arquivo");
    shell_newline();
    shell_print("tamanho: ");
    print_number(st.size);
    shell_print(" bytes\n");
    shell_print("uid: ");
    print_number(st.uid);
    shell_print(" gid: ");
    print_number(st.gid);
    shell_newline();
    char perm_buf[5];
    shell_format_perm(st.perm, perm_buf);
    shell_print("permissoes: 0");
    shell_print(perm_buf);
    shell_newline();
    return 0;
}

static int cmd_type(struct shell_context *ctx, int argc, char **argv) {
    if (handle_help(argc, argv, "type <caminho>", "Informa o tipo basico do arquivo.")) {
        return 0;
    }
    if (argc < 2) {
        print_error("informe caminho");
        suggest_help("type");
        return -1;
    }
    char path[SHELL_PATH_BUFFER];
    if (resolve_path(ctx, argv[1], path, sizeof(path)) != 0) {
        print_error("caminho invalido");
        suggest_help("type");
        return -1;
    }
    trim_trailing_slash(path);
    struct vfs_stat st;
    if (vfs_stat_path(path, &st) != 0) {
        print_error("alvo inexistente");
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
    print_number(st.uid);
    shell_print(" gid=");
    print_number(st.gid);
    shell_newline();
    return 0;
}

static int cmd_print_echo(struct shell_context *ctx, int argc, char **argv) {
    if (handle_help(argc, argv, "print-echo [texto]",
                    "Reproduz o texto informado na tela.")) {
        return 0;
    }
    (void)ctx;
    if (argc <= 1) {
        shell_newline();
        return 0;
    }
    for (int i = 1; i < argc; ++i) {
        shell_print(argv[i]);
        if (i != argc - 1) {
            vga_putc(' ');
        }
    }
    shell_newline();
    return 0;
}

static int cmd_help_any(struct shell_context *ctx, int argc, char **argv) {
    if (command_help_requested(argc, argv)) {
        shell_print("Uso: help-any\nLista todos os comandos registrados no NoirCLI.\n");
        return 0;
    }
    (void)ctx;(void)argc;(void)argv;
    shell_print("Comandos disponiveis:\n");
    for (size_t i = 0; i < sizeof(g_commands)/sizeof(g_commands[0]); ++i) {
        shell_print(" - ");
        shell_print(g_commands[i].name);
        shell_newline();
    }
    shell_print("Use help-docs para resumo rapido.\n");
    return 0;
}

static int cmd_help_docs(struct shell_context *ctx, int argc, char **argv) {
    if (command_help_requested(argc, argv)) {
        shell_print("Uso: help-docs\nExibe a referencia de comandos documentada em /docs/noiros-cli-reference.txt.\n");
        return 0;
    }
    (void)ctx;(void)argc;(void)argv;
    struct file *f = open_file_read("/docs/noiros-cli-reference.txt");
    if (!f) {
        shell_print("Documentacao nao encontrada em /docs/noiros-cli-reference.txt\n");
        shell_print("Verifique instalacao ou use help-any.\n");
        return 0;
    }
    stream_file(f);
    vfs_close(f);
    return 0;
}

static int cmd_mess(struct shell_context *ctx, int argc, char **argv) {
    if (command_help_requested(argc, argv)) {
        shell_print("Uso: mess\nLimpa a tela do terminal.\n");
        return 0;
    }
    (void)ctx;(void)argc;(void)argv;
    vga_clear();
    return 0;
}

static int cmd_bye(struct shell_context *ctx, int argc, char **argv) {
    if (handle_help(argc, argv, "bye",
                    "Encerra a sessao atual e retorna a tela de login.")) {
        return 0;
    }
    ctx->running = 0;
    ctx->logout = 1;
    shell_print("Encerrando sessao...\n");
    return 0;
}

static int cmd_print_me(struct shell_context *ctx, int argc, char **argv) {
    if (command_help_requested(argc, argv)) {
        shell_print("Uso: print-me\nMostra o usuario autenticado na sessao.\n");
        return 0;
    }
    (void)argc;(void)argv;
    const struct user_record *user = session_user(ctx->session);
    shell_print(user ? user->username : "desconhecido");
    shell_newline();
    return 0;
}

static void print_number(uint32_t value) {
    char buf[12];
    size_t len = 0;
    if (value == 0) {
        buf[len++] = '0';
    } else {
        uint32_t v = value;
        char tmp[10];
        size_t idx = 0;
        while (v && idx < sizeof(tmp)) {
            tmp[idx++] = (char)('0' + (v % 10));
            v /= 10;
        }
        while (idx) {
            buf[len++] = tmp[--idx];
        }
    }
    buf[len] = '\0';
    shell_print(buf);
}

static int cmd_print_id(struct shell_context *ctx, int argc, char **argv) {
    if (command_help_requested(argc, argv)) {
        shell_print("Uso: print-id\nMostra UID e GID do usuario atual.\n");
        return 0;
    }
    (void)argc;(void)argv;
    const struct user_record *user = session_user(ctx->session);
    if (!user) {
        return -1;
    }
    shell_print("uid=");
    print_number(user->uid);
    shell_print(" gid=");
    print_number(user->gid);
    shell_print(" role=");
    shell_print(user->role);
    shell_newline();
    return 0;
}

static int cmd_print_host(struct shell_context *ctx, int argc, char **argv) {
    if (command_help_requested(argc, argv)) {
        shell_print("Uso: print-host\nExibe o hostname configurado no sistema.\n");
        return 0;
    }
    (void)argc;(void)argv;
    shell_print(ctx->settings ? ctx->settings->hostname : "noiros");
    shell_newline();
    return 0;
}

static int cmd_print_version(struct shell_context *ctx, int argc, char **argv) {
    if (command_help_requested(argc, argv)) {
        shell_print("Uso: print-version\nMostra a versao do NoirOS.\n");
        return 0;
    }
    (void)ctx;(void)argc;(void)argv;
    shell_print("NoirOS 1 - Versao Singularity\n");
    return 0;
}

static uint32_t shell_uptime_seconds(void) {
    uint64_t ticks = pit_ticks();
    return (uint32_t)(ticks / 100u);
}

static void format_hms(uint32_t seconds, char *out, size_t out_len) {
    if (out_len < 9) {
        if (out_len) {
            out[0] = '\0';
        }
        return;
    }
    uint32_t hrs = seconds / 3600u;
    uint32_t mins = (seconds % 3600u) / 60u;
    uint32_t secs = seconds % 60u;
    out[0] = (char)('0' + (hrs / 10) % 10);
    out[1] = (char)('0' + (hrs % 10));
    out[2] = ':';
    out[3] = (char)('0' + (mins / 10) % 10);
    out[4] = (char)('0' + (mins % 10));
    out[5] = ':';
    out[6] = (char)('0' + (secs / 10) % 10);
    out[7] = (char)('0' + (secs % 10));
    out[8] = '\0';
}

static int cmd_print_time(struct shell_context *ctx, int argc, char **argv) {
    if (command_help_requested(argc, argv)) {
        shell_print("Uso: print-time\nMostra o horario atual (simulado) desde o boot.\n");
        return 0;
    }
    (void)ctx;(void)argc;(void)argv;
    uint32_t seconds = shell_uptime_seconds();
    uint32_t simulated = seconds % (24u * 3600u);
    char buffer[16];
    format_hms(simulated, buffer, sizeof(buffer));
    shell_print("hora atual (simulada) ");
    shell_print(buffer);
    shell_print(" (HH:MM:SS)\n");
    return 0;
}

static int cmd_print_insomnia(struct shell_context *ctx, int argc, char **argv) {
    if (command_help_requested(argc, argv)) {
        shell_print("Uso: print-insomnia\nMostra o tempo total de atividade do sistema.");
        shell_newline();
        return 0;
    }
    (void)ctx;(void)argc;(void)argv;
    uint32_t seconds = shell_uptime_seconds();
    char buffer[16];
    format_hms(seconds, buffer, sizeof(buffer));
    shell_print("uptime ");
    shell_print(buffer);
    shell_print(" (HH:MM:SS)\n");
    return 0;
}

static int cmd_print_envs(struct shell_context *ctx, int argc, char **argv) {
    if (command_help_requested(argc, argv)) {
        shell_print("Uso: print-envs\nExibe variaveis basicas da sessao (USER, HOME, HOST, etc.).\n");
        return 0;
    }
    (void)argc;(void)argv;
    const struct user_record *user = session_user(ctx->session);
    shell_print("USER=");
    shell_print(user ? user->username : "");
    shell_newline();
    shell_print("ROLE=");
    shell_print(user ? user->role : "");
    shell_newline();
    shell_print("UID=");
    print_number(user ? user->uid : 0);
    shell_newline();
    shell_print("GID=");
    print_number(user ? user->gid : 0);
    shell_newline();
    shell_print("HOME=");
    shell_print(user ? user->home : "/");
    shell_newline();
    shell_print("PWD=");
    shell_print(session_cwd(ctx->session));
    shell_newline();
    shell_print("HOST=");
    shell_print(ctx->settings ? ctx->settings->hostname : "noiros");
    shell_newline();
    shell_print("PATH=/bin:/system\n");
    return 0;
}

static int cmd_do_sync(struct shell_context *ctx, int argc, char **argv) {
    if (command_help_requested(argc, argv)) {
        shell_print("Uso: do-sync\nForca a gravacao do buffer de disco.");
        shell_newline();
        return 0;
    }
    (void)ctx; (void)argc; (void)argv;
    struct super_block *root = vfs_root();
    if (!root || !root->bdev) {
        print_error("sem dispositivo");
        return -1;
    }
    buffer_cache_sync(root->bdev);
    print_ok("buffers sincronizados");
    return 0;
}
