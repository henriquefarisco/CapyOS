/*
 * src/shell/commands/system_control/capy_command.c — alpha.241
 *
 * Unified `capy` command for module + wizard management.
 *
 * Subcommands (all tri-lingual via the existing locale gate):
 *   capy install <module>      thin wrapper around pkg-install
 *   capy module list           thin wrapper around pkg-list
 *   capy module status [name]  thin wrapper around pkg-info / pkg-list
 *   capy wizard                re-run the first-boot wizard
 *   capy wizard --modules      re-run ONLY the module-selection step
 *   capy update                pkg-fetch + status summary
 *   capy help                  built-in help
 *
 * The subcommands forward into the existing `pkg-*` handlers so the
 * underlying behaviour, audit log lines and error codes are shared.
 * The wizard re-run path unlinks `/system/install/bootstrap.done`
 * before any module retry, and unlinks `/system/first-run.done` when
 * `--modules` is omitted, then
 * calls `system_run_first_boot_setup()` which delegates to the same
 * TUI used on first boot.
 *
 * This module deliberately keeps state in the caller-provided
 * shell_context; it does NOT cache anything between invocations.
 */

#include "internal/system_control_internal.h"

#include "core/system_init.h"

static int capy_streq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (*a != *b) return 0;
        ++a; ++b;
    }
    return *a == *b;
}

static int capy_starts_with(const char *s, const char *prefix) {
    if (!s || !prefix) return 0;
    while (*prefix) {
        if (*s != *prefix) return 0;
        ++s; ++prefix;
    }
    return 1;
}

static void capy_print_help(struct shell_context *ctx) {
    (void)ctx;
    const char *language = shell_current_language();
    shell_print(localization_select(language,
        "capy <subcomando> [opcoes]\n"
        "  install <nome>        instala um modulo via capypkg\n"
        "  module list           lista modulos instalados\n"
        "  module status [nome]  detalhes do modulo (ou catalogo)\n"
        "  wizard                re-executa o assistente de primeiro boot\n"
        "  wizard --modules      re-executa apenas a etapa de modulos\n"
        "  update                atualiza indice e estado dos modulos\n"
        "  help                  esta mensagem\n",
        "capy <subcommand> [options]\n"
        "  install <name>        install a module via capypkg\n"
        "  module list           list installed modules\n"
        "  module status [name]  module detail (or catalog)\n"
        "  wizard                re-run the first-boot wizard\n"
        "  wizard --modules      re-run only the module-selection step\n"
        "  update                refresh module index and status\n"
        "  help                  this message\n",
        "capy <subcomando> [opciones]\n"
        "  install <nombre>      instala un modulo via capypkg\n"
        "  module list           lista modulos instalados\n"
        "  module status [nombre] detalle del modulo (o catalogo)\n"
        "  wizard                vuelve a ejecutar el asistente inicial\n"
        "  wizard --modules      vuelve a ejecutar solo la etapa de modulos\n"
        "  update                refresca indice y estado de modulos\n"
        "  help                  este mensaje\n"));
}

/* ── wizard re-run helpers ──────────────────────────────────────────── */

static int capy_unlink_marker(const char *path) {
    struct vfs_stat st;
    struct session_context *previous_session = session_active();
    int rc = 0;
    session_set_active(NULL);
    if (vfs_stat_path(path, &st) != 0) {
        goto done; /* not present is OK */
    }
    rc = (st.mode & VFS_MODE_DIR) ? vfs_rmdir(path) : vfs_unlink(path);
done:
    session_set_active(previous_session);
    return rc;
}

static int capy_wizard_rerun(struct shell_context *ctx, int modules_only) {
    const char *language = shell_current_language();
    struct session_context *previous_session = NULL;
    int rc = 0;
    (void)ctx;

    /* Always clear the per-bootstrap marker so the next bootstrap
     * sweep runs from scratch. The first-run.done marker is only
     * cleared when the operator asked for the full wizard. */
    if (capy_unlink_marker(INSTALL_PROFILE_BOOTSTRAP_DONE) != 0) {
        shell_print_error(localization_select(language,
            "nao consegui limpar o marker do bootstrap; tente novamente\n",
            "could not clear bootstrap marker; try again\n",
            "no fue posible borrar el marker del bootstrap; intenta de nuevo\n"));
        return -1;
    }

    if (modules_only) {
        /* Drive only capypkg_bootstrap_run with force=1; the user
         * already provided a profile.ini we trust. */
        int installed = 0;
        int failed = 0;
        previous_session = session_active();
        session_set_active(NULL);
        rc = capypkg_bootstrap_run(1, &installed, &failed);
        session_set_active(previous_session);
        if (rc == INSTALL_PROFILE_OK) {
            shell_print_ok(localization_select(language,
                "bootstrap de modulos concluido\n",
                "module bootstrap complete\n",
                "bootstrap de modulos completo\n"));
            return 0;
        }
        if (rc == INSTALL_PROFILE_ERR_STORAGE) {
            shell_print_error(localization_select(language,
                "bootstrap de modulos incompleto; o sistema tentara novamente. instalados=",
                "module bootstrap incomplete; the system will retry. installed=",
                "bootstrap de modulos incompleto; el sistema reintentara. instalados="));
            shell_print_number((uint32_t)installed);
            shell_print(localization_select(language, " falhas=", " failures=", " fallos="));
            shell_print_number((uint32_t)failed);
            shell_newline();
            return -1;
        }
        shell_print_error(localization_select(language,
            "bootstrap de modulos falhou; veja klog [audit] [capypkg]\n",
            "module bootstrap failed; check klog [audit] [capypkg]\n",
            "el bootstrap de modulos fallo; revisa klog [audit] [capypkg]\n"));
        return -1;
    }

    /* Full wizard: drop first-run.done and re-run the TUI. */
    if (capy_unlink_marker("/system/first-run.done") != 0) {
        shell_print_error(localization_select(language,
            "nao consegui limpar /system/first-run.done\n",
            "could not clear /system/first-run.done\n",
            "no fue posible borrar /system/first-run.done\n"));
        return -1;
    }
    shell_print(localization_select(language,
        "Reabrindo assistente de primeiro boot...\n",
        "Reopening first-boot wizard...\n",
        "Reabriendo asistente inicial...\n"));
    previous_session = session_active();
    session_set_active(NULL);
    rc = system_run_first_boot_setup();
    session_set_active(previous_session);
    return (rc == 0) ? 0 : -1;
}

