#include "shell/commands.h"
#include "shell/core.h"

#include "core/localization.h"
#include "core/service_manager.h"
#include "core/version.h"
#include "drivers/timer/pit.h"

static int cmd_print_me(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    if (shell_help_requested(argc, argv)) {
        shell_print(localization_select(
            language, "Uso: print-me\nMostra o usuario autenticado na sessao.\n",
            "Usage: print-me\nShows the authenticated user in the session.\n",
            "Uso: print-me\nMuestra el usuario autenticado en la sesion.\n"));
        return 0;
    }
    (void)argc;
    (void)argv;
    const struct user_record *user = session_user(ctx->session);
    shell_print(user ? user->username
                     : localization_select(language, "desconhecido", "unknown",
                                           "desconocido"));
    shell_newline();
    return 0;
}

static int cmd_print_id(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    if (shell_help_requested(argc, argv)) {
        shell_print(localization_select(
            language, "Uso: print-id\nMostra UID e GID do usuario atual.\n",
            "Usage: print-id\nShows UID and GID for the current user.\n",
            "Uso: print-id\nMuestra UID y GID del usuario actual.\n"));
        return 0;
    }
    (void)argc;
    (void)argv;
    const struct user_record *user = session_user(ctx->session);
    if (!user) {
        return -1;
    }
    shell_print("uid=");
    shell_print_number(user->uid);
    shell_print(" gid=");
    shell_print_number(user->gid);
    shell_print(" role=");
    shell_print(user->role);
    shell_newline();
    return 0;
}

static int cmd_print_host(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    if (shell_help_requested(argc, argv)) {
        shell_print(localization_select(
            language,
            "Uso: print-host\nExibe o hostname configurado no sistema.\n",
            "Usage: print-host\nShows the configured system hostname.\n",
            "Uso: print-host\nMuestra el hostname configurado del sistema.\n"));
        return 0;
    }
    (void)argc;
    (void)argv;
    shell_print(ctx->settings ? ctx->settings->hostname : "capyos");
    shell_newline();
    return 0;
}

static int cmd_print_version(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    if (shell_help_requested(argc, argv)) {
        shell_print(localization_select(
            language, "Uso: print-version\nMostra a versao do CapyOS.\n",
            "Usage: print-version\nShows the CapyOS version.\n",
            "Uso: print-version\nMuestra la version de CapyOS.\n"));
        return 0;
    }
    (void)ctx;
    (void)argc;
    (void)argv;
    shell_print("CapyOS ");
    shell_print(CAPYOS_VERSION_EXTENDED);
    shell_print(" [");
    shell_print(CAPYOS_VERSION_CHANNEL);
    shell_print("]\n");
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
    const char *language = shell_current_language();
    if (shell_help_requested(argc, argv)) {
        shell_print(localization_select(
            language,
            "Uso: print-time\nMostra o horario atual (simulado) desde o boot.\n",
            "Usage: print-time\nShows the current simulated time since boot.\n",
            "Uso: print-time\nMuestra la hora actual (simulada) desde el arranque.\n"));
        return 0;
    }
    (void)ctx;
    (void)argc;
    (void)argv;
    uint32_t seconds = shell_uptime_seconds();
    uint32_t simulated = seconds % (24u * 3600u);
    char buffer[16];
    format_hms(simulated, buffer, sizeof(buffer));
    shell_print(localization_select(language, "hora atual (simulada) ",
                                    "current time (simulated) ",
                                    "hora actual (simulada) "));
    shell_print(buffer);
    shell_print(" (HH:MM:SS)\n");
    return 0;
}

static int cmd_print_insomnia(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    if (shell_help_requested(argc, argv)) {
        shell_print(localization_select(
            language,
            "Uso: print-insomnia\nMostra o tempo total de atividade do sistema.",
            "Usage: print-insomnia\nShows the total system uptime.",
            "Uso: print-insomnia\nMuestra el tiempo total de actividad del sistema."));
        shell_newline();
        return 0;
    }
    (void)ctx;
    (void)argc;
    (void)argv;
    uint32_t seconds = shell_uptime_seconds();
    char buffer[16];
    format_hms(seconds, buffer, sizeof(buffer));
    shell_print("uptime ");
    shell_print(buffer);
    shell_print(" (HH:MM:SS)\n");
    return 0;
}

static int cmd_print_envs(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    if (shell_help_requested(argc, argv)) {
        shell_print(localization_select(
            language,
            "Uso: print-envs\nExibe variaveis basicas da sessao (USER, HOME, HOST, etc.).\n",
            "Usage: print-envs\nShows basic session variables (USER, HOME, HOST, etc.).\n",
            "Uso: print-envs\nMuestra variables basicas de la sesion (USER, HOME, HOST, etc.).\n"));
        return 0;
    }
    (void)argc;
    (void)argv;
    const struct user_record *user = session_user(ctx->session);
    shell_print("USER=");
    shell_print(user ? user->username : "");
    shell_newline();
    shell_print("ROLE=");
    shell_print(user ? user->role : "");
    shell_newline();
    shell_print("UID=");
    shell_print_number(user ? user->uid : 0);
    shell_newline();
    shell_print("GID=");
    shell_print_number(user ? user->gid : 0);
    shell_newline();
    shell_print("HOME=");
    shell_print(user ? user->home : "/");
    shell_newline();
    shell_print("PWD=");
    shell_print(session_cwd(ctx->session));
    shell_newline();
    shell_print("HOST=");
    shell_print(ctx->settings ? ctx->settings->hostname : "capyos");
    shell_newline();
    shell_print("LANG=");
    shell_print(session_language(ctx->session));
    shell_newline();
    shell_print("CHANNEL=");
    shell_print(CAPYOS_VERSION_CHANNEL);
    shell_newline();
    shell_print("VERSION=");
    shell_print(CAPYOS_VERSION_EXTENDED);
    shell_newline();
    shell_print("VERSION_FULL=");
    shell_print(CAPYOS_VERSION_FULL);
    shell_newline();
    shell_print("PATH=/bin:/system\n");
    return 0;
}

