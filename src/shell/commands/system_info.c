#include "shell/commands.h"
#include "shell/core.h"

#include "core/localization.h"
#include "core/service_boot_policy.h"
#include "core/service_manager.h"
#include "core/update_agent.h"
#include "core/user.h"
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

static int work_matches_filter(const char *filter, const char *name) {
    return !filter || !filter[0] || shell_string_equal(filter, name);
}

static void shell_print_signed_result(int32_t value) {
    if (value < 0) {
        shell_print("-");
        shell_print_number((uint32_t)(-value));
        return;
    }
    shell_print_number((uint32_t)value);
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

static void shell_print_bool_flag(const char *label, int value) {
    shell_print(label);
    shell_print(value ? "yes" : "no");
}

static void shell_print_path_status(const char *path) {
    struct vfs_stat st;
    int rc = vfs_stat_path(path, &st);
    shell_print(path);
    shell_print(" exists=");
    shell_print(rc == 0 ? "yes" : "no");
    if (rc == 0) {
        shell_print(" type=");
        shell_print((st.mode & VFS_MODE_DIR) ? "dir" : "file");
        shell_print(" size=");
        shell_print_number(st.size);
    }
    shell_newline();
}

static int shell_recovery_capyfs_check(struct capyfs_check_report *out) {
    struct super_block *root = vfs_root();
    if (!root || !root->bdev || !out) {
        return -1;
    }
    return capyfs_check(root->bdev, out);
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
        shell_print_signed_result(svc.last_result);
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

static int cmd_job_status(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    const char *filter = NULL;
    size_t count = 0;
    (void)ctx;

    if (shell_help_requested(argc, argv)) {
        shell_print(localization_select(
            language,
            "Uso: job-status [nome]\nMostra o estado dos jobs internos do kernel/work queue.\n",
            "Usage: job-status [name]\nShows the state of the internal kernel/work queue jobs.\n",
            "Uso: job-status [nombre]\nMuestra el estado de los jobs internos del kernel/work queue.\n"));
        return 0;
    }

    if (argc >= 2) {
        filter = argv[1];
    }

    count = work_queue_count();
    for (size_t i = 0; i < count; ++i) {
        struct system_work_status work;
        if (work_queue_get_at(i, &work) != 0) {
            continue;
        }
        if (!work_matches_filter(filter, work.name)) {
            continue;
        }
        shell_print(work.name);
        shell_print(" state=");
        shell_print(work_queue_state_label(work.state));
        shell_print(" rc=");
        shell_print_signed_result(work.last_result);
        shell_print(" runs=");
        shell_print_number(work.runs);
        shell_print(" failures=");
        shell_print_number(work.failures);
        shell_print(" every=");
        if (work.interval_ticks == 0u) {
            shell_print("manual");
        } else {
            shell_print_number(work.interval_ticks);
        }
        shell_print(" next=");
        if (work.state == SYSTEM_WORK_STATE_DISABLED) {
            shell_print("-");
        } else {
            shell_print_number((uint32_t)work.next_due_tick);
        }
        shell_newline();
        shell_print("  summary=");
        shell_print(work.summary[0] ? work.summary : "(no summary)");
        shell_newline();
    }
    return 0;
}

static int cmd_update_status(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    struct system_update_status status;
    (void)ctx;

    if (shell_help_requested(argc, argv)) {
        shell_print(localization_select(
            language,
            "Uso: update-status\nMostra o estado atual do catalogo local, do staging persistente e do update-agent.\n",
            "Usage: update-status\nShows the current state of the local catalog, persistent staging and update agent.\n",
            "Uso: update-status\nMuestra el estado actual del catalogo local, del staging persistente y del update-agent.\n"));
        return 0;
    }

    update_agent_status_get(&status);
    shell_print("configured=");
    shell_print(status.configured ? "yes" : "no");
    shell_print(" catalog=");
    shell_print(status.catalog_present ? "present" : "missing");
    shell_print(" update=");
    shell_print(status.update_available ? "available" : "none");
    shell_print(" stage=");
    shell_print(status.stage_ready ? "ready" : "empty");
    shell_print(" pending=");
    shell_print(status.pending_activation ? "armed" : "no");
    shell_print(" rc=");
    shell_print_signed_result(status.last_result);
    shell_newline();

    shell_print("channel=");
    shell_print(status.channel[0] ? status.channel : "-");
    shell_print(" branch=");
    shell_print(status.branch[0] ? status.branch : "-");
    shell_print(" source=");
    shell_print(status.source[0] ? status.source : "-");
    shell_newline();

    shell_print("current=");
    shell_print(status.current_version[0] ? status.current_version : "-");
    shell_print(" available=");
    shell_print(status.available_version[0] ? status.available_version : "-");
    if (status.published_at[0]) {
        shell_print(" published=");
        shell_print(status.published_at);
    }
    shell_newline();

    shell_print("manifest=");
    shell_print(status.manifest_path[0] ? status.manifest_path : "-");
    shell_newline();

    shell_print("staged=");
    shell_print(status.staged_version[0] ? status.staged_version : "-");
    shell_print(" staged-manifest=");
    shell_print(status.staged_manifest_path[0] ? status.staged_manifest_path : "-");
    shell_newline();

    shell_print("summary=");
    shell_print(status.summary[0] ? status.summary : "(no summary)");
    shell_newline();
    return 0;
}

static int cmd_update_history(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    char *content = NULL;
    size_t content_len = 0;
    (void)ctx;
    if (shell_help_requested(argc, argv)) {
        shell_print(localization_select(
            language,
            "Uso: update-history\nExibe o historico persistido de eventos do update-agent gravado em /var/log/update-history.log.\n",
            "Usage: update-history\nShows the persisted history of update-agent events stored in /var/log/update-history.log.\n",
            "Uso: update-history\nMuestra el historial persistido de eventos del update-agent guardado en /var/log/update-history.log.\n"));
        return 0;
    }

    if (shell_read_file("/var/log/update-history.log", &content, &content_len) != 0 ||
        !content || content_len == 0) {
        shell_print_error(localization_select(
            language,
            "nenhum historico persistido de update foi encontrado em /var/log/update-history.log",
            "no persisted update history was found in /var/log/update-history.log",
            "no se encontro ningun historial persistido de update en /var/log/update-history.log"));
        return -1;
    }

    shell_paginate_content(content);
    kfree(content);
    return 0;
}

static int cmd_recovery_status(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    (void)ctx;
    if (shell_help_requested(argc, argv)) {
        shell_print(localization_select(
            language,
            "Uso: recovery-status\nMostra o estado atual do modo de recuperacao, alvo de boot e diagnosticos basicos de storage/rede.\n",
            "Usage: recovery-status\nShows the current recovery mode state, boot target and basic storage/network diagnostics.\n",
            "Uso: recovery-status\nMuestra el estado actual del modo de recuperacion, el objetivo de arranque y diagnosticos basicos de almacenamiento/red.\n"));
        return 0;
    }
#if !defined(__x86_64__)
    shell_print_error(localization_select(language,
                                          "recovery-status indisponivel",
                                          "recovery-status unavailable",
                                          "recovery-status no disponible"));
    return -1;
#else
    {
        struct x64_kernel_recovery_status status;
        struct net_stack_status net_status;
        int net_rc = 0;
        x64_kernel_recovery_status_get(&status);
        shell_print("maintenance=");
        shell_print(status.maintenance_session ? "yes" : "no");
        shell_print(" degraded=");
        shell_print(status.degraded ? "yes" : "no");
        shell_print(" ram-fallback=");
        shell_print(status.recovery_ram_fallback ? "yes" : "no");
        shell_print(" reason=");
        shell_print(service_boot_policy_reason_label(status.reason));
        shell_newline();

        shell_print("bootstrap=");
        shell_print(service_manager_target_label(status.bootstrap_target));
        shell_print(" requested=");
        shell_print(service_manager_target_label(status.requested_target));
        shell_print(" boot=");
        shell_print(service_manager_target_label(status.boot_target));
        shell_print(" active=");
        shell_print(service_manager_target_label(status.active_target));
        if (ctx && ctx->settings && ctx->settings->service_target[0]) {
            shell_print(" saved=");
            shell_print(ctx->settings->service_target);
        }
        shell_newline();

        shell_print("storage backend=");
        shell_print(x64_storage_runtime_backend_name());
        shell_print(" validated=");
        shell_print(x64_storage_runtime_has_device() ? "yes" : "no");
        shell_print(" persistent=");
        shell_print((ctx && ctx->settings) ? "configured" : "unknown");
        shell_print(" report=/var/log/recovery-boot.txt");
        shell_newline();

        net_rc = net_stack_status(&net_status);
        shell_print("network status=");
        shell_print(net_rc == 0 ? "ok" : "unavailable");
        if (net_rc == 0) {
            shell_print(" runtime=");
            shell_print(net_status.runtime_supported ? "validated" : "missing");
            shell_print(" ready=");
            shell_print(net_status.ready ? "yes" : "no");
            shell_print(" nic=");
            shell_print(net_driver_name(net_status.nic.kind));
        }
        shell_newline();

        shell_print("update catalog=");
        shell_print(status.update_catalog_present ? "present" : "missing");
        shell_print(" channel=");
        shell_print(status.update_channel[0] ? status.update_channel : "stable");
        shell_print(" branch=");
        shell_print(status.update_branch[0] ? status.update_branch : "main");
        shell_print(" available=");
        shell_print(status.update_available ? "yes" : "no");
        shell_print(" stage=");
        shell_print(status.update_stage_ready ? "ready" : "empty");
        shell_print(" pending=");
        shell_print(status.update_pending_activation ? "armed" : "no");
        shell_print(" rc=");
        shell_print_signed_result(status.update_last_result);
        shell_newline();
        shell_print("update versions available=");
        shell_print(status.update_available_version[0] ? status.update_available_version : "-");
        shell_print(" staged=");
        shell_print(status.update_staged_version[0] ? status.update_staged_version : "-");
        shell_newline();

        shell_print("summary: ");
        shell_print(x64_kernel_recovery_reason_summary());
        shell_newline();
    }
    return 0;
#endif
}

static int cmd_recovery_report(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    char *content = NULL;
    size_t content_len = 0;
    (void)ctx;
    if (shell_help_requested(argc, argv)) {
        shell_print(localization_select(
            language,
            "Uso: recovery-report\nExibe o ultimo relatorio persistido do boot/recovery gravado em /var/log/recovery-boot.txt.\n",
            "Usage: recovery-report\nShows the latest persisted boot/recovery report stored in /var/log/recovery-boot.txt.\n",
            "Uso: recovery-report\nMuestra el ultimo informe persistido de boot/recovery guardado en /var/log/recovery-boot.txt.\n"));
        return 0;
    }

    if (shell_read_file("/var/log/recovery-boot.txt", &content, &content_len) != 0 ||
        !content || content_len == 0) {
        shell_print_error(localization_select(
            language,
            "nenhum relatorio persistido de recovery foi encontrado em /var/log/recovery-boot.txt",
            "no persisted recovery report was found in /var/log/recovery-boot.txt",
            "no se encontro ningun informe persistido de recovery en /var/log/recovery-boot.txt"));
        return -1;
    }

    shell_paginate_content(content);
    kfree(content);
    return 0;
}

static int cmd_recovery_history(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    char *content = NULL;
    size_t content_len = 0;
    (void)ctx;
    if (shell_help_requested(argc, argv)) {
        shell_print(localization_select(
            language,
            "Uso: recovery-history\nExibe o historico persistido de eventos de boot/recovery gravado em /var/log/recovery-history.log.\n",
            "Usage: recovery-history\nShows the persisted history of boot/recovery events stored in /var/log/recovery-history.log.\n",
            "Uso: recovery-history\nMuestra el historial persistido de eventos de boot/recovery guardado en /var/log/recovery-history.log.\n"));
        return 0;
    }

    if (shell_read_file("/var/log/recovery-history.log", &content, &content_len) != 0 ||
        !content || content_len == 0) {
        shell_print_error(localization_select(
            language,
            "nenhum historico persistido de recovery foi encontrado em /var/log/recovery-history.log",
            "no persisted recovery history was found in /var/log/recovery-history.log",
            "no se encontro ningun historial persistido de recovery en /var/log/recovery-history.log"));
        return -1;
    }

    shell_paginate_content(content);
    kfree(content);
    return 0;
}

static int cmd_recovery_storage(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    (void)ctx;
    if (shell_help_requested(argc, argv)) {
        shell_print(localization_select(
            language,
            "Uso: recovery-storage\nMostra o estado do runtime de storage, do VFS raiz e dos caminhos persistentes usados pela recuperacao.\n",
            "Usage: recovery-storage\nShows the storage runtime, root VFS and persistent paths used by recovery.\n",
            "Uso: recovery-storage\nMuestra el runtime de almacenamiento, el VFS raiz y las rutas persistentes usadas por la recuperacion.\n"));
        return 0;
    }
#if !defined(__x86_64__)
    shell_print_error(localization_select(language,
                                          "recovery-storage indisponivel",
                                          "recovery-storage unavailable",
                                          "recovery-storage no disponible"));
    return -1;
#else
    {
        struct x64_kernel_recovery_status status;
        struct super_block *root = vfs_root();
        struct vfs_stat config_stat;
        struct vfs_stat userdb_stat;
        struct capyfs_check_report fs_report;
        int fs_report_ok = 0;
        int config_ok = 0;
        int userdb_ok = 0;
        x64_kernel_recovery_status_get(&status);
        config_ok = vfs_stat_path("/system/config.ini", &config_stat) == 0;
        userdb_ok = vfs_stat_path(USER_DB_PATH, &userdb_stat) == 0;
        fs_report_ok = shell_recovery_capyfs_check(&fs_report) == 0;

        shell_print_bool_flag("fs.ready=", status.shell_fs_ready);
        shell_print(" ");
        shell_print_bool_flag("persistent=", status.persistent_storage);
        shell_print(" ");
        shell_print_bool_flag("ram-fallback=", status.recovery_ram_fallback);
        shell_print(" ");
        shell_print_bool_flag("validated-storage=", x64_storage_runtime_has_device());
        shell_newline();

        shell_print("backend=");
        shell_print(x64_storage_runtime_backend_name());
        shell_print(" firmware=");
        shell_print(x64_storage_runtime_uses_firmware() ? "yes" : "no");
        shell_print(" native-candidate=");
        shell_print(x64_storage_runtime_native_candidate_name());
        shell_newline();

        shell_print("data-path=");
        shell_print(x64_storage_runtime_data_path());
        shell_print(" native-data-path=");
        shell_print(x64_storage_runtime_native_data_path());
        shell_newline();

        shell_print("root-mounted=");
        shell_print((root && root->bdev) ? "yes" : "no");
        shell_newline();

        shell_print("capyfs=");
        if (fs_report_ok) {
            shell_print(capyfs_check_result_label(fs_report.result));
            shell_print(" reserved=");
            shell_print_number(fs_report.reserved_blocks_expected);
            shell_print(" root-entries=");
            shell_print_number(fs_report.root_entries);
        } else {
            shell_print("unavailable");
        }
        shell_newline();

        shell_print_path_status("/");
        shell_print_path_status("/system");
        shell_print_path_status("/etc");
        shell_print_path_status("/var/log");
        shell_print_path_status("/system/config.ini");
        shell_print_path_status(USER_DB_PATH);

        shell_print("remediation: ");
        if (!status.shell_fs_ready) {
            shell_print(localization_select(
                language,
                "o runtime do shell ainda nao montou um VFS confiavel; reinicie pela ISO e valide a chave do volume.",
                "the shell runtime has not mounted a trustworthy VFS yet; boot from the ISO and validate the volume key.",
                "el runtime del shell aun no monto un VFS confiable; arranca desde la ISO y valida la clave del volumen."));
        } else if (status.recovery_ram_fallback) {
            shell_print(localization_select(
                language,
                "a recuperacao esta em RAM temporaria; o volume persistente nao abriu. Corrija a chave/volume pela ISO antes de promover targets permanentes.",
                "recovery is running on temporary RAM storage; the persistent volume did not open. Fix the key/volume from the ISO before promoting permanent targets.",
                "la recuperacion se esta ejecutando sobre RAM temporal; el volumen persistente no se abrio. Corrige la clave/el volumen desde la ISO antes de promover objetivos permanentes."));
        } else if (!fs_report_ok || fs_report.result != CAPYFS_CHECK_OK) {
            shell_print(localization_select(
                language,
                "o volume abriu, mas a estrutura CAPYFS esta inconsistente; use recovery-storage-check e depois recupere via ISO antes de tentar repair/resume.",
                "the volume mounted, but the CAPYFS structure is inconsistent; use recovery-storage-check and then recover from the ISO before trying repair/resume.",
                "el volumen se monto, pero la estructura CAPYFS es inconsistente; usa recovery-storage-check y luego recupera desde la ISO antes de intentar repair/resume."));
        } else if (!status.persistent_storage || !x64_storage_runtime_has_device()) {
            shell_print(localization_select(
                language,
                "o volume persistente ainda nao esta validado; prefira VMware com storage AHCI/NVMe e confirme a presenca do volume DATA.",
                "persistent storage is not validated yet; prefer VMware with AHCI/NVMe storage and confirm the DATA volume is present.",
                "el almacenamiento persistente aun no esta validado; prefiere VMware con almacenamiento AHCI/NVMe y confirma la presencia del volumen DATA."));
        } else if (!config_ok || !userdb_ok || userdb_stat.size == 0) {
            shell_print(localization_select(
                language,
                "o volume persistente abriu, mas a base estrutural ainda esta incompleta; use recovery-storage-repair para reconstruir config.ini, diretórios criticos e, se preciso, resetar o admin.",
                "the persistent volume mounted, but the structural base is still incomplete; use recovery-storage-repair to rebuild config.ini, critical directories and, if needed, reset admin.",
                "el volumen persistente se monto, pero la base estructural aun esta incompleta; usa recovery-storage-repair para reconstruir config.ini, directorios criticos y, si hace falta, restablecer admin."));
        } else {
            shell_print(localization_select(
                language,
                "os prerequisitos de storage parecem saudaveis para tentar recovery-login/recovery-resume; se quiser regravar a base persistente, use recovery-storage-repair.",
                "storage prerequisites look healthy enough to try recovery-login/recovery-resume; if you want to rewrite the persistent base, use recovery-storage-repair.",
                "los prerequisitos de almacenamiento parecen saludables para intentar recovery-login/recovery-resume; si quieres regrabar la base persistente, usa recovery-storage-repair."));
        }
        shell_newline();
    }
    return 0;
#endif
}

static int cmd_recovery_storage_check(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    (void)ctx;
    if (shell_help_requested(argc, argv)) {
        shell_print(localization_select(
            language,
            "Uso: recovery-storage-check\nExecuta uma verificacao estrutural do CAPYFS montado e classifica superbloco, layout, bitmap, inode raiz e diretorio raiz.\n",
            "Usage: recovery-storage-check\nRuns a structural verification of the mounted CAPYFS and classifies superblock, layout, bitmap, root inode and root directory.\n",
            "Uso: recovery-storage-check\nEjecuta una verificacion estructural del CAPYFS montado y clasifica superbloque, layout, bitmap, inode raiz y directorio raiz.\n"));
        return 0;
    }
#if !defined(__x86_64__)
    shell_print_error(localization_select(language,
                                          "recovery-storage-check indisponivel",
                                          "recovery-storage-check unavailable",
                                          "recovery-storage-check no disponible"));
    return -1;
#else
    {
        struct x64_kernel_recovery_status status;
        struct capyfs_check_report report;
        int rc = 0;

        x64_kernel_recovery_status_get(&status);
        if (!status.shell_fs_ready) {
            shell_print_error(localization_select(
                language,
                "o runtime de storage ainda nao montou um VFS legivel",
                "the storage runtime has not mounted a readable VFS yet",
                "el runtime de almacenamiento aun no monto un VFS legible"));
            return -1;
        }

        rc = shell_recovery_capyfs_check(&report);
        if (rc != 0) {
            shell_print_error(localization_select(
                language,
                "nao foi possivel inspecionar o dispositivo raiz atual",
                "could not inspect the current root device",
                "no fue posible inspeccionar el dispositivo raiz actual"));
            return -1;
        }

        shell_print("capyfs=");
        shell_print(capyfs_check_result_label(report.result));
        shell_print(" super.magic=");
        shell_print_number(report.super.magic);
        shell_print(" version=");
        shell_print_number(report.super.version);
        shell_newline();

        shell_print("layout blocks=");
        shell_print_number(report.super.block_count);
        shell_print(" inodes=");
        shell_print_number(report.super.inode_count);
        shell_print(" data-start=");
        shell_print_number(report.super.data_start);
        shell_print(" reserved=");
        shell_print_number(report.reserved_blocks_expected);
        shell_newline();

        shell_print("root refs=");
        shell_print_number(report.root_referenced_blocks);
        shell_print(" entries=");
        shell_print_number(report.root_entries);
        shell_print(" detail=");
        shell_print_number(report.detail_primary);
        shell_print(":");
        shell_print_number(report.detail_secondary);
        shell_newline();

        shell_print("action: ");
        if (status.recovery_ram_fallback) {
            shell_print(localization_select(
                language,
                "voce esta sobre RAM temporaria; esse check nao representa o volume persistente quebrado. Corrija o disco/chave via ISO.",
                "you are running on temporary RAM; this check does not represent the broken persistent volume. Fix the disk/key from the ISO.",
                "estas ejecutando sobre RAM temporal; esta verificacion no representa el volumen persistente roto. Corrige el disco/la clave desde la ISO."));
        } else if (report.result == CAPYFS_CHECK_OK) {
            shell_print(localization_select(
                language,
                "estrutura CAPYFS consistente; se faltarem arquivos base, use recovery-storage-repair.",
                "CAPYFS structure looks consistent; if base files are missing, use recovery-storage-repair.",
                "la estructura CAPYFS es consistente; si faltan archivos base, usa recovery-storage-repair."));
        } else {
            shell_print(localization_select(
                language,
                "estrutura CAPYFS inconsistente; nao tente promover targets persistentes. Recupere o volume via ISO/backup antes de sair da manutencao.",
                "CAPYFS structure is inconsistent; do not promote persistent targets. Recover the volume from the ISO/backup before leaving maintenance.",
                "la estructura CAPYFS es inconsistente; no promuevas objetivos persistentes. Recupera el volumen desde la ISO/respaldo antes de salir de mantenimiento."));
        }
        shell_newline();
        return report.result == CAPYFS_CHECK_OK ? 0 : -1;
    }
#endif
}

static int cmd_recovery_network(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    (void)ctx;
    if (shell_help_requested(argc, argv)) {
        shell_print(localization_select(
            language,
            "Uso: recovery-network\nMostra o estado detalhado da rede durante a recuperacao e aponta a remediacao minima para promover o target de rede.\n",
            "Usage: recovery-network\nShows detailed network state during recovery and points to the minimum remediation needed to promote the network target.\n",
            "Uso: recovery-network\nMuestra el estado detallado de la red durante la recuperacion e indica la remediacion minima para promover el objetivo de red.\n"));
        return 0;
    }
#if !defined(__x86_64__)
    shell_print_error(localization_select(language,
                                          "recovery-network indisponivel",
                                          "recovery-network unavailable",
                                          "recovery-network no disponible"));
    return -1;
#else
    {
        struct net_stack_status st;
        char ip[16], mask[16], gw[16], dns[16];
        if (net_stack_status(&st) != 0) {
            shell_print_error(localization_select(
                language,
                "estado da rede indisponivel no runtime atual",
                "network state unavailable in the current runtime",
                "estado de red no disponible en el runtime actual"));
            return -1;
        }

        net_ipv4_format(st.ipv4.addr, ip);
        net_ipv4_format(st.ipv4.mask, mask);
        net_ipv4_format(st.ipv4.gateway, gw);
        net_ipv4_format(st.ipv4.dns, dns);

        shell_print("driver=");
        shell_print(net_driver_name(st.nic.kind));
        shell_print(" detected=");
        shell_print(st.nic.found ? "yes" : "no");
        shell_print(" runtime=");
        shell_print(st.runtime_supported ? "validated" : "missing");
        shell_print(" ready=");
        shell_print(st.ready ? "yes" : "no");
        shell_newline();

        if (st.nic.found) {
            shell_print("pci=");
            shell_print_number(st.nic.bus);
            shell_print(":");
            shell_print_number(st.nic.device);
            shell_print(".");
            shell_print_number(st.nic.function);
            shell_print(" vendor=");
            shell_print_number(st.nic.vendor_id);
            shell_print(" device=");
            shell_print_number(st.nic.device_id);
            shell_newline();
        }

        shell_print("ipv4=");
        shell_print(ip);
        shell_print(" mask=");
        shell_print(mask);
        shell_print(" gw=");
        shell_print(gw);
        shell_print(" dns=");
        shell_print(dns);
        shell_newline();

        shell_print("tx=");
        shell_print_number((uint32_t)st.stats.frames_tx);
        shell_print(" rx=");
        shell_print_number((uint32_t)st.stats.frames_rx);
        shell_print(" drop=");
        shell_print_number((uint32_t)st.stats.frames_drop);
        shell_print(" arp=");
        shell_print_number(st.arp_entries);
        shell_newline();

        shell_print("remediation: ");
        if (!st.nic.found) {
            shell_print(localization_select(
                language,
                "nenhuma NIC foi detectada; confirme que a VM possui um adaptador de rede anexado.",
                "no NIC was detected; confirm that the VM has a network adapter attached.",
                "no se detecto ninguna NIC; confirma que la VM tenga un adaptador de red conectado."));
        } else if (!st.runtime_supported && st.nic.kind == NET_NIC_KIND_VMXNET3) {
            shell_print(localization_select(
                language,
                "VMXNET3 foi detectado sem driver validado; troque a VM para E1000 antes de promover o target de rede.",
                "VMXNET3 was detected without a validated driver; switch the VM to E1000 before promoting the network target.",
                "VMXNET3 fue detectado sin driver validado; cambia la VM a E1000 antes de promover el objetivo de red."));
        } else if (!st.runtime_supported) {
            shell_print(localization_select(
                language,
                "a NIC atual nao possui driver validado; ajuste a VM para um hardware suportado antes de promover a rede.",
                "the current NIC does not have a validated driver; adjust the VM to supported hardware before promoting networking.",
                "la NIC actual no tiene un driver validado; ajusta la VM a hardware soportado antes de promover la red."));
        } else if (!st.ready) {
            shell_print(localization_select(
                language,
                "o runtime de rede existe, mas ainda nao esta pronto; confira cabos virtuais, configuracao IPv4 e tente net-refresh/net-status.",
                "the network runtime exists, but it is not ready yet; check virtual cabling, IPv4 configuration and try net-refresh/net-status.",
                "el runtime de red existe, pero aun no esta listo; revisa el cableado virtual, la configuracion IPv4 e intenta net-refresh/net-status."));
        } else {
            shell_print(localization_select(
                language,
                "os prerequisitos de rede parecem saudaveis para tentar recovery-resume network/full.",
                "network prerequisites look healthy enough to try recovery-resume network/full.",
                "los prerequisitos de red parecen saludables para intentar recovery-resume network/full."));
        }
        shell_newline();
    }
    return 0;
#endif
}

static struct shell_command g_system_info_commands[17];
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
    g_system_info_commands[8].name = "job-status";
    g_system_info_commands[8].handler = cmd_job_status;
    g_system_info_commands[9].name = "update-status";
    g_system_info_commands[9].handler = cmd_update_status;
    g_system_info_commands[10].name = "update-history";
    g_system_info_commands[10].handler = cmd_update_history;
    g_system_info_commands[11].name = "recovery-status";
    g_system_info_commands[11].handler = cmd_recovery_status;
    g_system_info_commands[12].name = "recovery-report";
    g_system_info_commands[12].handler = cmd_recovery_report;
    g_system_info_commands[13].name = "recovery-history";
    g_system_info_commands[13].handler = cmd_recovery_history;
    g_system_info_commands[14].name = "recovery-storage";
    g_system_info_commands[14].handler = cmd_recovery_storage;
    g_system_info_commands[15].name = "recovery-network";
    g_system_info_commands[15].handler = cmd_recovery_network;
    g_system_info_commands[16].name = "recovery-storage-check";
    g_system_info_commands[16].handler = cmd_recovery_storage_check;
    g_system_info_commands_initialized = 1;
}

const struct shell_command *shell_commands_system_info(size_t *count) {
    init_system_info_commands();
    if (count) {
        *count = 17;
    }
    return g_system_info_commands;
}
