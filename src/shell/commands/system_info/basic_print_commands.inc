#include "shell/commands.h"
#include "shell/core.h"

#include "lang/localization.h"
#include "services/service_boot_policy.h"
#include "services/service_manager.h"
#include "services/update_agent.h"
#include "auth/user.h"
#include "core/version.h"
#include "core/work_queue.h"
#include "drivers/timer/pit.h"
#if defined(__x86_64__)
#include "arch/x86_64/kernel_runtime_control.h"
#include "arch/x86_64/storage_runtime.h"
#include "net/stack.h"
#endif
#include "fs/capyfs.h"
#include "fs/vfs.h"
#include "memory/kmem.h"

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
    struct system_update_status status;
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
    shell_print("UPDATE_CHANNEL=");
    shell_print((ctx && ctx->settings && ctx->settings->update_channel[0])
                    ? ctx->settings->update_channel
                    : "stable");
    shell_newline();
    shell_print("UPDATE_BRANCH=");
    shell_print((ctx && ctx->settings &&
                 shell_string_equal(ctx->settings->update_channel, "develop"))
                    ? "develop"
                    : "main");
    shell_newline();
    update_agent_status_get(&status);
    shell_print("UPDATE_REMOTE_MANIFEST=");
    shell_print(status.remote_manifest_url[0] ? status.remote_manifest_url : "-");
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

