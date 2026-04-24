#include "arch/x86_64/kernel_volume_runtime.h"
#include "auth/session.h"

static int cmd_job_run(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  struct system_work_status work;
  int work_id = -1;
  (void)ctx;

  if (shell_help_requested(argc, argv) || argc < 2) {
    shell_print(localization_select(
        language,
        "Uso: job-run <nome>\nAgenda um job interno do kernel/work queue para execucao imediata no proximo tick.\n",
        "Usage: job-run <name>\nSchedules an internal kernel/work queue job for immediate execution on the next tick.\n",
        "Uso: job-run <nombre>\nAgenda un job interno del kernel/work queue para ejecucion inmediata en el siguiente tick.\n"));
    return argc < 2 ? -1 : 0;
  }

  work_id = work_queue_find(argv[1], &work);
  if (work_id < 0) {
    shell_print_error(localization_select(language,
                                          "job interno desconhecido",
                                          "unknown internal job",
                                          "job interno desconocido"));
    return -1;
  }
  if (work_queue_schedule_now((uint32_t)work_id, pit_ticks()) != 0) {
    shell_print_error(localization_select(language,
                                          "falha ao agendar job interno",
                                          "failed to schedule internal job",
                                          "fallo al programar el job interno"));
    return -1;
  }

  shell_print_ok(localization_select(language,
                                     "job agendado para execucao imediata",
                                     "job scheduled for immediate execution",
                                     "job programado para ejecucion inmediata"));
  shell_print(work.name);
  shell_newline();
  return 0;
}

static int update_runtime_writer(const char *path, const char *text) {
  struct session_context *previous_session = session_active();
  int rc = 0;

  session_set_active(NULL);
  rc = x64_kernel_volume_runtime_write_text_file(path, text);
  session_set_active(previous_session);
  return rc;
}

static int refresh_update_agent_service_state(int rc,
                                              struct system_update_status *status) {
  struct system_update_status local_status;

  if (!status) {
    status = &local_status;
  }
  update_agent_status_get(status);
  if (rc < 0) {
    (void)service_manager_set_state(SYSTEM_SERVICE_UPDATE_AGENT,
                                    SYSTEM_SERVICE_STATE_DEGRADED, rc,
                                    status->summary);
  } else {
    (void)service_manager_set_state(SYSTEM_SERVICE_UPDATE_AGENT,
                                    SYSTEM_SERVICE_STATE_READY,
                                    status->last_result, status->summary);
  }
  return rc;
}

static const char *update_channel_name_or_null(const char *value) {
  if (!value || !value[0]) {
    return NULL;
  }
  if (shell_string_equal(value, "stable")) {
    return "stable";
  }
  if (shell_string_equal(value, "develop")) {
    return "develop";
  }
  return NULL;
}

static int cmd_update_check(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  struct system_update_status status;
  int rc = 0;
  (void)ctx;

  if (shell_help_requested(argc, argv)) {
    shell_print(localization_select(
        language,
        "Uso: update-check\nExecuta imediatamente uma leitura do catalogo local de atualizacoes e atualiza o estado do update-agent.\n",
        "Usage: update-check\nImmediately reads the local update catalog and refreshes the update-agent state.\n",
        "Uso: update-check\nEjecuta inmediatamente una lectura del catalogo local de actualizaciones y actualiza el estado del update-agent.\n"));
    return 0;
  }

  rc = refresh_update_agent_service_state(update_agent_poll(), &status);
  if (rc < 0) {
    shell_print_error(localization_select(language,
                                          "catalogo local de atualizacao invalido",
                                          "local update catalog is invalid",
                                          "el catalogo local de actualizacion es invalido"));
    return -1;
  }

  shell_print_ok(localization_select(language,
                                     "catalogo local de atualizacao verificado",
                                     "local update catalog checked",
                                     "catalogo local de actualizacion verificado"));
  shell_print(status.summary);
  shell_newline();
  update_history_append_event("check", &status);
  return 0;
}

static int cmd_update_stage(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  struct system_update_status status;
  int rc = 0;
  (void)ctx;

  if (shell_help_requested(argc, argv)) {
    shell_print(localization_select(
        language,
        "Uso: update-stage\nCopia o manifesto cacheado mais recente para a area persistente de staging em /system/update/staged/.\n",
        "Usage: update-stage\nCopies the newest cached manifest into the persistent staging area under /system/update/staged/.\n",
        "Uso: update-stage\nCopia el manifiesto cacheado mas reciente al area persistente de staging en /system/update/staged/.\n"));
    return 0;
  }

  update_agent_set_writer(update_runtime_writer);
  rc = refresh_update_agent_service_state(update_agent_stage_latest(), &status);
  update_agent_set_writer(NULL);
  if (rc < 0) {
    shell_print_error(localization_select(language,
                                          "nao foi possivel preparar o staging local de atualizacao",
                                          "could not prepare the local update staging area",
                                          "no fue posible preparar el area local de staging de actualizacion"));
    shell_print(status.summary);
    shell_newline();
    return -1;
  }

  shell_print_ok(localization_select(language,
                                     "atualizacao preparada em staging",
                                     "update prepared in staging",
                                     "actualizacion preparada en staging"));
  shell_print(status.staged_version[0] ? status.staged_version : status.summary);
  shell_newline();
  update_history_append_event("stage", &status);
  return 0;
}