static int service_matches_filter(const char *filter, const char *name) {
    return !filter || !filter[0] || shell_string_equal(filter, name);
}

static void shell_print_service_dependencies(uint32_t dependency_mask) {
    int first = 1;
    size_t count = service_manager_count();

    if (dependency_mask == 0u) {
        shell_print("-");
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        struct system_service_status svc;
        if (service_manager_get_at(i, &svc) != 0) {
            continue;
        }
        if ((dependency_mask & (1u << svc.id)) == 0u) {
            continue;
        }
        if (!first) {
            shell_print(",");
        }
        shell_print(svc.name);
        first = 0;
    }
}

static int cmd_service_status(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    const char *filter = NULL;
    size_t count = 0;

    (void)ctx;
    if (shell_help_requested(argc, argv)) {
        shell_print(localization_select(
            language,
            "Uso: service-status [nome]\nMostra o estado dos servicos internos do sistema.\n",
            "Usage: service-status [name]\nShows the state of the internal system services.\n",
            "Uso: service-status [nombre]\nMuestra el estado de los servicios internos del sistema.\n"));
        return 0;
    }

    if (argc >= 2) {
        filter = argv[1];
    }

    if (!filter || !filter[0]) {
        struct system_service_target_status target;
        if (service_manager_target_current(&target) == 0) {
            shell_print("target=");
            shell_print(target.name);
            if (ctx && ctx->settings && ctx->settings->service_target[0]) {
                shell_print(" saved=");
                shell_print(ctx->settings->service_target);
            }
            shell_print(" mask=");
            shell_print_number(target.service_mask);
            shell_newline();
        }
    }

    count = service_manager_count();
    for (size_t i = 0; i < count; ++i) {
        struct system_service_status svc;
        if (service_manager_get_at(i, &svc) != 0) {
            continue;
        }
        if (!service_matches_filter(filter, svc.name)) {
            continue;
        }
        shell_print(svc.name);
        shell_print(" state=");
        shell_print(service_manager_state_label(svc.state));
        shell_print(" startup=");
        shell_print(service_manager_startup_label(svc.startup));
        shell_print(" critical=");
        shell_print(svc.critical ? "yes" : "no");
        shell_print(" rc=");
        if (svc.last_result < 0) {
            shell_print("-");
            shell_print_number((uint32_t)(-svc.last_result));
        } else {
            shell_print_number((uint32_t)svc.last_result);
        }
        shell_print(" transitions=");
        shell_print_number(svc.transitions);
        shell_print(" polls=");
        shell_print_number(svc.polls);
        shell_print(" every=");
        if (svc.poll_interval_ticks == 0u) {
            shell_print("loop");
        } else {
            shell_print_number(svc.poll_interval_ticks);
        }
        shell_print(" failures=");
        shell_print_number(svc.failures);
        shell_print(" restarts=");
        shell_print_number(svc.restarts);
        shell_print(" backoff=");
        shell_print_number(svc.backoff_ticks);
        shell_print(" retry=");
        shell_print_number(svc.restart_limit);
        shell_newline();
        shell_print("  deps=");
        shell_print_service_dependencies(svc.dependency_mask);
        shell_print(" summary=");
        shell_print(svc.summary[0] ? svc.summary : "(no summary)");
        shell_newline();
    }
    return 0;
}

static struct shell_command g_system_info_commands[8];
static int g_system_info_commands_initialized = 0;

static void init_system_info_commands(void) {
    if (g_system_info_commands_initialized) {
        return;
    }
    g_system_info_commands[0].name = "print-me";
    g_system_info_commands[0].handler = cmd_print_me;
    g_system_info_commands[1].name = "print-id";
    g_system_info_commands[1].handler = cmd_print_id;
    g_system_info_commands[2].name = "print-host";
    g_system_info_commands[2].handler = cmd_print_host;
    g_system_info_commands[3].name = "print-version";
    g_system_info_commands[3].handler = cmd_print_version;
    g_system_info_commands[4].name = "print-time";
    g_system_info_commands[4].handler = cmd_print_time;
    g_system_info_commands[5].name = "print-insomnia";
    g_system_info_commands[5].handler = cmd_print_insomnia;
    g_system_info_commands[6].name = "print-envs";
    g_system_info_commands[6].handler = cmd_print_envs;
    g_system_info_commands[7].name = "service-status";
    g_system_info_commands[7].handler = cmd_service_status;
    g_system_info_commands_initialized = 1;
}

const struct shell_command *shell_commands_system_info(size_t *count) {
    init_system_info_commands();
    if (count) {
        *count = 8;
    }
    return g_system_info_commands;
}
