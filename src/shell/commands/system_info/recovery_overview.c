#include "internal/system_info_internal.h"

int cmd_recovery_status(struct shell_context *ctx, int argc, char **argv) {
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

int cmd_recovery_report(struct shell_context *ctx, int argc, char **argv) {
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

int cmd_recovery_history(struct shell_context *ctx, int argc, char **argv) {
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

