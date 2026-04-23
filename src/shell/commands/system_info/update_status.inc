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
    shell_print("remote=");
    shell_print(status.remote_manifest_url[0] ? status.remote_manifest_url : "-");
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

