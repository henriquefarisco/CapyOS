/*
 * src/shell/commands/system_control/capypkg_commands.c
 *
 * Shell-facing commands for the CapyOS package adapter (Etapa 9).
 * These are thin wrappers around `services/capypkg.h` so the same
 * operations driven by CapyAgent's UI can be invoked from `capysh`.
 *
 * Naming follows the convention used by other system_control TUs
 * (verb-noun, lowercase, dash-separated). The kernel never executes
 * the installed bytes from here: install operations only stage the
 * payload under `/var/capypkg/<name>/` after SHA-256 verification
 * and (when the source repo requires it) Ed25519 signature check.
 */

#include "internal/system_control_internal.h"

static const char *trilang(const char *language, const char *pt,
                           const char *en, const char *es) {
    return localization_select(language, pt, en, es);
}

static void print_entry_short(const struct capypkg_entry *entry) {
    shell_print(" - ");
    shell_print(entry->name);
    shell_print(" @ ");
    shell_print(entry->version);
    if (entry->source_repo[0]) {
        shell_print("  [");
        shell_print(entry->source_repo);
        shell_print("]");
    }
    shell_newline();
    if (entry->summary[0]) {
        shell_print("   ");
        shell_print(entry->summary);
        shell_newline();
    }
}

static void report_result(const char *language, int rc, const char *op) {
    if (rc == CAPYPKG_OK) {
        shell_print_ok(trilang(language, "operacao concluida",
                               "operation completed",
                               "operacion completada"));
        if (op && op[0]) {
            shell_print(" (");
            shell_print(op);
            shell_print(")");
        }
        shell_newline();
        return;
    }
    shell_print_error(capypkg_result_label(rc));
    if (op && op[0]) {
        shell_print(" (");
        shell_print(op);
        shell_print(")");
    }
    shell_newline();
}

int cmd_pkg_list(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    struct capypkg_stats stats;
    (void)ctx;
    if (shell_help_requested(argc, argv)) {
        shell_print(trilang(
            language,
            "Uso: pkg-list [--installed|--available]\nLista pacotes instalados ou disponiveis no catalogo.\n",
            "Usage: pkg-list [--installed|--available]\nLists installed packages or the available catalog.\n",
            "Uso: pkg-list [--installed|--available]\nLista paquetes instalados o disponibles en el catalogo.\n"));
        return 0;
    }

    capypkg_stats_get(&stats);
    if (!stats.initialized) {
        shell_print_error(trilang(language,
                                  "adaptador de pacotes nao inicializado",
                                  "package adapter not initialized",
                                  "adaptador de paquetes no inicializado"));
        shell_newline();
        return -1;
    }

    int want_installed = 1;
    int want_available = 1;
    if (argc >= 2) {
        if (shell_string_equal(argv[1], "--installed")) {
            want_available = 0;
        } else if (shell_string_equal(argv[1], "--available")) {
            want_installed = 0;
        }
    }

    if (want_installed) {
        shell_print(trilang(language, "Pacotes instalados:\n",
                            "Installed packages:\n",
                            "Paquetes instalados:\n"));
        if (capypkg_installed_count() == 0u) {
            shell_print(trilang(language, " (nenhum)\n", " (none)\n",
                                " (ninguno)\n"));
        }
        for (size_t i = 0u; i < capypkg_installed_count(); ++i) {
            struct capypkg_entry entry;
            if (capypkg_installed_get_at(i, &entry) == CAPYPKG_OK) {
                print_entry_short(&entry);
            }
        }
    }
    if (want_available) {
        shell_print(trilang(language, "Catalogo disponivel:\n",
                            "Available catalog:\n",
                            "Catalogo disponible:\n"));
        if (capypkg_available_count() == 0u) {
            shell_print(trilang(
                language,
                " (vazio; rode pkg-fetch para sincronizar)\n",
                " (empty; run pkg-fetch to sync)\n",
                " (vacio; ejecute pkg-fetch para sincronizar)\n"));
        }
        for (size_t i = 0u; i < capypkg_available_count(); ++i) {
            struct capypkg_entry entry;
            if (capypkg_available_get_at(i, &entry) == CAPYPKG_OK) {
                print_entry_short(&entry);
            }
        }
    }
    return 0;
}

