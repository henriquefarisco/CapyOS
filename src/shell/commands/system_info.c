#include "shell/commands.h"
#include "shell/core.h"

#include "core/version.h"
#include "drivers/timer/pit.h"

static int cmd_print_me(struct shell_context *ctx, int argc, char **argv) {
    if (shell_help_requested(argc, argv)) {
        shell_print("Uso: print-me\nMostra o usuario autenticado na sessao.\n");
        return 0;
    }
    (void)argc;
    (void)argv;
    const struct user_record *user = session_user(ctx->session);
    shell_print(user ? user->username : "desconhecido");
    shell_newline();
    return 0;
}

static int cmd_print_id(struct shell_context *ctx, int argc, char **argv) {
    if (shell_help_requested(argc, argv)) {
        shell_print("Uso: print-id\nMostra UID e GID do usuario atual.\n");
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
    if (shell_help_requested(argc, argv)) {
        shell_print("Uso: print-host\nExibe o hostname configurado no sistema.\n");
        return 0;
    }
    (void)argc;
    (void)argv;
    shell_print(ctx->settings ? ctx->settings->hostname : "capyos");
    shell_newline();
    return 0;
}

static int cmd_print_version(struct shell_context *ctx, int argc, char **argv) {
    if (shell_help_requested(argc, argv)) {
        shell_print("Uso: print-version\nMostra a versao do CapyOS.\n");
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
    if (shell_help_requested(argc, argv)) {
        shell_print("Uso: print-time\nMostra o horario atual (simulado) desde o boot.\n");
        return 0;
    }
    (void)ctx;
    (void)argc;
    (void)argv;
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
    if (shell_help_requested(argc, argv)) {
        shell_print("Uso: print-insomnia\nMostra o tempo total de atividade do sistema.");
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
    if (shell_help_requested(argc, argv)) {
        shell_print("Uso: print-envs\nExibe variaveis basicas da sessao (USER, HOME, HOST, etc.).\n");
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

static struct shell_command g_system_info_commands[7];
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
    g_system_info_commands_initialized = 1;
}

const struct shell_command *shell_commands_system_info(size_t *count) {
    init_system_info_commands();
    if (count) {
        *count = 7;
    }
    return g_system_info_commands;
}
