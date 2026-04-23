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
