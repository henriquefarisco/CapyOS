/* shell_main.c: CapyCLI entry, self-test, diagnostics, and command loop. */
#include "shell/shell.h"
#include "shell/core.h"
#include "shell/commands.h"

#include "memory/kmem.h"
#include "drivers/console/tty.h"
#include "drivers/input/keyboard.h"
#include "core/user.h"
#include "fs/vfs.h"
#include "drivers/video/vga.h"

#include <stddef.h>
#include <stdint.h>

static struct shell_command_set g_command_sets[9];
static size_t g_command_set_count = 0;
static int g_command_sets_initialized = 0;
static void shell_hotkey_help_docs(void)
{
    tty_inject_line("help-docs", 1);
}

static void shell_init_command_sets(void)
{
    if (g_command_sets_initialized) {
        return;
    }

    size_t idx = 0;

#define ADD_COMMAND_SET(fetcher)                                                         \
    do {                                                                                 \
        size_t count_ = 0;                                                               \
        const struct shell_command *cmds_ = fetcher(&count_);                            \
        if (cmds_ && count_ && idx < sizeof(g_command_sets) / sizeof(g_command_sets[0])) \
        {                                                                                \
            g_command_sets[idx].commands = cmds_;                                       \
            g_command_sets[idx].count = count_;                                         \
            ++idx;                                                                      \
        }                                                                                \
    } while (0)

    ADD_COMMAND_SET(shell_commands_filesystem_navigation);
    ADD_COMMAND_SET(shell_commands_filesystem_content);
    ADD_COMMAND_SET(shell_commands_filesystem_manage);
    ADD_COMMAND_SET(shell_commands_filesystem_search);
    ADD_COMMAND_SET(shell_commands_help);
    ADD_COMMAND_SET(shell_commands_session);
    ADD_COMMAND_SET(shell_commands_system_info);
    ADD_COMMAND_SET(shell_commands_system_control);
    ADD_COMMAND_SET(shell_commands_network);

#undef ADD_COMMAND_SET

    g_command_set_count = idx;
    g_command_sets_initialized = 1;
}

const struct shell_command_set *shell_command_sets(size_t *count)
{
    shell_init_command_sets();
    if (count) {
        *count = g_command_set_count;
    }
    return g_command_sets;
}

void shell_context_init(struct shell_context *ctx,
                        struct session_context *session,
                        const struct system_settings *settings)
{
    if (!ctx) {
        return;
    }
    ctx->session = session;
    ctx->settings = settings;
    ctx->running = 1;
    ctx->logout = 0;
}

struct session_context *shell_context_session(struct shell_context *ctx)
{
    return ctx ? ctx->session : NULL;
}

const struct system_settings *shell_context_settings(const struct shell_context *ctx)
{
    return ctx ? ctx->settings : NULL;
}

int shell_context_running(const struct shell_context *ctx)
{
    return ctx ? ctx->running : 0;
}

int shell_context_should_logout(const struct shell_context *ctx)
{
    return ctx ? ctx->logout : 0;
}

void shell_request_exit(struct shell_context *ctx)
{
    if (ctx) {
        ctx->running = 0;
    }
}

void shell_request_logout(struct shell_context *ctx)
{
    if (ctx) {
        ctx->logout = 1;
        ctx->running = 0;
    }
}

int shell_string_equal(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b) {
        if (*a++ != *b++) {
            return 0;
        }
    }
    return *a == *b;
}

size_t shell_cstring_length(const char *s)
{
    size_t len = 0;
    if (!s) {
        return 0;
    }
    while (s[len]) {
        ++len;
    }
    return len;
}