int cmd_pkg_info(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    struct capypkg_entry entry;
    (void)ctx;
    if (shell_help_requested(argc, argv) || argc < 2) {
        shell_print(trilang(
            language,
            "Uso: pkg-info <nome>\nMostra metadados do pacote (instalado ou disponivel).\n",
            "Usage: pkg-info <name>\nShows metadata for a package (installed or available).\n",
            "Uso: pkg-info <nombre>\nMuestra metadatos del paquete (instalado o disponible).\n"));
        return argc < 2 ? -1 : 0;
    }
    int rc = capypkg_installed_get(argv[1], &entry);
    if (rc == CAPYPKG_ERR_NOT_FOUND) {
        rc = capypkg_available_get(argv[1], &entry);
    }
    if (rc != CAPYPKG_OK) {
        shell_print_error(capypkg_result_label(rc));
        shell_newline();
        return -1;
    }
    shell_print("name      : "); shell_print(entry.name); shell_newline();
    shell_print("version   : "); shell_print(entry.version); shell_newline();
    shell_print("state     : "); shell_print(capypkg_state_label(entry.state));
    shell_newline();
    if (entry.source_repo[0]) {
        shell_print("repo      : ");
        shell_print(entry.source_repo);
        shell_newline();
    }
    if (entry.summary[0]) {
        shell_print("summary   : ");
        shell_print(entry.summary);
        shell_newline();
    }
    shell_print("install   : "); shell_print(entry.install_root); shell_newline();
    shell_print("payload   : "); shell_print(entry.payload_url); shell_newline();
    shell_print("sha256    : "); shell_print(entry.payload_sha256); shell_newline();
    if (entry.signature_hex[0]) {
        shell_print("signature : ");
        shell_print(entry.signature_hex);
        shell_newline();
    }
    if (entry.size_bytes) {
        shell_print("size      : ");
        shell_print_number(entry.size_bytes);
        shell_newline();
    }
    if (entry.dep_count) {
        shell_print(trilang(language, "dependencias:\n",
                            "dependencies:\n",
                            "dependencias:\n"));
        for (uint32_t i = 0u; i < entry.dep_count; ++i) {
            shell_print(" - ");
            shell_print(entry.deps[i]);
            shell_newline();
        }
    }
    return 0;
}

int cmd_pkg_fetch(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    (void)ctx;
    if (shell_help_requested(argc, argv)) {
        shell_print(trilang(
            language,
            "Uso: pkg-fetch\nBaixa novamente o indice de pacotes de todos os repositorios configurados.\n",
            "Usage: pkg-fetch\nRe-downloads the package index from every configured repository.\n",
            "Uso: pkg-fetch\nDescarga el indice de paquetes de todos los repositorios configurados.\n"));
        return 0;
    }
    int rc = capypkg_fetch_index();
    report_result(language, rc, "pkg-fetch");
    return rc == CAPYPKG_OK ? 0 : -1;
}

int cmd_pkg_install(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    (void)ctx;
    if (shell_help_requested(argc, argv) || argc < 2) {
        shell_print(trilang(
            language,
            "Uso: pkg-install <nome>\nInstala um pacote verificando SHA-256 e assinatura.\n",
            "Usage: pkg-install <name>\nInstalls a package after SHA-256 and signature verification.\n",
            "Uso: pkg-install <nombre>\nInstala un paquete verificando SHA-256 y firma.\n"));
        return argc < 2 ? -1 : 0;
    }
    int rc = capypkg_install(argv[1]);
    report_result(language, rc, argv[1]);
    return rc == CAPYPKG_OK ? 0 : -1;
}

int cmd_pkg_remove(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    (void)ctx;
    if (shell_help_requested(argc, argv) || argc < 2) {
        shell_print(trilang(
            language,
            "Uso: pkg-remove <nome>\nRemove um pacote instalado e seu cache em /var/capypkg.\n",
            "Usage: pkg-remove <name>\nRemoves an installed package and its cache under /var/capypkg.\n",
            "Uso: pkg-remove <nombre>\nElimina un paquete instalado y su cache en /var/capypkg.\n"));
        return argc < 2 ? -1 : 0;
    }
    int rc = capypkg_remove(argv[1]);
    report_result(language, rc, argv[1]);
    return rc == CAPYPKG_OK ? 0 : -1;
}

int cmd_pkg_update(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    (void)ctx;
    if (shell_help_requested(argc, argv)) {
        shell_print(trilang(
            language,
            "Uso: pkg-update [<nome>]\nAtualiza um pacote especifico ou todos os pacotes instalados.\n",
            "Usage: pkg-update [<name>]\nUpdates a specific package or every installed package.\n",
            "Uso: pkg-update [<nombre>]\nActualiza un paquete o todos los paquetes instalados.\n"));
        return 0;
    }
    int rc;
    if (argc >= 2) {
        rc = capypkg_update(argv[1]);
    } else {
        rc = capypkg_update_all();
    }
    report_result(language, rc, argc >= 2 ? argv[1] : "all");
    return rc == CAPYPKG_OK ? 0 : -1;
}

int cmd_pkg_source_list(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    (void)ctx;
    if (shell_help_requested(argc, argv)) {
        shell_print(trilang(
            language,
            "Uso: pkg-source-list\nLista os repositorios capypkg configurados.\n",
            "Usage: pkg-source-list\nLists configured capypkg repositories.\n",
            "Uso: pkg-source-list\nLista los repositorios capypkg configurados.\n"));
        return 0;
    }
    size_t count = capypkg_repo_count();
    if (count == 0u) {
        shell_print(trilang(language,
                            "Nenhum repositorio configurado.\n",
                            "No repositories configured.\n",
                            "Ningun repositorio configurado.\n"));
        return 0;
    }
    for (size_t i = 0u; i < count; ++i) {
        struct capypkg_repo repo;
        if (capypkg_repo_get_at(i, &repo) != CAPYPKG_OK) continue;
        shell_print(" - ");
        shell_print(repo.name);
        shell_print("  ");
        shell_print(repo.index_url);
        if (repo.pinned) {
            shell_print("  (pinned)");
        }
        if (repo.require_signature) {
            shell_print("  (signed)");
        }
        shell_newline();
    }
    return 0;
}