static int cmd_update_arm(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  struct system_update_status status;
  int enable = 1;
  int rc = 0;
  (void)ctx;

  if (shell_help_requested(argc, argv)) {
    shell_print(localization_select(
        language,
        "Uso: update-arm [on|off]\nArma ou desarma a ativacao pendente do update staged sem remover o manifesto preparado.\n",
        "Usage: update-arm [on|off]\nArms or disarms pending activation for the staged update without removing the prepared manifest.\n",
        "Uso: update-arm [on|off]\nArma o desarma la activacion pendiente del update staged sin eliminar el manifiesto preparado.\n"));
    return 0;
  }

  if (argc >= 2) {
    if (shell_string_equal(argv[1], "off") || shell_string_equal(argv[1], "0") ||
        shell_string_equal(argv[1], "disable")) {
      enable = 0;
    } else if (!(shell_string_equal(argv[1], "on") ||
                 shell_string_equal(argv[1], "1") ||
                 shell_string_equal(argv[1], "enable"))) {
      shell_print_error(localization_select(language,
                                            "uso invalido",
                                            "invalid usage",
                                            "uso invalido"));
      shell_suggest_help("update-arm");
      return -1;
    }
  }

  update_agent_set_writer(update_runtime_writer);
  rc = refresh_update_agent_service_state(
      update_agent_set_pending_activation(enable), &status);
  update_agent_set_writer(NULL);
  if (rc < 0) {
    shell_print_error(localization_select(language,
                                          "nao foi possivel atualizar o estado pendente do staging",
                                          "could not update the pending staged state",
                                          "no fue posible actualizar el estado pendiente del staging"));
    shell_print(status.summary);
    shell_newline();
    return -1;
  }

  shell_print_ok(enable ? localization_select(language,
                                              "ativacao pendente armada",
                                              "pending activation armed",
                                              "activacion pendiente armada")
                        : localization_select(language,
                                              "ativacao pendente removida",
                                              "pending activation cleared",
                                              "activacion pendiente eliminada"));
  shell_print(status.summary);
  shell_newline();
  update_history_append_event(enable ? "arm" : "disarm", &status);
  return 0;
}

static int cmd_update_clear(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  struct system_update_status status;
  int rc = 0;
  (void)ctx;

  if (shell_help_requested(argc, argv)) {
    shell_print(localization_select(
        language,
        "Uso: update-clear\nRemove o manifesto staged e limpa o estado persistente de ativacao pendente.\n",
        "Usage: update-clear\nRemoves the staged manifest and clears the persistent pending-activation state.\n",
        "Uso: update-clear\nElimina el manifiesto staged y limpia el estado persistente de activacion pendiente.\n"));
    return 0;
  }

  rc = refresh_update_agent_service_state(update_agent_clear_stage(), &status);
  if (rc < 0) {
    shell_print_error(localization_select(language,
                                          "nao foi possivel limpar o staging local",
                                          "could not clear the local staging area",
                                          "no fue posible limpiar el area local de staging"));
    shell_print(status.summary);
    shell_newline();
    return -1;
  }

  shell_print_ok(localization_select(language,
                                     "staging local limpo",
                                     "local staging cleared",
                                     "staging local limpiado"));
  shell_print(status.summary);
  shell_newline();
  update_history_append_event("clear", &status);
  return 0;
}