void shell_copy(char *dst, size_t dst_len, const char *src)
{
    if (!dst || !dst_len) {
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

int shell_string_contains(const char *haystack, const char *needle)
{
    if (!needle || !needle[0]) {
        return 1;
    }
    if (!haystack) {
        return 0;
    }
    size_t hlen = shell_cstring_length(haystack);
    size_t nlen = shell_cstring_length(needle);
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

int shell_help_requested(int argc, char **argv)
{
    if (argc != 2 || !argv || !argv[1]) {
        return 0;
    }
    return shell_string_equal(argv[1], "-help") || shell_string_equal(argv[1], "--help");
}

int shell_handle_help(int argc, char **argv, const char *usage, const char *details)
{
    if (!shell_help_requested(argc, argv)) {
        return 0;
    }
    if (usage) {
        shell_print("Uso: ");
        shell_print(usage);
        shell_newline();
    }
    if (details) {
        shell_print(details);
        shell_newline();
    }
    return 1;
}

void shell_suggest_help(const char *cmd)
{
    shell_print("Use ");
    shell_print(cmd);
    shell_print(" -help para detalhes.\n");
}

static int shell_get_stat(const char *path, struct vfs_stat *st)
{
    return vfs_stat_path(path, st);
}

void shell_fill_metadata(struct shell_context *ctx, uint16_t mode, struct vfs_metadata *meta)
{
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

int shell_path_is_dir(const char *path)
{
    struct vfs_stat st;
    if (shell_get_stat(path, &st) != 0) {
        return 0;
    }
    return (st.mode & VFS_MODE_DIR) != 0;
}

int shell_path_is_file(const char *path)
{
    struct vfs_stat st;
    if (shell_get_stat(path, &st) != 0) {
        return 0;
    }
    return (st.mode & VFS_MODE_FILE) != 0;
}

void shell_trim_trailing_slash(char *path)
{
    size_t len = shell_cstring_length(path);
    while (len > 1 && path[len - 1] == '/') {
        path[--len] = '\0';
    }
}

int shell_resolve_path(struct shell_context *ctx, const char *input, char *out, size_t out_len)
{
    if (!ctx || !ctx->session) {
        return -1;
    }
    if (!input || !input[0]) {
        return session_resolve_path(ctx->session, ".", out, out_len);
    }
    return session_resolve_path(ctx->session, input, out, out_len);
}

int shell_join_path(const char *dir, const char *name, char *out, size_t out_len)
{
    if (!out || !out_len) {
        return -1;
    }
    size_t pos = 0;
    const char *base_dir = (dir && dir[0]) ? dir : "/";
    size_t dir_len = shell_cstring_length(base_dir);
    if (dir_len >= out_len) {
        return -1;
    }
    for (size_t i = 0; i < dir_len; ++i) {
        out[pos++] = base_dir[i];
    }
    if (pos > 1 && out[pos - 1] != '/') {
        if (pos >= out_len) {
            return -1;
        }
        out[pos++] = '/';
    }
    const char *leaf = name ? name : "";
    size_t name_len = shell_cstring_length(leaf);
    if (pos + name_len >= out_len) {
        return -1;
    }
    for (size_t i = 0; i < name_len; ++i) {
        out[pos++] = leaf[i];
    }
    if (pos == 0) {
        out[pos++] = '/';
    }
    out[pos] = '\0';
    return 0;
}

const char *shell_basename(const char *path)
{
    if (!path) {
        return "";
    }
    size_t len = shell_cstring_length(path);
    while (len > 0 && path[len - 1] == '/') {
        --len;
    }
    const char *last = path;
    for (size_t i = 0; i < len; ++i) {
        if (path[i] == '/' && i + 1 < len) {
            last = &path[i + 1];
        }
    }
    return last;
}

void shell_format_perm(uint16_t perm, char out[5])
{
    out[0] = (char)('0' + ((perm >> 6) & 0x7));
    out[1] = (char)('0' + ((perm >> 3) & 0x7));
    out[2] = (char)('0' + (perm & 0x7));
    out[3] = '\0';
    out[4] = '\0';
}

void shell_print(const char *text)
{
    if (text) {
        vga_write(text);
    }
}

void shell_newline(void)
{
    vga_newline();
}

void shell_print_error(const char *msg)
{
    vga_write("[erro] ");
    if (msg) {
        vga_write(msg);
    }
    vga_newline();
}

void shell_print_ok(const char *msg)
{
    vga_write("[ok] ");
    if (msg) {
        vga_write(msg);
    }
    vga_newline();
}

void shell_print_number(uint32_t value)
{
    char buffer[12];
    size_t len = 0;
    if (value == 0) {
        buffer[len++] = '0';
    } else {
        char tmp[10];
        size_t idx = 0;
        while (value && idx < sizeof(tmp)) {
            tmp[idx++] = (char)('0' + (value % 10));
            value /= 10;
        }
        while (idx) {
            buffer[len++] = tmp[--idx];
        }
    }
    buffer[len] = '\0';
    shell_print(buffer);
}

void shell_paginate_content(const char *content)
{
    if (!content) {
        return;
    }
    const int lines_per_page = 20;
    int line_count = 0;
    const char *p = content;

    tty_set_echo(0);

    while (*p) {
        while (*p && *p != '\n') {
            vga_putc(*p++);
        }
        vga_newline();
        if (*p == '\n') {
            ++p;
        }
        if (++line_count >= lines_per_page && *p) {
            shell_print("-- mais --");
            char c = tty_getc();
            vga_backspace_multiple(10);
            if (c == 'q' || c == 'Q') {
                break;
            }
            line_count = 0;
        }
    }

    tty_set_echo(1);
}

void shell_update_prompt(struct shell_context *ctx)
{
    const struct user_record *user = session_user(ctx->session);
    const char *username = user ? user->username : "user";
    const char *hostname = ctx->settings ? ctx->settings->hostname : "capyos";
    char prompt[TTY_PROMPT_MAX];
    size_t idx = 0;
    const char *parts[] = { username, "@", hostname, "> " };
    for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i) {
        const char *part = parts[i];
        size_t plen = shell_cstring_length(part);
        for (size_t j = 0; j < plen && idx < sizeof(prompt) - 1; ++j) {
            prompt[idx++] = part[j];
        }
    }
    prompt[idx] = '\0';
    tty_set_prompt(prompt);
}

int shell_parse_line(char *line, char **argv, int max_args)
{
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
            } else if (c == ' ' || c == '\t') {
                ++p;
                break;
            }
            *dest++ = c;
            ++p;
        }
        *dest = '\0';
        argv[argc++] = start;
    }
    return argc;
}

