static int diag_log_append(const char *msg)
{
    if (!msg || !msg[0]) {
        return 0;
    }
    klog(KLOG_INFO, msg);
    return 0;
}

static void diag_log_segments(const char *a, const char *b, const char *c,
                              const char *d, const char *e)
{
    char line[192];
    line[0] = '\0';
    if (a) shell_append_text(line, sizeof(line), a);
    if (b) shell_append_text(line, sizeof(line), b);
    if (c) shell_append_text(line, sizeof(line), c);
    if (d) shell_append_text(line, sizeof(line), d);
    if (e) shell_append_text(line, sizeof(line), e);
    (void)diag_log_append(line);
}

static void cli_log_process_begin(const char *name)
{
    diag_log_segments("[diag] Iniciando ", name, "...", NULL, NULL);
}

static void cli_log_process_begin_success(const char *name)
{
    diag_log_segments("[diag] Processo ", name, " iniciado.", NULL, NULL);
}

static void cli_log_process_progress(const char *name)
{
    diag_log_segments("[diag] ", name, " em andamento...", NULL, NULL);
}

static void cli_log_process_conclude(const char *name)
{
    diag_log_segments("[diag] Finalizando ", name, "...", NULL, NULL);
}

static void cli_log_process_finalize(const char *name)
{
    diag_log_segments("[diag] Processo ", name, " concluido.", NULL, NULL);
}

static void cli_log_process_finalize_success(const char *name)
{
    diag_log_segments("[diag] Processo ", name,
                      " finalizado com sucesso.", NULL, NULL);
}

static void cli_log_process_error(const char *name, const char *reason)
{
    diag_log_segments("[erro] Processo ", name, " falhou",
                      reason ? ": " : "", reason ? reason : "");
}

static void cli_log_dependency_wait(const char *dependency, const char *target)
{
    diag_log_segments("Aguardando processo ", dependency,
                      " para iniciar o processo ", target, "...");
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