/* ── module subcommand ──────────────────────────────────────────────── */

static int capy_module_subcommand(struct shell_context *ctx, int argc,
                                  char **argv) {
    const char *language = shell_current_language();
    if (argc < 1) {
        shell_print_error(localization_select(language,
            "uso: capy module <list|status [nome]>\n",
            "usage: capy module <list|status [name]>\n",
            "uso: capy module <list|status [nombre]>\n"));
        return -1;
    }
    if (capy_streq(argv[0], "list")) {
        char *forward_argv[2];
        forward_argv[0] = (char *)"pkg-list";
        forward_argv[1] = (char *)"--installed";
        return cmd_pkg_list(ctx, 2, forward_argv);
    }
    if (capy_streq(argv[0], "status")) {
        if (argc >= 2) {
            char *forward_argv[2];
            forward_argv[0] = (char *)"pkg-info";
            forward_argv[1] = argv[1];
            return cmd_pkg_info(ctx, 2, forward_argv);
        }
        char *forward_argv[2];
        forward_argv[0] = (char *)"pkg-list";
        forward_argv[1] = (char *)"--available";
        return cmd_pkg_list(ctx, 2, forward_argv);
    }
    shell_print_error(localization_select(language,
        "subcomando module desconhecido\n",
        "unknown module subcommand\n",
        "subcomando module desconocido\n"));
    return -1;
}

/* ── update subcommand ──────────────────────────────────────────────── */

static int capy_update_subcommand(struct shell_context *ctx) {
    char *fetch_argv[1];
    fetch_argv[0] = (char *)"pkg-fetch";
    return cmd_pkg_fetch(ctx, 1, fetch_argv);
}

/* ── install subcommand ─────────────────────────────────────────────── */

static int capy_install_subcommand(struct shell_context *ctx, int argc,
                                   char **argv) {
    const char *language = shell_current_language();
    if (argc < 1) {
        shell_print_error(localization_select(language,
            "uso: capy install <nome-do-modulo>\n",
            "usage: capy install <module-name>\n",
            "uso: capy install <nombre-del-modulo>\n"));
        return -1;
    }
    char *forward_argv[2];
    forward_argv[0] = (char *)"pkg-install";
    forward_argv[1] = argv[0];
    return cmd_pkg_install(ctx, 2, forward_argv);
}

/* ── top-level dispatcher ───────────────────────────────────────────── */

int cmd_capy(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    if (argc < 2) {
        capy_print_help(ctx);
        return 0;
    }
    const char *sub = argv[1];

    if (capy_streq(sub, "help") || capy_streq(sub, "--help") ||
        capy_streq(sub, "-h")) {
        capy_print_help(ctx);
        return 0;
    }
    if (capy_streq(sub, "install")) {
        return capy_install_subcommand(ctx, argc - 2, argv + 2);
    }
    if (capy_streq(sub, "module")) {
        return capy_module_subcommand(ctx, argc - 2, argv + 2);
    }
    if (capy_streq(sub, "update")) {
        return capy_update_subcommand(ctx);
    }
    if (capy_streq(sub, "wizard")) {
        int modules_only = 0;
        for (int i = 2; i < argc; ++i) {
            if (capy_starts_with(argv[i], "--modules")) {
                modules_only = 1;
            }
        }
        return capy_wizard_rerun(ctx, modules_only);
    }
    shell_print_error(localization_select(language,
        "subcomando capy desconhecido; use `capy help`\n",
        "unknown capy subcommand; use `capy help`\n",
        "subcomando capy desconocido; usa `capy help`\n"));
    return -1;
}