struct file *shell_open_file_read(const char *path)
{
    return vfs_open(path, 0);
}

struct file *shell_open_file_write(const char *path)
{
    return vfs_open(path, VFS_OPEN_WRITE);
}

int shell_stream_file(struct file *file)
{
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

int shell_copy_stream(struct file *src, struct file *dst)
{
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
            remaining -= written;
            ptr += written;
        }
    }
    return 0;
}

int shell_read_file(const char *path, char **out_buf, size_t *out_len)
{
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

static int diag_log_append(const char *msg)
{
    (void)msg;
    return 0;
}

static void cli_log_process_begin(const char *name)
{
    vga_write("[diag] Iniciando ");
    vga_write(name);
    vga_write("...\n");
}

static void cli_log_process_begin_success(const char *name)
{
    vga_write("[diag] Processo ");
    vga_write(name);
    vga_write(" iniciado.\n");
}

static void cli_log_process_progress(const char *name)
{
    vga_write("[diag] ");
    vga_write(name);
    vga_write(" em andamento...\n");
}

static void cli_log_process_conclude(const char *name)
{
    vga_write("[diag] Finalizando ");
    vga_write(name);
    vga_write("...\n");
}

static void cli_log_process_finalize(const char *name)
{
    vga_write("[diag] Processo ");
    vga_write(name);
    vga_write(" concluido.\n");
}

static void cli_log_process_finalize_success(const char *name)
{
    vga_write("[diag] Processo ");
    vga_write(name);
    vga_write(" finalizado com sucesso.\n");
}

static void cli_log_process_error(const char *name, const char *reason)
{
    vga_write("[erro] Processo ");
    vga_write(name);
    vga_write(" falhou");
    if (reason) {
        vga_write(": ");
        vga_write(reason);
    }
    vga_write(".\n");
}

static void cli_log_dependency_wait(const char *dependency, const char *target)
{
    vga_write("Aguardando processo ");
    vga_write(dependency);
    vga_write(" para iniciar o processo ");
    vga_write(target);
    vga_write("...\n");
}

static int shell_self_test(struct shell_context *ctx)
{
    const char *proc = "auto teste do CapyCLI";
    cli_log_process_begin(proc);
    cli_log_process_begin_success(proc);
    if (!ctx || !ctx->session) {
        cli_log_process_error(proc, "sessao CLI invalida.");
        cli_log_process_finalize(proc);
        return -1;
    }

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

    cli_log_process_conclude(proc);
    cli_log_process_finalize(proc);
    cli_log_process_finalize_success(proc);
    return 0;
}

static const struct shell_command *shell_find_command(const char *name)
{
    size_t set_count = 0;
    const struct shell_command_set *sets = shell_command_sets(&set_count);
    for (size_t i = 0; i < set_count; ++i) {
        for (size_t j = 0; j < sets[i].count; ++j) {
            if (shell_string_equal(name, sets[i].commands[j].name)) {
                return &sets[i].commands[j];
            }
        }
    }
    return NULL;
}

static int run_command_by_name(struct shell_context *ctx, const char *name, int argc, char **argv)
{
    const struct shell_command *cmd = shell_find_command(name);
    if (!cmd) {
        return -1;
    }
    return cmd->handler(ctx, argc, argv);
}

static int shell_run_diagnostics(struct shell_context *ctx)
{
    if (!ctx) {
        return -1;
    }
    const char *proc = "diagnostico de comandos basicos do CapyCLI";
    cli_log_process_begin(proc);
    cli_log_process_begin_success(proc);
    diag_log_append("[diag] CLI self-test iniciado");

    struct diag_case {
        const char *cmd;
        int argc;
        char *argv[3];
    };

    char arg_help[] = "-help";
    char path_root[] = "/";
    char cmd_list[] = "list";
    char cmd_print_host[] = "print-host";
    char cmd_print_me[] = "print-me";
    char cmd_print_id[] = "print-id";
    char cmd_print_time[] = "print-time";
    char cmd_help_any[] = "help-any";
    char cmd_mypath[] = "mypath";

    struct diag_case tests[] = {
        { "list", 2, { cmd_list, path_root, NULL } },
        { "print-host", 1, { cmd_print_host, NULL, NULL } },
        { "print-me", 1, { cmd_print_me, NULL, NULL } },
        { "print-id", 1, { cmd_print_id, NULL, NULL } },
        { "print-time", 1, { cmd_print_time, NULL, NULL } },
        { "help-any", 2, { cmd_help_any, arg_help, NULL } },
        { "mypath", 2, { cmd_mypath, arg_help, NULL } },
    };

    int failures = 0;
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        int rc = run_command_by_name(ctx, tests[i].cmd, tests[i].argc, tests[i].argv);
        if (rc != 0) {
            failures++;
        }
    }

    cli_log_process_conclude(proc);
    if (failures) {
        cli_log_process_error(proc, "falhas encontradas no diagnostico.");
        cli_log_process_finalize(proc);
        return -1;
    }
    cli_log_process_finalize(proc);
    cli_log_process_finalize_success(proc);
    return 0;
}