int cmd_pkg_source_add(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    (void)ctx;
    if (shell_help_requested(argc, argv) || argc < 3) {
        shell_print(trilang(
            language,
            "Uso: pkg-source-add <nome> <https-url> [--unsigned]\nAdiciona um repositorio capypkg. Por padrao exige assinatura.\n",
            "Usage: pkg-source-add <name> <https-url> [--unsigned]\nAdds a capypkg repository. Defaults to requiring signatures.\n",
            "Uso: pkg-source-add <nombre> <https-url> [--unsigned]\nAgrega un repositorio capypkg. Por defecto exige firma.\n"));
        return argc < 3 ? -1 : 0;
    }
    int require_sig = 1;
    if (argc >= 4 && shell_string_equal(argv[3], "--unsigned")) {
        require_sig = 0;
    }
    int rc = capypkg_repo_add(argv[1], argv[2], require_sig);
    report_result(language, rc, argv[1]);
    return rc == CAPYPKG_OK ? 0 : -1;
}

int cmd_pkg_source_remove(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    (void)ctx;
    if (shell_help_requested(argc, argv) || argc < 2) {
        shell_print(trilang(
            language,
            "Uso: pkg-source-remove <nome>\nRemove um repositorio capypkg configurado (pinned nao pode ser removido).\n",
            "Usage: pkg-source-remove <name>\nRemoves a configured capypkg repository (pinned ones are protected).\n",
            "Uso: pkg-source-remove <nombre>\nElimina un repositorio capypkg configurado (los fijos estan protegidos).\n"));
        return argc < 2 ? -1 : 0;
    }
    int rc = capypkg_repo_remove(argv[1]);
    report_result(language, rc, argv[1]);
    return rc == CAPYPKG_OK ? 0 : -1;
}

/* ── pkg-bootstrap: install-profile-driven module bootstrap ──────────────
 *
 * Thin shell wrapper around `capypkg_bootstrap_run` from
 * `services/capypkg_bootstrap.h`. The bridge logic lives in
 * `src/services/capypkg_bootstrap.c` so it can be called from both
 * the shell and the kernel auto-bootstrap hook.
 */

int cmd_pkg_bootstrap(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    (void)ctx;
    if (shell_help_requested(argc, argv)) {
        shell_print(trilang(
            language,
            "Uso: pkg-bootstrap [--force]\nLê /system/install/profile.ini e instala os modulos remotos quando profile=full ou custom.\n",
            "Usage: pkg-bootstrap [--force]\nReads /system/install/profile.ini and installs remote modules when profile=full or custom.\n",
            "Uso: pkg-bootstrap [--force]\nLee /system/install/profile.ini e instala los modulos remotos cuando profile=full o custom.\n"));
        return 0;
    }

    int force = 0;
    if (argc >= 2 && shell_string_equal(argv[1], "--force")) {
        force = 1;
    }

    int installed = 0;
    int failed = 0;
    int rc = capypkg_bootstrap_run(force, &installed, &failed);

    if (rc == INSTALL_PROFILE_OK && installed == 0 && failed == 0) {
        shell_print(trilang(
            language,
            "pkg-bootstrap: nada para instalar (profile=basic ou catalogo vazio).\n",
            "pkg-bootstrap: nothing to install (profile=basic or empty catalog).\n",
            "pkg-bootstrap: nada que instalar (profile=basic o catalogo vacio).\n"));
        return 0;
    }

    if (rc == INSTALL_PROFILE_ERR_NOT_READY) {
        shell_print_error(trilang(language,
                                  "adaptador de pacotes nao inicializado",
                                  "package adapter not initialized",
                                  "adaptador de paquetes no inicializado"));
        shell_newline();
        return -1;
    }
    if (rc == INSTALL_PROFILE_ERR_DENIED ||
        rc == INSTALL_PROFILE_ERR_PARSE ||
        rc == INSTALL_PROFILE_ERR_MISSING_FIELD ||
        rc == INSTALL_PROFILE_ERR_INVALID_ARG) {
        shell_print_error(trilang(
            language,
            "profile.ini invalido: ",
            "invalid profile.ini: ",
            "profile.ini invalido: "));
        shell_print(install_profile_result_label(rc));
        shell_newline();
        return -1;
    }

    shell_print(trilang(language,
                        "pkg-bootstrap concluido. instalados=",
                        "pkg-bootstrap completed. installed=",
                        "pkg-bootstrap completado. instalados="));
    shell_print_number((uint32_t)installed);
    shell_print(trilang(language, " falhas=", " failures=", " fallos="));
    shell_print_number((uint32_t)failed);
    shell_newline();
    return rc == INSTALL_PROFILE_OK ? 0 : -1;
}