static int cmd_update_import_manifest(struct shell_context *ctx, int argc,
                                      char **argv) {
  const char *language = shell_current_language();
  struct system_update_status status;
  char resolved_path[SHELL_PATH_BUFFER];
  int rc = 0;

  if (shell_help_requested(argc, argv) || argc < 2) {
    shell_print(localization_select(
        language,
        "Uso: update-import-manifest <caminho>\nImporta um manifesto externo para o catalogo local persistente e valida se ele combina com a trilha de update selecionada.\n",
        "Usage: update-import-manifest <path>\nImports an external manifest into the persistent local catalog and validates that it matches the selected update track.\n",
        "Uso: update-import-manifest <ruta>\nImporta un manifiesto externo al catalogo local persistente y valida que coincida con la ruta de update seleccionada.\n"));
    return argc < 2 ? -1 : 0;
  }

  if (shell_resolve_path(ctx, argv[1], resolved_path, sizeof(resolved_path)) !=
      0) {
    shell_print_error(localization_select(language,
                                          "caminho do manifesto invalido",
                                          "invalid manifest path",
                                          "ruta de manifiesto invalida"));
    return -1;
  }

  update_agent_set_writer(update_runtime_writer);
  rc = refresh_update_agent_service_state(
      update_agent_import_manifest_path(resolved_path), &status);
  update_agent_set_writer(NULL);
  if (rc < 0) {
    shell_print_error(localization_select(
        language,
        "nao foi possivel importar o manifesto para o catalogo local",
        "could not import the manifest into the local catalog",
        "no fue posible importar el manifiesto al catalogo local"));
    shell_print(status.summary);
    shell_newline();
    return -1;
  }

  shell_print_ok(localization_select(language,
                                     "manifesto importado para o catalogo local",
                                     "manifest imported into the local catalog",
                                     "manifiesto importado al catalogo local"));
  shell_print("available=");
  shell_print(status.available_version[0] ? status.available_version : "-");
  shell_print(" channel=");
  shell_print(status.channel[0] ? status.channel : "stable");
  shell_print(" branch=");
  shell_print(status.branch[0] ? status.branch : "main");
  shell_newline();
  update_history_append_event("import", &status);
  return 0;
}

static int cmd_update_channel(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  const char *channel = NULL;
  struct system_update_status status;
  struct system_settings *mutable_settings = NULL;
  int rc = 0;

  if (shell_help_requested(argc, argv)) {
    shell_print(localization_select(
        language,
        "Uso: update-channel [list|show|stable|develop]\nSeleciona a trilha de atualizacao estavel (main) ou em desenvolvimento (develop) e persiste isso no sistema.\n",
        "Usage: update-channel [list|show|stable|develop]\nSelects the stable (main) or development (develop) update track and persists it in the system.\n",
        "Uso: update-channel [list|show|stable|develop]\nSelecciona la rama de actualizacion estable (main) o de desarrollo (develop) y la persiste en el sistema.\n"));
    return 0;
  }

  if (argc < 2 || shell_string_equal(argv[1], "show")) {
    rc = refresh_update_agent_service_state(update_agent_poll(), &status);
    if (rc < 0) {
      shell_print_error(localization_select(language,
                                            "estado atual de update invalido",
                                            "current update state is invalid",
                                            "el estado actual del update es invalido"));
      return -1;
    }
    shell_print("channel=");
    shell_print(status.channel[0] ? status.channel : "stable");
    shell_print(" branch=");
    shell_print(status.branch[0] ? status.branch : "main");
    shell_print(" source=");
    shell_print(status.source[0] ? status.source : "-");
    shell_newline();
    shell_print("remote=");
    shell_print(status.remote_manifest_url[0] ? status.remote_manifest_url : "-");
    shell_newline();
    return 0;
  }

  if (shell_string_equal(argv[1], "list")) {
    shell_print(localization_select(
        language,
        "stable  -> branch main (atualizacoes estaveis)\n"
        "develop -> branch develop (atualizacoes em desenvolvimento)\n",
        "stable  -> branch main (stable updates)\n"
        "develop -> branch develop (development updates)\n",
        "stable  -> branch main (actualizaciones estables)\n"
        "develop -> branch develop (actualizaciones en desarrollo)\n"));
    return 0;
  }

  channel = update_channel_name_or_null(argv[1]);
  if (!channel) {
    shell_print_error(localization_select(language,
                                          "canal de update invalido",
                                          "invalid update channel",
                                          "canal de update invalido"));
    shell_suggest_help("update-channel");
    return -1;
  }

  if (ctx && ctx->settings) {
    mutable_settings = (struct system_settings *)ctx->settings;
    shell_copy(mutable_settings->update_channel,
               sizeof(mutable_settings->update_channel), channel);
  }

  if (system_save_update_channel(channel) != 0) {
    shell_print_ok(localization_select(language,
                                       "canal de update alterado apenas na sessao atual",
                                       "update channel changed only for the current session",
                                       "canal de update cambiado solo para la sesion actual"));
    shell_print(localization_text_for(language, LOC_TEXT_CONFIG_SAVE_WARNING));
    return 0;
  }

  rc = refresh_update_agent_service_state(update_agent_poll(), &status);
  if (rc < 0) {
    shell_print_error(localization_select(language,
                                          "canal salvo, mas o catalogo local ficou inconsistente",
                                          "channel saved, but the local catalog became inconsistent",
                                          "canal guardado, pero el catalogo local quedo inconsistente"));
    return -1;
  }

  shell_print_ok(localization_select(language,
                                     "trilha de update atualizada",
                                     "update track updated",
                                     "ruta de update actualizada"));
  shell_print("channel=");
  shell_print(status.channel);
  shell_print(" branch=");
  shell_print(status.branch);
  shell_newline();
  shell_print("remote=");
  shell_print(status.remote_manifest_url[0] ? status.remote_manifest_url : "-");
  shell_newline();
  update_history_append_event("channel", &status);
  return 0;
}