enum shell_result shell_run(struct session_context *session, const struct system_settings *settings)
{
    if (!session) {
        return SHELL_RESULT_EXIT;
    }

    keyboard_set_help_callback(shell_hotkey_help_docs);

    struct shell_context ctx;
    shell_context_init(&ctx, session, settings);

    char line[TTY_BUFFER_MAX];
    char *argv[SHELL_MAX_ARGS];

    session_set_active(session);

    /* Skip self-tests/diagnostics to avoid boot loops in constrained setups. */
    int run_diag = 0;
    if (settings && settings->diagnostics_enabled) {
        run_diag = 1;
    }
    if (run_diag) {
        if (shell_self_test(&ctx) != 0) {
            session_set_active(NULL);
            keyboard_set_help_callback(NULL);
            return SHELL_RESULT_EXIT;
        }

        cli_log_dependency_wait("auto teste do CapyCLI", "diagnostico de comandos basicos do CapyCLI");
        if (shell_run_diagnostics(&ctx) != 0) {
            shell_print_error("Processo diagnostico de comandos basicos do CapyCLI finalizado com erro (veja /var/log/cli-selftest.log)");
        } else {
            shell_print_ok("Processo diagnostico de comandos basicos do CapyCLI finalizado com sucesso.");
        }
    }

    while (shell_context_running(&ctx)) {
        shell_update_prompt(&ctx);
        tty_set_echo(1);
        tty_set_echo_mask('\0');
        tty_show_prompt();
        size_t len = tty_readline(line, sizeof(line));
        if (len == 0) {
            continue;
        }
        int argc = shell_parse_line(line, argv, SHELL_MAX_ARGS);
        if (argc == 0) {
            continue;
        }

        const struct shell_command *command = shell_find_command(argv[0]);
        if (!command) {
            shell_print_error("comando desconhecido");
            shell_print("Use help-any para listar comandos.\n");
            continue;
        }
        if (command->handler(&ctx, argc, argv) != 0) {
            shell_suggest_help(command->name);
        }
    }

    enum shell_result result = shell_context_should_logout(&ctx) ? SHELL_RESULT_LOGOUT : SHELL_RESULT_EXIT;
    session_set_active(NULL);
    keyboard_set_help_callback(NULL);
    return result;
}
